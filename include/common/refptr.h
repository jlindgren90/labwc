// SPDX-License-Identifier: GPL-2.0-only
#ifndef REFPTR_H
#define REFPTR_H

#include "alg.h"
#include <assert.h>

/*
 * Generic intrusive reference-counting pointer.
 *
 * The pointed-to type needs to inherit the "refcounted" mix-in and
 * implement a last_unref() function, which is called to perform
 * type-specific behavior when the reference count drops to zero.
 *
 * Shared-ownership semantics can be obtained by making last_unref()
 * delete the object, but other behaviors are possible too.
 *
 * It is an error to destroy an object while refptrs to it exist.
 */
template<typename T>
class refptr
{
public:
	refptr() {}
	refptr(const refptr &rp) { reset(rp.m_ptr); }
	refptr(refptr &&rp) : m_ptr(rp.m_ptr) { rp.m_ptr = nullptr; }
	~refptr() { reset(); }

	explicit refptr(T *ptr) { reset(ptr); }

	refptr &operator=(const refptr &rp) {
		return lab::reconstruct(*this, rp);
	}

	refptr &operator=(refptr &&rp) {
		return lab::reconstruct(*this, std::move(rp));
	}

	T *get() const { return m_ptr; }

	T &operator*() const { return *m_ptr; }
	T *operator->() const { return m_ptr; }

	explicit operator bool() const { return (bool)m_ptr; }

	bool operator==(T *ptr) const { return m_ptr == ptr; }
	bool operator==(const refptr &rp) const { return m_ptr == rp.m_ptr; }
	bool operator!=(T *ptr) const { return m_ptr != ptr; }
	bool operator!=(const refptr &rp) const { return m_ptr != rp.m_ptr; }

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
	friend refptr<T>;

	refcounted() {}
	~refcounted() { assert(m_refcount == 0); }

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

	T &operator*() const { return *m_ptr; }
	T *operator->() const { return m_ptr; }

	explicit operator bool() const { return (bool)m_ptr; }

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

#endif // REFPTR_H
