let cpu_usage = document.getElementById("sys-cpu");
let mem_usage = document.getElementById("sys-memory");
let uptime = document.getElementById("sys-uptime");

function system_state_check() {
    send_api(api_get_system_state)
    .then((data) => {
        update_text(cpu_usage, data.cpu_usage);
        update_text(mem_usage, data.mem_usage);
        update_text(uptime, data.uptime);
    })
    .catch(error => {
        console.error("系统状态获取失败：", error);
    });
}