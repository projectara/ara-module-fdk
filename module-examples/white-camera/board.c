/*
 * Copyright (c) 2015 Google, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its
 * contributors may be used to endorse or promote products derived from this
 * software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <nuttx/device.h>
#include <nuttx/device_camera.h>
#include <nuttx/device_table.h>
#include <nuttx/gpio.h>
#include <nuttx/i2c.h>
#include <nuttx/kmalloc.h>
#include <nuttx/util.h>

#include <arch/tsb/csi.h>

/* OV5645 I2C port and address */
#define OV5645_I2C_PORT                 0
#define OV5645_I2C_ADDR                 0x3c

/* OV5645 registers */
#define OV5645_ID_HIGH                  0x300a
#define OV5645_ID_LOW                   0x300b
#define OV5645_ID                       0x5645

#define REG_STREAM_ONOFF                0x4202

#define OV5645_REG_END                  0xffff

/* OV5645 GPIOs */
#define OV5645_GPIO_RESET               7
#define OV5645_GPIO_PWDN                8

/* Define white module supported number of streams */
#define WHITE_MODULE_MAX_STREAMS        1

/**
 * @brief camera device state
 */
enum ov5645_state {
    OV5645_STATE_OPEN,
    OV5645_STATE_CLOSED,
};

/**
 * @brief private camera device information
 */
struct sensor_info {
    struct device *dev;
    struct i2c_dev_s *cam_i2c;
    enum ov5645_state state;
    struct cdsi_dev *cdsidev;
    uint8_t req_id;
};

/**
 * @brief Struct to store register and value for sensor read/write
 */
struct reg_val_tbl {
    uint16_t reg_num;
    uint8_t value;
};

/**
 * @brief ov5645 sensor init registers for SXGA
 */
static const struct reg_val_tbl ov5645_init_setting[] = {
    /* SVGA 1280*960 */
    /* initial setting, Sysclk = 56Mhz, MIPI 2 lane 224MBps */
    {0x3008, 0x42}, /* software standby */
    {0x3103, 0x03}, /* clo0xfrom, 0xpl,L */
    {0x3503, 0x07}, /* AGC manual, AEC manual */
    {0x3002, 0x1c}, /* system reset */
    {0x3006, 0xc3}, /* clock enable */
    {0x300e, 0x45}, /* MIPI 2 lane */
    {0x3017, 0x40}, /* Frex, CSK input, Vsync output */
    {0x3018, 0x00}, /* GPIO input */
    {0x302e, 0x0b},
    {0x3037, 0x13}, /* PLL */
    {0x3108, 0x01}, /* PLL */
    {0x3611, 0x06},
    {0x3612, 0xab},
    {0x3614, 0x50},
    {0x3618, 0x04},
    {0x3034, 0x18}, /* PLL, MIPI 8-bit mode */
    {0x3035, 0x21}, /* PLL */
    {0x3036, 0x70}, /* PLL */
    {0x3500, 0x00}, /* exposure = 0x100 */
    {0x3501, 0x01}, /* exposure */
    {0x3502, 0x00}, /* exposure */
    {0x350a, 0x00}, /* gain = 0x3f */
    {0x350b, 0x3f}, /* gain */
    {0x3600, 0x09},
    {0x3601, 0x43},
    {0x3620, 0x33},
    {0x3621, 0xe0},
    {0x3622, 0x01},
    {0x3630, 0x2d},
    {0x3631, 0x00},
    {0x3632, 0x32},
    {0x3633, 0x52},
    {0x3634, 0x70},
    {0x3635, 0x13},
    {0x3636, 0x03},
    {0x3702, 0x6e},
    {0x3703, 0x52},
    {0x3704, 0xa0},
    {0x3705, 0x33},
    {0x3708, 0x66},
    {0x3709, 0x12},
    {0x370b, 0x61},
    {0x370c, 0xc3},
    {0x370f, 0x10},
    {0x3715, 0x08},
    {0x3717, 0x01},
    {0x371b, 0x20},
    {0x3731, 0x22},
    {0x3739, 0x70},
    {0x3901, 0x0a},
    {0x3905, 0x02},
    {0x3906, 0x10},
    {0x3719, 0x86},
    {0x3800, 0x00}, /* HS = 0 */
    {0x3801, 0x00}, /* HS */
    {0x3802, 0x00}, /* VS = 6 */
    {0x3803, 0x06}, /* VS */
    {0x3804, 0x0a}, /* HW = 2623 */
    {0x3805, 0x3f}, /* HW */
    {0x3806, 0x07}, /* VH = 1949 */
    {0x3807, 0x9d}, /* VH */
    {0x3808, 0x05}, /* DVPHO = 1280 */
    {0x3809, 0x00}, /* DVPHO */
    {0x380a, 0x03}, /* DVPVO = 960 */
    {0x380b, 0xc0}, /* DVPVO */
    {0x380c, 0x07}, /* HTS = 1896 */
    {0x380d, 0x68}, /* HTS */
    {0x380e, 0x03}, /* VTS = 984 */
    {0x380f, 0xd8}, /* VTS */
    {0x3810, 0x00}, /* H OFF = 16 */
    {0x3811, 0x10}, /* H OFF */
    {0x3812, 0x00}, /* V OFF = 6 */
    {0x3813, 0x06}, /* V OFF */
    {0x3814, 0x31}, /* X INC */
    {0x3815, 0x31}, /* Y INC */
    {0x3820, 0x47}, /* flip on, V bin on */
    {0x3821, 0x07}, /* mirror on, H bin on */
    {0x3824, 0x01}, /* PLL */
    {0x3826, 0x03},
    {0x3828, 0x08},
    {0x3a02, 0x03}, /* nigt mode ceiling = 984 */
    {0x3a03, 0xd8}, /* nigt mode ceiling */
    {0x3a08, 0x01}, /* B50 */
    {0x3a09, 0xf8}, /* B50 */
    {0x3a0a, 0x01}, /* B60 */
    {0x3a0b, 0xa4}, /* B60 */
    {0x3a0e, 0x02}, /* max 50 */
    {0x3a0d, 0x02}, /* max 60 */
    {0x3a14, 0x03}, /* 50Hz max exposure = 984 */
    {0x3a15, 0xd8}, /* 50Hz max exposure */
    {0x3a18, 0x01}, /* gain ceiling = 31.5x */
    {0x3a19, 0xf8}, /* gain ceiling */
    /* 50Hz/60Hz auto detect */
    {0x3c01, 0x34},
    {0x3c04, 0x28},
    {0x3c05, 0x98},
    {0x3c07, 0x07},
    {0x3c09, 0xc2},
    {0x3c0a, 0x9c},
    {0x3c0b, 0x40},
    {0x3c01, 0x34},
    {0x4001, 0x02}, /* BLC start line */
    {0x4004, 0x02}, /* B0xline, 0xnu,mber */
    {0x4005, 0x18}, /* BLC update by gain change */
    {0x4300, 0x32}, /* YUV 422, UYVY */
    {0x4514, 0x00},
    {0x4520, 0xb0},
    {0x460b, 0x37},
    {0x460c, 0x20},
    /* MIPI timing */
    {0x4800, 0x24}, /* non-continuous clock lane, LP-11 when idle */
    {0x4818, 0x01},
    {0x481d, 0xf0},
    {0x481f, 0x50},
    {0x4823, 0x70},
    {0x4831, 0x14},
    {0x4837, 0x10}, /* global timing */
    {0x5000, 0xa7}, /* Lenc/raw gamma/BPC/WPC/color interpolation on */
    {0x5001, 0x83}, /* SDE on, scale off, UV adjust off, color matrix/AWB on */
    {0x501d, 0x00},
    {0x501f, 0x00}, /* select ISP YUV 422 */
    {0x503d, 0x00},
    {0x505c, 0x30},
    /* AWB control */
    {0x5181, 0x59},
    {0x5183, 0x00},
    {0x5191, 0xf0},
    {0x5192, 0x03},
    /* AVG control */
    {0x5684, 0x10},
    {0x5685, 0xa0},
    {0x5686, 0x0c},
    {0x5687, 0x78},
    {0x5a00, 0x08},
    {0x5a21, 0x00},
    {0x5a24, 0x00},
    {0x4202, 0xff}, /* stop the stream */
    {0x3008, 0x02}, /* wake from software standby */
    {0x3503, 0x00}, /* AGC auto, AEC auto */
    /* AWB control */
    {0x5180, 0xff},
    {0x5181, 0xf2},
    {0x5182, 0x00},
    {0x5183, 0x14},
    {0x5184, 0x25},
    {0x5185, 0x24},
    {0x5186, 0x09},
    {0x5187, 0x09},
    {0x5188, 0x0a},
    {0x5189, 0x75},
    {0x518a, 0x52},
    {0x518b, 0xea},
    {0x518c, 0xa8},
    {0x518d, 0x42},
    {0x518e, 0x38},
    {0x518f, 0x56},
    {0x5190, 0x42},
    {0x5191, 0xf8},
    {0x5192, 0x04},
    {0x5193, 0x70},
    {0x5194, 0xf0},
    {0x5195, 0xf0},
    {0x5196, 0x03},
    {0x5197, 0x01},
    {0x5198, 0x04},
    {0x5199, 0x12},
    {0x519a, 0x04},
    {0x519b, 0x00},
    {0x519c, 0x06},
    {0x519d, 0x82},
    {0x519e, 0x38},
    /* matrix */
    {0x5381, 0x1e},
    {0x5382, 0x5b},
    {0x5383, 0x08},
    {0x5384, 0x0b},
    {0x5385, 0x84},
    {0x5386, 0x8f},
    {0x5387, 0x82},
    {0x5388, 0x71},
    {0x5389, 0x11},
    {0x538a, 0x01},
    {0x538b, 0x98},
    /* CIP */
    {0x5300, 0x08}, /* sharpen MT th1 */
    {0x5301, 0x30}, /* sharpen MT th2 */
    {0x5302, 0x10}, /* sharpen MT off1 */
    {0x5303, 0x00}, /* sharpen MT off2 */
    {0x5304, 0x08}, /* DNS th1 */
    {0x5305, 0x30}, /* DNS th2 */
    {0x5306, 0x08}, /* DNS off1 */
    {0x5307, 0x16}, /* DNS off2 */
    {0x5309, 0x08}, /* sharpen TH th1 */
    {0x530a, 0x30}, /* sharpen TH th2 */
    {0x530b, 0x04}, /* sharpen TH off1 */
    {0x530c, 0x06}, /* sharpen TH off2 */
    /* Gamma */
    {0x5480, 0x01}, /* bias on */
    {0x5481, 0x0e}, /* Y yst 00 */
    {0x5482, 0x18},
    {0x5483, 0x2b},
    {0x5484, 0x52},
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
    {0x548f, 0xea}, /* Y yst 0E */
    {0x5490, 0x1d}, /* Y yst 0F */
    /* SDE */
    {0x5580, 0x06},
    {0x5583, 0x40},
    {0x5584, 0x30},
    {0x5589, 0x10},
    {0x558a, 0x00},
    {0x558b, 0xf8},
    /* LENC */
    {0x5800, 0x3f},
    {0x5801, 0x16},
    {0x5802, 0x0e},
    {0x5803, 0x0d},
    {0x5804, 0x17},
    {0x5805, 0x3f},
    {0x5806, 0x0b},
    {0x5807, 0x06},
    {0x5808, 0x04},
    {0x5809, 0x04},
    {0x580a, 0x06},
    {0x580b, 0x0b},
    {0x580c, 0x09},
    {0x580d, 0x03},
    {0x580e, 0x00},
    {0x580f, 0x00},
    {0x5810, 0x03},
    {0x5811, 0x08},
    {0x5812, 0x0a},
    {0x5813, 0x03},
    {0x5814, 0x00},
    {0x5815, 0x00},
    {0x5816, 0x04},
    {0x5817, 0x09},
    {0x5818, 0x0f},
    {0x5819, 0x08},
    {0x581a, 0x06},
    {0x581b, 0x06},
    {0x581c, 0x08},
    {0x581d, 0x0c},
    {0x581e, 0x3f},
    {0x581f, 0x1e},
    {0x5820, 0x12},
    {0x5821, 0x13},
    {0x5822, 0x21},
    {0x5823, 0x3f},
    {0x5824, 0x68},
    {0x5825, 0x28},
    {0x5826, 0x2c},
    {0x5827, 0x28},
    {0x5828, 0x08},
    {0x5829, 0x48},
    {0x582a, 0x64},
    {0x582b, 0x62},
    {0x582c, 0x64},
    {0x582d, 0x28},
    {0x582e, 0x46},
    {0x582f, 0x62},
    {0x5830, 0x60},
    {0x5831, 0x62},
    {0x5832, 0x26},
    {0x5833, 0x48},
    {0x5834, 0x66},
    {0x5835, 0x44},
    {0x5836, 0x64},
    {0x5837, 0x28},
    {0x5838, 0x66},
    {0x5839, 0x48},
    {0x583a, 0x2c},
    {0x583b, 0x28},
    {0x583c, 0x26},
    {0x583d, 0xae},
    {0x5025, 0x00},
    {0x3a0f, 0x38}, /* AEC in H */
    {0x3a10, 0x30}, /* AEC in L */
    {0x3a1b, 0x38}, /* AEC out H */
    {0x3a1e, 0x30}, /* AEC out L */
    {0x3a11, 0x70}, /* control zone H */
    {0x3a1f, 0x18}, /* control zone L */
    {0x3008, 0x02}, /* software enable */

    {OV5645_REG_END, 0x00}, /* END MARKER */
};

/**
 * @brief ov5645 sensor registers for 30fps VGA
 */
static const struct reg_val_tbl ov5645_setting_30fps_VGA_640_480[] = {
    {0x3618, 0x00},
    {0x3035, 0x11},
    {0x3036, 0x46},
    {0x3600, 0x09},
    {0x3601, 0x43},
    {0x3708, 0x64},
    {0x370c, 0xc3},
    {0x3814, 0x31},
    {0x3815, 0x31},
    {0x3800, 0x00},
    {0x3801, 0x00},
    {0x3802, 0x00},
    {0x3803, 0x04},
    {0x3804, 0x0a},
    {0x3805, 0x3f},
    {0x3806, 0x07},
    {0x3807, 0x9b},
    {0x3808, 0x02},
    {0x3809, 0x80},
    {0x380a, 0x01},
    {0x380b, 0xe0},
    {0x380c, 0x07},
    {0x380d, 0x68},
    {0x380e, 0x04},
    {0x380f, 0x38},
    {0x3810, 0x00},
    {0x3811, 0x10},
    {0x3812, 0x00},
    {0x3813, 0x06},
    {0x3820, 0x41},
    {0x3821, 0x07},
    {0x3a02, 0x03},
    {0x3a03, 0xd8},
    {0x3a08, 0x01},
    {0x3a09, 0x0e},
    {0x3a0a, 0x00},
    {0x3a0b, 0xf6},
    {0x3a0e, 0x03},
    {0x3a0d, 0x04},
    {0x3a14, 0x03},
    {0x3a15, 0xd8},
    {0x4004, 0x02},
    {0x4005, 0x18},
    {0x4837, 0x16},
    {0x3503, 0x00},

    {OV5645_REG_END, 0x00}, /* END MARKER */
};

#if 1
/* video moide size: 1280*720. Below table is form ov5645 sample code */
static const struct reg_val_tbl ov5645_setting_30fps_720p_1280_720[] = {
    //Sysclk = 42Mhz, MIPI 2 lane 168MBps
    //0x3612, 0xa9,
    {0x3618, 0x00},
    {0x3035, 0x21},
    {0x3036, 0x54},
    {0x3600, 0x09},
    {0x3601, 0x43},
    {0x3708, 0x66},
    {0x370c, 0xc3},
    {0x3803, 0xfa}, // VS L
    {0x3806, 0x06}, // VH = 1705
    {0x3807, 0xa9}, // VH
    {0x3808, 0x05}, // DVPHO = 1280
    {0x3809, 0x00}, // DVPHO
    {0x380a, 0x02}, // DVPVO = 720
    {0x380b, 0xd0}, // DVPVO
    {0x380c, 0x07}, // HTS = 1892
    {0x380d, 0x64}, // HTS
    {0x380e, 0x02}, // VTS = 740
    {0x380f, 0xe4}, // VTS
    {0x3814, 0x31}, // X INC
    {0x3815, 0x31}, // X INC
    #ifdef OV5645_flip
    {0x3820, 0x47}, // flip on, V bin on
    #else
    {0x3820, 0x41}, // flip off, V bin on
    #endif
    #ifdef OV5645_mirror
    {0x3821, 0x07}, // mirror on, H bin on
    #else
    {0x3821, 0x01}, // mirror off, H bin on
    #endif
    {0x3a02, 0x02}, // night mode ceiling = 740
    {0x3a03, 0xe4}, // night mode ceiling
    {0x3a08, 0x00}, // B50 = 222
    {0x3a09, 0xde}, // B50
    {0x3a0a, 0x00}, // B60 = 185
    {0x3a0b, 0xb9}, // B60
    {0x3a0e, 0x03}, // max 50
    {0x3a0d, 0x04}, // max 60
    {0x3a14, 0x02}, // max 50hz exposure = 3/100
    {0x3a15, 0x9a}, // max 50hz exposure
    {0x3a18, 0x01}, // max gain = 31.5x
    {0x3a19, 0xf8}, // max gain
    {0x4004, 0x02}, // BLC line number
    {0x4005, 0x18}, // BLC update by gain change
    {0x4837, 0x16}, // MIPI global timing
    {0x3503, 0x00}, // AGC/AEC on
 };
#else
/**
 * @brief ov5645 sensor registers for 30fps 720p
 */
static const struct reg_val_tbl ov5645_setting_30fps_720p_1280_720[] = {
    {0x3618, 0x00},
    {0x3035, 0x11},
    {0x3036, 0x54},
    {0x3600, 0x09},
    {0x3601, 0x43},
    {0x3708, 0x64},
    {0x370c, 0xc3},
    {0x3814, 0x31},
    {0x3815, 0x31},
    {0x3800, 0x00},
    {0x3801, 0x00},
    {0x3802, 0x00},
    {0x3803, 0xfa},
    {0x3804, 0x0a},
    {0x3805, 0x3f},
    {0x3806, 0x06},
    {0x3807, 0xa9},
    {0x3808, 0x05},
    {0x3809, 0x00},
    {0x380a, 0x02},
    {0x380b, 0xd0},
    {0x380c, 0x07},
    {0x380d, 0x64},
    {0x380e, 0x02},
    {0x380f, 0xe4},
    {0x3810, 0x00},
    {0x3811, 0x10},
    {0x3812, 0x00},
    {0x3813, 0x04},
    {0x3820, 0x41},
    {0x3821, 0x07},
    {0x3a02, 0x02},
    {0x3a03, 0xe4},
    {0x3a08, 0x01},
    {0x3a09, 0xbc},
    {0x3a0a, 0x01},
    {0x3a0b, 0x72},
    {0x3a0e, 0x01},
    {0x3a0d, 0x02},
    {0x3a14, 0x02},
    {0x3a15, 0xe4},
    {0x4004, 0x02},
    {0x4005, 0x18},
    {0x4837, 0x16},
    {0x3503, 0x00},
    {0x3008, 0x02},

    {OV5645_REG_END, 0x00}, /* END MARKER */
};
#endif
/**
 * @brief ov5645 sensor registers for 30fps 1080p
 */
static const struct reg_val_tbl ov5645_setting_30fps_1080p_1920_1080[] = {
    {0x3612, 0xab},
    {0x3614, 0x50},
    {0x3618, 0x04},
    {0x3035, 0x21},
    {0x3036, 0x70},
    {0x3600, 0x08},
    {0x3601, 0x33},
    {0x3708, 0x63},
    {0x370c, 0xc0},
    {0x3800, 0x01},
    {0x3801, 0x50},
    {0x3802, 0x01},
    {0x3803, 0xb2},
    {0x3804, 0x08},
    {0x3805, 0xef},
    {0x3806, 0x05},
    {0x3807, 0xf1},
    {0x3808, 0x07},
    {0x3809, 0x80},
    {0x380a, 0x04},
    {0x380b, 0x38},
    {0x380c, 0x09},
    {0x380d, 0xc4},
    {0x380e, 0x04},
    {0x380f, 0x60},
    {0x3810, 0x00},
    {0x3811, 0x10},
    {0x3812, 0x00},
    {0x3813, 0x04},
    {0x3814, 0x11},
    {0x3815, 0x11},
    {0x3820, 0x41},
    {0x3821, 0x07},
    {0x3a02, 0x04},
    {0x3a03, 0x90},
    {0x3a08, 0x01},
    {0x3a09, 0xf8},
    {0x3a0a, 0x01},
    {0x3a0b, 0xf8},
    {0x3a0e, 0x02},
    {0x3a0d, 0x02},
    {0x3a14, 0x04},
    {0x3a15, 0x90},
    {0x3a18, 0x00},
    {0x4004, 0x02},
    {0x4005, 0x18},
    {0x4837, 0x10},
    {0x3503, 0x00},

    {OV5645_REG_END, 0x00}, /* END MARKER */
};

/**
 * @brief ov5645 sensor registers for 15fps QSXGA
 */
static const struct reg_val_tbl ov5645_setting_15fps_QSXGA_2592_1944[] = {
    {0x3820, 0x40},
    {0x3821, 0x06}, /*disable flip*/
    {0x3035, 0x21},
    {0x3036, 0x54},
    {0x3c07, 0x07},
    {0x3c09, 0xc2},
    {0x3c0a, 0x9c},
    {0x3c0b, 0x40},
    {0x3820, 0x40},
    {0x3821, 0x06},
    {0x3814, 0x11},
    {0x3815, 0x11},
    {0x3800, 0x00},
    {0x3801, 0x00},
    {0x3802, 0x00},
    {0x3803, 0x00},
    {0x3804, 0x0a},
    {0x3805, 0x3f},
    {0x3806, 0x07},
    {0x3807, 0x9f},
    {0x3808, 0x0a},
    {0x3809, 0x20},
    {0x380a, 0x07},
    {0x380b, 0x98},
    {0x380c, 0x0b},
    {0x380d, 0x1c},
    {0x380e, 0x07},
    {0x380f, 0xb0},
    {0x3810, 0x00},
    {0x3811, 0x10},
    {0x3812, 0x00},
    {0x3813, 0x04},
    {0x3618, 0x04},
    {0x3612, 0xab},
    {0x3708, 0x21},
    {0x3709, 0x12},
    {0x370c, 0x00},
    {0x3a02, 0x03},
    {0x3a03, 0xd8},
    {0x3a08, 0x01},
    {0x3a09, 0x27},
    {0x3a0a, 0x00},
    {0x3a0b, 0xf6},
    {0x3a0e, 0x03},
    {0x3a0d, 0x04},
    {0x3a14, 0x03},
    {0x3a15, 0xd8},
    {0x4001, 0x02},
    {0x4004, 0x06},
    {0x4713, 0x03},
    {0x4407, 0x04},
    {0x460b, 0x35},
    {0x460c, 0x22},
    {0x3824, 0x02},
    {0x5001, 0x83},

    {OV5645_REG_END, 0x00}, /* END MARKER */
};

/**
 * @brief ov5645 sensor registers for 30fps XGA
 */
static const struct reg_val_tbl ov5645_setting_30fps_XGA_1024_768[] = {
    {0x3618, 0x00},
    {0x3035, 0x11},
    {0x3036, 0x70},
    {0x3600, 0x09},
    {0x3601, 0x43},
    {0x3708, 0x64},
    {0x370c, 0xc3},
    {0x3814, 0x31},
    {0x3815, 0x31},
    {0x3800, 0x00},
    {0x3801, 0x00},
    {0x3802, 0x00},
    {0x3803, 0x06},
    {0x3804, 0x0a},
    {0x3805, 0x3f},
    {0x3806, 0x07},
    {0x3807, 0x9d},
    {0x3808, 0x04},
    {0x3809, 0x00},
    {0x380a, 0x03},
    {0x380b, 0x00},
    {0x380c, 0x07},
    {0x380d, 0x68},
    {0x380e, 0x03},
    {0x380f, 0xd8},
    {0x3810, 0x00},
    {0x3811, 0x10},
    {0x3812, 0x00},
    {0x3813, 0x06},
    {0x3820, 0x41},
    {0x3821, 0x07},
    {0x3a02, 0x03},
    {0x3a03, 0xd8},
    {0x3a08, 0x01},
    {0x3a09, 0xf8},
    {0x3a0a, 0x01},
    {0x3a0b, 0xa4},
    {0x3a0e, 0x02},
    {0x3a0d, 0x02},
    {0x3a14, 0x03},
    {0x3a15, 0xd8},
    {0x4004, 0x02},
    {0x4005, 0x18},
    {0x4837, 0x16},
    {0x3503, 0x00},

    {OV5645_REG_END, 0x00}, /* END MARKER */
};

/**
 * @brief ov5645 sensor registers for 30fps SXGA
 */
static const struct reg_val_tbl ov5645_setting_30fps_SXGA_1280_960[] = {
    // Sysclk = 56Mhz, MIPI 2 lane 224MBps
    //0x3612, 0xa9,
    {0x3618, 0x00},
    {0x3035, 0x21}, // PLL
    {0x3036, 0x70}, // PLL
    {0x3600, 0x09},
    {0x3601, 0x43},
    {0x3708, 0x66},
    {0x370c, 0xc3},
    {0x3803, 0x06}, // VS L
    {0x3806, 0x07}, // VH = 1949
    {0x3807, 0x9d}, // VH
    {0x3808, 0x05}, // DVPHO = 1280
    {0x3809, 0x00}, // DVPHO
    {0x380a, 0x03}, // DVPVO = 960
    {0x380b, 0xc0}, // DVPVO
    {0x380c, 0x07}, // HTS = 1896
    {0x380d, 0x68}, // HTS
    {0x380e, 0x03}, // VTS = 984
    {0x380f, 0xd8}, // VTS
    {0x3814, 0x31}, // X INC
    {0x3815, 0x31}, // Y INC
    #ifdef OV5645_flip
    {0x3820, 0x47}, // flip on, V bin on
    #else
    {0x3820, 0x41}, // flip off, V bin on
    #endif
    #ifdef OV5645_mirror
    {0x3821, 0x07}, // mirror on, H bin on
    #else
    {0x3821, 0x01}, // mirror off, H bin on
    #endif
#if 1
    {0x3a02, 0x07}, // night mode ceiling = 8/120
    {0x3a03, 0xb0}, // night mode ceiling
    {0x3a08, 0x01}, // B50
    {0x3a09, 0x27}, // B50
    {0x3a0a, 0x00}, // B60
    {0x3a0b, 0xf6}, // B60
    {0x3a0e, 0x03}, // max 50
    {0x3a0d, 0x04}, // max 60
    {0x3a14, 0x08}, // 50Hz max exposure = 7/100
    {0x3a15, 0x11}, // 50Hz max exposure
    {0x3a18, 0x01}, // max gain = 31.5x
    {0x3a19, 0xf8}, // max gain
#else
    /* Original OV5645 sample code */
    {0x3a02, 0x03},
    {0x3a03, 0xd8},
    {0x3a08, 0x01},
    {0x3a09, 0xf8},
    {0x3a0a, 0x01},
    {0x3a0b, 0xa4},
    {0x3a0e, 0x02},
    {0x3a0d, 0x02},
    {0x3a14, 0x03},
    {0x3a15, 0xd8},
    {0x3a18, 0x00},
    {0x3a19, 0xf8},
#endif
    {0x4004, 0x02}, // BLC line number
    {0x4005, 0x18}, // BLC update by gain change
    {0x4837, 0x10}, // MIPI global timing
    {0x3503, 0x00}, // AGC/AEC on

    {OV5645_REG_END, 0x00}, /* END MARKER */
};

/**
 * @brief ov5645 sensor mode
 */
struct ov5645_mode_info {
    int width;
    int height;
    unsigned int dtype;
    unsigned int format;
    unsigned int frame_max_size;

    const struct reg_val_tbl *regs;
};

/*
 * Supported formats ordered by expected frequency of usage (the most common
 * format being listed first).
 */
static const struct ov5645_mode_info ov5645_mode_settings[] = {
    /* SXGA - 1280*960 */
    {
        .width          = 1280,
        .height         = 960,
        .dtype          = MIPI_DT_YUV422_8BIT,
        .format         = CAMERA_UYVY422_PACKED,
        .frame_max_size = 1280 * 960 * 2,
        .regs           = ov5645_setting_30fps_SXGA_1280_960,
    },
    /* 1080p - 1920*1080 */
    {
        .width          = 1920,
        .height         = 1080,
        .dtype          = MIPI_DT_YUV422_8BIT,
        .format         = CAMERA_UYVY422_PACKED,
        .frame_max_size = 1920 * 1080 * 2,
        .regs           = ov5645_setting_30fps_1080p_1920_1080,
    },
    /* QSXGA - 2592*1944 */
    {
        .width          = 2592,
        .height         = 1944,
        .dtype          = MIPI_DT_YUV422_8BIT,
        .format         = CAMERA_UYVY422_PACKED,
        .frame_max_size = 2592 * 1944 * 2,
        .regs           = ov5645_setting_15fps_QSXGA_2592_1944,
    },
    /* 720p - 1280*720 */
    {
        .width          = 1280,
        .height         = 720,
        .dtype          = MIPI_DT_YUV422_8BIT,
        .format         = CAMERA_UYVY422_PACKED,
        .frame_max_size = 1280 * 720 * 2,
        .regs           = ov5645_setting_30fps_720p_1280_720,
    },
    /* XGA - 1024*768 */
    {
        .width          = 1024,
        .height         = 768,
        .dtype          = MIPI_DT_YUV422_8BIT,
        .format         = CAMERA_UYVY422_PACKED,
        .frame_max_size = 1024 * 768 * 2,
        .regs           = ov5645_setting_30fps_XGA_1024_768,
    },
    /* VGA - 640*480 */
    {
        .width          = 640,
        .height         = 480,
        .dtype          = MIPI_DT_YUV422_8BIT,
        .format         = CAMERA_UYVY422_PACKED,
        .frame_max_size = 640 * 480 * 2,
        .regs           = ov5645_setting_30fps_VGA_640_480,
    },
};

/**
 * @brief i2c read for camera sensor (It reads a single byte)
 * @param dev Pointer to structure of i2c device data
 * @param addr Address of i2c to read
 * @return the byte read on success or a negative error code on failure
 */
static int ov5645_read(struct i2c_dev_s *dev, uint16_t addr)
{
    uint8_t cmd[2];
    uint8_t buf;
    int ret;
    struct i2c_msg_s msg[] = {
        {
            .addr = OV5645_I2C_ADDR,
            .flags = 0,
            .buffer = cmd,
            .length = 2,
        }, {
            .addr = OV5645_I2C_ADDR,
            .flags = I2C_M_READ,
            .buffer = &buf,
            .length = 1,
        }
    };

    cmd[0] = (addr >> 8) & 0xff;
    cmd[1] = addr & 0xff;

    ret = I2C_TRANSFER(dev, msg, 2);
    if (ret != OK) {
        printf("ov5645: i2c read failed\n", ret);
        return -EIO;
    }

    return buf;
}

/**
 * @brief i2c write for camera sensor (It writes a single byte)
 * @param dev Pointer to structure of i2c device data
 * @param addr Address of i2c to write
 * @param data Data to write
 * @return zero for success or non-zero on any faillure
 */
static int ov5645_write(struct i2c_dev_s *dev, uint16_t addr, uint8_t data)
{
    uint8_t cmd[3];
    int ret;
    struct i2c_msg_s msg[] = {
        {
            .addr = OV5645_I2C_ADDR,
            .flags = 0,
            .buffer = cmd,
            .length = 3,
        },
    };

    cmd[0] = (addr >> 8) & 0xff;
    cmd[1] = addr & 0xFF;
    cmd[2] = data;

    ret = I2C_TRANSFER(dev, msg, 1);
    if (ret != OK) {
        return -EIO;
    }

    return 0;
}

/**
 * @brief i2c write for camera sensor (It writes array)
 * @param dev Pointer to structure of i2c device data
 * @param vals Address and values of i2c to write
 * @return zero for success or non-zero on any faillure
 */
static int ov5645_write_array(struct i2c_dev_s *dev,
                              const struct reg_val_tbl *vals)
{
    int ret;

    for ( ; vals->reg_num < OV5645_REG_END; vals++) {
        ret = ov5645_write(dev, vals->reg_num, vals->value);
        if (ret < 0) {
           return ret;
        }
    }

    return 0;
}

static int ov5645_set_stream(struct sensor_info *info, bool on)
{
    return ov5645_write(info->cam_i2c, REG_STREAM_ONOFF, on ? 0x00 : 0xff);
}

/**
 * @brief Power up the sensor
 * @param info Sensor data instance
 */
static void ov5645_power_on(struct sensor_info *info)
{
    gpio_direction_out(OV5645_GPIO_PWDN, 0); /* shutdown -> L */
    gpio_direction_out(OV5645_GPIO_RESET, 0); /* reset -> L */
    usleep(5000);

    gpio_direction_out(OV5645_GPIO_PWDN, 1); /* shutdown -> H */
    usleep(1000);

    gpio_direction_out(OV5645_GPIO_RESET, 1); /* reset -> H */
    usleep(1000);
}

/**
 * @brief Power down the sensor
 * @param info Sensor data instance
 */
static void ov5645_power_off(struct sensor_info *info)
{
    gpio_direction_out(OV5645_GPIO_PWDN, 0); /* shutdown -> L */
    usleep(1000);

    gpio_direction_out(OV5645_GPIO_RESET, 0); /* reset -> L */
    usleep(1000);
}

/**
 * @brief ov5645 sensor configuration function
 * @param info Sensor data instance
 * @param mode Mode to be configured
 * @return zero for success or non-zero on any faillure
 */
static int ov5645_configure(struct sensor_info *info,
                            const struct ov5645_mode_info *mode)
{
    int ret;

    /* Perform a software reset. */
    ov5645_write(info->cam_i2c, 0x3103, 0x11); /* Select PLL input clock */
    ov5645_write(info->cam_i2c, 0x3008, 0x82); /* Software reset */
    usleep(5000);

    /* Apply the initial configuration. */
    ret = ov5645_write_array(info->cam_i2c, ov5645_init_setting);
    if (ret < 0) {
        return -EIO;
    }

    /* Set the mode. */
    ret = ov5645_write_array(info->cam_i2c, mode->regs);
    if (ret) {
        printf("ov5645: failed to set mode\n", __func__);
        return -EIO;
    }

    return 0;
}

/**
 * @brief Get capabilities of camera module
 * @param dev Pointer to structure of device data
 * @param capabilities Pointer that will be stored Camera Module capabilities.
 * @return 0 on success, negative errno on error
 */
static int camera_op_capabilities(struct device *dev, uint32_t *size,
                                  uint8_t *capabilities)
{
    struct sensor_info *info = device_get_private(dev);

    if (info->state != OV5645_STATE_OPEN) {
        return -EPERM;
    }

    /* init capabilities [Fill in fake value]*/
    capabilities[0] = CAP_METADATA_GREYBUS;
    capabilities[0] |= CAP_METADATA_MIPI;
    capabilities[0] |= CAP_STILL_IMAGE;
    capabilities[0] |= CAP_JPEG;

    *size = sizeof(uint32_t);
    return 0;
}

/**
 * @brief Get required data size of camera module information
 * @param dev Pointer to structure of device data
 * @param capabilities Pointer that will be stored Camera Module capabilities.
 * @return 0 on success, negative errno on error
 */
static int camera_op_get_required_size(struct device *dev, uint8_t operation,
                                       uint16_t *size)
{
    struct sensor_info *info = device_get_private(dev);

    if (info->state != OV5645_STATE_OPEN) {
        return -EPERM;
    }

    switch (operation) {
    case SIZE_CAPABILITIES:
        *size = 16;
        return 0;
    default:
        return -EINVAL;
    }
}

/**
 * @brief Set streams configuration to camera module
 * @param dev Pointer to structure of device data
 * @param num_streams Number of streams
 * @param req_flags Flags set in the request by AP
 * @param config Pointer to structure of streams configuration
 * @param res_flags Flags set in the response by camera module
 * @param answer Pointer to structure of camera answer information
 * @return 0 on success, negative errno on error
 */
static int camera_op_set_streams_cfg(struct device *dev, uint8_t *num_streams,
                                     uint8_t req_flags,
                                     struct streams_cfg_req *config,
                                     uint8_t *res_flags,
                                     struct streams_cfg_ans *answer)
{
    struct sensor_info *info = device_get_private(dev);
    const struct ov5645_mode_info *cfg;
    uint8_t i;
    int ret;

    if (info->state != OV5645_STATE_OPEN)
        return -EINVAL;

    /*
     * When unconfiguring the module we can uninit CSI-RX right away as the
     * sensor is already stopped, and then power the sensor off.
     */
    if (*num_streams == 0) {
        csi_rx_uninit(info->cdsidev);
        ov5645_power_off(info);
        return 0;
    }

    /*
     * If more than one stream has been requested, set the
     * format configuration state flag anyway, because this
     * module supports just one stream
     */
    if (*num_streams > WHITE_MODULE_MAX_STREAMS) {
        *num_streams = WHITE_MODULE_MAX_STREAMS;
        *res_flags |= CAMERA_CONF_STREAMS_ADJUSTED;
    }

    /* Match the requested format against the camera module supported ones. */
    for (i = 0; i < ARRAY_SIZE(ov5645_mode_settings); i++) {
        cfg = &ov5645_mode_settings[i];

        if (config->width == cfg->width && config->height == cfg->height &&
            config->format == cfg->format)
            break;
    }

    /*
     * No formats matching the request found;
     *
     * FIXME: hack; as of now, return SXGA as default;
     * TODO: find the closest possible format to the required one
     */
    if (i == ARRAY_SIZE(ov5645_mode_settings)) {
        printf("camera: no matching format found\n");

        cfg = &ov5645_mode_settings[0];

        *res_flags |= CAMERA_CONF_STREAMS_ADJUSTED;
    }

    answer->width = cfg->width;
    answer->height = cfg->height;
    answer->format = cfg->format;
    answer->virtual_channel = 0;
    answer->data_type = cfg->dtype;
    answer->max_size = cfg->frame_max_size;

    /* If testing only or if the format has been adjusted we're done. */
    if (req_flags & CAMERA_CONF_STREAMS_TEST_ONLY ||
        *res_flags & CAMERA_CONF_STREAMS_ADJUSTED)
        return 0;

    /* Power the sensor up and configure it. */
    ov5645_power_on(info);

    ret = ov5645_configure(info, cfg);
    if (ret < 0) {
        ov5645_power_off(info);
        return ret;
    }

    /* Initialize the CSI receiver. */
    csi_rx_init(info->cdsidev, NULL);

    return 0;
}

/**
 * @brief Start the camera capture
 * @param dev Pointer to structure of device data
 * @param capt_info Capture parameters
 * @return 0 on success, negative errno on error
 */
static int camera_op_capture(struct device *dev, struct capture_info *capt_info)
{
    struct sensor_info *info = device_get_private(dev);
    int ret;

    if (info->state != OV5645_STATE_OPEN) {
        return -EPERM;
    }

    /*
     * Start the CSI receiver first as it requires the D-PHY lines to be in the
     * LP-11 state to synchronize to the transmitter.
     */
    ret = csi_rx_start(info->cdsidev);
    if (ret) {
        return ret;
    }

    /* Now start the video stream. */
    ret = ov5645_set_stream(info, true);
    if (ret) {
        return -EIO;
    }

    info->req_id = capt_info->request_id;

    return ret;
}

/**
 * @brief stop stream
 * @param dev The pointer to structure of device data
 * @param request_id The request id set by capture
 * @return 0 for success, negative errno on error.
 */
static int camera_op_flush(struct device *dev, uint32_t *request_id)
{
    struct sensor_info *info = device_get_private(dev);
    int ret;

    if (info->state != OV5645_STATE_OPEN) {
        return -EPERM;
    }

    /*
     * Stop the sensor first as the CSI receiver requires the D-PHY lines to be
     * in the LP-11 state to stop.
     */
    ret = ov5645_set_stream(info, false);
    if (ret) {
         return -EIO;
    }

    /* Now stop the CSI receiver. */
    ret = csi_rx_stop(info->cdsidev);
    if (ret) {
        return ret;
    }

    *request_id = info->req_id;

    return ret;
}

static int camera_sensor_detect(struct sensor_info *info)
{
    uint16_t id;
    int ret;

    /* Power up the sensor and verify the ID register. */
    ov5645_power_on(info);

    ret = ov5645_read(info->cam_i2c, OV5645_ID_HIGH);
    if (ret < 0) {
        goto done;
    }

    id = ret << 8;

    ret = ov5645_read(info->cam_i2c, OV5645_ID_LOW);
    if (ret < 0) {
        goto done;
    }

    id |= ret;

    if (id != OV5645_ID) {
        printf("ov5645 ID mismatch (0x%04x\n)", id);
        ret = -ENODEV;
    }

done:
    ov5645_power_off(info);
    return ret;
}

/**
 * @brief Open camera device
 * @param dev pointer to structure of device data
 * @return 0 on success, negative errno on error
 */
static int camera_dev_open(struct device *dev)
{
    struct sensor_info *info = device_get_private(dev);
    int ret;

    if (info->state == OV5645_STATE_OPEN) {
        return -EBUSY;
    }

    gpio_activate(OV5645_GPIO_PWDN);
    gpio_activate(OV5645_GPIO_RESET);

    /* Initialize I2C access. */
    info->cam_i2c = up_i2cinitialize(OV5645_I2C_PORT);
    if (!info->cam_i2c) {
        ret = -EIO;
        goto error;
    }

    /* Make sure the sensor is present. */
    ret = camera_sensor_detect(info);
    if (ret < 0) {
        goto error;
    }

    /* Open the CSI receiver. */
    info->cdsidev = csi_rx_open(0);
    if (info->cdsidev == NULL) {
        ret = -EINVAL;
        goto error;
    }

    info->state = OV5645_STATE_OPEN;

    return 0;

error:
    printf("Camera initialization failed\n");

    if (info->cdsidev)
        csi_rx_close(info->cdsidev);

    up_i2cuninitialize(info->cam_i2c);

    gpio_deactivate(OV5645_GPIO_PWDN);
    gpio_deactivate(OV5645_GPIO_RESET);

    return ret;
}

/**
 * @brief Close camera device
 * @param dev pointer to structure of device data
 */
static void camera_dev_close(struct device *dev)
{
    struct sensor_info *info = device_get_private(dev);

    /* Stop the stream, power the sensor down, and stop the CSI receiver. */
    ov5645_set_stream(info, false);
    ov5645_power_off(info);
    usleep(10);
    csi_rx_stop(info->cdsidev);

    /* Free all of the resources */
    csi_rx_close(info->cdsidev);
    up_i2cuninitialize(info->cam_i2c);

    gpio_deactivate(OV5645_GPIO_PWDN);
    gpio_deactivate(OV5645_GPIO_RESET);

    info->state = OV5645_STATE_CLOSED;
}

/**
 * @brief Probe camera device
 * @param dev pointer to structure of device data
 * @return 0 on success, negative errno on error
 */
static int camera_dev_probe(struct device *dev)
{
    struct sensor_info *info;

    info = zalloc(sizeof(*info));
    if (!info) {
        return -ENOMEM;
    }

    info->state = OV5645_STATE_CLOSED;
    info->dev = dev;
    device_set_private(dev, info);

    return 0;
}

/**
 * @brief Remove camera device
 * @param dev pointer to structure of device data
 */
static void camera_dev_remove(struct device *dev)
{
    struct sensor_info *info = device_get_private(dev);

    device_set_private(dev, NULL);
    free(info);
}

static struct device_camera_type_ops camera_type_ops = {
    .capabilities       = camera_op_capabilities,
    .get_required_size  = camera_op_get_required_size,
    .set_streams_cfg    = camera_op_set_streams_cfg,
    .capture            = camera_op_capture,
    .flush              = camera_op_flush,
};

static struct device_driver_ops camera_driver_ops = {
    .probe              = camera_dev_probe,
    .remove             = camera_dev_remove,
    .open               = camera_dev_open,
    .close              = camera_dev_close,
    .type_ops           = &camera_type_ops,
};

static struct device_driver camera_driver = {
    .type               = DEVICE_TYPE_CAMERA_HW,
    .name               = "camera",
    .desc               = "Ara White Camera Module Driver",
    .ops                = &camera_driver_ops,
};

static struct device camera_devices[] = {
    {
        .type           = DEVICE_TYPE_CAMERA_HW,
        .name           = "camera",
        .desc           = "Ara White Camera Module",
        .id             = 0,
    },
};

static struct device_table camera_device_table = {
    .device = camera_devices,
    .device_count = ARRAY_SIZE(camera_devices),
};

void ara_module_early_init(void)
{
}

void ara_module_init(void)
{
    device_table_register(&camera_device_table);
    device_register_driver(&camera_driver);
}
