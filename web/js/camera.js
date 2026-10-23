let camera_state = document.getElementById("camera-state");
let camera_res = document.getElementById("camera-res");
let camera_format = document.getElementById("camera-format");
let camera_fps = document.getElementById("camera-fps");
let img = document.querySelector("img");
let stream_active = false;

function camera_state_check() {
    send_api(api_get_camera_status)
    .then((data) => {
        // 状态
        update_text(camera_state, data.state === "Running" ? "Running" : "Paused");
        // 分辨率
        update_text(camera_res, data.resolution);
        // 像素格式
        update_text(camera_format, data.format);
        // FPS
        update_text(camera_fps, data.fps);
    })
    .catch(error => {
        console.error("获取摄像头状态失败：", error);
    });
}

function camera_start_button_clicked() {
    if (camera_state.textContent === "Running")
        return;

    send_api(api_post_camera_start, "POST")
    .then(data => {
        console.log(data);

        camera_state_check();
    })
    .catch(error => {
        console.error("获取摄像头开启失败：", error);
    });
}

function camera_stop_button_clicked() {
    if (camera_state.textContent === "Paused")
        return;

    stream_active = false;
    img.removeAttribute("src");

    send_api(api_post_camera_stop, "POST")
    .then(data => {
        console.log(data);
        camera_state_check();
    })
    .catch(error => {
        console.error("获取摄像头关闭失败：", error);
    });
}

function camera_stream_button_clicked() {
    if (stream_active)
        return;

    stream_active = true;
    // 保证每次点击时，img.src 都不同，请求正常发送
    // img.src = server_ip + api_get_camera_stream + "?t=" + Date.now();

    // 针对当前只有开启，没有关闭的情况，不需要发送多个请求
    // 而且 img.src 只能接收一个连接的帧数据
    // 如果在不关闭前一个连接的情况下显示另一个连接的帧，那么前一个连接会因为数据不被接收而一直阻塞，导致卡死
    img.src = server_ip + api_get_camera_stream + "?=" + Date.now();
}