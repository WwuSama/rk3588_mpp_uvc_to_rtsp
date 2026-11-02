#ifndef IPC_MPP_ENCODER_H
#define IPC_MPP_ENCODER_H

#include <mpp_frame.h>

#include "media/types.h"

typedef struct MppEncoder MppEncoder;

typedef struct {
    VideoFormat video;
    int bitrate;
    int gop;
} EncoderConfig;

int mpp_encoder_open(
    MppEncoder **encoder,
    const EncoderConfig *config,
    MppFrame first_frame);

/* SPS/PPS 与编码包均由编码器持有；编码包有效期至下次 encode 或 close。 */
MediaPacket mpp_encoder_headers(const MppEncoder *encoder);

/* 同步取回完整编码帧，成功后调用方可复用输入图像缓冲。 */
int mpp_encoder_encode(
    MppEncoder *encoder,
    MppFrame frame,
    MediaPacket *packet);

void mpp_encoder_close(MppEncoder *encoder);

#endif
