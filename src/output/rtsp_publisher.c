#include "output/rtsp_publisher.h"

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/mem.h>
#include <libavutil/time.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct RtspPublisher {
    AVFormatContext *context;
    AVStream *stream;
    AVPacket *packet;
    const volatile sig_atomic_t *stop;
    int connected;
    int64_t deadline_us;
    int64_t first_pts_us;
    int64_t last_pts;
    int64_t duration_us;
};

static int interrupt_io(void *opaque)
{
    const RtspPublisher *publisher = opaque;

    return *publisher->stop || av_gettime_relative() >= publisher->deadline_us;
}

static void begin_io(RtspPublisher *publisher)
{
    publisher->deadline_us = av_gettime_relative() + 5000000;
}

static int report_error(const char *operation, int result)
{
    char message[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(result, message, sizeof(message));
    fprintf(stderr, "RTSP: %s: %s\n", operation, message);

    return -1;
}

int rtsp_publisher_open(
    RtspPublisher **publisher,
    const RtspConfig *config,
    const MediaPacket *headers)
{
    RtspPublisher *instance = calloc(1, sizeof(*instance));
    if (!instance)
        return -1;

    instance->stop = config->stop;
    instance->first_pts_us = AV_NOPTS_VALUE;
    instance->last_pts = AV_NOPTS_VALUE;
    instance->duration_us = av_rescale(config->video.fps_den, 1000000, config->video.fps_num);

    int result = avformat_alloc_output_context2(
        &instance->context,
        NULL,
        "rtsp",
        config->url);
    if (result < 0)
        goto fail;

    instance->context->interrupt_callback = (AVIOInterruptCB) {interrupt_io, instance};
    instance->context->flags |= AVFMT_FLAG_FLUSH_PACKETS;
    instance->stream = avformat_new_stream(instance->context, NULL);
    instance->packet = av_packet_alloc();
    if (!instance->stream || !instance->packet) {
        result = AVERROR(ENOMEM);
        goto fail;
    }

    instance->stream->time_base = (AVRational) {1, 90000};
    instance->stream->avg_frame_rate = (AVRational) {
        config->video.fps_num,
        config->video.fps_den,
    };

    AVCodecParameters *codec = instance->stream->codecpar;
    codec->codec_type = AVMEDIA_TYPE_VIDEO;
    codec->codec_id = AV_CODEC_ID_H264;
    codec->width = config->video.width;
    codec->height = config->video.height;
    codec->bit_rate = config->bitrate;
    codec->format = AV_PIX_FMT_NV12;
    codec->color_range = AVCOL_RANGE_JPEG;
    codec->extradata = av_mallocz(headers->size + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!codec->extradata) {
        result = AVERROR(ENOMEM);
        goto fail;
    }

    memcpy(codec->extradata, headers->data, headers->size);
    codec->extradata_size = (int)headers->size;

    AVDictionary *options = NULL;
    av_dict_set(&options, "rtsp_transport", "tcp", 0);
    begin_io(instance);
    result = avformat_write_header(instance->context, &options);
    av_dict_free(&options);
    if (result < 0)
        goto fail;

    instance->connected = 1;
    *publisher = instance;
    return 0;

fail:
    report_error("connect", result);
    rtsp_publisher_close(instance);
    return -1;
}

int rtsp_publisher_write(RtspPublisher *publisher, const MediaPacket *packet)
{
    if (packet->size > INT_MAX)
        return -1;

    AVPacket *output = publisher->packet;
    int result = av_new_packet(output, (int)packet->size);
    if (result < 0)
        return report_error("allocate packet", result);

    memcpy(output->data, packet->data, packet->size);

    if (publisher->first_pts_us == AV_NOPTS_VALUE)
        publisher->first_pts_us = packet->pts_us;

    /* 保留摄像头采集间隔；无 B 帧，PTS 与 DTS 一致。 */
    output->pts = av_rescale_q(
        packet->pts_us - publisher->first_pts_us,
        AV_TIME_BASE_Q,
        publisher->stream->time_base);

    if (publisher->last_pts != AV_NOPTS_VALUE && output->pts <= publisher->last_pts)
        output->pts = publisher->last_pts + 1;

    publisher->last_pts = output->pts;
    output->dts = output->pts;
    output->duration = av_rescale_q(
        publisher->duration_us,
        AV_TIME_BASE_Q,
        publisher->stream->time_base);
    output->stream_index = publisher->stream->index;
    output->flags = packet->keyframe ? AV_PKT_FLAG_KEY : 0;

    begin_io(publisher);
    result = av_write_frame(publisher->context, output);
    av_packet_unref(output);

    return result < 0 ? report_error("publish", result) : 0;
}

void rtsp_publisher_close(RtspPublisher *publisher)
{
    if (!publisher)
        return;

    if (publisher->connected) {
        begin_io(publisher);
        av_write_trailer(publisher->context);
    }

    av_packet_free(&publisher->packet);
    avformat_free_context(publisher->context);
    free(publisher);
}
