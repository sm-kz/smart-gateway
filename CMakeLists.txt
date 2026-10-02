cmake_minimum_required(VERSION 3.31)
project(smart_gateway)
set(CMAKE_BUILD_TYPE Debug)

# 主程序
file(GLOB SOURCES "${PROJECT_SOURCE_DIR}/src/*.c")

set(APP smart_gateway)
add_executable(${APP} ${SOURCES})

target_include_directories(${APP} PRIVATE
    /usr/local/include
    ${PROJECT_SOURCE_DIR}/include)

set(USR_LIBS /usr/local/lib)
target_link_directories(${APP} PRIVATE 
                        ${USR_LIBS}/freetype
                        ${USR_LIBS}/jpeg
                        ${USR_LIBS}/libpng
                        ${USR_LIBS}/zlib
                        ${USR_LIBS}/tslib
                        ${USR_LIBS}/paho.mqtt.c
                        ${USR_LIBS}/alsalib)

target_link_libraries(${APP} PRIVATE
    freetype 
    jpeg 
    png 
    ts 
    paho-mqtt3c 
    asound 
    Threads::Threads)

find_package(Threads REQUIRED)
target_compile_options(${APP} PRIVATE -pthread -Wall)

# camera 库
add_library(camera STATIC ${PROJECT_SOURCE_DIR}/lib/camera/camera.c)
target_include_directories(camera PUBLIC ${PROJECT_SOURCE_DIR}/include)

# camera_service 服务程序
add_executable(camera_service services/camera_service.c)
target_link_libraries(camera_service camera Threads::Threads)

# camera_ctl 调试程序
add_executable(camera_ctl tools/camera_ctl.c)
target_include_directories(camera_ctl PRIVATE ${PROJECT_SOURCE_DIR}/include)

set(BOARD_HOME /home/rance/linux/nfs/rootfs/home/menoa)
add_custom_target(send
                DEPENDS ${APP} camera_service camera_ctl
                COMMAND sudo cp $<TARGET_FILE:${APP}>
                        ${BOARD_HOME}/
                COMMAND sudo cp $<TARGET_FILE:camera_service>
                        ${BOARD_HOME}/
                COMMAND sudo cp $<TARGET_FILE:camera_ctl>
                        ${BOARD_HOME}/
                COMMENT "Copying programs to board"
                )
