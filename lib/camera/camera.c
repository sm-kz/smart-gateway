#include "camera.h"
#include "camera_protocol.h"

#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <unistd.h>

/**
 * struct camera 成员初始化
 */
void camera_init(struct camera *camera, int index) {
    memset(camera, 0, sizeof(*camera));
    
    camera->fd = -1;
    camera->index = index;
    camera->width = 800;
    camera->height = 480;
    camera->pixelformat = CAMERA_PIX_FMT_RGB565;
    camera->pending_buffer = -1;

    camera->shm_fd = -1;
    camera->shm_mm = NULL;
}

/**
 * 打开摄像头设备
 */
int camera_open(struct camera *camera) {
    char devname[16];
    sprintf(devname, "/dev/video%d", camera->index);
    camera->fd = open(devname, O_RDWR | O_NONBLOCK);
    if (camera->fd < 0) {
        perror("[Camera] open");
        return -1;
    }

    camera->state = OPENED;

    return 0;
}

/**
 * 设置分辨率
 */
void camera_set_res(struct camera *camera, __u32 width, __u32 height) {
    camera->width = width;
    camera->height = height;
}

/**
 * 设置像素格式
 */
void camera_set_pixelfmt(struct camera *camera, __u32 pixelfmt) {
    camera->pixelformat = pixelfmt;
}

/**
 * 设置fps
 */
void camera_set_fps(struct camera *camera, __u32 fps) {
    camera->fps = fps;
}

/**
 * 同步格式设置
 */
int camera_sync_fmt(struct camera *camera) {
    if (camera->state == CLOSED) {
        fprintf(stderr, "[Camera] Error: attempt to access no device\r\n");
        return -1;
    }
    if (camera->state == STREAMING) {
        fprintf(stderr, "[Camera] Error: attempt to modify buffers when straming\r\n");
        return -1;
    }

    struct v4l2_format fmt = {0};
    int ret;

    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = camera->width;
    fmt.fmt.pix.height = camera->height;
    fmt.fmt.pix.pixelformat = camera->pixelformat;

    ret = ioctl(camera->fd, VIDIOC_S_FMT, &fmt);
    if (ret < 0) {
        if (errno == ETIMEDOUT) {
            fprintf(stderr, "[Camera] Error: VIDIOC_S_FMT timed out\r\n");
            return -1;
        }
        fprintf(stderr, "[Camera] Error: VIDIOC_S_FMT failed\r\n");
        return -1;
    }

    camera->width = fmt.fmt.pix.width;
    camera->height = fmt.fmt.pix.height;
    camera->bytesperline = fmt.fmt.pix.bytesperline;

    return 0;
}

/**
 * 同步帧率
 */
int camera_sync_fps(struct camera *camera) {
    if (camera->state == CLOSED) {
        fprintf(stderr, "[Camera] Error: attempt to access no device\r\n");
        return -1;
    }
    if (camera->state == STREAMING) {
        fprintf(stderr, "[Camera] Error: attempt to modify buffers when straming\r\n");
        return -1;
    }

    struct v4l2_streamparm parm = {0};
    int ret;

    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    parm.parm.capture.timeperframe.numerator = 1;
    parm.parm.capture.timeperframe.denominator = camera->fps;

    ret = ioctl(camera->fd, VIDIOC_S_PARM, &parm);
    if (ret < 0) {
        if (errno == ETIMEDOUT) {
            fprintf(stderr, "[Camera] Error: VIDIOC_S_PARM timed out\r\n");
            return -1;
        }
        fprintf(stderr, "[Camera] Error: VIDIOC_S_PARM failed\r\n");
        return -1;
    }

    camera->fps = parm.parm.capture.timeperframe.denominator;
    return 0;
}

/**
 * 准备帧缓冲
 */
int camera_prepare_buffer(struct camera *camera, __u32 buf_count) {
    if (camera->state == CLOSED) {
        fprintf(stderr, "[Camera] Error: attempt to access no device\r\n");
        return -1;
    }
    if (camera->state == STREAMING) {
        fprintf(stderr, "[Camera] Error: attempt to modify buffers when straming\r\n");
        return -1;
    }

    struct v4l2_requestbuffers reqbuf = {0};
    int ret, i;

    reqbuf.count = buf_count;
    reqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    reqbuf.memory = V4L2_MEMORY_MMAP;

    ret = ioctl(camera->fd, VIDIOC_REQBUFS, &reqbuf);
    if (ret < 0) {
        if (errno == ETIMEDOUT) {
            fprintf(stderr, "[Camera] Error: VIDIOC_REQBUFS timed out\r\n");
            return -1;
        }
        fprintf(stderr, "[Camera] Error: VIDIOC_REQBUFS failed\r\n");
        return -1;
    }

    if (camera->buffers != NULL && camera->buf_count != reqbuf.count) {
        free(camera->buffers);
        camera->buffers = NULL;
        camera->buf_count = 0;
    }

    camera->buffers = malloc(sizeof(struct camera_buffer) * reqbuf.count);
    if (!camera->buffers) {
        fprintf(stderr, "[Camera] Error: malloc for camera_buffer failed\r\n");
        return -1;
    }

    camera->buf_count = reqbuf.count;

    /* 内存映射 */
    struct v4l2_buffer buf;
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    for (i = 0; i < camera->buf_count; ++i) {
        buf.index = i;
        if (ioctl(camera->fd, VIDIOC_QUERYBUF, &buf)) {
            fprintf(stderr, "[Camera] Error: VIDIOC_QUERYBUF failed\r\n");
            goto release;
        }
        camera->buffers[i].size = buf.length;
        camera->buffers[i].start = mmap(NULL, buf.length,
                                        PROT_READ | PROT_WRITE,
                                        MAP_SHARED, camera->fd, buf.m.offset);
        if (camera->buffers[i].start == MAP_FAILED) {
            fprintf(stderr, "[Camera] Error: mmaping with length of %d failed\r\n",
                    buf.length);
            camera->buffers[i].start = NULL;
            camera->buffers[i].size = 0;
            goto release;
        }
    }

    return 0;

release:
    while(i-- > 0) {
        buf.index = i;
        munmap(camera->buffers[i].start, camera->buffers[i].size);
        camera->buffers[i].start = NULL;
        camera->buffers[i].size = 0;
    }
    free(camera->buffers);
    camera->buffers = NULL;

    return -1;
}

/**
 * 开启视频流
 */
int camera_stream_on(struct camera *camera) {
    if (camera->state == CLOSED) {
        fprintf(stderr, "[Camera] Error: attempt to access no device\r\n");
        return -1;
    }
    if (camera->state == STREAMING)
        return 0;

    struct v4l2_buffer buf;
    int i;

    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    for (i = 0; i < camera->buf_count; ++i) {
        buf.index = i;
        if (ioctl(camera->fd, VIDIOC_QBUF, &buf) < 0) {
            fprintf(stderr, "[Camera] Erroor: VIDIOC_QBUF failed\r\n");
            return -1;
        }
    }

    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (ioctl(camera->fd, VIDIOC_STREAMON, &type) < 0) {
        fprintf(stderr, "[Camera] Erroor: VIDIOC_STREAMON failed\r\n");
        return -1;
    }

    camera->state = STREAMING;

    return 0;
}

/**
 * 关闭视频流
 */
int camera_stream_off(struct camera *camera) {
    if (camera->state == CLOSED) {
        fprintf(stderr, "[Camera] Error: attempt to access no device\r\n");
        return -1;
    }
    if (camera->state == OPENED)
        return 0;

    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (ioctl(camera->fd, VIDIOC_STREAMOFF, &type) < 0) {
        fprintf(stderr, "[Camera] Erroor: VIDIOC_STREAMOFF failed\r\n");
        return -1;
    }

    camera->state = OPENED;

    return 0;
}

/**
 * 摄像头缓冲入队
 */
int camera_queue(struct camera *camera) {
    if (camera->state != STREAMING) {
        fprintf(stderr, "[Camera] Warning: no frame to process\r\n");
        return -1;
    }

    // 没有未处理的帧
    if (camera->pending_buffer == -1)
        return 0;

    struct v4l2_buffer buf;
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = camera->pending_buffer;
    if (ioctl(camera->fd, VIDIOC_QBUF, &buf) < 0) {
        fprintf(stderr, "[Camera] Error: VIDIOC_QBUF failed\r\n");
        return -1;
    }

    camera->pending_buffer = -1;
    return 0;
}

/**
 * 摄像头缓冲出队
 */
int camera_dequeue(struct camera *camera) {
    if (camera->state != STREAMING) {
        fprintf(stderr, "[Camera] Warning: no frame to process\r\n");
        return -1;
    }

    // 还有未处理的帧
    if (camera->pending_buffer != -1)
        return 0;

    struct v4l2_buffer buf;
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    if(ioctl(camera->fd, VIDIOC_DQBUF, &buf) < 0) {
        fprintf(stderr, "[Camera] Error: VIDIOC_DQBUF failed\r\n");
        return -1;
    }

    camera->pending_buffer = buf.index;
    return buf.index;
}

/**
 * 关闭摄像头
 */
int camera_close(struct camera *camera) {
    if (camera->state == CLOSED)
        return 0;

    if (camera->state == STREAMING)
        camera_stream_off(camera);

    if (camera->buffers) {
        int i;
        for (i = 0; i < camera->buf_count; ++i) {
            munmap(camera->buffers[i].start, camera->buffers[i].size);
            camera->buffers[i].start = NULL;
            camera->buffers[i].size = 0;
        }
        
        free(camera->buffers);
        camera->buffers = NULL;
    }

    close(camera->fd);
    camera->fd = -1;
    camera->pending_buffer = -1;
    camera->state = CLOSED;

    return 0;
}

/**
 * 创建共享内存 
 */
int camera_shm_create(struct camera *camera) {
    if (camera->shm_fd >= 0)
        return 0;

    if (camera->fd < 0) {
        fprintf(stderr, "[Camera] Error: creating shm before camera opened is not allowed\r\n");
        return -1;
    }

    camera->shm_fd = shm_open(CAMERA_SHM_PATH, O_RDWR | O_CREAT, 0666);
    if (camera->shm_fd < 0) {
        perror("[Camera] shm_open");
        return -1;
    }
    
    int shm_size;
    shm_size = sizeof(struct camera_ring_header) + 
               (sizeof(struct camera_frame_header) + 
               camera->bytesperline * camera->height) * CAMERA_RING_SIZE;
    if (ftruncate(camera->shm_fd, shm_size) < 0) {
        perror("[Camera] ftruncate");
        goto error;
    }

    camera->shm_mm = mmap(NULL, shm_size, 
                  PROT_WRITE | PROT_READ,
                  MAP_SHARED, camera->shm_fd, 0);
    if (camera->shm_mm == MAP_FAILED) {
        perror("[Camera] mmap");
        goto error;
    }

    // 初始化共享内存数据头
    struct camera_ring_buffer *shared = camera->shm_mm;
    shared->header.magic = CAMERA_MAGIC;
    shared->header.width = camera->width;
    shared->header.height = camera->height;
    shared->header.pixelformat = camera->pixelformat;
    shared->header.bytesperline = camera->bytesperline;
    shared->header.frame_size = camera->buffers[0].size;
    shared->header.frame_count = CAMERA_RING_SIZE;
    shared->header.write_sequence = 0;

    return 0;

error:
    close(camera->shm_fd);
    camera->shm_fd = -1;
    shm_unlink(CAMERA_SHM_PATH);
    return -1;
}

/**
 * 销毁共享内存
 */
int camera_shm_destroy(struct camera *camera) {
    if (camera->shm_fd == -1) {
        return 0;
    }

    int shm_size;
    shm_size = sizeof(struct camera_ring_header) + 
               (sizeof(struct camera_frame_header) + 
               camera->bytesperline * camera->height) * CAMERA_RING_SIZE;
               
    munmap(camera->shm_mm, shm_size);
    camera->shm_mm = NULL;

    close(camera->shm_fd);
    camera->shm_fd = -1;
    shm_unlink(CAMERA_SHM_PATH);

    return 0;
}