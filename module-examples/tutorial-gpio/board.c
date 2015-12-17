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
 *
 * Author: Joel Porquet <joel@porquet.org>
 */

#include <syslog.h>

#include <tsb_scm.h>

void ara_module_early_init(void)
{
}

void ara_module_init(void)
{
    lowsyslog("GPIO Tutorial Module init\n");

    /*
     * Configure shared I2S_MCLK/DBG_TRCLK/GPIO18 pin as a GPIO pin:
     *  => PinShare[PIN_ETM] = 0 and PinShare[PIN_GPIO18] = 1
     *      (PIN_ETM = 4, PIN_GPIO18 = 11) */

    /* take ownership of the pinsharing bits (PIN_ETM and PIN_GPIO18) */
    if (tsb_request_pinshare(TSB_PIN_ETM | TSB_PIN_GPIO18)) {
        lowsyslog("Cannot get ownership for GPIO18 pin\n");
        return;
    }

    /* set the pinsharing bits for configuring GPIO18 */
    tsb_clr_pinshare(TSB_PIN_ETM);
    tsb_set_pinshare(TSB_PIN_GPIO18);
}

