find_path(FFmpeg_INCLUDE_DIR NAMES libavformat/avformat.h
    PATH_SUFFIXES aarch64-linux-gnu x86_64-linux-gnu
)

foreach(component avformat avcodec avutil)
    find_library(FFmpeg_${component}_LIBRARY NAMES ${component})
endforeach()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(FFmpeg REQUIRED_VARS
    FFmpeg_INCLUDE_DIR
    FFmpeg_avformat_LIBRARY
    FFmpeg_avcodec_LIBRARY
    FFmpeg_avutil_LIBRARY
)

if(FFmpeg_FOUND AND NOT TARGET FFmpeg::FFmpeg)
    add_library(FFmpeg::FFmpeg INTERFACE IMPORTED)
    set_target_properties(FFmpeg::FFmpeg PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${FFmpeg_INCLUDE_DIR}"
        INTERFACE_LINK_LIBRARIES "${FFmpeg_avformat_LIBRARY};${FFmpeg_avcodec_LIBRARY};${FFmpeg_avutil_LIBRARY}"
    )
endif()
