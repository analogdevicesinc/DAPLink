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

#include <string.h>
#include "max32690.h"
#include "IO_Config.h"
#include "gpio_regs.h"
#include "uart_regs.h"
#include "uart.h"
#include "uart_mxc.h"
#include "circ_buf.h"

// Size must be 2^n
#define BUFFER_SIZE (4096)
#define DEFAULT_BAUD_RATE (115200)

// Track bit rate to avoid calculation from bus clock, clock scaler and baud divisor values
static uint32_t baudrate;

static mxc_uart_regs_t *CdcAcmUart = MXC_UART2;

circ_buf_t write_buffer;
uint8_t write_buffer_data[BUFFER_SIZE];
circ_buf_t read_buffer;
uint8_t read_buffer_data[BUFFER_SIZE];

/******************************************************************************/


/******************************************************************************/
int32_t uart_initialize(void)
{
    int idx;

    MXC_UART_Init(CdcAcmUart, DEFAULT_BAUD_RATE, MXC_UART_IBRO_CLK);
    MXC_UART_EnableInt(CdcAcmUart, MXC_F_UART_INT_EN_RX_OV | MXC_F_UART_INT_EN_RX_THD | MXC_F_UART_INT_EN_TX_HE);
    NVIC_EnableIRQ(UART2_IRQn);

    circ_buf_init(&write_buffer, write_buffer_data, sizeof(write_buffer_data));
    circ_buf_init(&read_buffer, read_buffer_data, sizeof(read_buffer_data));

    return 1;
}

/******************************************************************************/
int32_t uart_uninitialize(void)
{

    MXC_UART_Shutdown(CdcAcmUart);
    // Clear buffers
    memset(&write_buffer_data, 0, sizeof(write_buffer_data));
    memset(&read_buffer_data, 0, sizeof(read_buffer_data));

    return 1;
}

/******************************************************************************/
void uart_set_control_line_state(uint16_t ctrl_bmp)
{
}

/******************************************************************************/
int32_t uart_reset(void)
{
    circ_buf_init(&write_buffer, write_buffer_data, sizeof(write_buffer_data));
    circ_buf_init(&read_buffer, read_buffer_data, sizeof(read_buffer_data));

    return 1;
}

/******************************************************************************/
int32_t uart_set_configuration(UART_Configuration *config)
{
    uint32_t ctrl = 0;

    switch (config->Parity) {
        case UART_PARITY_NONE: ctrl = MXC_UART_PARITY_DISABLE;  break;
        case UART_PARITY_ODD: ctrl = MXC_UART_PARITY_ODD_0;     break;
        case UART_PARITY_EVEN: ctrl = MXC_UART_PARITY_EVEN_0;   break;
        default:
            break;
    }

    MXC_UART_SetParity(CdcAcmUart, (mxc_uart_parity_t)ctrl);

    if (config->DataBits > 4 && config->DataBits < 9)
        MXC_UART_SetDataSize(CdcAcmUart, config->DataBits);

    MXC_UART_SetStopBits(CdcAcmUart, (mxc_uart_stop_t)config->StopBits);

    MXC_UART_SetFrequency(CdcAcmUart, config->Baudrate, MXC_UART_IBRO_CLK);

    return 1;
}

/******************************************************************************/
int32_t uart_get_configuration(UART_Configuration *config)
{
    return 1;
}

/******************************************************************************/
int32_t uart_write_free(void)
{
    return circ_buf_count_free(&write_buffer);
}

/******************************************************************************/
int32_t uart_write_data(uint8_t *data, uint16_t size)
{
    uint16_t xfer_count = size;
    int len = (int)size;

    if (circ_buf_count_used(&write_buffer) == 0) {
        MXC_UART_Write(CdcAcmUart, data, &len);
    }

    if (len != (int)size) {
        xfer_count = circ_buf_write(&write_buffer, data + len, (int)size - len);
    }

    return size - xfer_count;
}

/******************************************************************************/
int32_t uart_read_data(uint8_t *data, uint16_t size)
{
    return circ_buf_read(&read_buffer, data, size);
}

/******************************************************************************/
void UART_IRQHandler(void)
{
   // Capture interrupt flag state at entry
    uint32_t intfl = MXC_UART_GetFlags(CdcAcmUart);
    // Clear interrupts that will be serviced
    MXC_UART_ClearFlags(CdcAcmUart, intfl);

    if (intfl & MXC_F_UART_INT_FL_RX_OV) {
        // Flush RX FIFO, prepare for new characters
        MXC_UART_ClearRXFIFO(CdcAcmUart);
    }

    while (MXC_UART_GetRXFIFOAvailable(CdcAcmUart) &&
            circ_buf_count_free(&read_buffer)) {
        circ_buf_push(&read_buffer, CdcAcmUart->fifo);
    }

    while (circ_buf_count_used(&write_buffer) &&
            MXC_UART_GetTXFIFOAvailable(CdcAcmUart)) {
        CdcAcmUart->fifo = circ_buf_pop(&write_buffer);
    }
}

/******************************************************************************/
void UART0_IRQHandler(void)
{
    UART_IRQHandler();
}

/******************************************************************************/
void UART1_IRQHandler(void)
{
    UART_IRQHandler();
}

/******************************************************************************/
void UART2_IRQHandler(void)
{
    UART_IRQHandler();
}
