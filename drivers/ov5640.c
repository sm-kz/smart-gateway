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
#include <asm-generic/bitops/non-atomic.h>
#include <linux/regmap.h>
#include <linux/clk.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-event.h>
#include <media/v4l2-mediabus.h>

#define OV5640_SOURCE_CLK       24000000

struct reg_value {
    unsigned short reg;
    unsigned char val;
    unsigned char mask;
};

struct ov5640_framesize {
    int width;
    int height;
    int min_fps;
    int max_fps;
    int max_freq;
    struct reg_value *regs;
};

/* 保存当前传感器信息 */
static int prev_pclk;
static int AE_Target = 52, night_mode;
static int prev_HTS;
static int AE_high, AE_low;

/* 320 * 240 */
static struct reg_value ov5640_qvga[] = {
    {0x3814, 0x33}, // timing X inc, 4 times downsampling
    {0x3815, 0x33}, // timing Y inc, 4 times downsampling
    {0x3800, 0x00}, // HS
    {0x3801, 0x00}, // HS
    {0x3802, 0x00}, // VS
    {0x3803, 0x00}, // VS
    {0x3804, 0x04}, // HW (HE)
    {0x3805, 0xff}, // HW (HE)  1280
    {0x3806, 0x03}, // VH (VE)
    {0x3807, 0xbf}, // VH (VE)  960
    {0x3808, 0x01}, // DVPHO
    {0x3809, 0x40}, // DVPHO    320
    {0x380a, 0x00}, // DVPVO
    {0x380b, 0xf0}, // DVPVO    240
    {0x380c, 0x04},
    {0x380d, 0x00}, // HTS
    {0x3810, 0x00},
    {0x3811, 0x00},
    {0x3812, 0x00},
    {0x3813, 0x00},
    {0, 0}, // sentinel
};

/* 640 * 480 */
static struct reg_value ov5640_vga[] = {
    {0x3814, 0x31}, // timing X inc, 2 times downsampling
    {0x3815, 0x31}, // timing Y inc, 2 times downsampling
    {0x3800, 0x00}, // HS
    {0x3801, 0x10}, // HS
    {0x3802, 0x00}, // VS
    {0x3803, 0x10}, // VS
    {0x3804, 0x05}, // HW (HE)
    {0x3805, 0x0f}, // HW (HE)
    {0x3806, 0x03}, // VH (VE)
    {0x3807, 0xcf}, // VH (VE)
    {0x3808, 0x02}, // DVPHO
    {0x3809, 0x80}, // DVPHO    640
    {0x380a, 0x01}, // DVPVO
    {0x380b, 0xe0}, // DVPVO    480
    {0x380c, 0x05},
    {0x380d, 0x68}, // HTS
    {0x3810, 0x00},
    {0x3811, 0x00},
    {0x3812, 0x00},
    {0x3813, 0x00},
    {0, 0}, // sentinel
};

/* 800 * 480 */
static struct reg_value ov5640_4i3[] = {
    {0x3814, 0x31}, // timing X inc, 2 times downsampling
    {0x3815, 0x31}, // timing Y inc, 2 times downsampling
    {0x3800, 0x00}, // HS
    {0x3801, 0x10}, // HS
    {0x3802, 0x00}, // VS
    {0x3803, 0x10}, // VS
    {0x3804, 0x06}, // HW (HE)
    {0x3805, 0x4f}, // HW (HE)
    {0x3806, 0x03}, // VH (VE)
    {0x3807, 0xcf}, // VH (VE)
    {0x3808, 0x03}, // DVPHO
    {0x3809, 0x20}, // DVPHO    800
    {0x380a, 0x01}, // DVPVO
    {0x380b, 0xe0}, // DVPVO    480
    {0x380c, 0x06},
    {0x380d, 0x68}, // HTS
    {0x3810, 0x00},
    {0x3811, 0x00},
    {0x3812, 0x00},
    {0x3813, 0x00},
    {0, 0}, // sentinel
};

enum {
    OV5640_FRAMESIZE_QVGA = 0,
    OV5640_FRAMESIZE_VGA,
    OV5640_FRAMESIZE_4I3
};

static struct ov5640_framesize ov5640_framesizes[] = {
	{ /* QVGA */
		.width		= 320,
		.height		= 240,
        .min_fps    = 30,
        .max_fps    = 120,
        .max_freq   = 48000000,
		.regs		= ov5640_qvga,
	}, { /* VGA */
		.width		= 640,
		.height		= 480,
        .min_fps    = 15,
        .max_fps    = 90,
        .max_freq   = 64000000,
		.regs		= ov5640_vga,
	}, { /* 4I3 */
        .width		= 800,
		.height		= 480,
        .min_fps    = 30,
        .max_fps    = 90,
        .max_freq   = 64000000,
		.regs		= ov5640_4i3,
    }
};

struct ov5640_fmt {
    unsigned int code;      // enum v4l2_mbus_pixelcode
    unsigned int color_space;   // enum v4l2_colorspace
    int bpp;
    struct reg_value *regs;
};

static struct reg_value ov5640_fmt_rgb565[] = {
    {0x4300, 0x61}, // RGB565
    {0x501f, 0x01}, // ISP RGB
    {0, 0}, // sentinel
};

static struct ov5640_fmt ov5640_fmts[] = {
    {
        .code = MEDIA_BUS_FMT_RGB565_2X8_LE,
        .color_space = V4L2_COLORSPACE_SRGB,
        .bpp = 16,
        .regs = ov5640_fmt_rgb565
    }
};

static struct reg_value reg_init_settings[] = {
    {0x3103, 0x03}, // system clock from PLL
    {0x3017, 0xff}, // DVP control and D[9:6]
    {0x3018, 0xff}, // DVP control and D[5:0]
    {0x3034, 0x1a}, // PLL, DVP 8-bit
    {0x460c, 0x22},
    {0x3824, 0x02}, // DVP PCLK divider used by this test
    {0x3820, 0x07}, // ISP vflip
    {0x3630, 0x36}, 
    {0x3631, 0x0e},
    {0x3632, 0xe2},
    {0x3633, 0x12},
    {0x3621, 0xe0},
    {0x3704, 0xa0},
    {0x3703, 0x5a},
    {0x3715, 0x78},
    {0x3717, 0x01},
    {0x370b, 0x60},
    {0x3705, 0x1a},
    {0x3905, 0x02},
    {0x3906, 0x10},
    {0x3901, 0x0a},
    {0x3731, 0x12},
    {0x3600, 0x08}, // analog/VCM bias: official sensor initialization
    {0x3601, 0x33},
    {0x302d, 0x60},
    {0x3620, 0x52},
    {0x371b, 0x20},
    {0x471c, 0x50},
    {0x3a13, 0x43}, // pre-gain = 1.05x
    {0x3a18, 0x00}, // AEC gain ceiling = 7.75x
    {0x3a19, 0xE0}, // AEC gain ceiling
    {0x3635, 0x13},
    {0x3636, 0x03},
    {0x3634, 0x40},
    {0x3622, 0x01},
    //50/60Hz detection
    {0x3c01, 0x34}, // sum auto, band counter enable, threshold = 4
    {0x3c04, 0x28}, // threshold low sum
    {0x3c05, 0x98}, // threshold high sum
    {0x3c06, 0x00}, // light meter 1 threshold H
    {0x3c07, 0x07}, // light meter 1 threshold L
    {0x3c08, 0x00}, // light meter 2 threshold H
    {0x3c09, 0x1c}, // light meter 2 threshold L
    {0x3c0a, 0x9c}, // sample number H
    {0x3c0b, 0x40}, // sample number L
    {0x3708, 0x64}, // official sensor analog timing
    {0x3709, 0x52},
    {0x370c, 0x03},
    {0x3618, 0x00},
    {0x3612, 0x29},
    {0x4001, 0x02}, // official BLC start line
    {0x4004, 0x02}, // official BLC black-line correction
    {0x4005, 0x1a}, // official BLC always-update
    {0x3000, 0x00}, // enable MCU, OTP
    {0x3004, 0xff}, // enable BIST, MCU memory, MCU, OTP, STROBE, D5060, timing, array clock
    {0x300e, 0x58}, // MIPI 2 lane? power down PHY HS TX, PHY LP RX, DVP enable
    // {0x302e, 0x00},
    // {0x440e, 0x00},
    {0x5000, 0x27}, // disable lens shading correction for noise diagnosis
    {0x5001, 0xa3},
    {0x3a0f, 0x30}, // stable in high
    {0x3a10, 0x28}, // stable in low
    {0x3a1b, 0x30}, // stable out high
    {0x3a1e, 0x26}, // stable out low
    {0x3a11, 0x60}, // fast zone high
    {0x3a1f, 0x14}, // fast zone low
    //LENC
    {0x5800, 0x23},
    {0x5801, 0x14},
    {0x5802, 0x0f},
    {0x5803, 0x0f},
    {0x5804, 0x12},
    {0x5805, 0x26},
    {0x5806, 0x0c},
    {0x5807, 0x08},
    {0x5808, 0x05},
    {0x5809, 0x05},
    {0x580a, 0x08},
    {0x580b, 0x0d},
    {0x580c, 0x08},
    {0x580d, 0x03},
    {0x580e, 0x00},
    {0x580f, 0x00},
    {0x5810, 0x03},
    {0x5811, 0x09},
    {0x5812, 0x07},
    {0x5813, 0x03},
    {0x5814, 0x00},
    {0x5815, 0x01},
    {0x5816, 0x03},
    {0x5817, 0x08},
    {0x5818, 0x0d},
    {0x5819, 0x08},
    {0x581a, 0x05},
    {0x581b, 0x06},
    {0x581c, 0x08},
    {0x581d, 0x0e},
    {0x581e, 0x29},
    {0x581f, 0x17},
    {0x5820, 0x11},
    {0x5821, 0x11},
    {0x5822, 0x15},
    {0x5823, 0x28},
    {0x5824, 0x46},
    {0x5825, 0x26},
    {0x5826, 0x08},
    {0x5827, 0x26},
    {0x5828, 0x64},
    {0x5829, 0x26},
    {0x582a, 0x24},
    {0x582b, 0x22},
    {0x582c, 0x24},
    {0x582d, 0x24},
    {0x582e, 0x06},
    {0x582f, 0x22},
    {0x5830, 0x40},
    {0x5831, 0x42},
    {0x5832, 0x24},
    {0x5833, 0x26},
    {0x5834, 0x24},
    {0x5835, 0x22},
    {0x5836, 0x22},
    {0x5837, 0x26},
    {0x5838, 0x44},
    {0x5839, 0x24},
    {0x583a, 0x26},
    {0x583b, 0x28},
    {0x583c, 0x42},
    {0x583d, 0xce}, // LENC BR offset
    //AWB
    {0x5180, 0xff}, // AWB B block
    {0x5181, 0xf2}, // AWB control
    {0x5182, 0x00}, // [7:4] max local counter, [3:0] max fast counter
    {0x5183, 0x14}, // AWB advance
    {0x5184, 0x25},
    {0x5185, 0x24},
    {0x5186, 0x09},
    {0x5187, 0x09},
    {0x5188, 0x09},
    {0x5189, 0x75},
    {0x518a, 0x54},
    {0x518b, 0xe0},
    {0x518c, 0xb2},
    {0x518d, 0x42},
    {0x518e, 0x3d},
    {0x518f, 0x56},
    {0x5190, 0x46},
    {0x5191, 0xf8}, // AWB top limit
    {0x5192, 0x04}, // AWB botton limit
    {0x5193, 0xf0}, // Red limit
    {0x5194, 0xf0}, // Green Limit
    {0x5195, 0xf0}, // Blue limit
    {0x5196, 0x03}, // AWB control
    {0x5197, 0x01}, // local limit
    {0x5198, 0x04},
    {0x5199, 0x12},
    {0x519a, 0x04},
    {0x519b, 0x00},
    {0x519c, 0x06},
    {0x519d, 0x82},
    {0x519e, 0x38}, // AWB control
    //Gamma
    {0x5480, 0x01}, // BIAS plus on
    {0x5481, 0x08},
    {0x5482, 0x14},
    {0x5483, 0x28},
    {0x5484, 0x51},
    {0x5485, 0x65},
    {0x5486, 0x71},
    {0x5487, 0x7d},
    {0x5488, 0x87},
    {0x5489, 0x91},
    {0x548a, 0x9a},
    {0x548b, 0xaa},
    {0x548c, 0xb8},
    {0x548d, 0xcd},
    {0x548e, 0xdd},
    {0x548f, 0xea},
    {0x5490, 0x1d},
    // color matrix: use the validated board coefficients
    {0x5381, 0x1e},
    {0x5382, 0x5b},
    {0x5383, 0x08},
    {0x5384, 0x0a},
    {0x5385, 0x7e},
    {0x5386, 0x88},
    {0x5387, 0x7c},
    {0x5388, 0x6c},
    {0x5389, 0x10},
    {0x538a, 0x01},
    {0x538b, 0x98},
    // UV adjustment: use the validated board coefficients
    {0x5580, 0x02},
    {0x5583, 0x40},
    {0x5584, 0x10},
    {0x5589, 0x10},
    {0x558a, 0x00},
    {0x558b, 0xf8},
    //CIP
    {0x3a02, 0x03}, // official exposure window high
    {0x3a03, 0xd8}, // official exposure window high
    {0x3a14, 0x03}, // official exposure window low
    {0x3a15, 0xd8}, // official exposure window low
    {0x5300, 0x08}, // sharpen-MT th1
    {0x5301, 0x30}, // sharpen-MT th2
    {0x5302, 0x10}, // sharpen-MT off1
    {0x5303, 0x00}, // sharpen-MT off2
    {0x5304, 0x08}, // De-noise th1
    {0x5305, 0x30}, // De-noise th2
    {0x5306, 0x08}, // De-noise off1
    {0x5307, 0x16}, // De-noise off2
    {0x5309, 0x08}, // sharpen-TH th1
    {0x530a, 0x30}, // sharpen-TH th2
    {0x530b, 0x04}, // sharpen-TH off1
    {0x530c, 0x06}, // sharpen-TH off2
    {0x4740, 0x21},
    {0x3035, 0x11},
    {0x3008, 0x02}, // software standby
    {0, 0}, // sentinel
};

struct ov5640_struct {
    dev_t devid;
    struct cdev cdev;
    struct file_operations fops;
    struct device *dev;
    struct class *class;
    struct i2c_client *i2c_cli;
    struct mutex mutex;
    int gpio_rst;
    int gpio_pwdn;

    /* v4l2 subdev */
    struct v4l2_subdev sd;
    struct v4l2_ctrl_handler ctrls;
    
    /* hardware control */
    int framesize_index;
    int fps;
    int fmt_index;
    int streaming;
};

static struct ov5640_struct *sd_to_ov5640(struct v4l2_subdev *sd) {
    return container_of(sd, struct ov5640_struct, sd);
}

static int ov5640_write(struct ov5640_struct *ov5640, unsigned int reg, unsigned int val) {
    struct i2c_client *cli = ov5640->i2c_cli;
    u8 buf[3]; // 2字节寄存器地址(大端) + 1字节数据
    int ret;
    struct i2c_msg msg = {
        .addr  = cli->addr,
        .flags = 0,
        .len   = 3,
        .buf   = buf,
    };

    buf[0] = (reg >> 8) & 0xFF;  // 高字节在前
    buf[1] = reg & 0xFF;         // 低字节在后
    buf[2] = val & 0xFF;

    ret = i2c_transfer(cli->adapter, &msg, 1);
    return (ret == 1) ? 0 : -EIO;
}

static int ov5640_read(struct ov5640_struct *ov5640, unsigned int reg, unsigned int *val) {
    struct i2c_client *cli = ov5640->i2c_cli;
    u8 reg_buf[2];
    u8 data_buf[1];
    int ret;
    struct i2c_msg msgs[2] = {
        {
            .addr  = cli->addr,
            .flags = 0,
            .len   = 2,
            .buf   = reg_buf,
        },
        {
            .addr  = cli->addr,
            .flags = I2C_M_RD,
            .len   = 1,
            .buf   = data_buf,
        },
    };

    reg_buf[0] = (reg >> 8) & 0xFF;
    reg_buf[1] = reg & 0xFF;

    ret = i2c_transfer(cli->adapter, msgs, 2);
    if (ret < 0) {
        printk("i2c_transfer error=%d\n", ret);
        return ret;
    }

    if (ret != 2) {
        printk("i2c_transfer only %d msgs\n", ret);
        return -EIO;
    }

    *val = data_buf[0];
    return 0;
}

static int ov5640_write_array(struct ov5640_struct *ov5640,
			      const struct reg_value *regs)
{
	int i, ret = 0;

	for (i = 0; ret == 0 && regs[i].reg != 0; i++)
		ret = ov5640_write(ov5640, regs[i].reg, regs[i].val);

	return ret;
}

static int ov5640_update_bits(struct ov5640_struct *ov5640, unsigned int reg, 
                                unsigned int mask, unsigned int val) {
     unsigned int temp;
    int ret;

    ret = ov5640_read(ov5640, reg, &temp);
    if (ret < 0)
        return ret;

    temp &= ~mask;
    temp |= (val & mask);

    return ov5640_write(ov5640, reg, temp);
}

/*------------------ ov5640 common hardware operations ------------------------*/
static int ov5640_get_HTS(struct ov5640_struct *ov5640)
{
	unsigned int hts_l, hts_h;
	int ret;

	ret = ov5640_read(ov5640, 0x380c, &hts_h);
	if (ret < 0)
		return ret;

	ret = ov5640_read(ov5640, 0x380d, &hts_l);
	if (ret < 0)
		return ret;

	return ((hts_h & 0x0f) << 8) | hts_l;
}

static int ov5640_get_VTS(struct ov5640_struct *ov5640)
{
	unsigned int vts_l, vts_h;
	int ret;

	ret = ov5640_read(ov5640, 0x380e, &vts_h);
	if (ret < 0)
		return ret;

	ret = ov5640_read(ov5640, 0x380f, &vts_l);
	if (ret < 0)
		return ret;

	return ((vts_h & 0xff) << 8) | vts_l;
}

static int ov5640_set_VTS(struct ov5640_struct *ov5640, int VTS)
{
	int temp;

	temp = VTS & 0xff;
	ov5640_write(ov5640, 0x380f, temp);

	temp = VTS>>8;
	ov5640_write(ov5640, 0x380e, temp);
	return 0;
}

static int ov5640_get_shutter(struct ov5640_struct *ov5640)
{
	int shutter;
	unsigned int regval;

    ov5640_read(ov5640, 0x3500, &regval);
	shutter = (regval & 0x0f);

    ov5640_read(ov5640, 0x3501, &regval);
	shutter = (shutter << 8) + regval;

    ov5640_read(ov5640, 0x3502, &regval);
	shutter = (shutter << 4) + (regval >> 4);

	return shutter;
}

static int ov5640_set_shutter(struct ov5640_struct *ov5640, int shutter)
{
	int temp;

	shutter = shutter & 0xffff;
	temp = shutter & 0x0f;
	temp = temp << 4;
	ov5640_write(ov5640, 0x3502, temp);

	temp = shutter & 0xfff;
	temp = temp >> 4;
	ov5640_write(ov5640, 0x3501, temp);

	temp = shutter >> 12;
	ov5640_write(ov5640, 0x3500, temp);

	return 0;
}

static int ov5640_get_gain16(struct ov5640_struct *ov5640)
{
	int gain16;
	unsigned int regval;

    ov5640_read(ov5640, 0x350a, &regval);
	gain16 = regval & 0x03;

    ov5640_read(ov5640, 0x350b, &regval);
	gain16 = (gain16 << 8) + regval;

	return gain16;
}

static int ov5640_set_gain16(struct ov5640_struct *ov5640, int gain16)
{
	int temp;

	gain16 = gain16 & 0x3ff;
	temp = gain16 & 0xff;

	ov5640_write(ov5640, 0x350b, temp);
	temp = gain16 >> 8;

	ov5640_write(ov5640, 0x350a, temp);
	return 0;
}

static int ov5640_get_light_freq(struct ov5640_struct *ov5640)
{
	unsigned int temp, temp1, light_frequency;

	ov5640_read(ov5640, 0x3c01, &temp);
	if (temp & 0x80) {
		/* manual */
		ov5640_read(ov5640, 0x3c00, &temp1);
		if (temp1 & 0x04) {
			/* 50Hz */
			light_frequency = 50;
		} else {
			/* 60Hz */
			light_frequency = 60;
		}
	} else {
		/* auto */
		ov5640_read(ov5640, 0x3c0c, &temp1);
		if (temp1 & 0x01) {
			/* 50Hz */
			light_frequency = 50;
		} else {
			/* 60Hz */
			light_frequency = 60;
		}
	}

	return light_frequency;
}

unsigned long ov5640_get_pclk(struct ov5640_struct *ov5640, unsigned int source_freq);

static int ov5640_set_bandingfilter(struct ov5640_struct *ov5640)
{
	int prev_VTS;
	unsigned int band_step60, max_band60, band_step50, max_band50;
	int ret;

	prev_pclk = ov5640_get_pclk(ov5640, OV5640_SOURCE_CLK);
	prev_HTS = ov5640_get_HTS(ov5640);
	prev_VTS = ov5640_get_VTS(ov5640);
	if (prev_pclk <= 0 || prev_HTS <= 0 || prev_VTS <= 4)
		return -EINVAL;

	band_step60 = prev_pclk / (prev_HTS * 60 * 2);
	band_step50 = prev_pclk / (prev_HTS * 50 * 2);
	if (!band_step60 || !band_step50)
		return -EINVAL;

	ret = ov5640_write(ov5640, 0x3a0a, band_step60 >> 8);
	if (ret < 0)
		return ret;
	ret = ov5640_write(ov5640, 0x3a0b, band_step60 & 0xff);
	if (ret < 0)
		return ret;
	max_band60 = (prev_VTS - 4) / band_step60;
	ret = ov5640_write(ov5640, 0x3a0d, max_band60);
	if (ret < 0)
		return ret;

	ret = ov5640_write(ov5640, 0x3a08, band_step50 >> 8);
	if (ret < 0)
		return ret;
	ret = ov5640_write(ov5640, 0x3a09, band_step50 & 0xff);
	if (ret < 0)
		return ret;
	max_band50 = (prev_VTS - 4) / band_step50;
	return ov5640_write(ov5640, 0x3a0e, max_band50);
}

/* stable in high */
static int ov5640_set_AE_target(struct ov5640_struct *ov5640, int target)
{
	unsigned int fast_high, fast_low;

	AE_low = target * 23 / 25; /* 0.92 */
	AE_high = target * 27 / 25; /* 1.08 */
	fast_high = AE_high << 1;

	if (fast_high > 255)
		fast_high = 255;
	fast_low = AE_low >> 1;

	ov5640_write(ov5640, 0x3a0f, AE_high);
	ov5640_write(ov5640, 0x3a10, AE_low);
	ov5640_write(ov5640, 0x3a1b, AE_high);
	ov5640_write(ov5640, 0x3a1e, AE_low);
	ov5640_write(ov5640, 0x3a11, fast_high);
	ov5640_write(ov5640, 0x3a1f, fast_low);

	return 0;
}

/* enable = 0 to turn off night mode
   enable = 1 to turn on night mode */
static int ov5640_set_night_mode(struct ov5640_struct *ov5640, int enable)
{
	unsigned int mode;

	ov5640_read(ov5640, 0x3a00, &mode);

	if (enable) {
		/* night mode on */
		mode |= 0x04;
		ov5640_write(ov5640, 0x3a00, mode);
	} else {
		/* night mode off */
		mode &= 0xfb;
		ov5640_write(ov5640, 0x3a00, mode);
	}

	return 0;
}

/* enable = 0 to turn off AEC/AGC
   enable = 1 to turn on AEC/AGC */
static void ov5640_turn_on_AE_AG(struct ov5640_struct *ov5640, int enable)
{
	unsigned int ae_ag_ctrl;

	ov5640_read(ov5640, 0x3503, &ae_ag_ctrl);
	if (enable) {
		/* turn on auto AE/AG */
		ae_ag_ctrl = ae_ag_ctrl & ~(0x03);
	} else {
		/* turn off AE/AG */
		ae_ag_ctrl = ae_ag_ctrl | 0x03;
	}
	ov5640_write(ov5640, 0x3503, ae_ag_ctrl);
}

static int ov5640_driver_capability(struct ov5640_struct *ov5640, int strength)
{
	unsigned int temp = 0;

	if (strength > 4 || strength < 1) {
		pr_err("The valid driver capability of ov5640 is 1x~4x\n");
		return -EINVAL;
	}

	ov5640_read(ov5640, 0x302c, &temp);

	temp &= ~0xc0;	/* clear [7:6] */
	temp |= ((strength - 1) << 6);	/* set [7:6] */

	ov5640_write(ov5640, 0x302c, temp);

	return 0;
}
/* --------------------------------------------------------------- */

static int ov5640_init_setting(struct ov5640_struct *ov5640) {
    if (ov5640_write(ov5640, 0x3103, 0x01) < 0) {
        pr_err("error write in %s\r\n", __func__);
    }   // SCCB clock from pad
    if (ov5640_write(ov5640, 0x3008, 0x82) < 0) {
        pr_err("error write in %s\r\n", __func__);
    }   // software reset

    msleep(10);

    if (ov5640_write_array(ov5640, reg_init_settings) < 0) {
        pr_err("error in setting %s\n", __func__);
        return -1;
    }

    ov5640_turn_on_AE_AG(ov5640, 1);
    
    ov5640_set_AE_target(ov5640, AE_Target);
        
    ov5640_set_gain16(ov5640, 64);

    return 0;
}

unsigned long ov5640_get_pclk(struct ov5640_struct *ov5640, unsigned int source_freq) {
    unsigned int temp1, temp2;
    unsigned int multiplier, sysdiv, pll_rdiv, bit2x_div, pclk_rdiv, p_div, scale_div;
    unsigned int prediv2x_index;
    unsigned long vco, pclk;

    int pclk_rdiv_map[] = {
        1, 2, 4, 8
    };
    int prediv2x_map[] = {
        2, 2, 4, 6, 8, 3, 12, 5, 16
    };

    ov5640_read(ov5640, 0x3037, &temp1);
    pll_rdiv = ((temp1 >> 4) & 0x01) + 1;

    ov5640_read(ov5640, 0x3034, &temp1);
    switch (temp1 & 0xF)
    {
    case 0x8:
        bit2x_div = 4;
        break;
    case 0xA:
        bit2x_div = 5;
        break;
    default:
        bit2x_div = 2;
        break;
    }

    ov5640_read(ov5640, 0x3035, &temp1);
    p_div = temp1 & 0x0F;
    sysdiv = temp1 >> 4;
    if (sysdiv == 0) {
        sysdiv = 16;
    }

    ov5640_read(ov5640, 0x300e, &temp1);
    if (temp1 >> 5)
        p_div /= 1;
    else
        p_div /= 2;

    ov5640_read(ov5640, 0x3824, &scale_div);
    scale_div &= 0x1F;
    ov5640_read(ov5640, 0x460c, &temp1);
    if (temp1 & 0x02)
        scale_div /= 2;
    else
        scale_div = 1;
    
    ov5640_read(ov5640, 0x3036, &temp1);
    multiplier = temp1;

    ov5640_read(ov5640, 0x3037, &temp1);
    prediv2x_index = temp1;
    pll_rdiv = ((temp1 >> 4) & 0x01) + 1;

    ov5640_read(ov5640, 0x3108, &temp1);
    temp2 = (temp1 >> 4) & 0x03;
    pclk_rdiv = pclk_rdiv_map[temp2];

    vco = source_freq * multiplier / prediv2x_map[prediv2x_index] * 2;
    pclk = vco / sysdiv / pll_rdiv / bit2x_div * 2 / pclk_rdiv / p_div / scale_div;

    // printk("source: %d, multiplier: %d, prediv: %d\r\n", source_freq, multiplier, prediv2x_map[prediv2x_index]);
    // printk("vco: %d, sysdiv: %d, pll_rdiv: %d, pclk: %d\r\n", vco, sysdiv, pll_rdiv, pclk);

    return pclk;
}

#include <linux/kernel.h>
#include <linux/string.h>

/* 假设存在这个宏，如果是用户态驱动需要自行定义 */
#ifndef ARRAY_SIZE
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#endif

unsigned long ov5640_set_pclk(struct ov5640_struct *ov5640, unsigned int source_freq, unsigned int target_freq)
{
    unsigned int multiplier, prediv, sysdiv, pll_rdiv, bit2x_div, pclk_rdiv, p_div, scale_div;
    unsigned int best_multiplier = 0, best_prediv_index = 0;
    unsigned int best_sysdiv = 0, best_pclk_rdiv_i = 0;
    unsigned long delta, best_delta = ~0UL;
    unsigned long vco, pclk, best_pclk = 0;
    int step = 2;
    unsigned int temp;

    int pclk_rdiv_map[] = {
        1, 2, 4, 8
    };
    int prediv2x_map[] = {
        2, 2, 4, 6, 8, 3, 12, 5, 16
    };
    int i, j;
    int ret;

    ov5640_read(ov5640, 0x3037, &temp);
    pll_rdiv = ((temp >> 4) & 0x01) + 1;

    ov5640_read(ov5640, 0x3034, &temp);
    switch (temp & 0xF)
    {
    case 0x8:
        bit2x_div = 4;
        break;
    case 0xA:
        bit2x_div = 5;
        break;
    default:
        bit2x_div = 2;
        break;
    }

    ov5640_read(ov5640, 0x3035, &p_div);
    p_div &= 0x0F;
    ov5640_read(ov5640, 0x300e, &temp);
    if (temp >> 5)
        p_div /= 1;
    else
        p_div /= 2;


    ov5640_read(ov5640, 0x3824, &scale_div);
    scale_div &= 0x1F;
    ov5640_read(ov5640, 0x460c, &temp);
    if (temp & 0x02)
        scale_div /= 2;
    else
        scale_div = 1;

    for (multiplier = 252; multiplier >= 4; multiplier -= step) {
        if (multiplier == 128)
            step = 1;
        
        for (j = 0; j < ARRAY_SIZE(prediv2x_map); ++j) {
            if (source_freq / prediv2x_map[j] < 4000000)
                continue;

            vco = source_freq * multiplier / prediv2x_map[j] * 2;

            if (vco > 1000000000)
                continue;
            if (vco < 500000000)
                continue;
            
            for (sysdiv = 1; sysdiv <= 16; sysdiv++) {
                for (i = 0; i < ARRAY_SIZE(pclk_rdiv_map); ++i) {
                    pclk_rdiv = pclk_rdiv_map[i];

                    pclk = vco / sysdiv / pll_rdiv / bit2x_div * 2 / pclk_rdiv / p_div / scale_div;
                    if (pclk > target_freq)
                        delta = pclk - target_freq;
                    else
                        delta = target_freq - pclk;

                    if (delta < best_delta) {
                        best_multiplier = multiplier;
                        best_prediv_index = j;
                        best_sysdiv = sysdiv;
                        best_pclk_rdiv_i = i;
                        best_delta = delta;
                        best_pclk = pclk;
                    }
                }
            }
        }
    }

    if (best_sysdiv == 16) 
        best_sysdiv = 0;

    ret = ov5640_update_bits(ov5640, 0x3035, 0xF0,
                             best_sysdiv << 4);
    if (ret < 0)
        return ret;

    ret = ov5640_update_bits(ov5640, 0x3036, 0xFF, best_multiplier);
    if (ret < 0)
        return ret;

    ret = ov5640_update_bits(ov5640, 0x3037, 0x1F,
                             (pll_rdiv - 1) << 4 | best_prediv_index);
    if (ret < 0)
        return ret;

    ret = ov5640_update_bits(ov5640, 0x3108, 0x30,
                             best_pclk_rdiv_i << 4);
    if (ret < 0)
        return ret;

    return best_pclk;
}

static int ov5640_set_fps (struct ov5640_struct *ov5640) {
    struct ov5640_framesize fs;
    int hts, vts, target_freq;
    unsigned int exp_lines_50hz, exp_lines_60hz;
    int ret;
    unsigned int exp_pixels_50hz, exp_pixels_60hz;

    fs = ov5640_framesizes[ov5640->framesize_index];
    if (ov5640->fps <= 0)
        ov5640->fps = fs.min_fps;
    if (ov5640->fps > fs.max_fps)
        ov5640->fps = fs.max_fps;
    if (ov5640->fps < fs.min_fps)
        ov5640->fps = fs.min_fps;

    target_freq = ov5640_set_pclk(ov5640, OV5640_SOURCE_CLK,
                                  fs.max_freq);
    if (target_freq <= 0)
        return -EINVAL;

    hts = ov5640_get_HTS(ov5640);
    if (hts <= 0)
        return hts < 0 ? hts : -EINVAL;

    vts = target_freq / hts / ov5640->fps;

    while (vts < (fs.height + fs.height / 10)) {
        ov5640->fps--;          // 自动降低帧率
        if (ov5640->fps < 5) {  // 防死循环，降到 5fps 以下直接报错
            pr_err("OV5640: Cannot achieve stable fps at this PCLK\n");
            return -EINVAL;
        }
        // 重新按降低后的帧率计算 VTS
        vts = target_freq / hts / ov5640->fps;
    }

    ret = ov5640_set_VTS(ov5640, vts);
    if (ret < 0)
        return ret;

    /* set exposure lines */
    exp_pixels_50hz = target_freq / 100;
    exp_pixels_60hz = target_freq / 120;
    exp_lines_50hz = exp_pixels_50hz / hts;
    exp_lines_60hz = exp_pixels_60hz / hts;

    ret = ov5640_write(ov5640, 0x3a08, (exp_lines_60hz >> 8) & 0x03);
    if (ret < 0)
        return ret;
    ret = ov5640_write(ov5640, 0x3a09, exp_lines_60hz & 0xFF);
    if (ret < 0)
        return ret;
    ret = ov5640_write(ov5640, 0x3a0a, (exp_lines_50hz >> 8) & 0x03);
    if (ret < 0)
        return ret;
    ret = ov5640_write(ov5640, 0x3a0b, exp_lines_50hz & 0xFF);
    if (ret < 0)
        return ret;

    ret = ov5640_set_bandingfilter(ov5640);
    if (ret < 0)
        return ret;

    /* Allow automatic exposure and white balance to settle. */
    msleep(max(20, 9000 / ov5640->fps));

    return ov5640->fps;
}


static void ov5640_print_id(struct ov5640_struct *ov5640) {
    unsigned int id_high = 0, id_low = 0;
    ov5640_read(ov5640, 0x300A, &id_high);
    ov5640_read(ov5640, 0x300B, &id_low);

    printk("OV5640 ID = %#x\r\n", (id_high << 8) | id_low);
}

static void ov5640_power_up(struct ov5640_struct *ov5640) {
    /* 硬件上电时序 */
    gpio_set_value(ov5640->gpio_rst, 0);
    gpio_set_value(ov5640->gpio_pwdn, 1);
    msleep(10);
    gpio_set_value(ov5640->gpio_pwdn, 0);
    msleep(5);
    gpio_set_value(ov5640->gpio_rst, 1);
    msleep(50);
}

static void ov5640_power_down(struct ov5640_struct *ov5640) {
    gpio_set_value(ov5640->gpio_pwdn, 1);
}

static void ov5640_wake_up(struct ov5640_struct *ov5640) {
    gpio_set_value(ov5640->gpio_pwdn, 0);
}

static int ov5640_set_framesize(struct ov5640_struct *ov5640) {
    return ov5640_write_array(ov5640, ov5640_framesizes[ov5640->framesize_index].regs);
}

static int ov5640_set_format(struct ov5640_struct *ov5640) {
    return ov5640_write_array(ov5640, ov5640_fmts[ov5640->fmt_index].regs);
}

static int ov5640_set_test_pattern(struct ov5640_struct *ov5640, int enable, int mode)
{
	int ret;
	unsigned int val;

	ret = ov5640_read(ov5640, 0x503d, &val);
	if (ret < 0)
		return ret;

	switch (enable) {
	case 0:
		val &= ~0x80;
		break;
	case 1:
		val &= ~(3 << 2);
		val |= 0x80;
        val |= mode << 2;
		break;
	}

	return ov5640_write(ov5640, 0x503d, val);
}

static void ov5640_device_init(struct ov5640_struct *ov5640) {
    ov5640_power_up(ov5640);
    ov5640->streaming = 0;

    ov5640_init_setting(ov5640);
    ov5640_print_id(ov5640);
    // ov5640_set_test_pattern(ov5640, 1, 0);
}

static int ov5640_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct ov5640_struct *ov5640 =
			container_of(ctrl->handler, struct ov5640_struct, ctrls);

	switch (ctrl->id) {
	case V4L2_CID_TEST_PATTERN:
		return ov5640_set_test_pattern(ov5640, ctrl->val, 0);
	}

	return 0;
}

static struct v4l2_ctrl_ops ov5640_ctrl_ops = {
	.s_ctrl = ov5640_s_ctrl,
};

static const char * const ov5640_test_pattern_menu[] = {
	"Disabled",
	"Vertical Color Bars",
};

/**
 * V4L2 subdev video level operations
 */

 /* 设置像素格式和分辨率 */
static int ov5640_s_fmt(struct v4l2_subdev *sd, struct v4l2_mbus_framefmt *mf) {
    struct ov5640_struct *ov5640 = sd_to_ov5640(sd);
    unsigned int i;
    int best = -1;
    int best_dist = INT_MAX;

    mutex_lock(&ov5640->mutex);

    if (ov5640->streaming) {
        mutex_unlock(&ov5640->mutex);
        return -EBUSY;
    }

    for (i = 0; i < ARRAY_SIZE(ov5640_fmts); i++) {
        if (ov5640_fmts[i].code == mf->code) {
            ov5640->fmt_index = i;
            break;
        }
    }
    if (i == ARRAY_SIZE(ov5640_fmts)) {
        ov5640->fmt_index = 0;
        mf->code = ov5640_fmts[0].code;
    }

    for (i = 0; i < ARRAY_SIZE(ov5640_framesizes); i++) {
        int dist = abs(ov5640_framesizes[i].width - mf->width) +
                   abs(ov5640_framesizes[i].height - mf->height);

        if (dist < best_dist) {
            best_dist = dist;
            best = i;
        }
    }

    if (best < 0)
        best = OV5640_FRAMESIZE_VGA;

    ov5640->framesize_index = best;
    mf->code = ov5640_fmts[ov5640->fmt_index].code;
    mf->colorspace = ov5640_fmts[ov5640->fmt_index].color_space;
    mf->field = V4L2_FIELD_NONE;
    mf->width = ov5640_framesizes[best].width;
    mf->height = ov5640_framesizes[best].height;

    mutex_unlock(&ov5640->mutex);
    return 0;
}

/* 设置帧率 */
static int ov5640_s_parm(struct v4l2_subdev *sd, struct v4l2_streamparm *pa) {
    struct ov5640_struct *ov5640 = sd_to_ov5640(sd);

    if (pa->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
        return -EINVAL;

    if (pa->parm.capture.timeperframe.numerator == 0 ||
        pa->parm.capture.timeperframe.denominator == 0)
        return -EINVAL;

    ov5640->fps = pa->parm.capture.timeperframe.denominator /
                  pa->parm.capture.timeperframe.numerator;
    if (ov5640->fps <= 0)
        return -EINVAL;

    pa->parm.capture.timeperframe.numerator = 1;
    pa->parm.capture.timeperframe.denominator = ov5640->fps;

    return 0;
}

/* 设置视频流启动和停止 */
static int ov5640_s_stream(struct v4l2_subdev *sd, int enable) {
    struct i2c_client *i2c = v4l2_get_subdevdata(sd);
    struct ov5640_struct *ov5640 = sd_to_ov5640(sd);
    int ret = 0;

	dev_dbg(&i2c->dev, "%s: enable: %d\n", __func__, enable);

    mutex_lock(&ov5640->mutex);

    enable = !!enable;

    if (ov5640->streaming == enable) {
        mutex_unlock(&ov5640->mutex);
        return 0;
    }

    if (enable) {
        ov5640_wake_up(ov5640);
        msleep(20);

        ret = ov5640_set_framesize(ov5640);
        if (ret < 0)
            goto out;

        ret = ov5640_set_format(ov5640);
        if (ret < 0)
            goto out;

        ret = ov5640_set_test_pattern(ov5640, 0, 0);
        if (ret < 0)
            goto out;

        ret = ov5640_set_fps(ov5640);
        if (ret < 0)
            goto out;

        pr_info("OV5640: auto FPS, pclk=%d hts=%d vts=%d fps=%d\n",
                ov5640_get_pclk(ov5640, OV5640_SOURCE_CLK),
                ov5640_get_HTS(ov5640), ov5640_get_VTS(ov5640),
                ov5640->fps);

        ret = ov5640_write(ov5640, 0x3008, 0x02);
        if (ret < 0)
            goto out;
    } else {
        ret = ov5640_write(ov5640, 0x3008, 0x42);
        if (ret < 0)
            goto out;

        ov5640_power_down(ov5640);
    }

    ov5640->streaming = enable;

out:
    mutex_unlock(&ov5640->mutex);
    return ret;
}

int enum_ov5640_fmt(struct v4l2_subdev *sd, unsigned int index,
			     u32 *code) {
    if (index >= ARRAY_SIZE(ov5640_fmts)) {
        return -EINVAL;
    }
    
    *code = ov5640_fmts[index].code;
    return 0;
}

int ov5640_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh) {
    struct ov5640_struct *ov5640 = sd_to_ov5640(sd);

    dev_dbg(&ov5640->i2c_cli->dev, "%s:\r\n", __func__);

    ov5640_wake_up(ov5640);
    return 0;
}

int ov5640_close(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh) {
    struct ov5640_struct *ov5640 = sd_to_ov5640(sd);

    dev_dbg(&ov5640->i2c_cli->dev, "%s:\r\n", __func__);

    ov5640_power_down(ov5640);
    ov5640->streaming = 0;
    return 0;
}

static const struct v4l2_subdev_core_ops ov5640_subdev_core_ops = {
	.log_status = v4l2_ctrl_subdev_log_status,
	.subscribe_event = v4l2_ctrl_subdev_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
};

static const struct v4l2_subdev_video_ops ov5640_subdev_video_ops = {
    .s_stream = ov5640_s_stream,
    .s_mbus_fmt = ov5640_s_fmt,
    .s_parm = ov5640_s_parm,
    .enum_mbus_fmt = enum_ov5640_fmt,
};

static const struct v4l2_subdev_ops ov5640_subdev_ops = {
    .core = &ov5640_subdev_core_ops,
    .video = &ov5640_subdev_video_ops,
};

static const struct v4l2_subdev_internal_ops ov5640_isubdev_internal_ops = {
    .open = ov5640_open,
    .close = ov5640_close,
};


static int ov5640_probe(struct i2c_client *cli, const struct i2c_device_id *device_id) {
    struct ov5640_struct *ov5640 = NULL;
    struct v4l2_subdev *sd;
    struct device_node *ov5640_node;
    int ret = 0;

    ov5640 = devm_kzalloc(&cli->dev, sizeof(struct ov5640_struct), GFP_KERNEL);
    if (!ov5640) {
        pr_err("error in allocation for ov5640\r\n");
        return -ENOMEM;
    }

    ov5640->i2c_cli = cli;
    ov5640->framesize_index = OV5640_FRAMESIZE_VGA;
    ov5640->fps = ov5640_framesizes[ov5640->framesize_index].min_fps;
    ov5640->fmt_index = 0;
    mutex_init(&ov5640->mutex);

    /* 解析设备树节点 */
    ov5640_node = cli->dev.of_node;
    ov5640->gpio_pwdn = of_get_named_gpio(ov5640_node, "pwn-gpios", 0);
    if (ov5640->gpio_pwdn < 0) {
        pr_err("error in getting gpio node\r\n");
        ret = ov5640->gpio_pwdn;
        goto fail1;
    }
    gpio_request(ov5640->gpio_pwdn, "pwdn");
    gpio_direction_output(ov5640->gpio_pwdn, 1);

    ov5640->gpio_rst = of_get_named_gpio(ov5640_node, "rst-gpios", 0);
    if (ov5640->gpio_rst < 0) {
        pr_err("error in getting gpio node\r\n");
        ret = ov5640->gpio_rst;
        goto fail2;
    }
    gpio_request(ov5640->gpio_rst, "rst");
    gpio_direction_output(ov5640->gpio_rst, 0);


    v4l2_ctrl_handler_init(&ov5640->ctrls, 1);
    v4l2_ctrl_new_std_menu_items(&ov5640->ctrls, &ov5640_ctrl_ops,
                                V4L2_CID_TEST_PATTERN,
                                ARRAY_SIZE(ov5640_test_pattern_menu) - 1,
                                0, 0, ov5640_test_pattern_menu);
    ov5640->sd.ctrl_handler = &ov5640->ctrls;

    if (ov5640->ctrls.error) {
        pr_err("%s: control initialization error %d\r\n",
                __func__, ov5640->ctrls.error);
        v4l2_ctrl_handler_free(&ov5640->ctrls);
        ret = ov5640->ctrls.error;
        goto fail3;
    }

    sd = &ov5640->sd;
    cli->flags |= I2C_CLIENT_SCCB;
    v4l2_i2c_subdev_init(sd, cli, &ov5640_subdev_ops);

    sd->internal_ops = &ov5640_isubdev_internal_ops;
    sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
                V4L2_SUBDEV_FL_HAS_EVENTS;

    ret = v4l2_async_register_subdev(sd);
	if (ret)
		goto fail4;

    ov5640_device_init(ov5640);

    return 0;

fail4:
    v4l2_ctrl_handler_free(&ov5640->ctrls);
fail3:
    gpio_free(ov5640->gpio_rst);
fail2:
    gpio_free(ov5640->gpio_pwdn);
fail1:
    mutex_destroy(&ov5640->mutex);
    return ret;
}

static int ov5640_remove(struct i2c_client *cli) {
    struct v4l2_subdev *sd = i2c_get_clientdata(cli);
    struct ov5640_struct *ov5640 = sd_to_ov5640(sd);

    v4l2_async_unregister_subdev(sd);
    v4l2_ctrl_handler_free(&ov5640->ctrls);
    gpio_free(ov5640->gpio_rst);
    gpio_free(ov5640->gpio_pwdn);
    mutex_destroy(&ov5640->mutex);
    
    return 0;
}

const struct i2c_device_id ov5640_id_table[] = {
    {.name = "ov5640"},
    {/* sentinel */}
};

const struct of_device_id ov5640_match_table[] = {
    {.compatible = "ovti,ov5640", .name = "ov5640"},
    {/* sentinel */}
};

static struct i2c_driver ov5640_driver = {
    .id_table = ov5640_id_table,
    .driver = {
        .of_match_table = of_match_ptr(ov5640_match_table),
        .owner = THIS_MODULE,
        .name = "ov5640"
    },
    .probe = ov5640_probe,
    .remove = ov5640_remove
};

static int __init ov5640_module_init(void) {
    return i2c_add_driver(&ov5640_driver);
}

static void __exit ov5640_module_exit(void) {
    i2c_del_driver(&ov5640_driver);
}
module_init(ov5640_module_init);
module_exit(ov5640_module_exit);

MODULE_LICENSE("GPL");
