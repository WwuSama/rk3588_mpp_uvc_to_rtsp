cmake_minimum_required(VERSION 3.20)

project(ipc_camera VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
find_package(MPP REQUIRED)
find_package(FFmpeg REQUIRED)

add_compile_definitions(_POSIX_C_SOURCE=200809L)
add_compile_options(-Wall -Wextra -Wpedantic)

add_subdirectory(src)

install(TARGETS ipc_camera RUNTIME DESTINATION bin)
install(FILES config/mediamtx.yml DESTINATION share/ipc_camera)

