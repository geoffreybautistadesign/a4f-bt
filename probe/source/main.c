// a4f_bt_probe : sonde GoldHEN qui vérifie, depuis le processus d'un jeu, qu'on peut
// piloter le dongle Bluetooth USB avec libSceUsbd avant d'y porter BTstack.
//
// v3 : libSceUsbd ne liste aucun périphérique depuis un jeu ; si elle échoue, on passe
// directement par le pilote ugen du noyau (/dev/usb/B.A.0), comme libusb sous FreeBSD.
//
// Étapes (chacune notée dans /data/GoldHEN/a4f_bt_probe.log et en notification) :
//  1. charger libSceUsbd.sprx et trouver ses fonctions ;
//  2. sceUsbdInit, lister les périphériques USB, trouver le dongle (classe 0xE0) ;
//  3. l'ouvrir, détacher un éventuel pilote, réclamer l'interface 0 ;
//  4. envoyer des commandes HCI (endpoint de contrôle) et lire les événements
//     (endpoint interrupt 0x81) : Reset, version de la puce, adresse ;
//  5. lancer une recherche Bluetooth de 6 s pour vérifier la radio
//     (AirPods en mode appairage : on doit voir 1C:77:54:71:14:B6).
// Si une étape est refusée, on réessaie une fois après sys_sdk_jailbreak()
// pour savoir si c'est le bac à sable du jeu qui bloque.

#include <Common.h>
// types libusb seulement : les fonctions sceUsbd* sont résolues à la main (voir U)
#include <orbis/_types/libusb.h>
#include "plugin_common.h"
#include "ugen.h"

attr_public const char *g_pluginName = "a4f_bt_probe";
attr_public const char *g_pluginDesc = "Test du dongle Bluetooth USB via libSceUsbd";
attr_public const char *g_pluginAuth = "geoffrey";
attr_public u32 g_pluginVersion = 0x00000500; // 5.00

// Le même fichier sert aussi d'appli homebrew autonome (ps4/a4f_bt_app, -DA4F_PROBE_APP) :
// processus à elle, hors d'un jeu.
#ifdef A4F_PROBE_APP
#define LOG_PATH GOLDHEN_PATH "/a4f_bt_app.log"
#else
#define LOG_PATH GOLDHEN_PATH "/a4f_bt_probe.log"
#endif

// ---------------------------------------------------------------- journal

static FILE *g_log;

static void logf_(const char *fmt, ...) {
    char line[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    final_printf("%s\n", line);
    if (g_log) {
        fprintf(g_log, "%s\n", line);
        fflush(g_log);   // le jeu peut planter à l'étape suivante : on écrit tout de suite
    }
}

static void log_hex(const char *what, const u8 *p, int n) {
    char s[3 * 64 + 1];
    int k = 0;
    for (int i = 0; i < n && i < 64; i++)
        k += snprintf(s + k, sizeof(s) - k, "%02x ", p[i]);
    s[k] = 0;
    logf_("%s (%d) %s", what, n, s);
}

// ---------------------------------------------------------------- jailbreak

static struct jailbreak_backup g_jb;
static int g_jailbroken;

// Renvoie 1 si on vient de sortir du bac à sable (donc ça vaut la peine de réessayer).
static int try_jailbreak(const char *why) {
    if (g_jailbroken) return 0;
    int r = sys_sdk_jailbreak(&g_jb);
    logf_("echec %s -> sys_sdk_jailbreak = %d", why, r);
    if (r != 0) return 0;
    g_jailbroken = 1;
    return 1;
}

// ---------------------------------------------------------------- libSceUsbd chargée à la main
// On ne lie pas libSceUsbd à la compilation : si le jeu n'a pas le droit de la charger,
// le plugin ne se chargerait pas du tout et on n'apprendrait rien.

static struct {
    int32_t (*Init)(void);
    int32_t (*Exit)(void);
    int32_t (*GetDeviceList)(libusb_device ***);
    int32_t (*FreeDeviceList)(libusb_device **);
    int32_t (*GetDeviceDescriptor)(libusb_device *, struct libusb_device_descriptor *);
    int32_t (*Open)(libusb_device *, libusb_device_handle **);
    libusb_device_handle *(*OpenDeviceWithVidPid)(uint16_t, uint16_t);
    int32_t (*HandleEventsTimeout)(void *);
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
    static const char *paths[] = { "libSceUsbd.sprx", "/system/common/lib/libSceUsbd.sprx" };
    int32_t h = -1;
    for (int attempt = 0; attempt < 2; attempt++) {
        for (unsigned i = 0; i < 2; i++) {
            h = (int32_t) sceKernelLoadStartModule(paths[i], 0, NULL, 0, NULL, NULL);
            logf_("sceKernelLoadStartModule(%s) = 0x%08x", paths[i], (u32) h);
            if (h >= 0) break;
        }
        if (h >= 0 || !try_jailbreak("chargement libSceUsbd")) break;
    }
    if (h < 0) return -1;

#define SYM(field, name) do { \
        int r_ = sceKernelDlsym(h, name, (void **) &U.field); \
        if (r_ != 0 || !U.field) { logf_("symbole %s introuvable (0x%08x)", name, (u32) r_); return -1; } \
    } while (0)
    SYM(Init, "sceUsbdInit");
    SYM(Exit, "sceUsbdExit");
    SYM(GetDeviceList, "sceUsbdGetDeviceList");
    SYM(FreeDeviceList, "sceUsbdFreeDeviceList");
    SYM(GetDeviceDescriptor, "sceUsbdGetDeviceDescriptor");
    SYM(Open, "sceUsbdOpen");
    SYM(OpenDeviceWithVidPid, "sceUsbdOpenDeviceWithVidPid");
    SYM(HandleEventsTimeout, "sceUsbdHandleEventsTimeout");
    SYM(Close, "sceUsbdClose");
    SYM(KernelDriverActive, "sceUsbdKernelDriverActive");
    SYM(DetachKernelDriver, "sceUsbdDetachKernelDriver");
    SYM(ClaimInterface, "sceUsbdClaimInterface");
    SYM(ReleaseInterface, "sceUsbdReleaseInterface");
    SYM(ControlTransfer, "sceUsbdControlTransfer");
    SYM(InterruptTransfer, "sceUsbdInterruptTransfer");
    SYM(BulkTransfer, "sceUsbdBulkTransfer");
#undef SYM
    logf_("libSceUsbd chargee, toutes les fonctions trouvees");
    return 0;
}

// ---------------------------------------------------------------- HCI sur USB (comme hci_transport_h2_libusb.c)

static libusb_device_handle *g_dev;
static int g_ugen = -1;                 // fd /dev/usb/B.A.0 si on passe par ugen
static struct ugen_fs_endpoint g_eps[1]; // endpoint 0 = interrupt IN 0x81 (événements HCI)

static int hci_send(u16 opcode, const u8 *params, u8 len) {
    u8 cmd[3 + 32];
    cmd[0] = opcode & 0xff;
    cmd[1] = opcode >> 8;
    cmd[2] = len;
    if (len) memcpy(cmd + 3, params, len);
    // Commande HCI = requête de contrôle classe, destinataire interface (0x20)
    int r;
    if (g_ugen >= 0) {
        struct ugen_ctl_request req = { 0 };
        req.ucr_data = cmd;
        req.ucr_request.bmRequestType = 0x20;
        req.ucr_request.wLength = 3 + len;
        r = ioctl(g_ugen, UGEN_DO_REQUEST, &req);
        if (r == 0) r = req.ucr_actlen;
    } else {
        r = U.ControlTransfer(g_dev, 0x20, 0, 0, 0, cmd, 3 + len, 1000);
    }
    logf_("HCI cmd 0x%04x -> %d", opcode, r);
    return r;
}

// Lit un événement HCI. Renvoie sa longueur, ou un code d'erreur libusb (<0).
static int hci_read_event(u8 *ev, int size, u32 timeout_ms) {
    if (g_ugen >= 0) {
        void *buf = ev;
        uint32_t len = size;
        g_eps[0].ppBuffer = &buf;
        g_eps[0].pLength = &len;
        g_eps[0].nFrames = 1;
        g_eps[0].flags = UGEN_FS_FLAG_SINGLE_SHORT_OK | UGEN_FS_FLAG_MULTI_SHORT_OK;
        g_eps[0].timeout = timeout_ms;
        struct ugen_fs_index idx = { 0 };
        if (ioctl(g_ugen, UGEN_FS_START, &idx) != 0) return -99;
        for (u32 t = 0; t < timeout_ms + 200; t += 5) {
            idx.ep_index = 0;
            if (ioctl(g_ugen, UGEN_FS_COMPLETE, &idx) == 0) {
                if (g_eps[0].status == 0) return (int) len;
                return g_eps[0].status == 15 /* USB_ERR_TIMEOUT */ ? -7 : -100 - g_eps[0].status;
            }
            sceKernelUsleep(5000);
        }
        ioctl(g_ugen, UGEN_FS_STOP, &idx);
        return -7;
    }
    int32_t n = 0;
    int r = U.InterruptTransfer(g_dev, 0x81, ev, size, &n, timeout_ms);
    if (r == (int) 0x80240007) return -7;   // délai dépassé, code Sony de LIBUSB_ERROR_TIMEOUT
    return r < 0 ? r : n;
}

// Attend le Command Complete de opcode ; recopie l'événement dans ev.
static int hci_wait_complete(u16 opcode, u8 *ev, int size) {
    for (int i = 0; i < 20; i++) {
        int n = hci_read_event(ev, size, 500);
        if (n < 0) {
            logf_("lecture evenement -> %d", n);
            if (n != -7) return n;  // -7 = LIBUSB_ERROR_TIMEOUT : on réessaie
            continue;
        }
        log_hex("evt", ev, n);
        if (n >= 6 && ev[0] == 0x0e && (ev[3] | (ev[4] << 8)) == opcode) return n;
    }
    return -1;
}

static int g_found_devices;

static void hci_inquiry(void) {
    u8 ev[260];
    const u8 params[] = { 0x33, 0x8b, 0x9e, 0x05, 0x00 }; // GIAC, 5 x 1,28 s, réponses illimitées
    if (hci_send(0x0401, params, sizeof(params)) < 0) return;
    u64 end = sceKernelGetProcessTime() + 8000000;
    while (sceKernelGetProcessTime() < end) {
        int n = hci_read_event(ev, sizeof(ev), 500);
        if (n == -7) continue;
        if (n < 0) { logf_("lecture evenement -> %d", n); return; }
        if (ev[0] == 0x0f) { logf_("Inquiry: Command Status %d", ev[2]); continue; }
        if (ev[0] == 0x01) { logf_("Inquiry terminee"); return; }
        if (ev[0] == 0x02 || ev[0] == 0x22 || ev[0] == 0x2f) {
            // l'adresse suit le nombre de réponses, octets à l'envers ; CoD 6 (0x02) ou 5 (0x22/0x2f) octets plus loin
            const u8 *a = ev + 3;
            const u8 *cod = ev + 3 + 6 + (ev[0] == 0x02 ? 3 : 2);
            logf_("appareil %02X:%02X:%02X:%02X:%02X:%02X classe %02x%02x%02x",
                  a[5], a[4], a[3], a[2], a[1], a[0], cod[2], cod[1], cod[0]);
            g_found_devices++;
        } else {
            log_hex("evt", ev, n);
        }
    }
}

// ---------------------------------------------------------------- noeuds du noyau

// Liste un dossier (format dirent FreeBSD) sur quelques lignes du journal.
static void log_dir(const char *path) {
    int fd = sceKernelOpen(path, 0x20000 /* O_DIRECTORY */, 0);
    if (fd < 0) { logf_("ouverture %s -> 0x%08x", path, (u32) fd); return; }
    char buf[2048], line[200];
    int k = snprintf(line, sizeof(line), "%s :", path);
    int n;
    while ((n = sceKernelGetdents(fd, buf, sizeof(buf))) > 0) {
        for (int off = 0; off < n;) {
            u16 reclen = *(u16 *) (buf + off + 4);
            u8 namlen = *(u8 *) (buf + off + 7);
            const char *name = buf + off + 8;
            if (reclen == 0) break;
            if (name[0] != '.') {
                if (k + namlen + 2 >= (int) sizeof(line)) { logf_("%s", line); k = snprintf(line, sizeof(line), "  "); }
                k += snprintf(line + k, sizeof(line) - k, " %.*s", namlen, name);
            }
            off += reclen;
        }
    }
    logf_("%s", line);
    sceKernelClose(fd);
}

// ---------------------------------------------------------------- sonde

static const char *hci_tests(void);

static const char *probe_usbd(void) {
    struct proc_info pi;
    if (sys_sdk_proc_info(&pi) == 0)
        logf_("processus %s (%s) titre %s", pi.name, pi.path, pi.titleid);

    if (load_usbd() < 0) return "libSceUsbd non chargeable";

    int r = U.Init();
    logf_("sceUsbdInit = 0x%08x", (u32) r);
    if (r < 0 && try_jailbreak("sceUsbdInit")) {
        r = U.Init();
        logf_("sceUsbdInit (jailbreak) = 0x%08x", (u32) r);
    }
    if (r < 0) return "sceUsbdInit refuse";

    // v2 : ce que voit le noyau (ugen = USB générique, ubt = pilote Bluetooth BSD, usb = bus)
    log_dir("/dev");
    log_dir("/dev/usb");

    // v2 : la liste peut se remplir de façon asynchrone ; on réessaie 10 s en traitant les événements
    libusb_device **list = NULL;
    int count = 0;
    for (int t = 0; t < 10; t++) {
        count = U.GetDeviceList(&list);
        logf_("sceUsbdGetDeviceList (essai %d) = %d", t + 1, count);
        if (count != 0) break;
        U.FreeDeviceList(list);
        list = NULL;
        struct { s64 sec, usec; } tv = { 0, 200000 };
        int e = U.HandleEventsTimeout(&tv);
        if (t == 0) logf_("sceUsbdHandleEventsTimeout = 0x%08x", (u32) e);
        sceKernelSleep(1);
    }
    if (count < 0) return "liste USB refusee";

    libusb_device *bt = NULL;
    for (int i = 0; i < count; i++) {
        struct libusb_device_descriptor d;
        if (U.GetDeviceDescriptor(list[i], &d) < 0) continue;
        logf_("  usb %04x:%04x classe %02x/%02x/%02x", d.idVendor, d.idProduct,
              d.bDeviceClass, d.bDeviceSubClass, d.bDeviceProtocol);
        if (!bt && d.idVendor == 0x0a12 && d.idProduct == 0x0001)   // jamais le Bluetooth interne
            bt = list[i];
    }
    if (bt) {
        r = U.Open(bt, &g_dev);
        logf_("sceUsbdOpen = 0x%08x", (u32) r);
        if (r < 0 && try_jailbreak("sceUsbdOpen")) {
            r = U.Open(bt, &g_dev);
            logf_("sceUsbdOpen (jailbreak) = 0x%08x", (u32) r);
        }
    }
    if (list) U.FreeDeviceList(list);
    if (!g_dev) {
        // v2 : la liste peut être filtrée alors que l'ouverture directe marche
        g_dev = U.OpenDeviceWithVidPid(0x0a12, 0x0001);
        logf_("sceUsbdOpenDeviceWithVidPid(0a12:0001) = %p", (void *) g_dev);
    }
    if (!g_dev) return bt ? "ouverture du dongle refusee" : "dongle Bluetooth introuvable";

    r = U.KernelDriverActive(g_dev, 0);
    logf_("sceUsbdKernelDriverActive(0) = %d", r);
    if (r == 1) logf_("sceUsbdDetachKernelDriver(0) = %d", U.DetachKernelDriver(g_dev, 0));

    r = U.ClaimInterface(g_dev, 0);
    logf_("sceUsbdClaimInterface(0) = 0x%08x", (u32) r);
    if (r < 0 && try_jailbreak("sceUsbdClaimInterface")) {
        r = U.ClaimInterface(g_dev, 0);
        logf_("sceUsbdClaimInterface (jailbreak) = 0x%08x", (u32) r);
    }
    if (r < 0) { U.Close(g_dev); return "interface du dongle refusee"; }

    const char *res = hci_tests();
    U.ReleaseInterface(g_dev, 0);
    U.Close(g_dev);
    return res;
}

// Commandes HCI de base + recherche ; marche avec les deux transports.
static const char *hci_tests(void) {
    const char *res = NULL;
    u8 ev[260];
    if (hci_send(0x0c03, NULL, 0) < 0) res = "commande HCI refusee";
    else if (hci_wait_complete(0x0c03, ev, sizeof(ev)) < 0) res = "pas de reponse au HCI Reset";

    if (!res) {
        hci_send(0x1001, NULL, 0);   // Read Local Version Information
        if (hci_wait_complete(0x1001, ev, sizeof(ev)) >= 14)
            logf_("puce : HCI %d rev 0x%04x LMP %d fabricant 0x%04x sous-version 0x%04x",
                  ev[6], ev[7] | (ev[8] << 8), ev[9], ev[10] | (ev[11] << 8), ev[12] | (ev[13] << 8));
        hci_send(0x1009, NULL, 0);   // Read BD_ADDR
        if (hci_wait_complete(0x1009, ev, sizeof(ev)) >= 12)
            logf_("adresse %02X:%02X:%02X:%02X:%02X:%02X", ev[11], ev[10], ev[9], ev[8], ev[7], ev[6]);

        // endpoint bulk (ACL, servira au son) : pas de données attendues, un timeout (-7) suffit
        if (g_ugen < 0) {
            int32_t n = 0;
            logf_("lecture bulk 0x82 = %d", U.BulkTransfer(g_dev, 0x82, ev, sizeof(ev), &n, 200));
        }

        hci_inquiry();
    }
    return res;
}

// v3 : accès direct par le pilote ugen du noyau.
static const char *probe_ugen(void) {
    if (!g_jailbroken && !try_jailbreak("acces /dev/usb")) return "jailbreak impossible";
    logf_("uid %d euid %d", getuid(), geteuid());
    char path[32];
    int found = -1;
    for (int bus = 0; bus < 2 && found < 0; bus++) {
        for (int addr = 1; addr < 16; addr++) {
            snprintf(path, sizeof(path), "/dev/usb/%d.%d.0", bus, addr);
            int fd = sceKernelOpen(path, 2 /* O_RDWR */, 0);
            if (fd < 0 && fd != (int) 0x80020002 /* ENOENT : pas de périphérique */) {
                // v3.1 : noter pourquoi (0x80020001 EPERM, 0x8002000d EACCES, 0x80020010 EBUSY...)
                int ro = sceKernelOpen(path, 0 /* O_RDONLY */, 0);
                char alt[32];
                snprintf(alt, sizeof(alt), "/dev/ugen%d.%d", bus, addr);
                int fa = sceKernelOpen(alt, 2, 0);
                logf_("%s : O_RDWR 0x%08x, O_RDONLY 0x%08x, %s 0x%08x",
                      path, (u32) fd, (u32) ro, alt, (u32) fa);
                if (ro >= 0) sceKernelClose(ro);
                fd = fa;
            }
            if (fd < 0) continue;
            struct ugen_device_descriptor d;
            int r = ioctl(fd, UGEN_GET_DEVICE_DESC, &d);
            char drv[64] = "";
            struct ugen_gen_descriptor g = { 0 };
            g.ugd_data = drv;
            g.ugd_maxlen = sizeof(drv) - 1;
            int rd = ioctl(fd, UGEN_GET_IFACE_DRIVER, &g);
            logf_("%s : desc %d -> %04x:%04x classe %02x/%02x/%02x, pilote iface0 %s",
                  path, r, d.idVendor, d.idProduct, d.bDeviceClass, d.bDeviceSubClass, d.bDeviceProtocol,
                  rd == 0 ? drv : "(aucun)");
            // uniquement notre dongle CSR : ne jamais détacher le Bluetooth interne de la PS4 (manettes)
            if (found < 0 && r == 0 && d.idVendor == 0x0a12 && d.idProduct == 0x0001) {
                found = fd;
                break;
            }
            sceKernelClose(fd);
        }
    }
    if (found < 0) return "dongle introuvable dans /dev/usb";
    g_ugen = found;

    int iface = 0, cfg = -1;
    logf_("UGEN_IFACE_DRIVER_ACTIVE = %d", ioctl(g_ugen, UGEN_IFACE_DRIVER_ACTIVE, &iface));
    logf_("UGEN_IFACE_DRIVER_DETACH = %d", ioctl(g_ugen, UGEN_IFACE_DRIVER_DETACH, &iface));
    logf_("UGEN_GET_CONFIG = %d (config %d)", ioctl(g_ugen, UGEN_GET_CONFIG, &cfg), cfg);
    if (cfg != 1) {
        cfg = 1;
        logf_("UGEN_SET_CONFIG(1) = %d", ioctl(g_ugen, UGEN_SET_CONFIG, &cfg));
    }
    int r = ioctl(g_ugen, UGEN_CLAIM_INTERFACE, &iface);
    logf_("UGEN_CLAIM_INTERFACE(0) = %d", r);

    struct ugen_fs_init fi = { g_eps, 1 };
    r = ioctl(g_ugen, UGEN_FS_INIT, &fi);
    logf_("UGEN_FS_INIT = %d", r);
    struct ugen_fs_open fo = { 0 };
    fo.max_bufsize = 1024;
    fo.max_frames = 1;
    fo.ep_index = 0;
    fo.ep_no = 0x81;
    int ro = ioctl(g_ugen, UGEN_FS_OPEN, &fo);
    logf_("UGEN_FS_OPEN(0x81) = %d, paquet max %d", ro, fo.max_packet_length);

    const char *res = (r == 0 && ro == 0) ? hci_tests() : "preparation de l'endpoint 0x81 refusee";

    struct ugen_fs_index idx = { 0 };
    ioctl(g_ugen, UGEN_FS_CLOSE, &idx);
    struct ugen_fs_uninit fu = { 0 };
    ioctl(g_ugen, UGEN_FS_UNINIT, &fu);
    ioctl(g_ugen, UGEN_RELEASE_INTERFACE, &iface);
    sceKernelClose(g_ugen);
    g_ugen = -1;
    return res;
}

static const char *probe(void) {
    const char *r = probe_usbd();
    if (!r) return NULL;
    logf_("libSceUsbd : %s -> essai par ugen", r);
    return probe_ugen();
}

static void *probe_thread(void *arg) {
    (void) arg;
#ifndef A4F_PROBE_APP
    sceKernelSleep(8);   // laisser le jeu démarrer
#endif
    g_log = fopen(LOG_PATH, "w");
    logf_("a4f_bt_probe %s, journal %s", BUILD_TYPE, g_log ? "ok" : "impossible a ouvrir");
    Notify(TEX_ICON_SYSTEM, "a4f_bt_probe\nTest du dongle Bluetooth...");

    const char *err = probe();
    if (err) {
        logf_("RESULTAT : ECHEC - %s%s", err, g_jailbroken ? " (meme apres jailbreak)" : "");
        Notify(TEX_ICON_SYSTEM, "a4f_bt_probe : ECHEC\n%s", err);
    } else {
        logf_("RESULTAT : OK%s, %d appareil(s) Bluetooth vu(s)",
              g_jailbroken ? " (avec jailbreak)" : " (sans jailbreak)", g_found_devices);
        Notify(TEX_ICON_SYSTEM, "a4f_bt_probe : OK %s\n%d appareil(s) Bluetooth vu(s)",
               g_jailbroken ? "(jailbreak)" : "", g_found_devices);
    }
    if (g_jailbroken) sys_sdk_unjailbreak(&g_jb);
    if (g_log) fclose(g_log);
    return NULL;
}

#ifdef A4F_PROBE_APP
int main(void) {
    probe_thread(NULL);
    sceKernelSleep(30);  // laisser le temps de lire la notification avant de quitter
    return 0;
}
#else
s32 attr_public plugin_load(s32 argc, const char *argv[]) {
    final_printf("[GoldHEN] <%s\\Ver.0x%08x> %s\n", g_pluginName, g_pluginVersion, __func__);
    OrbisPthread th;
    scePthreadCreate(&th, NULL, probe_thread, NULL, "a4f_bt_probe");
    return 0;
}

s32 attr_public plugin_unload(s32 argc, const char *argv[]) {
    final_printf("[GoldHEN] <%s\\Ver.0x%08x> %s\n", g_pluginName, g_pluginVersion, __func__);
    return 0;
}

s32 attr_module_hidden module_start(s64 argc, const void *args) {
    return 0;
}

s32 attr_module_hidden module_stop(s64 argc, const void *args) {
    return 0;
}
#endif
