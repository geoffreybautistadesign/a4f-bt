// Tampon circulaire PCM s16 stéréo 48 kHz, un producteur / un consommateur, sans verrou.
// Producteur : thread qui reçoit le son (UDP sur Mac, hook sceAudioOut sur PS4).
// Consommateur : run loop BTstack qui alimente l'encodeur SBC.
#pragma once

#include <stdint.h>
#include <stdatomic.h>

#define A4F_RING_RATE        48000
#define A4F_RING_FRAMES      (1u << 15)   // ~680 ms, puissance de 2
#define A4F_RING_PREFILL_MS  40           // on attend 40 ms de son avant de jouer
#define A4F_RING_MAX_MS      120          // au-delà on jette le surplus (dérive d'horloge)

typedef struct {
    int16_t buf[A4F_RING_FRAMES * 2];
    _Atomic uint32_t wr;     // en trames, ne fait qu'augmenter (modulo 2^32)
    _Atomic uint32_t rd;
    // état consommateur
    int      playing;        // 0 tant que le pré-remplissage n'est pas atteint
    uint32_t pos_frac;       // position fractionnaire 16.16 pour le rééchantillonnage
    int16_t  prev[2], cur[2]; // deux dernières trames lues, pour l'interpolation
    // statistiques
    _Atomic uint32_t overflows; // trames perdues car tampon plein
    uint32_t underruns;
    uint32_t dropped;           // trames jetées pour rattraper la dérive
} a4f_ring_t;

void     a4f_ring_init(a4f_ring_t *r);
uint32_t a4f_ring_fill(a4f_ring_t *r);   // trames disponibles
// Producteur : ajoute n trames entrelacées L/R. Renvoie le nombre écrit.
uint32_t a4f_ring_push(a4f_ring_t *r, const int16_t *pcm, uint32_t n);
// Consommateur : produit exactement n trames à out_rate Hz (silence si sous-alimentation).
void     a4f_ring_pull(a4f_ring_t *r, int16_t *out, uint32_t n, uint32_t out_rate);
