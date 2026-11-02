#include "capture/uvc_capture.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    void *data;
    size_t size;
} CaptureBuffer;

struct UvcCapture {
    int fd;
    int streaming;
    VideoFormat format;
    size_t packet_capacity;
    CaptureBuffer *buffers;
    unsigned buffer_count;
    struct v4l2_buffer current;
};

static int device_ioctl(int fd, unsigned long request, void *argument)
{
    int result;

    do {
        result = ioctl(fd, request, argument);
    } while (result < 0 && errno == EINTR);

    return result;
}

static int configure_device(UvcCapture *capture, const VideoFormat *requested)
{
    struct v4l2_capability capability = {0};
    struct v4l2_format format = {0};
    struct v4l2_streamparm parameters = {0};

    if (device_ioctl(capture->fd, VIDIOC_QUERYCAP, &capability) < 0)
        return -1;

    unsigned caps = capability.capabilities;
    if (caps & V4L2_CAP_DEVICE_CAPS)
        caps = capability.device_caps;

    if (!(caps & V4L2_CAP_VIDEO_CAPTURE) || !(caps & V4L2_CAP_STREAMING)) {
        fprintf(stderr, "UVC: device requires single-plane streaming capture\n");
        return -1;
    }

    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    format.fmt.pix.width = requested->width;
    format.fmt.pix.height = requested->height;
    format.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
    format.fmt.pix.field = V4L2_FIELD_NONE;

    if (device_ioctl(capture->fd, VIDIOC_S_FMT, &format) < 0)
        return -1;

    if (format.fmt.pix.pixelformat != V4L2_PIX_FMT_MJPEG) {
        fprintf(stderr, "UVC: camera did not accept MJPEG\n");
        return -1;
    }

    parameters.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (device_ioctl(capture->fd, VIDIOC_G_PARM, &parameters) < 0)
        return -1;

    if (!(parameters.parm.capture.capability & V4L2_CAP_TIMEPERFRAME)) {
        fprintf(stderr, "UVC: camera does not expose its frame interval\n");
        return -1;
    }

    parameters.parm.capture.timeperframe.numerator = requested->fps_den;
    parameters.parm.capture.timeperframe.denominator = requested->fps_num;
    if (device_ioctl(capture->fd, VIDIOC_S_PARM, &parameters) < 0)
        return -1;

    struct v4l2_fract interval = parameters.parm.capture.timeperframe;
    if (!interval.numerator || !interval.denominator)
        return -1;

    capture->format = (VideoFormat) {
        .width = format.fmt.pix.width,
        .height = format.fmt.pix.height,
        .fps_num = interval.denominator,
        .fps_den = interval.numerator,
    };
    capture->packet_capacity = format.fmt.pix.sizeimage;

    return 0;
}

static int start_capture(UvcCapture *capture)
{
    struct v4l2_requestbuffers request = {
        .count = 4,
        .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
        .memory = V4L2_MEMORY_MMAP,
    };

    if (device_ioctl(capture->fd, VIDIOC_REQBUFS, &request) < 0 || !request.count)
        return -1;

    capture->buffers = calloc(request.count, sizeof(*capture->buffers));
    if (!capture->buffers)
        return -1;

    capture->buffer_count = request.count;

    for (unsigned i = 0; i < request.count; ++i) {
        struct v4l2_buffer buffer = {
            .type = request.type,
            .memory = request.memory,
            .index = i,
        };

        if (device_ioctl(capture->fd, VIDIOC_QUERYBUF, &buffer) < 0)
            return -1;

        void *data = mmap(
            NULL,
            buffer.length,
            PROT_READ | PROT_WRITE,
            MAP_SHARED,
            capture->fd,
            buffer.m.offset);

        if (data == MAP_FAILED)
            return -1;

        capture->buffers[i] = (CaptureBuffer) {data, buffer.length};

        if (device_ioctl(capture->fd, VIDIOC_QBUF, &buffer) < 0)
            return -1;
    }

    enum v4l2_buf_type type = request.type;
    if (device_ioctl(capture->fd, VIDIOC_STREAMON, &type) < 0)
        return -1;

    capture->streaming = 1;
    return 0;
}

int uvc_capture_open(
    UvcCapture **capture,
    const char *device,
    const VideoFormat *requested)
{
    UvcCapture *instance = calloc(1, sizeof(*instance));
    if (!instance)
        return -1;

    instance->fd = open(device, O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (instance->fd < 0 || configure_device(instance, requested) < 0 ||
        start_capture(instance) < 0) {
        fprintf(stderr, "UVC: open/configure %s failed (errno=%d)\n", device, errno);
        uvc_capture_close(instance);
        return -1;
    }

    *capture = instance;
    return 0;
}

const VideoFormat *uvc_capture_format(const UvcCapture *capture)
{
    return &capture->format;
}

size_t uvc_capture_packet_capacity(const UvcCapture *capture)
{
    return capture->packet_capacity;
}

int uvc_capture_acquire(UvcCapture *capture, MediaPacket *packet)
{
    struct pollfd descriptor = {.fd = capture->fd, .events = POLLIN};
    int result = poll(&descriptor, 1, 1000);

    if (result == 0 || (result < 0 && errno == EINTR))
        return 1;

    if (result < 0 || descriptor.revents & (POLLERR | POLLHUP | POLLNVAL))
        return -1;

    capture->current = (struct v4l2_buffer) {
        .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
        .memory = V4L2_MEMORY_MMAP,
    };

    if (device_ioctl(capture->fd, VIDIOC_DQBUF, &capture->current) < 0)
        return errno == EAGAIN ? 1 : -1;

    struct v4l2_buffer *buffer = &capture->current;
    if (buffer->index >= capture->buffer_count)
        return -1;

    if (!buffer->bytesused || buffer->flags & V4L2_BUF_FLAG_ERROR ||
        buffer->bytesused > capture->buffers[buffer->index].size) {
        return uvc_capture_release(capture) < 0 ? -1 : 1;
    }

    int64_t pts_us;
    if ((buffer->flags & V4L2_BUF_FLAG_TIMESTAMP_MASK) ==
        V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC) {
        pts_us = (int64_t)buffer->timestamp.tv_sec * 1000000 +
            buffer->timestamp.tv_usec;
    } else {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        pts_us = (int64_t)now.tv_sec * 1000000 + now.tv_nsec / 1000;
    }

    *packet = (MediaPacket) {
        .data = capture->buffers[buffer->index].data,
        .size = buffer->bytesused,
        .pts_us = pts_us,
    };

    return 0;
}

int uvc_capture_release(UvcCapture *capture)
{
    return device_ioctl(capture->fd, VIDIOC_QBUF, &capture->current);
}

void uvc_capture_close(UvcCapture *capture)
{
    if (!capture)
        return;

    if (capture->streaming) {
        enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        device_ioctl(capture->fd, VIDIOC_STREAMOFF, &type);
    }

    for (unsigned i = 0; i < capture->buffer_count; ++i) {
        if (capture->buffers[i].data)
            munmap(capture->buffers[i].data, capture->buffers[i].size);
    }

    free(capture->buffers);

    if (capture->fd >= 0)
        close(capture->fd);

    free(capture);
}
