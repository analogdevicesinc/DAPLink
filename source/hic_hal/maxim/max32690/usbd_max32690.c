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
#include "rl_usb.h"
#include "util.h"

#include "max32690.h"
#include "usbhs_regs.h"
#include "tmr_regs.h"
#include "fcr_regs.h"
#include "usb_mxc.h"
#include "mxc_sys.h"
#include "mcr_regs.h"
#include "mxc_delay.h"
#include "usb_event.h"
#include "tmr.h"

#define __NO_USB_LIB_C
#include "usb_config.c"

#define CONT_TIMER MXC_TMR0 // Can be MXC_TMR0 through MXC_TMR5

#define EPNUM_MASK  (~USB_ENDPOINT_DIRECTION_MASK)

static volatile int setup_waiting;
static volatile int ep0_expect_zlp;
static volatile int suspended;
static MXC_USB_Req_t out_requests[MXC_USBHS_NUM_EP];
static MXC_USB_Req_t in_requests[MXC_USBHS_NUM_EP];
static uint8_t out_data[MXC_USBHS_NUM_EP][64];

/******************************************************************************/

static void reset_state(void)
{

    suspended = 0;
    setup_waiting = 0;
    ep0_expect_zlp = 0;

}

void delay_us(unsigned int usec)
{
    /* mxc_delay() takes unsigned long, so can't use it directly */
    MXC_Delay(usec);
}

/******************************************************************************/
static int usbStartupCallback(void)
{
    MXC_SYS_ClockSourceEnable(MXC_SYS_CLOCK_IPO);
    MXC_MCR->ldoctrl |= MXC_F_MCR_LDOCTRL_0P9EN;
    MXC_SYS_ClockEnable(MXC_SYS_PERIPH_CLOCK_USB);
    MXC_SYS_Reset_Periph(MXC_SYS_RESET0_USB);

    return E_NO_ERROR;
}

/******************************************************************************/
static int usbShutdownCallback(void)
{
    MXC_SYS_ClockDisable(MXC_SYS_PERIPH_CLOCK_USB);

    return E_NO_ERROR;
}

/******************************************************************************/
static int eventCallback(maxusb_event_t evt, void *data)
{

    switch (evt) {
    case MAXUSB_EVENT_NOVBUS:
        MXC_USB_EventDisable(MAXUSB_EVENT_BRST);
        MXC_USB_EventDisable(MAXUSB_EVENT_SUSP);
        //MXC_USB_EventDisable(MAXUSB_EVENT_BACT);
        MXC_USB_EventDisable(MAXUSB_EVENT_SUDAV);
#ifdef __RTX
        if (USBD_RTX_DevTask) {
            isr_evt_set(USBD_EVT_POWER_OFF, USBD_RTX_DevTask);
        }
#else
        if (USBD_P_Power_Event) {
            USBD_P_Power_Event(0);
        }
#endif
        break;

    case MAXUSB_EVENT_VBUS:
        MXC_USB_EventClear(MAXUSB_EVENT_BRST);
        MXC_USB_EventEnable(MAXUSB_EVENT_BRST, eventCallback, NULL);
        MXC_USB_EventClear(MAXUSB_EVENT_SUSP);
        MXC_USB_EventEnable(MAXUSB_EVENT_SUSP, eventCallback, NULL);
        MXC_USB_EventClear(MAXUSB_EVENT_SUDAV);
        MXC_USB_EventEnable(MAXUSB_EVENT_SUDAV, eventCallback, NULL);
        //MXC_USB_EventEnable(MAXUSB_EVENT_BACT, eventCallback, NULL);
#ifdef __RTX
        if (USBD_RTX_DevTask) {
            isr_evt_set(USBD_EVT_POWER_ON,  USBD_RTX_DevTask);
        }
#else
        if (USBD_P_Power_Event) {
            USBD_P_Power_Event(1);
        }
#endif
        break;

    case MAXUSB_EVENT_BRSTDN:
    case MAXUSB_EVENT_BRST:
        if (suspended) {
            suspended = 0;
#ifdef __RTX
            if (USBD_RTX_DevTask) {
                isr_evt_set(USBD_EVT_RESUME, USBD_RTX_DevTask);
            }
#else
            if (USBD_P_Resume_Event) {
                USBD_P_Resume_Event();
            }
#endif
        }
        
        reset_state();
        usbd_reset_core();

#ifdef __RTX
        if (USBD_RTX_DevTask) {
            isr_evt_set(USBD_EVT_RESET, USBD_RTX_DevTask);
        }
#else
        if (USBD_P_Reset_Event) {
            USBD_P_Reset_Event();
        }
#endif
        break;

    case MAXUSB_EVENT_SUSP:
        suspended = 1;
#ifdef __RTX
        if (USBD_RTX_DevTask) {
            isr_evt_set(USBD_EVT_SUSPEND, USBD_RTX_DevTask);
        }
#else
        if (USBD_P_Suspend_Event) {
            USBD_P_Suspend_Event();
        }
#endif
        break;

    case MAXUSB_EVENT_BACT:
        if (usbd_configured()) {
            //USBD_CDC_ACM_SOF_Event();
        }
        break;

    case MAXUSB_EVENT_SUDAV:
        setup_waiting = 1;
        if (USBD_P_EP[0]) {
            USBD_P_EP[0](USBD_EVT_SETUP);
        }
        break;

    default:
        break;
    }

    return 0;
}

/*
 *  USB Device Initialize Function
 *   Called by the User to initialize USB Device
 *    Return Value:    None
 */
void USBD_Init (void)
{
    maxusb_cfg_options_t usb_opts;
    
    /* Start out in full speed */
    usb_opts.enable_hs = 0; /* 0:Full Speed     1:High Speed */
    usb_opts.delay_us = delay_us; /* Function which will be used for delays */
    usb_opts.init_callback = usbStartupCallback;
    usb_opts.shutdown_callback = usbShutdownCallback;

    memset(out_requests, 0, sizeof(MXC_USB_Req_t) * MXC_USBHS_NUM_EP);

    ep0_expect_zlp = 0;
    setup_waiting = 0;
    suspended = 0;

    /* Initialize the usb module */
    if (MXC_USB_Init(&usb_opts) != 0) {
        while (1) {}
    }

    MXC_USB_EventEnable(MAXUSB_EVENT_NOVBUS, eventCallback, NULL);
    MXC_USB_EventEnable(MAXUSB_EVENT_VBUS, eventCallback, NULL);

    NVIC_EnableIRQ(USB_IRQn);
}

/*
 *  USB Device Connect Function
 *   Called by the User to Connect/Disconnect USB Device
 *    Parameters:      con:   Connect/Disconnect
 *    Return Value:    None
 */
void USBD_Connect (BOOL con)
{
    if (con) {
        MXC_USB_Connect();
    } else {
        MXC_USB_Disconnect();
    }
}

/*
 *  USB Device Remote Wakeup Configuration Function
 *    Parameters:      cfg:   Device Enable/Disable
 *    Return Value:    None
 */
void USBD_WakeUpCfg (BOOL cfg)
{
}

/*
 *  USB Device Set Address Function
 *    Parameters:      adr:   USB Device Address
 *    Return Value:    None
 */
void USBD_SetAddress (U32 adr, U32 setup)
{
    MXC_USB_SetFuncAddr(adr);
}

void TMR0_IRQHandler(void)
{
    MXC_TMR_ClearFlags(CONT_TIMER);
    if (usbd_configured()) {
        USBD_CDC_ACM_SOF_Event();
    }
}

/*
 *  USB Device Configure Function
 *    Parameters:      cfg:   Device Configure/Deconfigure
 *    Return Value:    None
 */
void USBD_Configure (BOOL cfg)
{
    #define SOF_INT_US  1000
    if (cfg) {
        mxc_tmr_cfg_t tmr;
        uint32_t periodTicks = MXC_TMR_GetPeriod(CONT_TIMER, MXC_TMR_APB_CLK, 32, SOF_INT_US);
        /*
        Steps for configuring a timer for PWM mode:
        1. Disable the timer
        2. Set the prescale value
        3  Configure the timer for continuous mode
        4. Set polarity, timer parameters
        5. Enable Timer
        */
        MXC_TMR_Shutdown(CONT_TIMER);
        tmr.pres = TMR_PRES_32;
        tmr.mode = TMR_MODE_CONTINUOUS;
        tmr.bitMode = TMR_BIT_MODE_32;
        tmr.clock = MXC_TMR_APB_CLK;
        tmr.cmp_cnt = periodTicks; //SystemCoreClock*(1/interval_time);
        tmr.pol = 0;
        if (MXC_TMR_Init(CONT_TIMER, &tmr, 0) != E_NO_ERROR) {
            return;
        }

        MXC_TMR_EnableInt(CONT_TIMER);
        NVIC_EnableIRQ(TMR0_IRQn);
        MXC_TMR_Start(CONT_TIMER);
    } else {
        // Disable tmr
        MXC_TMR_Stop(CONT_TIMER);
    }
}

/*
 *  Configure USB Device Endpoint according to Descriptor
 *    Parameters:      pEPD:  Pointer to Device Endpoint Descriptor
 *    Return Value:    None
 */
void USBD_ConfigEP (USB_ENDPOINT_DESCRIPTOR *pEPD)
{
    maxusb_ep_type_t type;

    if (pEPD->bEndpointAddress & USB_ENDPOINT_DIRECTION_MASK) {
        type = MAXUSB_EP_TYPE_IN;
    } else {
        type = MAXUSB_EP_TYPE_OUT;
    }

    MXC_USB_ConfigEp(pEPD->bEndpointAddress & EPNUM_MASK, type, pEPD->wMaxPacketSize);
}

/*
 *  Set Direction for USB Device Control Endpoint
 *    Parameters:      dir:   Out (dir == 0), In (dir <> 0)
 *    Return Value:    None
 */
void USBD_DirCtrlEP (U32 dir)
{
    /* Not needed */
}

static void read_callback(void *cbdata)
{
    MXC_USB_Req_t *req = (MXC_USB_Req_t*)cbdata;
    
    if (USBD_P_EP[req->ep] && (req->error_code == 0)) {
        USBD_P_EP[req->ep](USBD_EVT_OUT);
    }

    if (!req->ep || (req->error_code == -1)) {
        return;
    }

    if (req->error_code == 0xff) {
        MXC_USB_Ackstat(0);
    }

    req->data = out_data[req->ep];
    req->callback = read_callback;
    req->cbdata = &out_requests[req->ep];
    req->reqlen = 64;
    req->actlen = 0;
    req->error_code = 0;
    req->type = MAXUSB_TYPE_PKT;

    MXC_USB_ReadEndpoint(req);
}

/*
 *  Enable USB Device Endpoint
 *    Parameters:      EPNum: Device Endpoint Number
 *                       EPNum.0..3: Address
 *                       EPNum.7:    Dir
 *    Return Value:    None
 */
void USBD_EnableEP (U32 EPNum)
{
    if (EPNum & USB_ENDPOINT_DIRECTION_MASK) {
        return;
    }

    if (!EPNum) {
        return;
    }

    MXC_USB_Req_t *req = &out_requests[EPNum];
    
    req->ep         = EPNum;
    req->data       = out_data[EPNum];
    req->callback   = read_callback;
    req->cbdata     = req;
    req->reqlen     = 64;
    req->actlen     = 0;
    req->error_code = 0;
    req->type       = MAXUSB_TYPE_PKT;

    MXC_USB_ReadEndpoint(req);

}

/*
 *  Disable USB Device Endpoint
 *    Parameters:      EPNum: Device Endpoint Number
 *                       EPNum.0..3: Address
 *                       EPNum.7:    Dir
 *    Return Value:    None
 */
void USBD_DisableEP (U32 EPNum)
{

}

/*
 *  Reset USB Device Endpoint
 *    Parameters:      EPNum: Device Endpoint Number
 *                       EPNum.0..3: Address
 *                       EPNum.7:    Dir
 *    Return Value:    None
 */
void USBD_ResetEP (U32 EPNum)
{
}

/*
 *  Set Stall for USB Device Endpoint
 *    Parameters:      EPNum: Device Endpoint Number
 *                       EPNum.0..3: Address
 *                       EPNum.7:    Dir
 *    Return Value:    None
 */
void USBD_SetStallEP (U32 EPNum)
{
    MXC_USB_Stall(EPNum & EPNUM_MASK);
}

/*
 *  Clear Stall for USB Device Endpoint
 *    Parameters:      EPNum: Device Endpoint Number
 *                       EPNum.0..3: Address
 *                       EPNum.7:    Dir
 *    Return Value:    None
 */
void USBD_ClrStallEP (U32 EPNum)
{
    MXC_USB_Unstall(EPNum & EPNUM_MASK);
    USBD_EnableEP(EPNum);
}

/*
 *  Read USB Device Endpoint Data
 *    Parameters:      EPNum: Device Endpoint Number
 *                       EPNum.0..3: Address
 *                       EPNum.7:    Dir
 *                     pData: Pointer to Data Buffer
 *    Return Value:    Number of bytes read
 */
U32 USBD_ReadEP (U32 EPNum, U8 *pData, U32 size)
{
    EPNum &= EPNUM_MASK;

    if (EPNum == 0 && setup_waiting) {
        USB_SETUP_PACKET *sup;
        setup_waiting = 0;
        MXC_USB_GetSetup((MXC_USB_SetupPkt *)pData);

        sup = (USB_SETUP_PACKET*)pData;

        if ( (sup->bmRequestType.Dir == REQUEST_HOST_TO_DEVICE) && (sup->wLength > 0) ) {
            // There is an OUT stage for this setup packet. Register a request.
            MXC_USB_Req_t *req = &out_requests[EPNum];
    
            req->ep         = EPNum;
            req->data       = out_data[EPNum];
            req->callback   = read_callback;
            req->cbdata     = req;
            req->reqlen     = sup->wLength;
            req->actlen     = 0;
            req->error_code = 0;
            req->type       = MAXUSB_TYPE_TRANS;

            MXC_USB_ReadEndpoint(req);
        }
    } else {
        if (out_requests[EPNum].actlen > 0) {
            if (out_requests[EPNum].actlen > size)
                out_requests[EPNum].actlen = size;
            memcpy(pData, out_data[EPNum], out_requests[EPNum].actlen);
        }
        size = out_requests[EPNum].actlen;
        out_requests[EPNum].actlen = 0;
    }

    return size;
}


static void write_callback(void *cbdata)
{
    MXC_USB_Req_t *req = (MXC_USB_Req_t*)cbdata;
    
    if (USBD_P_EP[req->ep] && (req->error_code == 0)) {
        USBD_P_EP[req->ep](USBD_EVT_IN);
    }

    if (!MXC_USB_GetRequest(req->ep)) {
        if (!req->ep) {
            MXC_USB_SetSetupPhase(SETUP_IDLE);
            MXC_USBHS->csr0 |= MXC_F_USBHS_CSR0_INPKTRDY | MXC_F_USBHS_CSR0_DATA_END;
        }
        if (req->error_code == 0) {
            MXC_USB_Ackstat(0);
        } else {
            MXC_USB_Stall(req->ep);
        }
    }
}

/*
 *  Write USB Device Endpoint Data
 *    Parameters:      EPNum: Endpoint Number
 *                       EPNum.0..3: Address
 *                       EPNum.7:    Dir
 *                     pData: Pointer to Data Buffer
 *                     cnt:   Number of bytes to write
 *    Return Value:    Number of bytes written
 */

U32 USBD_WriteEP (U32 EPNum, U8 *pData, U32 cnt)
{
    MXC_USB_Req_t *req;
    EPNum &= EPNUM_MASK;

    if (EPNum == 0) {
        if ((cnt == 0) && !ep0_expect_zlp) {
            MXC_USB_Ackstat(0);
            return 0;
        } else if (cnt == USBD_MAX_PACKET0) {
            ep0_expect_zlp = 1;
        } else {
            ep0_expect_zlp = 0;
        }
    }

    req = &in_requests[EPNum];

    req->ep = EPNum;
    req->data = pData;
    req->reqlen = cnt;
    req->cbdata = req;
    req->callback = write_callback;
    req->actlen = 0;
    req->error_code = 0;
    
    if (MXC_USB_WriteEPPkg(req) != 0) {
        cnt = 0;
    }

    return cnt;
}

/*
 *  USB Device Interrupt Service Routine
 */
void USB_IRQHandler (void)
{
    NVIC_DisableIRQ(USB_IRQn);
    USBD_SignalHandler();
}


void USBD_Handler(void)
{
    MXC_USB_EventHandler();
    
    NVIC_EnableIRQ(USB_IRQn);
}
