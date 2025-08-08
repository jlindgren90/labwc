/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_LISTENER_H
#define LABWC_LISTENER_H

#include <wayland-server-core.h>

template<typename T>
class listener : private wl_listener
{
public:
	using func = void (T::*)(void *);

	listener(T *target, func func)
		: wl_listener{}, m_target(target), m_func(func) {}

	~listener() { disconnect(); }

	listener(const listener &) = delete;
	listener &operator=(const listener &) = delete;

	void connect(wl_signal *signal) {
		disconnect();
		this->notify = run;
		wl_signal_add(signal, this);
	}

	void disconnect() {
		if (this->notify) {
			wl_list_remove(&this->link);
			this->link = wl_list{};
			this->notify = nullptr;
		}
	}

private:
	static void run(struct wl_listener *wl_listener, void *data) {
		auto self = static_cast<listener *>(wl_listener);
		(self->m_target->*(self->m_func))(data);
	}

	T *const m_target;
	func const m_func;
};

#define DECLARE_LISTENER(type, name) \
	listener<type> on_##name{this, &type::handle_##name}

#define DECLARE_HANDLER(type, name) \
	void handle_##name(void * = nullptr); \
	DECLARE_LISTENER(type, name)

#define CONNECT_LISTENER(src, dest, name) \
	(dest)->on_##name.connect(&(src)->events.name)

class destroyable
{
public:
	destroyable() {}
	virtual ~destroyable() {}

	destroyable(const destroyable &) = delete;
	destroyable &operator=(const destroyable &) = delete;

	DECLARE_LISTENER(destroyable, destroy);

private:
	void handle_destroy(void *) { delete this; }
};

#endif
