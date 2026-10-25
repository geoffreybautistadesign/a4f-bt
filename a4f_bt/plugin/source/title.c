#include "a4f_title.h"

#include <string.h>

// Format PSF : en-tête de 20 octets (magic "\0PSF", version, début de la table des clés,
// début des données, nombre d'entrées), puis une entrée de 16 octets par clé.
static uint32_t rd32(const uint8_t *p) {
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24);
}

static uint16_t rd16(const uint8_t *p) {
    return (uint16_t) (p[0] | (p[1] << 8));
}

int a4f_sfo_title_id(const uint8_t *sfo, size_t len, char out[16]) {
    if (len < 20 || memcmp(sfo, "\0PSF", 4) != 0) return -1;
    uint32_t keys = rd32(sfo + 8), data = rd32(sfo + 12), n = rd32(sfo + 16);
    if (keys > len || data > len || n > (len - 20) / 16) return -1;
    for (uint32_t i = 0; i < n; i++) {
        const uint8_t *e = sfo + 20 + 16 * i;
        uint32_t key = keys + rd16(e), dlen = rd32(e + 4), doff = data + rd32(e + 12);
        if (key + sizeof("TITLE_ID") > len || memcmp(sfo + key, "TITLE_ID", sizeof("TITLE_ID")) != 0) continue;
        if (doff > len || dlen > len - doff || dlen == 0) return -1;
        size_t k = 0;
        while (k < dlen && k < 15 && sfo[doff + k]) {
            out[k] = (char) sfo[doff + k];
            k++;
        }
        out[k] = '\0';
        return k ? 0 : -1;
    }
    return -1;
}

int a4f_is_game_title(const char *title_id) {
    return memcmp(title_id, "CUSA", 4) == 0 || memcmp(title_id, "PCAS", 4) == 0 ||
           memcmp(title_id, "PLAS", 4) == 0;
}
