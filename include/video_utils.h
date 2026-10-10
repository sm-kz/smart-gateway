#ifndef __VIDEO_UTILS_H
#define __VIDEO_UTILS_H

#include <stddef.h>
#include <pthread.h>

#define MAX_CLIENTS     5

struct stream_client {
    int fd;                 // 客户端 socket
    pthread_t thread;       // 推流线程
    volatile int running;   // 线程运行标志
    void *(*routine)(void *);   // 线程运行函数

    struct stream_client *next; // 链表节点
    struct stream_manager *parent;
};

struct stream_manager {
    struct stream_client *head;
    size_t client_count;

    pthread_t thread;               // manager 编码线程
    pthread_mutex_t mutex;          // 内部状态锁
    pthread_mutex_t frame_mutex;    // 帧读写锁
    pthread_cond_t frame_cond;      // 通知客户有新帧到来
    

    /* 所有流客户共享内存 */
    unsigned char *buf_jpeg;
    size_t len_jpeg;
};

struct stream_manager *stream_manager_create(void *(*routine)(void *));
void stream_manager_destroy(struct stream_manager *manager);

struct stream_client *stream_client_create(int fd, void *(*routine)(void *));
int stream_client_destroy(struct stream_client *client);
int stream_client_add(struct stream_manager *manager, struct stream_client *client);
void stream_client_del(struct stream_manager *manager, struct stream_client *client);
void stream_client_del_all(struct stream_manager *manager);

void image_rgb565_to_rgb888(unsigned char **dst,
                            size_t *outsize,
                            unsigned char *src,
                            unsigned int width,
                            unsigned int height,
                            unsigned int stride);

int image_to_jpeg(unsigned char **dst,
                  size_t *outsize,
                  unsigned char *src,
                  unsigned int width,
                  unsigned int height);

#endif