#include "fmv_replace.h"
#include "fmv_video.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <stdint.h>

#ifndef NFS_HD_ROOT
#error Test root required
#endif
#define FRAMES 120
#define SIZE 64
#define MOVIE_VA 0x00800000u

/* recomp_manual.c in the game: the last row the colour conversion wrote. */
uint32_t nfsu2_movie_row;

static uint32_t path_hash(const char *s)
{
    uint32_t h = 0xFFFFFFFFu;
    for (; *s; s++) h = h * 33u + (unsigned char)*s;
    return h;
}

/* test.ivf: grey, luma 30+frame in its top half and 130+frame below. */
static void write_pack(const char *name, const char *magic, uint32_t version)
{
    char path[512];
    mkdir(NFS_HD_ROOT "/fmv", 0755);
    snprintf(path, sizeof path, NFS_HD_ROOT "/fmv/%08X.fmv", path_hash(name));
    FILE *in = fopen(NFS_HD_ROOT "/test.ivf", "rb"); assert(in);
    FILE *o = fopen(path, "wb"); assert(o);
    const uint32_t header[8] = {0, version, 640, 480, SIZE, SIZE, FRAMES, SIZE * SIZE * 4};
    const uint64_t data_offset = 40;
    fwrite(header, sizeof header, 1, o); fwrite(&data_offset, sizeof data_offset, 1, o);
    fseek(o, 0, SEEK_SET); fwrite(magic, 4, 1, o); fseek(o, 40, SEEK_SET);
    unsigned char ivf_header[32];
    assert(fread(ivf_header, sizeof ivf_header, 1, in) == 1);
    int c;
    while ((c = fgetc(in)) != EOF) fputc(c, o);
    fclose(in); fclose(o);
}

/* The grey the pack shows after the game decoded `decoded` frames, or -1. */
static int shown(uint32_t va, uint32_t color, uint32_t w, uint32_t h)
{
    uint32_t pw = 0, ph = 0;
    const uint32_t *p = nfsu2_fmv_picture(va, color, w, h, &pw, &ph);
    if (!p) return -1;
    assert(pw == SIZE && ph == SIZE);
    assert(p[0] >> 24 == 0xFF);
    /* Bottom-up, as the game's texture: the picture's top half is stored last. */
    const int top = (int)(p[(SIZE - 8) * SIZE + SIZE / 2] & 0xFF);
    const int bottom = (int)(p[8 * SIZE + SIZE / 2] & 0xFF);
    assert(bottom > top + 60);
    return top;
}

/* Limited-range BT.601, as the game's own tables (0x3D0810). */
static int expected(int frame)
{
    int v = (298 * (30 + frame - 16) + 128) >> 8;
    return v < 0 ? 0 : v > 255 ? 255 : v;
}

static void near(int got, int frame)
{
    if (got < expected(frame) - 3 || got > expected(frame) + 3) {
        fprintf(stderr, "frame %d: grey %d, expected %d\n", frame, got, expected(frame));
        abort();
    }
}

/* A decoder that falls behind keeps an earlier frame up; the game asks again on
 * its next frames, so wait for the right one as it would. */
static void reach(int frame)
{
    int got = -1;
    for (int tries = 0; tries < 2000; tries++) {
        got = shown(MOVIE_VA, 0x12, 640, 480);
        if (got >= expected(frame) - 3 && got <= expected(frame) + 3) return;
    }
    near(got, frame);
}

static void decode(int n)
{
    while (n--) nfsu2_fmv_decoded();
}

int main(int argc, char **argv)
{
    const uint32_t row = MOVIE_VA + 640u * 4u * 479u;
    nfsu2_movie_row = row;
    if (argc > 1 && !strcmp(argv[1], "invalid")) {
        write_pack("MOVIES\\BAD.VP6", "N2MV", 2);
        write_pack("MOVIES\\WORSE.VP6", "NFMV", 1);
        nfsu2_fmv_movie("MOVIES\\bad.vp6"); decode(3);
        assert(shown(MOVIE_VA, 0x12, 640, 480) < 0);
        nfsu2_fmv_movie("MOVIES\\worse.vp6"); decode(3);
        assert(shown(MOVIE_VA, 0x12, 640, 480) < 0);
        puts("fmv invalid packs ignored");
        return 0;
    }
    write_pack("MOVIES\\SCENE01.VP6", "N2MV", 1);

    /* A movie without a pack plays as on the disc. */
    nfsu2_fmv_movie("MOVIES\\PSA.vp6"); decode(5);
    assert(shown(MOVIE_VA, 0x12, 640, 480) < 0);
    assert(!nfsu2_fmv_tag(MOVIE_VA, 0x12, 640, 480));

    /* The name is matched by the hash of its upper-case path, with any prefix. */
    nfsu2_fmv_movie("D:\\MOVIES\\scene01.vp6");
    assert(shown(MOVIE_VA, 0x12, 640, 480) < 0);        /* nothing decoded yet */
    decode(1);
    reach(0);
    const uint32_t tag0 = nfsu2_fmv_tag(MOVIE_VA, 0x12, 640, 480);
    assert(tag0);
    decode(1);
    reach(1);
    assert(nfsu2_fmv_tag(MOVIE_VA, 0x12, 640, 480) != tag0);

    /* Only the movie texture: format, size and the converted row must match. */
    assert(shown(MOVIE_VA, 0x06, 640, 480) < 0);
    assert(shown(MOVIE_VA, 0x12, 512, 480) < 0);
    assert(shown(MOVIE_VA + 640u * 480u * 4u, 0x12, 640, 480) < 0);
    assert(!nfsu2_fmv_tag(MOVIE_VA + 640u * 480u * 4u, 0x12, 640, 480));

    /* Frames the game skipped past are dropped. */
    decode(48);
    reach(49);
    decode(70);                                         /* 119: the last frame */
    reach(119);
    decode(1);                                          /* past the pack */
    assert(shown(MOVIE_VA, 0x12, 640, 480) < 0);
    assert(!nfsu2_fmv_tag(MOVIE_VA, 0x12, 640, 480));

    /* The same movie again starts from its first frame. */
    nfsu2_fmv_movie("MOVIES\\SCENE01.vp6"); decode(1);
    reach(0);
    decode(9);
    reach(9);

    /* Another movie ends the pack's use. */
    nfsu2_fmv_movie("MOVIES\\PSA.vp6"); decode(2);
    assert(shown(MOVIE_VA, 0x12, 640, 480) < 0);
    nfsu2_fmv_video_stop();
    puts("fmv frames follow the decoded movie");
    return 0;
}
