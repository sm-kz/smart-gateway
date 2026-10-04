#ifndef __CAMERA_CLIENT_H
#define __CAMERA_CLIENT_H

#include "camera_protocol.h"
#include <sys/types.h>

struct camera_client {
    int shm_fd;
    struct camera_frame *shared;
    size_t shm_size;
    __u32 last_sequence;
};

void camera_client_init(struct camera_client *client);
int camera_client_open(struct camera_client *client);
int camera_client_command(struct camera_client *client, 
                        enum camera_command command,
                        struct camera_response *response);
ssize_t camera_client_get_frame(struct camera_client *client, void *buf, size_t max_len);
int camera_client_get_info(struct camera_client *client, struct camera_frame_header *header);
void camera_client_close(struct camera_client *client);

#endif