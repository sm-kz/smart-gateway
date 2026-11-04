let camera_state = document.getElementById("camera-state");
let camera_res = document.getElementById("camera-res");
let camera_format = document.getElementById("camera-format");
let camera_fps = document.getElementById("camera-fps");
let img = document.querySelector("img");
let stream_active = false;
let stream_id = null;

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

function camera_streamon_button_clicked() {
    if (stream_active)
        return;

    stream_active = true;

    stream_id = Date.now().toString() + "-" +
                Math.random().toString(36).slice(2);

    img.src = server_ip + api_get_camera_streamon + "?id=" + encodeURIComponent(stream_id);
}

async function camera_streamoff_button_clicked() {
    if (!stream_active)
        return;

    stream_active = false;
    
    const id = stream_id;
    stream_id = null;

    // 关闭 MJPEG 连接
    img.src = "";

    // 发送关闭流客户端的报文
    try {
        const response = await fetch(server_ip + api_post_camera_streamoff, {
            method: "POST",
            headers: {
                "Content-Type": "text/plain"
            },
            body: id
        });

        if (!response.ok)
            console.error("streamoff failed:", response.status);

    } catch (error) {
        console.error("streamoff error:", error);
    }
}