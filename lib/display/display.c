#include "display.h"

#include <sys/ioctl.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <string.h>

/**
 * 初始化显示结构体成员 
 */
void display_init(struct display *disp, int index) {
    memset(disp, 0, sizeof(*disp));

    disp->fd = -1;
    disp->index = index;
    disp->width = 0;
    disp->height = 0;
    disp->pixelformat = FORMAT_RGB888;
    disp->buffer.start = NULL;
    disp->buffer.size = 0;
}

static int display_get_format(struct display *disp, struct fb_var_screeninfo *vinfo) {
    if (vinfo->bits_per_pixel == 16) {
        if (vinfo->red.offset == 11 &&
            vinfo->red.length == 5 &&
            vinfo->green.offset == 5 &&
            vinfo->green.length == 6 &&
            vinfo->blue.offset == 0 &&
            vinfo->blue.length == 5) {
            disp->pixelformat = FORMAT_RGB565;
        } else {
            return -1;
        }
    } else if (vinfo->bits_per_pixel == 24) {
        if (vinfo->red.offset == 16 &&
            vinfo->red.length == 8 &&
            vinfo->green.offset == 8 &&
            vinfo->green.length == 8 &&
            vinfo->blue.offset == 0 &&
            vinfo->blue.length == 8) {
            disp->pixelformat = FORMAT_RGB888;
        } else {
            return -1;
        }
    } else {
        return -1;
    }

    return 0;
}

int display_set_format(struct display *disp, enum pixel_format format) {
    struct fb_var_screeninfo vinfo;
    if (ioctl(disp->fd, FBIOGET_VSCREENINFO, &vinfo) < 0) {
        perror("[Display] ioctl FBIOGET_VSCREENINFO");
        return -1;
    }

    switch (format)
    {
    case FORMAT_RGB565:
        vinfo.bits_per_pixel = 16;
        vinfo.red.offset = 11;
        vinfo.red.length = 5;
        vinfo.green.offset = 5;
        vinfo.green.length = 6;
        vinfo.blue.offset = 0;
        vinfo.blue.length = 5;
        break;
    case FORMAT_RGB888:
        vinfo.bits_per_pixel = 24;
        vinfo.red.offset = 16;
        vinfo.red.length = 8;
        vinfo.green.offset = 8;
        vinfo.green.length = 8;
        vinfo.blue.offset = 0;
        vinfo.blue.length = 8;
        break;
    default:
        return -1;
    }

    if (ioctl(disp->fd, FBIOPUT_VSCREENINFO, &vinfo) < 0) {
        perror("[Display] ioctl FBIOPUT_VSCREENINFO");
        return -1;
    }

    return 0;
}

/**
 * 打开显示设备，并同步设备参数
 */
int display_open(struct display *disp) {
    if (disp->fd >= 0)
        return 0;

    char devname[16];
    sprintf(devname, "/dev/fb%d", disp->index);
    disp->fd = open(devname, O_RDWR);
    if (disp->fd < 0) {
        perror("[Display] open");
        return -1;
    }
    
    /* 获取显示屏参数 */
    struct fb_var_screeninfo vinfo;
    if (ioctl(disp->fd, FBIOGET_VSCREENINFO, &vinfo) < 0) {
        perror("[Display] ioctl FBIOGET_VSCREENINFO");
        goto error;
    }

    struct fb_fix_screeninfo finfo;
    if (ioctl(disp->fd, FBIOGET_FSCREENINFO, &finfo) < 0) {
        perror("[Display] ioctl FBIOGET_FSCREENINFO");
        goto error;
    }

    disp->width = vinfo.xres;
    disp->height = vinfo.yres;
    disp->bytesperline = finfo.line_length;     // 每行可能有 padding，所以不能直接像素字节数乘宽

    if (display_get_format(disp, &vinfo) < 0) {
        fprintf(stderr, "[Display] Error: unknown pixel format\r\n");
        goto error;
    }

    /* 内存映射 */
    disp->buffer.size = disp->bytesperline * disp->height;
    disp->buffer.start = mmap(NULL, disp->buffer.size,
                              PROT_READ | PROT_WRITE, MAP_SHARED,
                              disp->fd, 0);
    if (disp->buffer.start == MAP_FAILED) {
        perror("[Display] Error: mmap");
        goto error;
    }

    return 0;

error:
    close(disp->fd);
    disp->fd = -1;
    return -1;
}

/**
 * 关闭显示设备
 */
void display_close(struct display *disp) {
    if (disp->buffer.start) {
        munmap(disp->buffer.start, disp->buffer.size);
        disp->buffer.start = NULL;
        disp->buffer.size = 0;
    }

    if (disp->fd >= 0) {
        close(disp->fd);
        disp->fd = -1;
    }
}

/**
 * 填充帧缓冲
 */
void display_fill(struct display *disp, 
                  void *data,
                  __u32 x,
                  __u32 y,
                  __u32 width,
                  __u32 height,
                  __u32 bytesperline,
                  enum pixel_format format) {
    if (disp->buffer.start == NULL || 
        data == NULL ||
        disp->pixelformat != format)
        return;

    __u32 bytesperpixel;
    switch (disp->pixelformat)
    {
    case FORMAT_RGB565:
        bytesperpixel = 2;
        break;
    case FORMAT_RGB888:
        bytesperpixel = 3;
        break;
    default:
        return;
    }
    
    __u32 copy_width = width;
    __u32 copy_height = height;

    if (copy_width > disp->width - x)
        copy_width = disp->width - x;

    if (copy_height > disp->height - x)
        copy_height = disp->height - x;

    __u32 copy_bytes = copy_width * bytesperpixel;

    unsigned char *p = (unsigned char *)disp->buffer.start + disp->bytesperline * y + bytesperpixel * x;
    __u32 row;
    for (row = 0; row < copy_height; ++row) {
        memcpy(p, data, copy_bytes);
        p += disp->bytesperline;
        data += bytesperline;
    }
}