#include "camera_protocol.h"
#include "ipc_utils.h"
#include "camera_client.h"
#include "display.h"
#include "video_utils.h"

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

struct display disp;

struct camera_client web_cclient;   // web 服务主线程专用 client，主要用于指令发送。 子线程有各自的 client，用于获取帧

pthread_t display_thread;
pthread_t upload_thread;

static volatile sig_atomic_t displaying = 0;
static volatile sig_atomic_t exit_requested = 0;

static void sigterm_handler(int sig) {
    (void)sig;
    exit_requested = 1;
}

static void *display_routine(void *arg) {
    /* 连接本地 camera_service */
    struct camera_client cclient;
    camera_client_init(&cclient);
    if (camera_client_open(&cclient) < 0) {
        fprintf(stderr, "[Web Service] Error: camera_client_open\r\n");
        pthread_exit(NULL);
    }

    struct camera_ring_header header = {0};
    if (camera_client_get_info(&cclient, &header) < 0) {
        fprintf(stderr, "[Web Service] Error: camera_client_get_info\r\n");
        camera_client_close(&cclient);
        pthread_exit(NULL);
    }

    unsigned char *buf = NULL;
    buf = malloc(header.frame_size);
    if (!buf) {
        fprintf(stderr, "[Web Service] Error: malloc\r\n");
        camera_client_close(&cclient);
        pthread_exit(NULL);
    }

    while (displaying) {
        if (exit_requested)
            break;

        /* 显示到 LCD */
        if (camera_client_get_frame(&cclient, buf, header.frame_size) == header.frame_size) {
            display_fill(&disp, 
                         buf, 
                         0, 0, 
                         header.width, header.height, 
                         header.bytesperline, 
                         DISPLAY_FORMAT_RGB565);
        }

        usleep(16000);  // 以 60 帧的标准检查
    }

    camera_client_close(&cclient);
    free(buf);
}

static void *upload_routine(void *arg) {
    /* 连接本地 camera_service */
    struct camera_client cclient;
    camera_client_init(&cclient);
    if (camera_client_open(&cclient) < 0) {
        fprintf(stderr, "[Web Service] Error: camera_client_open\r\n");
        pthread_exit(NULL);
    }

    struct camera_ring_header header = {0};
    if (camera_client_get_info(&cclient, &header) < 0) {
        fprintf(stderr, "[Web Service] Error: camera_client_get_info\r\n");
        camera_client_close(&cclient);
        pthread_exit(NULL);
    }

    unsigned char *buf_565 = NULL;
    unsigned char *buf_888 = NULL;
    unsigned char *buf_compressed = NULL;
    size_t len_888 = 0, len_compressed = 0;

    buf_565 = malloc(header.frame_size);
    if (!buf_565) {
        fprintf(stderr, "[Web Service] Error: malloc\r\n");
        camera_client_close(&cclient);
        pthread_exit(NULL);
    }

    len_888 = header.width * header.height * 3;
    buf_888 = malloc(len_888);
    if (!buf_888) {
        fprintf(stderr, "[Web Service] Error: malloc\r\n");
        camera_client_close(&cclient);
        free(buf_565);
        pthread_exit(NULL);
    }

    while (displaying) {
        if (exit_requested)
            break;

        /* 显示到 Web */
        if (camera_client_get_frame(&cclient, buf_565, header.frame_size) == header.frame_size) {
            image_rgb565_to_rgb888(&buf_888, 
                                   &len_888,
                                   buf_565,
                                   header.width,
                                   header.height,
                                   header.bytesperline);
            image_to_jpeg(&buf_compressed, 
                          &len_compressed,
                          buf_888,
                          header.width,
                          header.height);
        }
        
        usleep(16000);  // 以 60 帧的标准检查
    }

    camera_client_close(&cclient);
    if (buf_compressed)
        free(buf_compressed);
    free(buf_888);
    free(buf_565);
}

void response_to_json(struct camera_response *response, char *buf, size_t maxlen) {
    char command_str[10] = "";
    char format_str[10] = "";
    char state_str[10] = "";

    switch (response->command)
    {
    case CAMERA_CMD_START:
        sprintf(command_str, "Start");
        break;
    case CAMERA_CMD_STOP:
        sprintf(command_str, "Stop");
        break;
    case CAMERA_CMD_STATUS:
        sprintf(command_str, "Status");
        break;
    default:
        break;
    }

    switch (response->data.status.pixelformat)
    {
    case CAMERA_PIX_FMT_RGB565:
        sprintf(format_str, "RGB565");
        break;
    case CAMERA_PIX_FMT_RGB888:
        sprintf(format_str, "RGB888");
        break;
    default:
        break;
    }

    switch (response->data.status.state)
    {
    case CAMERA_SERVICE_READY:
        sprintf(state_str, "Paused");
        break;
    case CAMERA_SERVICE_STREAMING:
        sprintf(state_str, "Running");
        break;
    default:
        break;
    }

    snprintf(buf, maxlen,
        "{\r\n"
        "   \"magic\": \"%#x\",\r\n"
        "   \"version\": %u,\r\n"
        "   \"command\": \"%s\",\r\n"
        "   \"result\": %d,\r\n"
        "   \"resolution\": \"%ux%u\",\r\n"
        "   \"fps\": %u,\r\n"
        "   \"format\": \"%s\",\r\n"
        "   \"state\": \"%s\"\r\n"
        "}",
        response->magic,
        response->version,
        command_str,
        response->result,
        response->data.status.width,
        response->data.status.height,
        response->data.status.fps,
        format_str,
        state_str
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
            if (camera_client_command(&web_cclient, CAMERA_CMD_STATUS, &resp) == 0) {
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
            if (camera_client_command(&web_cclient, CAMERA_CMD_START, NULL) == 0) {
                status_code = 200;
                strcpy(status_text, "OK");
                strcpy(body,
                    "{\r\n"
                    "   \"state\": \"started\"\r\n"
                    "}\r\n");

                // 显示还未开始，开启显示线程
                if (displaying == 0) {
                    displaying = 1;
                    pthread_create(&display_thread, NULL, display_routine, NULL);
                    pthread_create(&upload_thread, NULL, upload_routine, NULL);
                }
            } else {
                status_code = 410;
                strcpy(status_text, "Gone");
            }
        } else if (!strcmp(target, "/api/stop")) {
            if (camera_client_command(&web_cclient, CAMERA_CMD_STOP, NULL) == 0) {
                status_code = 200;
                strcpy(status_text, "OK");
                strcpy(body,
                    "{\r\n"
                    "   \"state\": \"stopped\"\r\n"
                    "}\r\n");

                // 显示已开始，关闭显示线程
                if (displaying == 1) {
                    displaying = 0;
                    pthread_join(display_thread, NULL);
                    pthread_join(upload_thread, NULL);
                }
            } else {
                status_code = 410;
                strcpy(status_text, "Gone");
            }
        }
        break;
    default:
        break;
    }

    char response[512];
    int len = snprintf(response, sizeof(response),
                    "HTTP/1.1 %d %s\r\n"
                    "Content-Type: application/json\r\n"
                    "Content-Length: %zu\r\n"
                    "Connection: close\r\n"
                    "Access-Control-Allow-Origin: *\r\n"    // 跨域读取开发板返回的数据
                    "\r\n"
                    "%s",
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

    /* ----------------------- 连接本地摄像头服务 ----------------------- */
    camera_client_init(&web_cclient);
    if (camera_client_open(&web_cclient) < 0) {
        fprintf(stderr, "[Web Service] Error: camera_client_open\r\n");
        goto error;
    }
    /* ---------------------------------------------------------------- */

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
    camera_client_close(&web_cclient);
    close(server_fd);

    return 0;

error2:
    display_close(&disp);
error1:
    camera_client_close(&web_cclient);
error:
    close(server_fd);
    return -1;
}