#ifndef IPC_MEDIA_TYPES_H
#define IPC_MEDIA_TYPES_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    unsigned width;
    unsigned height;
    unsigned fps_num;
    unsigned fps_den;
} VideoFormat;

/* 数据由生产模块持有；pts_us 使用单调时钟，单位为微秒。 */
typedef struct {
    const uint8_t *data;
    size_t size;
    int64_t pts_us;
    int keyframe;
} MediaPacket;

#endif
