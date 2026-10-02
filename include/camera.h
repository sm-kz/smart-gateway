#ifndef __CAMERA_H
#define __CAMERA_H

#include <linux/videodev2.h>

#define PIXEL_FMT_RGB565  V4L2_PIX_FMT_RGB565

struct camera_buffer {
    void *start;
    __u32 size;
};

enum camera_state {
    CLOSED = 0,
    OPENED,
    STREAMING
};

struct camera {
    int index;
    int fd;
    __u32 width;
	__u32 height;
	__u32 pixelformat;
    __u32 bytesperline;
    __u32 fps;
    __u32 buf_count;
    struct camera_buffer *buffers;

    enum camera_state state;
    int pending_buffer;
};

void camera_init(struct camera *camera);
void camera_set_index(struct camera *camera, int index);
int camera_open(struct camera *camera);
void camera_set_res(struct camera *camera, __u32 width, __u32 height);
void camera_set_pixelfmt(struct camera *camera, __u32 pixelfmt);
void camera_set_fps(struct camera *camera, __u32 fps);
int camera_sync_fmt(struct camera *camera);
int camera_sync_fps(struct camera *camera);
int camera_prepare_buffer(struct camera *camera, __u32 buf_count);
int camera_stream_on(struct camera *camera);
int camera_stream_off(struct camera *camera);
int camera_queue(struct camera *camera);
int camera_dequeue(struct camera *camera);
int camera_close(struct camera *camera);

#endif