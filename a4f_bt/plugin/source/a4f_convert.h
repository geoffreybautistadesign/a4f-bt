// Conversion des formats sceAudioOut vers du s16 stéréo.
// Code C pur, sans dépendance PS4, pour pouvoir le tester sur Mac/PC.
#pragma once

#include <stdint.h>

// Valeurs de "param & 0xFF" passées à sceAudioOutOpen.
enum {
    A4F_FMT_S16_MONO      = 0,
    A4F_FMT_S16_STEREO    = 1,
    A4F_FMT_S16_8CH       = 2,
    A4F_FMT_FLOAT_MONO    = 3,
    A4F_FMT_FLOAT_STEREO  = 4,
    A4F_FMT_FLOAT_8CH     = 5,
    A4F_FMT_S16_8CH_STD   = 6,
    A4F_FMT_FLOAT_8CH_STD = 7,
};

// Nombre de canaux du format, ou 0 si format inconnu.
int a4f_channels(uint32_t fmt);

// Taille d'une trame (tous canaux) en octets, ou 0 si format inconnu.
int a4f_frame_bytes(uint32_t fmt);

// Convertit `frames` trames de `src` (format `fmt`) en s16 stéréo entrelacé dans `dst`.
// Le 7.1 est mixé en stéréo. Retourne 0 si OK, -1 si format inconnu.
int a4f_to_s16_stereo(const void *src, uint32_t fmt, uint32_t frames, int16_t *dst);
