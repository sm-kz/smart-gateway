#include "http_utils.h"
#include "ipc_utils.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/**
 * 解析 http 请求的 method
 */
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

    char *te = memchr(s, '?', e - s);

    size_t target_size;
    if (te == NULL)
        target_size = e - s + 1;
    else
        target_size = te - s + 1;

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

ssize_t http_send_response(int fd,
                           const int status_code,
                           const char *status_text,
                           const char *content_type,
                           const char *body) {
    char response[1024];
    int len = snprintf(response, sizeof(response),
                    "HTTP/1.1 %d %s\r\n"
                    "Content-Type: %s\r\n"
                    "Content-Length: %zu\r\n"
                    "Connection: close\r\n"
                    "Access-Control-Allow-Origin: *\r\n"    // 跨域读取开发板返回的数据
                    "\r\n"
                    "%s",
                    status_code, 
                    status_text, 
                    content_type,
                    strlen(body), 
                    body);

    return send_all(fd, response, strlen(response));
}