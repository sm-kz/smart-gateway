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
static void sigterm_handler (int sig) {
    (void)sig;
    exit_requested = 1;
}

int camera_recv_requeset(int client_fd, struct camera_request *request) {
    int n = recv_all(client_fd, request, sizeof(*request));
    if (n != sizeof(*request)) {
        if (n < 0)
            perror("[Camera Service] recv");
        
        fprintf(stderr, "[Camera Service] Error: %d bytes data is expected to receive, "
                "but %d bytes received\r\n", sizeof(*request), n);

        return EINVAL;
    }

    /* 数据校验 */
    if (request->magic != CAMERA_MAGIC || 
        request->version != CAMERA_VERSION) {
        fprintf(stderr, "[Camera Service] Invalid camera magic or version\r\n");
        return EPROTO;
    }

    /* 指令检查 */
    if (request->command >= CAMERA_CMD_MAX) {
        fprintf(stderr, "[Camera Service] Invalid camera command\r\n");
        return EBADRQC;
    }

    return 0;
}

void *camera_service_routine (void *arg) {
    struct camera *camera = arg;
    struct camera_ring_buffer *shared;
    int index;
    int buffer_size;

    shared = camera->shm_mm;

    struct pollfd pfd = {0};
    pfd.fd = camera->fd;
    pfd.events = POLLIN;

    while(camera_service_status == CAMERA_SERVICE_STREAMING) {
        if (exit_requested)
            break;
            
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

        __u32 seq = shared->header.write_sequence + 1;
        __u32 slot = seq % shared->header.frame_count;

        size_t stride =
            sizeof(struct camera_frame_header) +
            shared->header.frame_size;

        struct camera_frame_header *frame =
            (struct camera_frame_header *)
            (&shared->data[slot * stride]);

        memcpy((char *)frame + sizeof(*frame),
            camera->buffers[index].start,
            camera->buffers[index].size);

        frame->sequence = seq;
        frame->timestamp = 0;
        
        shared->header.write_sequence = seq;

        camera_queue(camera);
    }

exit:
    pthread_exit(NULL);
}

static int pipe_ready(int fd, const int ready) {
    if (fd >= 0) {
        char res = ready;
        if (write(fd, &res, 1) != 1) {
            perror("[Sensor Service] ready notify");
            return -1;
        }
        close(fd);
        fd = -1;
    }

    return 0;
}

int camera_service(int pipe) {
    /* 初始化摄像头 */
    camera_init(&camera, 1);
    camera_set_pixelfmt(&camera, CAMERA_PIX_FMT_RGB565);
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
    sa.sa_handler = sigterm_handler;
    sigemptyset(&sa.sa_mask);
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
        goto error1;

    /* 初始化就绪，将就绪状态写入管道 */
    if (pipe_ready(pipe, 1) < 0) {
        goto error2;
    }
    
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
            perror("[Camera Service] accept");
            continue;    
        }

        /* 接收 */
        struct camera_request req;
        int ret;
        ret = camera_recv_requeset(client_fd, &req);

        struct camera_response resp = {0};
        resp.magic = CAMERA_MAGIC;
        resp.version = CAMERA_VERSION;
        resp.command = req.command;
        resp.result = ret;

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
            close(client_fd);
            continue;
        }

        // 线程处理
        if (camera_service_status != old_state) {
            if (camera_service_status == CAMERA_SERVICE_STREAMING) {
                // 开启视频流
                camera_stream_on(&camera);
                if (pthread_create(&stream_thread, NULL, camera_service_routine, &camera) != 0) {
                    fprintf(stderr, "[Camera Service] Error: pthread_create failed\r\n");
                    close(client_fd);
                    continue;
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
    pipe_ready(pipe, 0);
    return -1;
}