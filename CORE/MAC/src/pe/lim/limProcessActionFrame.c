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

/*
 * Airgo Networks, Inc proprietary. All rights reserved.
 * This file limProcessActionFrame.cc contains the code
 * for processing Action Frame.
 * Author:      Michael Lui
 * Date:        05/23/03
 * History:-
 * Date           Modified by    Modification Information
 * --------------------------------------------------------------------
 *
 */
#include "palTypes.h"
#include "wniApi.h"
#include "sirApi.h"
#include "aniGlobal.h"
#ifdef FEATURE_WLAN_NON_INTEGRATED_SOC
#include "halDataStruct.h"
#endif
#if (WNI_POLARIS_FW_PRODUCT == AP)
#include "wniCfgAp.h"
#else
#include "wniCfgSta.h"
#endif
#include "schApi.h"
#include "utilsApi.h"
#include "limTypes.h"
#include "limUtils.h"
#include "limAssocUtils.h"
#include "limSecurityUtils.h"
#include "limSerDesUtils.h"
#include "limSendSmeRspMessages.h"
#include "parserApi.h"
#include "limAdmitControl.h"
#include "wmmApsd.h"
#include "limSendMessages.h"
#if defined WLAN_FEATURE_VOWIFI
#include "rrmApi.h"
#endif

#if defined FEATURE_WLAN_CCX
#include "ccxApi.h"
#endif
#include "wlan_qct_wda.h"


#define BA_DEFAULT_TX_BUFFER_SIZE 64

typedef enum
{
  LIM_ADDBA_RSP = 0,
  LIM_ADDBA_REQ = 1
}tLimAddBaValidationReqType;

/* Note: The test passes if the STAUT stops sending any frames, and no further
 frames are transmitted on this channel by the station when the AP has sent
 the last 6 beacons, with the channel switch information elements as seen
 with the sniffer.*/
#define SIR_CHANSW_TX_STOP_MAX_COUNT 6
/**-----------------------------------------------------------------
\fn     limStopTxAndSwitchChannel
\brief  Stops the transmission if channel switch mode is silent and
        starts the channel switch timer.

\param  pMac
\return NONE
-----------------------------------------------------------------*/
void limStopTxAndSwitchChannel(tpAniSirGlobal pMac, tANI_U8 sessionId)
{
    tANI_U8 isFullPowerRequested = 0;

    PELOG1(limLog(pMac, LOG1, FL("Channel switch Mode == %d\n"), 
                       pMac->lim.gLimChannelSwitch.switchMode);)

    if (pMac->lim.gLimChannelSwitch.switchMode == eSIR_CHANSW_MODE_SILENT ||
        pMac->lim.gLimChannelSwitch.switchCount <= SIR_CHANSW_TX_STOP_MAX_COUNT)
    {
        /* Freeze the transmission */
        limFrameTransmissionControl(pMac, eLIM_TX_ALL, eLIM_STOP_TX);

        /*Request for Full power only if the device is in powersave*/
        if(!limIsSystemInActiveState(pMac))
        {
            /* Request Full Power */
            limSendSmePreChannelSwitchInd(pMac);
            isFullPowerRequested = 1;
        }
    }
    else
    {
        /* Resume the transmission */
        limFrameTransmissionControl(pMac, eLIM_TX_ALL, eLIM_RESUME_TX);
    }

    /* change the channel immediatly only if the channel switch count is 0 and the 
     * device is not in powersave 
     * If the device is in powersave channel switch should happen only after the
     * device comes out of the powersave */
    if (pMac->lim.gLimChannelSwitch.switchCount == 0) 
    {
        if(limIsSystemInActiveState(pMac))
        {
            limProcessChannelSwitchTimeout(pMac);
        }
        else if(!isFullPowerRequested)
        {
            /* If the Full power is already not requested 
             * Request Full Power so the channel switch happens 
             * after device comes to full power */
            limSendSmePreChannelSwitchInd(pMac);
        }
        return;
    }

    MTRACE(macTrace(pMac, TRACE_CODE_TIMER_ACTIVATE, 0, eLIM_CHANNEL_SWITCH_TIMER));

    pMac->lim.limTimers.gLimChannelSwitchTimer.sessionId = sessionId;

    if (tx_timer_activate(&pMac->lim.limTimers.gLimChannelSwitchTimer) != TX_SUCCESS)
    {
        limLog(pMac, LOGP, FL("tx_timer_activate failed\n"));
    }
    return;
}

/**------------------------------------------------------------
\fn     limStartChannelSwitch
\brief  Switches the channel if switch count == 0, otherwise
        starts the timer for channel switch and stops BG scan
        and heartbeat timer tempororily.

\param  pMac
\param  psessionEntry
\return NONE
------------------------------------------------------------*/
tSirRetStatus limStartChannelSwitch(tpAniSirGlobal pMac, tpPESession psessionEntry)
{
    PELOG1(limLog(pMac, LOG1, FL("Starting the channel switch\n"));)
    /* Deactivate and change reconfigure the timeout value */
    limDeactivateAndChangeTimer(pMac, eLIM_CHANNEL_SWITCH_TIMER);

    /* Follow the channel switch, forget about the previous quiet. */
    //If quiet is running, chance is there to resume tx on its timeout.
    //so stop timer for a safer side.
    if (pMac->lim.gLimSpecMgmt.quietState == eLIM_QUIET_BEGIN)
    {
        MTRACE(macTrace(pMac, TRACE_CODE_TIMER_DEACTIVATE, 0, eLIM_QUIET_TIMER));
        if (tx_timer_deactivate(&pMac->lim.limTimers.gLimQuietTimer) != TX_SUCCESS)
        {
            limLog(pMac, LOGP, FL("tx_timer_deactivate failed\n"));
            return eSIR_FAILURE;
        }
    }
    else if (pMac->lim.gLimSpecMgmt.quietState == eLIM_QUIET_RUNNING)
    {
        MTRACE(macTrace(pMac, TRACE_CODE_TIMER_DEACTIVATE, 0, eLIM_QUIET_BSS_TIMER));
        if (tx_timer_deactivate(&pMac->lim.limTimers.gLimQuietBssTimer) != TX_SUCCESS)
        {
            limLog(pMac, LOGP, FL("tx_timer_deactivate failed\n"));
            return eSIR_FAILURE;
        }
    }
    pMac->lim.gLimSpecMgmt.quietState = eLIM_QUIET_INIT;

    /* Prepare for 11h channel switch */
    limPrepareFor11hChannelSwitch(pMac, psessionEntry);

    /** Dont add any more statements here as we posted finish scan request
     * to HAL, wait till we get the response
     */
    return eSIR_SUCCESS;
}


/**
 *  __limProcessChannelSwitchActionFrame
 *
 *FUNCTION:
 *
 *LOGIC:
 *
 *ASSUMPTIONS:
 *
 *NOTE:
 *
 * @param  pMac - Pointer to Global MAC structure
 * @param  *pRxPacketInfo - A pointer to packet info structure
 * @return None
 */

static void

__limProcessChannelSwitchActionFrame(tpAniSirGlobal pMac, tANI_U8 *pRxPacketInfo,tpPESession psessionEntry)
{

    tpSirMacMgmtHdr         pHdr;
    tANI_U8                 *pBody;
    tDot11fChannelSwitch    *pChannelSwitchFrame;
    tANI_U16                beaconPeriod;
    tANI_U32                val;
    tANI_U32                frameLen;
    tANI_U32                nStatus;
    eHalStatus              status;

    pHdr = WDA_GET_RX_MAC_HEADER(pRxPacketInfo);
    pBody = WDA_GET_RX_MPDU_DATA(pRxPacketInfo);
    frameLen = WDA_GET_RX_PAYLOAD_LEN(pRxPacketInfo);

    PELOG3(limLog(pMac, LOG3, FL("Received Channel switch action frame\n"));)
    if (!psessionEntry->lim11hEnable)
        return;

    status = palAllocateMemory( pMac->hHdd, (void **)&pChannelSwitchFrame, sizeof(*pChannelSwitchFrame));
    if (eHAL_STATUS_SUCCESS != status)
    {
        limLog(pMac, LOGE,
            FL("palAllocateMemory failed, status = %d \n"), status);
        return;
    }

    /* Unpack channel switch frame */
    nStatus = dot11fUnpackChannelSwitch(pMac, pBody, frameLen, pChannelSwitchFrame);

    if( DOT11F_FAILED( nStatus ))
    {
        limLog( pMac, LOGE,
            FL( "Failed to unpack and parse an 11h-CHANSW Request (0x%08x, %d bytes):\n"),
            nStatus,
            frameLen);
        palFreeMemory(pMac->hHdd, pChannelSwitchFrame);
        return;
    }
    else if(DOT11F_WARNED( nStatus ))
    {
        limLog( pMac, LOGW,
            FL( "There were warnings while unpacking an 11h-CHANSW Request (0x%08x, %d bytes):\n"),
            nStatus,
            frameLen);
    }

    if (palEqualMemory( pMac->hHdd,(tANI_U8 *) &psessionEntry->bssId,
                  (tANI_U8 *) &pHdr->sa,
                  sizeof(tSirMacAddr)))
    {
        #if 0
        if (wlan_cfgGetInt(pMac, WNI_CFG_BEACON_INTERVAL, &val) != eSIR_SUCCESS)
        {
            palFreeMemory(pMac->hHdd, pChannelSwitchFrame);
            limLog(pMac, LOGP, FL("could not retrieve Beacon interval\n"));
            return;
        }
        #endif// TO SUPPORT BT-AMP

        /* copy the beacon interval from psessionEntry*/
        val = psessionEntry->beaconParams.beaconInterval;

        beaconPeriod = (tANI_U16) val;

        pMac->lim.gLimChannelSwitch.primaryChannel = pChannelSwitchFrame->ChanSwitchAnn.newChannel;
        pMac->lim.gLimChannelSwitch.switchCount = pChannelSwitchFrame->ChanSwitchAnn.switchCount;
        pMac->lim.gLimChannelSwitch.switchTimeoutValue = SYS_MS_TO_TICKS(beaconPeriod) *
                                                         pMac->lim.gLimChannelSwitch.switchCount;
        pMac->lim.gLimChannelSwitch.switchMode = pChannelSwitchFrame->ChanSwitchAnn.switchMode;

       PELOG3(limLog(pMac, LOG3, FL("Rcv Chnl Swtch Frame: Timeout in %d ticks\n"),
                             pMac->lim.gLimChannelSwitch.switchTimeoutValue);)

        /* Only primary channel switch element is present */
        pMac->lim.gLimChannelSwitch.state = eLIM_CHANNEL_SWITCH_PRIMARY_ONLY;
        pMac->lim.gLimChannelSwitch.secondarySubBand = eANI_CB_SECONDARY_NONE;

        if(GET_CB_ADMIN_STATE(pMac->lim.gCbState))
        {
            switch(pChannelSwitchFrame->ExtChanSwitchAnn.secondaryChannelOffset)
            {
                case eHT_SECONDARY_CHANNEL_OFFSET_UP:
                    pMac->lim.gLimChannelSwitch.state = eLIM_CHANNEL_SWITCH_PRIMARY_AND_SECONDARY;
                    pMac->lim.gLimChannelSwitch.secondarySubBand = eANI_CB_SECONDARY_UP;
                    break;

                case eHT_SECONDARY_CHANNEL_OFFSET_DOWN:
                    pMac->lim.gLimChannelSwitch.state = eLIM_CHANNEL_SWITCH_PRIMARY_AND_SECONDARY;
                    pMac->lim.gLimChannelSwitch.secondarySubBand = eANI_CB_SECONDARY_DOWN;
                    break;

                case eHT_SECONDARY_CHANNEL_OFFSET_NONE:
                default:
                    /* Nothing to be done here */
                    break;
            }
        }

    }
    else
    {
        PELOG1(limLog(pMac, LOG1, FL("LIM: Received action frame not from our BSS, dropping..."));)
    }

    if (eSIR_SUCCESS != limStartChannelSwitch(pMac, psessionEntry))
    {
        PELOG1(limLog(pMac, LOG1, FL("Could not start channel switch\n"));)
    }

    palFreeMemory(pMac->hHdd, pChannelSwitchFrame);
    return;
} /*** end limProcessChannelSwitchActionFrame() ***/


static void
__limProcessAddTsReq(tpAniSirGlobal pMac, tANI_U8 *pRxPacketInfo,tpPESession psessionEntry)
{
#if (WNI_POLARIS_FW_PRODUCT == AP)

    tSirAddtsReqInfo addts;
    tSirRetStatus    retval;
    tpSirMacMgmtHdr  pHdr;
    tSirMacScheduleIE schedule;
    tpDphHashNode    pSta;
    tANI_U16              status;
    tANI_U16              aid;
    tANI_U32              frameLen;
    tANI_U8              *pBody;
    tANI_U8 tspecIdx = 0; //index in the sch tspec table.

    pHdr = WDA_GET_RX_MAC_HEADER(pRxPacketInfo);
    pBody = WDA_GET_RX_MPDU_DATA(pRxPacketInfo);
    frameLen = WDA_GET_RX_PAYLOAD_LEN(pRxPacketInfo);


    if ((psessionEntry->limSystemRole != eLIM_AP_ROLE)||(psessionEntry->limSystemRole != eLIM_BT_AMP_AP_ROLE))
    {
        PELOGW(limLog(pMac, LOGW, FL("AddTs request at non-AP: ignoring\n"));)
        return;
    }

    pSta = dphLookupHashEntry(pMac, pHdr->sa, &aid, &psessionEntry->dph.dphHashTable);
    if (pSta == NULL)
    {
        PELOGE(limLog(pMac, LOGE, FL("Station context not found - ignoring AddTs\n"));)
        return;
    }

    PELOGW(limLog(pMac, LOGW, "AddTs Request from STA %d\n", aid);)
    retval = sirConvertAddtsReq2Struct(pMac, pBody, frameLen, &addts);
    if (retval != eSIR_SUCCESS)
    {
        PELOGW(limLog(pMac, LOGW, FL("AddTs parsing failed (error %d)\n"), retval);)
        return;
    }

    status = eSIR_MAC_SUCCESS_STATUS;


    if (addts.wmeTspecPresent)
    {
        if ((! psessionEntry->limWmeEnabled) || (! pSta->wmeEnabled))
        {
            PELOGW(limLog(pMac, LOGW, FL("Ignoring addts request: wme not enabled/capable\n"));)
            status = eSIR_MAC_WME_REFUSED_STATUS;
        }
        else
        {
            PELOG2(limLog(pMac, LOG2, FL("WME Addts received\n"));)
        }
    }
    else if (addts.wsmTspecPresent)
    {
        if ((! psessionEntry->limWsmEnabled) || (! pSta->wsmEnabled))
        {
            PELOGW(limLog(pMac, LOGW, FL("Ignoring addts request: wsm not enabled/capable\n"));)
            status = eSIR_MAC_REQ_DECLINED_STATUS;
        }
        else
        {
            PELOG2(limLog(pMac, LOG2, FL("WSM Addts received\n"));)
        }
    }
    else if ((! psessionEntry->limQosEnabled) || (! pSta->lleEnabled))
    {
        PELOGW(limLog(pMac, LOGW, FL("Ignoring addts request: qos not enabled/capable\n"));)
        status = eSIR_MAC_REQ_DECLINED_STATUS;
    }
    else
    {
        PELOG2(limLog(pMac, LOG2, FL("11e QoS Addts received\n"));)
    }

    // for edca, if no Admit Control, ignore the request
    if ((status == eSIR_MAC_SUCCESS_STATUS) &&
        (addts.tspec.tsinfo.traffic.accessPolicy == SIR_MAC_ACCESSPOLICY_EDCA) &&
        (! psessionEntry->gLimEdcaParamsBC[upToAc(addts.tspec.tsinfo.traffic.userPrio)].aci.acm))
    {
        limLog(pMac, LOGW, FL("AddTs with UP %d has no ACM - ignoring request\n"),
               addts.tspec.tsinfo.traffic.userPrio);
        status = (addts.wmeTspecPresent) ?
                 eSIR_MAC_WME_REFUSED_STATUS : eSIR_MAC_UNSPEC_FAILURE_STATUS;
    }

    if (status != eSIR_MAC_SUCCESS_STATUS)
    {
        limSendAddtsRspActionFrame(pMac, pHdr->sa, status, &addts, NULL,psessionEntry);
        return;
    }

    // try to admit the STA and send the appropriate response
    retval = limAdmitControlAddTS(pMac, &pSta->staAddr[0], &addts, NULL, pSta->assocId, true, &schedule, &tspecIdx, psessionEntry);
    if (retval != eSIR_SUCCESS)
    {
        PELOGW(limLog(pMac, LOGW, FL("Unable to admit TS\n"));)
        status = (addts.wmeTspecPresent) ?
                 eSIR_MAC_WME_REFUSED_STATUS : eSIR_MAC_UNSPEC_FAILURE_STATUS;
    }
    else if (addts.tspec.tsinfo.traffic.accessPolicy == SIR_MAC_ACCESSPOLICY_EDCA)
    {
        if(eSIR_SUCCESS != limSendHalMsgAddTs(pMac, pSta->staIndex, tspecIdx, addts.tspec, psessionEntry->peSessionId))
        {
          limLog(pMac, LOGW, FL("AddTs with UP %d failed in limSendHalMsgAddTs - ignoring request\n"),
                 addts.tspec.tsinfo.traffic.userPrio);
          status = (addts.wmeTspecPresent) ?
                   eSIR_MAC_WME_REFUSED_STATUS : eSIR_MAC_UNSPEC_FAILURE_STATUS;

           limAdmitControlDeleteTS(pMac, pSta->assocId, &addts.tspec.tsinfo, NULL, &tspecIdx);
        }
        if (status != eSIR_MAC_SUCCESS_STATUS)
        {
            limSendAddtsRspActionFrame(pMac, pHdr->sa, status, &addts, NULL,psessionEntry);
            return;
        }
    }
#if 0 //only EDCA is supported now.
    else if (addts.numTclas > 1)
    {
        limLog(pMac, LOGE, FL("Sta %d: Too many Tclas (%d), only 1 supported\n"),
               aid, addts.numTclas);
        status = (addts.wmeTspecPresent) ?
                 eSIR_MAC_WME_REFUSED_STATUS : eSIR_MAC_UNSPEC_FAILURE_STATUS;
    }
    else if (addts.numTclas == 1)
    {
        limLog(pMac, LOGW, "AddTs Request from STA %d: tsid %d, UP %d, OK!\n", aid,
               addts.tspec.tsinfo.traffic.tsid, addts.tspec.tsinfo.traffic.userPrio);
        status = eSIR_MAC_SUCCESS_STATUS;
    }
#endif

    limLog(pMac, LOGW, "AddTs Request from STA %d: Sending AddTs Response with status %d\n",
           aid, status);

    limSendAddtsRspActionFrame(pMac, pHdr->sa, status, &addts, &schedule,psessionEntry);
#endif
}


static void
__limProcessAddTsRsp(tpAniSirGlobal pMac, tANI_U8 *pRxPacketInfo,tpPESession psessionEntry)
{
    tSirAddtsRspInfo addts;
    tSirRetStatus    retval;
    tpSirMacMgmtHdr  pHdr;
    tpDphHashNode    pSta;
    tANI_U16         aid;
    tANI_U32         frameLen;
    tANI_U8          *pBody;
    tpLimTspecInfo   tspecInfo;
    tANI_U8          ac; 
    tpDphHashNode    pStaDs = NULL;
    tANI_U8          rspReqd = 1;
    tANI_U32   cfgLen;
    tSirMacAddr  peerMacAddr;


    pHdr = WDA_GET_RX_MAC_HEADER(pRxPacketInfo);
    pBody = WDA_GET_RX_MPDU_DATA(pRxPacketInfo);
    frameLen = WDA_GET_RX_PAYLOAD_LEN(pRxPacketInfo);


    PELOGW(limLog(pMac, LOGW, "Recv AddTs Response\n");)
    if ((psessionEntry->limSystemRole == eLIM_AP_ROLE)||(psessionEntry->limSystemRole == eLIM_BT_AMP_AP_ROLE))
    {
        PELOGW(limLog(pMac, LOGW, FL("AddTsRsp recvd at AP: ignoring\n"));)
        return;
    }

    pSta = dphLookupHashEntry(pMac, pHdr->sa, &aid, &psessionEntry->dph.dphHashTable);
    if (pSta == NULL)
    {
        PELOGE(limLog(pMac, LOGE, FL("Station context not found - ignoring AddTsRsp\n"));)
        return;
    }

    retval = sirConvertAddtsRsp2Struct(pMac, pBody, frameLen, &addts);
    if (retval != eSIR_SUCCESS)
    {
        PELOGW(limLog(pMac, LOGW, FL("AddTsRsp parsing failed (error %d)\n"), retval);)
        return;
    }

    // don't have to check for qos/wme capabilities since we wouldn't have this
    // flag set otherwise
    if (! pMac->lim.gLimAddtsSent)
    {
        // we never sent an addts request!
        PELOGW(limLog(pMac, LOGW, "Recvd AddTsRsp but no request was ever sent - ignoring\n");)
        return;
    }

    if (pMac->lim.gLimAddtsReq.req.dialogToken != addts.dialogToken)
    {
        limLog(pMac, LOGW, "AddTsRsp: token mismatch (got %d, exp %d) - ignoring\n",
               addts.dialogToken, pMac->lim.gLimAddtsReq.req.dialogToken);
        return;
    }

    /*
     * for successful addts reponse, try to add the classifier.
     * if this fails for any reason, we should send a delts request to the ap
     * for now, its ok not to send a delts since we are going to add support for
     * multiple tclas soon and until then we won't send any addts requests with
     * multiple tclas elements anyway.
     * In case of addClassifier failure, we just let the addts timer run out
     */
    if (((addts.tspec.tsinfo.traffic.accessPolicy == SIR_MAC_ACCESSPOLICY_HCCA) ||
         (addts.tspec.tsinfo.traffic.accessPolicy == SIR_MAC_ACCESSPOLICY_BOTH)) &&
        (addts.status == eSIR_MAC_SUCCESS_STATUS))
    {
        // add the classifier - this should always succeed
        if (addts.numTclas > 1) // currently no support for multiple tclas elements
        {
            limLog(pMac, LOGE, FL("Sta %d: Too many Tclas (%d), only 1 supported\n"),
                   aid, addts.numTclas);
            return;
        }
        else if (addts.numTclas == 1)
        {
            limLog(pMac, LOGW, "AddTs Response from STA %d: tsid %d, UP %d, OK!\n", aid,
                   addts.tspec.tsinfo.traffic.tsid, addts.tspec.tsinfo.traffic.userPrio);
        }
    }
    limLog(pMac, LOGW, "Recv AddTsRsp: tsid %d, UP %d, status %d \n",
          addts.tspec.tsinfo.traffic.tsid, addts.tspec.tsinfo.traffic.userPrio,
          addts.status);

    // deactivate the response timer
    limDeactivateAndChangeTimer(pMac, eLIM_ADDTS_RSP_TIMER);

    if (addts.status != eSIR_MAC_SUCCESS_STATUS)
    {
        limLog(pMac, LOGW, "Recv AddTsRsp: tsid %d, UP %d, status %d \n",
              addts.tspec.tsinfo.traffic.tsid, addts.tspec.tsinfo.traffic.userPrio,
              addts.status);
        limSendSmeAddtsRsp(pMac, true, addts.status, psessionEntry, addts.tspec, 
                psessionEntry->smeSessionId, psessionEntry->transactionId);

        // clear the addts flag
        pMac->lim.gLimAddtsSent = false;

        return;
    }
#ifdef FEATURE_WLAN_CCX
    if (addts.tsmPresent)
    {
        limLog(pMac, LOGW, "TSM IE Present\n");
        psessionEntry->ccxContext.tsm.tid = addts.tspec.tsinfo.traffic.userPrio;
        vos_mem_copy(&psessionEntry->ccxContext.tsm.tsmInfo,
                                         &addts.tsmIE,sizeof(tSirMacCCXTSMIE));
        limActivateTSMStatsTimer(pMac, psessionEntry);
    }
#endif
    /* Since AddTS response was successful, check for the PSB flag
     * and directional flag inside the TS Info field. 
     * An AC is trigger enabled AC if the PSB subfield is set to 1  
     * in the uplink direction.
     * An AC is delivery enabled AC if the PSB subfield is set to 1 
     * in the downlink direction.
     * An AC is trigger and delivery enabled AC if the PSB subfield  
     * is set to 1 in the bi-direction field.
     */
    if (addts.tspec.tsinfo.traffic.psb == 1)
        limSetTspecUapsdMask(pMac, &addts.tspec.tsinfo, SET_UAPSD_MASK);
    else 
        limSetTspecUapsdMask(pMac, &addts.tspec.tsinfo, CLEAR_UAPSD_MASK);


    /* ADDTS success, so AC is now admitted. We shall now use the default
     * EDCA parameters as advertised by AP and send the updated EDCA params
     * to HAL. 
     */
    ac = upToAc(addts.tspec.tsinfo.traffic.userPrio);
    if(addts.tspec.tsinfo.traffic.direction == SIR_MAC_DIRECTION_UPLINK)
    {
      pMac->lim.gAcAdmitMask[SIR_MAC_DIRECTION_UPLINK] |= (1 << ac);
    }
    else if(addts.tspec.tsinfo.traffic.direction == SIR_MAC_DIRECTION_DNLINK)
    {
      pMac->lim.gAcAdmitMask[SIR_MAC_DIRECTION_DNLINK] |= (1 << ac);
    }
    else if(addts.tspec.tsinfo.traffic.direction == SIR_MAC_DIRECTION_BIDIR)
    {
      pMac->lim.gAcAdmitMask[SIR_MAC_DIRECTION_UPLINK] |= (1 << ac);
      pMac->lim.gAcAdmitMask[SIR_MAC_DIRECTION_DNLINK] |= (1 << ac);
    }

    limSetActiveEdcaParams(pMac, psessionEntry->gLimEdcaParams, psessionEntry);

    pStaDs = dphGetHashEntry(pMac, DPH_STA_HASH_INDEX_PEER, &psessionEntry->dph.dphHashTable);
    if (pStaDs != NULL)
    {
        if (pStaDs->aniPeer == eANI_BOOLEAN_TRUE) 
            limSendEdcaParams(pMac, psessionEntry->gLimEdcaParamsActive, pStaDs->bssId, eANI_BOOLEAN_TRUE);
        else
            limSendEdcaParams(pMac, psessionEntry->gLimEdcaParamsActive, pStaDs->bssId, eANI_BOOLEAN_FALSE);
    }
    else
        limLog(pMac, LOGE, FL("Self entry missing in Hash Table \n"));

        
    sirCopyMacAddr(peerMacAddr,psessionEntry->bssId);

    //if schedule is not present then add TSPEC with svcInterval as 0.
    if(!addts.schedulePresent)
      addts.schedule.svcInterval = 0;
    if(eSIR_SUCCESS != limTspecAdd(pMac, pSta->staAddr, pSta->assocId, &addts.tspec,  addts.schedule.svcInterval, &tspecInfo))
    {
        PELOGE(limLog(pMac, LOGE, FL("Adding entry in lim Tspec Table failed \n"));)
        limSendDeltsReqActionFrame(pMac, peerMacAddr, rspReqd, &addts.tspec.tsinfo, &addts.tspec,
                psessionEntry);
        pMac->lim.gLimAddtsSent = false;
        return;   //Error handling. send the response with error status. need to send DelTS to tear down the TSPEC status.
    }
    if((addts.tspec.tsinfo.traffic.accessPolicy != SIR_MAC_ACCESSPOLICY_EDCA) ||
       ((upToAc(addts.tspec.tsinfo.traffic.userPrio) < MAX_NUM_AC) &&
       (psessionEntry->gLimEdcaParams[upToAc(addts.tspec.tsinfo.traffic.userPrio)].aci.acm)))
    {
        retval = limSendHalMsgAddTs(pMac, pSta->staIndex, tspecInfo->idx, addts.tspec, psessionEntry->peSessionId);
        if(eSIR_SUCCESS != retval)
        {
            limAdmitControlDeleteTS(pMac, pSta->assocId, &addts.tspec.tsinfo, NULL, &tspecInfo->idx);
    
            // Send DELTS action frame to AP        
            cfgLen = sizeof(tSirMacAddr);
            limSendDeltsReqActionFrame(pMac, peerMacAddr, rspReqd, &addts.tspec.tsinfo, &addts.tspec,
                    psessionEntry);
            limSendSmeAddtsRsp(pMac, true, retval, psessionEntry, addts.tspec,
                    psessionEntry->smeSessionId, psessionEntry->transactionId);
            pMac->lim.gLimAddtsSent = false;
            return;
        }
        PELOGW(limLog(pMac, LOGW, FL("AddTsRsp received successfully(UP %d, TSID %d)\n"),
           addts.tspec.tsinfo.traffic.userPrio, addts.tspec.tsinfo.traffic.tsid);)
    }
    else
    {
        PELOGW(limLog(pMac, LOGW, FL("AddTsRsp received successfully(UP %d, TSID %d)\n"),
               addts.tspec.tsinfo.traffic.userPrio, addts.tspec.tsinfo.traffic.tsid);)
        PELOGW(limLog(pMac, LOGW, FL("no ACM: Bypass sending WDA_ADD_TS_REQ to HAL \n"));)
        // Use the smesessionId and smetransactionId from the PE session context
        limSendSmeAddtsRsp(pMac, true, eSIR_SME_SUCCESS, psessionEntry, addts.tspec,
                psessionEntry->smeSessionId, psessionEntry->transactionId);
    }

    // clear the addts flag
    pMac->lim.gLimAddtsSent = false;
    return;
}


static void
__limProcessDelTsReq(tpAniSirGlobal pMac, tANI_U8 *pRxPacketInfo,tpPESession psessionEntry)
{
    tSirRetStatus    retval;
    tSirDeltsReqInfo delts;
    tpSirMacMgmtHdr  pHdr;
    tpDphHashNode    pSta;
    tANI_U32              frameLen;
    tANI_U16              aid;
    tANI_U8              *pBody;
    tANI_U8               tsStatus;
    tSirMacTSInfo   *tsinfo;
    tANI_U8 tspecIdx;
    tANI_U8  ac;
    tpDphHashNode  pStaDs = NULL;


    pHdr = WDA_GET_RX_MAC_HEADER(pRxPacketInfo);
    pBody = WDA_GET_RX_MPDU_DATA(pRxPacketInfo);
    frameLen = WDA_GET_RX_PAYLOAD_LEN(pRxPacketInfo);

    pSta = dphLookupHashEntry(pMac, pHdr->sa, &aid, &psessionEntry->dph.dphHashTable);
    if (pSta == NULL)
    {
        PELOGE(limLog(pMac, LOGE, FL("Station context not found - ignoring DelTs\n"));)
        return;
    }

    // parse the delts request
    retval = sirConvertDeltsReq2Struct(pMac, pBody, frameLen, &delts);
    if (retval != eSIR_SUCCESS)
    {
        PELOGW(limLog(pMac, LOGW, FL("DelTs parsing failed (error %d)\n"), retval);)
        return;
    }

    if (delts.wmeTspecPresent)
    {
        if ((!psessionEntry->limWmeEnabled) || (! pSta->wmeEnabled))
        {
            PELOGW(limLog(pMac, LOGW, FL("Ignoring delts request: wme not enabled/capable\n"));)
            return;
        }
        PELOG2(limLog(pMac, LOG2, FL("WME Delts received\n"));)
    }
    else if ((psessionEntry->limQosEnabled) && pSta->lleEnabled)
        {
        PELOG2(limLog(pMac, LOG2, FL("11e QoS Delts received\n"));)
        }
    else if ((psessionEntry->limWsmEnabled) && pSta->wsmEnabled)
        {
        PELOG2(limLog(pMac, LOG2, FL("WSM Delts received\n"));)
        }
    else
    {
        PELOGW(limLog(pMac, LOGW, FL("Ignoring delts request: qos not enabled/capable\n"));)
        return;
    }

    tsinfo = delts.wmeTspecPresent ? &delts.tspec.tsinfo : &delts.tsinfo;

    // if no Admit Control, ignore the request
    if ((tsinfo->traffic.accessPolicy == SIR_MAC_ACCESSPOLICY_EDCA))
    {
    
#if(defined(ANI_PRODUCT_TYPE_AP) || defined(ANI_PRODUCT_TYPE_AP_SDK))
        if ((psessionEntry->limSystemRole == eLIM_AP_ROLE &&
        (! psessionEntry->gLimEdcaParamsBC[upToAc(tsinfo->traffic.userPrio)].aci.acm)) ||
        (psessionEntry->limSystemRole != eLIM_AP_ROLE &&
        (! psessionEntry->gLimEdcaParams[upToAc(tsinfo->traffic.userPrio)].aci.acm)))
#else
        if ((upToAc(tsinfo->traffic.userPrio) >= MAX_NUM_AC) || (! psessionEntry->gLimEdcaParams[upToAc(tsinfo->traffic.userPrio)].aci.acm))
#endif
        {
            limLog(pMac, LOGW, FL("DelTs with UP %d has no AC - ignoring request\n"),
                   tsinfo->traffic.userPrio);
            return;
        }
    }

    // try to delete the TS
    if (eSIR_SUCCESS != limAdmitControlDeleteTS(pMac, pSta->assocId, tsinfo, &tsStatus, &tspecIdx))
    {
        PELOGW(limLog(pMac, LOGW, FL("Unable to Delete TS\n"));)
        return;
    }

    else if ((tsinfo->traffic.accessPolicy == SIR_MAC_ACCESSPOLICY_HCCA) ||
             (tsinfo->traffic.accessPolicy == SIR_MAC_ACCESSPOLICY_BOTH))
    {
      //Edca only for now.
    }
    else
    {
      //send message to HAL to delete TS
      if(eSIR_SUCCESS != limSendHalMsgDelTs(pMac, pSta->staIndex, tspecIdx, delts))
      {
        limLog(pMac, LOGW, FL("DelTs with UP %d failed in limSendHalMsgDelTs - ignoring request\n"),
                         tsinfo->traffic.userPrio);
         return;
      }
    }

    /* We successfully deleted the TSPEC. Update the dynamic UAPSD Mask.
     * The AC for this TSPEC is no longer trigger enabled if this Tspec
     * was set-up in uplink direction only.
     * The AC for this TSPEC is no longer delivery enabled if this Tspec
     * was set-up in downlink direction only.
     * The AC for this TSPEC is no longer triiger enabled and delivery 
     * enabled if this Tspec was a bidirectional TSPEC.
     */
    limSetTspecUapsdMask(pMac, tsinfo, CLEAR_UAPSD_MASK);


    /* We're deleting the TSPEC.
     * The AC for this TSPEC is no longer admitted in uplink/downlink direction
     * if this TSPEC was set-up in uplink/downlink direction only.
     * The AC for this TSPEC is no longer admitted in both uplink and downlink
     * directions if this TSPEC was a bi-directional TSPEC.
     * If ACM is set for this AC and this AC is admitted only in downlink
     * direction, PE needs to downgrade the EDCA parameter 
     * (for the AC for which TS is being deleted) to the
     * next best AC for which ACM is not enabled, and send the
     * updated values to HAL. 
     */ 
    ac = upToAc(tsinfo->traffic.userPrio);

    if(tsinfo->traffic.direction == SIR_MAC_DIRECTION_UPLINK)
    {
      pMac->lim.gAcAdmitMask[SIR_MAC_DIRECTION_UPLINK] &= ~(1 << ac);
    }
    else if(tsinfo->traffic.direction == SIR_MAC_DIRECTION_DNLINK)
    {
      pMac->lim.gAcAdmitMask[SIR_MAC_DIRECTION_DNLINK] &= ~(1 << ac);
    }
    else if(tsinfo->traffic.direction == SIR_MAC_DIRECTION_BIDIR)
    {
      pMac->lim.gAcAdmitMask[SIR_MAC_DIRECTION_UPLINK] &= ~(1 << ac);
      pMac->lim.gAcAdmitMask[SIR_MAC_DIRECTION_DNLINK] &= ~(1 << ac);
    }

    limSetActiveEdcaParams(pMac, psessionEntry->gLimEdcaParams, psessionEntry);

    pStaDs = dphGetHashEntry(pMac, DPH_STA_HASH_INDEX_PEER, &psessionEntry->dph.dphHashTable);
    if (pStaDs != NULL)
    {
        if (pStaDs->aniPeer == eANI_BOOLEAN_TRUE) 
            limSendEdcaParams(pMac, psessionEntry->gLimEdcaParamsActive, pStaDs->bssId, eANI_BOOLEAN_TRUE);
        else
            limSendEdcaParams(pMac, psessionEntry->gLimEdcaParamsActive, pStaDs->bssId, eANI_BOOLEAN_FALSE);
    }
    else
        limLog(pMac, LOGE, FL("Self entry missing in Hash Table \n"));

    PELOG1(limLog(pMac, LOG1, FL("DeleteTS succeeded\n"));)
    if((psessionEntry->limSystemRole != eLIM_AP_ROLE)&&(psessionEntry->limSystemRole != eLIM_BT_AMP_AP_ROLE))
      limSendSmeDeltsInd(pMac, &delts, aid,psessionEntry);

#ifdef FEATURE_WLAN_CCX
    limDeactivateAndChangeTimer(pMac,eLIM_TSM_TIMER);
#endif

}


#ifdef ANI_SUPPORT_11H
/**
 *  limProcessBasicMeasReq
 *
 *FUNCTION:
 * This function is called by limProcessMeasurementRequestFrame()
 * when it received a Basic measurement Request action frame.
 * Station/BP receiving this should perform basic measurements
 * and then send Basic Measurement Report. AP should not perform
 * any measurements, and send report indicating refusal.
 *
 *LOGIC:
 *
 *ASSUMPTIONS:
 *
 *NOTE:
 *
 * @param  pMac - Pointer to Global MAC structure
 * @param  pMeasReqFrame - A pointer to Basic Meas. Req structure
 * @return None
 */
static void
__limProcessBasicMeasReq(tpAniSirGlobal pMac,
                       tpSirMacMeasReqActionFrame pMeasReqFrame,
                       tSirMacAddr peerMacAddr)
{
    // TBD - Station shall perform basic measurements

    if (limSendMeasReportFrame(pMac,
                               pMeasReqFrame,
                               peerMacAddr) != eSIR_SUCCESS)
    {
        PELOGE(limLog(pMac, LOGE, FL("fail to send Basic Meas report \n"));)
        return;
    }
}


/**
 *  limProcessCcaMeasReq
 *
 *FUNCTION:
 * This function is called by limProcessMeasurementRequestFrame()
 * when it received a CCA measurement Request action frame.
 * Station/BP receiving this should perform CCA measurements
 * and then send CCA Measurement Report. AP should not perform
 * any measurements, and send report indicating refusal.
 *
 *LOGIC:
 *
 *ASSUMPTIONS:
 *
 *NOTE:
 *
 * @param  pMac - Pointer to Global MAC structure
 * @param  pMeasReqFrame - A pointer to CCA Meas. Req structure
 * @return None
 */
static void
__limProcessCcaMeasReq(tpAniSirGlobal pMac,
                     tpSirMacMeasReqActionFrame pMeasReqFrame,
                     tSirMacAddr peerMacAddr)
{
    // TBD - Station shall perform cca measurements

    if (limSendMeasReportFrame(pMac,
                               pMeasReqFrame,
                               peerMacAddr) != eSIR_SUCCESS)
    {
        PELOGE(limLog(pMac, LOGE, FL("fail to send CCA Meas report \n"));)
        return;
    }
}


/**
 *  __limProcessRpiMeasReq
 *
 *FUNCTION:
 * This function is called by limProcessMeasurementRequestFrame()
 * when it received a RPI measurement Request action frame.
 * Station/BP/AP  receiving this shall not perform any measurements,
 * and send report indicating refusal.
 *
 *LOGIC:
 *
 *ASSUMPTIONS:
 *
 *NOTE:
 *
 * @param  pMac - Pointer to Global MAC structure
 * @param  pMeasReqFrame - A pointer to RPI Meas. Req structure
 * @return None
 */
static void
__limProcessRpiMeasReq(tpAniSirGlobal pMac,
                     tpSirMacMeasReqActionFrame pMeasReqFrame,
                     tSirMacAddr peerMacAddr)
{
    if (limSendMeasReportFrame(pMac,
                               pMeasReqFrame,
                               peerMacAddr) != eSIR_SUCCESS)
    {
        PELOGE(limLog(pMac, LOGE, FL("fail to send RPI Meas report \n"));)
        return;
    }
}


/**
 *  __limProcessMeasurementRequestFrame
 *
 *FUNCTION:
 *
 *LOGIC:
 *
 *ASSUMPTIONS:
 *
 *NOTE:
 *
 * @param  pMac - Pointer to Global MAC structure
 * @param  *pRxPacketInfo - A pointer to packet info structure
 * @return None
 */

static void
__limProcessMeasurementRequestFrame(tpAniSirGlobal pMac, tANI_U8 *pRxPacketInfo)
{
    tpSirMacMgmtHdr                       pHdr;
    tANI_U8                                    *pBody;
    tpSirMacMeasReqActionFrame            pMeasReqFrame;
    tANI_U32                                   frameLen;

    pHdr = WDA_GET_RX_MAC_HEADER(pRxPacketInfo);
    pBody = WDA_GET_RX_MPDU_DATA(pRxPacketInfo);
    frameLen = WDA_GET_RX_PAYLOAD_LEN(pRxPacketInfo);

    if ( eHAL_STATUS_SUCCESS !=
        palAllocateMemory( pMac->hHdd, (void **)&pMeasReqFrame, sizeof( tSirMacMeasReqActionFrame ) ) )
    {
        limLog(pMac, LOGE,
            FL("limProcessMeasurementRequestFrame: palAllocateMemory failed \n"));
        return;
    }

    if (sirConvertMeasReqFrame2Struct(pMac, pBody, pMeasReqFrame, frameLen) !=
        eSIR_SUCCESS)
    {
        PELOGW(limLog(pMac, LOGW, FL("Rcv invalid Measurement Request Action Frame \n"));)
        return;
    }


    switch(pMeasReqFrame->measReqIE.measType)
    {
        case SIR_MAC_BASIC_MEASUREMENT_TYPE:
            __limProcessBasicMeasReq(pMac, pMeasReqFrame, pHdr->sa);
            break;

        case SIR_MAC_CCA_MEASUREMENT_TYPE:
            __limProcessCcaMeasReq(pMac, pMeasReqFrame, pHdr->sa);
            break;

        case SIR_MAC_RPI_MEASUREMENT_TYPE:
            __limProcessRpiMeasReq(pMac, pMeasReqFrame, pHdr->sa);
            break;

        default:
            PELOG1(limLog(pMac, LOG1, FL("Unknown Measurement Type %d \n"),
                   pMeasReqFrame->measReqIE.measType);)
            break;
    }

} /*** end limProcessMeasurementRequestFrame ***/


/**
 *  limProcessTpcRequestFrame
 *
 *FUNCTION:
 *  This function is called upon receiving Tpc Request frame.
 *
 *NOTE:
 *
 * @param  pMac - Pointer to Global MAC structure
 * @param  *pRxPacketInfo - A pointer to packet info structure
 * @return None
 */

static void
__limProcessTpcRequestFrame(tpAniSirGlobal pMac, tANI_U8 *pRxPacketInfo)
{
    tpSirMacMgmtHdr                       pHdr;
    tANI_U8                                    *pBody;
    tpSirMacTpcReqActionFrame             pTpcReqFrame;
    tANI_U32                                   frameLen;

    pHdr = WDA_GET_RX_MAC_HEADER(pRxPacketInfo);
    pBody = WDA_GET_RX_MPDU_DATA(pRxPacketInfo);
    frameLen = WDA_GET_RX_PAYLOAD_LEN(pRxPacketInfo);

    PELOG1(limLog(pMac, LOG1, FL("****LIM: Processing TPC Request from peer ****"));)

    if ( eHAL_STATUS_SUCCESS !=
        palAllocateMemory( pMac->hHdd, (void **)&pTpcReqFrame, sizeof( tSirMacTpcReqActionFrame ) ) )
    {
        PELOGE(limLog(pMac, LOGE, FL("palAllocateMemory failed \n"));)
        return;
    }

    if (sirConvertTpcReqFrame2Struct(pMac, pBody, pTpcReqFrame, frameLen) !=
        eSIR_SUCCESS)
    {
        PELOGW(limLog(pMac, LOGW, FL("Rcv invalid TPC Req Action Frame \n"));)
        return;
    }

    if (limSendTpcReportFrame(pMac,
                              pTpcReqFrame,
                              pHdr->sa) != eSIR_SUCCESS)
    {
        PELOGE(limLog(pMac, LOGE, FL("fail to send TPC Report Frame. \n"));)
        return;
    }
}
#endif


/**
 * \brief Validate an ADDBA Req from peer with respect
 * to our own BA configuration
 *
 * \sa __limValidateAddBAParameterSet
 *
 * \param pMac The global tpAniSirGlobal object
 *
 * \param baParameterSet The ADDBA Parameter Set.
 *
 * \param pDelBAFlag this parameter is NULL except for call from processAddBAReq
 *        delBAFlag is set when entry already exists.
 *
 * \param reqType ADDBA Req v/s ADDBA Rsp
 * 1 - ADDBA Req
 * 0 - ADDBA Rsp
 *
 * \return eSIR_SUCCESS if setup completes successfully
 *         eSIR_FAILURE is some problem is encountered
 */

static tSirMacStatusCodes
__limValidateAddBAParameterSet( tpAniSirGlobal pMac,
    tpDphHashNode pSta,
    tDot11fFfAddBAParameterSet baParameterSet,
    tANI_U8 dialogueToken,
    tLimAddBaValidationReqType reqType ,
    tANI_U8* pDelBAFlag /*this parameter is NULL except for call from processAddBAReq*/)
{
  if(baParameterSet.tid >= STACFG_MAX_TC)
  {
      return eSIR_MAC_WME_INVALID_PARAMS_STATUS;
  }

  //check if there is already a BA session setup with this STA/TID while processing AddBaReq
  if((true == pSta->tcCfg[baParameterSet.tid].fUseBARx) &&
        (LIM_ADDBA_REQ == reqType))
  {
      //There is already BA session setup for STA/TID.
      limLog( pMac, LOGW,
          FL( "AddBAReq rcvd when there is already a session for this StaId = %d, tid = %d\n " ),
          pSta->staIndex, baParameterSet.tid);
      limPrintMacAddr( pMac, pSta->staAddr, LOGW );

      if(pDelBAFlag)
        *pDelBAFlag = true;
  }
  return eSIR_MAC_SUCCESS_STATUS;
}

/**
 * \brief Validate a DELBA Ind from peer with respect
 * to our own BA configuration
 *
 * \sa __limValidateDelBAParameterSet
 *
 * \param pMac The global tpAniSirGlobal object
 *
 * \param baParameterSet The DELBA Parameter Set.
 *
 * \param pSta Runtime, STA-related configuration cached
 * in the HashNode object
 *
 * \return eSIR_SUCCESS if setup completes successfully
 *         eSIR_FAILURE is some problem is encountered
 */
static tSirMacStatusCodes 
__limValidateDelBAParameterSet( tpAniSirGlobal pMac,
    tDot11fFfDelBAParameterSet baParameterSet,
    tpDphHashNode pSta )
{
tSirMacStatusCodes statusCode = eSIR_MAC_STA_BLK_ACK_NOT_SUPPORTED_STATUS;

  // Validate if a BA is active for the requested TID
    if( pSta->tcCfg[baParameterSet.tid].fUseBATx ||
        pSta->tcCfg[baParameterSet.tid].fUseBARx )
    {
      statusCode = eSIR_MAC_SUCCESS_STATUS;

      limLog( pMac, LOGW,
          FL("Valid DELBA Ind received. Time to send WDA_DELBA_IND to HAL...\n"));
    }
    else
      limLog( pMac, LOGW,
          FL("Received an INVALID DELBA Ind for TID %d...\n"),
          baParameterSet.tid );

  return statusCode;
}

/**
 * \brief Process an ADDBA REQ
 *
 * \sa limProcessAddBAReq
 *
 * \param pMac The global tpAniSirGlobal object
 *
 * \param pRxPacketInfo Handle to the Rx packet info from HDD
 *
 * \return none
 *
 */
static void
__limProcessAddBAReq( tpAniSirGlobal pMac, tANI_U8 *pRxPacketInfo,tpPESession psessionEntry)
{
    tDot11fAddBAReq frmAddBAReq;
    tpSirMacMgmtHdr pHdr;
    tpDphHashNode pSta;
    tSirMacStatusCodes status = eSIR_MAC_SUCCESS_STATUS;
    tANI_U16 aid;
    tANI_U32 frameLen, nStatus;
    tANI_U8 *pBody;
    tANI_U8 delBAFlag =0;

    pHdr = WDA_GET_RX_MAC_HEADER( pRxPacketInfo );
    pBody = WDA_GET_RX_MPDU_DATA( pRxPacketInfo );
    frameLen = WDA_GET_RX_PAYLOAD_LEN( pRxPacketInfo );

    // Unpack the received frame
    nStatus = dot11fUnpackAddBAReq( pMac, pBody, frameLen, &frmAddBAReq );
    if( DOT11F_FAILED( nStatus ))
    {
        limLog( pMac, LOGE,
            FL("Failed to unpack and parse an ADDBA Request (0x%08x, %d bytes):\n"),
            nStatus,
            frameLen );

        PELOG2(sirDumpBuf( pMac, SIR_DBG_MODULE_ID, LOG2, pBody, frameLen );)

        // Without an unpacked request we cannot respond, so silently ignore the request
        return;
    }
    else if ( DOT11F_WARNED( nStatus ) )
    {
        limLog( pMac, LOGW,
            FL( "There were warnings while unpacking an ADDBA Request (0x%08x, %d bytes):\n"),
            nStatus,
            frameLen );

        PELOG2(sirDumpBuf( pMac, SIR_DBG_MODULE_ID, LOG2, pBody, frameLen );)
    }

    pSta = dphLookupHashEntry( pMac, pHdr->sa, &aid, &psessionEntry->dph.dphHashTable );
    if( pSta == NULL )
    {
        limLog( pMac, LOGE,
            FL( "STA context not found - ignoring ADDBA from \n" ));
        limPrintMacAddr( pMac, pHdr->sa, LOGW );

        // FIXME - Should we do this?
        status = eSIR_MAC_INABLITY_TO_CONFIRM_ASSOC_STATUS;
        goto returnAfterError;
    }

    limLog( pMac, LOGW,
      FL( "ADDBA Req from STA with AID %d, tid = %d\n" ),
      aid, frmAddBAReq.AddBAParameterSet.tid);

#ifdef WLAN_SOFTAP_VSTA_FEATURE
    // we can only do BA on "hard" STAs
    if (!(IS_HWSTA_IDX(pSta->staIndex)))
    {
        status = eSIR_MAC_REQ_DECLINED_STATUS;
        goto returnAfterError;
    }
#endif //WLAN_SOFTAP_VSTA_FEATURE


    // Now, validate the ADDBA Req
    if( eSIR_MAC_SUCCESS_STATUS !=
      (status = __limValidateAddBAParameterSet( pMac, pSta,
                                              frmAddBAReq.AddBAParameterSet,
                                              0, //dialogue token is don't care in request validation.
                                              LIM_ADDBA_REQ, &delBAFlag)))
        goto returnAfterError;

    //BA already set, so we need to delete it before adding new one.
    if(delBAFlag)
    {
        if( eSIR_SUCCESS != limPostMsgDelBAInd( pMac,
            pSta,
            (tANI_U8)frmAddBAReq.AddBAParameterSet.tid,
            eBA_RECIPIENT,psessionEntry))
        {
            status = eSIR_MAC_UNSPEC_FAILURE_STATUS;
            goto returnAfterError;
        }
    }

  // Check if the ADD BA Declined configuration is Disabled
    if ((pMac->lim.gAddBA_Declined & ( 1 << frmAddBAReq.AddBAParameterSet.tid ) )) {
        limLog( pMac, LOGE, FL( "Declined the ADDBA Req for the TID %d  \n" ),
                        frmAddBAReq.AddBAParameterSet.tid);
        status = eSIR_MAC_REQ_DECLINED_STATUS;
        goto returnAfterError;
    }

  //
  // Post WDA_ADDBA_REQ to HAL.
  // If HAL/HDD decide to allow this ADDBA Req session,
  // then this BA session is termed active
  //

  // Change the Block Ack state of this STA to wait for
  // ADDBA Rsp from HAL
  LIM_SET_STA_BA_STATE(pSta, frmAddBAReq.AddBAParameterSet.tid, eLIM_BA_STATE_WT_ADD_RSP);

  if( eSIR_SUCCESS != limPostMsgAddBAReq( pMac,
        pSta,
        (tANI_U8) frmAddBAReq.DialogToken.token,
        (tANI_U8) frmAddBAReq.AddBAParameterSet.tid,
        (tANI_U8) frmAddBAReq.AddBAParameterSet.policy,
        frmAddBAReq.AddBAParameterSet.bufferSize,
        frmAddBAReq.BATimeout.timeout,
        (tANI_U16) frmAddBAReq.BAStartingSequenceControl.ssn,
        eBA_RECIPIENT,psessionEntry))
    status = eSIR_MAC_UNSPEC_FAILURE_STATUS;
  else
    return;

returnAfterError:

  //
  // Package LIM_MLM_ADDBA_RSP to MLME, with proper
  // status code. MLME will then send an ADDBA RSP
  // over the air to the peer MAC entity
  //
  if( eSIR_SUCCESS != limPostMlmAddBARsp( pMac,
        pHdr->sa,
        status,
        frmAddBAReq.DialogToken.token,
        (tANI_U8) frmAddBAReq.AddBAParameterSet.tid,
        (tANI_U8) frmAddBAReq.AddBAParameterSet.policy,
        frmAddBAReq.AddBAParameterSet.bufferSize,
        frmAddBAReq.BATimeout.timeout,psessionEntry))
  {
    limLog( pMac, LOGW,
        FL( "Failed to post LIM_MLM_ADDBA_RSP to " ));
    limPrintMacAddr( pMac, pHdr->sa, LOGW );
  }

}

/**
 * \brief Process an ADDBA RSP
 *
 * \sa limProcessAddBARsp
 *
 * \param pMac The global tpAniSirGlobal object
 *
 * \param pRxPacketInfo Handle to the packet info structure from HDD
 *
 * \return none
 *
 */
static void
__limProcessAddBARsp( tpAniSirGlobal pMac, tANI_U8 *pRxPacketInfo,tpPESession psessionEntry)
{
tDot11fAddBARsp frmAddBARsp;
tpSirMacMgmtHdr pHdr;
tpDphHashNode pSta;
tSirMacReasonCodes reasonCode = eSIR_MAC_UNSPEC_FAILURE_REASON;
tANI_U16 aid;
tANI_U32 frameLen, nStatus;
tANI_U8 *pBody;

  pHdr = WDA_GET_RX_MAC_HEADER( pRxPacketInfo );
  pBody = WDA_GET_RX_MPDU_DATA( pRxPacketInfo );
  frameLen = WDA_GET_RX_PAYLOAD_LEN( pRxPacketInfo );

  pSta = dphLookupHashEntry( pMac, pHdr->sa, &aid, &psessionEntry->dph.dphHashTable );
  if( pSta == NULL )
  {
    limLog( pMac, LOGE,
        FL( "STA context not found - ignoring ADDBA from \n" ));
    limPrintMacAddr( pMac, pHdr->sa, LOGW );
    return;
  }

#ifdef WLAN_SOFTAP_VSTA_FEATURE
  // We can only do BA on "hard" STAs.  We should not have issued an ADDBA
  // Request, so we should never be processing a ADDBA Response
  if (!(IS_HWSTA_IDX(pSta->staIndex)))
  {
    return;
  }
#endif //WLAN_SOFTAP_VSTA_FEATURE

  // Unpack the received frame
  nStatus = dot11fUnpackAddBARsp( pMac, pBody, frameLen, &frmAddBARsp );
  if( DOT11F_FAILED( nStatus ))
  {
    limLog( pMac, LOGE,
        FL( "Failed to unpack and parse an ADDBA Response (0x%08x, %d bytes):\n"),
        nStatus,
        frameLen );

    PELOG2(sirDumpBuf( pMac, SIR_DBG_MODULE_ID, LOG2, pBody, frameLen );)
    goto returnAfterError;
  }
  else if ( DOT11F_WARNED( nStatus ) )
  {
    limLog( pMac, LOGW,
        FL( "There were warnings while unpacking an ADDBA Response (0x%08x, %d bytes):\n"),
        nStatus,
        frameLen );

    PELOG2(sirDumpBuf( pMac, SIR_DBG_MODULE_ID, LOG2, pBody, frameLen );)
  }

  limLog( pMac, LOGW,
      FL( "ADDBA Rsp from STA with AID %d, tid = %d, status = %d\n" ),
      aid, frmAddBARsp.AddBAParameterSet.tid, frmAddBARsp.Status.status);

  //if there is no matchin dialougue token then ignore the response.

  if(eSIR_SUCCESS != limSearchAndDeleteDialogueToken(pMac, frmAddBARsp.DialogToken.token,
        pSta->assocId, frmAddBARsp.AddBAParameterSet.tid))
  {
      PELOGW(limLog(pMac, LOGW, FL("dialogueToken in received addBARsp did not match with outstanding requests\n"));)
      return;
  }

  // Check first if the peer accepted the ADDBA Req
  if( eSIR_MAC_SUCCESS_STATUS == frmAddBARsp.Status.status )
  {
    //if peer responded with buffer size 0 then we should pick the default.
    if(0 == frmAddBARsp.AddBAParameterSet.bufferSize)
        frmAddBARsp.AddBAParameterSet.bufferSize = BA_DEFAULT_TX_BUFFER_SIZE;

    // Now, validate the ADDBA Rsp
    if( eSIR_MAC_SUCCESS_STATUS !=
        __limValidateAddBAParameterSet( pMac, pSta,
                                       frmAddBARsp.AddBAParameterSet,
                                       (tANI_U8)frmAddBARsp.DialogToken.token,
                                       LIM_ADDBA_RSP, NULL))
      goto returnAfterError;
  }
  else
    goto returnAfterError;

  // Change STA state to wait for ADDBA Rsp from HAL
  LIM_SET_STA_BA_STATE(pSta, frmAddBARsp.AddBAParameterSet.tid, eLIM_BA_STATE_WT_ADD_RSP);

  //
  // Post WDA_ADDBA_REQ to HAL.
  // If HAL/HDD decide to allow this ADDBA Rsp session,
  // then this BA session is termed active
  //

  if( eSIR_SUCCESS != limPostMsgAddBAReq( pMac,
        pSta,
        (tANI_U8) frmAddBARsp.DialogToken.token,
        (tANI_U8) frmAddBARsp.AddBAParameterSet.tid,
        (tANI_U8) frmAddBARsp.AddBAParameterSet.policy,
        frmAddBARsp.AddBAParameterSet.bufferSize,
        frmAddBARsp.BATimeout.timeout,
        0,
        eBA_INITIATOR,psessionEntry))
    reasonCode = eSIR_MAC_UNSPEC_FAILURE_REASON;
  else
    return;

returnAfterError:

  // TODO: Do we need to signal an error status to SME,
  // if status != eSIR_MAC_SUCCESS_STATUS

  // Restore STA "BA" State
  LIM_SET_STA_BA_STATE(pSta, frmAddBARsp.AddBAParameterSet.tid, eLIM_BA_STATE_IDLE);
  //
  // Need to send a DELBA IND to peer, who
  // would have setup a BA session with this STA
  //
  if( eSIR_MAC_SUCCESS_STATUS == frmAddBARsp.Status.status )
  {
    //
    // Package LIM_MLM_DELBA_REQ to MLME, with proper
    // status code. MLME will then send a DELBA IND
    // over the air to the peer MAC entity
    //
    if( eSIR_SUCCESS != limPostMlmDelBAReq( pMac,
          pSta,
          eBA_INITIATOR,
          (tANI_U8) frmAddBARsp.AddBAParameterSet.tid,
          reasonCode, psessionEntry))
    {
      limLog( pMac, LOGW,
          FL( "Failed to post LIM_MLM_DELBA_REQ to " ));
      limPrintMacAddr( pMac, pHdr->sa, LOGW );
    }
  }
}

/**
 * \brief Process a DELBA Indication
 *
 * \sa limProcessDelBAInd
 *
 * \param pMac The global tpAniSirGlobal object
 *
 * \param pRxPacketInfo Handle to the Rx packet info from HDD
 *
 * \return none
 *
 */
static void
__limProcessDelBAReq( tpAniSirGlobal pMac, tANI_U8 *pRxPacketInfo,tpPESession psessionEntry)
{
tDot11fDelBAInd frmDelBAInd;
tpSirMacMgmtHdr pHdr;
tpDphHashNode pSta;
tANI_U16 aid;
tANI_U32 frameLen, nStatus;
tANI_U8 *pBody;

  pHdr = WDA_GET_RX_MAC_HEADER( pRxPacketInfo );
  pBody = WDA_GET_RX_MPDU_DATA( pRxPacketInfo );
  frameLen = WDA_GET_RX_PAYLOAD_LEN( pRxPacketInfo );

  pSta = dphLookupHashEntry( pMac, pHdr->sa, &aid, &psessionEntry->dph.dphHashTable );
  if( pSta == NULL )
  {
    limLog( pMac, LOGE, FL( "STA context not found - ignoring DELBA from \n"));
    limPrintMacAddr( pMac, pHdr->sa, LOGW );
    return;
  }

  limLog( pMac, LOG1, FL( "DELBA Ind from STA with AID %d\n" ), aid );

  // Unpack the received frame
  nStatus = dot11fUnpackDelBAInd( pMac, pBody, frameLen, &frmDelBAInd );
  if( DOT11F_FAILED( nStatus ))
  {
    limLog( pMac, LOGE,
        FL( "Failed to unpack and parse a DELBA Indication (0x%08x, %d bytes):\n"),
        nStatus,
        frameLen );

    PELOG2(sirDumpBuf( pMac, SIR_DBG_MODULE_ID, LOG2, pBody, frameLen );)
    return;
  }
  else if ( DOT11F_WARNED( nStatus ) )
  {
    limLog( pMac, LOGW,
        FL( "There were warnings while unpacking a DELBA Indication (0x%08x, %d bytes):\n"),
        nStatus,
        frameLen );

    PELOG2(sirDumpBuf( pMac, SIR_DBG_MODULE_ID, LOG2, pBody, frameLen );)
  }

  limLog( pMac, LOGW,
      FL( "Received DELBA for TID %d, Reason code %d\n" ),
      frmDelBAInd.DelBAParameterSet.tid,
      frmDelBAInd.Reason.code );

  // Now, validate the DELBA Ind
  if( eSIR_MAC_SUCCESS_STATUS != __limValidateDelBAParameterSet( pMac,
                                             frmDelBAInd.DelBAParameterSet,
                                             pSta ))
      return;

  //
  // Post WDA_DELBA_IND to HAL and delete the
  // existing BA session
  //
  // NOTE - IEEE 802.11-REVma-D8.0, Section 7.3.1.16
  // is kind of confusing...
  //
  if( eSIR_SUCCESS != limPostMsgDelBAInd( pMac,
        pSta,
        (tANI_U8) frmDelBAInd.DelBAParameterSet.tid,
        (eBA_RECIPIENT == frmDelBAInd.DelBAParameterSet.initiator)?
          eBA_INITIATOR: eBA_RECIPIENT,psessionEntry))
    limLog( pMac, LOGE, FL( "Posting WDA_DELBA_IND to HAL failed \n"));

  return;

}

static void
__limProcessSMPowerSaveUpdate(tpAniSirGlobal pMac, tANI_U8 *pRxPacketInfo ,tpPESession psessionEntry)
{

#if 0
        tpSirMacMgmtHdr                           pHdr;
        tDot11fSMPowerSave                    frmSMPower;
        tSirMacHTMIMOPowerSaveState  state;
        tpDphHashNode                             pSta;
        tANI_U16                                        aid;
        tANI_U32                                        frameLen, nStatus;
        tANI_U8                                          *pBody;

        pHdr = SIR_MAC_BD_TO_MPDUHEADER( pBd );
        pBody = SIR_MAC_BD_TO_MPDUDATA( pBd );
        frameLen = SIR_MAC_BD_TO_PAYLOAD_LEN( pBd );

        pSta = dphLookupHashEntry(pMac, pHdr->sa, &aid, &psessionEntry->dph.dphHashTable );
        if( pSta == NULL ) {
            limLog( pMac, LOGE,FL( "STA context not found - ignoring UpdateSM PSave Mode from \n" ));
            limPrintMacAddr( pMac, pHdr->sa, LOGW );
            return;
        }

        /**Unpack the received frame */
        nStatus = dot11fUnpackSMPowerSave( pMac, pBody, frameLen, &frmSMPower);

        if( DOT11F_FAILED( nStatus )) {
            limLog( pMac, LOGE, FL( "Failed to unpack and parse a Update SM Power (0x%08x, %d bytes):\n"),
                                                    nStatus, frameLen );
            PELOG2(sirDumpBuf( pMac, SIR_DBG_MODULE_ID, LOG2, pBody, frameLen );)
            return;
        }else if ( DOT11F_WARNED( nStatus ) ) {
            limLog(pMac, LOGW, FL( "There were warnings while unpacking a SMPower Save update (0x%08x, %d bytes):\n"),
                                nStatus, frameLen );
            PELOG2(sirDumpBuf( pMac, SIR_DBG_MODULE_ID, LOG2, pBody, frameLen );)
        }

        limLog(pMac, LOGW, FL("Received SM Power save Mode update Frame with PS_Enable:%d"
                            "PS Mode: %d"), frmSMPower.SMPowerModeSet.PowerSave_En,
                                                    frmSMPower.SMPowerModeSet.Mode);

        /** Update in the DPH Table about the Update in the SM Power Save mode*/
        if (frmSMPower.SMPowerModeSet.PowerSave_En && frmSMPower.SMPowerModeSet.Mode)
            state = eSIR_HT_MIMO_PS_DYNAMIC;
        else if ((frmSMPower.SMPowerModeSet.PowerSave_En) && (frmSMPower.SMPowerModeSet.Mode ==0))
            state = eSIR_HT_MIMO_PS_STATIC;
        else if ((frmSMPower.SMPowerModeSet.PowerSave_En == 0) && (frmSMPower.SMPowerModeSet.Mode == 0))
            state = eSIR_HT_MIMO_PS_NO_LIMIT;
        else {
            PELOGW(limLog(pMac, LOGW, FL("Received SM Power save Mode update Frame with invalid mode"));)
            return;
        }

        if (state == pSta->htMIMOPSState) {
            PELOGE(limLog(pMac, LOGE, FL("The PEER is already set in the same mode"));)
            return;
        }

        /** Update in the HAL Station Table for the Update of the Protection Mode */
        pSta->htMIMOPSState = state;
        limPostSMStateUpdate(pMac,pSta->staIndex, pSta->htMIMOPSState);

#endif
        
}

#if defined WLAN_FEATURE_VOWIFI

static void
__limProcessRadioMeasureRequest( tpAniSirGlobal pMac, tANI_U8 *pRxPacketInfo ,tpPESession psessionEntry )
{
     tpSirMacMgmtHdr                pHdr;
     tDot11fRadioMeasurementRequest frm;
     tANI_U32                       frameLen, nStatus;
     tANI_U8                        *pBody;

     pHdr = WDA_GET_RX_MAC_HEADER( pRxPacketInfo );
     pBody = WDA_GET_RX_MPDU_DATA( pRxPacketInfo );
     frameLen = WDA_GET_RX_PAYLOAD_LEN( pRxPacketInfo );

     if( psessionEntry == NULL )
     {
          return;
     }

     /**Unpack the received frame */
     nStatus = dot11fUnpackRadioMeasurementRequest( pMac, pBody, frameLen, &frm );

     if( DOT11F_FAILED( nStatus )) {
          limLog( pMac, LOGE, FL( "Failed to unpack and parse a Radio Measure request (0x%08x, %d bytes):\n"),
                    nStatus, frameLen );
          PELOG2(sirDumpBuf( pMac, SIR_DBG_MODULE_ID, LOG2, pBody, frameLen );)
               return;
     }else if ( DOT11F_WARNED( nStatus ) ) {
          limLog(pMac, LOGW, FL( "There were warnings while unpacking a Radio Measure request (0x%08x, %d bytes):\n"),
                    nStatus, frameLen );
          PELOG2(sirDumpBuf( pMac, SIR_DBG_MODULE_ID, LOG2, pBody, frameLen );)
     }

     // Call rrm function to handle the request.

     rrmProcessRadioMeasurementRequest( pMac, pHdr->sa, &frm, psessionEntry );
}

static void
__limProcessLinkMeasurementReq( tpAniSirGlobal pMac, tANI_U8 *pRxPacketInfo ,tpPESession psessionEntry )
{
     tpSirMacMgmtHdr               pHdr;
     tDot11fLinkMeasurementRequest frm;
     tANI_U32                      frameLen, nStatus;
     tANI_U8                       *pBody;

     pHdr = WDA_GET_RX_MAC_HEADER( pRxPacketInfo );
     pBody = WDA_GET_RX_MPDU_DATA( pRxPacketInfo );
     frameLen = WDA_GET_RX_PAYLOAD_LEN( pRxPacketInfo );

     if( psessionEntry == NULL )
     {
          return;
     }

     /**Unpack the received frame */
     nStatus = dot11fUnpackLinkMeasurementRequest( pMac, pBody, frameLen, &frm );

     if( DOT11F_FAILED( nStatus )) {
          limLog( pMac, LOGE, FL( "Failed to unpack and parse a Link Measure request (0x%08x, %d bytes):\n"),
                    nStatus, frameLen );
          PELOG2(sirDumpBuf( pMac, SIR_DBG_MODULE_ID, LOG2, pBody, frameLen );)
               return;
     }else if ( DOT11F_WARNED( nStatus ) ) {
          limLog(pMac, LOGW, FL( "There were warnings while unpacking a Link Measure request (0x%08x, %d bytes):\n"),
                    nStatus, frameLen );
          PELOG2(sirDumpBuf( pMac, SIR_DBG_MODULE_ID, LOG2, pBody, frameLen );)
     }

     // Call rrm function to handle the request.

     rrmProcessLinkMeasurementRequest( pMac, pRxPacketInfo, &frm, psessionEntry );

}

static void
__limProcessNeighborReport( tpAniSirGlobal pMac, tANI_U8 *pRxPacketInfo ,tpPESession psessionEntry )
{
     tpSirMacMgmtHdr               pHdr;
     tDot11fNeighborReportResponse frm;
     tANI_U32                      frameLen, nStatus;
     tANI_U8                       *pBody;

     pHdr = WDA_GET_RX_MAC_HEADER( pRxPacketInfo );
     pBody = WDA_GET_RX_MPDU_DATA( pRxPacketInfo );
     frameLen = WDA_GET_RX_PAYLOAD_LEN( pRxPacketInfo );

     if( psessionEntry == NULL )
     {
          return;
     }

     /**Unpack the received frame */
     nStatus = dot11fUnpackNeighborReportResponse( pMac, pBody, frameLen, &frm );

     if( DOT11F_FAILED( nStatus )) {
          limLog( pMac, LOGE, FL( "Failed to unpack and parse a Neighbor report response (0x%08x, %d bytes):\n"),
                    nStatus, frameLen );
          PELOG2(sirDumpBuf( pMac, SIR_DBG_MODULE_ID, LOG2, pBody, frameLen );)
               return;
     }else if ( DOT11F_WARNED( nStatus ) ) {
          limLog(pMac, LOGW, FL( "There were warnings while unpacking a Neighbor report response (0x%08x, %d bytes):\n"),
                    nStatus, frameLen );
          PELOG2(sirDumpBuf( pMac, SIR_DBG_MODULE_ID, LOG2, pBody, frameLen );)
     }

     //Call rrm function to handle the request.
     rrmProcessNeighborReportResponse( pMac, &frm, psessionEntry ); 

}

#endif

#ifdef WLAN_FEATURE_11W
/**
 * limProcessActionFrame
 *
 *FUNCTION:
 * This function is called by limProcessActionFrame() upon
 * SA query request Action frame reception.
 *
 *LOGIC:
 *
 *ASSUMPTIONS:
 *
 *NOTE:
 *
 * @param  pMac - Pointer to Global MAC structure
 * @param  *pBd - A pointer to Buffer descriptor + associated PDUs
 * @return None
 */
static void __limProcessSAQueryRequestActionFrame(tpAniSirGlobal pMac, tANI_U8 *pRxPacketInfo,tpPESession psessionEntry)
{
    tpSirMacMgmtHdr     pHdr;
    tANI_U8             *pBody;
    tANI_U16            transId = 0;           

    /* Prima  --- Below Macro not available in prima 
       pHdr = SIR_MAC_BD_TO_MPDUHEADER(pBd);
       pBody = SIR_MAC_BD_TO_MPDUDATA(pBd); */

    pHdr = WDA_GET_RX_MAC_HEADER(pRxPacketInfo);
    pBody = WDA_GET_RX_MPDU_DATA(pRxPacketInfo);

    /*Extract 11w trsansId from SA query request action frame
      In SA query response action frame we will send same transId
      In SA query request action frame:
      Category       : 1 byte
      Action         : 1 byte
      Transaction ID : 2 bbytes */

    transId = pBody[2];
    transId = transId << 8;
    transId |= pBody[3];
    
    //Send 11w SA query response action frame
    if (limSendSaQueryResponseFrame(pMac,
                              transId,
                              pHdr->sa,psessionEntry) != eSIR_SUCCESS)
    {
        PELOGE(limLog(pMac, LOGE, FL("fail to send SA query response action frame. \n"));)
        return;
    }
}

#endif

/**
 * limProcessActionFrame
 *
 *FUNCTION:
 * This function is called by limProcessMessageQueue() upon
 * Action frame reception.
 *
 *LOGIC:
 *
 *ASSUMPTIONS:
 *
 *NOTE:
 *
 * @param  pMac - Pointer to Global MAC structure
 * @param  *pRxPacketInfo - A pointer to packet info structure
 * @return None
 */

void
limProcessActionFrame(tpAniSirGlobal pMac, tANI_U8 *pRxPacketInfo,tpPESession psessionEntry)
{
    tANI_U8 *pBody = WDA_GET_RX_MPDU_DATA(pRxPacketInfo);
    tpSirMacActionFrameHdr pActionHdr = (tpSirMacActionFrameHdr) pBody;

   
    switch (pActionHdr->category)
    {
        case SIR_MAC_ACTION_QOS_MGMT:
            if (psessionEntry->limQosEnabled)
            {
                switch (pActionHdr->actionID)
                {
                    case SIR_MAC_QOS_ADD_TS_REQ:
                        __limProcessAddTsReq(pMac, (tANI_U8 *) pRxPacketInfo,psessionEntry);
                        break;

                    case SIR_MAC_QOS_ADD_TS_RSP:
                        __limProcessAddTsRsp(pMac, (tANI_U8 *) pRxPacketInfo,psessionEntry);
                        break;

                    case SIR_MAC_QOS_DEL_TS_REQ:
                        __limProcessDelTsReq(pMac, (tANI_U8 *) pRxPacketInfo,psessionEntry);
                        break;

                    default:
                        PELOGE(limLog(pMac, LOGE, FL("Qos action %d not handled\n"), pActionHdr->actionID);)
                        break;
                }
                break ;
            }

           break;

        case SIR_MAC_ACTION_SPECTRUM_MGMT:
            switch (pActionHdr->actionID)
            {
#ifdef ANI_SUPPORT_11H
                case SIR_MAC_ACTION_MEASURE_REQUEST_ID:
                    if(psessionEntry->lim11hEnable)
                    {
                        __limProcessMeasurementRequestFrame(pMac, pRxPacketInfo);
                    }
                    break;

                case SIR_MAC_ACTION_TPC_REQUEST_ID:
                    if ((psessionEntry->limSystemRole == eLIM_STA_ROLE) ||
                        (pessionEntry->limSystemRole == eLIM_AP_ROLE))
                    {
                        if(psessionEntry->lim11hEnable)
                        {
                            __limProcessTpcRequestFrame(pMac, pRxPacketInfo);
                        }
                    }
                    break;

#endif
                case SIR_MAC_ACTION_CHANNEL_SWITCH_ID:
                    if (psessionEntry->limSystemRole == eLIM_STA_ROLE)
                    {
                        __limProcessChannelSwitchActionFrame(pMac, pRxPacketInfo,psessionEntry);
                    }
                    break;
                default:
                    PELOGE(limLog(pMac, LOGE, FL("Spectrum mgmt action id %d not handled\n"), pActionHdr->actionID);)
                    break;
            }
            break;

        case SIR_MAC_ACTION_WME:
            if (! psessionEntry->limWmeEnabled)
            {
                limLog(pMac, LOGW, FL("WME mode disabled - dropping action frame %d\n"),
                       pActionHdr->actionID);
                break;
            }
            switch(pActionHdr->actionID)
            {
                case SIR_MAC_QOS_ADD_TS_REQ:
                    __limProcessAddTsReq(pMac, (tANI_U8 *) pRxPacketInfo,psessionEntry);
                    break;

                case SIR_MAC_QOS_ADD_TS_RSP:
                    __limProcessAddTsRsp(pMac, (tANI_U8 *) pRxPacketInfo,psessionEntry);
                    break;

                case SIR_MAC_QOS_DEL_TS_REQ:
                    __limProcessDelTsReq(pMac, (tANI_U8 *) pRxPacketInfo,psessionEntry);
                    break;

                default:
                    PELOGE(limLog(pMac, LOGE, FL("WME action %d not handled\n"), pActionHdr->actionID);)
                    break;
            }
            break;

        case SIR_MAC_ACTION_BLKACK:
            // Determine the "type" of BA Action Frame
            switch(pActionHdr->actionID)
            {
              case SIR_MAC_BLKACK_ADD_REQ:
                __limProcessAddBAReq( pMac, (tANI_U8 *) pRxPacketInfo,psessionEntry);
                break;

              case SIR_MAC_BLKACK_ADD_RSP:
                __limProcessAddBARsp( pMac, (tANI_U8 *) pRxPacketInfo,psessionEntry);
                break;

              case SIR_MAC_BLKACK_DEL:
                __limProcessDelBAReq( pMac, (tANI_U8 *) pRxPacketInfo,psessionEntry);
                break;

              default:
                break;
            }

            break;
    case SIR_MAC_ACTION_HT:
        /** Type of HT Action to be performed*/
        switch(pActionHdr->actionID) {
        case SIR_MAC_SM_POWER_SAVE:
            __limProcessSMPowerSaveUpdate(pMac, (tANI_U8 *) pRxPacketInfo,psessionEntry);
            break;
        default:
            PELOGE(limLog(pMac, LOGE, FL("Action ID %d not handled in HT Action category\n"), pActionHdr->actionID);)
            break;
        }
        break;

#if defined WLAN_FEATURE_VOWIFI
    case SIR_MAC_ACTION_RRM:
        if( pMac->rrm.rrmPEContext.rrmEnable )
        {
            switch(pActionHdr->actionID) {
                case SIR_MAC_RRM_RADIO_MEASURE_REQ:
                    __limProcessRadioMeasureRequest( pMac, (tANI_U8 *) pRxPacketInfo, psessionEntry );
                    break;
                case SIR_MAC_RRM_LINK_MEASUREMENT_REQ:
                    __limProcessLinkMeasurementReq( pMac, (tANI_U8 *) pRxPacketInfo, psessionEntry );
                    break;
                case SIR_MAC_RRM_NEIGHBOR_RPT:   
                    __limProcessNeighborReport( pMac, (tANI_U8*) pRxPacketInfo, psessionEntry );
                    break;
                default:
                    PELOGE( limLog( pMac, LOGE, FL("Action ID %d not handled in RRM\n"), pActionHdr->actionID);)
                    break;

            }
        }
        else
        {
            // Else we will just ignore the RRM messages.
            PELOGE( limLog( pMac, LOGE, FL("RRM Action frame ignored as RRM is disabled in cfg\n"));)
        }
        break;
#endif
#if defined WLAN_FEATURE_P2P
    case SIR_MAC_ACTION_PUBLIC_USAGE:
        switch(pActionHdr->actionID) {
        case SIR_MAC_ACTION_VENDOR_SPECIFIC:
            {
              tpSirMacVendorSpecificPublicActionFrameHdr pPubAction = (tpSirMacVendorSpecificPublicActionFrameHdr) pActionHdr;
              tpSirMacMgmtHdr     pHdr;
              tANI_U32            frameLen;
              tANI_U8 P2POui[] = { 0x50, 0x6F, 0x9A, 0x09 };

              pHdr = WDA_GET_RX_MAC_HEADER(pRxPacketInfo);
              frameLen = WDA_GET_RX_PAYLOAD_LEN(pRxPacketInfo);

              //Check if it is a P2P public action frame.
              if( palEqualMemory( pMac->hHdd, pPubAction->Oui, P2POui, 4 ) )
              {
                 /* Forward to the SME to HDD to wpa_supplicant */
                 // type is ACTION
                 limSendSmeMgmtFrameInd(pMac, pHdr->fc.subType, 
                    (tANI_U8*)pHdr, frameLen + sizeof(tSirMacMgmtHdr), 0, 
                    WDA_GET_RX_CH( pRxPacketInfo ));
              }
              else
              {
                 limLog( pMac, LOGE, FL("Unhandled public action frame (Vendor specific). OUI %x %x %x %x\n"),
                      pPubAction->Oui[0], pPubAction->Oui[1], pPubAction->Oui[2], pPubAction->Oui[3] );
              }
           }
            break;

        default:
            PELOGE(limLog(pMac, LOGE, FL("Unhandled public action frame -- %x \n"), pActionHdr->actionID);)
            break;
        }
        break;
#endif

#ifdef WLAN_FEATURE_11W
    case SIR_MAC_ACTION_SA_QUERY:
    {
        /**11w SA query request action frame received**/
        __limProcessSAQueryRequestActionFrame(pMac,(tANI_U8*) pRxPacketInfo, psessionEntry );
        break;
     }
#endif

    default:
       PELOGE(limLog(pMac, LOGE, FL("Action category %d not handled\n"), pActionHdr->category);)
       break;
    }
}

#if defined WLAN_FEATURE_P2P
/**
 * limProcessActionFrameNoSession
 *
 *FUNCTION:
 * This function is called by limProcessMessageQueue() upon
 * Action frame reception and no session.
 * Currently only public action frames can be received from
 * a non-associated station.
 *
 *LOGIC:
 *
 *ASSUMPTIONS:
 *
 *NOTE:
 *
 * @param  pMac - Pointer to Global MAC structure
 * @param  *pBd - A pointer to Buffer descriptor + associated PDUs
 * @return None
 */

void
limProcessActionFrameNoSession(tpAniSirGlobal pMac, tANI_U8 *pBd)
{
   tANI_U8 *pBody = WDA_GET_RX_MPDU_DATA(pBd);
   tpSirMacVendorSpecificPublicActionFrameHdr pActionHdr = (tpSirMacVendorSpecificPublicActionFrameHdr) pBody;

   limLog( pMac, LOGE, "Received a Action frame -- no session");

   switch ( pActionHdr->category )
   {
      case SIR_MAC_ACTION_PUBLIC_USAGE:
         switch(pActionHdr->actionID) {
            case SIR_MAC_ACTION_VENDOR_SPECIFIC:
              {
                tpSirMacMgmtHdr     pHdr;
                tANI_U32            frameLen;
                tANI_U8 P2POui[] = { 0x50, 0x6F, 0x9A, 0x09 };

                pHdr = WDA_GET_RX_MAC_HEADER(pBd);
                frameLen = WDA_GET_RX_PAYLOAD_LEN(pBd);

                //Check if it is a P2P public action frame.
                if( palEqualMemory( pMac->hHdd, pActionHdr->Oui, P2POui, 4 ) )
                {
                  /* Forward to the SME to HDD to wpa_supplicant */
                  // type is ACTION
                  limSendSmeMgmtFrameInd(pMac, pHdr->fc.subType, 
                      (tANI_U8*)pHdr, frameLen + sizeof(tSirMacMgmtHdr), 0,
                      WDA_GET_RX_CH( pBd ));
                }
                else
                {
                  limLog( pMac, LOGE, FL("Unhandled public action frame (Vendor specific). OUI %x %x %x %x\n"),
                      pActionHdr->Oui[0], pActionHdr->Oui[1], pActionHdr->Oui[2], pActionHdr->Oui[3] );
                }
              }
               break;
            default:
               PELOGE(limLog(pMac, LOGE, FL("Unhandled public action frame -- %x \n"), pActionHdr->actionID);)
                  break;
         }
         break;
      default:
         PELOGE(limLog(pMac, LOGE, FL("Unhandled action frame without session -- %x \n"), pActionHdr->category);)
            break;

   }
}
#endif
