#include "ipc_utils.h"
#include <sys/socket.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

/* SOCK_STREAM 读取的数据大小没有边界。为了保证一次性读完，使用该函数进行读取 */
ssize_t recv_all(int fd, void *buf, size_t len) {
    if (buf == NULL || len == 0)
        return 0;

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
    if (buf == NULL || len == 0)
        return 0;

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

enum req_method http_get_request_method(const char *request) {
    if (!strncmp(request, "GET", 3))
        return REQUEST_GET;
    else if (!strncmp(request, "POST", 4))
        return REQUEST_POST;
    else
        return REQUEST_INVALID;
}

/**
 * 解析 http 请求的 target
 */
size_t http_get_request_target(const char *request, char **target, size_t *size) {
    if (request == NULL || target == NULL || size == NULL)
        return 0;

    char *s = strchr(request, ' ');
    if (s == NULL)
        return 0;
    ++s;

    char *e = strchr(s, ' ');
    if (e == NULL)
        return 0;

    size_t target_size = e - s + 1;

    if (target_size > *size) {
        char *temp = realloc(*target, target_size);
        if (temp == NULL)
            return 0;
        *target = temp;
        *size = target_size;
    }
    
    memcpy(*target, s, target_size - 1);
    (*target)[target_size - 1] = '\0';

    return target_size;
}