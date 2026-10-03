#include "camera_client.h"
#include "camera.h"

#include <stdio.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/un.h>

void camera_client_init(struct camera_client *client) {
    client->shm_fd = -1;
    client->shared = NULL;
    client->shm_size = 0;
    client->last_sequence = 0;
}

int camera_client_open(struct camera_client *client) {
    if (client->shm_fd >= 0)
        return 0;

    client->shm_fd = shm_open(SHM_PATH, O_RDWR, 0);
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

    client->last_sequence = client->shared->header.sequence;

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

ssize_t camera_client_get_frame(struct camera_client *client, void *buf, size_t max_len) {
    if (client->shared == NULL) {
        fprintf(stderr, "[Camera Client] Error: client hasn't mapped memory yet\r\n");
        return -1;
    }

    if (client->shared->header.sequence == client->last_sequence)
        return 0;

    int index = client->shared->header.frame_active;
    int len = client->shared->header.frame_size > max_len ? max_len : client->shared->header.frame_size;

    memcpy(buf, &client->shared->data[index * client->shared->header.frame_size], len);
    client->last_sequence = client->shared->header.sequence;

    return len;
}

int camera_client_get_info(struct camera_client *client, struct camera_frame_header *header) {
    if (client->shared == NULL) {
        fprintf(stderr, "[Camera Client] Error: client hasn't mapped memory yet\r\n");
        return -1;
    }

    *header = client->shared->header;
    return 0;
}

void camera_client_close(struct camera_client *client) {
    if (client->shared == NULL)
        return;

    close(client->shm_fd);
    client->shm_fd = 0;
    
    munmap(client->shared, client->shm_size);
    client->shared = NULL;

    shm_unlink(SHM_PATH);
}