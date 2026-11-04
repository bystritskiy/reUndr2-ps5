/* reUndr 2 PS5, MIT. Is a default.xbe the build of the game the port was made from? */
#ifndef NFSU2_XBE_CHECK_H
#define NFSU2_XBE_CHECK_H
#include <stdint.h>
typedef struct {
    uint32_t raw_size;
    uint8_t digest[20];      /* as stored in the section header */
    const char *data_sha256; /* NULL: the port runs this section recompiled */
} Nfsu2XbeSection;
/* 1 when path is the file with full_sha256, or the same build with only the
 * header or recompiled code patched (region, media, an untouched disc).
 * found gets the file's SHA-256, or "" when it cannot be read. */
int nfsu2_xbe_check(const char *path, const char *full_sha256,
                    const Nfsu2XbeSection *ref, unsigned count, char found[65]);
#endif
