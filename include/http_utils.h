#ifndef __HTTP_UTILS_H
#define __HTTP_UTILS_H

#include <sys/types.h>

enum req_method {
    REQUEST_INVALID = -1,
    REQUEST_GET,
    REQUEST_POST
};

enum req_method http_get_request_method(const char *request);
size_t http_get_request_target(const char *request, char **target, size_t *len);

#endif