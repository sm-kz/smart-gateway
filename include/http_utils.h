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
ssize_t http_send_response(int fd,
                           const int status_code,
                           const char *status_text,
                           const char *content_type,
                           const char *body);
                           
#endif