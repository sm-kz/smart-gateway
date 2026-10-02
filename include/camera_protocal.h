#ifndef __CAMERA_PROTOCAL_H
#define __CAMERA_PROTOCAL_H

#include <linux/types.h>

/**
 * 自定义摄像头服务协议
 */

#define CAMERA_SOCK_PATH    "/tmp/camera.sock"

#define CAMERA_MAGIC    0x43414D31
#define CAMERA_VERSION  1

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