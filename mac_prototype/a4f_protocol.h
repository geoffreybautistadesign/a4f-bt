// Audio4Free - protocole réseau partagé entre le plugin PS4 et le récepteur.
// Tout est en little-endian (PS4 et Mac/iPhone sont tous little-endian).
#pragma once

#include <stdint.h>

#define A4F_VERSION       1
#define A4F_PS4_PORT      9050   // port UDP sur lequel le plugin écoute les "hello"

#define A4F_MAGIC_HELLO   0x48463441u  // "A4FH" : récepteur -> PS4
#define A4F_MAGIC_AUDIO   0x41463441u  // "A4FA" : PS4 -> récepteur

#define A4F_FLAG_MUTE_TV  0x01   // couper le son sur la TV pendant le streaming

#define A4F_SAMPLE_RATE   48000
#define A4F_CHUNK_FRAMES  256    // 256 trames stéréo s16 = 1024 octets, tient dans un MTU

// Envoyé par le récepteur toutes les secondes. Le plugin stream vers
// l'adresse/port source du dernier hello reçu, et s'arrête après 3 s sans hello.
typedef struct __attribute__((packed)) {
    uint32_t magic;      // A4F_MAGIC_HELLO
    uint8_t  version;    // A4F_VERSION
    uint8_t  flags;      // A4F_FLAG_*
    uint16_t reserved;
} A4fHello;

// En-tête de chaque paquet audio, suivi de frames * 2 * int16 (L, R entrelacés).
typedef struct __attribute__((packed)) {
    uint32_t magic;      // A4F_MAGIC_AUDIO
    uint8_t  version;
    uint8_t  port;       // index du port audio du jeu (un jeu peut en ouvrir plusieurs)
    uint16_t frames;
    uint32_t seq;        // numéro de séquence par port, pour détecter les pertes
    uint32_t reserved;
} A4fAudioHeader;
