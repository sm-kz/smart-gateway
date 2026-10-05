#include "camera_client.h"
#include <poll.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

int main(int argc, char **argv) {
    int ret;
    struct camera_client client;

    camera_client_init(&client);
    ret = camera_client_open(&client);
    if (ret < 0)
        return -1;
    
    int fd = open("/dev/fb0", O_RDWR);
    if (fd < 0) {
        perror("[Viewer] open");
    }
    char *buf = mmap(NULL, client.shared->header.frame_size,
                     PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    // 这里有误，共享内存的数据变化不会引起 POLLIN 事件
    struct pollfd pfd;
    pfd.fd = client.shm_fd;
    pfd.events = POLLIN;

    struct camera_frame_header header;
    camera_client_get_info(&client, &header);
    printf("resolution: %ux%u\r\n", header.width, header.height);

    while (1) {
        if (poll(&pfd, 1, 200) <= 0) {
            if (errno == ETIMEDOUT)
                continue;
            break;
        }

        if (!(pfd.revents & POLLIN))
            continue;

        camera_client_get_frame(&client, buf, client.shared->header.frame_size);
    }

}