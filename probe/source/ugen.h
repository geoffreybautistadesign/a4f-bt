// Accès USB direct par /dev/usb/B.A.0 (pilote ugen du noyau FreeBSD 9 de la PS4),
// sans passer par libSceUsbd. Structures et ioctls recopiés de FreeBSD stable/9
// sys/dev/usb/usb_ioctl.h (même interface que le backend libusb20 de FreeBSD).
#pragma once

#include <stdint.h>
#include <sys/ioctl.h>

struct ugen_device_request {   // struct usb_device_request (uWord = 2 octets)
    uint8_t  bmRequestType;
    uint8_t  bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} __attribute__((packed));

struct ugen_ctl_request {      // struct usb_ctl_request
    void    *ucr_data;
    uint16_t ucr_flags;
    uint16_t ucr_actlen;
    uint8_t  ucr_addr;
    struct ugen_device_request ucr_request;
} __attribute__((aligned(8)));

struct ugen_gen_descriptor {   // struct usb_gen_descriptor
    void    *ugd_data;
    uint16_t ugd_lang_id;
    uint16_t ugd_maxlen;
    uint16_t ugd_actlen;
    uint16_t ugd_offset;
    uint8_t  ugd_config_index;
    uint8_t  ugd_string_index;
    uint8_t  ugd_iface_index;
    uint8_t  ugd_altif_index;
    uint8_t  ugd_endpt_index;
    uint8_t  ugd_report_type;
    uint8_t  reserved[8];
} __attribute__((aligned(8)));

struct ugen_device_descriptor { // struct usb_device_descriptor
    uint8_t  bLength, bDescriptorType;
    uint16_t bcdUSB;
    uint8_t  bDeviceClass, bDeviceSubClass, bDeviceProtocol, bMaxPacketSize;
    uint16_t idVendor, idProduct, bcdDevice;
    uint8_t  iManufacturer, iProduct, iSerialNumber, bNumConfigurations;
} __attribute__((packed));

struct ugen_fs_endpoint {      // struct usb_fs_endpoint
    void   **ppBuffer;
    uint32_t *pLength;
    uint32_t nFrames;
    uint32_t aFrames;
    uint16_t flags;
    uint16_t timeout;
    uint16_t isoc_time_complete;
    int      status;
} __attribute__((aligned(8)));

struct ugen_fs_init {
    struct ugen_fs_endpoint *pEndpoints;
    uint8_t ep_index_max;
} __attribute__((aligned(8)));

struct ugen_fs_uninit { uint8_t dummy; };

struct ugen_fs_open {
    uint32_t max_bufsize;
    uint32_t max_frames;
    uint16_t max_packet_length;
    uint8_t  dev_index;
    uint8_t  ep_index;
    uint8_t  ep_no;
} __attribute__((aligned(4)));

struct ugen_fs_index { uint8_t ep_index; };   // usb_fs_start / stop / complete / close

#define UGEN_FS_FLAG_SINGLE_SHORT_OK 0x0001
#define UGEN_FS_FLAG_MULTI_SHORT_OK  0x0002

#define UGEN_GET_CONFIG          _IOR ('U', 100, int)
#define UGEN_SET_CONFIG          _IOW ('U', 101, int)
#define UGEN_GET_DEVICE_DESC     _IOR ('U', 105, struct ugen_device_descriptor)
#define UGEN_DO_REQUEST          _IOWR('U', 111, struct ugen_ctl_request)
#define UGEN_GET_IFACE_DRIVER    _IOWR('U', 121, struct ugen_gen_descriptor)
#define UGEN_CLAIM_INTERFACE     _IOW ('U', 122, int)
#define UGEN_RELEASE_INTERFACE   _IOW ('U', 123, int)
#define UGEN_IFACE_DRIVER_ACTIVE _IOW ('U', 124, int)
#define UGEN_IFACE_DRIVER_DETACH _IOW ('U', 125, int)
#define UGEN_FS_START            _IOW ('U', 192, struct ugen_fs_index)
#define UGEN_FS_STOP             _IOW ('U', 193, struct ugen_fs_index)
#define UGEN_FS_COMPLETE         _IOR ('U', 194, struct ugen_fs_index)
#define UGEN_FS_INIT             _IOW ('U', 195, struct ugen_fs_init)
#define UGEN_FS_UNINIT           _IOW ('U', 196, struct ugen_fs_uninit)
#define UGEN_FS_OPEN             _IOWR('U', 197, struct ugen_fs_open)
#define UGEN_FS_CLOSE            _IOW ('U', 198, struct ugen_fs_index)
