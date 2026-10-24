// Plugin GoldHEN a4f_bt : fait tourner BTstack dans le jeu et envoie le son aux AirPods
// par le dongle Bluetooth USB (port USB droit de la PS4 Pro).
// Version publiée : tonalité de test. Le son du jeu reste à brancher (voir README) :
// il suffit d'appeler a4f_bt_push_pcm() avec du PCM s16 stéréo 48 kHz.
//
// L'USB passe par libSceUsbd (même API que libusb), chargée à l'exécution comme dans
// la sonde a4f_bt_probe, qui a validé ces appels sur le firmware 13.52.

#include <Common.h>
#include <orbis/_types/libusb.h>
#include "plugin_common.h"

#include "a4f_bt.h"

attr_public const char *g_pluginName = "a4f_bt";
attr_public const char *g_pluginDesc = "Son du jeu vers des AirPods par un dongle Bluetooth USB";
attr_public const char *g_pluginAuth = "geoffrey";
attr_public u32 g_pluginVersion = 0x00000100; // 1.00

#define DATA_DIR  GOLDHEN_PATH "/a4f_bt/"
#define DUMP_PATH GOLDHEN_PATH "/a4f_bt/hci.pklg"
#define DEBUG_PATH GOLDHEN_PATH "/a4f_bt/debug.log"

#define SCE_USBD_ERROR_TIMEOUT 0x80240007   // = LIBUSB_ERROR_TIMEOUT

// ---------------------------------------------------------------- libSceUsbd

static struct {
    int32_t (*Init)(void);
    int32_t (*GetDeviceList)(libusb_device ***);
    int32_t (*FreeDeviceList)(libusb_device **);
    int32_t (*GetDeviceDescriptor)(libusb_device *, struct libusb_device_descriptor *);
    int32_t (*Open)(libusb_device *, libusb_device_handle **);
    void    (*Close)(libusb_device_handle *);
    int32_t (*KernelDriverActive)(libusb_device_handle *, int32_t);
    int32_t (*DetachKernelDriver)(libusb_device_handle *, int32_t);
    int32_t (*ClaimInterface)(libusb_device_handle *, int32_t);
    int32_t (*ReleaseInterface)(libusb_device_handle *, int32_t);
    int32_t (*ControlTransfer)(libusb_device_handle *, uint8_t, uint8_t, uint16_t, uint16_t,
                               unsigned char *, uint16_t, uint32_t);
    int32_t (*InterruptTransfer)(libusb_device_handle *, unsigned char, unsigned char *, int32_t,
                                 int32_t *, uint32_t);
    int32_t (*BulkTransfer)(libusb_device_handle *, unsigned char, unsigned char *, int32_t,
                            int32_t *, uint32_t);
} U;

static int load_usbd(void) {
    int32_t h = (int32_t) sceKernelLoadStartModule("libSceUsbd.sprx", 0, NULL, 0, NULL, NULL);
    if (h < 0) h = (int32_t) sceKernelLoadStartModule("/system/common/lib/libSceUsbd.sprx", 0, NULL, 0, NULL, NULL);
    if (h < 0) {
        final_printf("libSceUsbd: chargement 0x%08x\n", (u32) h);
        return -1;
    }
#define SYM(field, name) if (sceKernelDlsym(h, name, (void **) &U.field) != 0 || !U.field) return -1
    SYM(Init, "sceUsbdInit");
    SYM(GetDeviceList, "sceUsbdGetDeviceList");
    SYM(FreeDeviceList, "sceUsbdFreeDeviceList");
    SYM(GetDeviceDescriptor, "sceUsbdGetDeviceDescriptor");
    SYM(Open, "sceUsbdOpen");
    SYM(Close, "sceUsbdClose");
    SYM(KernelDriverActive, "sceUsbdKernelDriverActive");
    SYM(DetachKernelDriver, "sceUsbdDetachKernelDriver");
    SYM(ClaimInterface, "sceUsbdClaimInterface");
    SYM(ReleaseInterface, "sceUsbdReleaseInterface");
    SYM(ControlTransfer, "sceUsbdControlTransfer");
    SYM(InterruptTransfer, "sceUsbdInterruptTransfer");
    SYM(BulkTransfer, "sceUsbdBulkTransfer");
#undef SYM
    return U.Init() < 0 ? -1 : 0;
}

static int map(int32_t r) {
    if (r >= 0) return A4F_USB_OK;
    if ((u32) r == SCE_USBD_ERROR_TIMEOUT) return A4F_USB_TIMEOUT;
    return A4F_USB_ERROR;
}

static void *usb_open(uint16_t vid, uint16_t pid) {
    libusb_device **list = NULL;
    int n = U.GetDeviceList(&list);
    libusb_device_handle *h = NULL;
    for (int i = 0; i < n && !h; i++) {
        struct libusb_device_descriptor d;
        if (U.GetDeviceDescriptor(list[i], &d) < 0 || d.idVendor != vid || d.idProduct != pid) continue;
        if (U.Open(list[i], &h) < 0) h = NULL;
    }
    if (list) U.FreeDeviceList(list);
    if (!h) return NULL;
    if (U.KernelDriverActive(h, 0) == 1) U.DetachKernelDriver(h, 0);
    if (U.ClaimInterface(h, 0) < 0) {
        U.Close(h);
        return NULL;
    }
    return h;
}

static void usb_close(void *dev) {
    U.ReleaseInterface(dev, 0);
    U.Close(dev);
}

static int usb_control_out(void *dev, uint8_t *data, uint16_t len, unsigned timeout_ms) {
    int32_t r = U.ControlTransfer(dev, 0x20, 0, 0, 0, data, len, timeout_ms);
    return r == len ? A4F_USB_OK : map(r < 0 ? r : -1);
}

static int usb_interrupt_in(void *dev, uint8_t ep, uint8_t *buf, int len, int *got, unsigned timeout_ms) {
    int32_t n = 0;
    int r = map(U.InterruptTransfer(dev, ep, buf, len, &n, timeout_ms));
    *got = n;
    return r;
}

static int usb_bulk(void *dev, uint8_t ep, uint8_t *buf, int len, int *got, unsigned timeout_ms) {
    int32_t n = 0;
    int r = map(U.BulkTransfer(dev, ep, buf, len, &n, timeout_ms));
    *got = n;
    return r;
}

static const a4f_usb_api_t usb_api = { usb_open, usb_close, usb_control_out, usb_interrupt_in, usb_bulk };

// ---------------------------------------------------------------- libc
// Les en-têtes du toolchain (musl) appellent ces deux fonctions, que la libc PS4
// n'exporte pas sous ce nom.

extern int *__error(void);   // errno façon FreeBSD (libkernel)

int *__errno_location(void) {
    return __error();
}

// assert() raté dans BTstack : on le signale et on bloque ce thread seulement (le jeu continue).
void __assert_fail(const char *expr, const char *file, int line, const char *func) {
    final_printf("assert %s (%s:%d %s)\n", expr, file, line, func);
    Notify(TEX_ICON_SYSTEM, "A4F BT\nErreur interne %s:%d", file, line);
    for (;;) sceKernelSleep(3600);
}

// ---------------------------------------------------------------- plugin

static void on_status(const char *msg) {
    Notify(TEX_ICON_SYSTEM, "A4F BT\n%s", msg);
}

// Journal de debug : une ligne par seconde, fichier refermé à chaque fois (survit à un plantage).
static void *debug_thread(void *arg) {
    (void) arg;
    FILE *f = fopen(DEBUG_PATH, "w");
    if (f) fclose(f);
    for (u32 t = 0;; t++) {
        char bt[256];
        a4f_bt_debug(bt, sizeof(bt));
        f = fopen(DEBUG_PATH, "a");
        if (f) {
            fprintf(f, "%3us | %s\n", t, bt);
            fclose(f);
        }
        sceKernelSleep(1);
    }
    return NULL;
}

static void *bt_thread(void *arg) {
    (void) arg;
    sceKernelSleep(5);   // laisser le jeu démarrer
    if (load_usbd() < 0) {
        Notify(TEX_ICON_SYSTEM, "A4F BT\nlibSceUsbd indisponible");
        return NULL;
    }
    sceKernelMkdir(DATA_DIR, 0777);
    a4f_bt_config_t cfg = {
        .usb = &usb_api, .vid = 0x0a12, .pid = 0x0001,
        .data_dir = DATA_DIR, .dump_path = DUMP_PATH,
        .status = on_status,
    };
    a4f_bt_set_tone(true);   // tonalité de test, tant que rien n'appelle a4f_bt_push_pcm()
    a4f_bt_run(&cfg);
    return NULL;
}

s32 attr_public plugin_load(s32 argc, const char *argv[]) {
    final_printf("[GoldHEN] <%s\\Ver.0x%08x> %s\n", g_pluginName, g_pluginVersion, __func__);
    OrbisPthread th, dbg;
    scePthreadCreate(&dbg, NULL, debug_thread, NULL, "a4f_bt_dbg");
    scePthreadCreate(&th, NULL, bt_thread, NULL, "a4f_bt");
    return 0;
}

s32 attr_public plugin_unload(s32 argc, const char *argv[]) {
    final_printf("[GoldHEN] <%s\\Ver.0x%08x> %s\n", g_pluginName, g_pluginVersion, __func__);
    a4f_bt_stop();
    return 0;
}

s32 attr_module_hidden module_start(s64 argc, const void *args) {
    return 0;
}

s32 attr_module_hidden module_stop(s64 argc, const void *args) {
    return 0;
}
