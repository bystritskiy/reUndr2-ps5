/* A text screen for what happens before the game. MIT. */
#ifndef NFSU2_SCREEN_PS5_H
#define NFSU2_SCREEN_PS5_H
#include <stdint.h>

typedef struct {
 unsigned y,scale;   /* top pixel row on 1080p; 5x7 glyphs drawn scale times larger */
 uint32_t color;     /* 0xAARRGGBB, 0 for white */
 const char *text;
} Nfsu2ScreenLine;

/* Shows the lines, and a progress bar unless percent is negative. Negative on failure. */
int nfsu2_screen_show(const Nfsu2ScreenLine *lines,unsigned count,int percent);
/* Gives VideoOut back; the game's renderer cannot open it before this. */
void nfsu2_screen_close(void);

#endif
