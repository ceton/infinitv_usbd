#ifndef INFINITV_USBD_INET_UTIL
#define INFINITV_USBD_INET_UTIL

#include <gio/gio.h>

G_BEGIN_DECLS

typedef struct {
    char name[16];
    GInetAddress* addr;
} InetInterface;


GPtrArray*
list_inet_interfaces();

G_END_DECLS

#endif
