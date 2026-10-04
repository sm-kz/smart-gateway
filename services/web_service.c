#include "camera_protocol.h"
#include "camera.h"
#include "ipc_utils.h"
#include "camera_client.h"

#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>
#include <poll.h>
#include <pthread.h>
#include <string.h>

#define WEB_PORT    16767

void response_to_json(struct camera_response *response, char *buf, size_t maxlen) {
    snprintf(buf, maxlen,
        "{\r\n"
        "   magic: %#x\r\n"
        "   version: %u\r\n"
        "   command: %u\r\n"
        "   result: %d\r\n"
        "   resolution: %ux%u\r\n"
        "   fps: %u\r\n"
        "   format: %#x\r\n"
        "   state: %u\r\n"
        "}",
        response->magic,
        response->version,
        response->command,
        response->result,
        response->data.status.width,
        response->data.status.height,
        response->data.status.fps,
        response->data.status.pixelformat,
        response->data.status.state
    );

    buf[maxlen - 1] = '\0';
}


int main(int argc, char **argv) {
    /* ---------------------- web 服务端初始化 ------------------------- */
    /* 创建 socket */
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);  // TCP
    if (server_fd < 0) {
        perror("[Web Service] socket");
        return -1;
    }

    /* 绑定 IP */
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);   // 监听开发板所有网卡
    addr.sin_port = htons(WEB_PORT);
    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("[Web Service] bind");
        goto error;
    }

    /* 开启监听 */
    if (listen(server_fd, 5) < 0) {
        perror("[Web Service] listen");
        goto error;
    }
    /* --------------------------------------------------------------- */

    /* ------------------ 连接本地 camera_service --------------------- */
    struct camera_client cclient;
    camera_client_init(&cclient);
    if (camera_client_open(&cclient) < 0) {
        fprintf(stderr, "[Web Service] Error: camera_client_open\r\n");
        goto error;
    }
    /* --------------------------------------------------------------- */

    int client_fd;

    while (1) {
        client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0) {
            perror("[Web Service] accept");
            continue;    
        }

        char request[4096];
        ssize_t n = recv(client_fd, request, sizeof(request) - 1, 0);
        if (n <= 0) {
            close(client_fd);
            continue;
        }
        request[n] = 0;

        printf(" --------------- HTTP request ---------------\r\n");
        printf("%s\r\n", request);
        printf(" --------------------------------------------\r\n");

        /* 请求提取与处理 */
        enum req_method method = http_get_request_method(request);
        static char *target = NULL;
        static size_t tsize = 0;
        http_get_request_target(request, &target, &tsize);

        int status_code = 0;
        char status_text[10] = "";
        char body[258] = "";

        switch(method) {
        case REQUEST_GET:
            if (!strcmp(target, "/")) {
                status_code = 200;
                strcpy(status_text, "OK");
                strcpy(body, "Hello smart gateway");
            } else if (!strcmp(target, "/api/status")) {
                struct camera_response resp;
                if (camera_client_command(&cclient, CAMERA_CMD_STATUS, &resp) == 0) {
                    status_code = 200;
                    strcpy(status_text, "OK");
                    response_to_json(&resp, body, sizeof(body));
                } else {
                    status_code = 410;
                    strcpy(status_text, "Gone");
                }
            } else {
                status_code = 404;
                strcpy(status_text, "Not Found");
            }
            break;
        case REQUEST_POST:
            if (!strcmp(target, "/api/start")) {
                if (camera_client_command(&cclient, CAMERA_CMD_START, NULL) == 0) {
                    status_code = 200;
                    strcpy(status_text, "OK");
                    strcpy(body, "{state: started}");
                } else {
                    status_code = 410;
                    strcpy(status_text, "Gone");
                }
            } else if (!strcmp(target, "/api/stop")) {
                if (camera_client_command(&cclient, CAMERA_CMD_STOP, NULL) == 0) {
                    status_code = 200;
                    strcpy(status_text, "OK");
                    strcpy(body, "{state: stoped}");
                } else {
                    status_code = 410;
                    strcpy(status_text, "Gone");
                }
            }
        default:
            break;
        }

        char response[512];
        int len = snprintf(response, sizeof(response),
                        "HTTP/1.1 %d %s\r\n"
                        "Content-Type: text/plain\r\n"
                        "Content-Length: %zu\r\n"
                        "Connection: close\r\n"
                        "\r\n"
                        "%s",
                        status_code, status_text,
                        strlen(body), body);

        send(client_fd, response, strlen(response), 0);
        close(client_fd);
    }

    camera_client_close(&cclient);
    close(server_fd);

    return 0;

error1:
    camera_client_close(&cclient);
error:
    close(server_fd);
    return -1;
}