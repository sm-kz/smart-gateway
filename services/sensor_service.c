#include "sensor_protocol.h"
#include "ipc_utils.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <unistd.h>
#include <linux/un.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>

#define MPU6050_DEV_PATH    "/sys/bus/iio/devices/iio:device1"
#define ACCEL_SCALE         MPU6050_DEV_PATH"/in_accel_scale"
#define ACCEL_X_RAW         MPU6050_DEV_PATH"/in_accel_x_raw"
#define ACCEL_Y_RAW         MPU6050_DEV_PATH"/in_accel_y_raw"
#define ACCEL_Z_RAW         MPU6050_DEV_PATH"/in_accel_z_raw"
#define ACCEL_X_CALIBBIAS   MPU6050_DEV_PATH"/in_accel_x_calibbias"
#define ACCEL_Y_CALIBBIAS   MPU6050_DEV_PATH"/in_accel_y_calibbias"
#define ACCEL_Z_CALIBBIAS   MPU6050_DEV_PATH"/in_accel_z_calibbias"
#define GYRO_SCALE          MPU6050_DEV_PATH"/in_anglvel_scale"
#define GYRO_X_RAW          MPU6050_DEV_PATH"/in_anglvel_x_raw"
#define GYRO_Y_RAW          MPU6050_DEV_PATH"/in_anglvel_y_raw"
#define GYRO_Z_RAW          MPU6050_DEV_PATH"/in_anglvel_z_raw"
#define GYRO_X_CALIBBIAS    MPU6050_DEV_PATH"/in_anglvel_x_calibbias"
#define GYRO_Y_CALIBBIAS    MPU6050_DEV_PATH"/in_anglvel_y_calibbias"
#define GYRO_Z_CALIBBIAS    MPU6050_DEV_PATH"/in_anglvel_z_calibbias"
#define TEMP_SCALE          MPU6050_DEV_PATH"/in_temp_scale"
#define TEMP_RAW            MPU6050_DEV_PATH"/in_temp_raw"
#define TEMP_OFFSET         MPU6050_DEV_PATH"/in_temp_offset"

// MPU6050 IIO 设备属性文件描述符
int fd_accel_scale;
int fd_accel_x_raw, fd_accel_y_raw, fd_accel_z_raw;
int fd_accel_x_bias, fd_accel_y_bias, fd_accel_z_bias;
int fd_gyro_scale;
int fd_gyro_x_raw, fd_gyro_y_raw, fd_gyro_z_raw;
int fd_gyro_x_bias, fd_gyro_y_bias, fd_gyro_z_bias;
int fd_temp_scale;
int fd_temp_raw;
int fd_temp_offset;

static sig_atomic_t exit_requested = 0;
static sig_atomic_t sensor_state = 1;   // mpu6050 驱动没有低功耗模式或关机的接口，该变量会一直为 1

int sensor_files_open() {
    fd_accel_scale = open(ACCEL_SCALE, O_RDONLY);
    if (fd_accel_scale < 0)
        return -1;
    fd_accel_x_raw = open(ACCEL_X_RAW, O_RDONLY);
    if (fd_accel_x_raw < 0)
        goto close;
    fd_accel_y_raw = open(ACCEL_Y_RAW, O_RDONLY);
    if (fd_accel_y_raw < 0)
        goto close1;
    fd_accel_z_raw = open(ACCEL_Z_RAW, O_RDONLY);
    if (fd_accel_z_raw < 0)
        goto close2;
    fd_accel_x_bias = open(ACCEL_X_CALIBBIAS, O_RDONLY);
    if (fd_accel_x_bias < 0)
        goto close3;
    fd_accel_y_bias = open(ACCEL_Y_CALIBBIAS, O_RDONLY);
    if (fd_accel_y_bias < 0)
        goto close4;
    fd_accel_z_bias = open(ACCEL_Z_CALIBBIAS, O_RDONLY);
    if (fd_accel_z_bias < 0)
        goto close5;
    
    fd_gyro_scale = open(GYRO_SCALE, O_RDONLY);
    if (fd_gyro_scale < 0)
        goto close6;
    fd_gyro_x_raw = open(GYRO_X_RAW, O_RDONLY);
    if (fd_gyro_x_raw < 0)
        goto close7;
    fd_gyro_y_raw = open(GYRO_Y_RAW, O_RDONLY);
    if (fd_gyro_y_raw < 0)
        goto close8;
    fd_gyro_z_raw = open(GYRO_Z_RAW, O_RDONLY);
    if (fd_gyro_z_raw < 0)
        goto close9;
    fd_gyro_x_bias = open(GYRO_X_CALIBBIAS, O_RDONLY);
    if (fd_gyro_x_bias < 0)
        goto close10;
    fd_gyro_y_bias = open(GYRO_Y_CALIBBIAS, O_RDONLY);
    if (fd_gyro_y_bias < 0)
        goto close11;
    fd_gyro_z_bias = open(GYRO_Z_CALIBBIAS, O_RDONLY);
    if (fd_gyro_z_bias < 0)
        goto close12;

    fd_temp_scale = open(TEMP_SCALE, O_RDONLY);
    if (fd_temp_scale < 0)
        goto close13;
    fd_temp_raw = open(TEMP_RAW, O_RDONLY);
    if (fd_temp_raw < 0)
        goto close14;
    fd_temp_offset = open(TEMP_OFFSET, O_RDONLY);
    if (fd_temp_offset < 0)
        goto close15;

    return 0;

close15:
    close(fd_temp_raw);
close14:
    close(fd_temp_scale);
close13:
    close(fd_gyro_z_bias);
close12:
    close(fd_gyro_y_bias);
close11:
    close(fd_gyro_x_bias);
close10:
    close(fd_gyro_z_raw);
close9:
    close(fd_gyro_y_raw);
close8:
    close(fd_gyro_x_raw);
close7:
    close(fd_gyro_scale);
close6:
    close(fd_accel_z_bias);
close5:
    close(fd_accel_y_bias);
close4:
    close(fd_accel_x_bias);
close3:
    close(fd_accel_z_raw);
close2:
    close(fd_accel_y_raw);
close1:
    close(fd_accel_x_raw);
close:
    close(fd_accel_scale);
    return -1;
}

void sensor_files_close() {
    close(fd_temp_offset);
    close(fd_temp_raw);
    close(fd_temp_scale);
    close(fd_gyro_z_bias);
    close(fd_gyro_y_bias);
    close(fd_gyro_x_bias);
    close(fd_gyro_z_raw);
    close(fd_gyro_y_raw);
    close(fd_gyro_x_raw);
    close(fd_gyro_scale);
    close(fd_accel_z_bias);
    close(fd_accel_y_bias);
    close(fd_accel_x_bias);
    close(fd_accel_z_raw);
    close(fd_accel_y_raw);
    close(fd_accel_x_raw);
    close(fd_accel_scale);
}

int sensor_recv_requeset(int client_fd, struct sensor_request *request) {
    int n = recv_all(client_fd, request, sizeof(*request));
    if (n != sizeof(*request)) {
        if (n < 0)
            perror("[Sensor Service] recv");
        
        fprintf(stderr, "[Sensor Service] Error: %d bytes data is expected to receive, "
                "but %d bytes received\r\n", sizeof(*request), n);

        return EINVAL;
    }

    /* 数据校验 */
    if (request->magic != SENSOR_MAGIC || 
        request->version != SENSOR_VERSION) {
        fprintf(stderr, "[Sensor Service] Invalid sensor magic or version\r\n");
        return EPROTO;
    }

    /* 指令检查 */
    if (request->command >= SENSOR_CMD_MAX) {
        fprintf(stderr, "[Sensor Service] Invalid sensor command\r\n");
        return EBADRQC;
    }

    return 0;
}

int sensor_get_accels(float *x, float *y, float *z) {
    float scale;
    int x_raw, y_raw, z_raw;
    int x_bias, y_bias, z_bias;

    if (pread(fd_accel_scale, &scale, sizeof(scale), 0) != sizeof(scale))
        return -1;
    if (pread(fd_accel_x_raw, &x_raw, sizeof(x_raw), 0) != sizeof(x_raw))
        return -1;
    if (pread(fd_accel_y_raw, &y_raw, sizeof(y_raw), 0) != sizeof(y_raw))
        return -1;
    if (pread(fd_accel_z_raw, &z_raw, sizeof(z_raw), 0) != sizeof(z_raw))
        return -1;
    if (pread(fd_accel_x_bias, &x_bias, sizeof(x_bias), 0) != sizeof(x_bias))
        return -1;
    if (pread(fd_accel_y_bias, &y_bias, sizeof(y_bias), 0) != sizeof(y_bias))
        return -1;
    if (pread(fd_accel_z_bias, &z_bias, sizeof(z_bias), 0) != sizeof(z_bias))
        return -1;

    *x = (x_raw - x_bias) * scale;
    *y = (y_raw - y_bias) * scale;
    *z = (z_raw - z_bias) * scale;

    return 0;
}

int sensor_get_gyros(float *x, float *y, float *z) {
    float scale;
    int x_raw, y_raw, z_raw;
    int x_bias, y_bias, z_bias;

    if (pread(fd_gyro_scale, &scale, sizeof(scale), 0) != sizeof(scale))
        return -1;
    if (pread(fd_gyro_x_raw, &x_raw, sizeof(x_raw), 0) != sizeof(x_raw))
        return -1;
    if (pread(fd_gyro_y_raw, &y_raw, sizeof(y_raw), 0) != sizeof(y_raw))
        return -1;
    if (pread(fd_gyro_z_raw, &z_raw, sizeof(z_raw), 0) != sizeof(z_raw))
        return -1;
    if (pread(fd_gyro_x_bias, &x_bias, sizeof(x_bias), 0) != sizeof(x_bias))
        return -1;
    if (pread(fd_gyro_y_bias, &y_bias, sizeof(y_bias), 0) != sizeof(y_bias))
        return -1;
    if (pread(fd_gyro_z_bias, &z_bias, sizeof(z_bias), 0) != sizeof(z_bias))
        return -1;

    *x = (x_raw - x_bias) * scale;
    *y = (y_raw - y_bias) * scale;
    *z = (z_raw - z_bias) * scale;

    return 0;
}

int sensor_get_temp(float *temp) {
    float scale;
    int temp_raw;
    int temp_offset;

    if (pread(fd_temp_scale, &scale, sizeof(scale), 0) != sizeof(scale))
        return -1;
    if (pread(fd_temp_raw, &temp_raw, sizeof(temp_raw), 0) != sizeof(temp_raw))
        return -1;
    if (pread(fd_temp_offset, &temp_offset, sizeof(temp_offset), 0) != sizeof(temp_offset))
        return -1;
    
    *temp = (temp_raw - temp_offset) * scale;

    return 0;
}

int sensor_get_data(struct sensor_data *data) {
    if (sensor_get_accels(&data->accel_x, &data->accel_y, &data->accel_z)) {
        fprintf(stderr, "[Sensor Service] Error: sensor_get_accels\r\n");
        return -1;
    }

    if (sensor_get_gyros(&data->gyro_x, &data->gyro_y, &data->gyro_z)) {
        fprintf(stderr, "[Sensor Service] Error: sensor_get_gyros\r\n");
        return -1;
    }

    if (sensor_get_temp(&data->temperature)) {
        fprintf(stderr, "[Sensor Service] Error: sensor_get_temp\r\n");
        return -1;
    }

    return 0;
}

static int pipe_ready(int fd, const int ready) {
    if (fd >= 0) {
        char res = ready;
        if (write(fd, &res, 1) != 1) {
            perror("[Sensor Service] ready notify");
            return -1;
        }
        close(fd);
    }

    return 0;
}

int sensor_service(int pipe) {
    /* 打开 IIO 设备所有文件 */
    if (sensor_files_open() < 0) {
        fprintf(stderr, "[Sensor Service] Error: sensor_files_open\r\n");
        return -1;
    }

    /* 建立 socket */
    int server_fd;
    server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("[Sensor Service] socket");
        goto error;
    }

    /* 绑定 socket 路径 */
    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SENSOR_SOCK_PATH, strlen(SENSOR_SOCK_PATH));

    unlink(SENSOR_SOCK_PATH);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("[Sensor Service] bind");
        goto error1;
    }

    /* 监听 */
    if (listen(server_fd, 5) < 0) {
        perror("[Sensor Service] listen");
        goto error2;
    }

    /* 初始化状态填入管道 */
    if (pipe_ready(pipe, 1) < 0) {
        goto error2;
    }

    int client_fd;

    while (!exit_requested) {
        client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0) {
            if (errno == EINTR) {
                if (exit_requested)
                    break;
            }
            perror("[Sensor Service] accept");
            continue;
        }

        struct sensor_request req = {0};
        int ret;
        ret = sensor_recv_requeset(client_fd, &req);

        struct sensor_response resp = {0};
        resp.magic = SENSOR_MAGIC;
        resp.version = SENSOR_VERSION;
        resp.command = req.command;
        resp.result = ret;

        /* 无设备状态控制接口 */

        resp.state = sensor_state;

        if (send_all(client_fd, &resp, sizeof(resp)) < 0) {
            perror("[Sensor Service] send");
            close(client_fd);
            continue;
        }
    }

error2:
    unlink(SENSOR_SOCK_PATH);
error1:
    close(server_fd);
error:
    sensor_files_close();
    pipe_ready(pipe, 0);
    return -1;
}