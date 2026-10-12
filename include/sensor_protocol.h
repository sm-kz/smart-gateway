#ifndef __SENSOR_PROTOCOL_H
#define __SENSOR_PROTOCOL_H

#include <linux/types.h>

#define SENSOR_SOCK_PATH    "/tmp/sensor.sock"

#define SENSOR_MAGIC    0x534E5352  // SNSR
#define SENSOR_VERSION  1

struct sensor_data {
    float accel_x;
    float accel_y;
    float accel_z;

    float gyro_x;
    float gyro_y;
    float gyro_z;

    float temperature;
};

struct sensor_request {
    __u32 magic;
    __u16 version;
    __u16 command;
};

struct sensor_response {
    __u32 magic;
    __u16 version;
    __u16 command;
    __u16 result;

    __u16 state;
    struct sensor_data data;
};

enum sensor_command {
    SENSOR_CMD_START = 0,
    SENSOR_CMD_STOP,
    SENSOR_CMD_DATA,
    SENSOR_CMD_MAX
};

#endif