#include "inet_util.h"
#include <glib.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <sys/socket.h>
#include <net/if.h>

#include <netlink/route/link.h>
#include <netlink/route/addr.h>

typedef struct {
    GPtrArray* interfaces;
    struct nl_cache* link_cache;
    struct nl_cache* addr_cache;
    struct rtnl_link* cur_link;
} NLHelper;

static void
addr_foreach( struct nl_object* o, void* userdata )
{
    NLHelper* helper = userdata;
    struct rtnl_addr* addr = (struct rtnl_addr*)o;
    struct nl_addr* naddr = rtnl_addr_get_local(addr);


    InetInterface* ia = g_slice_new0( InetInterface );
    g_strlcpy( ia->name, rtnl_link_get_name( helper->cur_link ), 16 );
    ia->addr = g_inet_address_new_from_bytes(
            nl_addr_get_binary_addr( naddr ),
            AF_INET );

    g_ptr_array_add( helper->interfaces, ia );
}

static void
link_foreach( struct nl_object* o, void* userdata )
{
    NLHelper* helper = userdata;
    struct rtnl_link* link = (struct rtnl_link*)o;
    helper->cur_link = link;

    guint flags = rtnl_link_get_flags( link );

    if( !(flags & IFF_UP) || flags & IFF_LOOPBACK ) {
        return;
    }

    char* name = rtnl_link_get_name( link );

    struct rtnl_addr* filter = rtnl_addr_alloc();
    rtnl_addr_set_ifindex(filter, rtnl_link_name2i(helper->link_cache, name));
    rtnl_addr_set_family(filter, AF_INET);

    nl_cache_foreach_filter(helper->addr_cache, (struct nl_object*)filter, addr_foreach, helper);

    rtnl_addr_put( filter );
}

static void
free_inet_interface( gpointer p )
{
    InetInterface* ii = p;
    if( ii ) {
        g_object_unref( ii->addr );
        g_slice_free( InetInterface, ii );
    }
}

GPtrArray*
list_inet_interfaces()
{
    NLHelper helper = {};
    int i;
    struct nl_handle* sock = nl_handle_alloc();

    nl_connect( sock, NETLINK_ROUTE );

    helper.link_cache = rtnl_link_alloc_cache( sock );
    helper.addr_cache = rtnl_addr_alloc_cache( sock );

    helper.interfaces = g_ptr_array_new();
    g_ptr_array_set_free_func( helper.interfaces, free_inet_interface );

    nl_cache_foreach( helper.link_cache, link_foreach, &helper );

    nl_cache_free( helper.link_cache );
    nl_cache_free( helper.addr_cache );
    nl_handle_destroy( sock );

    return helper.interfaces;
}
