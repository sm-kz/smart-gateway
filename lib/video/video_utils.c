#include "video_utils.h"
#include <stdio.h>
#include <jpeglib.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>

static struct stream_client *list_tail(struct stream_client *head) {
    struct stream_client *res = head;

    while (res->next) {
        res = res->next;
    }

    return res;
}

static struct stream_client *list_prev(struct stream_client *head, struct stream_client *item) {
    if (item == NULL)
        return NULL;

    struct stream_client *current = head;

    while (current->next) {
        if (current->next == item)
            return current;

        current = current->next;
    }

    return NULL;
}

static void list_add(struct stream_client *head, struct stream_client *item) {
    struct stream_client *tail = list_tail(head);
    item->next = NULL;
    tail->next = item;
}

static int list_del(struct stream_client *head, struct stream_client *item) {
    struct stream_client *prev = list_prev(head, item);

    if (prev == NULL)
        return -1;

    prev->next = item->next;
    item->next = NULL;
    return 0;
}

struct stream_client *stream_client_create(int fd, void *(*routine)(void *)) {
    struct stream_client *client = malloc(sizeof(struct stream_client));
    if (!client)
        return NULL;

    client->fd = fd;
    client->running = 0;
    client->routine = routine;
    client->next = NULL;
    client->parent = NULL; 
    memset(client->id, 0, sizeof(client->id));

    return client;
}

// 销毁流客户，如果客户有所属的流管理器，从管理链表中移除
int stream_client_destroy(struct stream_client *client) {
    int ret = 0;
    // 从列链表中移除
    if (client->parent) {
        ret = list_del(client->parent->head, client);
        pthread_mutex_lock(&client->parent->mutex);
        --client->parent->client_count;
        pthread_mutex_unlock(&client->parent->mutex);
    }

    // 删除线程
    if (client->running) {
        pthread_cancel(client->thread);
        client->running = 0;
    }
    pthread_join(client->thread, NULL);

    // 关闭 socket
    if (client->fd > 0)
        close(client->fd);

    free(client);

    return ret;
}

// 向流管理器的链表中添加流客户，并启动客户线程
int stream_client_add(struct stream_manager *manager, struct stream_client *client) {
    pthread_mutex_lock(&manager->mutex);

    if (manager->client_count == MAX_CLIENTS || 
        client == NULL || 
        client->fd <= 0) {
            pthread_mutex_unlock(&manager->mutex);
            return -1;
        }

    // 加入链表
    list_add(manager->head, client);
    manager->client_count++;
    client->parent = manager;

    // 创建线程
    client->running = 1;
    if (pthread_create(&client->thread, NULL, client->routine, client) != 0) {
        if (list_del(manager->head, client) == 0) {
            manager->client_count--;
            client->running = 0;

            pthread_mutex_unlock(&manager->mutex);

            return -1;
        }
    }

    pthread_mutex_unlock(&manager->mutex);

    return 0;
}

// 删除流管理器中的流客户，取消客户线程，但不销毁客户变量
int stream_client_del(struct stream_manager *manager, struct stream_client *client) {
    if (manager->client_count == 0 ||
        client == NULL ||
        client->parent != manager)
        return -1;

    pthread_mutex_lock(&manager->mutex);
        
    // 从链表中删除
    if (list_del(manager->head, client) == 0)
        manager->client_count--;
    else
        return -1;
    
    pthread_mutex_unlock(&manager->mutex);

    // 删除线程
    if (client->running) {
        pthread_cancel(client->thread);
        client->running = 0;
    }
    pthread_join(client->thread, NULL);

    return 0;
}

struct stream_client *stream_find_client_by_fd(struct stream_manager *manager, int fd) {
    if (!manager || !manager->head->next)
        return NULL;

    struct stream_client *client = manager->head->next;

    while(client) {
        if (client->fd == fd)
            break;
        client = client->next;
    }
    
    return client;
}

struct stream_client *stream_find_client_by_id(struct stream_manager *manager, char id[37]) {
    if (!manager || !manager->head->next)
        return NULL;

    struct stream_client *client = manager->head->next;

    while(client) {
        if (strcmp(client->id, id))
            break;
        client = client->next;
    }
    
    return client;
}

struct stream_manager *stream_manager_create(void *(*routine)(void *)) {
    if (routine == NULL)
        return NULL;

    struct stream_manager *manager = calloc(1, sizeof(struct stream_manager));
    if (!manager)
        return NULL;

    manager->head = malloc(sizeof(struct stream_client));
    if (!manager->head) {
        free(manager);
        return NULL;
    }

    pthread_cond_init(&manager->frame_cond, NULL);
    pthread_mutex_init(&manager->mutex, NULL);
    pthread_mutex_init(&manager->frame_mutex, NULL);

    if (pthread_create(&manager->thread, NULL, routine, manager) != 0) {
        pthread_mutex_destroy(&manager->frame_mutex);
        pthread_mutex_destroy(&manager->mutex);
        pthread_cond_destroy(&manager->frame_cond);
        free(manager->head);
        free(manager);
        return NULL;
    }

    return manager;
}

void stream_manager_destroy(struct stream_manager *manager) {
    if (manager == NULL)
        return;

    // 清理链表中的客户
    struct stream_client *client = list_tail(manager->head);
    while (client && client != manager->head) {
        stream_client_del(manager, client);
        stream_client_destroy(client);
        client = list_tail(manager->head);
    }

    pthread_cancel(manager->thread);
    pthread_join(manager->thread, NULL);

    pthread_mutex_destroy(&manager->frame_mutex);
    pthread_mutex_destroy(&manager->mutex);
    pthread_cond_destroy(&manager->frame_cond);

    if (manager->buf_jpeg)
        free(manager->buf_jpeg);

    free(manager->head);
    free(manager);
}

#define RGB565_RGB888(src, dst) do { \
    unsigned short src_pix = *(unsigned short *)src; \
    unsigned char r = ((src_pix >> 11) & 0x1F); \
    unsigned char g = ((src_pix >> 5) & 0x3F); \
    unsigned char b = (src_pix & 0x1F); \
    ((unsigned char *)dst)[0] = (r << 3) | (r >> 2); \
    ((unsigned char *)dst)[1] = (g << 2) | (g >> 4); \
    ((unsigned char *)dst)[2] = (b << 3) | (b >> 2); \
} while (0)

#define RGB888_RGB565(src, dst) do { \
    unsigned char r = ((unsigned char *)(src))[0]; \
    unsigned char g = ((unsigned char *)(src))[1]; \
    unsigned char b = ((unsigned char *)(src))[2]; \
    unsigned short pixel = \
        ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3); \
    ((unsigned char *)(dst))[0] = pixel & 0xFF; \
    ((unsigned char *)(dst))[1] = pixel >> 8; \
} while (0)

/**
 * 将 RGB565 格式的图图像转为 RGB888
 * 如果源图像每行有填充，那么意味着 stride 大于 width * 2
 * 转换后的 RGB888图像没有填充，其 stride 等于 width * 3
 */
void image_rgb565_to_rgb888(unsigned char **dst,
                            size_t *outsize,
                            unsigned char *src,
                            unsigned int width,
                            unsigned int height,
                            unsigned int stride)
{
    if (!dst || !outsize || !src)
        return;

    unsigned int i, j;
    size_t total_size;

    total_size = width * height * 3;
    if (total_size > *outsize || *dst == NULL) {
        unsigned char *temp = realloc(*dst, total_size);
        if (!temp)
            return;

        *dst = temp;
        *outsize = total_size;
    }

    for (i = 0; i < height; ++i) {
        for (j = 0; j < width; ++j) {
            RGB565_RGB888(&src[i * stride + j * 2], &(*dst)[(i * width + j) * 3]);
        }
    } 
}

int image_to_jpeg(unsigned char **dst,
                  size_t *outsize,
                  unsigned char *src,
                  unsigned int width,
                  unsigned int height) 
{
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr emgr;

    // 绑定错误处理，初始化压缩对象
    cinfo.err = jpeg_std_error(&emgr);
    jpeg_create_compress(&cinfo);

    // 绑定压缩输出内存
    jpeg_mem_dest(&cinfo, dst, outsize);

    // 设置图片信息
    cinfo.image_width = width;
    cinfo.image_height = height;
    cinfo.in_color_space = JCS_RGB;
    cinfo.input_components = 3;     // 原生只支持 RGB888 格式的压缩，如果要压缩 RGB565,需要转为 RGB888 再压缩

    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, 75, TRUE);

    // 开始压缩
    jpeg_start_compress(&cinfo, TRUE);      // 写入全部扫描行
    JSAMPROW row_ptr;
    while (cinfo.next_scanline < cinfo.image_height) {
        row_ptr = &src[cinfo.next_scanline * width * 3];
        jpeg_write_scanlines(&cinfo, &row_ptr, 1);
    }

    // 结束压缩，清理资源
    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);

    return 0;
}