// SPDX-License-Identifier: GPL-2.0-only
#ifndef LABWC_STR_H
#define LABWC_STR_H

#include <string>

/*
 * Slightly tweaked version of std::string.
 *
 * Conversion from (const char *) must be explicit. Conversion from
 * NULL, instead of being undefined behavior, produces an empty string.
 *
 * c() replaces c_str() for brevity.
 *
 * operator bool() is provided for easy migration from (const char *),
 * but note that an empty string also converts to false (because there
 * is no distinction between null and empty).
 */
class lab_str : public std::string
{
public:
	lab_str() {};
	lab_str(const lab_str &s) : std::string(s) {}
	lab_str(lab_str &&s) : std::string(std::move(s)) {}

	explicit lab_str(const char *s) : std::string(s ? s : "") {}

	lab_str &operator=(const lab_str &s) {
		std::string::operator=(s);
		return *this;
	}

	lab_str &operator=(lab_str &&s) {
		std::string::operator=(std::move(s));
		return *this;
	}

	const char *c() const { return std::string::c_str(); }
	const char *c_str() = delete; // use c() instead

	explicit operator bool() const { return !empty(); }

	bool operator==(const lab_str &s) const {
		return static_cast<const std::string &>(*this)
			== static_cast<const std::string &>(s);
	}

	bool operator!=(const lab_str &s) const {
		return static_cast<const std::string &>(*this)
			!= static_cast<const std::string &>(s);
	}

	bool operator==(const char *s) const {
		// compare equal to (indirect) null if empty
		return static_cast<const std::string &>(*this) == (s ? s : "");
	}

	bool operator!=(const char *s) const {
		// compare equal to (indirect) null if empty
		return static_cast<const std::string &>(*this) != (s ? s : "");
	}

	// prevent direct comparison to null (prefer operator bool())
	bool operator==(std::nullptr_t) const = delete;
	bool operator!=(std::nullptr_t) const = delete;
};

// uncommon, but provided for symmetry
static inline bool operator==(const char *s, const lab_str &ls) {
	return ls.operator==(s);
}

static inline bool operator!=(const char *s, const lab_str &ls) {
	return ls.operator!=(s);
}

bool operator==(std::nullptr_t, const lab_str &) = delete;
bool operator!=(std::nullptr_t, const lab_str &) = delete;

#endif // LABWC_STR_H
