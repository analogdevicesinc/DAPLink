/* CMSIS-DAP Interface Firmware
 * Copyright (c) 2009-2013 ARM Limited
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "max32690.h"
#include "gpio_regs.h"
#include "IO_Config.h"
#include "gpio.h"
#include "gpio_mxc.h"

// Bitband pointers
volatile uint32_t *tck_in;
volatile uint32_t *tck_out_set;
volatile uint32_t *tck_out_clr;
volatile uint32_t *tms_in;
volatile uint32_t *tms_out_set;
volatile uint32_t *tms_out_clr;
volatile uint32_t *rst_in;
volatile uint32_t *rst_out_set;
volatile uint32_t *rst_out_clr;

uint32_t swdio_port;
uint32_t swdio_pin;
uint32_t swclk_port;
uint32_t swclk_pin;
uint32_t nreset_port;
uint32_t nreset_pin;

int32_t uart_set_instance(uint32_t inst);

const mxc_gpio_cfg_t led_pin = { MXC_GPIO0, MXC_GPIO_PIN_14, MXC_GPIO_FUNC_OUT, MXC_GPIO_PAD_NONE, MXC_GPIO_VSSEL_VDDIO, MXC_GPIO_DRVSTR_0 };
const mxc_gpio_cfg_t led2_pin = { MXC_GPIO2, MXC_GPIO_PIN_12, MXC_GPIO_FUNC_OUT, MXC_GPIO_PAD_NONE, MXC_GPIO_VSSEL_VDDIO, MXC_GPIO_DRVSTR_0 };

/******************************************************************************/
void gpio_init(void)
{
    swdio_port = PIN_SWDIO_PORT;
    swdio_pin = PIN_SWDIO_PIN;
    swclk_port = PIN_SWCLK_PORT;
    swclk_pin = PIN_SWCLK_PIN;
    nreset_port = PIN_nRESET_PORT;
    nreset_pin = PIN_nRESET_PIN;
    MXC_GPIO_Config(&led_pin);
    MXC_GPIO_Config(&led2_pin);
}

/******************************************************************************/
void gpio_set_hid_led(gpio_led_state_t state)
{
    if (state == GPIO_LED_ON) {
        MXC_GPIO_OutClr(led_pin.port, led_pin.mask);
    } else {
        MXC_GPIO_OutSet(led_pin.port, led_pin.mask);
    }
}//

/******************************************************************************/
void gpio_set_msc_led(gpio_led_state_t state)
{

}

/******************************************************************************/
void gpio_set_cdc_led(gpio_led_state_t state)
{
    if (state == GPIO_LED_ON) {
        MXC_GPIO_OutClr(led2_pin.port, led2_pin.mask);
    } else {
        MXC_GPIO_OutSet(led2_pin.port, led2_pin.mask);
    }
}//

/******************************************************************************/
uint8_t gpio_get_reset_btn_no_fwrd(void)
{
    return 0;
}

/******************************************************************************/
uint8_t gpio_get_reset_btn_fwrd(void)
{
    return 0;
}

/******************************************************************************/
void gpio_set_board_power(bool powerEnabled)
{
}
