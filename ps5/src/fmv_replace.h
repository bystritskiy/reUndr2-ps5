#ifndef NFSU2_FMV_REPLACE_H
#define NFSU2_FMV_REPLACE_H
#include <stdint.h>
/* The path the game built for the movie it opens, e.g. "MOVIES\scene01.vp6". */
void nfsu2_fmv_movie(const char *path);
/* One more VP6 frame of the open movie was decoded. */
void nfsu2_fmv_decoded(void);
/* Nonzero while texture va is the movie picture and a pack frame stands in for it;
 * changes with every frame, so it can be mixed into the texture's hash. */
uint32_t nfsu2_fmv_tag(uint32_t va, uint32_t color, uint32_t w, uint32_t h);
/* BGRA picture to show in place of the A8R8G8B8 texture at va, or NULL to use the
 * game's own. Valid until the next call; *w and *h get its size. */
const void *nfsu2_fmv_picture(uint32_t va, uint32_t color, uint32_t w, uint32_t h,
                              uint32_t *out_w, uint32_t *out_h);
#endif
