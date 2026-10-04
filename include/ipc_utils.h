#ifndef __IPC_UTILS_H
#define __IPC_UTILS_H

#include <sys/types.h>

ssize_t recv_all(int fd, void *buf, size_t len);
ssize_t send_all(int fd, void *buf, size_t len);

enum req_method {
    REQUEST_INVALID = -1,
    REQUEST_GET,
    REQUEST_POST
};

enum req_method http_get_request_method(const char *request);
size_t http_get_request_target(const char *request, char **target, size_t *len);

#endif