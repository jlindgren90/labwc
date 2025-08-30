// SPDX-License-Identifier: GPL-2.0-only
#ifndef REFPTR_H
#define REFPTR_H

#include "alg.h"
#include <assert.h>
#include <memory>
#include <utility>

/* Generic owning pointer (slightly modified std::unique_ptr) */
template<typename T>
class ownptr : public std::unique_ptr<T>
{
public:
	using value_type = T;

	using std::unique_ptr<T>::unique_ptr;
	using std::unique_ptr<T>::get;
	using std::unique_ptr<T>::reset;

	explicit ownptr(T *ptr) : std::unique_ptr<T>(ptr) {}

	// deleted due to making null dereference too easy
	T &operator*() const = delete;
	T *operator->() const = delete;

	// safe usage pattern to prevent accidental null dereference
	[[nodiscard]] bool check(T *&ptr) { return (bool)(ptr = get()); }

	template<typename... Args>
	T *set_new(Args &&... args) {
		reset(new T(std::forward<Args>(args)...));
		return get();
	}
};

/*
 * Common base for a counting reference (used by ref and refptr).
 *
 * The referenced type needs to inherit the "refcounted" mix-in and
 * implement a last_unref() function, which is called to perform
 * type-specific behavior when the reference count drops to zero.
 *
 * Shared-ownership semantics can be obtained by making last_unref()
 * delete the object, but other behaviors are possible too.
 *
 * It is an error to destroy an object with a non-zero refcount.
 */
template<typename T>
class ref_base
{
public:
	using value_type = T;

	ref_base() {}
	ref_base(const ref_base &r) { reset(r.m_ptr); }
	ref_base(ref_base &&r) : m_ptr(r.m_ptr) { r.m_ptr = nullptr; }
	~ref_base() { reset(); }

	ref_base &operator=(const ref_base &r) {
		return lab::reconstruct(*this, r);
	}

	ref_base &operator=(ref_base &&rp) {
		return lab::reconstruct(*this, std::move(rp));
	}

	T *get() const { return m_ptr; }

	void reset(T *ptr = nullptr) {
		if (ptr) {
			ptr->m_refcount++;
		}
		if (m_ptr) {
			m_ptr->m_refcount--;
			if (!m_ptr->m_refcount) {
				m_ptr->last_unref();
			}
		}
		m_ptr = ptr;
	}

private:
	T *m_ptr = nullptr;
};

/* Restricted intrusive counting reference. Cannot be null. */
template<typename T>
class ref : private ref_base<T>
{
public:
	explicit ref(T &obj) { reset(obj); }

	// inherit copy from ref_base
	ref(const ref &) = default;
	ref &operator=(const ref &) = default;

	// prevent move as it would leave the source null
	ref(ref &&) = delete;
	ref &operator=(ref &&) = delete;

	using ref_base<T>::get;

	void reset(T &obj) { ref_base<T>::reset(&obj); }

	T &operator*() const { return *get(); }
	T *operator->() const { return get(); }
};

template<typename T, typename... Args>
ref<T> make_ref(Args &&... args)
{
	return ref(*new T(std::forward<Args>(args)...));
}

/*
 * Full-featured intrusive reference-counting pointer.
 * Can be null and provides full set of conversion/equality ops.
 */
template<typename T>
class refptr : public ref_base<T>
{
public:
	using ref_base<T>::ref_base;
	using ref_base<T>::get;
	using ref_base<T>::reset;

	explicit refptr(T *ptr) { reset(ptr); }

	// allow conversion from ref (but not vice versa)
	refptr(const ref<T> &r) { reset(r.get()); }

	explicit operator bool() const { return (bool)get(); }

	// safe usage pattern to prevent accidental null dereference
	[[nodiscard]] bool check(T *&ptr) { return (bool)(ptr = get()); }

	bool operator==(T *ptr) const { return get() == ptr; }
	bool operator==(const refptr &rp) const { return get() == rp.get(); }
	bool operator!=(T *ptr) const { return get() != ptr; }
	bool operator!=(const refptr &rp) const { return get() != rp.get(); }
};

template<typename T>
static inline bool operator==(T *ptr, const refptr<T> &rp) {
	return rp.operator==(ptr);
}

template<typename T>
static inline bool operator!=(T *ptr, const refptr<T> &rp) {
	return rp.operator!=(ptr);
}

/* Mix-in for a reference-counted type */
template<typename T>
class refcounted
{
public:
	friend ref_base<T>;

	refcounted() {}
	~refcounted() {
		// make sure all references are gone
		assert(m_refcount == 0);
	}

	// The mix-in itself does not support copy/move, since the refcount
	// is not transferable. A derived class may implement copy/move if
	// desired, but remember that after copying/moving, any existing
	// refptrs will still point to the original object.
	refcounted(const refcounted &) = delete;
	refcounted &operator=(const refcounted &) = delete;

	unsigned refcount() const { return m_refcount; }

	// required to be defined in T:
	// void last_unref();

private:
	unsigned m_refcount = 0;
};

/* Specialization where last_unref() does nothing */
template<typename T>
class ref_guarded : public refcounted<T>
{
public:
	void last_unref() { /* no-op */ }
};

/* Specialization where last_unref() deletes the object */
template<typename T>
class ref_owned : public refcounted<T>
{
public:
	void last_unref() { delete static_cast<T *>(this); }
};

/*
 * Generic intrusive weak pointer.
 *
 * Automatically resets to null when the pointed-to object is deleted.
 * The pointed-to type needs to inherit the "weak_target" mix-in.
 */
template<typename T>
class weakptr
{
public:
	using value_type = T;

	weakptr() {}
	weakptr(const weakptr &wp) { reset(wp.m_ptr); }
	// move constructor omitted (cannot be moved efficiently)
	~weakptr() { reset(); }

	explicit weakptr(T *ptr) { reset(ptr); }
	explicit weakptr(const refptr<T> &rp) { reset(rp.get()); }

	weakptr &operator=(const weakptr &wp) {
		return lab::reconstruct(*this, wp);
	}

	T *get() const { return m_ptr; }

	explicit operator bool() const { return (bool)m_ptr; }

	// safe usage pattern to prevent accidental null dereference
	[[nodiscard]] bool check(T *&ptr) { return (bool)(ptr = get()); }

	bool operator==(T *ptr) const { return m_ptr == ptr; }
	bool operator==(const weakptr &wp) const { return m_ptr == wp.m_ptr; }
	bool operator==(const refptr<T> &rp) const { return m_ptr == rp.get(); }
	bool operator!=(T *ptr) const { return m_ptr != ptr; }
	bool operator!=(const weakptr &wp) const { return m_ptr != wp.m_ptr; }
	bool operator!=(const refptr<T> &rp) const { return m_ptr != rp.get(); }

	void reset(T *ptr = nullptr) {
		if (m_ptr) {
			// remove from linked list
			if (m_ptr->m_weak_head == this) {
				m_ptr->m_weak_head = this->next;
			} else {
				auto prior = m_ptr->m_weak_head;
				while (prior->next != this) {
					prior = prior->next;
				}
				prior->next = this->next;
			}
		}
		m_ptr = ptr;
		if (ptr) {
			// add to linked list
			this->next = ptr->m_weak_head;
			ptr->m_weak_head = this;
		} else {
			this->next = nullptr;
		}
	}

private:
	T *m_ptr = nullptr;
	weakptr<T> *next = nullptr;
};

template<typename T>
static inline bool operator==(T *ptr, const weakptr<T> &wp) {
	return wp.operator==(ptr);
}

template<typename T>
static inline bool operator!=(T *ptr, const weakptr<T> &wp) {
	return wp.operator!=(ptr);
}

template<typename T>
static inline bool operator==(const refptr<T> &rp, const weakptr<T> &wp) {
	return wp.operator==(rp);
}

template<typename T>
static inline bool operator!=(const refptr<T> &rp, const weakptr<T> &wp) {
	return wp.operator!=(rp);
}

/* Mix-in for a weak pointer target type */
template<typename T>
class weak_target
{
public:
	friend weakptr<T>;

	weak_target() {}

	~weak_target() {
		while (m_weak_head) {
			m_weak_head->reset();
		}
	}

	// The mix-in itself does not support copy/move, since the weakptrs
	// would be left dangling. A derived class may implement copy/move
	// if desired, but remember that after copying/moving, any existing
	// weakptrs will still point to the original object.
	weak_target(const weak_target &) = delete;
	weak_target &operator=(const weak_target &) = delete;

private:
	weakptr<T> *m_weak_head = nullptr; // linked list
};

/* Variant of weakptr where reset() also deletes the object */
template<typename T>
class weak_owner : public weakptr<T>
{
public:
	weak_owner() {}
	~weak_owner() { reset(); }

	// copy/move semantics not currently defined
	weak_owner(const weak_owner &) = delete;
	weak_owner &operator=(const weak_owner &) = delete;

	void reset(T *ptr = nullptr) {
		delete weakptr<T>::get();
		weakptr<T>::reset(ptr);
	}
};

#define ASSERT_PTR(ptr, name)     \
	auto name = (ptr).get();  \
	assert(name)

/* Shorthand for use in "if" or (less commonly) "for" conditions */
#define CHECK_PTR(ptr, name) \
	decltype(ptr)::value_type *name; (ptr).check(name)

#define CHECK_PTR_OR_RET(ptr, name)        \
	decltype(ptr)::value_type *name;   \
	do {                               \
		if (!(ptr).check(name)) {  \
			return;            \
		}                          \
	} while (0)

#define CHECK_PTR_OR_RET_VAL(ptr, name, retval)  \
	decltype(ptr)::value_type *name;         \
	do {                                     \
		if (!(ptr).check(name)) {        \
			return (retval);         \
		}                                \
	} while (0)

#endif // REFPTR_H
