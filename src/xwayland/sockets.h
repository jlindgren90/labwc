#ifndef XWAYLAND_SOCKETS_H
#define XWAYLAND_SOCKETS_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool set_cloexec(int fd, bool cloexec);
void unlink_display_sockets(int display);
int open_display_sockets(int socks[2]);

#ifdef __cplusplus
}
#endif

#endif
