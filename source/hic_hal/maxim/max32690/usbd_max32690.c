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

#define __NO_USB_LIB_C
#include "usb_config.c"


#define EPNUM_MASK  (~USB_ENDPOINT_DIRECTION_MASK)

volatile int configured;
static volatile int setup_waiting;
volatile int suspended;
static MXC_USB_Req_t out_requests[MXC_USBHS_NUM_EP];
static uint8_t out_data[MXC_USBHS_NUM_EP][64];

/******************************************************************************/

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
        MXC_USB_EventDisable(MAXUSB_EVENT_DPACT);
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
        MXC_USB_EventEnable(MAXUSB_EVENT_SUDAV, eventCallback, NULL);
        MXC_USB_EventEnable(MAXUSB_EVENT_DPACT, eventCallback, NULL);
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

    case MAXUSB_EVENT_DPACT:
        if (usbd_configured()) {
            USBD_CDC_ACM_SOF_Event();
        }
        break;
    
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

/*
 *  USB Device Configure Function
 *    Parameters:      cfg:   Device Configure/Deconfigure
 *    Return Value:    None
 */
void USBD_Configure (BOOL cfg)
{

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
    
    EPNum &= EPNUM_MASK;

    out_requests[EPNum].ep = EPNum;
    out_requests[EPNum].data = out_data[EPNum];
    out_requests[EPNum].callback = read_callback;
    out_requests[EPNum].cbdata = &out_requests[EPNum];
    out_requests[EPNum].reqlen = 64;
    out_requests[EPNum].actlen = 0;
    out_requests[EPNum].type = MAXUSB_TYPE_PKT;

    MXC_USB_ReadEndpoint(&out_requests[EPNum]);

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
    if (EPNum & USB_ENDPOINT_DIRECTION_MASK) {
        return;
    }

    EPNum &= EPNUM_MASK;

    MXC_USBHS->introuten &= ~(1 << EPNum);
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
        setup_waiting = 0;
        MXC_USB_GetSetup((MXC_USB_SetupPkt *)pData);
    } else {
        if (out_requests[EPNum].actlen > 0) {
            if (out_requests[EPNum].actlen > size)
                out_requests[EPNum].actlen = size;
            memcpy(pData, out_data[EPNum], out_requests[EPNum].actlen);
        }
        size = out_requests[EPNum].actlen;
    }

    return size;
}


static void write_callback(void *cbdata)
{
    MXC_USB_Req_t *req = (MXC_USB_Req_t*)cbdata;
    
    if (USBD_P_EP[req->ep]) {
        USBD_P_EP[req->ep](USBD_EVT_IN);
    }

    if (!MXC_USB_GetRequest(req->ep)) {
        if (req->error_code == 0) {
            if (!req->ep) {
                MXC_USB_SetSetupPhase(SETUP_IDLE);
                MXC_USBHS->csr0 |= MXC_F_USBHS_CSR0_INPKTRDY | MXC_F_USBHS_CSR0_DATA_END;
            }
            MXC_USB_Ackstat(0);
        } else {
            MXC_USB_Stall(req->ep);
        }
    }
}

static MXC_USB_Req_t write_req;

static const MXC_USB_Req_t write_req_init = {
  0,                       /* ep */
  NULL,                    /* data */
  0,                       /* reqlen */
  0,                       /* actlen */
  0,                       /* error_code */
  write_callback,          /* callback */
  &write_req               /* callback data */
};

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
    EPNum &= EPNUM_MASK;

    if (pData == NULL && cnt == 0) {
        MXC_USB_Ackstat(0);
        return 0;
    }

    memcpy(&write_req, &write_req_init, sizeof(MXC_USB_Req_t));

    write_req.ep = EPNum;
    write_req.data = pData;
    write_req.reqlen = cnt;
    
    if (MXC_USB_WriteEPPkg(&write_req) != 0) {
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
