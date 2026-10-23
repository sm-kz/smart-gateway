#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/ide.h>
#include <linux/init.h>
#include <linux/module.h>
#include <asm/io.h>
#include <linux/cdev.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/of_irq.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/iio/iio.h>
#include <linux/sort.h>

/* I2C 设备地址 */
#define DEVADDR         0x68

/* 寄存器地址 */
#define SMPLRT_DIV          0x19
#define CONFIG              0x1A
#define GYRO_CONFIG         0x1B
#define ACCEL_CONFIG        0x1C
#define ACCEL_XOUT_H        0x3B
#define ACCEL_XOUT_L        0x3C
#define ACCEL_YOUT_H        0x3D
#define ACCEL_YOUT_L        0x3E
#define ACCEL_ZOUT_H        0x3F
#define ACCEL_ZOUT_L        0x40
#define TEMP_OUT_H          0x41
#define TEMP_OUT_L          0x42
#define GYRO_XOUT_H         0x43
#define GYRO_XOUT_L         0x44
#define GYRO_YOUT_H         0x45
#define GYRO_YOUT_L         0x46
#define GYRO_ZOUT_H         0x47
#define GYRO_ZOUT_L         0x48
#define PWR_MGMT_1          0x6B
#define PWR_MGMT_2          0x6C
#define WHO_AM_I            0x75


struct scale_type {
    int start;
    int end;
    int lsb;
    int ntimes;
};

// 陀螺仪档位
static const struct scale_type gyro_scales[] = {
    [0] = { -250, 250, 1310, 10 },
    [1] = { -500, 500, 655, 10 },
    [2] = { -1000, 1000, 328, 10 },
    [3] = { -2000, 2000, 164, 10 }
};

// 加速度档位
static const struct scale_type accel_scales[] = {
    [0] = { -2, 2, 16384, 1 },
    [1] = { -4, 4, 8192, 1 },
    [2] = { -8, 8, 4096, 1 },
    [3] = { -16, 16, 2048, 1 },
};

// 温度档位
static const struct scale_type temp_scale = { -45, 85, 340, 1 };

struct mpu6050_struct{
    struct i2c_client       *cli;
    struct iio_dev          *dev;
    struct regmap           *regmap;
    struct regmap_config    rcfg;
    struct mutex            mutex;

    int gyrox_off, gyroy_off, gyroz_off;
    int accelx_off, accely_off, accelz_off;
    int temp_off;
};

enum mpu6050_scan {
    MPU6050_SCAN_ACCL_X,
    MPU6050_SCAN_ACCL_Y,
    MPU6050_SCAN_ACCL_Z,
    MPU6050_SCAN_TEMP,
    MPU6050_SCAN_GYRO_X,
    MPU6050_SCAN_GYRO_Y,
    MPU6050_SCAN_GYRO_Z
};

#define MPU6050_CHAN(_type, _channel2, _address, _scan_index)    \
{\
    .type = _type,\
    .channel2 = _channel2,\
    .address = _address,\
    .scan_index = _scan_index,\
    .scan_type = {\
        .sign = 's',\
        .realbits = 16,\
        .storagebits = 16,\
        .shift = 0,\
        .repeat = 0,\
        .endianness = IIO_BE\
    },\
    .info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | \
                          BIT(IIO_CHAN_INFO_CALIBBIAS), \
    .info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE), \
    .modified = 1\
}

struct iio_chan_spec mpu6050_channels[] = {
    {
        .type = IIO_TEMP,
        .channel = MPU6050_SCAN_TEMP,
        .address = TEMP_OUT_H,
        .scan_index = MPU6050_SCAN_TEMP,
        .scan_type = {
            .sign = 's',
            .realbits = 16,
            .storagebits = 16,
            .shift = 0,
            .repeat = 0,
            .endianness = IIO_BE
        },
        .info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | 
                              BIT(IIO_CHAN_INFO_SCALE) |
                              BIT(IIO_CHAN_INFO_OFFSET)
    },
    MPU6050_CHAN(IIO_ACCEL, IIO_MOD_X, ACCEL_XOUT_H, MPU6050_SCAN_ACCL_X),
    MPU6050_CHAN(IIO_ACCEL, IIO_MOD_Y, ACCEL_YOUT_H, MPU6050_SCAN_ACCL_Y),
    MPU6050_CHAN(IIO_ACCEL, IIO_MOD_Z, ACCEL_ZOUT_H, MPU6050_SCAN_ACCL_Z),
    MPU6050_CHAN(IIO_ANGL_VEL, IIO_MOD_X, GYRO_XOUT_H, MPU6050_SCAN_GYRO_X),
    MPU6050_CHAN(IIO_ANGL_VEL, IIO_MOD_Y, GYRO_YOUT_H, MPU6050_SCAN_GYRO_Y),
    MPU6050_CHAN(IIO_ANGL_VEL, IIO_MOD_Z, GYRO_ZOUT_H, MPU6050_SCAN_GYRO_Z)
};

static int mpu6050_read_reg(struct mpu6050_struct *dev, unsigned int reg, unsigned int *val){
    return regmap_read(dev->regmap, reg, val);
}

static int mpu6050_write_reg(struct mpu6050_struct *dev, unsigned int reg, unsigned int val){
    return regmap_write(dev->regmap, reg, val);
}

static int mpu6050_read_regs(struct mpu6050_struct *dev, unsigned int reg, void *val, size_t len){
    return regmap_bulk_read(dev->regmap, reg, val, len);
}

static void mpu6050_calibration(struct mpu6050_struct* dev);

/* 
 * 当硬件出现扰动时，I2C 读取会出错，后续 MPU6050的 I2C 进入异常状态导致无法正常通信
 * 所以需要对 MPU6050 重新初始化
 */
static int mpu6050_device_reinit(struct mpu6050_struct* dev) {
    unsigned int regval = 0;
    /* 软件复位 */
    regval = (1 << 7);
    mpu6050_write_reg(dev, PWR_MGMT_1, regval);

    msleep(100);

    /* 选择时钟 */
    regval = (1 << 0);
    mpu6050_write_reg(dev, PWR_MGMT_1, regval);

    /* 设置采样分频。开启数字低通滤波后，输出时钟频率为 1KHz，本程序将采样频率设置为 50Hz */
    regval = 19;
    mpu6050_write_reg(dev, SMPLRT_DIV, regval);

    /* 设置数字低通滤波。对于50Hz的采样频率，滤波带宽根据采样定理，设置为25Hz便可 */
    regmap_update_bits(dev->regmap, CONFIG, 0x07, 4);

    /* gyro 配置。范围为+-1000 */
    regval = 2 << 3;
    mpu6050_write_reg(dev, GYRO_CONFIG, regval);

    /* accel 配置。范围为+-16 */
    regval = 3 << 3;
    mpu6050_write_reg(dev, ACCEL_CONFIG, regval);

    msleep(100);
    return 0;
}

static int mpu6050_device_init(struct mpu6050_struct* dev)
{
    unsigned int regval = 0;
    
    /* 选择时钟 */
    regval = (1 << 0);
    mpu6050_write_reg(dev, PWR_MGMT_1, regval);

    /* 设置采样分频。开启数字低通滤波后，输出时钟频率为 1KHz，本程序将采样频率设置为 50Hz */
    regval = 19;
    mpu6050_write_reg(dev, SMPLRT_DIV, regval);

    /* 设置数字低通滤波。对于50Hz的采样频率，滤波带宽根据采样定理，设置为25Hz便可 */
    regmap_update_bits(dev->regmap, CONFIG, 0x07, 4);

    /* gyro 配置。范围为+-1000 */
    regval = 2 << 3;
    mpu6050_write_reg(dev, GYRO_CONFIG, regval);

    /* accel 配置。范围为+-16 */
    regval = 3 << 3;
    mpu6050_write_reg(dev, ACCEL_CONFIG, regval);

    /* 获取设备ID */
    mpu6050_read_reg(dev, WHO_AM_I, &regval);
    regval >>= 1;
    printk("mpu6050 ID: %#X\r\n", regval);

    mpu6050_calibration(dev);
    return 0;
}

static int mpu6050_get_temp(struct mpu6050_struct *dev, int *val) {
    int ret = 0;
    unsigned char buf[2];
    u16 raw = 0;
    
    mutex_lock(&dev->mutex);
    ret = mpu6050_read_regs(dev, TEMP_OUT_H, buf, 2);
    if (ret) {
        dev_err(&dev->cli->dev, "Error reading temp regs\r\n");
        mutex_unlock(&dev->mutex);
        mpu6050_device_reinit(dev);
        return ret;
    }

    raw = buf[0] << 8 | buf[1];
    *val = sign_extend32(raw, 15);
    mutex_unlock(&dev->mutex);

    return 0;
}

static int mpu6050_get_gyros(struct mpu6050_struct *dev, int *x, int *y, int *z) {
    int ret = 0;
    unsigned char buf[6];
    u16 raw = 0;

    if (!x && !y && !z)
        return -EINVAL;

    mutex_lock(&dev->mutex);
    ret = mpu6050_read_regs(dev, GYRO_XOUT_H, buf, 6);
    if (ret) {
        dev_err(&dev->cli->dev, "Error reading gyro regs\r\n");
        mutex_unlock(&dev->mutex);
        mpu6050_device_reinit(dev);
        return ret;
    }
    
    if (x) {
        raw = buf[0] << 8 | buf[1];
        *x = sign_extend32(raw, 15);
    }
    if (y) {
        raw = buf[2] << 8 | buf[3];
        *y = sign_extend32(raw, 15);
    }
    if (z) {
        raw = buf[4] << 8 | buf[5];
        *z = sign_extend32(raw, 15);
    }
    
    mutex_unlock(&dev->mutex);
    return 0;
}

static int mpu6050_get_accels(struct mpu6050_struct *dev, int *x, int *y, int *z) {
    int ret = 0;
    unsigned char buf[6];
    u16 raw = 0;

    if (!x && !y && !z)
        return -EINVAL;

    mutex_lock(&dev->mutex);
    ret = mpu6050_read_regs(dev, ACCEL_XOUT_H, buf, 6);
    if (ret) {
        dev_err(&dev->cli->dev, "Error reading accel regs\r\n");
        mutex_unlock(&dev->mutex);
        mpu6050_device_reinit(dev);
        return ret;
    }
    
    if (x) {
        raw = buf[0] << 8 | buf[1];
        *x = sign_extend32(raw, 15);
    }
    if (y) {
        raw = buf[2] << 8 | buf[3];
        *y = sign_extend32(raw, 15);
    }
    if (z) {
        raw = buf[4] << 8 | buf[5];
        *z = sign_extend32(raw, 15);
    }
    
    mutex_unlock(&dev->mutex);
    return 0;
}

/* ====================== 数据处理部分 =========================== */
#define CALIBRATION_SAMPLES     50

static int cmp_int(const void *a, const void *b)
{
    int _a = *(const int *)a;
    int _b = *(const int *)b;
    return (_a > _b) - (_a < _b);
}

static int data_collection(struct mpu6050_struct* dev,
                            int *xvals, int *yvals, int *zvals, 
                            unsigned int len, u8 gyro) {
    int i = 0, ret = 0, fails = 0;
    for (i = 0; i < len; ++i) {
        if (gyro) 
            ret = mpu6050_get_gyros(dev, &xvals[i], &yvals[i], &zvals[i]);
        else
            ret = mpu6050_get_accels(dev, &xvals[i], &yvals[i], &zvals[i]);

        if (ret) {
            xvals[i] = yvals[i] = zvals[i] = 0;
            fails++;
            msleep(10);     // 失败时先延时缓一下
        }

        msleep(20);
    }

    if (fails >= len / 10)
        return -EIO;
    return 0;
}

static int data_anlysis(int *data, unsigned int len, int *avg) {
    int i = 0, valid_count = 0;
    int median, mad, th, sum = 0;
    int *sorted = NULL, *abs_devs;
    int ret = 0;

    if (len == 0)
        return -EIO;

    // 保存排序后的数组
    sorted = kmemdup(data, len * sizeof(int), GFP_KERNEL);
    if (!sorted)
        return -EIO;
    sort(sorted, len, sizeof(int), cmp_int, NULL);

    // 寻找中位数
    median = sorted[len / 2];
    kfree(sorted);

    // 计算每个样本到中位数的绝对偏差
    abs_devs = kmalloc_array(len, sizeof(int), GFP_KERNEL);
    if (!abs_devs)
        return -EIO;
    for (i = 0; i < len; ++i) {
        abs_devs[i] = abs(data[i] - median);
    }
    sort(abs_devs, len, sizeof(int), cmp_int, NULL);
    mad = abs_devs[len / 2];
    kfree(abs_devs);

    // 生成动态阈值，乘 3 是 3 sigma 法则的经验值
    th = (mad > 0) ? (mad * 3) : 5;

    for (i = 0; i < len; ++i) {
        if (abs(data[i] - median) < th) {
            sum += data[i];
            valid_count++;
        }
    }

    if (valid_count > len * 4 / 5) {
        *avg = (int)(sum / valid_count);
        ret = 0;
    } else {
        *avg = median;
        ret = -EIO;
    }

    return ret; 
}
/* ======================================================== */

static int mpu6050_gyros_calib(struct mpu6050_struct* dev) {
    int xvals[CALIBRATION_SAMPLES], yvals[CALIBRATION_SAMPLES], zvals[CALIBRATION_SAMPLES];
    int avg = 0;
    u16 retry = 9;
    int ret = 0;

    do {
        ret = data_collection(dev, xvals, yvals, zvals, CALIBRATION_SAMPLES, 1);
        if (ret) continue;

        ret = data_anlysis(xvals, CALIBRATION_SAMPLES, &avg);
        if (ret) continue;
        else dev->gyrox_off = avg;

        ret = data_anlysis(yvals, CALIBRATION_SAMPLES, &avg);
        if (ret) continue;
        else dev->gyroy_off = avg;

        ret = data_anlysis(zvals, CALIBRATION_SAMPLES, &avg);
        if (ret) continue;
        else dev->gyroz_off = avg;
    } while (ret && retry-- > 0);

    return ret;
}

static void mpu6050_calibration(struct mpu6050_struct* dev) {
    int ret = 0;

    printk("Now starting calibration, please keep still.\r\n");
    msleep(500);

    ret = mpu6050_gyros_calib(dev);
    if (!ret) {
        printk("Gyros calibration succeed. x: %d, y: %d, z: %d\r\n", 
        dev->gyrox_off, dev->gyroy_off, dev->gyroz_off);
    } else {
        printk("Gyros calibration failed\r\n");
    }
}

static int mpu6050_read_raw(struct iio_dev *indio_dev,
                            struct iio_chan_spec const *chan,
                            int *val,
                            int *val2,
                            long mask) 
{
    int ntimes = 1;
    int ret = 0;
    unsigned int regval;
    struct mpu6050_struct *dev = iio_priv(indio_dev);

    switch(mask) {
        case IIO_CHAN_INFO_SCALE:
            *val = 0;
            switch(chan->type) {
                case IIO_TEMP:
                    /* 分辨率为每摄氏度 340 LSB，1/340 = 0.002941176 */
                    ntimes = temp_scale.ntimes > 1 ? temp_scale.ntimes : 1;
                    *val2 = 1000000000 * ntimes / temp_scale.lsb;
                    return IIO_VAL_INT_PLUS_NANO;
                case IIO_ANGL_VEL:
                    mpu6050_read_reg(dev, GYRO_CONFIG, &regval);
                    regval = (regval >> 3) & 3;
                    ntimes = gyro_scales[regval].ntimes > 1 ? gyro_scales[regval].ntimes : 1;
                    *val2 = 1000000 * ntimes / gyro_scales[regval].lsb;
                    return IIO_VAL_INT_PLUS_MICRO;
                case IIO_ACCEL:
                    mpu6050_read_reg(dev, ACCEL_CONFIG, &regval);
                    regval = (regval >> 3) & 3;
                    ntimes = accel_scales[regval].ntimes > 1 ? accel_scales[regval].ntimes : 1;
                    *val2 = 1000000000 * ntimes / accel_scales[regval].lsb;
                    return IIO_VAL_INT_PLUS_NANO;
                default:
                    return -EINVAL;
            }
        case IIO_CHAN_INFO_RAW:
            switch (chan->type) {
                case IIO_TEMP:
                    ret = mpu6050_get_temp(dev, val);
                    return ret ? -EINVAL : IIO_VAL_INT;
                case IIO_ANGL_VEL:
                    switch (chan->channel2) {
                        case IIO_MOD_X:
                            ret = mpu6050_get_gyros(dev, val, NULL, NULL);
                            return ret ? -EINVAL : IIO_VAL_INT;
                        case IIO_MOD_Y:
                            ret = mpu6050_get_gyros(dev, NULL, val, NULL);
                            return ret ? -EINVAL : IIO_VAL_INT;
                        case IIO_MOD_Z:
                            ret = mpu6050_get_gyros(dev, NULL, NULL, val);
                            return ret ? -EINVAL : IIO_VAL_INT;
                        default:
                            return -EINVAL;
                    }
                case IIO_ACCEL:
                    switch (chan->channel2) {
                        case IIO_MOD_X:
                            ret = mpu6050_get_accels(dev, val, NULL, NULL);
                            return ret ? -EINVAL : IIO_VAL_INT;
                        case IIO_MOD_Y:
                            ret = mpu6050_get_accels(dev, NULL, val, NULL);
                            return ret ? -EINVAL : IIO_VAL_INT;
                        case IIO_MOD_Z:
                            ret = mpu6050_get_accels(dev, NULL, NULL, val);
                            return ret ? -EINVAL : IIO_VAL_INT;
                        default:
                            return -EINVAL;
                    }
                default:
                    return -EINVAL;
            }
        case IIO_CHAN_INFO_OFFSET:
            *val = dev->temp_off;
            return IIO_VAL_INT;
        case IIO_CHAN_INFO_CALIBBIAS:
            switch (chan->type) {
                case IIO_ANGL_VEL:
                    switch (chan->channel2) {
                        case IIO_MOD_X:
                            *val = dev->gyrox_off;
                            return IIO_VAL_INT;
                        case IIO_MOD_Y:
                            *val = dev->gyroy_off;
                            return IIO_VAL_INT;
                        case IIO_MOD_Z:
                            *val = dev->gyroz_off;
                            return IIO_VAL_INT; 
                        default:
                            return -EINVAL;   
                    }
                case IIO_ACCEL:
                    switch (chan->channel2) {
                        case IIO_MOD_X:
                            *val = dev->accelx_off;
                            return IIO_VAL_INT;
                        case IIO_MOD_Y:
                            *val = dev->accely_off;
                            return IIO_VAL_INT;
                        case IIO_MOD_Z:
                            *val = dev->accelz_off;
                            return IIO_VAL_INT; 
                        default:
                            return -EINVAL;   
                    }
                default:
                    return -EINVAL;
            }
        default:
            return -EINVAL;
    }

    return -EINVAL;
}

static int mpu6050_write_raw(struct iio_dev *indio_dev,
			 struct iio_chan_spec const *chan,
			 int val,
			 int val2,
			 long mask)
{
    int ret = 0;
    struct mpu6050_struct *dev = iio_priv(indio_dev);

    switch (mask) {
        case IIO_CHAN_INFO_SCALE:
            /* 设置 scale 时，只有 val 有效 */
            switch (chan->type) {
                case IIO_ANGL_VEL:
                    mutex_lock(&dev->mutex);
                    ret = regmap_update_bits(dev->regmap, GYRO_CONFIG, 3 << 3, val);
                    mutex_unlock(&dev->mutex);
                    return ret;
                case IIO_ACCEL:
                    mutex_lock(&dev->mutex);
                    ret = regmap_update_bits(dev->regmap, ACCEL_CONFIG, 3 << 3, val);
                    mutex_unlock(&dev->mutex);
                    return ret;
                default:
                    return -EINVAL;
            }
        default:
            return -EINVAL;
    }
    return 0;
}

struct iio_info mpu6050_info = {
    .driver_module = THIS_MODULE,
    .read_raw = mpu6050_read_raw,
    .write_raw = mpu6050_write_raw
};

static int mpu6050_probe(struct i2c_client *cli, const struct i2c_device_id *devid)
{
    int ret = 0;
    struct mpu6050_struct *mpu6050_data;
    struct iio_dev *iio_dev;

    /* 创建 iio 设备 */
    iio_dev = iio_device_alloc(sizeof(struct mpu6050_struct));
    if (iio_dev == NULL) {
        printk("No memory to allocate for device %s\r\n", cli->name);
        return -ENOMEM;
    }
    mpu6050_data = iio_priv(iio_dev);
    mpu6050_data->cli = cli;
    i2c_set_clientdata(cli, mpu6050_data);

    mutex_init(&mpu6050_data->mutex);
    
    /* regmap 初始化 */
    mpu6050_data->rcfg.reg_bits = 8;
    mpu6050_data->rcfg.val_bits = 8;
    mpu6050_data->rcfg.read_flag_mask = 0;
    mpu6050_data->rcfg.write_flag_mask = 0;
    mpu6050_data->regmap = regmap_init_i2c(cli, &mpu6050_data->rcfg);
    if (IS_ERR(mpu6050_data->regmap)) {
        printk("regmap init failed\r\n");
        ret = PTR_ERR(mpu6050_data->regmap);
        goto fail_regmap_init;
    }
    
    /* 硬件设备初始化 */
    ret = mpu6050_device_init(mpu6050_data);
    if (ret) {
        printk("device init failed\r\n");
        goto fail_init;
    }

    /* 初始化 iio 设备其他成员 */
    iio_dev->modes = INDIO_DIRECT_MODE;
    iio_dev->dev.parent = &cli->dev;
    iio_dev->channels = mpu6050_channels;
    iio_dev->num_channels = ARRAY_SIZE(mpu6050_channels);
    iio_dev->name = "mympu";
    iio_dev->info = &mpu6050_info;
    ret = iio_device_register(iio_dev);
    if (ret) {
        printk("IIO device register failed\r\n");
        goto fail_init;
    }

    return 0;

fail_init:
    regmap_exit(mpu6050_data->regmap);
fail_regmap_init:
    iio_device_free(iio_dev);
    return ret;
}

static int mpu6050_remove(struct i2c_client *cli)
{
    struct mpu6050_struct *mpu6050_data = i2c_get_clientdata(cli);
    struct iio_dev *iio_dev = iio_priv_to_dev(mpu6050_data);

    iio_device_unregister(iio_dev);
    regmap_exit(mpu6050_data->regmap);
    iio_device_free(iio_dev);

    return 0;
}

static const struct i2c_device_id mpu6050_id_table[] = {
    {.name = "mpu6050-id"},
    {/* sentinel */}
};

static const struct of_device_id mpu6050_match_table[] = {
    {.compatible = "atk,mpu6050"},
    { /*sentinel */}
};

static struct i2c_driver mpu6050_driver = {
    .id_table = mpu6050_id_table,
    .probe = mpu6050_probe,
    .remove = mpu6050_remove,
	.driver = {
        .name = "mpu6050-driver",
        .of_match_table = of_match_ptr(mpu6050_match_table),
        .owner = THIS_MODULE
    }
};

static int __init mpu6050_init(void)
{
    return i2c_register_driver(THIS_MODULE, &mpu6050_driver);
}

static void __exit mpu6050_exit(void)
{
    i2c_del_driver(&mpu6050_driver);
}

module_init(mpu6050_init);
module_exit(mpu6050_exit);
MODULE_LICENSE("GPL");
MODULE_INFO(intree, "Y");
