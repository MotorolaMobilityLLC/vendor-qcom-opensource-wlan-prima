/*
 * Copyright (c) 2012, Code Aurora Forum. All rights reserved.
 *
 * Previously licensed under the ISC license by Qualcomm Atheros, Inc.
 *
 *
 * Permission to use, copy, modify, and/or distribute this software for
 * any purpose with or without fee is hereby granted, provided that the
 * above copyright notice and this permission notice appear in all
 * copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
 * WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
 * AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/**
 *
 *  @file:         wlan_qct_dev_defs.h
 *
 *  @brief:        This file contains the hardware related definitions.
 *
 *  Copyright (C)  2008, Qualcomm, Inc. All rights reserved.
 */

#ifndef __WLAN_QCT_DEV_DEFS_H
#define __WLAN_QCT_DEV_DEFS_H


/* --------------------------------------------------------------------
 * HW definitions for WLAN Chip
 * --------------------------------------------------------------------
 */

/*In prima 12 HW stations are supported including BCAST STA(staId 0)
 and SELF STA(staId 1) so total ASSOC stations which can connect to Prima
 SoftAP = 12 - 1(Self STa) - 1(Bcast Sta) = 10 Stations. */

#define HAL_NUM_STA                 12
#define HAL_NUM_BSSID               2
#define HAL_NUM_UMA_DESC_ENTRIES    12

#define HAL_INVALID_BSSIDX          HAL_NUM_BSSID

#define MAX_NUM_OF_BACKOFFS         8
#define HAL_MAX_ASSOC_ID            HAL_NUM_STA

#define WLANHAL_TX_BD_HEADER_SIZE   40  //FIXME_PRIMA - Revisit
#define WLANHAL_RX_BD_HEADER_SIZE   76  

/*
 * From NOVA Mac Arch document
 *  Encryp. mode    The encryption mode
 *  000: Encryption functionality is not enabled
 *  001: Encryption is set to WEP
 *  010: Encryption is set to WEP 104
 *  011: Encryption is set to TKIP
 *  100: Encryption is set to AES
 *  101 - 111: Reserved for future
 */

#define HAL_ENC_POLICY_NULL        0
#define HAL_ENC_POLICY_WEP40       1
#define HAL_ENC_POLICY_WEP104      2
#define HAL_ENC_POLICY_TKIP        3
#define HAL_ENC_POLICY_AES_CCM     4

/* --------------------------------------------------------------------- */
/* BMU */
/* --------------------------------------------------------------------- */

/*
 * BMU WQ assignment, as per Prima Programmer's Guide - FIXME_PRIMA: Revisit
 *
 */

typedef enum sBmuWqId {

    /* ====== In use WQs ====== */

    /* BMU */
    BMUWQ_BMU_IDLE_BD = 0,
    BMUWQ_BMU_IDLE_PDU = 1,

    /* RxP */
    BMUWQ_RXP_UNKNWON_ADDR = 2,  /* currently unhandled by HAL */

    /* DPU RX */
    BMUWQ_DPU_RX = 3,

    /* DPU TX */
    BMUWQ_DPU_TX = 6,

    /* Firmware */
    BMUWQ_FW_TRANSMIT = 12,  /* DPU Tx->FW Tx */
    BMUWQ_FW_RECV = 7,       /* DPU Rx->FW Rx */

    BMUWQ_FW_RPE_RECV = 16,   /* RXP/RPE Rx->FW Rx */
    FW_SCO_WQ = BMUWQ_FW_RPE_RECV,

    /* DPU Error */
    BMUWQ_DPU_ERROR_WQ = 8,

    /* DXE RX */
    BMUWQ_DXE_RX = 11,

    BMUWQ_DXE_RX_HI = 4,

    /* ADU/UMA */
    BMUWQ_ADU_UMA_TX = 23,
    BMUWQ_ADU_UMA_RX = 24,

    /* BMU BTQM */
    BMUWQ_BTQM = 25,

    /* Special WQ for BMU to dropping all frames coming to this WQ ID */
    BMUWQ_SINK = 255,

    /* Total BMU WQ count in Volans */
    BMUWQ_NUM = 27,

    //Volans has excluded support for WQs 17 through 22.
    BMUWQ_NOT_SUPPORTED_MASK = 0x7e0000,

    /* Aliases */
    BMUWQ_BTQM_TX_MGMT = BMUWQ_BTQM,
    BMUWQ_BTQM_TX_DATA = BMUWQ_BTQM,
    BMUWQ_BMU_WQ2 = BMUWQ_RXP_UNKNWON_ADDR,
    BMUWQ_FW_DPU_TX = 5,

    //WQ where all the frames with addr1/addr2/addr3 with value 254/255 go to. 
    BMUWQ_FW_RECV_EXCEPTION = 14, //using BMUWQ_FW_MESSAGE WQ for this purpose.

    //WQ where all frames with unknown Addr2 filter exception cases frames will pushed if FW wants host to 
    //send deauth to the sender. 
    BMUWQ_HOST_RX_UNKNOWN_ADDR2_FRAMES = 15, //using BMUWQ_FW_DXECH2_0 for this purpose.

    /* ====== Unused/Reserved WQ ====== */

    /* ADU/UMA Error WQ */
    BMUWQ_ADU_UMA_TX_ERROR_WQ = 13, /* Not in use by HAL */
    BMUWQ_ADU_UMA_RX_ERROR_WQ = 10, /* Not in use by HAL */

    /* DPU Error WQ2 */
    BMUWQ_DPU_ERROR_WQ2 = 9, /* Not in use by HAL */

    /* FW WQs */
    //This WQ is being used for RXP to push in frames in exception cases ( addr1/add2/addr3 254/255)
    //BMUWQ_FW_MESG = 14,      /* DxE Tx->FW, Not in use by FW */
    //BMUWQ_FW_DXECH2_0 = 15,  /* BD/PDU<->MEM conversion using DxE CH2.  Not in use by FW */
    BMUWQ_FW_DXECH2_1 = 16,  /* BD/PDU<->MEM conversion using DxE CH2.  Not in use by FW */

/*  These WQs are not supported in Volans
    BMUWQ_BMU_WQ17 = 17,
    BMUWQ_BMU_WQ18 = 18,
    BMUWQ_BMU_WQ19 = 19,
    BMUWQ_BMU_WQ20 = 20,
    BMUWQ_BMU_WQ21 = 21,
    BMUWQ_BMU_WQ22 = 22
*/
} tBmuWqId;

typedef enum
{
    BTQM_QID0 = 0,
    BTQM_QID1,
    BTQM_QID2,
    BTQM_QID3,
    BTQM_QID4,
    BTQM_QID5,
    BTQM_QID6,
    BTQM_QID7,
    BTQM_QID8,
    BTQM_QID9,
    BTQM_QID10,

    BTQM_QUEUE_TX_TID_0 = BTQM_QID0,
    BTQM_QUEUE_TX_TID_1,
    BTQM_QUEUE_TX_TID_2,
    BTQM_QUEUE_TX_TID_3,
    BTQM_QUEUE_TX_TID_4,
    BTQM_QUEUE_TX_TID_5,
    BTQM_QUEUE_TX_TID_6,
    BTQM_QUEUE_TX_TID_7,


    /* Queue Id <-> BO 
       */
    BTQM_QUEUE_TX_nQOS = BTQM_QID8,
    BTQM_QUEUE_SELF_STA_BCAST_MGMT = BTQM_QID10,    
    BTQM_QUEUE_SELF_STA_UCAST_MGMT = BTQM_QID9,
    BTQM_QUEUE_SELF_STA_UCAST_DATA = BTQM_QID9,
    BTQM_QUEUE_NULL_FRAME          = BTQM_QID9,      
    BTQM_QUEUE_SELF_STA_PROBE_RSP =  BTQM_QID9,
    BTQM_QUEUE_TX_AC_BE = BTQM_QUEUE_TX_TID_0,
    BTQM_QUEUE_TX_AC_BK = BTQM_QUEUE_TX_TID_2,
    BTQM_QUEUE_TX_AC_VI = BTQM_QUEUE_TX_TID_4,
    BTQM_QUEUE_TX_AC_VO = BTQM_QUEUE_TX_TID_6
}tBtqmQId;

#define STACFG_MAX_TC   8

/* --------------------------------------------------------------------- */
/* BD  type*/
/* --------------------------------------------------------------------- */
#define HWBD_TYPE_GENERIC                  0   /* generic BD format */
#define HWBD_TYPE_FRAG                     1   /* fragmentation BD format*/

#endif /* __WLAN_QCT_DEV_DEFS_H */
