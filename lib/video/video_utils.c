#include "video_utils.h"
#include <stdio.h>
#include <jpeglib.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>

struct stream_client *stream_client_create(int fd, void *(*routine)(void *)) {
    struct stream_client *client = malloc(sizeof(struct stream_client));
    if (!client)
        return NULL;

    client->fd = fd;
    client->running = 0;
    client->routine = routine;
    INIT_LIST_HEAD(&client->head);
    client->parent = NULL; 
    memset(client->id, 0, sizeof(client->id));

    return client;
}

// 销毁流客户，如果客户有所属的流管理器，从管理链表中移除
int stream_client_destroy(struct stream_client *client) {
    if (client->parent)
        stream_client_del(client->parent, client);

    // 关闭 socket
    if (client->fd > 0)
        close(client->fd);

    free(client);

    return 0;
}

// 向流管理器的链表中添加流客户，并启动客户线程
int stream_client_add(struct stream_manager *manager, struct stream_client *client) {
    if (manager->client_count == MAX_CLIENTS || 
        client == NULL || 
        client->fd <= 0) {
            return -1;
        }

    pthread_mutex_lock(&manager->mutex);

    // 加入链表
    list_add(&client->head, &manager->head);
    client->parent = manager;
    manager->client_count++;

    // 创建线程
    client->running = 1;
    if (pthread_create(&client->thread, NULL, client->routine, client) != 0) {
        list_del_init(&client->head);
        client->running = 0;

        pthread_mutex_unlock(&manager->mutex);

        return -1;
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
        
    // 删除线程
    if (client->running) {
        client->running = 0;
        pthread_cancel(client->thread);
        pthread_join(client->thread, NULL);
    }

    // 从链表中删除
    pthread_mutex_lock(&manager->mutex);

    list_del_init(&client->head);
    client->parent = NULL;
    --manager->client_count;

    pthread_mutex_unlock(&manager->mutex);

    return 0;
}

struct stream_client *stream_find_client_by_fd(struct stream_manager *manager, int fd) {
    if (!manager || list_empty(&manager->head))
        return NULL;

    struct stream_client *client;
    list_foreach_entry(client, &manager->head, head) {
        if (client->fd == fd)
            return client;
    }
    
    return NULL;
}

struct stream_client *stream_find_client_by_id(struct stream_manager *manager, char id[37]) {
    if (!manager || list_empty(&manager->head))
        return NULL;

    struct stream_client *client;
    list_foreach_entry(client, &manager->head, head) {
        if (strcmp(client->id, id))
            return client;
    }
    
    return NULL;
}

struct stream_manager *stream_manager_create(void *(*routine)(void *)) {
    if (routine == NULL)
        return NULL;

    struct stream_manager *manager = calloc(1, sizeof(struct stream_manager));
    if (!manager)
        return NULL;

    INIT_LIST_HEAD(&manager->head);

    pthread_cond_init(&manager->frame_cond, NULL);
    pthread_mutex_init(&manager->mutex, NULL);
    pthread_mutex_init(&manager->frame_mutex, NULL);

    if (pthread_create(&manager->thread, NULL, routine, manager) != 0) {
        pthread_mutex_destroy(&manager->frame_mutex);
        pthread_mutex_destroy(&manager->mutex);
        pthread_cond_destroy(&manager->frame_cond);
        free(manager);
        return NULL;
    }

    return manager;
}

void stream_manager_destroy(struct stream_manager *manager) {
    if (manager == NULL)
        return;

    // 清理链表中的客户
    struct stream_client *client;
    while(!list_empty(&manager->head)) {
        client = list_first_entry(&manager->head, struct stream_client, head);
        stream_client_destroy(client);
    }
    
    pthread_cancel(manager->thread);
    pthread_join(manager->thread, NULL);

    pthread_mutex_destroy(&manager->frame_mutex);
    pthread_mutex_destroy(&manager->mutex);
    pthread_cond_destroy(&manager->frame_cond);

    if (manager->buf_jpeg)
        free(manager->buf_jpeg);
        
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