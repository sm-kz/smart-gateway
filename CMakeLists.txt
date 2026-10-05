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
target_compile_options(${APP} PRIVATE -pthread)

# ipc_utils 库
add_library(ipc_utils STATIC ${PROJECT_SOURCE_DIR}/lib/ipc/ipc_utils.c)
target_include_directories(ipc_utils PUBLIC ${PROJECT_SOURCE_DIR}/include)

# camera 库
add_library(camera STATIC ${PROJECT_SOURCE_DIR}/lib/camera/camera.c)
target_include_directories(camera PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(camera PRIVATE rt)

# camera_service 服务程序
add_executable(camera_service ${PROJECT_SOURCE_DIR}/services/camera_service.c)
target_link_libraries(camera_service PRIVATE ipc_utils camera Threads::Threads)

# camera_client 库
add_library(camera_client STATIC ${PROJECT_SOURCE_DIR}/lib/camera_client/camera_client.c)
target_include_directories(camera_client PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(camera_client PRIVATE ipc_utils)

# camera_ctl 调试程序
add_executable(camera_ctl ${PROJECT_SOURCE_DIR}/tools/camera_ctl.c)
target_include_directories(camera_ctl PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(camera_ctl PRIVATE ipc_utils)

# camera_viewer 调试程序
add_executable(camera_viewer ${PROJECT_SOURCE_DIR}/tools/camera_viewer.c)
target_include_directories(camera_viewer PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(camera_viewer PRIVATE camera_client rt)

# web_service 程序
add_executable(web_service ${PROJECT_SOURCE_DIR}/services/web_service.c)
target_link_libraries(web_service PRIVATE camera ipc_utils camera_client display)

# display 库
add_library(display STATIC ${PROJECT_SOURCE_DIR}/lib/display/display.c)
target_include_directories(display PUBLIC ${PROJECT_SOURCE_DIR}/include)

set(BOARD_HOME /home/rance/linux/nfs/rootfs/home/menoa)
add_custom_target(send
                DEPENDS ${APP} camera_service camera_ctl
                COMMAND sudo cp $<TARGET_FILE:${APP}>
                        ${BOARD_HOME}/
                COMMAND sudo cp $<TARGET_FILE:camera_service>
                        ${BOARD_HOME}/
                COMMAND sudo cp $<TARGET_FILE:camera_ctl>
                        ${BOARD_HOME}/
                COMMAND sudo cp $<TARGET_FILE:camera_viewer>
                        ${BOARD_HOME}/
                COMMAND sudo cp $<TARGET_FILE:web_service>
                        ${BOARD_HOME}/
                COMMENT "Copying programs to board"
                )
