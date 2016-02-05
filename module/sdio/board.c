/**
 * Copyright (c) 2015 Google Inc.
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

#include <syslog.h>
#include <errno.h>

#include <nuttx/config.h>
#include <nuttx/device.h>
#include <nuttx/device_table.h>
#include <nuttx/device_sdio_board.h>

#define SD_POWER_EN_PIN    9 /* GPIO 9 */
#define SD_CARD_DETECT_PIN 22 /* GPIO 22 */

static struct device_resource sdio_board_resources[] = {
    {
        .name  = "sdio_gpio_power",
        .type  = DEVICE_RESOURCE_TYPE_GPIO,
        .start = SD_POWER_EN_PIN,
        .count = 1,
    },
    {
        .name  = "sdio_gpio_cd",
        .type  = DEVICE_RESOURCE_TYPE_GPIO,
        .start = SD_CARD_DETECT_PIN,
        .count = 1,
    },
};

static struct device devices[] = {
    {
        .type           = DEVICE_TYPE_SDIO_BOARD_HW,
        .name           = "sdio_board",
        .desc           = "SDIO Board Device",
        .id             = 0,
        .resources      = sdio_board_resources,
        .resource_count = ARRAY_SIZE(sdio_board_resources),
    },
};

static struct device_table sdio_device_table = {
    .device = devices,
    .device_count = ARRAY_SIZE(devices),
};

void ara_module_early_init(void)
{
}

void ara_module_init(void)
{
    extern struct device_driver sdio_board_driver;

    lowsyslog("SDIO board module init\n");

    device_table_register(&sdio_device_table);
    device_register_driver(&sdio_board_driver);
}
