#include "sensor_client.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>
#include <stdio.h>

int sensor_client_init(struct sensor_client *client) {

}

int sensor_client_command(struct sensor_client *client,
                          enum sensor_command command,
                          struct sensor_response *resp) {
    /* camera service 采用的短连接，即一次请求一次连接，所以客户端同样采用短连接 */

    /* 创建 socket */
    int sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        perror("[Sensor Client] socket");
        return -1;
    }

    /* 连接 sensor service */
    struct sockaddr_un addr;
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, SENSOR_SOCK_PATH);
    if (connect(sock_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("[Sensor Client] connect");
        close(sock_fd);
        return -1;
    }

    /* 发送请求 */
    struct sensor_request req;
    req.magic = SENSOR_MAGIC;
    req.version = SENSOR_VERSION;
    req.command = command;

    if (send_all(sock_fd, &req, sizeof(req)) < 0) {
        perror("[Sensor Client] send");
        close(sock_fd);
        return -1;
    }

    /* 等待响应 */
    int ret;
    struct pollfd pfd = {0};
    pfd.fd = sock_fd;
    pfd.events = POLLIN;
    ret = poll(&pfd, 1, 3000);
    if (ret < 0) {
        perror("[Sensor Client] poll");
        close(sock_fd);
        return -1;
    }

    if (ret == 0) {
        fprintf(stderr, "[Sensor Client] poll timeout\n");
        close(sock_fd);
        return -1;
    }
    
    if (pfd.revents & POLLIN) {
        if (resp == NULL) {
            struct sensor_response dummy;
            recv_all(sock_fd, &dummy, sizeof(dummy));
        } else {
            if (recv_all(sock_fd, resp, sizeof(*resp)) != sizeof(*resp)) {
                close(sock_fd);
                return -1;
            }
        }
    }

    close(sock_fd);
    return 0;
}
