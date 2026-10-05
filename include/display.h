#ifndef __DISPLAY_H
#define __DISPLAY_H

#include <linux/fb.h>

enum pixel_format {
    FORMAT_RGB565 = 0,
    FORMAT_RGB888
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
	enum pixel_format pixelformat;

    struct fb_buffer buffer;
};


void display_init(struct display *disp, int index);
int display_open(struct display *disp);
void display_close(struct display *disp);
int display_set_format(struct display *disp, enum pixel_format format);
void display_fill(struct display *disp, 
                  void *data,
                  __u32 x,
                  __u32 y,
                  __u32 width,
                  __u32 height,
                  __u32 bytesperline,
                  enum pixel_format format);

#endif