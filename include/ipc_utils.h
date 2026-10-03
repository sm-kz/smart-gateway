#ifndef __IPC_UTILS_H
#define __IPC_UTILS_H

#include <sys/types.h>

ssize_t recv_all(int fd, void *buf, size_t len);
ssize_t send_all(int fd, void *buf, size_t len);

#endif