#include "a4f_convert.h"

static int is_float(uint32_t fmt) {
    return fmt == A4F_FMT_FLOAT_MONO || fmt == A4F_FMT_FLOAT_STEREO ||
           fmt == A4F_FMT_FLOAT_8CH || fmt == A4F_FMT_FLOAT_8CH_STD;
}

int a4f_channels(uint32_t fmt) {
    switch (fmt) {
    case A4F_FMT_S16_MONO:
    case A4F_FMT_FLOAT_MONO:
        return 1;
    case A4F_FMT_S16_STEREO:
    case A4F_FMT_FLOAT_STEREO:
        return 2;
    case A4F_FMT_S16_8CH:
    case A4F_FMT_FLOAT_8CH:
    case A4F_FMT_S16_8CH_STD:
    case A4F_FMT_FLOAT_8CH_STD:
        return 8;
    default:
        return 0;
    }
}

int a4f_frame_bytes(uint32_t fmt) {
    return a4f_channels(fmt) * (is_float(fmt) ? 4 : 2);
}

static inline float sample_at(const void *src, int is_f, uint32_t idx) {
    if (is_f)
        return ((const float *)src)[idx];
    return ((const int16_t *)src)[idx] * (1.0f / 32768.0f);
}

static inline int16_t to_s16(float v) {
    if (v > 1.0f) v = 1.0f;
    if (v < -1.0f) v = -1.0f;
    return (int16_t)(v * 32767.0f);
}

int a4f_to_s16_stereo(const void *src, uint32_t fmt, uint32_t frames, int16_t *dst) {
    int ch = a4f_channels(fmt);
    int is_f = is_float(fmt);
    if (ch == 0)
        return -1;

    if (fmt == A4F_FMT_S16_STEREO) {
        const int16_t *s = (const int16_t *)src;
        for (uint32_t i = 0; i < frames * 2; i++)
            dst[i] = s[i];
        return 0;
    }

    for (uint32_t i = 0; i < frames; i++) {
        float l, r;
        if (ch == 1) {
            l = r = sample_at(src, is_f, i);
        } else if (ch == 2) {
            l = sample_at(src, is_f, i * 2);
            r = sample_at(src, is_f, i * 2 + 1);
        } else {
            // Ordre 7.1 : L, R, C, LFE, Ls, Rs, Lb, Rb. Downmix ITU sans LFE,
            // normalisé pour que la somme des gains d'un côté fasse 1.
            uint32_t b = i * 8;
            float c  = sample_at(src, is_f, b + 2);
            float ls = sample_at(src, is_f, b + 4);
            float rs = sample_at(src, is_f, b + 5);
            float lb = sample_at(src, is_f, b + 6);
            float rb = sample_at(src, is_f, b + 7);
            const float k = 0.7071f, norm = 1.0f / (1.0f + 3.0f * 0.7071f);
            l = (sample_at(src, is_f, b + 0) + k * c + k * ls + k * lb) * norm;
            r = (sample_at(src, is_f, b + 1) + k * c + k * rs + k * rb) * norm;
        }
        dst[i * 2]     = to_s16(l);
        dst[i * 2 + 1] = to_s16(r);
    }
    return 0;
}
