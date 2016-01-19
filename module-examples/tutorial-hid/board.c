/**
 * Copyright (c) 2016 Google Inc.
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
 * Author: Michael Mogenson <michael.mogenson@leaflabs.com>
 */

#include <nuttx/config.h>
#include <nuttx/device.h>
#include <nuttx/device_hid.h>
#include <nuttx/device_table.h>
#include <nuttx/util.h>

#include <syslog.h>

static struct device_resource hid_btn_resources[] = {
    {
        .name   = "HID Button A",
        .type   = DEVICE_RESOURCE_TYPE_GPIO,
        .start  = 18,   // GPIO18
        .count  = 1,
    },
    {
        .name   = "HID Button B",
        .type   = DEVICE_RESOURCE_TYPE_GPIO,
        .start  = 23,   // GPIO23
        .count  = 1,
    },
};

/* Device driver data */
static struct device devices[] = {
    {
        .type           = DEVICE_TYPE_HID_HW,
        .name           = "hid_button",
        .desc           = "HID Button Driver",
        .id             = 0,
        .resources      = hid_btn_resources,
        .resource_count = ARRAY_SIZE(hid_btn_resources),
    },
};

/* Device driver table */
static struct device_table hid_device_table = {
    .device = devices,
    .device_count = ARRAY_SIZE(devices),
};

extern struct device_driver hid_button_driver;

void ara_module_early_init(void)
{
}

void ara_module_init(void)
{
    lowsyslog("HID Tutorial Module Init\n");

    device_table_register(&hid_device_table);
    device_register_driver(&hid_button_driver);
}
