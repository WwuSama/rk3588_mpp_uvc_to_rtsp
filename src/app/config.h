#ifndef IPC_APP_CONFIG_H
#define IPC_APP_CONFIG_H

#include "media/types.h"

typedef struct {
    const char *device;
    const char *rtsp_url;
    VideoFormat video;
    int bitrate;
    int gop;
} AppConfig;

/* 0: 成功；1: 已打印帮助；-1: 参数错误。 */
int app_config_parse(AppConfig *config, int argc, char **argv);

#endif
