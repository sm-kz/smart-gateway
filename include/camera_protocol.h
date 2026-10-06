#ifndef __CAMERA_PROTOCAL_H
#define __CAMERA_PROTOCAL_H

#include <linux/types.h>

/**
 * 自定义摄像头服务协议
 * 架构：本地协议分为四层，自顶向下为：
 *      
 *      Web 层：通过 HTTP 协议进行通信，比如 POST /api/start 请求摄像头开启
 *      |-> 通过 AF_UNIX(camera_client) 进行连接，数据桥梁是 shm
 *      Camera Service 层：通过 AF_UNIX 进行通信，维护底层摄像头的信息和状态
 */

#define CAMERA_SOCK_PATH    "/tmp/camera.sock"

#define CAMERA_MAGIC    0x43414D45  // CAME
#define CAMERA_VERSION  1
#define CAMERA_RING_SIZE    6

struct camera_status {
    __u32 state;
    __u32 width;
    __u32 height;
    __u32 pixelformat;
    __u32 fps;
};

struct camera_request {
    __u32 magic;
    __u16 version;
    __u16 command;
};

struct camera_response {
    __u32 magic;
    __u16 version;
    __u16 command;
    __s32 result;
    union {
        struct camera_status status;
    } data;
};

struct camera_ring_header {
    __u32 magic;

    __u32 width;
    __u32 height;
    __u32 pixelformat;
    __u32 bytesperline;

    __u32 frame_size;       // 一帧图像数据大小
    __u32 frame_count;

    __u32 write_sequence;   // 当前已发布的帧序号
};

struct camera_frame_header {
    __u32 sequence;
    __u64 timestamp;
};

struct camera_frame {
    struct camera_frame_header header;
    __u8 data[];    // 保存一帧图像数据
};

/* 环形缓冲 */
struct camera_ring_buffer {
    struct camera_ring_header header;
    __u8 data[];    // 以 camera_frame 结构体数据进行组织
};

enum camera_service_state {
    CAMERA_SERVICE_READY = 0,
    CAMERA_SERVICE_STREAMING
};

enum camera_command {
    CAMERA_CMD_START = 1,
    CAMERA_CMD_STOP,
    CAMERA_CMD_STATUS,
    CAMERA_CMD_MAX
};

#endif