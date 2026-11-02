#include "app/pipeline.h"

#include <stdio.h>

#include "capture/uvc_capture.h"
#include "codec/mpp_decoder.h"
#include "codec/mpp_encoder.h"
#include "output/rtsp_publisher.h"

typedef struct {
    UvcCapture *capture;
    MppDecoder *decoder;
    MppEncoder *encoder;
    RtspPublisher *publisher;
    VideoFormat format;
} Pipeline;

static int open_output(
    Pipeline *pipeline,
    const AppConfig *config,
    const volatile sig_atomic_t *stop,
    MppFrame frame)
{
    EncoderConfig encoder_config = {
        .video = pipeline->format,
        .bitrate = config->bitrate,
        .gop = config->gop,
    };

    if (mpp_encoder_open(&pipeline->encoder, &encoder_config, frame) < 0)
        return -1;

    RtspConfig rtsp_config = {
        .url = config->rtsp_url,
        .video = pipeline->format,
        .bitrate = config->bitrate,
        .stop = stop,
    };
    MediaPacket headers = mpp_encoder_headers(pipeline->encoder);

    return rtsp_publisher_open(&pipeline->publisher, &rtsp_config, &headers);
}

static int process_frame(
    Pipeline *pipeline,
    const AppConfig *config,
    const volatile sig_atomic_t *stop)
{
    MediaPacket jpeg = {0};
    int result = uvc_capture_acquire(pipeline->capture, &jpeg);
    if (result != 0) {
        if (result < 0)
            fprintf(stderr, "UVC: capture failed\n");

        return result;
    }

    MppFrame frame = NULL;
    result = mpp_decoder_decode(pipeline->decoder, &jpeg, &frame);

    /* 压缩帧已被解码器消费，立即归还 UVC 缓冲。 */
    if (uvc_capture_release(pipeline->capture) < 0) {
        fprintf(stderr, "UVC: requeue failed\n");
        return -1;
    }

    if (result != 0)
        return result;

    if (!pipeline->encoder && open_output(pipeline, config, stop, frame) < 0)
        return -1;

    /* 同步取回编码结果后，下一轮才允许解码器覆写同一个 DMA 缓冲。 */
    MediaPacket h264 = {0};
    if (mpp_encoder_encode(pipeline->encoder, frame, &h264) < 0)
        return -1;

    return rtsp_publisher_write(pipeline->publisher, &h264);
}

int pipeline_run(const AppConfig *config, const volatile sig_atomic_t *stop)
{
    Pipeline pipeline = {0};
    int result = -1;

    if (uvc_capture_open(&pipeline.capture, config->device, &config->video) < 0)
        goto done;

    pipeline.format = *uvc_capture_format(pipeline.capture);
    fprintf(
        stderr,
        "UVC: MJPEG %ux%u @ %u/%u fps\n",
        pipeline.format.width,
        pipeline.format.height,
        pipeline.format.fps_num,
        pipeline.format.fps_den);

    if (mpp_decoder_open(
            &pipeline.decoder,
            &pipeline.format,
            uvc_capture_packet_capacity(pipeline.capture)) < 0)
        goto done;

    while (!*stop) {
        if (process_frame(&pipeline, config, stop) < 0)
            goto done;
    }

    result = 0;

done:
    rtsp_publisher_close(pipeline.publisher);
    /* 编码器可能仍引用解码缓冲，必须先销毁编码器。 */
    mpp_encoder_close(pipeline.encoder);
    mpp_decoder_close(pipeline.decoder);
    uvc_capture_close(pipeline.capture);

    return *stop ? 0 : result;
}
