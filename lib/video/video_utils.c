#include "video_utils.h"
#include <stdio.h>
#include <jpeglib.h>
#include <stdlib.h>

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