#include "config.h"
#include "inet_util.h"

#define G_USB_API_IS_SUBJECT_TO_CHANGE
#include <gusb.h>
#include <stdlib.h>

#define READ_BUFFER_SIZE 512
#define NUM_READ_BUFFERS 5
#define NUM_TUNERS 4

#define BASE_MPEG_EP 3

typedef struct _infinitv_usbd_state infinitv_usbd_state_t;
typedef struct _infinitv infinitv_t;

typedef struct {
    guchar buffer[READ_BUFFER_SIZE];
    infinitv_t* in;
    guchar ep;
} read_buffer_t;

struct _infinitv {
    guint number;
    infinitv_usbd_state_t* iu;
    GUsbDevice* device;
    read_buffer_t buffers[NUM_TUNERS][NUM_READ_BUFFERS];
    gchar if_name[16];
    GInetAddress* if_addr;
    GSocket* rpc_socket;
};

struct _infinitv_usbd_state {
    GMainLoop* main_loop;
    GUsbContext* usb_context;
    GUsbDeviceList* usb_list;
    GPtrArray* devices;
};

static void
data_ready(
        GObject* source,
        GAsyncResult* res,
        gpointer user_data)
{
    GError* error = NULL;
    read_buffer_t* rb = user_data;
    infinitv_t* in = rb->in;
    gssize len = g_usb_device_bulk_transfer_finish( in->device, res, &error );

    if( error ) {
        g_printerr("read failed on ep 0x%02x '%s'\n", rb->ep, error->message);
        g_error_free( error );
        //TODO remove device?
        return;
    }

    //TODO pack data into rtp packet
    //TODO write data to TUN device
    //
    //resubmit
    g_usb_device_bulk_transfer_async(
            in->device,
            rb->ep,
            rb->buffer,
            READ_BUFFER_SIZE,
            0,
            NULL,
            data_ready,
            rb);
}

static void
submit_buffers(
        infinitv_t* in)
{
    int i,j;
    for( i=0; i<NUM_TUNERS; i++ ) {
        for( j=0; j<NUM_READ_BUFFERS; j++ ) {
            read_buffer_t* rb = &in->buffers[i][j];
            rb->in = in;
            rb->ep = 0x80 | ( BASE_MPEG_EP + i );
            g_usb_device_bulk_transfer_async(
                    in->device,
                    rb->ep,
                    rb->buffer,
                    READ_BUFFER_SIZE,
                    0,
                    NULL,
                    data_ready,
                    rb);
        }
    }
}

static gboolean
rpc_read(
        GSocket* socket,
        GIOCondition cond,
        gpointer user_data)
{
    infinitv_t* it = user_data;
    GSocketAddress* src;
    gchar buf[1024];
    if( cond == G_IO_IN ) {
        gssize len = g_socket_receive_from( socket, &src, buf, sizeof(buf), NULL, NULL );
        if( len > 0 ) {
            g_print("rpc %d\n", len);
        } else {
            g_print("recv error %d\n", len);
        }
    }

    return FALSE;
}

static void
check_for_infinitv(
        infinitv_usbd_state_t* iu,
        GUsbDevice* device)
{
    GError* error = NULL;
    guint16 vid = g_usb_device_get_vid( device );
    guint16 pid = g_usb_device_get_pid( device );
    if( vid == 0x2432 && pid == 0x0aa2 ) {
        g_print("found infinitv (%s)\n", g_usb_device_get_platform_id( device ));

        gboolean ret = g_usb_device_open( device, &error );
        if( !ret ) {
            g_printerr("failed to open device %s\n", error->message);
            g_error_free( error );
            return;
        }

        ret = g_usb_device_set_configuration( device, 0x01, &error );
        if( !ret ) {
            g_printerr("failed to set config %s\n", error->message);
            g_error_free(error);
            return;
        }

        ret = g_usb_device_claim_interface( device, 0,
                G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
                &error);
        if( !ret ) {
            g_printerr("failed to claim if %s\n", error->message);
            g_error_free(error);
            return;
        }

        infinitv_t* it = g_slice_new0( infinitv_t );
        it->number = 0;//TODO look up device number 
        it->iu = iu;
        it->device = device;
        g_snprintf( it->if_name, 16, "usb%d", it->number );

        submit_buffers(it);

        int i;
        GPtrArray* iis = list_inet_interfaces();
        for( i=0; i<iis->len; i++ ) {
            InetInterface* ii = g_ptr_array_index( iis, i );
            if( strcmp( ii->name, it->if_name ) == 0 ) {
                it->if_addr = g_object_ref( ii->addr );
                break;
            }
        }
        g_ptr_array_unref( iis );

        if( !it->if_addr ) {
            g_print("could not find network interface for infinitv\n");
        } else {
            GSocketAddress* sock_addr = g_inet_socket_address_new(it->if_addr, 3000);
            it->rpc_socket = g_socket_new(G_SOCKET_FAMILY_IPV4,
                    G_SOCKET_TYPE_DATAGRAM,
                    G_SOCKET_PROTOCOL_UDP,
                    &error);

            if( error ) {
                g_printerr("error creating rpc sock %s\n", error->message);
                g_error_free( error );
                error = NULL;
            }

            if( !g_socket_bind( it->rpc_socket, sock_addr, TRUE, &error ) ) {
                g_printerr("error binding rpc sock %s\n", error->message);
                g_error_free( error );
                error = NULL;
            }

            GSource* source = g_socket_create_source( it->rpc_socket, G_IO_IN, NULL );
            g_source_set_callback( source, (GSourceFunc)rpc_read, NULL, NULL );
            g_source_attach( source, NULL );

            g_object_unref( sock_addr );
        }

        g_ptr_array_add( iu->devices, it );
    }
}

static void
usb_device_list_added_cb(
        GUsbDeviceList* list,
        GUsbDevice* device,
        gpointer user_data)
{
    infinitv_usbd_state_t* iu = user_data;
    check_for_infinitv(iu, device);
}

static void
usb_device_list_removed_cb(
        GUsbDeviceList* list,
        GUsbDevice* device,
        gpointer user_data)
{
    infinitv_usbd_state_t* iu = user_data;
    g_print("device %s removed %x:%x\n",
            g_usb_device_get_platform_id( device ),
            g_usb_device_get_bus( device ),
            g_usb_device_get_address( device ));
}

int main(int argc, char** argv)
{
    GError* error = NULL;
    GPtrArray* devices;
    GUsbDevice* device;
    int i;

    g_thread_init(NULL);
    g_type_init();

    infinitv_usbd_state_t* iu = g_slice_new0( infinitv_usbd_state_t );

    iu->devices = g_ptr_array_new();

    iu->main_loop = g_main_loop_new( NULL, FALSE );

    iu->usb_context = g_usb_context_new( &error );
    
    if( error ) {
        g_printerr("Error creating GUsb context: %s\n",
                error->message);
        g_error_free(error);
        return EXIT_FAILURE;
    }
 
    iu->usb_list = g_usb_device_list_new( iu->usb_context );
    g_usb_device_list_coldplug( iu->usb_list );

    devices = g_usb_device_list_get_devices( iu->usb_list );
    for( i=0; i<devices->len; i++ ) {
        device = g_ptr_array_index( devices, i );
        check_for_infinitv( iu, device );
    }

    g_signal_connect( iu->usb_list, "device-added",
            G_CALLBACK( usb_device_list_added_cb ),
            iu);

    g_signal_connect( iu->usb_list, "device-removed",
            G_CALLBACK( usb_device_list_removed_cb ),
            iu);

    g_ptr_array_unref( devices );


    g_main_loop_run( iu->main_loop );
    return 0;
}
