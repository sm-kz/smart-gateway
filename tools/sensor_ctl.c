#include "sensor_protocol.h"
#include "ipc_utils.h"

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>

#define HELP_STRING     "usage:\r\n"\
                        "sensor_ctl <command>\r\n"\
                        "command:\r\n"\
                        "   data:      get accels and gyros data\r\n"

int main(int argc, char **argv) {
    if (argc < 2) {
        printf(HELP_STRING);
        return -1;
    }

    char command[12];
    strcpy(command, argv[1]);

    /* 创建 socket */
    int client_fd;
    client_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (client_fd < 0) {
        perror("socket");
        return -1;
    }

    /* 连接 camera service */
    struct sockaddr_un addr;
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, SENSOR_SOCK_PATH);
    if (connect(client_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect");
        return -1;
    }

    /* 发送请求 */
    struct sensor_request req;
    req.magic = SENSOR_MAGIC;
    req.version = 1;
    if (!strcmp(command, "data"))
        req.command = SENSOR_CMD_DATA;
    else
        req.command = SENSOR_CMD_MAX;

    if (send_all(client_fd, &req, sizeof(req)) < 0) {
        perror("send");
        return -1;
    }

    /* 等待响应 */
    struct pollfd pfd = {0};
    pfd.fd = client_fd;
    pfd.events = POLLIN;
    if (poll(&pfd, 1, 3000) < 0) {
        perror("poll");
        return -1;
    }

    struct sensor_response resp = {0};
    if (pfd.revents & POLLIN) {
        if (recv_all(client_fd, &resp, sizeof(resp)) == sizeof(resp)) {
            printf("[send]\r\n"
                    "magic: %#x\r\n"
                    "version: %d\r\n"
                    "command: %d\r\n"
                    "result: %d\r\n"
                    "state: %d\r\n"
                    "x_accel=%.2f, y_accel=%.2f, z_accel=%.2f\r\n"
                    "x_gyro=%.2f, y_gyro=%.2f, z_gyro=%.2f\r\n",
                    resp.magic, resp.version, resp.command, resp.result, resp.state,
                    resp.data.accel_x, resp.data.accel_y, resp.data.accel_z,
                    resp.data.gyro_x, resp.data.gyro_y, resp.data.gyro_z);
        }
    }

    close(client_fd);
    return 0;
}