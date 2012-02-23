#include "config.h"

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
    infinitv_usbd_state_t* iu;
    GUsbDevice* device;
    read_buffer_t buffers[NUM_TUNERS][NUM_READ_BUFFERS];
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

    g_print("got %d\n", len);

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
    

static void
check_for_infinitv(
        infinitv_usbd_state_t* iu,
        GUsbDevice* device)
{
    GError* error = NULL;
    guint16 vid = g_usb_device_get_vid( device );
    guint16 pid = g_usb_device_get_pid( device );
    if( vid == 0x2432 && pid == 0x0aa2 ) {
        g_print("found infinitv\n");

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

        infinitv_t* i = g_slice_new0( infinitv_t );
        i->iu = iu;
        i->device = device;

        submit_buffers(i);

        g_ptr_array_add( iu->devices, iu );
    }
}

static void
usb_device_list_added_cb(
        GUsbDeviceList* list,
        GUsbDevice* device,
        gpointer user_data)
{
    infinitv_usbd_state_t* iu = user_data;
    g_print("device %s added %x:%x\n",
            g_usb_device_get_platform_id( device ),
            g_usb_device_get_bus( device ),
            g_usb_device_get_address( device ));
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
