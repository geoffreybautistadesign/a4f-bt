// A4F BT : source A2DP (BTstack) qui envoie du son à un casque Bluetooth (AirPods) via
// un dongle USB. Même code sur Mac (test) et PS4 (plugin GoldHEN) ; seule l'API USB change.
//
// Comportement : à l'allumage du dongle, reconnexion au dernier casque connu ; sinon
// recherche d'un casque en mode appairage. En cas d'échec ou de déconnexion, on réessaie
// tout seul (reconnexion et recherche en alternance).
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "hci_transport_a4f_usb.h"

typedef struct {
    const a4f_usb_api_t *usb;
    uint16_t vid, pid;               // dongle (CSR8510 : 0a12:0001)
    const char *data_dir;            // dossier des clés d'appairage, avec '/' final
    const char *dump_path;           // journal HCI PacketLogger, ou NULL
    void (*status)(const char *msg); // changement d'état (notification PS4 / printf)
} a4f_bt_config_t;

// Lance BTstack et ne rend la main qu'après a4f_bt_stop(). Renvoie 0 si arrêt normal.
int  a4f_bt_run(const a4f_bt_config_t *config);
void a4f_bt_stop(void);

// Son envoyé : tonalité de test, ou PCM s16 stéréo 48 kHz poussé par a4f_bt_push_pcm.
void     a4f_bt_set_tone(bool on);
// Volume envoyé au casque, 0 à 100 (30 par défaut). Appelable depuis n'importe quel thread.
void     a4f_bt_set_volume(int percent);
int      a4f_bt_get_volume(void);
// Vrai quand le casque reçoit le son (flux A2DP démarré).
bool     a4f_bt_is_streaming(void);
uint32_t a4f_bt_push_pcm(const int16_t *pcm, uint32_t frames);   // appelable depuis n'importe quel thread

// Une ligne d'état pour un journal de débogage (appelable depuis n'importe quel thread).
void a4f_bt_debug(char *buf, int len);
