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

#include "FlashOS.h"
#include "max32690.h"
#include "flc_regs.h"
#include "flc.h"
#include "nvic_table.h"

volatile uint32_t isr_cnt;
volatile uint32_t isr_flags;

/******************************************************************************/
void FLC0_IRQHandler(void)
{
    uint32_t temp;
    isr_cnt++;
    temp = MXC_FLC0->intr;

    if (temp & MXC_F_FLC_INTR_DONE) {
        MXC_FLC0->intr &= ~MXC_F_FLC_INTR_DONE;
    }

    if (temp & MXC_F_FLC_INTR_AF) {
        MXC_FLC0->intr &= ~MXC_F_FLC_INTR_AF;
    }

    isr_flags = temp;
}

/******************************************************************************/
uint32_t Init(uint32_t adr, uint32_t clk, uint32_t fnc)
{
    if (MXC_FLC_Busy()) {
        return 1;
    }

    MXC_FLC_Init();

    MXC_NVIC_SetVector(FLC0_IRQn, FLC0_IRQHandler); // Assign ISR
    NVIC_EnableIRQ(FLC0_IRQn); // Enable interrupt

    __enable_irq();

    // Clear and enable flash programming interrupts
    MXC_FLC_EnableInt(MXC_F_FLC_INTR_DONEIE | MXC_F_FLC_INTR_AFIE);
    isr_flags = 0;
    isr_cnt = 0;

    return 0;
}

/******************************************************************************/
uint32_t UnInit(uint32_t fnc)
{
    return  0;  // Finished without Errors
}

/******************************************************************************/
/*
 *  Erase complete Flash Memory
 *    Return Value:   0 - OK,  1 - Failed
 */
int EraseChip(void)
{
    while(MXC_FLC_Busy());
    
    MXC_FLC_MassErase();
    
    while(MXC_FLC_Busy());

    return 0;
}

/******************************************************************************/
/*
 *  Erase Sector in Flash Memory
 *    Parameter:      address:  Sector Address
 *    Return Value:   0 - OK,  1 - Failed
 */
int EraseSector(unsigned long address)
{
    while(MXC_FLC_Busy());
    
    MXC_FLC_PageErase(address);
    
    while(MXC_FLC_Busy());

    return 0;
}

/******************************************************************************/
/*
 *  Program Page in Flash Memory
 *    Parameter:      address:  Page Start Address
 *                    size:     Page Size
 *                    buffer:   Page Data
 *    Return Value:   0 - OK,  1 - Failed
 */
int ProgramPage(unsigned long address, unsigned long size, unsigned char *buffer8)
{
    while(MXC_FLC_Busy());

    MXC_FLC_Write(address, size, (uint32_t*)buffer8);
    return  0;
}
