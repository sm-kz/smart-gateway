#include "camera_protocol.h"
#include "camera.h"
#include "ipc_utils.h"

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
#include <signal.h>

static struct camera camera;
static volatile sig_atomic_t camera_service_status = CAMERA_SERVICE_READY;
static volatile sig_atomic_t stream_thread_valid = 0;
static volatile sig_atomic_t exit_requested = 0;

/* 进程正式被 kill 前释放资源 */
void sigterm_handle (int sig, siginfo_t *info, void *context) {
    (void)sig;
    (void)info;
    (void)context;

    exit_requested = 1;
}

void *camera_service_routine (void *arg) {
    struct camera *camera = arg;
    struct camera_frame *shared;
    int index;
    int buffer_size;

    shared = camera->shm_mm;

    struct pollfd pfd = {0};
    pfd.fd = camera->fd;
    pfd.events = POLLIN;

    while(camera_service_status == CAMERA_SERVICE_STREAMING) {
        if (poll(&pfd, 1, 200) <= 0) {
            if (errno == ETIMEDOUT)
                continue;
            else {
                perror("[Camera Service] poll\r\n");
                break;
            }
        }

        if (!(pfd.revents & POLLIN))
            continue;

        index = camera_dequeue(camera);
        if (index < 0) {
            fprintf(stderr, "[Camera Service] Warning: frame lost\r\n");
            continue;
        }

        /* 帧处理流程 */
        memcpy(&shared->data[(!shared->header.frame_active) * shared->header.frame_size], 
            camera->buffers[index].start, camera->buffers[index].size);

        shared->header.sequence++;
        shared->header.frame_active = !shared->header.frame_active;

        camera_queue(camera);
    }

exit:
    pthread_exit(NULL);
}

int main(int argc, char **argv) {
    /* 初始化摄像头 */
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

    /* 初始化 SIGINT 信号处理 */
    struct sigaction sa = {0};
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = sigterm_handle;
    if (sigaction(SIGTERM, &sa, NULL) < 0) {
        perror("[Camera Service] sigaction\r\n");
        goto error;
    }

    /* 建立 socket */
    int server_fd;
    struct sockaddr_un addr = {0};

    server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("[Camera Service] socket");
        goto error;
    }

    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, CAMERA_SOCK_PATH, strlen(CAMERA_SOCK_PATH));

    // 如果已有 `CAMERA_SOCK_PATH` 文件，先删除
    unlink(CAMERA_SOCK_PATH);

    // 绑定，并生成 `CAMERA_SOCK_PATH` 文件，为本服务程序提供一个本地标识
    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("[Camera Service] bind");
        goto error1;
    }

    if (listen(server_fd, 5) < 0) {
        perror("[Camera Service] listen");
        goto error1;
    }

    /* 创建共享内存，让后续摄像头数据可以被客户程序使用 */
    if (camera_shm_create(&camera) < 0)
        goto error2;
    
    int client_fd;
    pthread_t stream_thread;

    while (!exit_requested) {
        // 阻塞等待客户端连接
        client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0) {
            if (errno == EINTR) {
                if (exit_requested)
                    break;
                }
            continue;    
            perror("[Camera Service] accept");
        }

        /* 接收 */
        struct camera_request req;
        if (recv_all(client_fd, &req, sizeof(req)) != sizeof(req))
            goto error3;

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
            goto error3;
        }

        /* 指令检查 */
        if (req.command >= CAMERA_CMD_MAX) {
            fprintf(stderr, "[Camera Service] Invalid camera command\r\n");
            resp.result = EBADRQC;
            send(client_fd, &resp, sizeof(resp), 0);
            goto error3;
        }

        // 记录旧状态，避免重复开启或关闭
        int old_state = camera_service_status;

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
        
        if (send_all(client_fd, &resp, sizeof(resp)) < 0) {
            perror("[Camera Service] send");
            goto error3;
        }

        // 线程处理
        if (camera_service_status != old_state) {
            if (camera_service_status == CAMERA_SERVICE_STREAMING) {
                // 开启视频流
                camera_stream_on(&camera);
                if (pthread_create(&stream_thread, NULL, camera_service_routine, &camera) != 0) {
                    fprintf(stderr, "[Camera Service] Error: pthread_create failed\r\n");
                    goto error3;
                }
                stream_thread_valid = 1;
            } else if (stream_thread_valid){
                pthread_join(stream_thread, NULL);
                stream_thread_valid = 0;
                // 关闭视频流
                camera_stream_off(&camera);
            }
        }

        close(client_fd);
    }

    /* 清理工作，仅针对 SIGTERM，对 SIGKILL 无效 */
    if (stream_thread_valid) {
        // 通知视频线程结束循环
        camera_service_status = CAMERA_SERVICE_READY;

        pthread_join(stream_thread, NULL);
        stream_thread_valid = 0;
        // 关闭视频流
        camera_stream_off(&camera);
    }

    camera_shm_destroy(&camera);
    close(server_fd);
    unlink(CAMERA_SOCK_PATH);
    camera_close(&camera);

    return 0;

error3:
    close(client_fd);
error2:
    camera_shm_destroy(&camera);
error1:
    close(server_fd);
    unlink(CAMERA_SOCK_PATH);
error:
    camera_close(&camera);
    return -1;
}