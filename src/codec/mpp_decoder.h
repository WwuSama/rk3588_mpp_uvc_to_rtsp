#ifndef IPC_MPP_DECODER_H
#define IPC_MPP_DECODER_H

#include <mpp_frame.h>

#include "media/types.h"

typedef struct MppDecoder MppDecoder;

int mpp_decoder_open(
    MppDecoder **decoder,
    const VideoFormat *format,
    size_t packet_capacity);

/* 0: 借出 NV12 帧；1: 损坏帧；-1: 致命错误。
 * 帧有效期至下次 decode 或 close；调用方不 deinit。
 */
int mpp_decoder_decode(
    MppDecoder *decoder,
    const MediaPacket *packet,
    MppFrame *frame);

void mpp_decoder_close(MppDecoder *decoder);

#endif
