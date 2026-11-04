/* Unpacks an Xbox disc image (XDVDFS) on the console. MIT. */
#ifndef NFSU2_DISC_IMAGE_H
#define NFSU2_DISC_IMAGE_H

enum {
 NFSU2_IMAGE_OK,
 NFSU2_IMAGE_NOT_A_DISC,
 NFSU2_IMAGE_WRONG_DISC,
 NFSU2_IMAGE_DAMAGED,
 NFSU2_IMAGE_WRITE
};

/* Copies the disc into target. accept sees default.xbe before anything else is
 * copied and may refuse it; progress gets each new percentage. */
int nfsu2_image_extract(const char *image,const char *target,int (*accept)(const char *xbe),void (*progress)(unsigned));

#endif
