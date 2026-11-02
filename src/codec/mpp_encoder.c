#include "codec/mpp_encoder.h"

#include <rk_mpi.h>
#include <stdio.h>
#include <stdlib.h>

struct MppEncoder {
    MppCtx context;
    MppApi *api;
    MppPacket packet;
    uint8_t headers[4096];
    size_t headers_size;
};

static int configure_encoder(
    MppEncoder *encoder,
    const EncoderConfig *config,
    MppFrame frame)
{
    MppEncCfg settings = NULL;
    MPP_RET result = mpp_enc_cfg_init(&settings);
    if (result != MPP_OK)
        return result;

    result = encoder->api->control(encoder->context, MPP_ENC_GET_CFG, settings);
    if (result != MPP_OK)
        goto done;

    struct {
        const char *name;
        RK_S32 value;
    } values[] = {
        {"prep:width", mpp_frame_get_width(frame)},
        {"prep:height", mpp_frame_get_height(frame)},
        {"prep:hor_stride", mpp_frame_get_hor_stride(frame)},
        {"prep:ver_stride", mpp_frame_get_ver_stride(frame)},
        {"prep:format", mpp_frame_get_fmt(frame)},
        {"prep:range", MPP_FRAME_RANGE_JPEG},
        {"rc:mode", MPP_ENC_RC_MODE_CBR},
        {"rc:bps_target", config->bitrate},
        {"rc:bps_max", config->bitrate / 16 * 17},
        {"rc:bps_min", config->bitrate / 16 * 15},
        {"rc:fps_in_flex", 0},
        {"rc:fps_in_num", config->video.fps_num},
        /* MPP 的帧率分母配置键使用 denorm 拼写。 */
        {"rc:fps_in_denorm", config->video.fps_den},
        {"rc:fps_out_flex", 0},
        {"rc:fps_out_num", config->video.fps_num},
        {"rc:fps_out_denorm", config->video.fps_den},
        {"rc:gop", config->gop},
        {"rc:drop_mode", MPP_ENC_RC_DROP_FRM_DISABLED},
        {"rc:qp_init", -1},
        {"rc:qp_min", 10},
        {"rc:qp_max", 51},
        {"rc:qp_min_i", 10},
        {"rc:qp_max_i", 51},
        {"h264:profile", 100},
        {"h264:level", 52},
        {"h264:cabac_en", 1},
        {"h264:cabac_idc", 0},
        {"h264:trans8x8", 1},
        /* 完整帧输出，应用每送入一帧就取回一个 access unit。 */
        {"split:mode", MPP_ENC_SPLIT_NONE},
    };

    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
        result = mpp_enc_cfg_set_s32(settings, values[i].name, values[i].value);
        if (result != MPP_OK) {
            fprintf(stderr, "MPP: unsupported encoder setting %s\n", values[i].name);
            goto done;
        }
    }

    result = encoder->api->control(encoder->context, MPP_ENC_SET_CFG, settings);

done:
    mpp_enc_cfg_deinit(settings);
    return result;
}

static int read_headers(MppEncoder *encoder)
{
    MppPacket packet = NULL;
    MPP_RET result = mpp_packet_init(
        &packet,
        encoder->headers,
        sizeof(encoder->headers));
    if (result != MPP_OK)
        return result;

    mpp_packet_set_length(packet, 0);
    result = encoder->api->control(encoder->context, MPP_ENC_GET_HDR_SYNC, packet);
    encoder->headers_size = mpp_packet_get_length(packet);
    mpp_packet_deinit(&packet);

    return result == MPP_OK && !encoder->headers_size ? MPP_NOK : result;
}

int mpp_encoder_open(
    MppEncoder **encoder,
    const EncoderConfig *config,
    MppFrame first_frame)
{
    if (mpp_frame_get_fmt(first_frame) != MPP_FMT_YUV420SP ||
        mpp_frame_get_width(first_frame) != config->video.width ||
        mpp_frame_get_height(first_frame) != config->video.height) {
        fprintf(stderr, "MPP: decoder output must match negotiated NV12 geometry\n");
        return -1;
    }

    MppEncoder *instance = calloc(1, sizeof(*instance));
    if (!instance)
        return -1;

    MPP_RET result = mpp_create(&instance->context, &instance->api);
    if (result != MPP_OK)
        goto fail;

    RK_S64 timeout = 2000;
    result = instance->api->control(instance->context, MPP_SET_OUTPUT_TIMEOUT, &timeout);
    if (result != MPP_OK)
        goto fail;

    result = mpp_init(instance->context, MPP_CTX_ENC, MPP_VIDEO_CodingAVC);
    if (result != MPP_OK)
        goto fail;

    result = configure_encoder(instance, config, first_frame);
    if (result != MPP_OK)
        goto fail;

    MppEncHeaderMode header_mode = MPP_ENC_HEADER_MODE_EACH_IDR;
    result = instance->api->control(
        instance->context,
        MPP_ENC_SET_HEADER_MODE,
        &header_mode);
    if (result != MPP_OK)
        goto fail;

    result = read_headers(instance);
    if (result != MPP_OK)
        goto fail;

    *encoder = instance;
    return 0;

fail:
    fprintf(stderr, "MPP: H.264 encoder initialization failed (%d)\n", result);
    mpp_encoder_close(instance);
    return -1;
}

MediaPacket mpp_encoder_headers(const MppEncoder *encoder)
{
    return (MediaPacket) {
        .data = encoder->headers,
        .size = encoder->headers_size,
    };
}

int mpp_encoder_encode(
    MppEncoder *encoder,
    MppFrame frame,
    MediaPacket *packet)
{
    if (encoder->packet)
        mpp_packet_deinit(&encoder->packet);

    MPP_RET result = encoder->api->encode_put_frame(encoder->context, frame);
    if (result != MPP_OK)
        goto fail;

    result = encoder->api->encode_get_packet(encoder->context, &encoder->packet);
    if (result != MPP_OK || !encoder->packet)
        goto fail;

    if (mpp_packet_is_partition(encoder->packet)) {
        fprintf(stderr, "MPP: unexpected partitioned H.264 output\n");
        return -1;
    }

    RK_S32 intra = 0;
    MppMeta metadata = mpp_packet_get_meta(encoder->packet);
    if (metadata)
        mpp_meta_get_s32(metadata, KEY_OUTPUT_INTRA, &intra);

    *packet = (MediaPacket) {
        .data = mpp_packet_get_pos(encoder->packet),
        .size = mpp_packet_get_length(encoder->packet),
        .pts_us = mpp_packet_get_pts(encoder->packet),
        .keyframe = intra,
    };

    return 0;

fail:
    fprintf(stderr, "MPP: H.264 encode failed (%d)\n", result);
    return -1;
}

void mpp_encoder_close(MppEncoder *encoder)
{
    if (!encoder)
        return;

    if (encoder->context)
        mpp_destroy(encoder->context);

    if (encoder->packet)
        mpp_packet_deinit(&encoder->packet);

    free(encoder);
}
