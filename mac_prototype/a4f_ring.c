#include "a4f_ring.h"

#include <string.h>

#define MS_TO_FRAMES(ms) ((uint32_t)(ms) * (A4F_RING_RATE / 1000))

void a4f_ring_init(a4f_ring_t *r){
    memset(r, 0, sizeof(*r));
    r->pos_frac = 1u << 16;   // force la lecture d'une première trame
}

uint32_t a4f_ring_fill(a4f_ring_t *r){
    return atomic_load_explicit(&r->wr, memory_order_acquire) -
           atomic_load_explicit(&r->rd, memory_order_acquire);
}

uint32_t a4f_ring_push(a4f_ring_t *r, const int16_t *pcm, uint32_t n){
    uint32_t wr = atomic_load_explicit(&r->wr, memory_order_relaxed);
    uint32_t rd = atomic_load_explicit(&r->rd, memory_order_acquire);
    uint32_t space = A4F_RING_FRAMES - (wr - rd);
    if (n > space){
        atomic_fetch_add_explicit(&r->overflows, n - space, memory_order_relaxed);
        n = space;
    }
    for (uint32_t i = 0; i < n; i++){
        uint32_t k = (wr + i) & (A4F_RING_FRAMES - 1);
        r->buf[2 * k]     = pcm[2 * i];
        r->buf[2 * k + 1] = pcm[2 * i + 1];
    }
    atomic_store_explicit(&r->wr, wr + n, memory_order_release);
    return n;
}

// Lit une trame du tampon dans cur ; renvoie 0 si vide.
static int read_frame(a4f_ring_t *r, int16_t cur[2]){
    uint32_t rd = atomic_load_explicit(&r->rd, memory_order_relaxed);
    uint32_t wr = atomic_load_explicit(&r->wr, memory_order_acquire);
    if (wr == rd) return 0;
    uint32_t k = rd & (A4F_RING_FRAMES - 1);
    cur[0] = r->buf[2 * k];
    cur[1] = r->buf[2 * k + 1];
    atomic_store_explicit(&r->rd, rd + 1, memory_order_release);
    return 1;
}

void a4f_ring_pull(a4f_ring_t *r, int16_t *out, uint32_t n, uint32_t out_rate){
    uint32_t fill = a4f_ring_fill(r);

    if (!r->playing){
        if (fill < MS_TO_FRAMES(A4F_RING_PREFILL_MS)){
            memset(out, 0, n * 4);
            return;
        }
        r->playing = 1;
    }

    // Dérive d'horloge (le producteur va un peu plus vite que le Bluetooth) :
    // on revient au niveau de pré-remplissage en jetant le surplus.
    if (fill > MS_TO_FRAMES(A4F_RING_MAX_MS)){
        uint32_t drop = fill - MS_TO_FRAMES(A4F_RING_PREFILL_MS);
        atomic_fetch_add_explicit(&r->rd, drop, memory_order_release);
        r->dropped += drop;
    }

    // Rééchantillonnage linéaire 48 kHz -> out_rate (pas = 1.0 si out_rate = 48 kHz).
    uint32_t step = (uint32_t)(((uint64_t)A4F_RING_RATE << 16) / out_rate);
    for (uint32_t i = 0; i < n; i++){
        while (r->pos_frac >= (1u << 16)){
            r->prev[0] = r->cur[0];
            r->prev[1] = r->cur[1];
            if (!read_frame(r, r->cur)){
                // plus de son : silence jusqu'au prochain pré-remplissage
                r->underruns++;
                r->playing = 0;
                r->pos_frac = 1u << 16;
                memset(out + 2 * i, 0, (n - i) * 4);
                return;
            }
            r->pos_frac -= 1u << 16;
        }
        int32_t f = (int32_t)r->pos_frac;
        out[2 * i]     = (int16_t)(r->prev[0] + (int32_t)(((int64_t)(r->cur[0] - r->prev[0]) * f) >> 16));
        out[2 * i + 1] = (int16_t)(r->prev[1] + (int32_t)(((int64_t)(r->cur[1] - r->prev[1]) * f) >> 16));
        r->pos_frac += step;
    }
}
