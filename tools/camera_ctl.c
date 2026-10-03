#include "camera_protocol.h"
#include "ipc_utils.h"

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>

#define HELP_STRING     "usage:\r\n"\
                        "camera_ctl <command>\r\n"\
                        "command:\r\n"\
                        "   start:      start streaming\r\n"\
                        "   stop:       stop streaming\r\n"\
                        "   status:     show camera status\r\n"

int main(int argc, char **argv) {
    if (argc < 2) {
        printf(HELP_STRING);
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
    strcpy(addr.sun_path, CAMERA_SOCK_PATH);
    if (connect(client_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect");
        return -1;
    }

    /* 发送请求 */
    struct camera_request req;
    req.magic = CAMERA_MAGIC;
    req.version = 1;
    if (!strcmp(command, "start"))
        req.command = CAMERA_CMD_START;
    else if (!strcmp(command, "stop"))
        req.command = CAMERA_CMD_STOP;
    else if (!strcmp(command, "status"))
        req.command = CAMERA_CMD_STATUS;
    else
        req.command = 0;

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

    struct camera_response resp = {0};
    if (pfd.revents & POLLIN) {
        if (recv_all(client_fd, &resp, sizeof(resp)) == sizeof(resp)) {
            printf("[response]\r\n"\
                   "magic: %#x\r\n"\
                   "version: %d\r\n"\
                   "command: %d\r\n"\
                   "result: %d\r\n"\
                   "resolution: %dx%d\r\n"\
                   "format: %#x\r\n"\
                   "fps: %d\r\n"\
                   "state: %d\r\n",
                    resp.magic, resp.version, resp.command, resp.result, 
                    resp.data.status.width, resp.data.status.height,
                    resp.data.status.pixelformat, resp.data.status.fps, 
                    resp.data.status.state);
        }
    }

    close(client_fd);
    return 0;
}