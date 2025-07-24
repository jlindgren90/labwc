// SPDX-License-Identifier: GPL-2.0-only
#ifndef REFLIST_H
#define REFLIST_H

#include "refptr.h"
#include <assert.h>
#include <limits.h>
#include <algorithm>
#include <iterator>
#include <vector>

/**
 * List of refptrs, with fast append and prepend.
 * Behaves predictably if modified during iteration.
 *
 * Items are removed by setting refptrs in the list to null. The list
 * is automatically compacted to remove nulls when no iterators exist.
 *
 * Iterators behave as follows:
 *
 * 1. Once created, an iterator continues to point to the same item,
 *    even if other items are added to or removed from the list.
 *
 * 2. Each iterator recalls the bounds of the list at the time the
 *    iterator was created, and will not iterate over new items added
 *    to the list since then.
 *
 * 3. Iterators automatically skip over null refptrs.
 *
 * Some limitations:
 *
 * 1. Inserting items into the middle of the list is not directly
 *    supported. (It's possible to emulate insertion by removing and
 *    re-adding all the items after the insertion point.)
 *
 * 2. None of the following may be called while any iterators exist:
 *      - ~reflist()
 *      - operator=()
 *      - clear()
 *
 * 3. Currently, only non-const iterators are provided.
 */
template<typename T>
class reflist : public refcounted<reflist<T>>
{
public:
	friend refptr<reflist>;

	class iter
	{
	public:
		friend reflist;

		// std::iterator_traits
		using iterator_category = std::bidirectional_iterator_tag;
		using value_type = refptr<T>;
		using difference_type = int;
		using pointer = refptr<T> *;
		using reference = refptr<T> &;

		// return refptr by value (not reference) to prevent
		// accidentally modifying the list through the iter
		refptr<T> operator*() { return m_list->at(m_idx); }

		// gives the actual item pointed to (not the refptr)
		T *operator->() { return get(); }

		bool operator==(const iter &it) {
			// can't compare different lists or directions
			assert(m_list == it.m_list && m_dir == it.m_dir);
			// intentionally comparing only index (not start or end)
			return m_idx == it.m_idx;
		}

		bool operator!=(const iter &it) { return !operator==(it); }

		iter &operator++() {
			m_idx = skip_null(m_idx + m_dir, m_dir);
			return *this;
		}

		iter &operator--() {
			m_idx = skip_null(m_idx - m_dir, -m_dir);
			return *this;
		}

		bool valid() { return m_idx >= m_start && m_idx < m_end; }

		T *get() { return m_list->at(m_idx).get(); }

		refptr<T> remove() { return std::move(m_list->at(m_idx)); }

	private:
		refptr<reflist> m_list;
		int m_start, m_end, m_idx, m_dir;

		iter(reflist *list, int idx, int dir)
			: m_list(list), m_start(list->start_idx()),
				m_end(list->end_idx()),
				m_idx(skip_null(idx, dir)),
				m_dir(dir) {}

		int skip_null(int idx, int dir) {
			if (dir > 0) {
				idx = std::max(idx, m_start);
				while (idx < m_end && !m_list->at(idx)) {
					idx++;
				}
				// use INT_MAX - 1 so all past-end iters
				// are equal (while allowing one increment
				// without overflow)
				return (idx < m_end) ? idx : INT_MAX - 1;
			} else {
				idx = std::min(idx, m_end - 1);
				while (idx >= m_start && !m_list->at(idx)) {
					idx--;
				}
				// use INT_MIN + 1 so all pre-start iters
				// are equal (while allowing one decrement
				// without overflow)
				return (idx >= m_start) ? idx : INT_MIN + 1;
			}
		}
	};

	class reverse_view
	{
	public:
		friend reflist;

		iter begin() { return m_list->rbegin(); }
		iter end() { return m_list->rend(); }

	private:
		refptr<reflist> m_list;

		reverse_view(reflist *list) : m_list(list) {}
	};

	reflist() {}

	reflist(const reflist &list) {
		// this produces a compacted copy (nulls omitted)
		append_all(const_cast<reflist &>(list).begin(),
			const_cast<reflist &>(list).end());
	}

	reflist(reflist &&list) {
		assert(list.refcount() == 0);
		m_fwd_items = std::move(list.m_fwd_items);
		m_rev_items = std::move(list.m_rev_items);
		m_cached_size = list.m_cached_size;
		list.m_cached_size = 0;
	}

	reflist &operator=(const reflist &list) {
		return lab::reconstruct(*this, list);
	}

	reflist &operator=(reflist &&list) {
		return lab::reconstruct(*this, std::move(list));
	}

	iter begin() { return iter(this, start_idx(), 1); }
	iter end() { return iter(this, end_idx(), 1); }

	// std::reverse_iterator is not used because its offset-by-one
	// semantics could behave unpredictably when combined with the
	// null-skipping behavior of reflist::iter (specifically, removals
	// could unexpectedly change the item an iterator points to).
	iter rbegin() { return iter(this, end_idx() - 1, -1); }
	iter rend() { return iter(this, start_idx() - 1, -1); }

	reverse_view reversed() { return reverse_view(this); }

	bool empty() { return begin() == end(); }
	int size() { return std::distance(begin(), end()); }

	void clear() { *this = reflist(); }

	template<typename V>
	void append(V &&val) {
		m_fwd_items.emplace_back(std::forward<V>(val));
	}

	template<typename It>
	void append_all(It start, It stop) {
		std::copy(start, stop, std::back_inserter(m_fwd_items));
	}

	template<typename V>
	void prepend(V &&val) {
		m_rev_items.emplace_back(std::forward<V>(val));
	}

	template<typename V>
	bool remove(const V &val) {
		auto it = lab::find(*this, val);
		if (it != end()) {
			it.remove();
			return true;
		}
		return false;
	}

private:
	std::vector<refptr<T>> m_fwd_items;
	std::vector<refptr<T>> m_rev_items; // in reverse order
	int m_cached_size = 0;

	int start_idx() { return -(int)m_rev_items.size(); }
	int end_idx() { return m_fwd_items.size(); }

	refptr<T> &at(int idx) {
		return (idx >= 0) ? m_fwd_items.at(idx)
			: m_rev_items.at(-1 - idx);
	}

	void last_unref() {
		// compact after last iter destroyed, if size changed
		if (end_idx() - start_idx() > m_cached_size) {
			lab::remove(m_fwd_items, nullptr);
			lab::remove(m_rev_items, nullptr);
			m_cached_size = end_idx() - start_idx();
		}
	}
};

#endif // REFLIST_H
