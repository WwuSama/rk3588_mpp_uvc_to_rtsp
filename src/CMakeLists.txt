add_library(camera_capture STATIC capture/uvc_capture.c)
target_include_directories(camera_capture PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

add_library(camera_codec STATIC codec/mpp_decoder.c codec/mpp_encoder.c)
target_include_directories(camera_codec PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(camera_codec PUBLIC MPP::MPP)

add_library(camera_output STATIC output/rtsp_publisher.c)
target_include_directories(camera_output PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(camera_output PRIVATE FFmpeg::FFmpeg)

add_executable(ipc_camera main.c app/config.c app/pipeline.c)
target_link_libraries(ipc_camera PRIVATE camera_capture camera_codec camera_output)
