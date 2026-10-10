const server_ip = "http://192.168.0.50:16767";
const api_get_status = "/api/status";
const api_get_stream = "/api/stream";
const api_post_start = "/api/start";
const api_post_stop = "/api/stop";


let camera_state = document.getElementById("camera-state");
let camera_res = document.getElementById("camera-res");
let camera_format = document.getElementById("camera-format");
let camera_fps = document.getElementById("camera-fps");
let start_button = document.getElementById("start-button");
let stop_button = document.getElementById("stop-button");
let stream_button = document.getElementById("stream-button");
let img = document.querySelector("img");

function camera_api(api, method = "GET") {
    return fetch(server_ip + api, {
        method: method,
        cache: "no-store"   // 实时查询的状态不需要缓存
    })
    .then((response) => {
        if (!response.ok) {
            throw new Error(`HTTP 错误！状态：${response.status}`);
        }

        return response.json();
    });
}

function update_text(element, value) {
    const text = String(value);

    if (element.textContent !== text) {
        element.textContent = text;
    }
}

function camera_state_check(){
    camera_api(api_get_status)
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

    camera_api(api_post_start, "POST")
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

    camera_api(api_post_stop, "POST")
    .then(data => {
        console.log(data);
        img.removeAttribute("src");
        camera_state_check();
    })
    .catch(error => {
        console.error("获取摄像头关闭失败：", error);
    });
}

function camera_stream_button_clicked() {
    // 保证每次点击时，img.src 都不同，请求正常发送
    // img.src = server_ip + api_get_stream + "?t=" + Date.now();

    // 针对当前只有开启，没有关闭的情况，不需要发送多个请求
    // 而且 img.src 只能接收一个连接的帧数据
    // 如果在不关闭前一个连接的情况下显示另一个连接的帧，那么前一个连接会因为数据不被接收而一直阻塞，导致卡死
    img.src = server_ip + api_get_stream;
}

start_button.addEventListener("click", camera_start_button_clicked);
stop_button.addEventListener("click", camera_stop_button_clicked);
stream_button.addEventListener("click", camera_stream_button_clicked);

camera_state_check();
let regular_status_check = setInterval(camera_state_check, 3000);
