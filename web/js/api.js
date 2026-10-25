// 开发板服务 IP
const server_ip = "http://192.168.0.50:16767";
// 摄像头 api
const api_post_camera_start = "/api/camera/start";
const api_post_camera_stop = "/api/camera/stop";
const api_get_camera_streamon = "/api/camera/streamon";
const api_post_camera_streamoff = "/api/camera/streamoff";
const api_get_camera_status = "/api/camera/status";
// 传感器 api
const api_get_sensor_data = "/api/sensor/data";
// 系统监测 api
const api_get_system_state = "/api/system/state" 


function send_api(api, method = "GET") {
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