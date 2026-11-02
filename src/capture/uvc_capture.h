#ifndef IPC_UVC_CAPTURE_H
#define IPC_UVC_CAPTURE_H

#include "media/types.h"

typedef struct UvcCapture UvcCapture;

int uvc_capture_open(
    UvcCapture **capture,
    const char *device,
    const VideoFormat *requested);

const VideoFormat *uvc_capture_format(const UvcCapture *capture);
size_t uvc_capture_packet_capacity(const UvcCapture *capture);

/* 0: 已借出一帧；1: 暂无有效帧；-1: 设备错误。借出的帧必须 release。 */
int uvc_capture_acquire(UvcCapture *capture, MediaPacket *packet);
int uvc_capture_release(UvcCapture *capture);
void uvc_capture_close(UvcCapture *capture);

#endif
