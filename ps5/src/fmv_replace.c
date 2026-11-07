/* Host-side movie picture replacement. The game keeps decoding and drawing its
 * own VP6 movie, which keeps the audio, timing, skipping and the 16:9 crop
 * (upstream/src/movie_crop.c) its own. While a pack for the open movie exists,
 * the movie texture is shown from frame N of the pack after the game decoded
 * frame N. Decoding, conversion and drawing run on the game's thread, so the
 * last decoded frame is the one in the texture.
 *
 * A pack is <app>/hd/fmv/HASH.fmv, HASH the ZDIR hash of the upper-case movie
 * path, so the movies the disc lists only by hash need no names: a 40-byte
 * header, then the frames of an IVF file. One pack frame per MV0K/MV0F chunk.
 * Without a pack the movie plays as on the disc. NFSU2_FMV=0 turns this off,
 * NFSU2_HD_DIR names a folder whose fmv/ comes first. */
#include "fmv_replace.h"
#include "fmv_video.h"
#include <fcntl.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#ifndef NFS_HD_ROOT
#define NFS_HD_ROOT "/app0/hd"
#endif
#define VERSION 1
#define SOURCE_W 640u
#define SOURCE_H 480u
#define MOVIE_BYTES (SOURCE_W * SOURCE_H * 4u)

typedef struct {
    char magic[4];
    uint32_t version, source_w, source_h, width, height, frames, frame_bytes;
    uint64_t data_offset;
} NfsFmvHeader;
_Static_assert(sizeof(NfsFmvHeader) == 40, "FMV pack header ABI");

/* upstream/src/recomp_manual.c: the last row the game's colour conversion wrote. */
extern uint32_t nfsu2_movie_row;

static int fd = -1;
static NfsFmvHeader header;
static uint32_t hash_open;
static atomic_uint decoded;
static int restart;
static int disabled = -1;

static uint32_t path_hash(const char *s)
{
    uint32_t h = 0xFFFFFFFFu;
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
        h = h * 33u + c;
    }
    return h;
}

static int valid(const NfsFmvHeader *h, off_t size)
{
    return !memcmp(h->magic, "N2MV", 4) && h->version == VERSION
        && h->source_w == SOURCE_W && h->source_h == SOURCE_H
        && h->width && h->height && h->width <= 4096 && h->height <= 4096
        && !(h->width & 1) && !(h->height & 1)
        && h->frame_bytes == h->width * h->height * 4
        && h->frames && h->frames <= 1u << 20
        && h->data_offset >= sizeof *h && (off_t)h->data_offset < size;
}

static int open_pack(const char *root, uint32_t hash)
{
    char path[320];
    snprintf(path, sizeof path, "%s/fmv/%08X.fmv", root, hash);
    int f = open(path, O_RDONLY);
    if (f < 0) return -1;
    const off_t size = lseek(f, 0, SEEK_END);
    if (pread(f, &header, sizeof header, 0) != (ssize_t)sizeof header || !valid(&header, size)) {
        fprintf(stderr, "[FMV] %s: not a pack\n", path);
        close(f);
        return -1;
    }
    fprintf(stderr, "[FMV] %08X: %u frames %ux%u\n", hash, header.frames, header.width, header.height);
    return f;
}

/* "%sMOVIES\\%s%s": the prefix before MOVIES is not part of the ZDIR name. */
void nfsu2_fmv_movie(const char *path)
{
    if (disabled < 0) {
        const char *e = getenv("NFSU2_FMV");
        disabled = e && !strcmp(e, "0");
    }
    atomic_store(&decoded, 0);
    restart = 1;
    if (disabled) return;
    const char *name = NULL;
    for (const char *p = path; *p && !name; p++)
        if (!strncasecmp(p, "MOVIES\\", 7)) name = p;
    const uint32_t hash = path_hash(name ? name : path);
    if (fd >= 0 && hash == hash_open) return;
    if (fd >= 0) {
        nfsu2_fmv_video_stop();
        close(fd);
        fd = -1;
    }
    const char *outside = getenv("NFSU2_HD_DIR");
    if (outside && *outside && strcmp(outside, NFS_HD_ROOT)) fd = open_pack(outside, hash);
    if (fd < 0) fd = open_pack(NFS_HD_ROOT, hash);
    hash_open = fd >= 0 ? hash : 0;
}

void nfsu2_fmv_decoded(void)
{
    atomic_fetch_add(&decoded, 1);
}

static uint32_t current(uint32_t va, uint32_t color, uint32_t w, uint32_t h)
{
    if (fd < 0 || color != 0x12 || w != SOURCE_W || h != SOURCE_H) return 0;
    if (nfsu2_movie_row < va || nfsu2_movie_row >= va + MOVIE_BYTES) return 0;
    const uint32_t n = atomic_load(&decoded);
    return n <= header.frames ? n : 0;
}

uint32_t nfsu2_fmv_tag(uint32_t va, uint32_t color, uint32_t w, uint32_t h)
{
    const uint32_t n = current(va, color, w, h);
    return n ? n * 2654435761u | 1u : 0;
}

const void *nfsu2_fmv_picture(uint32_t va, uint32_t color, uint32_t w, uint32_t h,
                              uint32_t *out_w, uint32_t *out_h)
{
    const uint32_t n = current(va, color, w, h);
    if (!n) return NULL;
    const void *pixels = nfsu2_fmv_video_frame(fd, header.data_offset, header.width, header.height,
                                               n - 1, restart);
    restart = 0;
    if (!pixels) return NULL;
    *out_w = header.width;
    *out_h = header.height;
    return pixels;
}
