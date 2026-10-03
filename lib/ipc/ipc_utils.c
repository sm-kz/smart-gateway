#include "ipc_utils.h"
#include <sys/socket.h>
#include <errno.h>

/* SOCK_STREAM 读取的数据大小没有边界。为了保证一次性读完，使用该函数进行读取 */
ssize_t recv_all(int fd, void *buf, size_t len) {
    ssize_t received = 0;
    char *p = buf;

    while (received < len) {
        ssize_t ret = recv(fd, p + received, len - received, 0);
        if (ret > 0) {
            received += ret;
            continue;
        }
        if (ret == 0) {
            /* 对端关闭连接 */
            return 0;
        }
        if (errno == EINTR) {
            /* 被信号打断，重新接收 */
            continue;
        }
        return -1;
    }

    return received;
}

ssize_t send_all(int fd, void *buf, size_t len) {
    ssize_t sent = 0;
    char *p = buf;

    while (sent < len) {
        ssize_t ret = send(fd, p + sent, len - sent, 0);
        if (ret > 0) {
            sent += ret;
            continue;
        }
        if (ret < 0 && errno == EINTR)
            continue;

        return -1;
    }

    return sent;
}