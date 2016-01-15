/**
 * Copyright (c) 2015-2016 Google Inc.
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
 *
 * Author: Mark Greer <mgreer@animalcreek.com>
 */

#include <syslog.h>
#include <errno.h>
#include <nuttx/util.h>
#include <nuttx/device.h>
#include <nuttx/device_table.h>
#include <nuttx/device_audio_board.h>
#include <nuttx/device_codec.h>
#include <nuttx/ara/audio_board.h>

extern struct device_driver audio_board_driver;
extern struct device_driver rt5647_codec;

static struct audio_board_dai white_audio_dais_bundle_0[] = {
    {
        .data_cport = 4, /* Must match Audio DATA CPort in manifest */
        .i2s_dev_id = 0, /* ID of I2S Device */
    },
};

static struct audio_board_bundle white_audio_bundles[] = {
    {
        .mgmt_cport     = 3, /* Must match Audio MGMT CPort in manifest */
        .codec_dev_id   = 0, /* ID of codec device */
        .dai_count      = ARRAY_SIZE(white_audio_dais_bundle_0),
        .dai            = white_audio_dais_bundle_0,
    },
};

static struct audio_board_init_data white_audio_board_init_data = {
    .bundle_count   = ARRAY_SIZE(white_audio_bundles),
    .bundle         = white_audio_bundles,
};

static struct device_resource white_audio_rt5647_resources[] = {
    {
        .name  = "rt5647_i2c_addr",
        .type  = DEVICE_RESOURCE_TYPE_I2C_ADDR,
        .start = 0x1b,
        .count = 1,
    },
};

static struct device white_audio_devices[] = {
    {
        .type           = DEVICE_TYPE_AUDIO_BOARD_HW,
        .name           = "audio_board",
        .desc           = "White-module Audio Information",
        .id             = 0,
        .init_data      = &white_audio_board_init_data,
    },
    {
        .type           = DEVICE_TYPE_CODEC_HW,
        .name           = "rt5647",
        .desc           = "Realtek ALC5647 Audio Codec",
        .id             = 0,
        .resources      = white_audio_rt5647_resources,
        .resource_count = ARRAY_SIZE(white_audio_rt5647_resources),
    },
};

static struct device_table white_audio_device_table = {
    .device = white_audio_devices,
    .device_count = ARRAY_SIZE(white_audio_devices),
};

void ara_module_early_init(void)
{
}

void ara_module_init(void)
{
    lowsyslog("White Audio Module init\n");

    device_table_register(&white_audio_device_table);

    device_register_driver(&audio_board_driver);
    device_register_driver(&rt5647_codec);
}
