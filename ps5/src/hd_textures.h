#ifndef NFSU2_HD_TEXTURES_H
#define NFSU2_HD_TEXTURES_H
#include <stdint.h>
typedef struct {
    uint32_t color, source_w, source_h, source_bytes, sample_hash;
    uint32_t width, height, hash_lo, hash_hi;
    char file[48];
} NfsHdTexture;
const NfsHdTexture *nfsu2_hd_find(const void *source, uint32_t color,
                                uint32_t w, uint32_t h, uint32_t bytes,
                                uint32_t sample_hash);
void *nfsu2_hd_load(const NfsHdTexture *texture);
#endif
