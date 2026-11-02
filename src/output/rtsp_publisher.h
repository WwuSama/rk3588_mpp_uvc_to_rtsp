#ifndef IPC_RTSP_PUBLISHER_H
#define IPC_RTSP_PUBLISHER_H

#include <signal.h>

#include "media/types.h"

typedef struct RtspPublisher RtspPublisher;

typedef struct {
    const char *url;
    VideoFormat video;
    int bitrate;
    const volatile sig_atomic_t *stop;
} RtspConfig;

int rtsp_publisher_open(
    RtspPublisher **publisher,
    const RtspConfig *config,
    const MediaPacket *headers);

int rtsp_publisher_write(RtspPublisher *publisher, const MediaPacket *packet);
void rtsp_publisher_close(RtspPublisher *publisher);

#endif
