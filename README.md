# RK3588 UVC Camera

使用 V4L2 采集 UVC 摄像头的 MJPEG 图像，直接调用 Rockchip MPP 解码为
NV12，再调用 MPP 编码为 H.264，通过 RTSP/TCP 发布到板卡上的独立
RTSP 服务。默认请求 3840×2160、30 fps、20 Mbps、GOP 30。

```mermaid
flowchart LR
    UVC[UVC MJPEG] --> Capture[V4L2 MMAP 采集]
    Capture --> Decoder[MPP JPEG 解码]
    Decoder -->|NV12 DMA 缓冲| Encoder[MPP H.264 编码]
    Encoder --> Publisher[FFmpeg RTSP 发布]
    Publisher --> Server[板卡 MediaMTX]
    Server --> Player[电脑 VLC / ffplay 拉流]
```

## 分层与目录

| 目录 | 职责 |
| --- | --- |
| `src/app` | 参数解析、模块装配与逐帧调度 |
| `src/capture` | V4L2 格式协商、MMAP、帧借用与归还 |
| `src/codec` | MPP task、MJPEG 解码、H.264 编码、SPS/PPS |
| `src/output` | H.264 封装、时间戳换算、RTSP 发布 |
| `src/media` | 与设备、协议无关的数据结构 |
| `config` | 独立 RTSP 服务配置 |
| `cmake` | 交叉工具链与依赖查找 |

采集、编解码、输出分别构建成静态库；只有应用层负责串联。
采集层和输出层不依赖 MPP。MPP 的帧句柄只在编解码层与调度层之间传递。
没有添加通用插件框架、复杂继承、额外线程队列或后台服务管理。

## 交叉编译

开发机需要 CMake ≥ 3.20、`aarch64-linux-gnu-gcc` 和板卡 sysroot。
sysroot 需要包含 MPP、libavformat、libavcodec、libavutil 的头文件、动态库，
以及它们的运行依赖。当前仓库旁的 `../../sysroot` 已包含这些库。

```bash
cmake --preset orangepi5pro
cmake --build --preset orangepi5pro --parallel
```

产物为 `build/orangepi5pro/src/ipc_camera`。
工具链路径由 preset 传入，顶层 CMake 不强制设置，方便直接在板卡构建：

```bash
cmake -S . -B build/native -DCMAKE_BUILD_TYPE=Release
cmake --build build/native --parallel
```

如需使用其他 sysroot：

```bash
cmake --preset orangepi5pro -DCMAKE_SYSROOT=/path/to/board/sysroot
```

## 板卡运行

将可执行文件和 `config/mediamtx.yml` 复制到板卡，准备好 Linux ARM64
版本的 [MediaMTX](https://github.com/bluenviron/mediamtx/releases)。
MPP 和 FFmpeg 运行库使用与 sysroot 对应的板卡版本。

先检查摄像头实际支持的 MJPEG 分辨率、帧率和视频节点：

```bash
v4l2-ctl --device /dev/video0 --list-formats-ext
```

在板卡终端一启动 RTSP 服务：

```bash
./mediamtx config/mediamtx.yml
```

在板卡终端二启动采集程序：

```bash
./ipc_camera \
    --device /dev/video0 \
    --width 3840 \
    --height 2160 \
    --fps 30 \
    --bitrate 20000000 \
    --gop 30 \
    --rtsp-url rtsp://127.0.0.1:8554/camera
```

在电脑端拉流，将地址替换为板卡 IP：

```bash
ffplay -rtsp_transport tcp rtsp://192.168.1.100:8554/camera
```

也可以用 VLC 打开相同地址。MediaMTX 负责接收发布和服务多个客户端；
本程序不内置 RTSP 服务，不会自动启动外部进程。
配置中的发布路径为 `camera`，修改 URL 路径时需同步修改服务配置。
配置字段参见 [MediaMTX 官方配置说明](https://mediamtx.org/docs/references/configuration-file)。

## 缓冲与时序

- V4L2 帧由采集层持有，`acquire` 后必须 `release`。
- MJPEG 压缩数据复制到可复用的 MPP DMA 输入缓冲，使用 task 接口兼容旧版 MPP。
- 解码器强制 NV12 输出；编码器按第一张解码帧的真实 stride 配置。
- 解码和编码复用同一张 DMA 图像，不经过 CPU YUV 转换或整帧复制。
- 每张图像同步完成解码、编码和发布后再处理下一帧，避免 DMA 缓冲提前覆写。
- 编码器输出完整 H.264 access unit，启动时获取 SPS/PPS，每个 IDR 重发 SPS/PPS。
- 采集时间戳按微秒贯穿 MPP，发布层以首帧为起点换算到 RTP 90 kHz；编码无 B 帧。
- FFmpeg 仅负责封装与传输，不参与视频解码和编码；发布前复制 H.264 压缩包。
- 当前仅启用 RTSP/TCP，MediaMTX 设置 `udpMaxPayloadSize: 1472`，
  与 FFmpeg 4.4 的 TCP 分包上限对齐，避免服务重新打包。
  该字段也参与服务的 RTP 分包大小计算；若以后启用 UDP，需按网络 MTU 重新配置。
  服务配置关闭 MoQ，仅启动所需的 RTSP 服务。

接口采用借用语义，所有权与有效期在对应头文件中注明。
程序对设备错误、MPP 失败、网络错误打印日志并退出；损坏的采集帧或解码帧会丢弃。
Ctrl+C / SIGTERM 触发清理，网络操作设置 5 秒中断期限；不做自动重连或软件编解码回退。

## 验证边界

当前已使用仓库对应 sysroot 完成交叉编译与链接，产物为 AArch64 ELF。
另在开发机验证了 13 项参数解析与采集设备打开失败清理场景，安装检查通过。
尚未连接 RK3588 和 UVC 摄像头验证实际解码、编码、拉流与吞吐。
启动日志会打印摄像头实际协商的尺寸和帧率，设备可能调整请求值。
4K 30 fps 取决于摄像头模式、USB 带宽、板卡 MPP/驱动及逐帧链路耗时，
编译通过不能证明实机帧率。

上板后建议用 `ffprobe` 确认输出参数，并用播放器检查连续播放、重连和画面颜色：

```bash
ffprobe -v error \
    -rtsp_transport tcp \
    -select_streams v:0 \
    -show_entries stream=codec_name,width,height,avg_frame_rate \
    -of default=noprint_wrappers=1 \
    rtsp://192.168.1.100:8554/camera
```

MPP 接口行为参考 [Rockchip 官方开发文档](https://github.com/rockchip-linux/mpp/blob/develop/doc/Rockchip_Developer_Guide_MPP_CN.md)。
