#include "http_utils.h"
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