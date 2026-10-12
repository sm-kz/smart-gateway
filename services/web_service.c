#include "camera_protocol.h"
#include "ipc_utils.h"
#include "camera_client.h"
#include "display.h"
#include "ipc_utils.h"
#include "http_utils.h"
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

enum http_result {
    HTTP_CLOSE = 0,
    HTTP_TRANSFER = 1
};

int server_fd = -1;

struct display disp;

struct stream_manager *stream_manager = NULL;
struct camera_client web_cclient;   // web 服务主线程专用 client，主要用于指令发送。 子线程有各自的 client，用于获取帧

pthread_t display_thread;
static void *display_routine(void *arg);
static void *compress_routine(void *arg);
static void *upload_routine(void *arg);

static volatile sig_atomic_t displaying = 0;
static volatile sig_atomic_t streaming = 0;
static volatile sig_atomic_t exit_requested = 0;

static void sigterm_handler(int sig) {
    (void)sig;
    exit_requested = 1;
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

static enum http_result http_request_process(int client_fd) {
    char request[4096];
    ssize_t n = recv(client_fd, request, sizeof(request) - 1, 0);
    if (n <= 0) {
        return HTTP_CLOSE;
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
        } else if (!strcmp(target, "/api/stream")) {
            if (stream_manager == NULL) {
                fprintf(stderr, "[Web Service] Warning: /api/start must be sent before /api/stream\r\n");
                return HTTP_CLOSE;
            }

            // 加入 stream_manager 后由其释放
            struct stream_client *sclient = stream_client_create(client_fd, upload_routine);
            if (!sclient) {
                fprintf(stderr, "[Web Service] Error: stream_client_create");
                return HTTP_CLOSE;
            }

            if (stream_client_add(stream_manager, sclient) < 0) {
                fprintf(stderr, "[Web Service] Error: stream_client_add");
                free(sclient);
                return HTTP_CLOSE;
            }

            // 开启/关闭 视频流的请求属于长连接
            return HTTP_TRANSFER;
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

                    /* 初始化流管理器 */
                    stream_manager = stream_manager_create(compress_routine);
                    if (stream_manager == NULL) {
                        fprintf(stderr, "[Web Service] Error: stream_manager_create\r\n");
                    }
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

                    /* 销毁流管理器 */
                    stream_manager_destroy(stream_manager);
                    stream_manager = NULL;
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

    send_all(client_fd, response, strlen(response));
    return HTTP_CLOSE;
}

static void http_mjpeg_header(int fd) {
    /* MJPEG 响应头 */
    const char *header =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n"   // 重点是这个首部
    "Cache-Control: no-store\r\n"   // 千万不能缓存，因为图像可能会静止
    "Access-Control-Allow-Origin: *\r\n" 
    "Connection: close\r\n"         // close 表示不复用这个连接处理下一个 HTTP 请求
    "\r\n";

    send_all(fd, header, strlen(header));
}

static int http_jpeg_frame(int fd, unsigned char *data, size_t size) {
    char *frame;
    frame = malloc(256 + size);
    if (frame == NULL)
        return -1;

    int n;
    n = sprintf(frame,
                "--frame\r\n"
                "Content-Type: image/jpeg\r\n"
                "Content-Length: %zu\r\n"
                "\r\n", 
                size);
    memcpy(frame + n, data, size);
    
    int frame_len = n + size;
    if (send_all(fd, frame, frame_len) < 0)
        return -1;

    free(frame);
    return 0;
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

// 压缩事务，由 stream 管理线程处理
static void *compress_routine(void *arg) {
    struct stream_manager *stream_manager = arg;

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
    size_t len_888 = 0;
    stream_manager->buf_jpeg = NULL;
    stream_manager->len_jpeg = 0;

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

        /* 发送到 Web */
        if (camera_client_get_frame(&cclient, buf_565, header.frame_size) == header.frame_size) {
            image_rgb565_to_rgb888(&buf_888, 
                                   &len_888,
                                   buf_565,
                                   header.width,
                                   header.height,
                                   header.bytesperline);

            pthread_mutex_lock(&stream_manager->frame_mutex);

            image_to_jpeg(&stream_manager->buf_jpeg, 
                          &stream_manager->len_jpeg,
                          buf_888,
                          header.width,
                          header.height);

            pthread_mutex_unlock(&stream_manager->frame_mutex);
            pthread_cond_broadcast(&stream_manager->frame_cond);
        }
        
        usleep(16000);  // 以 60 帧的标准检查
    }
    pthread_cond_broadcast(&stream_manager->frame_cond);

    camera_client_close(&cclient);
    if (stream_manager->buf_jpeg) {
        free(stream_manager->buf_jpeg);
        stream_manager->buf_jpeg = NULL;
        stream_manager->len_jpeg = 0;
    }
    free(buf_888);
    free(buf_565);
}

static void mutex_cleanup(void *arg) {
    pthread_mutex_unlock((pthread_mutex_t *)arg);
}

// 上传事务，由 stream 客户线程处理
static void *upload_routine(void *arg) {
    struct stream_client *sclient = arg;
    struct stream_manager *manager = sclient->parent;

    char *frame_buf = NULL;
    size_t frame_size = 0, frame_capacity = 0;
  
    // 发送 MJPEG 头
    http_mjpeg_header(sclient->fd);

    while (displaying) {
        if (exit_requested)
            break;
        
        pthread_mutex_lock(&stream_manager->frame_mutex);

        pthread_cleanup_push(mutex_cleanup, &stream_manager->frame_mutex);

        // pthread_cond_wait 属于取消点，如果不用 cleanup 函数来解锁，那么线程就会带着锁未被解开而结束，容易死锁
        pthread_cond_wait(&stream_manager->frame_cond, &stream_manager->frame_mutex);
        
        // 新帧到来，复制一份，因为 http_jpeg_frame 在 send 时会阻塞，如果持有锁，那么可能造成死锁
        frame_size = manager->len_jpeg;
        if (frame_size > frame_capacity) {  // 帧大小大于缓冲容量时扩容
            char *temp = realloc(frame_buf, frame_size);

            if (temp == NULL) {
                pthread_mutex_unlock(&stream_manager->frame_mutex);
                break;
            }

            frame_buf = temp;
            frame_capacity = frame_size;
        }
        memcpy(frame_buf, stream_manager->buf_jpeg, frame_size);

        pthread_cleanup_pop(1);

        // 有了 pop 弹出 clean 函数进行解锁，这里不需要再解锁了
        // pthread_mutex_unlock(&stream_manager->frame_mutex);

        // 发送 jpeg 帧数据
        if (http_jpeg_frame(sclient->fd, frame_buf, frame_size) < 0) {
            fprintf(stderr, "client %d disconnected or send timeout\n",
                    sclient->fd);
            break;
        }
    }

    free(frame_buf);
    return NULL;
}

static int pipe_ready(int fd, const int ready) {
    if (fd >= 0) {
        char res = ready;
        if (write(fd, &res, 1) != 1) {
            perror("[Web Service] ready notify");
            return -1;
        }
        close(fd);
    }

    return 0;
}

int web_service(int pipe) {
    /* 初始化 SIGINT 信号处理 */
    struct sigaction sa = {0};
    sa.sa_handler = sigterm_handler;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGTERM, &sa, NULL) < 0) {
        perror("[Web Service] sigaction\r\n");
        goto error;
    }

    /* web 服务端初始化 */
    /* 创建 socket */
    server_fd = socket(AF_INET, SOCK_STREAM, 0);  // TCP
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

    /* 连接本地摄像头服务 */
    camera_client_init(&web_cclient);
    if (camera_client_open(&web_cclient) < 0) {
        fprintf(stderr, "[Web Service] Error: camera_client_open\r\n");
        goto error;
    }

    /* 管理 fb 设备 */
    display_init(&disp, 0);
    if (display_open(&disp) < 0) {
        fprintf(stderr, "[Web Service] Error: display_open\r\n");
        goto error1;
    }

    /* 初始化就绪，将就绪状态写入管道 */
    if (pipe_ready(pipe, 1) < 0) {
        goto error2;
    }

    int client_fd = -1;

    while (!exit_requested) {
        client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0) {
            perror("[Web Service] accept");
            continue;    
        }

        // 短连接由主线程自己关闭，长连接由对应线程关闭
        if (http_request_process(client_fd) == HTTP_CLOSE)
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
    pipe_ready(pipe, 0);
    return -1;
}