cmake_minimum_required(VERSION 3.31)
project(smart_gateway)
set(CMAKE_BUILD_TYPE Debug)

# 主程序
file(GLOB SOURCES "${PROJECT_SOURCE_DIR}/src/*.c")
file(GLOB SERVICES "${PROJECT_SOURCE_DIR}/services/*.c")

set(APP smart_gateway)
add_executable(${APP} ${SOURCES} ${SERVICES})

target_include_directories(${APP} PRIVATE
    /usr/local/include
    ${PROJECT_SOURCE_DIR}/include)

set(USR_LIBS /usr/local/lib)
target_link_directories(${APP} PRIVATE 
                        ${USR_LIBS}/jpeg)

target_link_libraries(${APP} PRIVATE
    # 标准库
    Threads::Threads
    m

    # 第三方库
    jpeg 

    # 自定义库
    camera 
    ipc_utils
    http_utils
    camera_client 
    display
    sensor_client
    video_utils)

find_package(Threads REQUIRED)
target_compile_options(${APP} PRIVATE -pthread)

# ipc_utils 库
add_library(ipc_utils STATIC ${PROJECT_SOURCE_DIR}/lib/ipc/ipc_utils.c)
target_include_directories(ipc_utils PUBLIC ${PROJECT_SOURCE_DIR}/include)

# http_utils 库
add_library(http_utils STATIC ${PROJECT_SOURCE_DIR}/lib/http/http_utils.c)
target_include_directories(http_utils PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(http_utils PRIVATE ipc_utils)

# video_utils 库
add_library(video_utils STATIC ${PROJECT_SOURCE_DIR}/lib/video/video_utils.c)
target_include_directories(video_utils PUBLIC 
                        /usr/local/include
                        ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(video_utils PRIVATE jpeg)

# camera 库
add_library(camera STATIC ${PROJECT_SOURCE_DIR}/lib/camera/camera.c)
target_include_directories(camera PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(camera PRIVATE rt)

# camera_client 库
add_library(camera_client STATIC ${PROJECT_SOURCE_DIR}/lib/camera_client/camera_client.c)
target_include_directories(camera_client PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(camera_client PRIVATE ipc_utils)

# display 库
add_library(display STATIC ${PROJECT_SOURCE_DIR}/lib/display/display.c)
target_include_directories(display PUBLIC ${PROJECT_SOURCE_DIR}/include)

# sensor_client 库
add_library(sensor_client STATIC ${PROJECT_SOURCE_DIR}/lib/sensor_client/sensor_client.c)
target_include_directories(sensor_client PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(sensor_client PRIVATE ipc_utils)

# camera_ctl 调试程序
add_executable(camera_ctl ${PROJECT_SOURCE_DIR}/tools/camera_ctl.c)
target_include_directories(camera_ctl PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(camera_ctl PRIVATE ipc_utils)

# camera_viewer 调试程序
add_executable(camera_viewer ${PROJECT_SOURCE_DIR}/tools/camera_viewer.c)
target_include_directories(camera_viewer PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(camera_viewer PRIVATE camera_client rt)

# sensor_ctl 调试程序
add_executable(sensor_ctl ${PROJECT_SOURCE_DIR}/tools/sensor_ctl.c)
target_include_directories(sensor_ctl PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(sensor_ctl PRIVATE ipc_utils)


set(BOARD_HOME /home/rance/linux/nfs/rootfs/home/menoa/program)
add_custom_target(send
                DEPENDS ${APP} camera_service camera_ctl
                COMMAND sudo cp $<TARGET_FILE:${APP}>
                        ${BOARD_HOME}/
                # COMMAND sudo cp $<TARGET_FILE:camera_ctl>
                #         ${BOARD_HOME}/
                # COMMAND sudo cp $<TARGET_FILE:camera_viewer>
                #         ${BOARD_HOME}/
                COMMAND sudo cp $<TARGET_FILE:sensor_ctl>
                        ${BOARD_HOME}/
                COMMENT "Copying programs to board"
                )
