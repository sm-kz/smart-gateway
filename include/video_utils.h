#ifndef __VIDEO_UTILS_H
#define __VIDEO_UTILS_H

#include <stddef.h>

void image_rgb565_to_rgb888(unsigned char **dst,
                            size_t *outsize,
                            unsigned char *src,
                            unsigned int width,
                            unsigned int height,
                            unsigned int stride);

int image_to_jpeg(unsigned char **dst,
                  size_t *outsize,
                  unsigned char *src,
                  unsigned int width,
                  unsigned int height);

#endif