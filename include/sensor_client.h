#ifndef __SENSOR_CLIENT_H
#define __SENSOR_CLIENT_H

#include <linux/types.h>
#include "sensor_protocol.h"

/* mpu6050 的控制客户端结构体 */
struct sensor_client {
    
};

int sensor_client_init(struct sensor_client *client);

int sensor_client_command(struct sensor_client *client,
                          enum sensor_command command,
                          struct sensor_response *resp);
                          
#endif