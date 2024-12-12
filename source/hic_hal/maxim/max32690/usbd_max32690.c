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
#include "tmr.h"

#define __NO_USB_LIB_C
#include "usb_config.c"

#define CONT_TIMER MXC_TMR0 // Can be MXC_TMR0 through MXC_TMR5

#define EPNUM_MASK  (~USB_ENDPOINT_DIRECTION_MASK)

volatile int configured;
static volatile int setup_waiting;
volatile int suspended;
static volatile int usb_read_complete;
static volatile int ep0_expect_zlp;
static volatile int expect_out_stage[MXC_USBHS_NUM_EP];
static volatile int expect_in_stage[MXC_USBHS_NUM_EP];
static MXC_USB_Req_t setup_req;

static void status_stage_callback(void *cbdata);

static const MXC_USB_Req_t setup_req_init = {
  0,                       /* ep */
  NULL,                    /* data */
  0,                       /* reqlen */
  0,                       /* actlen */
  0,                       /* error_code */
  status_stage_callback,   /* callback */
  &setup_req                /* callback data */
};

static void status_stage_callback(void *cbdata)
{
  MXC_USB_Req_t *req = (MXC_USB_Req_t*)cbdata;

  if (req->error_code == 0) {
    /* Send ACK to Status stage */
    MXC_USB_Ackstat(0);
  } else {
    /* STALL the Status stage */
    MXC_USB_Stall(0);
  }

  /* Clear the request to indicate completion */
  memset(req, 0, sizeof(MXC_USB_Req_t));
}

/******************************************************************************/


/*
 *  USB Device Interrupt enable
 *   Called by USBD_Init to enable the USB Interrupt
 *    Return Value:    None
 */

void          USBD_IntrEna(void)
{
    NVIC_EnableIRQ(USB_IRQn);            /* Enable OTG interrupt */
}

/******************************************************************************/
/*
 *  Usb interrupt enable/disable
 *    Parameters:      ena: enable/disable
 *                       0: disable interrupt
 *                       1: enable interrupt
 */
#ifdef __RTX
void __svc(1) USBD_Intr (int ena);
void __SVC_1 (int ena)
{
    if (ena) {
        NVIC_EnableIRQ(USB_IRQn);           /* Enable USB interrupt               */
    } else {
        NVIC_DisableIRQ(USB_IRQn);          /* Disable USB interrupt              */
    }
}
#endif

/******************************************************************************/
static void reset_state(void)
{
    suspended = 0;
    setup_waiting = 0;
    ep0_expect_zlp = 0;

    for (int i = 0; i < MXC_USBHS_NUM_EP; ++i) {
        expect_in_stage[i] = 0;
        expect_out_stage[i] = 0;
    }
}

void delay_us(unsigned int usec)
{
    /* mxc_delay() takes unsigned long, so can't use it directly */
    MXC_Delay(usec);
}

int usbStartupCallback(void)
{
    MXC_SYS_ClockSourceEnable(MXC_SYS_CLOCK_IPO);
    MXC_MCR->ldoctrl |= MXC_F_MCR_LDOCTRL_0P9EN;
    MXC_SYS_ClockEnable(MXC_SYS_PERIPH_CLOCK_USB);
    MXC_SYS_Reset_Periph(MXC_SYS_RESET0_USB);

    return E_NO_ERROR;
}

/******************************************************************************/
int usbShutdownCallback(void)
{
    MXC_SYS_ClockDisable(MXC_SYS_PERIPH_CLOCK_USB);

    return E_NO_ERROR;
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

    /* Initialize the usb module */
    if (MXC_USB_Init(&usb_opts) != 0) {
        while (1) {}
    }
    reset_state();
    MXC_USB_IrqEnable(MAXUSB_EVENT_NOVBUS);
    MXC_USB_IrqEnable(MAXUSB_EVENT_VBUS);
    MXC_USB_IrqDisable(MAXUSB_EVENT_BACT);
    MXC_USB_IrqDisable(MAXUSB_EVENT_BRST);
    MXC_USB_IrqDisable(MAXUSB_EVENT_SUSP);
    MXC_USB_IrqDisable(MAXUSB_EVENT_SUDAV);

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

void TMR0_IRQHandler(void)
{
    MXC_TMR_ClearFlags(CONT_TIMER);

    if (usbd_configured()) {
        USBD_CDC_ACM_SOF_Event();
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
    //if (!setup)
        MXC_USB_SetFuncAddr(adr);
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

/*
 *  Enable USB Device Endpoint
 *    Parameters:      EPNum: Device Endpoint Number
 *                       EPNum.0..3: Address
 *                       EPNum.7:    Dir
 *    Return Value:    None
 */
void USBD_EnableEP (U32 EPNum)
{

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
    MXC_USB_ResetEp(EPNum & EPNUM_MASK);
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
    U32 cnt;
    MXC_USB_SetupPkt sud;
    MXC_USB_Req_t req;

    EPNum &= EPNUM_MASK;

    if ((EPNum == 0) && !setup_waiting){
        return 0;
    }

    if ((EPNum == 0) && setup_waiting) {
        cnt = sizeof(USB_SETUP_PACKET);

        if (size < cnt) {
            util_assert(0);
            return 0;
        }
        setup_waiting = 0;
        MXC_USB_GetSetup(&sud);

        memcpy(pData, &sud, size);

    } else {
        req.ep = EPNum;
        req.reqlen = size;
        req.data = pData;
        MXC_USB_ReadEndpoint(&req);
        expect_out_stage[EPNum] = 1;
        cnt = req.actlen;
    }

    return cnt;
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
    MXC_USB_Req_t req;

    EPNum &= EPNUM_MASK;

    if (cnt > MXC_USBHS_MAX_PACKET) {
        cnt = MXC_USBHS_MAX_PACKET;
    }

    if (EPNum == 0) {
        if ((cnt == 0) && !ep0_expect_zlp) {
            // This is a status stage ACK.
            MXC_USB_Ackstat(0);
            return 0;
        } else if (cnt == USBD_MAX_PACKET0) {
            ep0_expect_zlp = 1;
        } else {
            ep0_expect_zlp = 0;
        }
        memcpy(&setup_req, &setup_req_init, sizeof(MXC_USB_Req_t));
        setup_req.data = (uint8_t*)pData;
        setup_req.reqlen = cnt;
        MXC_USB_WriteEndpoint(&setup_req);
        expect_in_stage[0] = 1;
        cnt = setup_req.actlen;
        return cnt;
    }

    req.ep = EPNum;
    req.data = pData;
    req.reqlen = cnt;
    
    MXC_USB_WriteEndpoint(&req);
    expect_in_stage[EPNum] = 1;

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

/* sent packet done handler*/
static void event_in_data(uint32_t irqs)
{
    uint32_t ep, buffer_bit, data_left;
    MXC_USB_Req_t *req;
    MXC_USB_Req_t local_req;
    unsigned int len;

    /* Loop for each data endpoint */
    for (ep = 0; ep < MXC_USBHS_NUM_EP; ep++) {
        buffer_bit = (1 << ep);
        if ((irqs & buffer_bit) == 0) { /* Not set, next Endpoint */
            continue;
        }

        if (expect_in_stage[ep]) {
            expect_in_stage[ep] = 0;
            MXC_USB_Ackstat(0);
        }

        if (USBD_P_EP[ep]) {
            USBD_P_EP[ep](USBD_EVT_IN);
            if (!expect_in_stage[ep]) { // no more data to send
                MXC_USBHS->csr0 |= MXC_F_USBHS_CSR0_INPKTRDY | MXC_F_USBHS_CSR0_DATA_END;
            }
        }
    }
}

/* received packet */
static void event_out_data(uint32_t irqs)
{
    uint32_t ep, buffer_bit, reqsize;

    /* Loop for each data endpoint */
    for (ep = 0; ep < MXC_USBHS_NUM_EP; ep++) {
        buffer_bit = (1 << ep);
        if ((irqs & buffer_bit) == 0) {
            continue;
        }

        if (!USBD_P_EP[ep]) {
            continue;
        }
        /* This function is called within interrupt context, so no need for a critical section */

        /* Select this endpoint for banked registers */
        MXC_USBHS->index = ep;

        if (!ep) {
            if (!(MXC_USBHS->csr0 & MXC_F_USBHS_CSR0_OUTPKTRDY)) {
                continue;
            }
            if (MXC_USBHS->count0 == 0) {
                continue;
            } else {
                /* Write as much as we can to the request buffer */
                reqsize = MXC_USBHS->count0;
            }
        } else {
            if (!(MXC_USBHS->outcsrl & MXC_F_USBHS_OUTCSRL_OUTPKTRDY)) {
                /* No packet on this endpoint? */
                continue;
            }
            if (MXC_USBHS->outcount == 0) {

                /* Signal to H/W that FIFO has been read */
                MXC_USBHS->outcsrl &= ~MXC_F_USBHS_OUTCSRL_OUTPKTRDY;

                /* Disable interrupt for this endpoint */
                MXC_USBHS->introuten &= ~(1 << ep);

                continue;
            }
        }

        if (expect_out_stage[ep]) {
            expect_out_stage[ep] = 0;
            MXC_USB_Ackstat(0);
        }
#ifdef __RTX
        if (USBD_RTX_EPTask[ep]) {
            isr_evt_set(USBD_EVT_OUT, USBD_RTX_EPTask[ep]);
        }
#else
        if (USBD_P_EP[ep]) {
        
            USBD_P_EP[ep](USBD_EVT_OUT);
        
            if (expect_out_stage[ep] == 0) {
                if (!ep) {
                    /* No more data */
                    MXC_USBHS->csr0 |= MXC_F_USBHS_CSR0_SERV_OUTPKTRDY | MXC_F_USBHS_CSR0_DATA_END;
                } else {
                    /* Signal to H/W that FIFO has been read */
                    MXC_USBHS->outcsrl &= ~MXC_F_USBHS_OUTCSRL_OUTPKTRDY;

                    /* Disable interrupt for this endpoint */
                    MXC_USBHS->introuten &= ~(1 << ep);
                }
            }
        }
#endif
    
    }
}

void USBD_Handler(void)
{
    uint32_t saved_index;
    uint32_t in_flags, out_flags, MXC_USB_flags, MXC_USB_mxm_flags;
    int i, aborted = 0;
    uint32_t intrusb, intrusben, intrin, intrinen, introut, introuten, mxm_int, mxm_int_en;

    /* Save current index register */
    saved_index = MXC_USBHS->index;

    /* Note: Hardware clears these after read, so we must process them all or they are lost */
    /*  Order of volatile accesses must be separated for IAR */
    intrusb = MXC_USBHS->intrusb;
    intrusben = MXC_USBHS->intrusben;
    MXC_USB_flags = intrusb & intrusben;

    intrin = MXC_USBHS->intrin;
    intrinen = MXC_USBHS->intrinen;
    in_flags = intrin & intrinen;

    introut = MXC_USBHS->introut;
    introuten = MXC_USBHS->introuten;
    out_flags = introut & introuten;

    /* These USB interrupt flags are W1C. */
    /*  Order of volatile accesses must be separated for IAR */
    mxm_int = MXC_USBHS->mxm_int;
    mxm_int_en = MXC_USBHS->mxm_int_en;
    MXC_USB_mxm_flags = mxm_int & mxm_int_en;
    MXC_USBHS->mxm_int = MXC_USB_mxm_flags;


    if (!!(MXC_USB_flags & MXC_F_USBHS_INTRUSB_RESET_INT)) { //BRST
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
    }
    
    if (!!(MXC_USB_flags & MXC_F_USBHS_INTRUSB_RESET_INT)) {//BRSTDN
        reset_state();
    } 
    
    /* suspend interrupt */
    if (!!(MXC_USB_flags & MXC_F_USBHS_INTRUSB_SUSPEND_INT)) {
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
    }

    if (!!(MXC_USB_mxm_flags & MXC_F_USBHS_MXM_INT_VBUS)) {
#ifdef __RTX
        if (USBD_RTX_DevTask) {
            isr_evt_set(USBD_EVT_POWER_ON,  USBD_RTX_DevTask);
        }
#else
        if (USBD_P_Power_Event) {
            USBD_P_Power_Event(1);
        }
#endif
    }

    if (!!(MXC_USB_mxm_flags & MXC_F_USBHS_MXM_INT_NOVBUS)) {

#ifdef __RTX
        if (USBD_RTX_DevTask) {
            isr_evt_set(USBD_EVT_POWER_OFF, USBD_RTX_DevTask);
        }
#else
        if (USBD_P_Power_Event) {
            USBD_P_Power_Event(0);
        }
#endif
    }

    /* Handle control state machine */
    if (in_flags & MXC_F_USBHS_INTRIN_EP0_IN_INT) {
        /* Select endpoint 0 */
        MXC_USBHS->index = 0;
        /* Process all error conditions */
        if (MXC_USBHS->csr0 & MXC_F_USBHS_CSR0_SENT_STALL) {
            /* Clear stall indication, go back to IDLE */
            MXC_USBHS->csr0 &= ~(MXC_F_USBHS_CSR0_SENT_STALL);
            /* Remove this from the IN flags so that it is not erroneously processed as data */
            in_flags &= ~MXC_F_USBHS_INTRIN_EP0_IN_INT;
            MXC_USB_SetSetupPhase(SETUP_IDLE);
            aborted = 1;
        }
        if (MXC_USBHS->csr0 & MXC_F_USBHS_CSR0_SETUP_END) {
            /* Abort pending requests, clear early end-of-control-transaction bit, go back to IDLE */
            MXC_USBHS->csr0 |= (MXC_F_USBHS_CSR0_SERV_SETUP_END);
            MXC_USB_SetSetupPhase(SETUP_IDLE);

            /* Remove this from the IN flags so that it is not erroneously processed as data */
            in_flags &= ~MXC_F_USBHS_INTRIN_EP0_IN_INT;
            MXC_USB_ResetEp(0);
            aborted = 1;
        }
        /* Now, check for a SETUP packet */
        if (!aborted) {
            if ((MXC_USB_GetSetupPhase() == SETUP_IDLE) && (MXC_USBHS->csr0 & MXC_F_USBHS_CSR0_OUTPKTRDY)) {
                /* Flag that we got a SETUP packet */
                setup_waiting = 1;
                /* Remove this from the IN flags so that it is not erroneously processed as data */
                in_flags &= ~MXC_F_USBHS_INTRIN_EP0_IN_INT;
                //if (USBD_P_EP[0]) {
                //    USBD_P_EP[0](USBD_EVT_SETUP);
                //}
            } else {
                /* Otherwise, we are in endpoint 0 data IN/OUT */
                /* Fix interrupt flags so that OUTs are processed properly */
                if (MXC_USB_GetSetupPhase() == SETUP_DATA_OUT) {
                    in_flags &= ~MXC_F_USBHS_INTRIN_EP0_IN_INT;
                    out_flags |= MXC_F_USBHS_INTRIN_EP0_IN_INT;
                }
                /* SETUP_NODATA is silently ignored by event_in_data() right now.. could fix this later */
            }
        }
    }

    /* do cleanup in cases of bus reset */
    if (!!(MXC_USB_flags & MXC_F_USBHS_INTRUSB_RESET_INT)) {
        MXC_USB_SetSetupPhase(SETUP_IDLE);

        /* kill any pending requests */
        for (i = 0; i < MXC_USBHS_NUM_EP; i++) {
            MXC_USB_ResetEp(i);
        }
        /* no need to process events after reset */
        return;
    }

    if (setup_waiting) {
#ifdef __RTX
        if (USBD_RTX_EPTask[0]) {
            isr_evt_set(USBD_EVT_SETUP, USBD_RTX_EPTask[0]);
        }
#else
        if (USBD_P_EP[0]) {
            USBD_P_EP[0](USBD_EVT_SETUP);
        }
#endif
    }
    if (in_flags) {
        event_in_data(in_flags);
    }

    if (out_flags) {
        event_out_data(out_flags);
    }



    /* Restore register index before exiting ISR */
    MXC_USBHS->index = saved_index;

    NVIC_EnableIRQ(USB_IRQn);
}
