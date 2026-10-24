// Réception des paquets audio A4F (a4f_protocol.h) en UDP, version POSIX pour le prototype Mac.
// Sur PS4 ce module disparaît : les hooks sceAudioOut appelleront a4f_ring_push directement.
#pragma once

#include <stdint.h>
#include "a4f_ring.h"

#define A4F_UDP_PORT 9051

typedef struct {
    uint32_t packets;   // paquets audio valides reçus
    uint32_t lost;      // trous dans les numéros de séquence
} a4f_udp_stats_t;

int  a4f_udp_start(a4f_ring_t *ring, uint16_t port);
void a4f_udp_get_stats(a4f_udp_stats_t *out);
