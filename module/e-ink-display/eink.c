/**
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
 *
 * Author: Philip Yang <philipy@bsquare.com>
 */

#include <stddef.h>
#include <string.h>
#include <errno.h>

#include <nuttx/lib.h>
#include <nuttx/kmalloc.h>
#include <nuttx/gpio.h>
#include <nuttx/clock.h>
#include <nuttx/device_hid.h>
#include <nuttx/hid.h>
#include <nuttx/util.h>

#define KEYCODE_PAGEUP          0x4B    /* KEY_PAGEUP */
#define KEYCODE_PAGEDOWN        0x4E    /* KEY_PAGEDOWN */
#define DEFAULT_MODIFIER        0

#define DEBOUNCE_TIMING         25      /* 250ms (1 SysTick = 10ms) */

#define VENDORID                0x0004
#define PRODUCTID               0x0001

#define HID_REPORT_DESC_LEN     35

/* GPIO buttons */
struct hid_btn_desc_s {
    int gpio;
    int keycode;
};

struct hid_btn_desc_s buttons[] = {
    {.keycode = KEYCODE_PAGEUP},
    {.keycode = KEYCODE_PAGEDOWN}
};

/**
 * Private information for buttons
 */
struct button_info {
    /** Chain to button linking list. */
    struct list_head list;

    /** Connected GPIO number */
    uint16_t gpio;

    /** Latest valid keyboard state */
    uint8_t last_keystate;

    /** The keycode for this button returned */
    uint8_t keycode;
};

/**
 * Report data for HID Button
 */
struct hid_kbd_data {
    /** modifier key: bit[0-4]: NumLock, CapsLock, ScrollLock, Compose, KANA */
    uint8_t modifier;

    /** keycode, 0 ~ 101 key value  */
    uint8_t keycode;
} __packed;

static struct device *eink_dev = NULL;

/**
 * Keyboard HID Device Descriptor
 */
struct hid_descriptor btn_dev_desc = {
    0x0A,
    HID_REPORT_DESC_LEN,
    0x0111, /* HID v1.11 compliant */
    PRODUCTID,
    VENDORID,
    0x00, /* no country code */
};

// Input report - 4 bytes
//
// Byte |  D7    D6    D5    D4     D3        D2        D1      D0
// -----+-------------------------------------------------------------------
//  0   |  0     0     0   KANA  Compose  ScrollLock CapsLock NumLock
//  1   |                         Keycode
//
// Output report - n/a
//
// Feature report - n/a
//

/**
 * Simulation report descriptor for HID KEYPAD
 */
uint8_t btn_report_desc[HID_REPORT_DESC_LEN] = {
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

/**
 * report length of each HID Reports in HID Report Descriptor
 */
struct hid_size_info btn_sizeinfo[] =
{
    /**
     * Parsed by HID Descriptor tool, this application only support INPUT
     * report, so the FEATURE and OUTPUT size is 0.
     */
    { .id = 0,
      .reports = {
          .size = { 2, 0, 0 }
       }
    },
};

/**
 * @brief Get eInk private data
 *
 * @param dev Pointer to structure of device data
 * @param gpio The button of gpio number
 * @return Return button_info struct pointer or NULL for not find.
 */
static struct button_info *eink_get_info(struct device *dev, uint16_t gpio)
{
    struct hid_info *info = device_get_private(dev);
    struct button_info *dev_info = NULL;
    struct list_head *iter;

    list_foreach(&info->device_list, iter) {
        dev_info = list_entry(iter, struct button_info, list);
        if (dev_info->gpio == gpio) {
            return dev_info;
        }
    }

    return NULL;
}

/**
 * @brief HID device keys interrupt routing
 *
 * @param irq IRQ number, same as GPIO number.
 * @param context Pointer to structure of device data
 * @return 0 on success, negative errno on error
 */
static int eink_handle_btn_irq_event(int irq, FAR void *context)
{

    struct hid_info *info = device_get_private(eink_dev);
    struct hid_kbd_data kbd;
    struct device *dev = eink_dev;
    struct button_info *btn_info = NULL;
    int value;

    if (!dev || !device_get_private(dev)) {
        return ERROR;
    }

    btn_info = eink_get_info(dev, irq);
    if (!btn_info) {
        return ERROR;
    }

    gpio_irq_mask(irq);

    value = gpio_get_value(btn_info->gpio);

    /* check whether the key state change or not */
    if (btn_info->last_keystate != value) {
        btn_info->last_keystate = value;

        kbd.modifier = 0;
        kbd.keycode = btn_info->last_keystate ? btn_info->keycode : 0;

        if (info->event_callback) {
            info->event_callback(dev, HID_INPUT_REPORT, (uint8_t*)&kbd,
                                 sizeof(struct hid_kbd_data));
       }
    }

    gpio_irq_unmask(irq);

    return OK;
}

/**
 * @brief Get HID Input report data
 *
 * @param dev Pointer to structure of device data
 * @param report_id HID report id
 * @param data Pointer of input buffer size
 * @param len Max input buffer size
 * @return 0 on success, negative for error
 */
static int eink_get_input_report(struct device *dev, enum hid_report_type report_id,
                                 uint8_t *data, uint16_t len)
{
    struct hid_kbd_data *kbd;
    int ret = 0;

    if (len) {
        if (!report_id) {
            if (len < sizeof(struct hid_kbd_data)) {
                return -EINVAL;
            }
            /* get keyboard data and return to upper layer */
            kbd = (struct hid_kbd_data *)data;
            kbd->modifier = 0;
            kbd->keycode = 0;
        } else {
            /* No multiple Report ID in this application. */
            ret = -EIO;
        }
    } else {
        /* Required Input Report in report descriptor was not found. */
        ret = -EIO;
    }
    return ret;
}

/**
 * @brief Specific GPIO deinitialize and release resource
 *
 * @param dev Pointer to structure of button_info
 */
static void eink_gpio_deinit(struct button_info *btn_info)
{
    gpio_irq_mask(btn_info->gpio);
    gpio_deactivate(btn_info->gpio);
    list_del(&btn_info->list);
    free(btn_info);
}

/**
 * @brief GPIOs deinitialize and release resources
 *
 * @param dev Pointer to structure of device data
 */
static void eink_gpios_deinit(struct device *dev)
{
    struct hid_info *info = device_get_private(dev);
    struct button_info *btn_info = NULL;
    struct list_head *iter;

    list_foreach(&info->device_list, iter) {
        btn_info = list_entry(iter, struct button_info, list);
        if (btn_info) {
            eink_gpio_deinit(btn_info);
            iter = &info->device_list;
        }
    }

    return;
}

/**
 * @brief Initialize button
 *
 * @param dev Pointer to structure of device data
 * @param index The index of hid_btn_desc_s table
 */
static int eink_gpio_init(struct device *dev, uint16_t index)
{
    struct hid_info *info = device_get_private(dev);
    struct button_info *btn_info = NULL;
    int gpio = buttons[index].gpio;
    int ret = 0;

    ret = gpio_activate(gpio);
    if (ret != 0) {
        return ret;
    }

    btn_info = zalloc(sizeof(*btn_info));
    if (!btn_info) {
        return -ENOMEM;
    }

    btn_info->gpio = gpio;
    gpio_direction_in(gpio);
    gpio_irq_mask(gpio);
    gpio_irq_settriggering(gpio, IRQ_TYPE_EDGE_BOTH);
    gpio_set_debounce(gpio, DEBOUNCE_TIMING);
    btn_info->keycode = buttons[index].keycode;
    gpio_irq_attach(gpio, eink_handle_btn_irq_event);
    list_add(&info->device_list, &btn_info->list);

    return ret;
}

/**
 * @brief Configure eInk display hardware setting
 *
 * @param dev Pointer to structure of device data
 * @param dev_info The pointer for hid_info struct
 *
 * @return 0 on success, negative errno on error
 */
static int eink_hw_initialize(struct device *dev, struct hid_info *dev_info)
{
    int ret = 0;
    int i;
    struct device_resource *r;

    for (i = 0; i < ARRAY_SIZE(buttons); i++) {
        r = device_resource_get(dev, DEVICE_RESOURCE_TYPE_GPIO, i);
        if (!r ) {
            goto err_hw_init;
        }
        /* initialize GPIO pin */
        if (r->start >= gpio_line_count()) {
            goto err_hw_init;
        }

        buttons[i].gpio = r->start;

        ret = eink_gpio_init(dev, i);
        if (ret) {
            goto err_hw_init;
        }
    }

    return ret;

err_hw_init:
    eink_gpios_deinit(dev);
    return ret;
}

/**
 * @brief Deinitialize eInk display hardware setting
 *
 * @param dev Pointer to structure of device data
 * @param dev_info The pointer for hid_info struct
 *
 * @return 0 on success, negative errno on error
 */
static int eink_hw_deinitialize(struct device *dev)
{
    eink_gpios_deinit(dev);

    return 0;
}

/**
 * @brief Set eink hardware power
 *
 * @param dev Pointer to structure of device data
 * @param on true for ON, false for OFF
 *
 * @return 0 on success, negative errno on error
 */
static int eink_power_set(struct device *dev, bool on)
{
    struct hid_info *info = device_get_private(dev);
    struct button_info *btn_info = NULL;
    struct list_head *iter;

    list_foreach(&info->device_list, iter) {
        btn_info = list_entry(iter, struct button_info, list);
        if (btn_info) {
            if (on) {
                /* enable interrupt */
                gpio_irq_unmask(btn_info->gpio);
            } else {
                gpio_irq_mask(btn_info->gpio);
            }
        }
    }

    return 0;
}

/**
 * @brief Process get report requirement
 *
 * @param dev Pointer to structure of device data
 * @param report_type Required report type
 * @param report_id Required report id
 * @param data Pointer to a Data buffer
 * @param len Length for required
 *
 * @return 0 on success, negative errno on error
 */
static int eink_get_report(struct device *dev, enum hid_report_type report_type,
                           uint8_t report_id, uint8_t *data, uint16_t len)
{
    int ret = 0;

    switch (report_type) {
        case HID_INPUT_REPORT:
            ret = eink_get_input_report(dev, report_id, data, len);
        break;
        default:
            ret = -EINVAL;
        break;
    }

    return ret;

}

static struct hid_vendor_ops eink_btn_ops = {
    .hw_initialize = eink_hw_initialize,
    .hw_deinitialize = eink_hw_deinitialize,
    .power_control = eink_power_set,
    .get_report = eink_get_report,
    .set_report = NULL,
};

int hid_device_init(struct device *dev, struct hid_info *dev_info)
{
    int ret = 0;

    dev_info->hdesc = &btn_dev_desc;
    dev_info->rdesc = btn_report_desc;
    dev_info->sinfo = btn_sizeinfo;
    dev_info->num_ids = ARRAY_SIZE(btn_sizeinfo);
    dev_info->hid_dev_ops = &eink_btn_ops;
    eink_dev = dev;

    return ret;

}
