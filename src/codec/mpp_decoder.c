#include "codec/mpp_decoder.h"

#include <linux/dma-buf.h>
#include <rk_mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

struct MppDecoder {
    MppCtx context;
    MppApi *api;
    MppBufferGroup group;
    MppBuffer input;
    MppBuffer output;
    MppPacket packet;
    MppFrame frame;
    size_t packet_capacity;
};

static int copy_packet(MppDecoder *decoder, const MediaPacket *packet)
{
    int fd = mpp_buffer_get_fd(decoder->input);
    struct dma_buf_sync sync = {.flags = DMA_BUF_SYNC_START | DMA_BUF_SYNC_WRITE};

    if (ioctl(fd, DMA_BUF_IOCTL_SYNC, &sync) < 0)
        return -1;

    uint8_t *input = mpp_buffer_get_ptr(decoder->input);
    memcpy(input, packet->data, packet->size);
    memset(input + packet->size, 0, 256);

    /* CPU 只写压缩包；交给 JPEG 硬件前完成 DMA cache 同步。 */
    sync.flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_WRITE;
    return ioctl(fd, DMA_BUF_IOCTL_SYNC, &sync);
}

int mpp_decoder_open(
    MppDecoder **decoder,
    const VideoFormat *format,
    size_t packet_capacity)
{
    MppDecoder *instance = calloc(1, sizeof(*instance));
    if (!instance)
        return -1;

    unsigned horizontal_stride = (format->width + 15) & ~15U;
    unsigned vertical_stride = (format->height + 15) & ~15U;
    /* JPEG 硬件按对齐尺寸访问；预留到 4:2:2 的大小，输出强制 NV12。 */
    size_t frame_size = (size_t)horizontal_stride * vertical_stride * 2;
    MppFrameFormat output_format = MPP_FMT_YUV420SP;
    MPP_RET result = mpp_create(&instance->context, &instance->api);

    if (result != MPP_OK)
        goto fail;

    result = mpp_init(instance->context, MPP_CTX_DEC, MPP_VIDEO_CodingMJPEG);
    if (result != MPP_OK)
        goto fail;

    result = instance->api->control(
        instance->context,
        MPP_DEC_SET_OUTPUT_FORMAT,
        &output_format);
    if (result != MPP_OK)
        goto fail;

    result = mpp_buffer_group_get_internal(&instance->group, MPP_BUFFER_TYPE_DRM);
    if (result != MPP_OK)
        goto fail;

    instance->packet_capacity = packet_capacity;
    result = mpp_buffer_get(instance->group, &instance->input, packet_capacity + 256);
    if (result != MPP_OK)
        goto fail;

    result = mpp_buffer_get(instance->group, &instance->output, frame_size);
    if (result != MPP_OK)
        goto fail;

    result = mpp_packet_init_with_buffer(&instance->packet, instance->input);
    if (result != MPP_OK)
        goto fail;

    result = mpp_frame_init(&instance->frame);
    if (result != MPP_OK)
        goto fail;

    mpp_frame_set_width(instance->frame, format->width);
    mpp_frame_set_height(instance->frame, format->height);
    mpp_frame_set_hor_stride(instance->frame, horizontal_stride);
    mpp_frame_set_ver_stride(instance->frame, vertical_stride);
    mpp_frame_set_fmt(instance->frame, output_format);
    mpp_frame_set_buffer(instance->frame, instance->output);

    *decoder = instance;
    return 0;

fail:
    fprintf(stderr, "MPP: JPEG decoder initialization failed (%d)\n", result);
    mpp_decoder_close(instance);
    return -1;
}

int mpp_decoder_decode(
    MppDecoder *decoder,
    const MediaPacket *packet,
    MppFrame *frame)
{
    if (packet->size > decoder->packet_capacity) {
        fprintf(stderr, "MPP: JPEG packet exceeds camera sizeimage\n");
        return -1;
    }

    if (copy_packet(decoder, packet) < 0) {
        perror("MPP: JPEG input DMA sync");
        return -1;
    }

    mpp_packet_set_pos(decoder->packet, mpp_buffer_get_ptr(decoder->input));
    mpp_packet_set_length(decoder->packet, packet->size);
    mpp_packet_set_pts(decoder->packet, packet->pts_us);
    mpp_frame_set_pts(decoder->frame, packet->pts_us);
    mpp_frame_set_errinfo(decoder->frame, 0);
    mpp_frame_set_discard(decoder->frame, 0);

    /* 旧版 MPP 的 MJPEG 使用 task 接口，输入和输出缓冲由本模块持有。 */
    MppTask task = NULL;
    MppFrame output = NULL;
    MPP_RET result = decoder->api->poll(decoder->context, MPP_PORT_INPUT, 2000);
    if (result != MPP_OK)
        goto fail;

    result = decoder->api->dequeue(decoder->context, MPP_PORT_INPUT, &task);
    if (result != MPP_OK)
        goto fail;

    mpp_task_meta_set_packet(task, KEY_INPUT_PACKET, decoder->packet);
    mpp_task_meta_set_frame(task, KEY_OUTPUT_FRAME, decoder->frame);

    result = decoder->api->enqueue(decoder->context, MPP_PORT_INPUT, task);
    if (result != MPP_OK)
        goto fail;

    result = decoder->api->poll(decoder->context, MPP_PORT_OUTPUT, 2000);
    if (result != MPP_OK)
        goto fail;

    result = decoder->api->dequeue(decoder->context, MPP_PORT_OUTPUT, &task);
    if (result != MPP_OK)
        goto fail;

    result = mpp_task_meta_get_frame(task, KEY_OUTPUT_FRAME, &output);
    MPP_RET enqueue_result = decoder->api->enqueue(
        decoder->context,
        MPP_PORT_OUTPUT,
        task);

    if (enqueue_result != MPP_OK)
        result = enqueue_result;

    if (result != MPP_OK || output != decoder->frame)
        goto fail;

    if (mpp_frame_get_errinfo(output) || mpp_frame_get_discard(output)) {
        fprintf(stderr, "MPP: damaged JPEG frame\n");
        return 1;
    }

    *frame = output;
    return 0;

fail:
    /* 失败后由应用退出并销毁 context，禁止复用仍在途的 DMA 缓冲。 */
    fprintf(stderr, "MPP: JPEG decode failed (%d)\n", result);
    return -1;
}

void mpp_decoder_close(MppDecoder *decoder)
{
    if (!decoder)
        return;

    if (decoder->context)
        mpp_destroy(decoder->context);

    if (decoder->frame)
        mpp_frame_deinit(&decoder->frame);

    if (decoder->packet)
        mpp_packet_deinit(&decoder->packet);

    if (decoder->output)
        mpp_buffer_put(decoder->output);

    if (decoder->input)
        mpp_buffer_put(decoder->input);

    if (decoder->group)
        mpp_buffer_group_put(decoder->group);

    free(decoder);
}
