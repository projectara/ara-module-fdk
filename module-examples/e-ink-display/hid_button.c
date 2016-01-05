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
#include <nuttx/device.h>
#include <nuttx/device_hid.h>
#include <nuttx/gpio.h>
#include <nuttx/clock.h>

#define HID_DEVICE_FLAG_PROBE   BIT(0)
#define HID_DEVICE_FLAG_OPEN    BIT(1)
#define HID_DEVICE_FLAG_POWERON BIT(2)

#define MAX_IO_INPUT            2       /* two buttons for this module */
#define GPIO_KBDPAGEUP          0
#define GPIO_KBDPAGEDOWN        9
#define KEYCODE_PAGEUP          0x4B    /* KEY_PAGEUP */
#define KEYCODE_PAGEDOWN        0x4E    /* KEY_PAGEDOWN */
#define DEFAULT_MODIFIER        0

#define DEBOUNCE_TIMEING        25      /* 250ms (1 SysTick = 10ms) */
#define COMMAND_INTERVAL        1000    /* 1ms */

#define VENDORID                0x18D1  /* need discussion */
#define PRODUCTID               0x1234  /* need discussion */

static struct device *hid_dev = NULL;

/**
 * HID Report Size Structure. Type define for a report item size
 * information structure, to retain the size of a device's reports by ID.
 */
struct hid_size_info {
    /** Report ID */
    uint8_t id;

    /**
     * HID Report length array
     *
     * size[0] : Input Report length
     * size[1] : Output Report length
     * size[2] : Feature Report length
     */
    uint16_t size[3];
};

/**
 * Private information for buttons
 */
struct button_info {
    /** Chain to button linking list. */
    struct list_head list;

    /** Connected GPIO number */
    uint16_t gpio;

    /** Latest valid keyboard interrupt time */
    uint32_t last_activetime;

    /** Latest valid keyboard state */
    uint8_t last_keystate;

    /** The keycode for this button returned */
    uint8_t Keycode;

    /** Notifying debounce count start */
    sem_t active_debounce;

    /** Handler for debounce count thread */
    pthread_t pthread_handler;

    /** inform the thread should be terminated */
    uint8_t thread_stop;
};

/**
 * Private information for eInk of HID button device
 */
struct hid_buttons_info {
    /** Chain to buttons linking list. */
    struct list_head buttons_list;

    /** Driver module representation of the device */
    struct device *dev;

    /** HID device descriptor */
    struct hid_descriptor *hdesc;

    /** HID report descriptor */
    uint8_t *rdesc;

    /** Number of HID Report structure */
    int num_ids;

    /** Report length of each HID Report */
    struct hid_size_info *sinfo;

    /** HID device driver operation state*/
    int state;

    /** Hid input event callback function */
    hid_event_callback event_callback;

    /** Exclusive access for operation */
    sem_t lock;

    /**
     * Default modifier key
     *
     * For 1-key HID device, device need to report modifier + keycode data.
     * bit[0-4] : Num Lock, Caps Lock, Scroll Lock, Compose, KANA
     */
    uint8_t modifier;
};

#define HID_REPORT_DESC_LEN     35

/**
 * Keyboard HID Device Descriptor
 */
struct hid_descriptor hid_dev_desc = {
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
uint8_t hid_report_desc[HID_REPORT_DESC_LEN] = {
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
 * Report data for HID Button
 */
struct hid_kbd_data {
    /** modifier key: bit[0-4]: NumLock, CapsLock, ScrollLock, Compose, KANA */
    uint8_t modifier;

    /** keycode, 0 ~ 101 key value  */
    uint8_t keycode;
} __packed;

/**
 * report length of each HID Reports in HID Report Descriptor
 */
struct hid_size_info hid_sizeinfo[] =
{
    /**
     * Parsed by HID Descriptor tool, this application only support INPUT
     * report, so the FEATURE and OUTPUT size is 0.
     */
    { 0, { 2, 0, 0 } },
};

/**
 * @brief Get button private data
 *
 * @param dev Pointer to structure of device data
 * @param gpio The button of gpio number
 * @return Return button_info struct pointer or NULL for not find.
 */
static struct button_info *btn_get_info(struct device *dev, uint16_t gpio)
{
    struct hid_buttons_info *info = device_get_private(dev);
    struct button_info *dev_info = NULL;
    struct list_head *iter;

    list_foreach(&info->buttons_list, iter) {
        dev_info = list_entry(iter, struct button_info, list);
        if (dev_info->gpio == gpio) {
            return dev_info;
        }
    }

    return NULL;
}

/**
 * @brief Get HID report length
 *
 * @param dev Pointer to structure of device data
 * @param report_type HID report type
 * @param report_id HID report id
 * @return 0 on success, negative errno on error
 */
static int btn_get_report_length(struct device *dev, uint8_t report_type,
                                 uint8_t report_id)
{
    struct hid_buttons_info *info = NULL;
    int ret = 0, i;

    /* check input parameters */
    if (!dev || !dev->private) {
        return -EINVAL;
    }

    info = device_get_private(dev);

    /* lookup the hid_size_info table to find the report size */
    for (i = 0; i < info->num_ids; i++) {
        if (info->sinfo[i].id == report_id) {
            ret = info->sinfo[i].size[report_type];
            break;
        }
    }
    return ret;
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
static int btn_get_input_report(struct device *dev, uint8_t report_id,
                                uint8_t *data, uint16_t len)
{
    struct hid_buttons_info *info = NULL;
    struct hid_kbd_data *kbd;
    int ret = 0, rptlen = 0;

    info = device_get_private(dev);

    rptlen = btn_get_report_length(dev, HID_INPUT_REPORT, report_id);

    if (rptlen) {
        if (!report_id) {
            if (len < sizeof(struct hid_kbd_data)) {
                return -EINVAL;
            }
            /* get keyboard data and return to upper layer */
            kbd = (struct hid_kbd_data *)data;
            kbd->modifier = info->modifier;
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
 * @brief Enable GPIO signal debounce filter
 *
 * @param dev Pointer to structure of device data
 * @param btn_info Pointer to structure of button_info
 * @param irq IRQ number.
 * @return 0 on success, negative errno on error
 */
static int btn_software_debounce(struct device *dev,
                                 struct button_info *btn_info, int irq)
{
    uint8_t value = 0;

    gpio_mask_irq(irq);

    value = gpio_get_value(btn_info->gpio);

    /* check whether the key state change or not */
    if (btn_info->last_keystate != value) {
        btn_info->last_keystate = value;

        /* Enable counting thread */
        btn_info->last_activetime = clock_systimer();
        sem_post(&btn_info->active_debounce);
    }

    gpio_unmask_irq(irq);

    return 0;
}

/**
 * @brief HID device PAGEUP key interrupt routing
 *
 * @param irq IRQ number.
 * @param context Pointer to structure of device data
 * @return 0 on success, negative errno on error
 */
int hid_handle_kbdup_irq_event(int irq, FAR void *context)
{
    struct device *dev = hid_dev;
    struct button_info *btn_info = NULL;
    int ret = 0;

    if (!dev || !dev->private) {
        return ERROR;
    }

    btn_info = btn_get_info(dev, GPIO_KBDPAGEUP);
    if (!btn_info) {
        return ERROR;
    }

    ret = btn_software_debounce(dev, btn_info, irq);
    if (ret) {
        return -EAGAIN;
    }

    return OK;
}

/**
 * @brief HID device PAGEDOWN key interrupt routing
 *
 * @param irq IRQ number.
 * @param context Pointer to structure of device data
 * @return 0 on success, negative errno on error
 */
int hid_handle_kbddn_irq_event(int irq, FAR void *context)
{
    struct device *dev = hid_dev;
    struct button_info *btn_info = NULL;
    int ret = 0;

    if (!dev || !dev->private) {
        return ERROR;
    }

    btn_info = btn_get_info(dev, GPIO_KBDPAGEDOWN);
    if (!btn_info) {
        return ERROR;
    }

    ret = btn_software_debounce(dev, btn_info, irq);
    if (ret) {
        return -EAGAIN;
    }

    return OK;
}

/**
 * @brief Power-on the HID device.
 *
 * @param dev Pointer to structure of device data
 * @return 0 on success, negative errno on error
 */
static int hid_button_power_on(struct device *dev)
{
    struct hid_buttons_info *info = NULL;
    int ret = 0;

    /* check input parameters */
    if (!dev || !dev->private) {
        return -EINVAL;
    }

    info = device_get_private(dev);

    sem_wait(&info->lock);

    if (!(info->state & HID_DEVICE_FLAG_OPEN)) {
        /* device isn't opened. */
        ret = -EIO;
        goto err_poweron;
    }

    if (!(info->state & HID_DEVICE_FLAG_POWERON)) {
        info->state |= HID_DEVICE_FLAG_POWERON;
        /* enable interrupt */
        gpio_unmask_irq(GPIO_KBDPAGEUP);
        gpio_unmask_irq(GPIO_KBDPAGEDOWN);
    } else {
        ret = -EBUSY;
    }

err_poweron:
    sem_post(&info->lock);
    return ret;
}

/**
 * @brief Power-off the HID device.
 *
 * @param dev Pointer to structure of device data
 * @return 0 on success, negative errno on error
 */
static int hid_button_power_off(struct device *dev)
{
    struct hid_buttons_info *info = NULL;
    int ret = 0;

    /* check input parameters */
    if (!dev || !dev->private) {
        return -EINVAL;
    }

    info = device_get_private(dev);

    sem_wait(&info->lock);

    if (!(info->state & HID_DEVICE_FLAG_OPEN)) {
        /* device isn't opened. */
        ret = -EIO;
        goto err_poweroff;
    }
    if (info->state & HID_DEVICE_FLAG_POWERON) {
        /* changed power-on state */
        info->state &= ~HID_DEVICE_FLAG_POWERON;
        /* disable interrupt */
        gpio_mask_irq(GPIO_KBDPAGEUP);
        gpio_mask_irq(GPIO_KBDPAGEDOWN);
    } else {
        ret = -EIO;
    }

err_poweroff:
    sem_post(&info->lock);
    return ret;
}

/**
 * @brief Get HID Descriptor
 *
 * @param dev Pointer to structure of device data
 * @param desc Pointer to structure of HID device descriptor
 * @return 0 on success, negative errno on error
 */
static int hid_button_get_desc(struct device *dev, struct hid_descriptor *desc)
{
    struct hid_buttons_info *info = NULL;
    int ret = 0;

    /* check input parameters */
    if (!dev || !dev->private || !desc) {
        return -EINVAL;
    }

    info = device_get_private(dev);

    sem_wait(&info->lock);

    if (!(info->state & HID_DEVICE_FLAG_OPEN)) {
        /* device isn't opened. */
        ret = -EIO;
        goto err_desc;
    }

    /* get HID device descriptor */
    memcpy(desc, info->hdesc, sizeof(struct hid_descriptor));

err_desc:
    sem_post(&info->lock);
    return ret;
}

/**
 * @brief Get HID Report Descriptor
 *
 * @param dev pointer to structure of device data
 * @param desc pointer to HID report descriptor
 * @return 0 on success, negative errno on error
 */
static int hid_button_get_report_desc(struct device *dev, uint8_t *desc)
{
    struct hid_buttons_info *info = NULL;
    int ret = 0;

    /* check input parameters */
    if (!dev || !dev->private || !desc) {
        return -EINVAL;
    }

    info = device_get_private(dev);

    sem_wait(&info->lock);

    if (!(info->state & HID_DEVICE_FLAG_OPEN)) {
        /* device isn't opened. */
        ret = -EIO;
        goto err_report_desc;
    }

    /* get HID report descriptor */
    memcpy(desc, info->rdesc, info->hdesc->report_desc_length);

err_report_desc:
    sem_post(&info->lock);
    return ret;
}

/**
 * @brief Get maximum report descriptor size
 *
 * Since the descriptor size is flexible for each REPORT type, so to create
 * this function for caller if they need for buffer design.
 *
 * @param dev Pointer to structure of device data
 * @param report_type HID report type
 * @return the report size on success, negative errno on error
 */
static int btn_get_maximum_report_length(struct device *dev,
                                         uint8_t report_type)
{
    struct hid_buttons_info *info = NULL;
    int i = 0, maxlen = 0, id = 0;

    /* check input parameters */
    if (!dev || !dev->private) {
        return -EINVAL;
    }

    info = device_get_private(dev);

    /* lookup the hid_size_info table to find the max report size
     * in specific Report type  */

    for (i = 0; i < info->num_ids; i++) {
        if (info->sinfo[i].size[report_type] > maxlen) {
            id = info->sinfo[i].id;
            maxlen = info->sinfo[i].size[report_type];
        }
    }

    /* If the Report ID isn't zero, add an extra 1-byte in return length for
     * Report ID store.
     */
    if (id) {
        maxlen++;
    }

    return maxlen;
}

/**
 * @brief Get HID Input / Feature report data
 *
 * @param dev Pointer to structure of device data
 * @param report_type HID report type
 * @param report_id HID report id
 * @param data Pointer of input buffer size
 * @param len Report buffer size
 * @return 0 on success, negative errno on error
 */
static int hid_button_get_report(struct device *dev, uint8_t report_type,
                                 uint8_t report_id, uint8_t *data,
                                 uint16_t len)
{
    struct hid_buttons_info *info = NULL;
    int ret = 0;

    /* check input parameters */
    if (!dev || !dev->private || !data) {
        return -EINVAL;
    }

    info = device_get_private(dev);

    sem_wait(&info->lock);

    if (!(info->state & HID_DEVICE_FLAG_OPEN)) {
        /* device isn't opened. */
        ret = -EIO;
        goto err_getreport;
    }

    switch (report_type) {
        case HID_INPUT_REPORT:
            ret = btn_get_input_report(dev, report_id, data, len);
        break;
        default:
            /* only support INPUT report */
            ret = -EINVAL;
        break;
    }

err_getreport:
    sem_post(&info->lock);
    return ret;
}

/**
 * @brief Register callback for HID Event Report
 *
 * @param dev Pointer to structure of device data
 * @param callback Callback function for event notify
 * @return 0 on success, negative errno on error
 */
static int hid_button_register_callback(struct device *dev,
                                        hid_event_callback callback)
{
    struct hid_buttons_info *info = NULL;

    /* check input parameters */
    if (!dev || !dev->private || !callback) {
        return -EINVAL;
    }

    info = device_get_private(dev);

    if (!(info->state & HID_DEVICE_FLAG_OPEN)) {
        /* device isn't opened. */
        return -EIO;
    }

    info->event_callback = callback;

    return 0;
}

/**
 * @brief Remove HID Event Report of callback register
 *
 * @param dev Pointer to structure of device data
 * @return 0 on success, negative errno on error
 */
static int hid_button_unregister_callback(struct device *dev)
{
    struct hid_buttons_info *info = NULL;

    /* check input parameters */
    if (!dev || !dev->private) {
        return -EINVAL;
    }

    info = device_get_private(dev);

    if (!(info->state & HID_DEVICE_FLAG_OPEN)) {
        /* device isn't opened. */
        return -EIO;
    }

    info->event_callback = NULL;
    return 0;
}

/**
 * @brief Routine for all keys debounce check
 *
 * @param dev Pointer to structure of device data
 * @param btn_info Pointer to structure of button_info
 */
static void btn_debounce_check_loop(struct device *dev,
                                    struct button_info *btn_info)
{
    struct hid_buttons_info *info = device_get_private(dev);
    struct hid_kbd_data kbd;
    uint32_t elapsed = 0, cur_time = 0;
    uint8_t value = 0;

    for (;;) {
        /* verify key status is still same during counting */
        value = gpio_get_value(btn_info->gpio);
        if (value != btn_info->last_keystate) {
            btn_info->last_keystate = value;
            break;
        }

        /* count > 250mS routine */
        cur_time = clock_systimer();
        if (cur_time < btn_info->last_activetime) {
            elapsed = cur_time + (0xFFFFFFFF - btn_info->last_activetime) + 1;
            /* Ticks overflow, reset last_activatime to current ticks */
            btn_info->last_activetime = cur_time;
        } else {
            elapsed = cur_time - btn_info->last_activetime;
        }

        if (elapsed > DEBOUNCE_TIMEING) {
            kbd.modifier = info->modifier;
            kbd.keycode = btn_info->last_keystate ? btn_info->Keycode : 0;

            if (info->event_callback) {
                info->event_callback(dev, HID_INPUT_REPORT, (uint8_t*)&kbd,
                                     sizeof(struct hid_kbd_data));
            }
            break;
        } else {
            usleep(COMMAND_INTERVAL);
        }
    }
}

/**
 * @brief Debounce check thread for PAGEUP
 *
 * @param dev Pointer to structure of device data
 */
static void *btn_pgup_bebounce_thread(void *data)
{
    struct device *dev = hid_dev;
    struct button_info *btn_info = (struct button_info *) data;

    if (!btn_info) {
        return NULL;
    }

    while (1) {
        sem_wait(&btn_info->active_debounce);

        if (btn_info->thread_stop) break;

        btn_debounce_check_loop(dev, btn_info);
    }

    return NULL;
}

/**
 * @brief Debounce check thread for PAGEDOWN
 *
 * @param dev Pointer to structure of device data
 */
static void *btn_pgdn_bebounce_thread(void *data)
{
    struct device *dev = hid_dev;
    struct button_info *btn_info = (struct button_info *) data;

    if (!btn_info) {
        return NULL;
    }

    while (1) {
        sem_wait(&btn_info->active_debounce);

        if (btn_info->thread_stop) break;

        btn_debounce_check_loop(dev, btn_info);
    }

    return NULL;
}

/**
 * @brief Specific GPIO deinitialize and release resource
 *
 * @param dev Pointer to structure of device data
 */
void btn_gpio_deinit(struct button_info *btn_info)
{
    if (btn_info->pthread_handler != (pthread_t)0) {
        btn_info->thread_stop = 1;
        sem_post(&btn_info->active_debounce);
        pthread_join(btn_info->pthread_handler, NULL);
    }

    sem_destroy(&btn_info->active_debounce);
    gpio_mask_irq(btn_info->gpio);
    gpio_deactivate(btn_info->gpio);
    list_del(&btn_info->list);
    free(btn_info);
}

/**
 * @brief GPIOs deinitialize and release resources
 *
 * @param dev Pointer to structure of device data
 */
static void hid_button_gpios_deinit(struct device *dev)
{
    struct button_info *btn_info = NULL;

    btn_info = btn_get_info(dev, GPIO_KBDPAGEUP);
    if (btn_info)
        btn_gpio_deinit(btn_info);

    btn_info = btn_get_info(dev, GPIO_KBDPAGEDOWN);
    if (btn_info)
        btn_gpio_deinit(btn_info);

    return;
}

/**
 * @brief Initialze GPIOs
 *
 * @param dev Pointer to structure of device data
 * @param gpio The gpio number for initialize
 */
static int hid_button_gpio_init(struct device *dev, uint16_t gpio)
{
    struct hid_buttons_info *info = device_get_private(dev);
    struct button_info *btn_info = NULL;
    int ret = 0;

    if (gpio != GPIO_KBDPAGEUP && gpio != GPIO_KBDPAGEDOWN) {
        return -EIO;
    }

    btn_info = zalloc(sizeof(*btn_info));
    if (!btn_info)
        return -ENOMEM;

    btn_info->gpio = gpio;

    gpio_activate(gpio);
    gpio_direction_in(gpio);
    gpio_mask_irq(gpio);
    set_gpio_triggering(gpio, IRQ_TYPE_EDGE_BOTH);
    sem_init(&btn_info->active_debounce, 0, 0);
    list_add(&info->buttons_list, &btn_info->list);

    if (gpio == GPIO_KBDPAGEUP) {
        btn_info->Keycode = KEYCODE_PAGEUP;
        ret = pthread_create(&btn_info->pthread_handler, NULL,
                             btn_pgup_bebounce_thread, btn_info);
        if (ret) {
            goto err_gpio_init;
        }
        gpio_irqattach(gpio, hid_handle_kbdup_irq_event);

    } else if (gpio == GPIO_KBDPAGEDOWN) {
        btn_info->Keycode = KEYCODE_PAGEDOWN;
        ret = pthread_create(&btn_info->pthread_handler, NULL,
                             btn_pgdn_bebounce_thread, btn_info);
        if (ret) {
            goto err_gpio_init;
        }
        gpio_irqattach(gpio, hid_handle_kbddn_irq_event);
    } else {
        goto err_gpio_init;
    }

    return ret;

err_gpio_init:
    hid_button_gpios_deinit(dev);
    return ret;
}

/**
 * @brief Open HID device
 *
 * This function is called when the caller is preparing to use this device
 * driver. This function should be called after probe () function and need to
 * check whether the driver already open or not. If driver was opened, it needs
 * to return an error code to the caller to notify the driver was opened.
 *
 * @param dev Pointer to structure of device data
 * @return 0 on success, negative errno on error
 */
static int hid_button_open(struct device *dev)
{
    struct hid_buttons_info *info = NULL;
    int ret = 0;

    /* check input parameter */
    if (!dev || !dev->private) {
        return -EINVAL;
    }

    info = device_get_private(dev);

    sem_wait(&info->lock);

    if (!(info->state & HID_DEVICE_FLAG_PROBE)) {
        ret = -EIO;
        goto err_open;
    }

    if (info->state & HID_DEVICE_FLAG_OPEN) {
        /* device has been opened, return error */
        ret = -EBUSY;
        goto err_open;
    }

    info->event_callback = NULL;

    /* initialize GPIO pin */
    if (GPIO_KBDPAGEUP >= gpio_line_count() ||
        GPIO_KBDPAGEDOWN >= gpio_line_count()) {
        ret = -EIO;
        goto err_open;
    }

    ret = hid_button_gpio_init(dev, GPIO_KBDPAGEUP);
    if (ret) {
        goto err_open;
    }

    ret = hid_button_gpio_init(dev, GPIO_KBDPAGEDOWN);
    if (ret) {
        goto err_open;
    }

    info->state |= HID_DEVICE_FLAG_OPEN;

err_open:
    sem_post(&info->lock);
    return ret;
}

/**
 * @brief Close HID device
 *
 * This function is called when the caller no longer using this driver. It
 * should release or close all resources that allocated by the open() function.
 * This function should be called after the open() function. If the device
 * is not opened yet, this function should return without any operations.
 *
 * @param dev Pointer to structure of device data
 */
static void hid_button_close(struct device *dev)
{
    struct hid_buttons_info *info = NULL;

    /* check input parameter */
    if (!dev || !dev->private) {
        return;
    }

    info = device_get_private(dev);

    sem_wait(&info->lock);

    if (!(info->state & HID_DEVICE_FLAG_OPEN)) {
        /* device isn't opened. */
        goto err_close;
    }

    if (info->state & HID_DEVICE_FLAG_POWERON) {
        hid_button_power_off(dev);
    }

    /* uninitialize GPIO pins */
    hid_button_gpios_deinit(dev);

    info->event_callback = NULL;

    if (info->state & HID_DEVICE_FLAG_OPEN) {
        /* clear open state */
        info->state &= ~HID_DEVICE_FLAG_OPEN;
    }

err_close:
    sem_post(&info->lock);
}

/**
 * @brief Probe HID device
 *
 * This function is called by the system to register the driver when the system
 * boot up. This function allocates memory for the private HID device
 * information, and then setup the hardware resource and interrupt handler.
 *
 * @param dev Pointer to structure of device data
 * @return 0 on success, negative errno on error
 */
static int hid_button_probe(struct device *dev)
{
    struct hid_buttons_info *info = NULL;

    if (!dev) {
        return -EINVAL;
    }

    info = zalloc(sizeof(*info));
    if (!info) {
        return -ENOMEM;
    }

    hid_dev = dev;
    info->dev = dev;
    info->state = HID_DEVICE_FLAG_PROBE;
    dev->private = info;

    info->hdesc = &hid_dev_desc;
    info->rdesc = hid_report_desc;
    info->sinfo = hid_sizeinfo;
    info->num_ids = ARRAY_SIZE(hid_sizeinfo);

    info->buttons_list.prev = &info->buttons_list;
    info->buttons_list.next = &info->buttons_list;

    sem_init(&info->lock, 0, 1);

    return 0;
}

/**
 * @brief Remove HID device
 *
 * This function is called by the system to unregister the driver. It should
 * release the hardware resource and interrupt setting, and then free memory
 * that allocated by the probe() function.
 * This function should be called after probe() function. If driver was opened,
 * this function should call close() function before releasing resources.
 *
 * @param dev Pointer to structure of device data
 */
static void hid_button_remove(struct device *dev)
{
    struct hid_buttons_info *info = NULL;

    /* check input parameter */
    if (!dev || !dev->private) {
        return;
    }

    info = device_get_private(dev);

    if (info->state & HID_DEVICE_FLAG_OPEN) {
        hid_button_close(dev);
    }

    info->hdesc = NULL;
    info->rdesc = NULL;
    info->sinfo = NULL;
    info->num_ids = 0;
    info->state = 0;
    sem_destroy(&info->lock);

    dev->private = NULL;
    hid_dev = NULL;
    free(info);
}

static struct device_hid_type_ops hid_button_type_ops = {
    .power_on = hid_button_power_on,
    .power_off = hid_button_power_off,
    .get_descriptor = hid_button_get_desc,
    .get_report_descriptor = hid_button_get_report_desc,
    .get_report_length = btn_get_report_length,
    .get_maximum_report_length = btn_get_maximum_report_length,
    .get_report = hid_button_get_report,
    .register_callback = hid_button_register_callback,
    .unregister_callback = hid_button_unregister_callback,
};

static struct device_driver_ops hid_button_driver_ops = {
    .probe          = hid_button_probe,
    .remove         = hid_button_remove,
    .open           = hid_button_open,
    .close          = hid_button_close,
    .type_ops       = &hid_button_type_ops,
};

struct device_driver hid_button_driver = {
    .type           = DEVICE_TYPE_HID_HW,
    .name           = "hid_Button",
    .desc           = "Button HID Driver",
    .ops            = &hid_button_driver_ops,
};
