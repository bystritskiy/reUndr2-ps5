/* port_options.h -- the options the host's port menu shows (PS5), kept by
 * recomp_manual.c with the title's own Options rows in nfsu2x_options.txt.
 * Values are indexes into each option's steps; every change applies at once. */
#ifndef NFSU2_PORT_OPTIONS_H
#define NFSU2_PORT_OPTIONS_H

enum {
    NFSU2_OPT_SCALE,        /* 1x 1.5x 2x 2.5x 3x 4.5x */
    NFSU2_OPT_FXAA,         /* off on */
    NFSU2_OPT_ANISO,        /* off 2x 4x 8x 16x */
    NFSU2_OPT_SQUARE,       /* off on */
    NFSU2_OPT_REFLECTIONS,  /* off on */
    NFSU2_OPT_LOD,          /* 1x 2x 4x 8x */
    NFSU2_OPT_GRID3D,       /* off on */
    NFSU2_OPT_FPS,          /* 60 30 */
    NFSU2_OPT_OVERLAY,      /* off on */
    NFSU2_OPT_PAUSE,        /* off on: the port menu holds the title */
    NFSU2_OPT_VIBRATION,    /* off 10% .. 100% */
    NFSU2_OPT_TEXT_LANG,    /* lang.txt's two text choices; from the next boot */
    NFSU2_OPT_VOICE_LANG,   /* lang.txt's two voice choices; from the next boot */
    NFSU2_OPT_SPEAKERS,     /* off on: the 5.1 mix on the speakers (PS5) */
    NFSU2_OPT_SPEAKER_TEST, /* off, eight channels, all in turn; never kept */
    NFSU2_OPT_BLUR,         /* motion blur: off, low, original */
    NFSU2_OPT_COUNT
};

#ifdef __cplusplus
extern "C" {
#endif
int nfsu2_opt_get(int id);
int nfsu2_opt_steps(int id);
void nfsu2_opt_set(int id, int value);
int nfsu2_opt_shown(int id);    /* this build and data have the option */
/* A language choice's name in UTF-8, marked while it waits for a restart. */
const char *nfsu2_opt_lang_text(int id, int value, char *buf, unsigned cap);
void nfsu2_opt_save(void);
extern int nfsu2_port_menu_seen;   /* kept with the options */
#ifdef __cplusplus
}
#endif
#endif
