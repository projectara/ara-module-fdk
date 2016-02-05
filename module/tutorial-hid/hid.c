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

#include <errno.h>

#include <nuttx/device.h>
#include <nuttx/device_hid.h>
#include <nuttx/gpio.h>
#include <nuttx/kmalloc.h>
#include <nuttx/util.h>

#include <stdint.h>
#include <string.h>
#include <syslog.h>

/* HID Keycode defines */
#define HID_KEYCODE_A               0x04    // 'A' key
#define HID_KEYCODE_B               0x05    // 'B' key
#define HID_KEYCODE_NONE            0x00    // no keys pressed
#define HID_KEYCODE_MODIFIER_NONE   0x00    // no modifier keys pressed

/* HID Descriptor Defines */
#define HID_VERSION                 0x0111  // HID version 1.11
#define HID_PRODUCT_ID              0xABCD  // not equal to module manifest PID
#define HID_VENDOR_ID               0x1234  // not equal to module manifest VID
#define HID_COUNTRY_CODE            0x00    // no country code
#define HID_REPORT_ID               0       // only support one report id

// saved driver device
static struct device *saved_dev = NULL;

/* HID Buttons Private Data Struct */
struct hid_info_s {
    struct hid_descriptor *hdesc;       // HID device descriptor
    uint8_t *rdesc;                     // HID report descriptor
    hid_event_callback event_callback;  // HID event callback function
 };

/* Report data for HID Button */
struct hid_btn_data_s {
    uint8_t modifier;   // bitfield for modifier keys
    uint8_t keycode;    // 0 -> 101 HID keycode value
} __packed;

/* GPIO buttons */
struct hid_btn_desc_s {
    int gpio;
    int keycode;
};

/* HID Report Descriptor */
static uint8_t hid_report_desc[] = {
    0x05, 0x01,     /* USAGE_PAGE (Generic Desktop) */
    0x09, 0x06,     /* USAGE (Keyboard) */
    0xa1, 0x01,     /* COLLECTION (Application) */
    0x05, 0x07,     /*   USAGE_PAGE (Keyboard) */
    0x19, 0xe0,     /*   USAGE_MINIMUM (Keyboard LeftControl) */
    0x29, 0xe7,     /*   USAGE_MAXIMUM (Keyboard Right GUI) */
    0x15, 0x00,     /*   LOGICAL_MINIMUM (0) */
    0x25, 0x01,     /*   LOGICAL_MAXIMUM (1) */
    0x75, 0x01,     /*   REPORT_SIZE (1) */
    0x95, 0x08,     /*   REPORT_COUNT (8) */
    0x81, 0x02,     /*   INPUT (Data,Var,Abs) */
    0x95, 0x01,     /*   REPORT_COUNT (1) */
    0x75, 0x08,     /*   REPORT_SIZE (8) */
    0x25, 0x65,     /*   LOGICAL_MAXIMUM (101) */
    0x19, 0x00,     /*   USAGE_MINIMUM (Reserved (no event)) */
    0x29, 0x65,     /*   USAGE_MAXIMUM (Keyboard Application) */
    0x81, 0x00,     /*   INPUT (Data,Ary,Abs) */
    0xc0            /* END_COLLECTION */
};

/* HID Device Descriptor */
static struct hid_descriptor hid_dev_desc = {
    sizeof(struct hid_descriptor),
    sizeof(hid_report_desc),
    HID_VERSION,
    HID_PRODUCT_ID,
    HID_VENDOR_ID,
    HID_COUNTRY_CODE
};

/* GPIO Buttons */
static struct hid_btn_desc_s hid_btn_desc[] = {
    { .keycode = HID_KEYCODE_A },   // 'a' key
    { .keycode = HID_KEYCODE_B }    // 'b' key
};

int hid_btn_handle_irq_event(int irq, FAR void *context)
{
    struct hid_btn_data_s hid_btn_data;
    struct hid_info_s *hid_info = device_get_private(saved_dev);

    int i = 0;
    for (i = 0; i < ARRAY_SIZE(hid_btn_desc); i++) {

        if (hid_btn_desc[i].gpio == irq) {

            if (gpio_get_value(hid_btn_desc[i].gpio)) {
              hid_btn_data.keycode = hid_btn_desc[i].keycode;
              lowsyslog("button %d pressed\n", irq);
            } else {
              hid_btn_data.keycode = HID_KEYCODE_NONE;
              lowsyslog("button %d released\n", irq);
            }

            hid_btn_data.modifier = HID_KEYCODE_MODIFIER_NONE;

            if (hid_info->event_callback != NULL) {
                hid_info->event_callback(saved_dev,
                                        HID_INPUT_REPORT,
                                        (uint8_t*) &hid_btn_data,
                                        sizeof(struct hid_btn_data_s));
            }

            break;
        }
    }

    return OK;
}

static int hid_btn_power_on(struct device *dev)
{
    int i = 0;
    for (i = 0; i < ARRAY_SIZE(hid_btn_desc); i++) {
        gpio_irq_unmask(hid_btn_desc[i].gpio);   // enable interrupts
    }

    return OK;
}

static int hid_btn_power_off(struct device *dev)
{
    int i = 0;
    for (i = 0; i < ARRAY_SIZE(hid_btn_desc); i++) {
        gpio_irq_mask(hid_btn_desc[i].gpio); // disable interrupt
    }

    return OK;
}

static int hid_btn_get_desc(struct device *dev, struct hid_descriptor *desc)
{
    struct hid_info_s *hid_info = device_get_private(dev);

    /* get HID device descriptor */
    memcpy(desc, hid_info->hdesc, sizeof(struct hid_descriptor));

    return OK;
}

static int hid_btn_get_report_desc(struct device *dev, uint8_t *desc)
{
    struct hid_info_s *hid_info = device_get_private(dev);

    /* get HID report descriptor */
    memcpy(desc, hid_info->rdesc, sizeof(hid_report_desc));

    return OK;
}

static int hid_btn_get_report_len(struct device *dev,
                                  enum hid_report_type report_type,
                                  uint8_t report_id)
{
    if (report_type == HID_INPUT_REPORT && report_id == HID_REPORT_ID) {
        // size of modifier byte + keycode byte = 2 bytes
        return sizeof(struct hid_btn_data_s);
    }   // else report length is still zero

    return 0;
}

static int hid_btn_get_max_report_len(struct device *dev,
                                      enum hid_report_type report_type)
{
    if (report_type == HID_INPUT_REPORT) {
        // size of modifier byte + keycode byte = 2 bytes
        return sizeof(struct hid_btn_data_s);
    }   // else max length remains zero

    return 0;
}

static int hid_btn_get_report(struct device *dev,
                              enum hid_report_type report_type,
                              uint8_t report_id, uint8_t *data, uint16_t len)
{
    struct hid_btn_data_s *hid_btn_data = NULL;

    if (len < sizeof(struct hid_btn_data_s)) {
        return -EINVAL;
    }

    if (report_type == HID_INPUT_REPORT && report_id == HID_REPORT_ID) {
        // fill *data with initial key state
        hid_btn_data = (struct hid_btn_data_s *) data;
        hid_btn_data->modifier = HID_KEYCODE_MODIFIER_NONE;
        hid_btn_data->keycode = HID_KEYCODE_NONE;
    } else {
        return -EINVAL; // only support type input report
    }

   return OK;
}

static int hid_btn_register_callback(struct device *dev,
                                     hid_event_callback callback)
{
    struct hid_info_s *hid_info = device_get_private(dev);

    hid_info->event_callback = callback;
    return OK;
}

static int hid_btn_unregister_callback(struct device *dev)
{
    struct hid_info_s *hid_info = device_get_private(dev);

    hid_info->event_callback = NULL;
    return OK;
}

static int hid_btn_probe(struct device *dev)
{
    struct hid_info_s *hid_info = zalloc(sizeof(struct hid_info_s));

    if (hid_info == NULL) {
        return -ENOMEM;
    }

    saved_dev = dev;

    hid_info->hdesc = &hid_dev_desc;
    hid_info->rdesc = hid_report_desc;
    hid_info->event_callback = NULL;

    device_set_private(dev, hid_info);
    
    return OK;
}

static int hid_btn_open(struct device *dev)
{
    struct device_resource *hid_btn_resource = NULL;
    int ret = OK;

    /* initialize GPIO pins */
    int i = 0;
    for (i = 0; i < ARRAY_SIZE(hid_btn_desc); i++) {

        hid_btn_resource = device_resource_get(dev,
                                               DEVICE_RESOURCE_TYPE_GPIO,
                                               i);
        if (hid_btn_resource == NULL) {
            return -EINVAL;
        }

        // assign driver resource pin number to hid_btn_desc gpio
        hid_btn_desc[i].gpio = hid_btn_resource->start;

        ret = gpio_activate(hid_btn_desc[i].gpio);
        if (ret != OK) {
            goto error_gpio_init;
        }

        gpio_direction_in(hid_btn_desc[i].gpio);
        gpio_irq_settriggering(hid_btn_desc[i].gpio, IRQ_TYPE_EDGE_BOTH);
        gpio_irq_attach(hid_btn_desc[i].gpio, hid_btn_handle_irq_event);
    }

    return OK;

error_gpio_init:
    while (i-- > 0) {
        gpio_deactivate(hid_btn_desc[i].gpio);
    }
    return ret;
}

static void hid_btn_close(struct device *dev)
{
    struct hid_info_s *hid_info = device_get_private(dev);

    /* uninitialize GPIO pins */
    int i = 0;
    for (i = 0; i < ARRAY_SIZE(hid_btn_desc); i++) {
        gpio_deactivate(hid_btn_desc[i].gpio);
    }

    /* clear HID event callback */
    hid_info->event_callback = NULL;
}

static void hid_btn_remove(struct device *dev)
{
    struct hid_info_s *hid_info = device_get_private(dev);

    if (device_is_open(dev)) {
        hid_btn_close(dev);
    }

    device_set_private(dev, NULL);
    saved_dev = NULL;
    free(hid_info);
}

/* HID Driver Operation Functions */
static struct device_hid_type_ops   hid_button_type_ops = {
    .power_on                       = hid_btn_power_on,
    .power_off                      = hid_btn_power_off,
    .get_descriptor                 = hid_btn_get_desc,
    .get_report_descriptor          = hid_btn_get_report_desc,
    .get_report_length              = hid_btn_get_report_len,
    .get_maximum_report_length      = hid_btn_get_max_report_len,
    .get_report                     = hid_btn_get_report,
    .register_callback              = hid_btn_register_callback,
    .unregister_callback            = hid_btn_unregister_callback
};

/* Device Driver Operation Functions */
static struct device_driver_ops     hid_button_driver_ops = {
    .probe                          = hid_btn_probe,
    .remove                         = hid_btn_remove,
    .open                           = hid_btn_open,
    .close                          = hid_btn_close,
    .type_ops                       = &hid_button_type_ops
};

/* Device Driver Info */
struct device_driver         hid_button_driver = {
    .type                           = DEVICE_TYPE_HID_HW,
    .name                           = "hid_button",
    .desc                           = "HID Button Driver",
    .ops                            = &hid_button_driver_ops
};
