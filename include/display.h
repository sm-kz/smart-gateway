#ifndef __DISPLAY_H
#define __DISPLAY_H

#include <linux/fb.h>

enum display_pixel_format {
    DISPLAY_FORMAT_RGB565 = 0,
    DISPLAY_FORMAT_RGB888
};

struct fb_buffer {
    void *start;
    __u32 size;
};

struct display {
    int fd;
    int index;
    __u32 width;
	__u32 height;
    __u32 bytesperline;
	enum display_pixel_format pixelformat;

    struct fb_buffer buffer;
};


void display_init(struct display *disp, int index);
int display_open(struct display *disp);
void display_close(struct display *disp);
int display_set_format(struct display *disp, enum display_pixel_format format);
void display_fill(struct display *disp, 
                  void *data,
                  __u32 x,
                  __u32 y,
                  __u32 width,
                  __u32 height,
                  __u32 bytesperline,
                  enum display_pixel_format format);

#endif