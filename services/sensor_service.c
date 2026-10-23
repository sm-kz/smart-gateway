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
#include <stdlib.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <time.h>
#include <signal.h>

static volatile sig_atomic_t exit_requested = 0;
static void sigterm_handler(int sig) {
    (void)sig;
    exit_requested = 1;
}

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

#define PI      3.141592653589f
#define EPS     1e-6f
#define RAD_TO_DEG(rad)     (180.0f / PI * rad)
#define DEG_TO_RAD(deg)     (PI / 180.0f * deg)

#define SAMPLE_PERIOD     10 // ms

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

/* 互补滤波器, 弧度制 */
struct {
    float roll;
    float pitch;
    float yaw;

    float tau;
    int initialized;
} attitude_filter;

/* 服务程序内部维护的传感器数据 */
static struct sensor_data private_sdata = {0};
pthread_mutex_t mutex;  // 数据锁

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

static int iio_read_int(int fd, int *res) {
    char buf[64];
    char *end;

    ssize_t n = pread(fd, buf, sizeof(buf) - 1, 0);
    if (n <= 0)
        return -1;

    buf[n] = '\0';
    errno = 0;

    long val = strtol(buf, &end, 10);

    if (errno != 0 || end == buf || val > INT_MAX || val < INT_MIN)
        return -1;

    *res = val;
    return 0;
}

static int iio_read_float(int fd, float *res) {
    char buf[64];
    char *end;

    ssize_t n = pread(fd, buf, sizeof(buf) - 1, 0);
    if (n <= 0)
        return -1;

    buf[n] = '\0';
    errno = 0;

    float val = strtof(buf, &end);

    if (errno != 0 || end == buf || !isfinite(val))
        return -1;

    *res = val;
    return 0;
}

int sensor_get_accels(float *x, float *y, float *z) {
    float scale;
    int x_raw, y_raw, z_raw;
    int x_bias, y_bias, z_bias;

    if (iio_read_float(fd_accel_scale, &scale))
        return -1;
    if (iio_read_int(fd_accel_x_raw, &x_raw))
        return -1;
    if (iio_read_int(fd_accel_y_raw, &y_raw))
        return -1;
    if (iio_read_int(fd_accel_z_raw, &z_raw))
        return -1;
    if (iio_read_int(fd_accel_x_bias, &x_bias))
        return -1;
    if (iio_read_int(fd_accel_y_bias, &y_bias))
        return -1;
    if (iio_read_int(fd_accel_z_bias, &z_bias))
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

    if (iio_read_float(fd_gyro_scale, &scale))
        return -1;
    if (iio_read_int(fd_gyro_x_raw, &x_raw))
        return -1;
    if (iio_read_int(fd_gyro_y_raw, &y_raw))
        return -1;
    if (iio_read_int(fd_gyro_z_raw, &z_raw))
        return -1;
    if (iio_read_int(fd_gyro_x_bias, &x_bias))
        return -1;
    if (iio_read_int(fd_gyro_y_bias, &y_bias))
        return -1;
    if (iio_read_int(fd_gyro_z_bias, &z_bias))
        return -1;

    *x = DEG_TO_RAD((x_raw - x_bias) * scale);
    *y = DEG_TO_RAD((y_raw - y_bias) * scale);
    *z = DEG_TO_RAD((z_raw - z_bias) * scale);

    return 0;
}

int sensor_get_temp(float *temp) {
    float scale;
    int temp_raw;
    int temp_offset;

    if (iio_read_float(fd_temp_scale, &scale))
        return -1;
    if (iio_read_int(fd_temp_raw, &temp_raw))
        return -1;
    if (iio_read_int(fd_temp_offset, &temp_offset))
        return -1;
    
    *temp = (temp_raw - temp_offset) * scale;

    return 0;
}

int sensor_get_data_raw(struct sensor_data *data) {
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

static float wrap_pi(float angle) {
    return remainderf(angle, 2 * PI);
}

void attitude_init(float tau) {
    memset(&attitude_filter, 0, sizeof(attitude_filter));
    attitude_filter.tau = tau > 0.0f ? tau : 0.5f;
}

int attitude_update(struct sensor_data *data, float dt) {
    if (!data || !isfinite(dt) || dt <= 0 || dt > 0.4)
        return -1;

    float norm = sqrtf(data->accel_x * data->accel_x +
                       data->accel_y * data->accel_y +
                       data->accel_z * data->accel_z);

    if (norm < EPS)
        return -1;

    // 加速度计计算 roll / pitch
    float roll_acc = atan2f(data->accel_y, data->accel_z);
    float pitch_acc = atan2f(
                            -data->accel_x,
                            sqrtf(data->accel_y * data->accel_y + data->accel_z * data->accel_z)
                        );

    // 首次采样，利用重力加速度初始化
    if (!attitude_filter.initialized) {
        attitude_filter.roll = roll_acc;
        attitude_filter.pitch = pitch_acc;
        attitude_filter.yaw = 0;
        attitude_filter.initialized = 1;
        return 0;
    }

    float roll = attitude_filter.roll;
    float pitch = attitude_filter.pitch;

    float sr = sinf(roll);
    float cr = cosf(roll);
    float cp = cosf(pitch);

    // 欧拉角在正负90度附近存在奇异性
    if (fabsf(cp) < 0.01f) {
        attitude_filter.roll = roll_acc;
        attitude_filter.pitch = pitch_acc;

        // 保留当前相对 Yaw，避免突然归零
        return 0;
    }

    float tp = tanf(pitch);

    // 机体角速度转为欧拉角变化率
    float roll_rate = data->gyro_x + sr * tp * data->gyro_y + cr * tp * data->gyro_z;
    float pitch_rate = cr * data->gyro_y - sr * data->gyro_z;
    float yaw_rate = sr / cp * data->gyro_y + cr / cp * data->gyro_z;

    // 陀螺仪预测
    float roll_gyro = roll + roll_rate * dt;
    float pitch_gyro = pitch + pitch_rate * dt;
    float yaw_gyro = attitude_filter.yaw + yaw_rate * dt;

    // 根据采样间隔计算滤波系数
    float alpha = attitude_filter.tau / (attitude_filter.tau + dt);

    // roll 使用角度差修正，避免180度跳变
    float roll_error = wrap_pi(roll_acc - roll_gyro);

    attitude_filter.roll = wrap_pi(roll_gyro + (1.0f - alpha) * roll_error);
    attitude_filter.pitch = wrap_pi(alpha * pitch_gyro + (1.0f - alpha) * pitch_acc);
    attitude_filter.yaw = wrap_pi(yaw_gyro);

    data->roll = RAD_TO_DEG(attitude_filter.roll);
    data->pitch = RAD_TO_DEG(attitude_filter.pitch);
    data->yaw = RAD_TO_DEG(attitude_filter.yaw);

    return 0;
}

void *sample_routine(void *arg) {
    /* 初始化滤波器 */
    attitude_init(0.2f);

    static struct timespec start_time = {0};
    static struct timespec end_time = {0};

    while (!exit_requested) {
        struct sensor_data data = {0};
        // 获取原始数据
        if (sensor_get_data_raw(&data))
            continue;

        clock_gettime(CLOCK_MONOTONIC, &end_time);

        double real_interval;
        // 使用系统时间计算 dt，避免误差积累
        if (start_time.tv_sec == 0 && start_time.tv_nsec == 0)
            real_interval = SAMPLE_PERIOD / 1000.0;
        else
            real_interval = end_time.tv_sec - start_time.tv_sec + (end_time.tv_nsec - start_time.tv_nsec) * 1e-9;

        clock_gettime(CLOCK_MONOTONIC, &start_time);

        // 计算并更新 roll、pitch、yaw
        if (attitude_update(&data, real_interval))
            continue;

        // 更新私有数据
        pthread_mutex_lock(&mutex);
        private_sdata = data;
        pthread_mutex_unlock(&mutex);
        
        usleep(SAMPLE_PERIOD * 1000);
    }

    return NULL;
}

static int pipe_ready(int fd, const int ready) {
    if (fd >= 0) {
        char res = ready;
        if (write(fd, &res, 1) != 1) {
            perror("[Sensor Service] ready notify");
            return -1;
        }
        close(fd);
        fd = -1;
    }

    return 0;
}

int sensor_service(int pipe) {
    /* 初始化 SIGINT 信号处理 */
    struct sigaction sa = {0};
    sa.sa_handler = sigterm_handler;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGTERM, &sa, NULL) < 0) {
        perror("[Sensor Service] sigaction\r\n");
        return -1;
    }

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

    /* 初始化数据锁 */
    pthread_mutex_init(&mutex, NULL);

    /* 创建采样线程 */
    pthread_t sample_pid;
    if (pthread_create(&sample_pid, NULL, sample_routine, NULL)) {
        fprintf(stderr, "[Sensor Service] Error: pthread_create");
        goto error2;
    }

    /* 初始化状态填入管道 */
    if (pipe_ready(pipe, 1) < 0) {
        goto error3;
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

        /* 根据指令执行响应的操作 */
        switch (req.command)
        {
        case SENSOR_CMD_DATA:
            pthread_mutex_lock(&mutex);
            resp.data = private_sdata;
            pthread_mutex_unlock(&mutex);
            break;

        default:
            break;
        }

        if (send_all(client_fd, &resp, sizeof(resp)) < 0) {
            perror("[Sensor Service] send");
            close(client_fd);
            continue;
        }

        close(client_fd);
    }

    pthread_cancel(sample_pid);
    pthread_join(sample_pid, NULL);
    pthread_mutex_destroy(&mutex);
    unlink(SENSOR_SOCK_PATH);
    close(server_fd);
    sensor_files_close();
    return 0;

error3:
    pthread_cancel(sample_pid);
    pthread_join(sample_pid, NULL);
    pthread_mutex_destroy(&mutex);
error2:
    unlink(SENSOR_SOCK_PATH);
error1:
    close(server_fd);
error:
    sensor_files_close();
    pipe_ready(pipe, 0);
    return -1;
}