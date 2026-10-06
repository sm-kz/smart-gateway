#include "camera_client.h"
#include "camera.h"
#include "ipc_utils.h"

#include <stdio.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/un.h>
#include <poll.h>

void camera_client_init(struct camera_client *client) {
    client->shm_fd = -1;
    client->shared = NULL;
    client->shm_size = 0;
    client->last_sequence = 0;
}

int camera_client_open(struct camera_client *client) {
    if (client->shm_fd >= 0)
        return 0;

    client->shm_fd = shm_open(CAMERA_SHM_PATH, O_RDWR, 0);
    if (client->shm_fd < 0) {
        perror("[Camera Client] shm_open");
        return -1;
    }

    // 获取共享内存对象元数据
    struct stat stat;
    if (fstat(client->shm_fd, &stat) < 0) {
        perror("[Camera Client] fstat");
        goto error;
    }

    client->shm_size = stat.st_size;

    client->shared = mmap(NULL, client->shm_size,
        PROT_READ, MAP_SHARED, 
        client->shm_fd, 0);
    if (client->shared == MAP_FAILED) {
        client->shared = NULL;
        perror("[Camera Client] shm_open");
        goto error;
    }

    client->last_sequence = client->shared->header.write_sequence;

    if (client->shared->header.magic != CAMERA_MAGIC) {
        fprintf(stderr, "[Camera Client] Error: valid maigic in frame header\r\n");
        goto error1;
    }

    return 0;

error1:
    munmap(client->shared, client->shm_size);
    client->shared = NULL;
error:
    close(client->shm_fd);
    client->shm_fd = -1;
    return -1;
}

int camera_client_command(struct camera_client *client, 
                        enum camera_command command,
                        struct camera_response *response) {
    /* camera service 采用的短连接，即一次请求一次连接，所以客户端同样采用短连接 */

    /* 创建 socket */
    int sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        perror("[Camera Client] socket");
        return -1;
    }

    /* 连接 camera service */
    struct sockaddr_un addr;
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, CAMERA_SOCK_PATH);
    if (connect(sock_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("[Camera Client] connect");
        close(sock_fd);
        return -1;
    }

    /* 发送请求 */
    struct camera_request req;
    req.magic = CAMERA_MAGIC;
    req.version = CAMERA_VERSION;
    req.command = command;

    if (send_all(sock_fd, &req, sizeof(req)) < 0) {
        perror("[Camera Client] send");
        close(sock_fd);
        return -1;
    }

    /* 等待响应 */
    struct pollfd pfd = {0};
    pfd.fd = sock_fd;
    pfd.events = POLLIN;
    if (poll(&pfd, 1, 3000) <= 0) {
        perror("[Camera Client] poll");
        close(sock_fd);
        return -1;
    }
    
    if (pfd.revents & POLLIN) {
        if (response == NULL) {
            struct camera_response dummy;
            recv_all(sock_fd, &dummy, sizeof(dummy));
        } else {
            if (recv_all(sock_fd, response, sizeof(*response)) != sizeof(*response)) {
                close(sock_fd);
                return -1;
            }
        }
    }

    close(sock_fd);
    return 0;
}

static struct camera_frame_header *
sequence_header_address(struct camera_client *client, __u32 sequence)
{
    __u32 index = sequence % client->shared->header.frame_count;

    size_t stride = sizeof(struct camera_frame_header) +
                    client->shared->header.frame_size;

    return (struct camera_frame_header *)(&client->shared->data[index * stride]);
}

static void *sequence_data_address(struct camera_client *client, __u32 sequence)
{
    struct camera_frame_header *header = sequence_header_address(client, sequence);

    return (char *)header + sizeof(*header);
}

ssize_t camera_client_get_frame(struct camera_client *client, void *buf, size_t max_len) {
    if (client->shared == NULL) {
        fprintf(stderr, "[Camera Client] Error: client hasn't mapped memory yet\r\n");
        return -1;
    }

    __u32 latest = client->shared->header.write_sequence;

    if (latest == client->last_sequence)
        return 0;

    __u32 next_sequence = client->last_sequence + 1;
    
    // 考虑这样一种情况：如果 service 写入速度大于 client 的读取速度，
    // 环形缓冲可能出现缓冲的覆盖。如果消费者太过落后，取落后最新数据一轮
    // 的较新数据。
    if (latest - next_sequence > client->shared->header.frame_count) {
        next_sequence = latest - client->shared->header.frame_count + 1;
    }

    struct camera_frame_header *frame =
        sequence_header_address(client, next_sequence);

    // 又被覆盖了，放弃
    if (frame->sequence != next_sequence)
        return 0;

    size_t len =
        client->shared->header.frame_size < max_len ?
        client->shared->header.frame_size : max_len;

    memcpy(buf, sequence_data_address(client, next_sequence), len);

    client->last_sequence = next_sequence;

    return len;
}

int camera_client_get_info(struct camera_client *client, struct camera_ring_header *header) {
    if (client->shared == NULL) {
        fprintf(stderr, "[Camera Client] Error: client hasn't mapped memory yet\r\n");
        return -1;
    }

    *header = client->shared->header;
    return 0;
}

void camera_client_close(struct camera_client *client) {
    if (client->shared != NULL) {
        munmap(client->shared, client->shm_size);
        client->shared = NULL;
    }

    if (client->shm_fd) {
        close(client->shm_fd);
        client->shm_fd = -1;
    }
}