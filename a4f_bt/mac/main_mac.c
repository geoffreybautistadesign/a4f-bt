// Test sur Mac du cœur A4F BT : même code que le plugin PS4, avec libusb à la place
// de libSceUsbd. Reçoit aussi le PCM A4F en UDP (port 9051, cf. src/pcm_send.py).
//   ./a4f_bt_mac          -> tonalité de test
//   ./a4f_bt_mac pcm      -> son reçu en UDP
#include <libusb.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "a4f_bt.h"
#include "a4f_protocol.h"

// ---- API USB par libusb (mêmes appels que sceUsbd* sur PS4)

static int map(int r){
    if (r == 0) return A4F_USB_OK;
    if (r == LIBUSB_ERROR_TIMEOUT) return A4F_USB_TIMEOUT;
    return A4F_USB_ERROR;
}

static void *usb_open(uint16_t vid, uint16_t pid){
    if (libusb_init(NULL) < 0) return NULL;
    libusb_device_handle *h = libusb_open_device_with_vid_pid(NULL, vid, pid);
    if (!h) return NULL;
    if (libusb_kernel_driver_active(h, 0) == 1) libusb_detach_kernel_driver(h, 0);
    if (libusb_claim_interface(h, 0) < 0){
        libusb_close(h);
        return NULL;
    }
    return h;
}

static void usb_close(void *dev){
    libusb_release_interface(dev, 0);
    libusb_close(dev);
}

static int usb_control_out(void *dev, uint8_t *data, uint16_t len, unsigned timeout_ms){
    int r = libusb_control_transfer(dev, 0x20, 0, 0, 0, data, len, timeout_ms);
    return r == len ? A4F_USB_OK : map(r < 0 ? r : LIBUSB_ERROR_IO);
}

static int usb_interrupt_in(void *dev, uint8_t ep, uint8_t *buf, int len, int *got, unsigned timeout_ms){
    return map(libusb_interrupt_transfer(dev, ep, buf, len, got, timeout_ms));
}

static int usb_bulk(void *dev, uint8_t ep, uint8_t *buf, int len, int *got, unsigned timeout_ms){
    return map(libusb_bulk_transfer(dev, ep, buf, len, got, timeout_ms));
}

static const a4f_usb_api_t usb_api = { usb_open, usb_close, usb_control_out, usb_interrupt_in, usb_bulk };

// ---- PCM A4F en UDP

static void *udp_thread(void *arg){
    (void) arg;
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in a = { 0 };
    a.sin_family = AF_INET;
    a.sin_port = htons(9051);
    if (bind(s, (struct sockaddr *) &a, sizeof(a)) < 0){
        perror("udp 9051");
        return NULL;
    }
    static uint8_t pkt[sizeof(A4fAudioHeader) + 2048 * 4];
    while (1){
        ssize_t n = recv(s, pkt, sizeof(pkt), 0);
        A4fAudioHeader h;
        if (n < (ssize_t) sizeof(h)) continue;
        memcpy(&h, pkt, sizeof(h));
        if (h.magic != A4F_MAGIC_AUDIO || h.port != 0 || (size_t) n != sizeof(h) + h.frames * 4u) continue;
        a4f_bt_push_pcm((const int16_t *) (pkt + sizeof(h)), h.frames);
    }
    return NULL;
}

static void on_status(const char *msg){
    printf(">>> %s\n", msg);
}

int main(int argc, char **argv){
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (argc > 1 && strcmp(argv[1], "pcm") == 0){
        a4f_bt_set_tone(false);
        pthread_t t;
        pthread_create(&t, NULL, udp_thread, NULL);
    }
    a4f_bt_config_t cfg = {
        .usb = &usb_api, .vid = 0x0a12, .pid = 0x0001,
        .data_dir = "/tmp/a4f_bt_", .dump_path = "/tmp/a4f_bt.pklg",
        .status = on_status,
    };
    return a4f_bt_run(&cfg);
}
