#include "app/config.h"

#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(const char *program)
{
    printf(
        "Usage: %s [options]\n"
        "  --device PATH    UVC capture device (default /dev/video0)\n"
        "  --width N        MJPEG width (default 3840)\n"
        "  --height N       MJPEG height (default 2160)\n"
        "  --fps N          Capture frame rate (default 30)\n"
        "  --bitrate N      H.264 bitrate in bits/s (default 20000000)\n"
        "  --gop N          IDR interval in frames (default 30)\n"
        "  --rtsp-url URL   Publish URL (default rtsp://127.0.0.1:8554/camera)\n"
        "  --help           Show help\n",
        program);
}

static int parse_number(const char *text, int maximum, int *value)
{
    char *end = NULL;
    errno = 0;
    long number = strtol(text, &end, 10);

    if (errno || end == text || *end || number <= 0 || number > maximum) {
        fprintf(stderr, "Invalid numeric argument: %s\n", text);
        return -1;
    }

    *value = (int)number;
    return 0;
}

int app_config_parse(AppConfig *config, int argc, char **argv)
{
    enum { OPTION_HELP = 256 };

    *config = (AppConfig) {
        .device = "/dev/video0",
        .rtsp_url = "rtsp://127.0.0.1:8554/camera",
        .video = {3840, 2160, 30, 1},
        .bitrate = 20000000,
        .gop = 30,
    };

    static const struct option options[] = {
        {"device", required_argument, NULL, 'd'},
        {"width", required_argument, NULL, 'w'},
        {"height", required_argument, NULL, 'h'},
        {"fps", required_argument, NULL, 'f'},
        {"bitrate", required_argument, NULL, 'b'},
        {"gop", required_argument, NULL, 'g'},
        {"rtsp-url", required_argument, NULL, 'u'},
        {"help", no_argument, NULL, OPTION_HELP},
        {NULL, 0, NULL, 0},
    };

    int option;
    while ((option = getopt_long(argc, argv, "", options, NULL)) != -1) {
        int value;
        int maximum;

        switch (option) {
        case 'd':
            config->device = optarg;
            break;
        case 'u':
            config->rtsp_url = optarg;
            break;
        case 'w':
        case 'h':
        case 'f':
        case 'b':
        case 'g':
            maximum = option == 'b' ? INT_MAX / 17 * 16 : 16384;
            if (parse_number(optarg, maximum, &value) < 0)
                return -1;

            switch (option) {
            case 'w':
                config->video.width = (unsigned)value;
                break;
            case 'h':
                config->video.height = (unsigned)value;
                break;
            case 'f':
                config->video.fps_num = (unsigned)value;
                break;
            case 'b':
                config->bitrate = value;
                break;
            case 'g':
                config->gop = value;
                break;
            }
            break;
        case OPTION_HELP:
            print_usage(argv[0]);
            return 1;
        default:
            return -1;
        }
    }

    if (optind != argc || strncmp(config->rtsp_url, "rtsp://", 7) != 0 ||
        config->video.width % 2 || config->video.height % 2) {
        fprintf(stderr, "Expected an RTSP URL, even dimensions and no positional arguments\n");
        return -1;
    }

    return 0;
}
