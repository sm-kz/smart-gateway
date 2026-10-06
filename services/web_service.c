#include "camera_protocol.h"
#include "ipc_utils.h"
#include "camera_client.h"
#include "display.h"

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
#include <stdlib.h>
#include <unistd.h>

#define WEB_PORT    16767

struct camera_client cclient;
struct display disp;
pthread_t display_thread;

static volatile sig_atomic_t displaying = 0;
static volatile sig_atomic_t exit_requested = 0;

static void sigterm_handler(int sig) {
    (void)sig;
    exit_requested = 1;
}

static void *display_routine(void *arg) {
    char *buf = NULL;
    struct camera_ring_header header = {0};

    if (camera_client_get_info(&cclient, &header) < 0) {
        fprintf(stderr, "[Web Service] Error: camera_client_get_info\r\n");
        pthread_exit(NULL);
    }

    buf = malloc(header.frame_size);
    if (!buf) {
        fprintf(stderr, "[Web Service] Error: malloc\r\n");
        pthread_exit(NULL);
    }

    while (displaying) {
        if (camera_client_get_frame(&cclient, buf, header.frame_size) == header.frame_size) {
            display_fill(&disp, 
                         buf, 
                         0, 0, 
                         header.width, header.height, 
                         header.bytesperline, 
                         FORMAT_RGB565);
        }

        usleep(16000);  // 以 60 帧的标准检查
    }
}

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

static void http_message_process(int client_fd) {
    char request[4096];
    ssize_t n = recv(client_fd, request, sizeof(request) - 1, 0);
    if (n <= 0) {
        close(client_fd);
        return;
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

                // 显示还未开始，开启显示线程
                if (displaying == 0) {
                    displaying = 1;
                    pthread_create(&display_thread, NULL, display_routine, NULL);
                }
            } else {
                status_code = 410;
                strcpy(status_text, "Gone");
            }
        } else if (!strcmp(target, "/api/stop")) {
            if (camera_client_command(&cclient, CAMERA_CMD_STOP, NULL) == 0) {
                status_code = 200;
                strcpy(status_text, "OK");
                strcpy(body, "{state: stoped}");

                // 显示已开始，关闭显示线程
                if (displaying == 1) {
                    displaying = 0;
                    pthread_join(display_thread, NULL);
                }
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
                    "%s\r\n",
                    status_code, status_text,
                    strlen(body), body);

    send(client_fd, response, strlen(response), 0);
}

int web_service(int pipe) {
    /* 初始化 SIGINT 信号处理 */
    struct sigaction sa = {0};
    sa.sa_handler = sigterm_handler;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGTERM, &sa, NULL) < 0) {
        perror("[Camera Service] sigaction\r\n");
        goto error;
    }

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
    camera_client_init(&cclient);
    if (camera_client_open(&cclient) < 0) {
        fprintf(stderr, "[Web Service] Error: camera_client_open\r\n");
        goto error;
    }
    /* --------------------------------------------------------------- */

    /* ----------------------- 管理 fb 设备 --------------------------- */
    display_init(&disp, 0);
    if (display_open(&disp) < 0) {
        fprintf(stderr, "[Web Service] Error: display_open\r\n");
        goto error1;
    }
    /* --------------------------------------------------------------- */

    /* 初始化就绪，将就绪状态写入管道 */
    if (pipe >= 0) {
        char ready = 1;
        if (write(pipe, &ready, 1) != 1) {
            perror("[Web Service] ready notify");
            goto error2;
        }
        close(pipe);
    }

    int client_fd;

    while (!exit_requested) {
        client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0) {
            perror("[Web Service] accept");
            continue;    
        }

        http_message_process(client_fd);
        
        close(client_fd);
    }
    
    display_close(&disp);
    camera_client_close(&cclient);
    close(server_fd);

    return 0;

error2:
    display_close(&disp);
error1:
    camera_client_close(&cclient);
error:
    close(server_fd);
    return -1;
}