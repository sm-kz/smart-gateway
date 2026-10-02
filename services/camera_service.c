#include "camera_protocal.h"
#include "camera.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <pthread.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <poll.h>

static int camera_service_status = CAMERA_SERVICE_READY;

void *camera_service_routine (void *arg) {
    struct camera *camera = arg;
    int fd;
    char *frame_buffer;
    int buffer_size;
    
    // LCD fb0设备显示
    fd = open("/dev/fb0", O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "[Camera Service] Error: failed to open /dev/fb0\r\n");
        pthread_exit(NULL);
    }

    buffer_size = camera->bytesperline * camera->height;
    frame_buffer = mmap(NULL, buffer_size,
                        PROT_WRITE | PROT_READ, MAP_SHARED,
                        fd, 0);
    if (frame_buffer == MAP_FAILED) {
        perror("[Camera Service] Error: mmap");
        goto exit;
    }

    struct pollfd pfd = {0};
    pfd.fd = camera->fd;
    pfd.events = POLLIN;

    while(camera_service_status == CAMERA_SERVICE_STREAMING) {
        if (poll(&pfd, 1, 200) < 0) {
            if (errno == ETIMEDOUT)
                continue;
            else {
                perror("[Camera Service] Error: poll failed\r\n");
                break;
            }
        }

        if (!(pfd.revents & POLLIN))
            continue;

        if (camera_dequeue(camera) < 0) {
            fprintf(stderr, "[Camera Service] Warning: frame lost\r\n");
            continue;
        }

        memcpy(frame_buffer, camera->buffers[camera->pending_buffer].start, buffer_size);

        camera_queue(camera);
    }

    munmap(frame_buffer, buffer_size);

exit:
    close(fd);
    pthread_exit(NULL);
}

int main(int argc, char **argv) {
    /* 初始化摄像头 */
    struct camera camera;
    camera_init(&camera);
    camera_set_index(&camera, 1);
    camera_set_pixelfmt(&camera, PIXEL_FMT_RGB565);
    camera_set_res(&camera, 800, 480);
    camera_set_fps(&camera, 30);
    if (camera_open(&camera) < 0)
        return -1;
    if (camera_sync_fmt(&camera) < 0)
        goto error;
    if (camera_sync_fps(&camera) < 0)
        goto error;
    if (camera_prepare_buffer(&camera, 3) < 0)
        goto error;

    /* 建立 socket */
    int server_fd;
    struct sockaddr_un addr = {0};

    server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("[Camera Service] Error: socket");
        goto error;
    }

    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, CAMERA_SOCK_PATH, strlen(CAMERA_SOCK_PATH));

    // 如果已有 `CAMERA_SOCK_PATH` 文件，先删除
    unlink(CAMERA_SOCK_PATH);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("[Camera Service] Error: bind");
        goto error1;
    }

    if (listen(server_fd, 5) < 0) {
        perror("[Camera Service] Error: listen");
        goto error1;
    }

    int client_fd;

    while (1) {
        // 阻塞等待客户端连接
        client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0)
            continue;

        /* 接收 */
        struct camera_request req;
        if (recv(client_fd, &req, sizeof(req), 0) != sizeof(req))
            goto error2;

        struct camera_response resp = {0};
        resp.magic = req.magic;
        resp.version = req.version;
        resp.command = req.command;
        resp.result = 0;

        /* 数据校验 */
        if (req.magic != CAMERA_MAGIC || 
            req.version != CAMERA_VERSION) {
            fprintf(stderr, "[Camera Service] Invalid camera magic or version\r\n");
            resp.result = EINVAL;
            send(client_fd, &resp, sizeof(resp), 0);
            goto error2;
        }

        /* 指令检查 */
        if (req.command >= CAMERA_CMD_MAX) {
            fprintf(stderr, "[Camera Service] Invalid camera command\r\n");
            resp.result = EBADRQC;
            send(client_fd, &resp, sizeof(resp), 0);
            goto error2;
        }

        /* 响应 */
        switch(req.command) {
        case CAMERA_CMD_START:
            camera_service_status = CAMERA_SERVICE_STREAMING;
            break;
        case CAMERA_CMD_STOP:
            camera_service_status = CAMERA_SERVICE_READY;
            break;
        case CAMERA_CMD_STATUS:
            resp.data.status.width = camera.width;
            resp.data.status.height = camera.height;
            resp.data.status.fps = camera.fps;
            resp.data.status.pixelformat = camera.pixelformat;
            break;
        default:
            break;
        }

        resp.data.status.state = camera_service_status;
        
        if (send(client_fd, &resp, sizeof(resp), 0) < 0) {
            perror("[Camera Service] Error: server send");
            close(client_fd);
            goto error1;
        }

        // 线程处理
        pthread_t stream_thread;
        static int stream_thread_valid = 0;

        if (req.command == CAMERA_CMD_START || req.command == CAMERA_CMD_STOP) {
            if (camera_service_status == CAMERA_SERVICE_STREAMING) {
                // 开启视频流
                camera_stream_on(&camera);
                if (pthread_create(&stream_thread, NULL, camera_service_routine, &camera) < 0) {
                    fprintf(stderr, "[Camera Service] Error: pthread_create failed\r\n");
                    goto error1;
                }
                stream_thread_valid = 1;
            } else if (stream_thread_valid){
                pthread_join(stream_thread, NULL);
                // 关闭视频流
                camera_stream_off(&camera);
                stream_thread_valid = 0;
            }
        }

        close(client_fd);
    }

error2:
    close(client_fd);
error1:
    close(server_fd);
error:
    camera_close(&camera);
    return -1;
}