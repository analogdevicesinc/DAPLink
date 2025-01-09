/**
 * @file    IO_Config.h
 * @brief
 *
 * DAPLink Interface Firmware
 * Copyright (c) 2009-2016, ARM Limited, All Rights Reserved
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may
 * not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __IO_CONFIG_H__
#define __IO_CONFIG_H__

#include "max32690.h"

// Non-Forwarded Reset In Pin
#define PIN_RESET_IN_NO_FWRD_PORT  2
#define PIN_RESET_IN_NO_FWRD_PIN   7

// nRESET
#define PIN_nRESET_PORT     2
#define PIN_nRESET_PIN      29

// SWCLK
#define PIN_SWCLK_PORT      2
#define PIN_SWCLK_PIN       25

// SWDIO
#define PIN_SWDIO_PORT      2
#define PIN_SWDIO_PIN       8

#define MXC_GPIO_SETMODE(pt, pn, m) \
    { \
      if (m == MXC_GPIO_FUNC_IN) { \
        MXC_GPIO_GET_GPIO(pt)->outen_clr = pn; \
        MXC_GPIO_GET_GPIO(pt)->en0_set = pn; \
        MXC_GPIO_GET_GPIO(pt)->en1_clr = pn; \
        MXC_GPIO_GET_GPIO(pt)->en2_clr = pn; \
      } else { \
        MXC_GPIO_GET_GPIO(pt)->outen_set = pn; \
        MXC_GPIO_GET_GPIO(pt)->en0_set = pn; \
        MXC_GPIO_GET_GPIO(pt)->en1_clr = pn; \
        MXC_GPIO_GET_GPIO(pt)->en2_clr = pn; \
      } \
    }
#define MXC_GPIO_SETBIT(pt, pn)     (MXC_SETBIT(&(MXC_GPIO_GET_GPIO(pt)->out_set), pn))
#define MXC_GPIO_CLRBIT(pt, pn)     (MXC_SETBIT(&(MXC_GPIO_GET_GPIO(pt)->out_clr), pn))
#define MXC_GPIO_GETBIT(pt, pn)     (MXC_GETBIT(&(MXC_GPIO_GET_GPIO(pt)->in), pn))

#endif
