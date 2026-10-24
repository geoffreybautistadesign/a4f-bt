// Plugin GoldHEN a4f_bt : fait tourner BTstack dans le jeu et envoie le son aux AirPods
// par le dongle Bluetooth USB (port USB droit de la PS4 Pro).
// v2 : son du jeu (hooks sceAudioOut d'Audio4Free) ; v1 envoyait une tonalité de test.
//
// L'USB passe par libSceUsbd (même API que libusb), chargée à l'exécution comme dans
// la sonde a4f_bt_probe, qui a validé ces appels sur le firmware 13.52.

#include <Common.h>
#include <orbis/_types/libusb.h>
#include "plugin_common.h"

#include "a4f_bt.h"
#include "a4f_convert.h"

attr_public const char *g_pluginName = "a4f_bt";
attr_public const char *g_pluginDesc = "Son du jeu vers des AirPods par un dongle Bluetooth USB";
attr_public const char *g_pluginAuth = "geoffrey";
attr_public u32 g_pluginVersion = 0x00000300; // 3.00

#define DATA_DIR  GOLDHEN_PATH "/a4f_bt/"
#define DUMP_PATH GOLDHEN_PATH "/a4f_bt/hci.pklg"
#define DEBUG_PATH GOLDHEN_PATH "/a4f_bt/debug.log"
#define TONE_FLAG_PATH GOLDHEN_PATH "/a4f_bt/tone"
#define VOLUME_PATH GOLDHEN_PATH "/a4f_bt/volume"   // volume AirPods 0-100, relu chaque seconde
#define TV_PATH GOLDHEN_PATH "/a4f_bt/tv"           // 0 = TV muette pendant que les AirPods jouent, 1 = son aussi sur la TV

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

// Dernier échec de load_usbd, pour la notification et le journal de debug :
// étape 1 = chargement du module, 2 = symbole introuvable (g_usbd_err = n° du symbole), 3 = sceUsbdInit.
static volatile s32 g_usbd_step, g_usbd_err, g_usbd_tries;

// Certains jeux (RE4 Remake, inFamous Second Son) ne voient pas libSceUsbd depuis leur bac à
// sable (0x80020002, fichier introuvable). Comme la sonde a4f_bt_probe, on sort alors le
// processus du bac à sable avec sys_sdk_jailbreak(), on charge le module, puis on y revient
// tout de suite (sys_sdk_unjailbreak) : rester dehors change la racine des fichiers de tout le
// processus, le jeu ne trouvait plus ses données (/app0) et restait sur un écran noir
// (inFamous Second Son, v6). Le module reste chargé une fois revenu dans le bac à sable.
static struct jailbreak_backup g_jb;
static volatile s32 g_jailbroken;   // 0 = non tenté, 1 = module chargé hors du bac à sable puis retour, -1 = refusé

static int32_t load_usbd_module(void) {
    int32_t h = (int32_t) sceKernelLoadStartModule("libSceUsbd.sprx", 0, NULL, 0, NULL, NULL);
    if (h < 0) h = (int32_t) sceKernelLoadStartModule("/system/common/lib/libSceUsbd.sprx", 0, NULL, 0, NULL, NULL);
    return h;
}

static int load_usbd(void) {
    g_usbd_tries++;
    int32_t h = load_usbd_module();
    if (h < 0 && g_jailbroken == 0) {
        g_jailbroken = sys_sdk_jailbreak(&g_jb) == 0 ? 1 : -1;
        final_printf("libSceUsbd: chargement 0x%08x, sys_sdk_jailbreak -> %d\n", (u32) h, g_jailbroken);
        if (g_jailbroken == 1) {
            h = load_usbd_module();
            sys_sdk_unjailbreak(&g_jb);
        }
    }
    if (h < 0) {
        final_printf("libSceUsbd: chargement 0x%08x\n", (u32) h);
        g_usbd_step = 1; g_usbd_err = h;
        return -1;
    }
    int sym = 0;
#define SYM(field, name) sym++; if (sceKernelDlsym(h, name, (void **) &U.field) != 0 || !U.field) { g_usbd_step = 2; g_usbd_err = sym; return -1; }
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
    int32_t r = U.Init();
    if (r < 0) {
        g_usbd_step = 3; g_usbd_err = r;
        return -1;
    }
    g_usbd_step = 0;
    return 0;
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

// ---------------------------------------------------------------- son du jeu
// Repris d'Audio4Free : on intercepte sceAudioOutOpen pour connaître le format du port,
// puis sceAudioOutOutput(s) pour copier chaque bloc, le convertir en s16 stéréo 48 kHz
// et le pousser vers le Bluetooth. Le jeu appelle toujours la vraie fonction (son TV intact).
//
// Le tampon Bluetooth n'a qu'une entrée : on capture le port principal du jeu
// (premier port MAIN ouvert). Mixer plusieurs ports viendra si un jeu en a besoin.
//
// Interception par la table d'imports (GOT) des modules du jeu, pas par les détours
// GoldHEN (HOOK32) : le détour recopie tel quel le début de sceAudioOutOutput dans son
// stub, sans corriger les instructions relatives à RIP ; l'original appelé par
// HOOK_CONTINUE rendait alors la main sans attendre et le jeu tournait en boucle
// (5 millions d'appels/s, écran noir puis plantage). Ici le code de libSceAudioOut
// reste intact : nos fonctions appellent directement les vraies.

#define SCE_AUDIO_OUT_PORT_TYPE_MAIN 0
#define MAX_GRAIN 2048

typedef struct {
    s32 handle;
    const void *ptr;
} AudioOutOutputParam;

s32 sceAudioOutOpen(s32 userId, s32 type, s32 index, u32 len, u32 freq, u32 param);
s32 sceAudioOutClose(s32 handle);
s32 sceAudioOutOutput(s32 handle, const void *ptr);
s32 sceAudioOutOutputs(AudioOutOutputParam *param, u32 num);


static volatile s32 g_port = -1;      // port capturé
static u32 g_port_grain, g_port_fmt;
static int16_t g_pcm[MAX_GRAIN * 2];

// compteurs pour le journal de debug (écrits par le thread audio du jeu, lus par le journal)
static volatile u32 g_n_open, g_n_output, g_n_outputs, g_n_captured;
static volatile s32 g_open_log[8][5];   // type, len, freq, fmt, handle des premiers sceAudioOutOpen

// TV : par défaut on envoie du silence au vrai sceAudioOut pour le port capturé, mais
// seulement pendant que les AirPods jouent (si le Bluetooth décroche, la TV reprend).
// Les autres ports (haut-parleur de la manette, ports non capturés) restent intacts.
static volatile s32 g_tv_on;                        // fichier TV_PATH, relu chaque seconde
static const u8 g_silence[MAX_GRAIN * 8 * 4];       // un bloc 7.1 float de la taille maximale

// Copie le bloc vers le Bluetooth ; renvoie le pointeur à passer au vrai sceAudioOut.
static const void *capture(s32 handle, const void *ptr) {
    if (handle != g_port || !ptr) return ptr;
    if (a4f_to_s16_stereo(ptr, g_port_fmt, g_port_grain, g_pcm) == 0) {
        a4f_bt_push_pcm(g_pcm, g_port_grain);
        g_n_captured++;
    }
    return (!g_tv_on && a4f_bt_is_streaming()) ? (const void *) g_silence : ptr;
}

s32 sceAudioOutOpen_hook(s32 userId, s32 type, s32 index, u32 len, u32 freq, u32 param) {
    s32 handle = sceAudioOutOpen(userId, type, index, len, freq, param);
    u32 fmt = param & 0xFF;
    final_printf("sceAudioOutOpen type=%d len=%u freq=%u fmt=%u -> 0x%08x\n", type, len, freq, fmt, handle);
    if (g_n_open < 8) {
        volatile s32 *e = g_open_log[g_n_open];
        e[0] = type; e[1] = (s32) len; e[2] = (s32) freq; e[3] = (s32) fmt; e[4] = handle;
    }
    g_n_open++;
    if (handle >= 0 && g_port < 0 && type == SCE_AUDIO_OUT_PORT_TYPE_MAIN &&
        freq == 48000 && len <= MAX_GRAIN && a4f_channels(fmt) != 0) {
        g_port_grain = len;
        g_port_fmt = fmt;
        g_port = handle;
        final_printf("port capture : 0x%08x (%u trames, format %u)\n", handle, len, fmt);
    }
    return handle;
}

s32 sceAudioOutClose_hook(s32 handle) {
    if (handle == g_port) g_port = -1;
    return sceAudioOutClose(handle);
}

s32 sceAudioOutOutput_hook(s32 handle, const void *ptr) {
    g_n_output++;
    return sceAudioOutOutput(handle, capture(handle, ptr));
}

s32 sceAudioOutOutputs_hook(AudioOutOutputParam *param, u32 num) {
    g_n_outputs++;
    // copie de la liste du jeu pour remplacer notre port par du silence sans toucher la sienne
    AudioOutOutputParam p[16];
    if (!param || num > 16) {
        for (u32 i = 0; param && i < num; i++) capture(param[i].handle, param[i].ptr);
        return sceAudioOutOutputs(param, num);
    }
    for (u32 i = 0; i < num; i++) {
        p[i].handle = param[i].handle;
        p[i].ptr = capture(param[i].handle, param[i].ptr);
    }
    return sceAudioOutOutputs(p, num);
}

// ---- redirection des imports

#define NUM_IMPORTS 4
static void *g_real[NUM_IMPORTS];   // vraies fonctions (vues par notre propre GOT, jamais modifiée)
static void *g_hook[NUM_IMPORTS];
static volatile u32 g_patched[NUM_IMPORTS];   // entrées redirigées par fonction
static char g_patched_modules[256];           // modules du jeu touchés, pour le journal

static void imports_init(void) {
    g_real[0] = (void *) sceAudioOutOpen;    g_hook[0] = (void *) sceAudioOutOpen_hook;
    g_real[1] = (void *) sceAudioOutClose;   g_hook[1] = (void *) sceAudioOutClose_hook;
    g_real[2] = (void *) sceAudioOutOutput;  g_hook[2] = (void *) sceAudioOutOutput_hook;
    g_real[3] = (void *) sceAudioOutOutputs; g_hook[3] = (void *) sceAudioOutOutputs_hook;
}

// Cherche dans les segments de données de chaque module chargé (sauf le nôtre et
// libSceAudioOut) les pointeurs vers les vraies fonctions et les remplace par nos
// fonctions. Rappelée plusieurs fois : un module chargé plus tard est aussi traité.
static void replace_pointers(void **from, void **to, int count_patches) {
    static OrbisKernelModule mods[1024];
    size_t n = 0;
    if (sceKernelGetModuleList(mods, sizeof(mods) / sizeof(mods[0]), &n) != 0) return;
    for (size_t m = 0; m < n && m < 1024; m++) {
        OrbisKernelModuleInfo info;
        memset(&info, 0, sizeof(info));
        info.size = sizeof(info);
        if (sceKernelGetModuleInfo(mods[m], &info) != 0) continue;
        if (strstr(info.name, "a4f_bt") || strstr(info.name, "libSceAudioOut")) continue;
        int hits = 0;
        for (u32 sgi = 0; sgi < info.segmentCount && sgi < 4; sgi++) {
            OrbisKernelModuleSegmentInfo *sg = &info.segmentInfo[sgi];
            if ((sg->prot & 4) || !(sg->prot & 1)) continue;   // données lisibles, pas le code
            uint64_t *w = (uint64_t *) sg->address;
            for (u32 i = 0; i < sg->size / 8; i++) {
                for (int f = 0; f < NUM_IMPORTS; f++) {
                    if (w[i] != (uint64_t) from[f]) continue;
                    if (!(sg->prot & 2)) {
                        void *page = (void *) ((uintptr_t) &w[i] & ~(uintptr_t) 0x3fff);
                        if (sceKernelMprotect(page, 0x8000, sg->prot | 2) != 0) continue;
                    }
                    w[i] = (uint64_t) to[f];
                    if (count_patches) g_patched[f]++;
                    hits++;
                }
            }
        }
        if (hits && count_patches && strlen(g_patched_modules) + strlen(info.name) + 2 < sizeof(g_patched_modules)) {
            strcat(g_patched_modules, info.name);
            strcat(g_patched_modules, " ");
        }
    }
}

static void patch_imports(void)   { replace_pointers(g_real, g_hook, 1); }
static void unpatch_imports(void) { replace_pointers(g_hook, g_real, 0); }

// ---------------------------------------------------------------- plugin

static void on_status(const char *msg) {
    Notify(TEX_ICON_SYSTEM, "A4F BT\n%s", msg);
}

// Journal de debug : une ligne par seconde, fichier refermé à chaque fois (survit à un plantage).
// Lit un entier (premier nombre du fichier) ; -1 si le fichier manque ou ne contient pas de
// nombre. Pas de fscanf : avec les en-têtes musl et la libc PS4, le volume
// changé par FTP pendant le jeu restait à l'ancienne valeur (v7).
static volatile s32 g_last_read = -2;   // dernière valeur lue dans VOLUME_PATH, pour le journal

static int read_int_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char buf[16];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    int v = -1;
    for (size_t i = 0; i < n; i++) {
        if (buf[i] >= '0' && buf[i] <= '9') v = (v < 0 ? 0 : v * 10) + (buf[i] - '0');
        else if (v >= 0) break;
    }
    return v;
}

// Même principe pour TV_PATH (0 ou 1, 0 par défaut).
static void read_tv(void) {
    FILE *f = fopen(TV_PATH, "r");
    if (!f) {
        f = fopen(TV_PATH, "w");
        if (f) {
            fprintf(f, "%d\n", (int) g_tv_on);
            fclose(f);
        }
        return;
    }
    fclose(f);
    int v = read_int_file(TV_PATH);
    if (v >= 0) g_tv_on = v != 0;
}

// Lit le volume dans VOLUME_PATH (un nombre de 0 à 100) ; crée le fichier s'il manque,
// pour qu'on le trouve par FTP. Modifiable pendant le jeu.
static void read_volume(void) {
    FILE *f = fopen(VOLUME_PATH, "r");
    if (!f) {
        sceKernelMkdir(DATA_DIR, 0777);
        f = fopen(VOLUME_PATH, "w");
        if (f) {
            fprintf(f, "%d\n", a4f_bt_get_volume());
            fclose(f);
        }
        return;
    }
    fclose(f);
    int v = read_int_file(VOLUME_PATH);
    g_last_read = v;
    if (v >= 0 && v != a4f_bt_get_volume()) a4f_bt_set_volume(v);
}

static void *debug_thread(void *arg) {
    (void) arg;
    FILE *f = fopen(DEBUG_PATH, "w");
    if (f) fclose(f);
    u32 logged_opens = 0;
    for (u32 t = 0;; t++) {
        // modules ou imports résolus après le démarrage : on repasse de temps en temps
        if (t < 30 || t % 10 == 0) patch_imports();
        read_volume();
        read_tv();
        char bt[256];
        a4f_bt_debug(bt, sizeof(bt));
        f = fopen(DEBUG_PATH, "a");
        if (f) {
            while (logged_opens < g_n_open && logged_opens < 8) {
                volatile s32 *e = g_open_log[logged_opens++];
                fprintf(f, "sceAudioOutOpen type %d len %d freq %d fmt %d -> 0x%08x\n", e[0], e[1], e[2], e[3], (u32) e[4]);
            }
            fprintf(f, "%3us open %u output %u outputs %u captures %u port 0x%08x usbd %d/%d/0x%x jb %d tv %d lu %d | imports %u/%u/%u/%u [%s] | %s\n",
                    t, g_n_open, g_n_output, g_n_outputs, g_n_captured, (u32) g_port, g_usbd_tries, g_usbd_step, (u32) g_usbd_err, g_jailbroken, (int) g_tv_on, g_last_read,
                    g_patched[0], g_patched[1], g_patched[2], g_patched[3], g_patched_modules, bt);
            fclose(f);
        }
        sceKernelSleep(1);
    }
    return NULL;
}

static void *bt_thread(void *arg) {
    (void) arg;
    sceKernelSleep(5);   // laisser le jeu démarrer
    // quelques essais : l'échec vu après un redémarrage de la console est peut-être passager
    while (load_usbd() < 0) {
        if (g_usbd_tries >= 10) {
            Notify(TEX_ICON_SYSTEM, "A4F BT\nlibSceUsbd indisponible\netape %d, 0x%08x, jb %d", g_usbd_step, (u32) g_usbd_err, g_jailbroken);
            return NULL;
        }
        sceKernelSleep(2);
    }
    sceKernelMkdir(DATA_DIR, 0777);
    a4f_bt_config_t cfg = {
        .usb = &usb_api, .vid = 0x0a12, .pid = 0x0001,
        .data_dir = DATA_DIR, .dump_path = DUMP_PATH,
        .status = on_status,
    };
    // son du jeu (silence tant que le jeu ne joue rien) ; fichier "tone" présent = tonalité de test
    FILE *tone = fopen(TONE_FLAG_PATH, "r");
    a4f_bt_set_tone(tone != NULL);
    if (tone) fclose(tone);
    a4f_bt_run(&cfg);
    return NULL;
}

s32 attr_public plugin_load(s32 argc, const char *argv[]) {
    final_printf("[GoldHEN] <%s\\Ver.0x%08x> %s\n", g_pluginName, g_pluginVersion, __func__);
    // imports redirigés tout de suite : le jeu ouvre ses ports audio dès son démarrage
    imports_init();
    patch_imports();
    OrbisPthread th, dbg;
    scePthreadCreate(&dbg, NULL, debug_thread, NULL, "a4f_bt_dbg");
    scePthreadCreate(&th, NULL, bt_thread, NULL, "a4f_bt");
    return 0;
}

s32 attr_public plugin_unload(s32 argc, const char *argv[]) {
    final_printf("[GoldHEN] <%s\\Ver.0x%08x> %s\n", g_pluginName, g_pluginVersion, __func__);
    unpatch_imports();
    a4f_bt_stop();
    return 0;
}

s32 attr_module_hidden module_start(s64 argc, const void *args) {
    return 0;
}

s32 attr_module_hidden module_stop(s64 argc, const void *args) {
    return 0;
}
