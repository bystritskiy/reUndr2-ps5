#ifndef NFSU2_FMV_VIDEO_H
#define NFSU2_FMV_VIDEO_H
#include <stdint.h>
/* BGRA pixels of a frame of the AV1 movie at data_offset in movie, valid until the
 * next call. NULL when there is nothing to show yet. */
const void *nfsu2_fmv_video_frame(int movie, uint64_t data_offset, uint32_t width, uint32_t height,
                                  uint32_t frame, int restart);
void nfsu2_fmv_video_stop(void);
#endif
