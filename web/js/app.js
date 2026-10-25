let start_button = document.getElementById("start-button");
let stop_button = document.getElementById("stop-button");
let streamon_button = document.getElementById("streamon-button");
let streamoff_button = document.getElementById("streamoff-button");

let attitude_obj = document.getElementById("attitude");
let heading_obj = document.getElementById("heading");

/* ===================== 主程序入口 ===================== */
attitude_obj.addEventListener("load", attitude_svg_init);
heading_obj.addEventListener("load", attitude_svg_init);

// 如果 SVG 已经加载完成，也尝试初始化一次
attitude_svg_init();
requestAnimationFrame(attitude_animate);

start_button.addEventListener("click", camera_start_button_clicked);
stop_button.addEventListener("click", camera_stop_button_clicked);
streamon_button.addEventListener("click", camera_streamon_button_clicked);
streamoff_button.addEventListener("click", camera_streamoff_button_clicked);

camera_state_check();
let regular_camera_status_check = setInterval(camera_state_check, 3000);

sensor_data_check();
let regular_sensor_data_check = setInterval(sensor_data_check, 50);

system_state_check();
let regular_system_state_check = setInterval(system_state_check, 1000);