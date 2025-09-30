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
 * List of smart pointers, with fast append and prepend.
 * Behaves predictably if modified during iteration.
 *
 * Items are removed by setting pointers in the list to null. The list
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
 * 3. Iterators automatically skip over null pointers.
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
template<typename T, typename Ptr = refptr<T>>
class reflist : public refcounted<reflist<T, Ptr>>
{
public:
	friend ref_base<reflist>;

	class iter
	{
	public:
		friend reflist;

		// std::iterator_traits
		using iterator_category = std::bidirectional_iterator_tag;
		using value_type = T;
		using difference_type = int;
		using pointer = T *;
		using reference = T &;

		explicit operator bool() { return (bool)m_val; }

		T *get() { return m_val.get(); }

		// iterator validity is usually checked in a loop condition,
		// so these are considered safe and allowed for that usage
		T &operator*() { return *get(); }
		T *operator->() { return get(); }

		bool operator==(const iter &it) {
			// can't compare different lists or directions
			assert(m_list.get() == it.m_list.get()
				&& m_dir == it.m_dir);
			// intentionally comparing only index (not start or end)
			return m_idx == it.m_idx;
		}

		bool operator!=(const iter &it) { return !operator==(it); }

		iter &operator++() {
			m_val.reset();
			m_idx += m_dir;
			find_valid(m_dir);
			return *this;
		}

		iter &operator--() {
			m_val.reset();
			m_idx -= m_dir;
			find_valid(m_dir);
			return *this;
		}

		Ptr remove() {
			if (m_val) {
				m_val.reset(); // release ref
				return std::move(m_list->at(m_idx));
			} else {
				return Ptr();
			}
		}

	private:
		ref<reflist> m_list;
		int m_start, m_end, m_idx, m_dir;
		refptr<T> m_val; // not ownptr (if Ptr = ownptr)

		iter(reflist &list, int idx, int dir)
			: m_list(list), m_start(list.start_idx()),
				m_end(list.end_idx()), m_idx(idx), m_dir(dir)
		{
			find_valid(m_dir);
		}

		void find_valid(int dir) {
			if (dir > 0) {
				m_idx = std::max(m_idx, m_start);
				for (; m_idx < m_end; m_idx++) {
					m_val.reset(m_list->at(m_idx).get());
					if (m_val) {
						return;
					}
				}
				// use INT_MAX - 1 so all past-end iters
				// are equal (while allowing one increment
				// without overflow)
				m_idx = INT_MAX - 1;
			} else {
				m_idx = std::min(m_idx, m_end - 1);
				for (; m_idx >= m_start; m_idx--) {
					m_val.reset(m_list->at(m_idx).get());
					if (m_val) {
						return;
					}
				}
				// use INT_MIN + 1 so all pre-start iters
				// are equal (while allowing one decrement
				// without overflow)
				m_idx = INT_MIN + 1;
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
		ref<reflist> m_list;

		reverse_view(reflist &list) : m_list(list) {}
	};

	reflist() {}

	// this produces a compacted copy (nulls omitted)
	reflist(const reflist &list) { append_all(list); }

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

	iter begin() { return iter(*this, start_idx(), 1); }
	iter end() { return iter(*this, end_idx(), 1); }

	// std::reverse_iterator is not used because its offset-by-one
	// semantics could behave unpredictably when combined with the
	// null-skipping behavior of reflist::iter (specifically, removals
	// could unexpectedly change the item an iterator points to).
	iter rbegin() { return iter(*this, end_idx() - 1, -1); }
	iter rend() { return iter(*this, start_idx() - 1, -1); }

	reverse_view reversed() { return reverse_view(*this); }

	bool empty() { return !begin(); }
	int size() { return std::distance(begin(), end()); }

	void clear() { *this = reflist(); }

	template<typename P>
	void append(P &&ptr) {
		m_fwd_items.emplace_back(std::forward<P>(ptr));
	}

	void append_all(const reflist &list) {
		auto &list_ = const_cast<reflist &>(list);
		for (auto it = list_.begin(); it; ++it) {
			m_fwd_items.emplace_back(it.get());
		}
	}

	template<typename P>
	void prepend(P &&ptr) {
		m_rev_items.emplace_back(std::forward<P>(ptr));
	}

	template<typename P>
	bool remove(const P &ptr) {
		return (bool)lab::find_ptr(begin(), ptr).remove();
	}

private:
	std::vector<Ptr> m_fwd_items;
	std::vector<Ptr> m_rev_items; // in reverse order
	int m_cached_size = 0;

	int start_idx() { return -(int)m_rev_items.size(); }
	int end_idx() { return m_fwd_items.size(); }

	Ptr &at(int idx) {
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

/*
 * Variant holding owning (rather than reference-counted) pointers.
 * Guarantees that items in the list cannot accidentally outlive the
 * list. T still needs to inherit refcounted, but not ref_owned.
 */
template<typename T>
using ownlist = reflist<T, ownptr<T>>;

#endif // REFLIST_H
