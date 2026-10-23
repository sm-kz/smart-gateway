let sensor_accel_x = document.getElementById("accel-x");
let sensor_accel_y = document.getElementById("accel-y");
let sensor_accel_z = document.getElementById("accel-z");
let sensor_gyro_x = document.getElementById("gyro-x");
let sensor_gyro_y = document.getElementById("gyro-y");
let sensor_gyro_z = document.getElementById("gyro-z");
let sensor_roll = document.getElementById("roll");
let sensor_pitch = document.getElementById("pitch");
let sensor_yaw = document.getElementById("yaw");

// 动画相关属性
let current_roll = 0;
let current_pitch = 0;
let current_yaw = 0;
let target_roll = 0;
let target_pitch = 0;
let target_yaw = 0;

let last_frame_time = null;

let roll_element = null;
let pitch_element = null;
let yaw_element = null;

function sensor_data_check() {
    send_api(api_get_sensor_data)
    .then((data) => {
        update_text(sensor_accel_x, data.accel_x);
        update_text(sensor_accel_y, data.accel_y);
        update_text(sensor_accel_z, data.accel_z);
        update_text(sensor_gyro_x, data.gyro_x);
        update_text(sensor_gyro_y, data.gyro_y);
        update_text(sensor_gyro_z, data.gyro_z);
        update_text(sensor_roll, data.roll);
        update_text(sensor_pitch, data.pitch);
        update_text(sensor_yaw, data.yaw);

        target_roll = Number(data.roll);
        target_pitch = Number(data.pitch);
        target_yaw = Number(data.yaw);
    })
    .catch(error => {
        console.error("获取传感器数据失败：", error);
    });
}

function attitude_svg_init() {
    const attitude = document.getElementById("attitude");
    const heading = document.getElementById("heading");

    if (!attitude || !heading)
        return false;

    const attitude_doc = attitude.contentDocument;
    const heading_doc = heading.contentDocument;

    if (!attitude_doc || !heading_doc)
        return false;

    roll_element = attitude_doc.getElementById("horizon-rotation");
    pitch_element = attitude_doc.getElementById("horizon-translation");
    yaw_element = heading_doc.getElementById("heading-rotation");

    return !!(roll_element && pitch_element && yaw_element)
}

function attitude_svg_update(roll, pitch, yaw) {
    if (![roll, pitch, yaw].every(Number.isFinite))
        return;

    roll_element.setAttribute("transform", `rotate(${-roll} 150, 150)`);
    pitch_element.setAttribute("transform", `translate(0 ${pitch * 3})`);
    yaw_element.setAttribute("transform", `rotate(${-yaw} 150 150)`);
}

function attitude_animate(timestamp) {
    if (last_frame_time === null)
        last_frame_time = timestamp;

    const dt = Math.min((timestamp - last_frame_time) / 1000, 0.1);
    last_frame_time = timestamp;

    const factor = 1 - Math.exp(-dt / 0.1);

    current_roll = lerp_angle(current_roll, target_roll, factor);
    current_pitch += (target_pitch - current_pitch) * factor;
    current_yaw = lerp_angle(current_yaw, target_yaw, factor);

    if (roll_element && pitch_element && yaw_element) {
        attitude_svg_update(current_roll, current_pitch, current_yaw);
    }
    requestAnimationFrame(attitude_animate);
}