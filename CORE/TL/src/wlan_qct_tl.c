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

/*===========================================================================


                       W L A N _ Q C T _ T L . C

  OVERVIEW:

  This software unit holds the implementation of the WLAN Transport Layer.

  The functions externalized by this module are to be called ONLY by other
  WLAN modules that properly register with the Transport Layer initially.

  DEPENDENCIES:

  Are listed for each API below.


  Copyright (c) 2008 QUALCOMM Incorporated.
  All Rights Reserved.
  Qualcomm Confidential and Proprietary
===========================================================================*/

/*===========================================================================

                      EDIT HISTORY FOR FILE


  This section contains comments describing changes made to the module.
  Notice that changes are listed in reverse chronological order.


   $Header$$DateTime$$Author$


  when        who     what, where, why
----------    ---    --------------------------------------------------------
2010-07-13    c_shinde Fixed an issue where WAPI rekeying was failing because 
                      WAI frame sent out during rekeying had the protected bit
                      set to 1.
2010-05-06    rnair   Changed name of variable from usLlcType to usEtherType
                      Changed function name from GetLLCType to GetEtherType
                      Fixed 802.3 to 802.11 frame translation issue where two 
                      bytes of the LLC header was getting overwritten in the
                      non-Qos path
2010-05-06    rnair   RxAuth path fix for modifying the header before ether
                      type is retreived (Detected while testing rekeying
                      in WAPI Volans)
2010-02-19    bad     Fixed 802.11 to 802.3 ft issues with WAPI
2010-02-19    rnair   WAPI: If frame is a WAI frame in TxConn and TxAuth, TL 
                      does frame translation. 
2010-02-01    rnair   WAPI: Fixed a bug where the value of ucIsWapiSta was not       
                      being set in the TL control block in the RegisterSTA func. 
2010-01-08    lti     Added TL Data Caching 
2009-11-04    rnair   WAPI: Moving common functionality to a seperate function
                      called WLANTL_GetLLCType
2009-10-15    rnair   WAPI: Featurizing WAPI code
2009-10-09    rnair   WAPI: Modifications to authenticated state handling of Rx data
2009-10-06    rnair   Adding support for WAPI 
2009-09-22    lti     Add deregistration API for management client
2009-07-16    rnair   Temporary fix to let TL fetch packets when multiple 
                      peers exist in an IBSS
2009-06-10    lti     Fix for checking TID value of meta info on TX - prevent
                      memory overwrite 
                      Fix for properly checking the sta id for resuming trigger
                      frame generation
2009-05-14    lti     Fix for sending out trigger frames
2009-05-15    lti     Addr3 filtering 
2009-04-13    lti     Assert if packet larger then allowed
                      Drop packet that fails flatten
2009-04-02    lti     Performance fixes for TL 
2009-02-19    lti     Added fix for LLC management on Rx Connect 
2009-01-16    lti     Replaced peek data with extract data for non BD opertions
                      Extracted frame control in Tl and pass to HAL for frame 
                      type evaluation
2009-02-02    sch     Add handoff support
2008-12-09    lti     Fixes for AMSS compilation 
                      Removed assert on receive when there is no station
2008-12-02    lti     Fix fo trigger frame generation 
2008-10-31    lti     Fix fo TL tx suspend
2008-10-01    lti     Merged in fixes from reordering
                      Disabled part of UAPSD functionality in TL
                      (will be re-enabled once UAPSD is tested)
                      Fix for UAPSD multiple enable
2008-08-10    lti     Fixes following UAPSD testing
                      Fixed infinite loop on mask computation when STA no reg
2008-08-06    lti     Fixes after QOS unit testing
2008-08-06    lti     Added QOS support
2008-07-23    lti     Fix for vos packet draining
2008-07-17    lti     Fix for data type value
                      Added frame translation code in TL
                      Avoid returning failure to PE in case previous frame is
                      still pending; fail previous and cache new one for tx
                      Get frames returning boolean true if more frames are pending
2008-07-03    lti     Fixes following pre-integration testing
2008-06-26    lti     Fixes following unit testing
                      Added alloc and free for TL context
                      Using atomic set u8 instead of u32
2008-05-16    lti     Created module

===========================================================================*/

/*----------------------------------------------------------------------------
 * Include Files
 * -------------------------------------------------------------------------*/
#include "wlan_qct_tl.h" 
#include "wlan_qct_wda.h" 
#include "wlan_qct_tli.h" 
#include "wlan_qct_tli_ba.h" 
#include "wlan_qct_tl_hosupport.h"
#if !defined( FEATURE_WLAN_INTEGRATED_SOC )
#include "wlan_qct_ssc.h"
#endif
#include "tlDebug.h"
#ifdef FEATURE_WLAN_WAPI
/*Included to access WDI_RxBdType */
#include "wlan_qct_wdi_bd.h"
#endif
/*Enables debugging behavior in TL*/
#define TL_DEBUG
//#define WLAN_SOFTAP_FLOWCTRL_EN

//#define BTAMP_TEST
/*----------------------------------------------------------------------------
 * Preprocessor Definitions and Constants
 * -------------------------------------------------------------------------*/
/*LLC header value*/
static v_U8_t WLANTL_LLC_HEADER[] =  {0xAA, 0xAA, 0x03, 0x00, 0x00, 0x00 };

#ifdef FEATURE_WLAN_CCX
/*Aironet SNAP header value*/
static v_U8_t WLANTL_AIRONET_SNAP_HEADER[] =  {0xAA, 0xAA, 0x03, 0x00, 0x40, 0x96, 0x00, 0x00 };
#endif //FEATURE_WLAN_CCX

/*BT-AMP packet LLC OUI value*/
const v_U8_t WLANTL_BT_AMP_OUI[] =  {0x00, 0x19, 0x58 };

#ifndef FEATURE_WLAN_INTEGRATED_SOC
#define WLANTL_BD_PDU_RESERVE_THRESHOLD           80
#endif

#ifdef VOLANS_PERF
#define WLANTL_BD_PDU_INTERRUPT_ENABLE_THRESHOLD  120
#define WLANTL_BD_PDU_INTERRUPT_GET_THRESHOLD  120

/* TL BD/PDU threshold to enable interrupt */
int bdPduInterruptEnableThreshold = WLANTL_BD_PDU_INTERRUPT_ENABLE_THRESHOLD;
int bdPduInterruptGetThreshold = WLANTL_BD_PDU_INTERRUPT_GET_THRESHOLD;
#endif /* VOLANS_PERF */

/*-----------------------------------*
 |   Type(2b)   |     Sub-type(4b)   |
 *-----------------------------------*/
#define WLANTL_IS_DATA_FRAME(_type_sub)                               \
                     ( WLANTL_DATA_FRAME_TYPE == ( (_type_sub) & 0x30 ))

#define WLANTL_IS_QOS_DATA_FRAME(_type_sub)                                      \
                     (( WLANTL_DATA_FRAME_TYPE == ( (_type_sub) & 0x30 )) &&     \
                      ( WLANTL_80211_DATA_QOS_SUBTYPE == ( (_type_sub) & 0xF ))) 

#define WLANTL_IS_MGMT_FRAME(_type_sub)                                     \
                     ( WLANTL_MGMT_FRAME_TYPE == ( (_type_sub) & 0x30 ))

#define WLANTL_IS_CTRL_FRAME(_type_sub)                                     \
                     ( WLANTL_CTRL_FRAME_TYPE == ( (_type_sub) & 0x30 ))

/*MAX Allowed len processed by TL - MAx MTU + 802.3 header + BD+DXE+XTL*/
#define WLANTL_MAX_ALLOWED_LEN    1514 + 100

#define WLANTL_MASK_AC  0x03

#ifdef WLAN_SOFTAP_FEATURE
//some flow_control define
//LWM mode will be enabled for this station if the egress/ingress falls below this ratio
#define WLANTL_LWM_EGRESS_INGRESS_THRESHOLD (0.75)

//Get enough sample to do the LWM related calculation
#define WLANTL_LWM_INGRESS_SAMPLE_THRESHOLD (64)

//Maximal on-fly packet per station in LWM mode
#define WLANTL_STA_BMU_THRESHOLD_MAX (256)

#define WLANTL_AC_MASK (0x7)
#define WLANTL_STAID_OFFSET (0x4)
#endif

/* UINT32 type endian swap */
#define SWAP_ENDIAN_UINT32(a)          ((a) = ((a) >> 0x18 ) |(((a) & 0xFF0000) >> 0x08) | \
                                            (((a) & 0xFF00) << 0x08)  | (((a) & 0xFF) << 0x18))



/*--------------------------------------------------------------------------
   TID to AC mapping in TL
 --------------------------------------------------------------------------*/
const v_U8_t  WLANTL_TID_2_AC[WLAN_MAX_TID] = {   WLANTL_AC_BE,
                                                  WLANTL_AC_BK,
                                                  WLANTL_AC_BK,
                                                  WLANTL_AC_BE,
                                                  WLANTL_AC_VI,
                                                  WLANTL_AC_VI,
                                                  WLANTL_AC_VO,
                                                  WLANTL_AC_VO };

/*----------------------------------------------------------------------------
 * Type Declarations
 * -------------------------------------------------------------------------*/
#define TL_LITTLE_BIT_ENDIAN

typedef struct
{

#ifndef TL_LITTLE_BIT_ENDIAN

   v_U8_t subType :4;
   v_U8_t type :2;
   v_U8_t protVer :2;

   v_U8_t order :1;
   v_U8_t wep :1;
   v_U8_t moreData :1;
   v_U8_t powerMgmt :1;
   v_U8_t retry :1;
   v_U8_t moreFrag :1;
   v_U8_t fromDS :1;
   v_U8_t toDS :1;

#else

   v_U8_t protVer :2;
   v_U8_t type :2;
   v_U8_t subType :4;

   v_U8_t toDS :1;
   v_U8_t fromDS :1;
   v_U8_t moreFrag :1;
   v_U8_t retry :1;
   v_U8_t powerMgmt :1;
   v_U8_t moreData :1;
   v_U8_t wep :1;
   v_U8_t order :1;

#endif

} WLANTL_MACFCType;

/* 802.11 header */
typedef struct
{
 /* Frame control field */
 WLANTL_MACFCType  wFrmCtrl;

 /* Duration ID */
 v_U16_t  usDurationId;

 /* Address 1 field  */
 v_U8_t   vA1[VOS_MAC_ADDR_SIZE];

 /* Address 2 field */
 v_U8_t   vA2[VOS_MAC_ADDR_SIZE];

 /* Address 3 field */
 v_U8_t   vA3[VOS_MAC_ADDR_SIZE];

 /* Sequence control field */
 v_U16_t  usSeqCtrl;

 // Find the size of the mandatory header size.
#define WLAN80211_MANDATORY_HEADER_SIZE \
    (sizeof(WLANTL_MACFCType) + sizeof(v_U16_t) + \
    (3 * (sizeof(v_U8_t) * VOS_MAC_ADDR_SIZE))  + \
    sizeof(v_U16_t))

 /* Optional A4 address */
 v_U8_t   optvA4[VOS_MAC_ADDR_SIZE];

 /* Optional QOS control field */
 v_U16_t  usQosCtrl;
}WLANTL_80211HeaderType;

/* 802.3 header */
typedef struct
{
 /* Destination address field */
 v_U8_t   vDA[VOS_MAC_ADDR_SIZE];

 /* Source address field */
 v_U8_t   vSA[VOS_MAC_ADDR_SIZE];

 /* Length field */
 v_U16_t  usLenType;
}WLANTL_8023HeaderType;

/*----------------------------------------------------------------------------
 * Global Data Definitions
 * -------------------------------------------------------------------------*/
#define WLAN_TL_INVALID_U_SIG 255
#define WLAN_TL_INVALID_B_SIG 255

#define WLAN_TL_AC_ARRAY_2_MASK( _pSTA, _ucACMask, i ) \
  do\
  {\
    _ucACMask = 0; \
    for ( i = 0; i < WLANTL_MAX_AC; i++ ) \
    { \
      if ( 0 != (_pSTA)->aucACMask[i] ) \
      { \
        _ucACMask |= ( 1 << i ); \
      } \
    } \
  } while (0);

/*----------------------------------------------------------------------------
 * Static Variable Definitions
 * -------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------
 * Static Function Declarations and Definitions
 * -------------------------------------------------------------------------*/

static VOS_STATUS 
WLANTL_GetEtherType
(
  v_U8_t               * aucBDHeader,
  vos_pkt_t            * vosDataBuff,
  v_U8_t                 ucMPDUHLen,
  v_U16_t              * usEtherType
);

#ifdef FEATURE_WLAN_WAPI
/*---------------------------------------------------------------------------
 * Adding a global variable to be used when doing frame translation in TxAuth
 * state so as to not set the protected bit to 1 in the case of WAI frames
 *---------------------------------------------------------------------------*/
v_U8_t gUcIsWai = 0;
#endif

/*----------------------------------------------------------------------------
 * Externalized Function Definitions
* -------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------
 * Function Declarations and Documentation
 * -------------------------------------------------------------------------*/

/*==========================================================================

  FUNCTION    WLANTL_Open

  DESCRIPTION
    Called by HDD at driver initialization. TL will initialize all its
    internal resources and will wait for the call to start to register
    with the other modules.

  DEPENDENCIES

  PARAMETERS

    IN
    pvosGCtx:       pointer to the global vos context; a handle to TL's
                    control block can be extracted from its context
    pTLConfig:      TL Configuration

  RETURN VALUE
    The result code associated with performing the operation

    VOS_STATUS_E_FAULT:  pointer to TL cb is NULL ; access would cause a page
                         fault
    VOS_STATUS_SUCCESS:  Everything is good :)

  SIDE EFFECTS

============================================================================*/
VOS_STATUS
WLANTL_Open
(
  v_PVOID_t               pvosGCtx,
  WLANTL_ConfigInfoType*  pTLConfig
)
{
  WLANTL_CbType*  pTLCb = NULL; 
  v_U8_t          ucIndex; 
  tHalHandle      smeContext;
#if defined WLAN_FEATURE_NEIGHBOR_ROAMING
  VOS_STATUS      status = VOS_STATUS_SUCCESS;
#endif
  /*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

  /*------------------------------------------------------------------------
    Sanity check
    Extract TL control block
   ------------------------------------------------------------------------*/
  vos_alloc_context( pvosGCtx, VOS_MODULE_ID_TL, 
                    (void*)&pTLCb, sizeof(WLANTL_CbType));

  pTLCb = VOS_GET_TL_CB(pvosGCtx);
  if (( NULL == pTLCb ) || ( NULL == pTLConfig ) )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
               "WLAN TL: Invalid input pointer on WLANTL_Open TL %x Config %x", pTLCb, pTLConfig ));
    return VOS_STATUS_E_FAULT;
  }

  smeContext = vos_get_context(VOS_MODULE_ID_SME, pvosGCtx);
  if ( NULL == smeContext )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
                      "%s: Invalid smeContext", __FUNCTION__));
    return VOS_STATUS_E_FAULT;
  }

  /* Zero out the memory so we are OK, when CleanCB is called.*/
  vos_mem_zero((v_VOID_t *)pTLCb, sizeof(WLANTL_CbType));

  /*------------------------------------------------------------------------
    Clean up TL control block, initialize all values
   ------------------------------------------------------------------------*/
  TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
             "WLAN TL:WLANTL_Open"));

  pTLCb->atlSTAClients = vos_mem_malloc(sizeof(WLANTL_STAClientType) * WLAN_MAX_STA_COUNT);
  if (NULL == pTLCb->atlSTAClients)
  {
    TLLOGE(VOS_TRACE(VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR, "WLAN TL: StaClients allocation failed\n"));
    vos_free_context(pvosGCtx, VOS_MODULE_ID_TL, pTLCb);
    return VOS_STATUS_E_FAULT;
  }

  vos_mem_zero((v_VOID_t *)pTLCb->atlSTAClients, sizeof(WLANTL_STAClientType) * WLAN_MAX_STA_COUNT);

  pTLCb->reorderBufferPool = vos_mem_malloc(sizeof(WLANTL_REORDER_BUFFER_T) * WLANTL_MAX_BA_SESSION);
  if (NULL == pTLCb->reorderBufferPool)
  {
    TLLOGE(VOS_TRACE(VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR, "WLAN TL: Reorder buffer allocation failed\n"));
    vos_mem_free(pTLCb->atlSTAClients);
    vos_free_context(pvosGCtx, VOS_MODULE_ID_TL, pTLCb);
    return VOS_STATUS_E_FAULT;
         
  }

  vos_mem_zero((v_VOID_t *)pTLCb->reorderBufferPool, sizeof(WLANTL_REORDER_BUFFER_T) * WLANTL_MAX_BA_SESSION);

  WLANTL_CleanCB(pTLCb, 0 /*do not empty*/);

  for ( ucIndex = 0; ucIndex < WLANTL_MAX_AC ; ucIndex++)
  {
    pTLCb->tlConfigInfo.ucAcWeights[ucIndex] =
                pTLConfig->ucAcWeights[ucIndex];
  }

#ifdef WLAN_SOFTAP_FEATURE
  // scheduling init to be the last one of previous round
  pTLCb->uCurServedAC = WLANTL_AC_BK;
  pTLCb->ucCurLeftWeight = 1;
  pTLCb->ucCurrentSTA = WLAN_MAX_STA_COUNT-1;

#if 0
  //flow control field init
  vos_mem_zero(&pTLCb->tlFCInfo, sizeof(tFcTxParams_type));
  //bit 0: set (Bd/pdu count) bit 1: set (request station PS change notification)
  pTLCb->tlFCInfo.fcConfig = 0x1;
#endif

  pTLCb->vosTxFCBuf = NULL;
  pTLCb->tlConfigInfo.uMinFramesProcThres =
                pTLConfig->uMinFramesProcThres;
#endif

  pTLCb->tlConfigInfo.uDelayedTriggerFrmInt =
                pTLConfig->uDelayedTriggerFrmInt;

  /*------------------------------------------------------------------------
    Allocate internal resources
   ------------------------------------------------------------------------*/
  vos_pkt_get_packet(&pTLCb->vosDummyBuf, VOS_PKT_TYPE_RX_RAW, 1, 1,
                     1/*true*/,NULL, NULL);

  WLANTL_InitBAReorderBuffer(pvosGCtx);
#if defined WLAN_FEATURE_NEIGHBOR_ROAMING
   /* Initialize Handoff support modue
    * RSSI measure and Traffic state monitoring */
  status = WLANTL_HSInit(pvosGCtx);
  if(!VOS_IS_STATUS_SUCCESS(status))
  {
    TLLOGE(VOS_TRACE(VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
              "Handoff support module init fail"));
    vos_mem_free(pTLCb->atlSTAClients);
    vos_mem_free(pTLCb->reorderBufferPool);
    vos_free_context(pvosGCtx, VOS_MODULE_ID_TL, pTLCb);
    return status;
  }
#endif

  pTLCb->isBMPS = VOS_FALSE;
  pmcRegisterDeviceStateUpdateInd( smeContext,
                                   WLANTL_PowerStateChangedCB, pvosGCtx );

  return VOS_STATUS_SUCCESS;
}/* WLANTL_Open */

/*==========================================================================

  FUNCTION    WLANTL_Start

  DESCRIPTION
    Called by HDD as part of the overall start procedure. TL will use this
    call to register with BAL as a transport layer entity.

  DEPENDENCIES

  PARAMETERS

    IN
    pvosGCtx:       pointer to the global vos context; a handle to TL's
                    control block can be extracted from its context

  RETURN VALUE
    The result code associated with performing the operation

    VOS_STATUS_E_FAULT:  pointer to TL cb is NULL ; access would cause a page
                         fault
    VOS_STATUS_SUCCESS:  Everything is good :)

    Other codes can be returned as a result of a BAL failure; see BAL API
    for more info

  SIDE EFFECTS

============================================================================*/
VOS_STATUS
WLANTL_Start
(
  v_PVOID_t  pvosGCtx
)
{
  WLANTL_CbType*      pTLCb      = NULL;
  v_U32_t             uResCount = WDA_TLI_MIN_RES_DATA;
  VOS_STATUS          vosStatus;
  /*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

  /*------------------------------------------------------------------------
    Sanity check
    Extract TL control block
   ------------------------------------------------------------------------*/
  pTLCb = VOS_GET_TL_CB(pvosGCtx);
  if ( NULL == pTLCb )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
               "WLAN TL:Invalid TL pointer from pvosGCtx on WLANTL_Start"));
    return VOS_STATUS_E_FAULT;
  }

  /*------------------------------------------------------------------------
    Register with WDA as transport layer client
    Request resources for tx from bus
  ------------------------------------------------------------------------*/
  TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
             "WLAN TL:WLAN TL:WLANTL_Start"));

  vosStatus = WDA_DS_Register( pvosGCtx, 
                          WLANTL_TxComp, 
                          WLANTL_RxFrames,
                          WLANTL_GetFrames, 
                          WLANTL_ResourceCB,
                          WDA_TLI_MIN_RES_DATA, 
                          pvosGCtx, 
                          &uResCount ); 

  if ( VOS_STATUS_SUCCESS != vosStatus )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR, 
               "WLAN TL:TL failed to register with BAL/WDA, Err: %d",
               vosStatus));
    return vosStatus;
  }

  /* Enable transmission */
  vos_atomic_set_U8( &pTLCb->ucTxSuspended, 0);

  pTLCb->uResCount = uResCount;

  return VOS_STATUS_SUCCESS;
}/* WLANTL_Start */

/*==========================================================================

  FUNCTION    WLANTL_Stop

  DESCRIPTION
    Called by HDD to stop operation in TL, before close. TL will suspend all
    frame transfer operation and will wait for the close request to clean up
    its resources.

  DEPENDENCIES

  PARAMETERS

    IN
    pvosGCtx:       pointer to the global vos context; a handle to TL's
                    control block can be extracted from its context

  RETURN VALUE
    The result code associated with performing the operation

    VOS_STATUS_E_FAULT:  pointer to TL cb is NULL ; access would cause a page
                         fault
    VOS_STATUS_SUCCESS:  Everything is good :)

  SIDE EFFECTS

============================================================================*/
VOS_STATUS
WLANTL_Stop
(
  v_PVOID_t  pvosGCtx
)
{
  WLANTL_CbType*  pTLCb = NULL;
  v_U8_t      ucIndex;
  /*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

  /*------------------------------------------------------------------------
    Sanity check
    Extract TL control block
   ------------------------------------------------------------------------*/
  pTLCb = VOS_GET_TL_CB(pvosGCtx);
  if ( NULL == pTLCb )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
         "WLAN TL:Invalid TL pointer from pvosGCtx on WLANTL_ChangeSTAState"));
    return VOS_STATUS_E_FAULT;
  }

  /*------------------------------------------------------------------------
    Stop TL and empty Station list
   ------------------------------------------------------------------------*/
  TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
             "WLAN TL:WLANTL_Stop"));

  /* Disable transmission */
  vos_atomic_set_U8( &pTLCb->ucTxSuspended, 1);

  if ( NULL != pTLCb->tlMgmtFrmClient.vosPendingDataBuff )
  {
    vos_pkt_return_packet(pTLCb->tlMgmtFrmClient.vosPendingDataBuff);
    pTLCb->tlMgmtFrmClient.vosPendingDataBuff = NULL;
  }

  if ( NULL != pTLCb->tlBAPClient.vosPendingDataBuff )
  {
    vos_pkt_return_packet(pTLCb->tlBAPClient.vosPendingDataBuff);
    pTLCb->tlBAPClient.vosPendingDataBuff = NULL;
  }

#if defined WLAN_FEATURE_NEIGHBOR_ROAMING
  if(VOS_STATUS_SUCCESS != WLANTL_HSStop(pvosGCtx))
  {
    TLLOGW(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_WARN,
               "Handoff Support module stop fail"));
  }
#endif

  /*-------------------------------------------------------------------------
    Clean client stations
   -------------------------------------------------------------------------*/
  for ( ucIndex = 0; ucIndex < WLAN_MAX_STA_COUNT ; ucIndex++)
  {
    WLANTL_CleanSTA(&pTLCb->atlSTAClients[ucIndex], 1 /*empty all queues*/);
  }


  return VOS_STATUS_SUCCESS;
}/* WLANTL_Stop */

/*==========================================================================

  FUNCTION    WLANTL_Close

  DESCRIPTION
    Called by HDD during general driver close procedure. TL will clean up
    all the internal resources.

  DEPENDENCIES

  PARAMETERS

    IN
    pvosGCtx:       pointer to the global vos context; a handle to TL's
                    control block can be extracted from its context

  RETURN VALUE
    The result code associated with performing the operation

    VOS_STATUS_E_FAULT:  pointer to TL cb is NULL ; access would cause a page
                         fault
    VOS_STATUS_SUCCESS:  Everything is good :)

  SIDE EFFECTS

============================================================================*/
VOS_STATUS
WLANTL_Close
(
  v_PVOID_t  pvosGCtx
)
{
  WLANTL_CbType*  pTLCb = NULL;
  tHalHandle smeContext;
  /*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

  /*------------------------------------------------------------------------
    Sanity check
    Extract TL control block
   ------------------------------------------------------------------------*/
  pTLCb = VOS_GET_TL_CB(pvosGCtx);
  if ( NULL == pTLCb )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
         "WLAN TL:Invalid TL pointer from pvosGCtx on WLANTL_ChangeSTAState"));
    return VOS_STATUS_E_FAULT;
  }
  /*------------------------------------------------------------------------
    Deregister from PMC
   ------------------------------------------------------------------------*/
  smeContext = vos_get_context(VOS_MODULE_ID_SME, pvosGCtx);
  if ( NULL == smeContext )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
                      "%s: Invalid smeContext", __FUNCTION__));
    // continue so that we can cleanup as much as possible
  }
  else
  {
    pmcDeregisterDeviceStateUpdateInd( smeContext, WLANTL_PowerStateChangedCB );
  }

#if defined WLAN_FEATURE_NEIGHBOR_ROAMING
  if(VOS_STATUS_SUCCESS != WLANTL_HSDeInit(pvosGCtx))
  {
    TLLOGW(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_WARN,
               "Handoff Support module DeInit fail"));
  }
#endif

  /*------------------------------------------------------------------------
    Cleanup TL control block.
   ------------------------------------------------------------------------*/
  TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
             "WLAN TL: WLANTL_Close"));
  WLANTL_CleanCB(pTLCb, 1 /* empty queues/lists/pkts if any*/);

  vos_mem_free(pTLCb->atlSTAClients);
  vos_mem_free(pTLCb->reorderBufferPool);

  /*------------------------------------------------------------------------
    Free TL context from VOSS global
   ------------------------------------------------------------------------*/
  vos_free_context(pvosGCtx, VOS_MODULE_ID_TL, pTLCb);
  return VOS_STATUS_SUCCESS;
}/* WLANTL_Close */

/*----------------------------------------------------------------------------
    INTERACTION WITH HDD
 ---------------------------------------------------------------------------*/
/*==========================================================================

  FUNCTION    WLANTL_ConfigureSwFrameTXXlationForAll

  DESCRIPTION
     Function to disable/enable frame translation for all association stations.

  DEPENDENCIES

  PARAMETERS
   IN
    pvosGCtx: VOS context 
    EnableFrameXlation TRUE means enable SW translation for all stations.
    .

  RETURN VALUE

   void.

============================================================================*/
void
WLANTL_ConfigureSwFrameTXXlationForAll
(
  v_PVOID_t pvosGCtx, 
  v_BOOL_t enableFrameXlation
)
{
  v_U8_t ucIndex;
  /*------------------------------------------------------------------------
    Extract TL control block
   ------------------------------------------------------------------------*/
  WLANTL_CbType* pTLCb = VOS_GET_TL_CB(pvosGCtx);

  if ( NULL == pTLCb )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
           "WLAN TL:Invalid TL pointer from pvosGCtx on "
           "WLANTL_ConfigureSwFrameTXXlationForAll"));
    return;
  }

  TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO,
     "WLANTL_ConfigureSwFrameTXXlationForAll: Configure SW frameXlation %d", 
      enableFrameXlation));

  for ( ucIndex = 0; ucIndex < WLAN_MAX_TID; ucIndex++) 
  {
    if ( 0 != pTLCb->atlSTAClients[ucIndex].ucExists )
    {
#ifdef WLAN_SOFTAP_VSTA_FEATURE
      // if this station was not allocated resources to perform HW-based
      // TX frame translation then force SW-based TX frame translation
      // otherwise use the frame translation supplied by the client
      if (!WDA_IsHwFrameTxTranslationCapable(pvosGCtx, ucIndex))
      {
        pTLCb->atlSTAClients[ucIndex].wSTADesc.ucSwFrameTXXlation = 1;
      }
      else
#endif
        pTLCb->atlSTAClients[ucIndex].wSTADesc.ucSwFrameTXXlation
                                             = enableFrameXlation;
    }
  }
}

/*===========================================================================

  FUNCTION    WLANTL_StartForwarding

  DESCRIPTION

    This function is used to ask serialization through TX thread of the   
    cached frame forwarding (if statation has been registered in the mean while) 
    or flushing (if station has not been registered by the time)

    In case of forwarding, upper layer is only required to call WLANTL_RegisterSTAClient()
    and doesn't need to call this function explicitly. TL will handle this inside 
    WLANTL_RegisterSTAClient(). 
    
    In case of flushing, upper layer is required to call this function explicitly
    
  DEPENDENCIES

    TL must have been initialized before this gets called.

   
  PARAMETERS

   ucSTAId:   station id 

  RETURN VALUE

    The result code associated with performing the operation
    Please check return values of vos_tx_mq_serialize.

  SIDE EFFECTS
    If TL was asked to perform WLANTL_CacheSTAFrame() in WLANTL_RxFrames(), 
    either WLANTL_RegisterSTAClient() or this function must be called 
    within reasonable time. Otherwise, TL will keep cached vos buffer until
    one of this function is called, and may end up with system buffer exhasution. 

    It's an upper layer's responsibility to call this function in case of
    flushing

============================================================================*/

VOS_STATUS 
WLANTL_StartForwarding
(
  v_U8_t ucSTAId,
  v_U8_t ucUcastSig, 
  v_U8_t ucBcastSig 
)
{
  vos_msg_t      sMessage;
  v_U32_t        uData;             
 /*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

  /* Signal the OS to serialize our event */
  VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO,
             "Serializing TL Start Forwarding Cached for control STA %d", 
              ucSTAId );

  vos_mem_zero( &sMessage, sizeof(vos_msg_t) );

  uData = ucSTAId | (ucUcastSig << 8 ) | (ucBcastSig << 16); 
  sMessage.bodyptr = (v_PVOID_t)uData;
  sMessage.type    = WLANTL_TX_FWD_CACHED;

  return vos_tx_mq_serialize(VOS_MQ_ID_TL, &sMessage);

} /* WLANTL_StartForwarding() */

/*===========================================================================

  FUNCTION    WLANTL_AssocFailed

  DESCRIPTION

    This function is used by PE to notify TL that cache needs to flushed' 
    when association is not successfully completed 

    Internally, TL post a message to TX_Thread to serialize the request to 
    keep lock-free mechanism.

   
  DEPENDENCIES

    TL must have been initialized before this gets called.

   
  PARAMETERS

   ucSTAId:   station id 

  RETURN VALUE

   none
   
  SIDE EFFECTS
   There may be race condition that PE call this API and send another association
   request immediately with same staId before TX_thread can process the message.

   To avoid this, we might need PE to wait for TX_thread process the message,
   but this is not currently implemented. 
   
============================================================================*/
void WLANTL_AssocFailed(v_U8_t staId)
{
  // flushing frames and forwarding frames uses the same message
  // the only difference is what happens when the message is processed
  // if the STA exist, the frames will be forwarded
  // and if it doesn't exist, the frames will be flushed
  // in this case we know it won't exist so the DPU index signature values don't matter
  if(!VOS_IS_STATUS_SUCCESS(WLANTL_StartForwarding(staId,0,0)))
  {
    VOS_TRACE(VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
       " %s fails to start forwarding", __FUNCTION__);
  }
}
  
  /*===========================================================================

  FUNCTION  WLANTL_Finish_ULA

  DESCRIPTION
     This function is used by HDD to notify TL to finish Upper layer authentication
     incase the last EAPOL packet is pending in the TL queue. 
     To avoid the race condition between sme set key and the last EAPOL packet 
     the HDD module calls this function just before calling the sme_RoamSetKey.

  DEPENDENCIES

     TL must have been initialized before this gets called.
  
  PARAMETERS

   callbackRoutine:   HDD Callback function.
   callbackContext : HDD userdata context.
  
   RETURN VALUE

   VOS_STATUS_SUCCESS/VOS_STATUS_FAILURE
   
  SIDE EFFECTS
   
============================================================================*/

VOS_STATUS WLANTL_Finish_ULA( void (*callbackRoutine) (void *callbackContext),
                              void *callbackContext)
{
#if defined( FEATURE_WLAN_INTEGRATED_SOC )
   return WDA_DS_FinishULA( callbackRoutine, callbackContext); 
#else
   vos_msg_t  vosMsg;   
   VOS_STATUS vosStatus = VOS_STATUS_SUCCESS;

   vosMsg.reserved = 0;
   vosMsg.bodyval  = (v_U32_t)callbackContext;
   vosMsg.bodyptr = callbackRoutine;
   vosMsg.type    = WLANSSC_FINISH_ULA;
   vosStatus = vos_tx_mq_serialize( VOS_MQ_ID_SSC, &vosMsg);
   return vosStatus;
#endif
}


/*===========================================================================

  FUNCTION    WLANTL_RegisterSTAClient

  DESCRIPTION

    This function is used by HDD to register as a client for data services
    with TL. HDD will call this API for each new station that it adds,
    thus having the flexibility of registering different callback for each
    STA it services.

  DEPENDENCIES

    TL must have been initialized before this gets called.

    Restriction:
      Main thread will have higher priority that Tx and Rx threads thus
      guaranteeing that a station will be added before any data can be
      received for it. (This enables TL to be lock free)

  PARAMETERS

   pvosGCtx:        pointer to the global vos context; a handle to TL's
                    control block can be extracted from its context
   pfnStARx:        function pointer to the receive packet handler from HDD
   pfnSTATxComp:    function pointer to the transmit complete confirmation
                    handler from HDD
   pfnSTAFetchPkt:  function pointer to the packet retrieval routine in HDD
   wSTADescType:    STA Descriptor, contains information related to the
                    new added STA

  RETURN VALUE

    The result code associated with performing the operation

    VOS_STATUS_E_INVAL: Input parameters are invalid
    VOS_STATUS_E_FAULT: Station ID is outside array boundaries or pointer to
                        TL cb is NULL ; access would cause a page fault
    VOS_STATUS_E_EXISTS: Station was already registered
    VOS_STATUS_SUCCESS:  Everything is good :)

  SIDE EFFECTS

============================================================================*/
VOS_STATUS
WLANTL_RegisterSTAClient
(
  v_PVOID_t                 pvosGCtx,
  WLANTL_STARxCBType        pfnSTARx,
  WLANTL_TxCompCBType       pfnSTATxComp,
  WLANTL_STAFetchPktCBType  pfnSTAFetchPkt,
  WLAN_STADescType*         pwSTADescType,
  v_S7_t                    rssi
)
{
  WLANTL_CbType*  pTLCb = NULL;
#ifdef ANI_CHIPSET_VOLANS
  v_U8_t    ucTid = 0;/*Local variable to clear previous replay counters of STA on all TIDs*/
#endif
  /*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

  /*------------------------------------------------------------------------
    Sanity check
   ------------------------------------------------------------------------*/
  if (( NULL == pwSTADescType ) || ( NULL == pfnSTARx ) ||
      ( NULL == pfnSTAFetchPkt ))
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
               "WLAN TL:Invalid parameter sent on WLANTL_RegisterSTAClient"));
    return VOS_STATUS_E_INVAL;
  }

  if ( WLANTL_STA_ID_INVALID( pwSTADescType->ucSTAId ) )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
         "WLAN TL:Invalid station id requested on WLANTL_RegisterSTAClient"));
    return VOS_STATUS_E_FAULT;
  }

  /*------------------------------------------------------------------------
    Extract TL control block
   ------------------------------------------------------------------------*/
  pTLCb = VOS_GET_TL_CB(pvosGCtx);
  if ( NULL == pTLCb )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
      "WLAN TL:Invalid TL pointer from pvosGCtx on WLANTL_RegisterSTAClient"));
    return VOS_STATUS_E_FAULT;
  }

  if ( 0 != pTLCb->atlSTAClients[pwSTADescType->ucSTAId].ucExists )
  {
    pTLCb->atlSTAClients[pwSTADescType->ucSTAId].ucExists++;
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
      "WLAN TL:Station was already registered on WLANTL_RegisterSTAClient"));
    return VOS_STATUS_E_EXISTS;
  }

  /*------------------------------------------------------------------------
    Register station with TL
   ------------------------------------------------------------------------*/
  TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
             "WLAN TL:Registering STA Client ID: %d", pwSTADescType->ucSTAId ));

  pTLCb->atlSTAClients[pwSTADescType->ucSTAId].pfnSTARx       = pfnSTARx;
  pTLCb->atlSTAClients[pwSTADescType->ucSTAId].pfnSTAFetchPkt = pfnSTAFetchPkt;

  /* Only register if different from NULL - TL default Tx Comp Cb will
    release the vos packet */
  if ( NULL != pfnSTATxComp )
  {
    pTLCb->atlSTAClients[pwSTADescType->ucSTAId].pfnSTATxComp   = pfnSTATxComp;
  }

  pTLCb->atlSTAClients[pwSTADescType->ucSTAId].tlState  = WLANTL_STA_INIT;
  pTLCb->atlSTAClients[pwSTADescType->ucSTAId].tlPri    =
                                                    WLANTL_STA_PRI_NORMAL;
  pTLCb->atlSTAClients[pwSTADescType->ucSTAId].wSTADesc.ucSTAId  =
    pwSTADescType->ucSTAId;

  TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
             "WLAN TL:Registering STA Client ID: %d with UC %d and BC %d", 
             pwSTADescType->ucSTAId, 
              pwSTADescType->ucUcastSig, pwSTADescType->ucBcastSig));

  pTLCb->atlSTAClients[pwSTADescType->ucSTAId].wSTADesc.wSTAType =
    pwSTADescType->wSTAType;

  pTLCb->atlSTAClients[pwSTADescType->ucSTAId].wSTADesc.ucQosEnabled =
    pwSTADescType->ucQosEnabled;

  pTLCb->atlSTAClients[pwSTADescType->ucSTAId].wSTADesc.ucAddRmvLLC =
    pwSTADescType->ucAddRmvLLC;

  pTLCb->atlSTAClients[pwSTADescType->ucSTAId].wSTADesc.ucProtectedFrame =
    pwSTADescType->ucProtectedFrame;

#ifdef FEATURE_WLAN_CCX
  pTLCb->atlSTAClients[pwSTADescType->ucSTAId].wSTADesc.ucIsCcxSta =
    pwSTADescType->ucIsCcxSta;

  TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
             "WLAN TL:Registering STA Client ID: %d QoS %d Add LLC %d ProtFrame %d CcxSta %d", 
             pwSTADescType->ucSTAId, 
             pwSTADescType->ucQosEnabled,
             pwSTADescType->ucAddRmvLLC,
             pwSTADescType->ucProtectedFrame,
             pwSTADescType->ucIsCcxSta));
#else

  TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
             "WLAN TL:Registering STA Client ID: %d QoS %d Add LLC %d ProtFrame %d", 
             pwSTADescType->ucSTAId, 
             pwSTADescType->ucQosEnabled,
             pwSTADescType->ucAddRmvLLC,
             pwSTADescType->ucProtectedFrame));

#endif //FEATURE_WLAN_CCX
#ifdef WLAN_SOFTAP_VSTA_FEATURE
  // if this station was not allocated resources to perform HW-based
  // TX frame translation then force SW-based TX frame translation
  // otherwise use the frame translation supplied by the client

  if (!WDA_IsHwFrameTxTranslationCapable(pvosGCtx, pwSTADescType->ucSTAId)
      || ( WLAN_STA_BT_AMP == pwSTADescType->wSTAType))
  {
      pwSTADescType->ucSwFrameTXXlation = 1;
  }
#endif

  pTLCb->atlSTAClients[pwSTADescType->ucSTAId].wSTADesc.ucSwFrameTXXlation =
    pwSTADescType->ucSwFrameTXXlation;
  pTLCb->atlSTAClients[pwSTADescType->ucSTAId].wSTADesc.ucSwFrameRXXlation =
    pwSTADescType->ucSwFrameRXXlation;

#ifdef FEATURE_WLAN_WAPI
  pTLCb->atlSTAClients[pwSTADescType->ucSTAId].wSTADesc.ucIsWapiSta =
    pwSTADescType->ucIsWapiSta;
#endif /* FEATURE_WLAN_WAPI */

  vos_copy_macaddr( &pTLCb->atlSTAClients[pwSTADescType->ucSTAId].
                      wSTADesc.vSTAMACAddress, &pwSTADescType->vSTAMACAddress);

  vos_copy_macaddr( &pTLCb->atlSTAClients[pwSTADescType->ucSTAId].
                      wSTADesc.vBSSIDforIBSS, &pwSTADescType->vBSSIDforIBSS);

  vos_copy_macaddr( &pTLCb->atlSTAClients[pwSTADescType->ucSTAId].
                 wSTADesc.vSelfMACAddress, &pwSTADescType->vSelfMACAddress);

#ifdef ANI_CHIPSET_VOLANS
  /* In volans release L replay check is done at TL */
  pTLCb->atlSTAClients[pwSTADescType->ucSTAId].ucIsReplayCheckValid = 
    pwSTADescType-> ucIsReplayCheckValid;
  pTLCb->atlSTAClients[pwSTADescType->ucSTAId].ulTotalReplayPacketsDetected =  0;
/*Clear replay counters of the STA on all TIDs*/
  for(ucTid = 0; ucTid < WLANTL_MAX_TID ; ucTid++)
  {
    pTLCb->atlSTAClients[pwSTADescType->ucSTAId].ullReplayCounter[ucTid] =  0;
  }
#endif

  /*--------------------------------------------------------------------
      Set the AC for the registered station to the highest priority AC
      Even if this AC is not supported by the station, correction will be
      made in the main TL loop after the supported mask is properly
      updated in the pending packets call
    --------------------------------------------------------------------*/
  pTLCb->atlSTAClients[pwSTADescType->ucSTAId].ucCurrentAC     = WLANTL_AC_VO;
  pTLCb->atlSTAClients[pwSTADescType->ucSTAId].ucCurrentWeight = 0;
  pTLCb->atlSTAClients[pwSTADescType->ucSTAId].ucServicedAC    = WLANTL_AC_BK;
  
  vos_mem_zero( pTLCb->atlSTAClients[pwSTADescType->ucSTAId].aucACMask,
                sizeof(pTLCb->atlSTAClients[pwSTADescType->ucSTAId].aucACMask)); 

  vos_mem_zero( &pTLCb->atlSTAClients[pwSTADescType->ucSTAId].wUAPSDInfo,
           sizeof(pTLCb->atlSTAClients[pwSTADescType->ucSTAId].wUAPSDInfo));

  /*--------------------------------------------------------------------
    Reordering info and AMSDU de-aggregation
    --------------------------------------------------------------------*/
  vos_mem_zero( pTLCb->atlSTAClients[pwSTADescType->ucSTAId].atlBAReorderInfo,
     sizeof(pTLCb->atlSTAClients[pwSTADescType->ucSTAId].atlBAReorderInfo[0])*
     WLAN_MAX_TID);

  vos_mem_zero( pTLCb->atlSTAClients[pwSTADescType->ucSTAId].aucMPDUHeader,
                WLANTL_MPDU_HEADER_LEN);

  pTLCb->atlSTAClients[pwSTADescType->ucSTAId].ucMPDUHeaderLen   = 0;
  pTLCb->atlSTAClients[pwSTADescType->ucSTAId].vosAMSDUChain     = NULL;
  pTLCb->atlSTAClients[pwSTADescType->ucSTAId].vosAMSDUChainRoot = NULL;


  /*--------------------------------------------------------------------
    Stats info
    --------------------------------------------------------------------*/
  vos_mem_zero( pTLCb->atlSTAClients[pwSTADescType->ucSTAId].auRxCount,
      sizeof(pTLCb->atlSTAClients[pwSTADescType->ucSTAId].auRxCount[0])*
      WLAN_MAX_TID);

  vos_mem_zero( pTLCb->atlSTAClients[pwSTADescType->ucSTAId].auTxCount,
      sizeof(pTLCb->atlSTAClients[pwSTADescType->ucSTAId].auRxCount[0])*
      WLAN_MAX_TID);
  /* Initial RSSI is always reported as zero because TL doesnt have enough
     data to calculate RSSI. So to avoid reporting zero, we are initializing
     RSSI with RSSI saved in BssDescription during scanning. */
  pTLCb->atlSTAClients[pwSTADescType->ucSTAId].rssiAvg = rssi;

  /*Tx not suspended and station fully registered*/
  vos_atomic_set_U8(
        &pTLCb->atlSTAClients[pwSTADescType->ucSTAId].ucTxSuspended, 0);

  /* Used until multiple station support will be added*/
  pTLCb->ucRegisteredStaId = pwSTADescType->ucSTAId;

  /* Save the BAP station ID for future usage */
  if ( WLAN_STA_BT_AMP == pwSTADescType->wSTAType )
  {
    pTLCb->tlBAPClient.ucBAPSTAId = pwSTADescType->ucSTAId;
  }

  /*------------------------------------------------------------------------
    Statistics info 
    -----------------------------------------------------------------------*/
  memset(&pTLCb->atlSTAClients[pwSTADescType->ucSTAId].trafficStatistics,
         0, sizeof(WLANTL_TRANSFER_STA_TYPE));


  /*------------------------------------------------------------------------
    Start with the state suggested by client caller
    -----------------------------------------------------------------------*/
  pTLCb->atlSTAClients[pwSTADescType->ucSTAId].tlState = 
    pwSTADescType->ucInitState;

  pTLCb->atlSTAClients[pwSTADescType->ucSTAId].ucRxBlocked = 1; 
  /*-----------------------------------------------------------------------
    After all the init is complete we can mark the existance flag 
    ----------------------------------------------------------------------*/
  pTLCb->atlSTAClients[pwSTADescType->ucSTAId].ucExists++;

#ifdef WLAN_SOFTAP_FEATURE
  //flow control fields init
  pTLCb->atlSTAClients[pwSTADescType->ucSTAId].ucLwmModeEnabled = FALSE;
  pTLCb->atlSTAClients[pwSTADescType->ucSTAId].ucLwmEventReported = FALSE;
  pTLCb->atlSTAClients[pwSTADescType->ucSTAId].bmuMemConsumed = 0;
  pTLCb->atlSTAClients[pwSTADescType->ucSTAId].uIngress_length = 0;
  pTLCb->atlSTAClients[pwSTADescType->ucSTAId].uBuffThresholdMax = WLANTL_STA_BMU_THRESHOLD_MAX;

  pTLCb->atlSTAClients[pwSTADescType->ucSTAId].uLwmThreshold = WLANTL_STA_BMU_THRESHOLD_MAX / 3;

  //@@@ HDDSOFTAP does not queue unregistered packet for now
  if ( WLAN_STA_SOFTAP != pwSTADescType->wSTAType )
  { 
#endif
    /*------------------------------------------------------------------------
      Forward received frames while STA was not yet registered 
    -  ----------------------------------------------------------------------*/
    if(!VOS_IS_STATUS_SUCCESS(WLANTL_StartForwarding( pwSTADescType->ucSTAId, 
                              pwSTADescType->ucUcastSig, 
                              pwSTADescType->ucBcastSig)))
    {
      VOS_TRACE(VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
         " %s fails to start forwarding", __FUNCTION__);
    }
#ifdef WLAN_SOFTAP_FEATURE
  }
#endif
  return VOS_STATUS_SUCCESS;
}/* WLANTL_RegisterSTAClient */

/*===========================================================================

  FUNCTION    WLANTL_ClearSTAClient

  DESCRIPTION

    HDD will call this API when it no longer needs data services for the
    particular station.

  DEPENDENCIES

    A station must have been registered before the clear registration is
    called.

  PARAMETERS

   pvosGCtx:        pointer to the global vos context; a handle to TL's
                    control block can be extracted from its context
   ucSTAId:         identifier for the STA to be cleared

  RETURN VALUE

    The result code associated with performing the operation

    VOS_STATUS_E_FAULT: Station ID is outside array boundaries or pointer to
                        TL cb is NULL ; access would cause a page fault
    VOS_STATUS_E_EXISTS: Station was not registered
    VOS_STATUS_SUCCESS:  Everything is good :)

  SIDE EFFECTS

============================================================================*/
VOS_STATUS
WLANTL_ClearSTAClient
(
  v_PVOID_t         pvosGCtx,
  v_U8_t            ucSTAId
)
{
  WLANTL_CbType*  pTLCb = NULL; 
  v_U8_t  ucIndex;
  /*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

  /*------------------------------------------------------------------------
    Sanity check
   ------------------------------------------------------------------------*/
  if ( WLANTL_STA_ID_INVALID( ucSTAId ) )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
           "WLAN TL:Invalid station id requested on WLANTL_ClearSTAClient"));
    return VOS_STATUS_E_FAULT;
  }

  /*------------------------------------------------------------------------
    Extract TL control block
   ------------------------------------------------------------------------*/
  pTLCb = VOS_GET_TL_CB(pvosGCtx);
  if ( NULL == pTLCb )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
       "WLAN TL:Invalid TL pointer from pvosGCtx on WLANTL_ClearSTAClient"));
    return VOS_STATUS_E_FAULT;
  }

  if ( 0 == pTLCb->atlSTAClients[ucSTAId].ucExists )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
    "WLAN TL:Station was not previously registered on WLANTL_ClearSTAClient"));
    return VOS_STATUS_E_EXISTS;
  }

  /* Delete BA sessions on all TID's */
  for ( ucIndex = 0; ucIndex < WLAN_MAX_TID ; ucIndex++) 
  {
     WLANTL_BaSessionDel (pvosGCtx, ucSTAId, ucIndex);
  }

  /*------------------------------------------------------------------------
    Clear station
   ------------------------------------------------------------------------*/
  TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
             "WLAN TL:Clearing STA Client ID: %d", ucSTAId ));
  WLANTL_CleanSTA(&pTLCb->atlSTAClients[ucSTAId], 1 /*empty packets*/);

  TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
             "WLAN TL:Clearing STA Reset History RSSI and Region number"));
  pTLCb->hoSupport.currentHOState.historyRSSI = 0;
  pTLCb->hoSupport.currentHOState.regionNumber = 0;

  return VOS_STATUS_SUCCESS;
}/* WLANTL_ClearSTAClient */

/*===========================================================================

  FUNCTION    WLANTL_ChangeSTAState

  DESCRIPTION

    HDD will make this notification whenever a change occurs in the
    connectivity state of a particular STA.

  DEPENDENCIES

    A station must have been registered before the change state can be
    called.

    RESTRICTION: A station is being notified as authenticated before the
                 keys are installed in HW. This way if a frame is received
                 before the keys are installed DPU will drop that frame.

    Main thread has higher priority that Tx and Rx threads thus guaranteeing
    the following:
        - a station will be in assoc state in TL before TL receives any data
          for it

  PARAMETERS

   pvosGCtx:        pointer to the global vos context; a handle to TL's
                    control block can be extracted from its context
   ucSTAId:         identifier for the STA that is pending transmission
   tlSTAState:     the new state of the connection to the given station


  RETURN VALUE

    The result code associated with performing the operation

    VOS_STATUS_E_INVAL:  Input parameters are invalid
    VOS_STATUS_E_FAULT:  Station ID is outside array boundaries or pointer to
                         TL cb is NULL ; access would cause a page fault
    VOS_STATUS_E_EXISTS: Station was not registered
    VOS_STATUS_SUCCESS:  Everything is good :)

  SIDE EFFECTS

============================================================================*/
VOS_STATUS
WLANTL_ChangeSTAState
(
  v_PVOID_t             pvosGCtx,
  v_U8_t                ucSTAId,
  WLANTL_STAStateType   tlSTAState
)
{
  WLANTL_CbType*  pTLCb = NULL;
  /*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

  /*------------------------------------------------------------------------
    Sanity check
   ------------------------------------------------------------------------*/
  if ( tlSTAState >= WLANTL_STA_MAX_STATE )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
              "WLAN TL:Invalid parameter sent on WLANTL_ChangeSTAState"));
    return VOS_STATUS_E_INVAL;
  }

  if ( WLANTL_STA_ID_INVALID( ucSTAId ) )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
             "WLAN TL:Invalid station id requested on WLANTL_ChangeSTAState"));
    return VOS_STATUS_E_FAULT;
  }

  /*------------------------------------------------------------------------
    Extract TL control block and check existance
   ------------------------------------------------------------------------*/
  pTLCb = VOS_GET_TL_CB(pvosGCtx);
  if ( NULL == pTLCb )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
         "WLAN TL:Invalid TL pointer from pvosGCtx on WLANTL_ChangeSTAState"));
    return VOS_STATUS_E_FAULT;
  }

  if ( 0 == pTLCb->atlSTAClients[ucSTAId].ucExists )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
     "WLAN TL:Station was not previously registered on WLANTL_ChangeSTAState"));
    return VOS_STATUS_E_EXISTS;
  }

  /*------------------------------------------------------------------------
    Change STA state
    No need to lock this operation, see restrictions above
   ------------------------------------------------------------------------*/
  TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
             "WLAN TL:Changing state for STA Client ID: %d from %d to %d",
             ucSTAId, pTLCb->atlSTAClients[ucSTAId].tlState, tlSTAState));

  pTLCb->atlSTAClients[ucSTAId].tlState = tlSTAState;

  return VOS_STATUS_SUCCESS;
}/* WLANTL_ChangeSTAState */

/*===========================================================================

  FUNCTION    WLANTL_STAPktPending

  DESCRIPTION

    HDD will call this API when a packet is pending transmission in its
    queues.

  DEPENDENCIES

    A station must have been registered before the packet pending
    notification can be sent.

    RESTRICTION: TL will not count packets for pending notification.
                 HDD is expected to send the notification only when
                 non-empty event gets triggered. Worst case scenario
                 is that TL might end up making a call when Hdds
                 queues are actually empty.

  PARAMETERS

    pvosGCtx:    pointer to the global vos context; a handle to TL's
                 control block can be extracted from its context
    ucSTAId:     identifier for the STA that is pending transmission

  RETURN VALUE

    The result code associated with performing the operation

    VOS_STATUS_E_INVAL:  Input parameters are invalid
    VOS_STATUS_E_FAULT:  Station ID is outside array boundaries or pointer
                         to TL cb is NULL ; access would cause a page fault
    VOS_STATUS_E_EXISTS: Station was not registered
    VOS_STATUS_SUCCESS:  Everything is good :)

  SIDE EFFECTS

============================================================================*/
VOS_STATUS
WLANTL_STAPktPending
(
  v_PVOID_t            pvosGCtx,
  v_U8_t               ucSTAId,
  WLANTL_ACEnumType    ucAc
)
{
  WLANTL_CbType*  pTLCb = NULL;
#ifdef WLAN_SOFTAP_FEATURE
  vos_msg_t      vosMsg;
#endif
  /*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

  VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO,
      "WLAN TL:Packet pending indication for STA: %d AC: %d", ucSTAId, ucAc);

  /*------------------------------------------------------------------------
    Sanity check
   ------------------------------------------------------------------------*/
  if ( WLANTL_STA_ID_INVALID( ucSTAId ) )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
              "WLAN TL:Invalid station id requested on WLANTL_STAPktPending"));
    return VOS_STATUS_E_FAULT;
  }

  /*------------------------------------------------------------------------
    Extract TL control block and check existance
   ------------------------------------------------------------------------*/
  pTLCb = VOS_GET_TL_CB(pvosGCtx);
  if ( NULL == pTLCb )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
         "WLAN TL:Invalid TL pointer from pvosGCtx on WLANTL_STAPktPending"));
    return VOS_STATUS_E_FAULT;
  }

  if ( 0 == pTLCb->atlSTAClients[ucSTAId].ucExists )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
    "WLAN TL:Station was not previously registered on WLANTL_STAPktPending"));
    return VOS_STATUS_E_EXISTS;
  }

  /*---------------------------------------------------------------------
    Temporary fix to enable TL to fetch packets when multiple peers join
    an IBSS. To fix CR177301. Needs to go away when the actual fix of
    going through all STA's in round robin fashion gets merged in from
    BT AMP branch.
    --------------------------------------------------------------------*/
  pTLCb->ucRegisteredStaId = ucSTAId;

  /*-----------------------------------------------------------------------
    Enable this AC in the AC mask in order for TL to start servicing it
    Set packet pending flag 
    To avoid race condition, serialize the updation of AC and AC mask 
    through WLANTL_TX_STAID_AC_IND message.
  -----------------------------------------------------------------------*/
#ifdef WLAN_SOFTAP_FEATURE
    if (WLAN_STA_SOFTAP != pTLCb->atlSTAClients[ucSTAId].wSTADesc.wSTAType)
    {
#endif

      pTLCb->atlSTAClients[ucSTAId].aucACMask[ucAc] = 1; 

      vos_atomic_set_U8( &pTLCb->atlSTAClients[ucSTAId].ucPktPending, 1);

      /*------------------------------------------------------------------------
        Check if there are enough resources for transmission and tx is not
        suspended.
        ------------------------------------------------------------------------*/
       if (( pTLCb->uResCount >=  WDA_TLI_MIN_RES_DATA ) &&
          ( 0 == pTLCb->ucTxSuspended ))
      {
        TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
              "Issuing Xmit start request to BAL"));
           WDA_DS_StartXmit(pvosGCtx);
      }
      else
      {
        /*---------------------------------------------------------------------
          No error code is sent because TL will resume tx autonomously if
          resources become available or tx gets resumed
          ---------------------------------------------------------------------*/
        TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
              "WLAN TL:Request to send but condition not met. Res: %d,Suspend: %d",
              pTLCb->uResCount, pTLCb->ucTxSuspended ));
      }
#ifdef WLAN_SOFTAP_FEATURE
    }
    else
    {
      vosMsg.reserved = 0;
      vosMsg.bodyval  = 0;
      vosMsg.bodyval = (ucAc | (ucSTAId << WLANTL_STAID_OFFSET));
      vosMsg.type     = WLANTL_TX_STAID_AC_IND;
      return vos_tx_mq_serialize( VOS_MQ_ID_TL, &vosMsg);
    }
#endif
  return VOS_STATUS_SUCCESS;
}/* WLANTL_STAPktPending */

/*==========================================================================

  FUNCTION    WLANTL_SetSTAPriority

  DESCRIPTION

    TL exposes this API to allow upper layers a rough control over the
    priority of transmission for a given station when supporting multiple
    connections.

  DEPENDENCIES

    A station must have been registered before the change in priority can be
    called.

  PARAMETERS

    pvosGCtx:       pointer to the global vos context; a handle to TL's
                    control block can be extracted from its context
    ucSTAId:        identifier for the STA that has to change priority

  RETURN VALUE

    The result code associated with performing the operation

    VOS_STATUS_E_INVAL:  Input parameters are invalid
    VOS_STATUS_E_FAULT:  Station ID is outside array boundaries or pointer
                         to TL cb is NULL ; access would cause a page fault
    VOS_STATUS_E_EXISTS: Station was not registered
    VOS_STATUS_SUCCESS:  Everything is good :)

  SIDE EFFECTS

============================================================================*/
VOS_STATUS
WLANTL_SetSTAPriority
(
  v_PVOID_t            pvosGCtx,
  v_U8_t               ucSTAId,
  WLANTL_STAPriorityType   tlSTAPri
)
{
  WLANTL_CbType*  pTLCb = NULL;
  /*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

  /*------------------------------------------------------------------------
    Sanity check
   ------------------------------------------------------------------------*/
  if ( WLANTL_STA_ID_INVALID( ucSTAId ) )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
            "WLAN TL:Invalid station id requested on WLANTL_SetSTAPriority"));
    return VOS_STATUS_E_FAULT;
  }

  /*------------------------------------------------------------------------
    Extract TL control block
   ------------------------------------------------------------------------*/
  pTLCb = VOS_GET_TL_CB(pvosGCtx);
  if ( NULL == pTLCb )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
        "WLAN TL:Invalid TL pointer from pvosGCtx on WLANTL_SetSTAPriority"));
    return VOS_STATUS_E_FAULT;
  }

  if ( 0 == pTLCb->atlSTAClients[ucSTAId].ucExists )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
    "WLAN TL:Station was not previously registered on WLANTL_SetSTAPriority"));
    return VOS_STATUS_E_EXISTS;
  }

  /*------------------------------------------------------------------------
    Re-analize if lock is needed when adding multiple stations
   ------------------------------------------------------------------------*/
  TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
             "WLAN TL:Changing state for STA Pri ID: %d from %d to %d",
             ucSTAId, pTLCb->atlSTAClients[ucSTAId].tlPri, tlSTAPri));
  pTLCb->atlSTAClients[ucSTAId].tlPri = tlSTAPri;

  return VOS_STATUS_SUCCESS;
}/* WLANTL_SetSTAPriority */


/*----------------------------------------------------------------------------
    INTERACTION WITH BAP
 ---------------------------------------------------------------------------*/

/*==========================================================================

  FUNCTION    WLANTL_RegisterBAPClient

  DESCRIPTION
    Called by SME to register itself as client for non-data BT-AMP packets.

  DEPENDENCIES
    TL must be initialized before this function can be called.

  PARAMETERS

    IN
    pvosGCtx:       pointer to the global vos context; a handle to TL's
                    or SME's control block can be extracted from its context
    pfnTlBAPRxFrm:  pointer to the receive processing routine for non-data
                    BT-AMP packets
    pfnFlushOpCompleteCb:
                    pointer to the call back function, for the Flush operation
                    completion.


  RETURN VALUE

    The result code associated with performing the operation

    VOS_STATUS_E_INVAL:  Input parameters are invalid
    VOS_STATUS_E_FAULT:  Station ID is outside array boundaries or pointer
                         to TL cb is NULL ; access would cause a page fault
    VOS_STATUS_E_EXISTS: BAL client was already registered
    VOS_STATUS_SUCCESS:  Everything is good :)

  SIDE EFFECTS

============================================================================*/
VOS_STATUS
WLANTL_RegisterBAPClient
(
  v_PVOID_t              pvosGCtx,
  WLANTL_BAPRxCBType     pfnTlBAPRxFrm,
  WLANTL_FlushOpCompCBType  pfnFlushOpCompleteCb
)
{
  WLANTL_CbType*  pTLCb = NULL;
  /*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

  /*------------------------------------------------------------------------
    Sanity check
   ------------------------------------------------------------------------*/
  if ( NULL == pfnTlBAPRxFrm )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
              "WLAN TL:Invalid parameter sent on WLANTL_RegisterBAPClient"));
    return VOS_STATUS_E_INVAL;
  }

  if ( NULL == pfnFlushOpCompleteCb )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
      "Invalid Flush Complete Cb parameter sent on WLANTL_RegisterBAPClient"));
    return VOS_STATUS_E_INVAL;
  }

  /*------------------------------------------------------------------------
    Extract TL control block
   ------------------------------------------------------------------------*/
  pTLCb = VOS_GET_TL_CB(pvosGCtx);
  if ( NULL == pTLCb )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
    "WLAN TL:Invalid TL pointer from pvosGCtx on WLANTL_RegisterBAPClient"));
    return VOS_STATUS_E_FAULT;
  }

  /*------------------------------------------------------------------------
    Make sure this is the first registration attempt
   ------------------------------------------------------------------------*/
  if ( 0 != pTLCb->tlBAPClient.ucExists )
  {
    pTLCb->tlBAPClient.ucExists++;
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
              "WLAN TL:BAP client was already registered"));
    return VOS_STATUS_E_EXISTS;
  }

  /*------------------------------------------------------------------------
    Register station with TL
   ------------------------------------------------------------------------*/
  TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
             "WLAN TL:Registering BAP Client" ));

  pTLCb->tlBAPClient.ucExists++;

  if ( NULL != pfnTlBAPRxFrm ) 
  {
    pTLCb->tlBAPClient.pfnTlBAPRx             = pfnTlBAPRxFrm;
  }

  pTLCb->tlBAPClient.pfnFlushOpCompleteCb   = pfnFlushOpCompleteCb;

  pTLCb->tlBAPClient.vosPendingDataBuff     = NULL;

  return VOS_STATUS_SUCCESS;
}/* WLANTL_RegisterBAPClient */


/*==========================================================================

  FUNCTION    WLANTL_TxBAPFrm

  DESCRIPTION
    BAP calls this when it wants to send a frame to the module

  DEPENDENCIES
    BAP must be registered with TL before this function can be called.

    RESTRICTION: BAP CANNOT push any packets to TL until it did not receive
                 a tx complete from the previous packet, that means BAP
                 sends one packet, wait for tx complete and then
                 sends another one

                 If BAP sends another packet before TL manages to process the
                 previously sent packet call will end in failure

  PARAMETERS

    IN
    pvosGCtx:       pointer to the global vos context; a handle to TL's
                    or BAP's control block can be extracted from its context
    vosDataBuff:   pointer to the vOSS buffer containing the packet to be
                    transmitted
    pMetaInfo:      meta information about the packet
    pfnTlBAPTxComp: pointer to a transmit complete routine for notifying
                    the result of the operation over the bus

  RETURN VALUE
    The result code associated with performing the operation

    VOS_STATUS_E_FAULT:  pointer to TL cb is NULL ; access would cause a
                         page fault
    VOS_STATUS_E_EXISTS: BAL client was not yet registered
    VOS_STATUS_E_BUSY:   The previous BT-AMP packet was not yet transmitted
    VOS_STATUS_SUCCESS:  Everything is good :)

    Other failure messages may be returned from the BD header handling
    routines, please check apropriate API for more info.

  SIDE EFFECTS

============================================================================*/
VOS_STATUS
WLANTL_TxBAPFrm
(
  v_PVOID_t               pvosGCtx,
  vos_pkt_t*              vosDataBuff,
  WLANTL_MetaInfoType*    pMetaInfo,
  WLANTL_TxCompCBType     pfnTlBAPTxComp
)
{
  WLANTL_CbType*  pTLCb      = NULL;
  VOS_STATUS      vosStatus  = VOS_STATUS_SUCCESS;
  v_MACADDR_t     vDestMacAddr;
  v_U16_t         usPktLen;
  v_U8_t          ucStaId = 0;
  v_U8_t          extraHeadSpace = 0;
  v_U8_t          ucWDSEnabled = 0;
  /*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

  /*------------------------------------------------------------------------
    Sanity check
    Extract TL control block
   ------------------------------------------------------------------------*/
  pTLCb = VOS_GET_TL_CB(pvosGCtx);
  if ( NULL == pTLCb )
  {
     TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
              "WLAN TL:Invalid TL pointer from pvosGCtx on WLANTL_TxBAPFrm"));
    return VOS_STATUS_E_FAULT;
  }

  /*------------------------------------------------------------------------
    Ensure that BAP client was registered previously
   ------------------------------------------------------------------------*/
  if (( 0 == pTLCb->tlBAPClient.ucExists ) ||
      ( WLANTL_STA_ID_INVALID(pTLCb->tlBAPClient.ucBAPSTAId) ))
  {
    pTLCb->tlBAPClient.ucExists++;
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
               "WLAN TL:BAP client not register on WLANTL_TxBAPFrm"));
    return VOS_STATUS_E_EXISTS;
  }

  /*------------------------------------------------------------------------
   Check if any BT-AMP Frm is pending
  ------------------------------------------------------------------------*/
  if ( NULL != pTLCb->tlBAPClient.vosPendingDataBuff )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
        "WLAN TL:BT-AMP Frame already pending tx in TL on WLANTL_TxBAPFrm"));
    return VOS_STATUS_E_BUSY;
  }

  /*------------------------------------------------------------------------
    Save buffer and notify BAL; no lock is needed if the above restriction
    is met
    Save the tx complete fnct pointer as tl specific data in the vos buffer
   ------------------------------------------------------------------------*/

  /*------------------------------------------------------------------------
    Translate 802.3 frame to 802.11
   ------------------------------------------------------------------------*/
  ucStaId = pTLCb->tlBAPClient.ucBAPSTAId;
  if (( 0 == pMetaInfo->ucDisableFrmXtl ) &&
      ( 0 != pTLCb->atlSTAClients[ucStaId].wSTADesc.ucSwFrameTXXlation ))
  {
    vosStatus = WLANTL_Translate8023To80211Header(vosDataBuff, &vosStatus,
                                                  pTLCb, ucStaId,
                                                  pMetaInfo->ucUP, &ucWDSEnabled, &extraHeadSpace);

    if ( VOS_STATUS_SUCCESS != vosStatus )
    {
      TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
                "WLAN TL:Error when translating header WLANTL_TxBAPFrm"));

      return vosStatus;
    }

    pMetaInfo->ucDisableFrmXtl = 1;
  }

  /*-------------------------------------------------------------------------
    Call HAL to fill BD header
   -------------------------------------------------------------------------*/

  /* Adding Type, SubType which was missing for EAPOL from BAP */
  pMetaInfo->ucType |= (WLANTL_80211_DATA_TYPE << 4);
  pMetaInfo->ucType |= (WLANTL_80211_DATA_QOS_SUBTYPE);

  vosStatus = WDA_DS_BuildTxPacketInfo( pvosGCtx, vosDataBuff , 
                    &vDestMacAddr, pMetaInfo->ucDisableFrmXtl, 
                    &usPktLen, pTLCb->atlSTAClients[ucStaId].wSTADesc.ucQosEnabled, 
                    ucWDSEnabled, extraHeadSpace, pMetaInfo->ucType,
                            &pTLCb->atlSTAClients[ucStaId].wSTADesc.vSelfMACAddress,
                    pMetaInfo->ucTID, 0 /* No ACK */, pMetaInfo->usTimeStamp,
                    pMetaInfo->ucIsEapol, pMetaInfo->ucUP );

  if ( VOS_STATUS_SUCCESS != vosStatus )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
               "WLAN TL:Failed while building TX header %d", vosStatus));
    return vosStatus;
  }

  if ( NULL != pfnTlBAPTxComp )
  {
    vos_pkt_set_user_data_ptr( vosDataBuff, VOS_PKT_USER_DATA_ID_TL,
                               (v_PVOID_t)pfnTlBAPTxComp);
  }
  else
  {
    vos_pkt_set_user_data_ptr( vosDataBuff, VOS_PKT_USER_DATA_ID_TL,
                               (v_PVOID_t)WLANTL_TxCompDefaultCb);

  }

  vos_atomic_set_U32( (v_U32_t*)&pTLCb->tlBAPClient.vosPendingDataBuff,
                      (v_U32_t)vosDataBuff);

  /*------------------------------------------------------------------------
    Check if thre are enough resources for transmission and tx is not
    suspended.
   ------------------------------------------------------------------------*/
  if (( pTLCb->uResCount >=  WDA_TLI_MIN_RES_BAP ) &&
      ( 0 == pTLCb->ucTxSuspended ))
  {
    WDA_DS_StartXmit(pvosGCtx);
  }
  else
  {
    /*---------------------------------------------------------------------
      No error code is sent because TL will resume tx autonomously if
      resources become available or tx gets resumed
     ---------------------------------------------------------------------*/
    TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
          "WLAN TL:Request to send from BAP but condition not met.Res: %d,"
                 "Suspend: %d", pTLCb->uResCount, pTLCb->ucTxSuspended ));
  }

  return VOS_STATUS_SUCCESS;
}/* WLANTL_TxBAPFrm */


/*----------------------------------------------------------------------------
    INTERACTION WITH SME
 ---------------------------------------------------------------------------*/

/*==========================================================================

  FUNCTION    WLANTL_GetRssi

  DESCRIPTION
    TL will extract the RSSI information from every data packet from the
    ongoing traffic and will store it. It will provide the result to SME
    upon request.

  DEPENDENCIES

    WARNING: the read and write of this value will not be protected
             by locks, therefore the information obtained after a read
             might not always be consistent.

  PARAMETERS

    IN
    pvosGCtx:       pointer to the global vos context; a handle to TL's
                    or SME's control block can be extracted from its context
    ucSTAId:        station identifier for the requested value

    OUT
    puRssi:         the average value of the RSSI


  RETURN VALUE
    The result code associated with performing the operation

    VOS_STATUS_E_INVAL:  Input parameters are invalid
    VOS_STATUS_E_FAULT:  Station ID is outside array boundaries or pointer
                         to TL cb is NULL ; access would cause a page fault
    VOS_STATUS_E_EXISTS: STA was not yet registered
    VOS_STATUS_SUCCESS:  Everything is good :)

  SIDE EFFECTS

============================================================================*/
VOS_STATUS
WLANTL_GetRssi
(
  v_PVOID_t        pvosGCtx,
  v_U8_t           ucSTAId,
  v_S7_t*          pRssi
)
{
  WLANTL_CbType*  pTLCb = NULL;
  /*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

  /*------------------------------------------------------------------------
    Sanity check
   ------------------------------------------------------------------------*/
  if ( NULL == pRssi )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
               "WLAN TL:Invalid parameter sent on WLANTL_GetRssi"));
    return VOS_STATUS_E_INVAL;
  }

  if ( WLANTL_STA_ID_INVALID( ucSTAId ) )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
               "WLAN TL:Invalid station id requested on WLANTL_GetRssi"));
    return VOS_STATUS_E_FAULT;
  }

  /*------------------------------------------------------------------------
    Extract TL control block and check existance
   ------------------------------------------------------------------------*/
  pTLCb = VOS_GET_TL_CB(pvosGCtx);
  if ( NULL == pTLCb )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
              "WLAN TL:Invalid TL pointer from pvosGCtx on WLANTL_GetRssi"));
    return VOS_STATUS_E_FAULT;
  }

  if ( 0 == pTLCb->atlSTAClients[ucSTAId].ucExists )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
         "WLAN TL:Station was not previously registered on WLANTL_GetRssi"));
    return VOS_STATUS_E_EXISTS;
  }

  /*------------------------------------------------------------------------
    Copy will not be locked; please read restriction
   ------------------------------------------------------------------------*/
  if(pTLCb->isBMPS)
  {
    *pRssi = pTLCb->atlSTAClients[ucSTAId].rssiAvgBmps;
    /* Check If RSSI is zero because we are reading rssAvgBmps updated by HAL in 
    previous GetStatsRequest. It may be updated as zero by Hal because EnterBmps 
    might not have happend by that time. Hence reading the most recent Rssi 
    calcluated by TL*/
    if(0 == *pRssi)
    {
      *pRssi = pTLCb->atlSTAClients[ucSTAId].rssiAvg;
    }
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
                                 "WLAN TL:bmpsRssi %d \n",*pRssi));
  }
  else
  {
    *pRssi = pTLCb->atlSTAClients[ucSTAId].rssiAvg;
  }

  TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
            "WLAN TL:WLANTL_GetRssi for STA: %d RSSI: %d", ucSTAId, *puRssi));

  return VOS_STATUS_SUCCESS;
}/* WLANTL_GetRssi */

/*==========================================================================

  FUNCTION    WLANTL_GetLinkQuality

  DESCRIPTION
    TL will extract the SNR information from every data packet from the
    ongoing traffic and will store it. It will provide the result to SME
    upon request.

  DEPENDENCIES

    WARNING: the read and write of this value will not be protected
             by locks, therefore the information obtained after a read
             might not always be consistent.

  PARAMETERS

    IN
    pvosGCtx:       pointer to the global vos context; a handle to TL's
                    or SME's control block can be extracted from its context
    ucSTAId:        station identifier for the requested value

    OUT
    puLinkQuality:         the average value of the SNR


  RETURN VALUE
    The result code associated with performing the operation

    VOS_STATUS_E_INVAL:  Input parameters are invalid
    VOS_STATUS_E_FAULT:  Station ID is outside array boundaries or pointer
                         to TL cb is NULL ; access would cause a page fault
    VOS_STATUS_E_EXISTS: STA was not yet registered
    VOS_STATUS_SUCCESS:  Everything is good :)

  SIDE EFFECTS

============================================================================*/
VOS_STATUS
WLANTL_GetLinkQuality
(
  v_PVOID_t             pvosGCtx,
  v_U8_t                ucSTAId,
  v_U32_t*              puLinkQuality
)
{
  WLANTL_CbType*  pTLCb = NULL;

  /*------------------------------------------------------------------------
    Sanity check
   ------------------------------------------------------------------------*/
  if ( NULL == puLinkQuality )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
                 "Invalid parameter sent on WLANTL_GetLinkQuality"));
    return VOS_STATUS_E_INVAL;
  }

  if ( WLANTL_STA_ID_INVALID( ucSTAId ) )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
                 "Invalid station id requested on WLANTL_GetLinkQuality"));
    return VOS_STATUS_E_FAULT;
  }

  /*------------------------------------------------------------------------
    Extract TL control block and check existance
   ------------------------------------------------------------------------*/
  pTLCb = VOS_GET_TL_CB(pvosGCtx);
  if ( NULL == pTLCb )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
                 "Invalid TL pointer from pvosGCtx on WLANTL_GetLinkQuality"));
    return VOS_STATUS_E_FAULT;
  }

  if ( 0 == pTLCb->atlSTAClients[ucSTAId].ucExists )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
                 "Station was not previously registered on WLANTL_GetLinkQuality"));
    return VOS_STATUS_E_EXISTS;
  }

  /*------------------------------------------------------------------------
    Copy will not be locked; please read restriction
   ------------------------------------------------------------------------*/
  *puLinkQuality = pTLCb->atlSTAClients[ucSTAId].uLinkQualityAvg;

  TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
               "WLANTL_GetLinkQuality for STA: %d LinkQuality: %d", ucSTAId, *puLinkQuality));

  return VOS_STATUS_SUCCESS;
}/* WLANTL_GetLinkQuality */

/*==========================================================================

  FUNCTION    WLANTL_FlushStaTID

  DESCRIPTION
    TL provides this API as an interface to SME (BAP) layer. TL inturn posts a
    message to HAL. This API is called by the SME inorder to perform a flush
    operation.

  DEPENDENCIES

  PARAMETERS

    IN
    pvosGCtx:       pointer to the global vos context; a handle to TL's
                    or SME's control block can be extracted from its context
    ucSTAId:        station identifier for the requested value
    ucTid:          Tspec ID for the new BA session

    OUT
    The response for this post is received in the main thread, via a response
    message from HAL to TL.

  RETURN VALUE
    VOS_STATUS_SUCCESS:  Everything is good :)

  SIDE EFFECTS
============================================================================*/
VOS_STATUS
WLANTL_FlushStaTID
(
  v_PVOID_t             pvosGCtx,
  v_U8_t                ucSTAId,
  v_U8_t                ucTid
)
{
  WLANTL_CbType*  pTLCb = NULL;
  tpFlushACReq FlushACReqPtr = NULL;
  vos_msg_t vosMessage;


  if ( WLANTL_STA_ID_INVALID( ucSTAId ) )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
                 "Invalid station id requested on WLANTL_FlushStaTID"));
    return VOS_STATUS_E_FAULT;
  }

  /*------------------------------------------------------------------------
    Extract TL control block and check existance
   ------------------------------------------------------------------------*/
  pTLCb = VOS_GET_TL_CB(pvosGCtx);
  if ( NULL == pTLCb )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
                 "Invalid TL pointer from pvosGCtx on WLANTL_FlushStaTID"));
    return VOS_STATUS_E_FAULT;
  }

  if ( 0 == pTLCb->atlSTAClients[ucSTAId].ucExists )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
                 "Station was not previously registered on WLANTL_FlushStaTID"));
    return VOS_STATUS_E_EXISTS;
  }

  /*------------------------------------------------------------------------
  We need to post a message with the STA, TID value to HAL. HAL performs the flush
  ------------------------------------------------------------------------*/
  FlushACReqPtr = vos_mem_malloc(sizeof(tFlushACReq));

  if ( NULL == FlushACReqPtr )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
        "WLAN TL: fatal failure, cannot allocate Flush Req structure"));
    VOS_ASSERT(0);
    return VOS_STATUS_E_NOMEM;
  }

  // Start constructing the message for HAL
  FlushACReqPtr->mesgType    = SIR_TL_HAL_FLUSH_AC_REQ;
  FlushACReqPtr->mesgLen     = sizeof(tFlushACReq);
  FlushACReqPtr->mesgLen     = sizeof(tFlushACReq);
  FlushACReqPtr->ucSTAId = ucSTAId;
  FlushACReqPtr->ucTid = ucTid;

  vosMessage.type            = WDA_TL_FLUSH_AC_REQ;
  vosMessage.bodyptr = (void *)FlushACReqPtr;

  vos_mq_post_message(VOS_MQ_ID_WDA, &vosMessage);
  return VOS_STATUS_SUCCESS;
}


/*----------------------------------------------------------------------------
    INTERACTION WITH PE
 ---------------------------------------------------------------------------*/

/*==========================================================================

  FUNCTION    WLANTL_RegisterMgmtFrmClient

  DESCRIPTION
    Called by PE to register as a client for management frames delivery.

  DEPENDENCIES
    TL must be initialized before this API can be called.

  PARAMETERS

    IN
    pvosGCtx:           pointer to the global vos context; a handle to
                        TL's control block can be extracted from its context
    pfnTlMgmtFrmRx:     pointer to the receive processing routine for
                        management frames

  RETURN VALUE
    The result code associated with performing the operation

    VOS_STATUS_E_INVAL:  Input parameters are invalid
    VOS_STATUS_E_FAULT:  pointer to TL cb is NULL ; access would cause a
                         page fault
    VOS_STATUS_E_EXISTS: Mgmt Frame client was already registered
    VOS_STATUS_SUCCESS:  Everything is good :)

  SIDE EFFECTS

============================================================================*/
VOS_STATUS
WLANTL_RegisterMgmtFrmClient
(
  v_PVOID_t               pvosGCtx,
  WLANTL_MgmtFrmRxCBType  pfnTlMgmtFrmRx
)
{
  WLANTL_CbType*  pTLCb = NULL;
  /*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

  /*------------------------------------------------------------------------
    Sanity check
   ------------------------------------------------------------------------*/
  if ( NULL == pfnTlMgmtFrmRx )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
           "WLAN TL:Invalid parameter sent on WLANTL_RegisterMgmtFrmClient"));
    return VOS_STATUS_E_INVAL;
  }

  /*------------------------------------------------------------------------
    Extract TL control block
   ------------------------------------------------------------------------*/
  pTLCb = VOS_GET_TL_CB(pvosGCtx);
  if ( NULL == pTLCb )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
          "WLAN TL:Invalid TL pointer from pvosGCtx on WLANTL_ChangeSTAState"));
    return VOS_STATUS_E_FAULT;
  }

  /*------------------------------------------------------------------------
    Make sure this is the first registration attempt
   ------------------------------------------------------------------------*/
  if ( 0 != pTLCb->tlMgmtFrmClient.ucExists )
  {
    pTLCb->tlMgmtFrmClient.ucExists++;
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
              "WLAN TL:Management frame client was already registered"));
    return VOS_STATUS_E_EXISTS;
  }

  /*------------------------------------------------------------------------
    Register station with TL
   ------------------------------------------------------------------------*/
  TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
            "WLAN TL:Registering Management Frame Client" ));

  pTLCb->tlMgmtFrmClient.ucExists++;

  if ( NULL != pfnTlMgmtFrmRx )
  {
    pTLCb->tlMgmtFrmClient.pfnTlMgmtFrmRx = pfnTlMgmtFrmRx;
  }

  pTLCb->tlMgmtFrmClient.vosPendingDataBuff = NULL;

  return VOS_STATUS_SUCCESS;
}/* WLANTL_RegisterMgmtFrmClient */

/*==========================================================================

  FUNCTION    WLANTL_DeRegisterMgmtFrmClient

  DESCRIPTION
    Called by PE to deregister as a client for management frames delivery.

  DEPENDENCIES
    TL must be initialized before this API can be called.

  PARAMETERS

    IN
    pvosGCtx:           pointer to the global vos context; a handle to
                        TL's control block can be extracted from its context
  RETURN VALUE
    The result code associated with performing the operation

    VOS_STATUS_E_FAULT:  pointer to TL cb is NULL ; access would cause a
                         page fault
    VOS_STATUS_E_EXISTS: Mgmt Frame client was never registered
    VOS_STATUS_SUCCESS:  Everything is good :)

  SIDE EFFECTS

============================================================================*/
VOS_STATUS
WLANTL_DeRegisterMgmtFrmClient
(
  v_PVOID_t               pvosGCtx
)
{
  WLANTL_CbType*  pTLCb = NULL;
  /*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

  /*------------------------------------------------------------------------
    Extract TL control block
   ------------------------------------------------------------------------*/
  pTLCb = VOS_GET_TL_CB(pvosGCtx);
  if ( NULL == pTLCb )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
          "WLAN TL:Invalid TL pointer from pvosGCtx on WLANTL_ChangeSTAState"));
    return VOS_STATUS_E_FAULT;
  }

  /*------------------------------------------------------------------------
    Make sure this is the first registration attempt
   ------------------------------------------------------------------------*/
  if ( 0 == pTLCb->tlMgmtFrmClient.ucExists )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
              "WLAN TL:Management frame client was never registered"));
    return VOS_STATUS_E_EXISTS;
  }

  /*------------------------------------------------------------------------
    Clear registration with TL
   ------------------------------------------------------------------------*/
  TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
            "WLAN TL:Deregistering Management Frame Client" ));

  pTLCb->tlMgmtFrmClient.pfnTlMgmtFrmRx = WLANTL_MgmtFrmRxDefaultCb;
  if ( NULL != pTLCb->tlMgmtFrmClient.vosPendingDataBuff)
  {
    TLLOGW(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_WARN,
              "WLAN TL:Management cache buffer not empty on deregistering"
               " - dropping packet" ));
    vos_pkt_return_packet(pTLCb->tlMgmtFrmClient.vosPendingDataBuff);

    pTLCb->tlMgmtFrmClient.vosPendingDataBuff = NULL; 
  }

  pTLCb->tlMgmtFrmClient.ucExists = 0;

  return VOS_STATUS_SUCCESS;
}/* WLANTL_RegisterMgmtFrmClient */

/*==========================================================================

  FUNCTION    WLANTL_TxMgmtFrm

  DESCRIPTION
    Called by PE when it want to send out a management frame.
    HAL will also use this API for the few frames it sends out, they are not
    management frames howevere it is accepted that an exception will be
    allowed ONLY for the usage of HAL.
    Generic data frames SHOULD NOT travel through this function.

  DEPENDENCIES
    TL must be initialized before this API can be called.

    RESTRICTION: If PE sends another packet before TL manages to process the
                 previously sent packet call will end in failure

                 Frames comming through here must be 802.11 frames, frame
                 translation in UMA will be automatically disabled.

  PARAMETERS

    IN
    pvosGCtx:       pointer to the global vos context;a handle to TL's
                    control block can be extracted from its context
    vosFrmBuf:      pointer to a vOSS buffer containing the management
                    frame to be transmitted
    usFrmLen:       the length of the frame to be transmitted; information
                    is already included in the vOSS buffer
    wFrmType:       the type of the frame being transmitted
    tid:            tid used to transmit this frame
    pfnCompTxFunc:  function pointer to the transmit complete routine
    pvBDHeader:     pointer to the BD header, if NULL it means it was not
                    yet constructed and it lies within TL's responsibility
                    to do so; if not NULL it is expected that it was
                    already packed inside the vos packet
    ucAckResponse:  flag notifying it an interrupt is needed for the
                    acknowledgement received when the frame is sent out
                    the air and ; the interrupt will be processed by HAL,
                    only one such frame can be pending in the system at
                    one time.


  RETURN VALUE
    The result code associated with performing the operation

    VOS_STATUS_E_INVAL:  Input parameters are invalid
    VOS_STATUS_E_FAULT:  pointer to TL cb is NULL ; access would cause a
                         page fault
    VOS_STATUS_E_EXISTS: Mgmt Frame client was not yet registered
    VOS_STATUS_E_BUSY:   The previous Mgmt packet was not yet transmitted
    VOS_STATUS_SUCCESS:  Everything is good :)

    Other failure messages may be returned from the BD header handling
    routines, please check apropriate API for more info.

  SIDE EFFECTS

============================================================================*/
VOS_STATUS
WLANTL_TxMgmtFrm
(
  v_PVOID_t            pvosGCtx,
  vos_pkt_t*           vosFrmBuf,
  v_U16_t              usFrmLen,
  v_U8_t               wFrmType,
  v_U8_t               ucTid,
  WLANTL_TxCompCBType  pfnCompTxFunc,
  v_PVOID_t            pvBDHeader,
  v_U8_t               ucAckResponse
)
{
  WLANTL_CbType*  pTLCb = NULL;
  v_MACADDR_t     vDestMacAddr;
  VOS_STATUS      vosStatus = VOS_STATUS_SUCCESS;
  v_U16_t         usPktLen;
  v_U32_t         usTimeStamp = 0;
  /*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

  /*------------------------------------------------------------------------
    Sanity check
   ------------------------------------------------------------------------*/
  if ( NULL == vosFrmBuf )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
               "WLAN TL:Invalid parameter sent on WLANTL_TxMgmtFrm"));
    return VOS_STATUS_E_INVAL;
  }

  /*------------------------------------------------------------------------
    Extract TL control block
   ------------------------------------------------------------------------*/
  pTLCb = VOS_GET_TL_CB(pvosGCtx);
  if ( NULL == pTLCb )
  {
    TLLOGP(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_FATAL,
              "WLAN TL:Invalid TL pointer from pvosGCtx on WLANTL_TxMgmtFrm"));
    return VOS_STATUS_E_FAULT;
  }

  /*------------------------------------------------------------------------
    Ensure that management frame client was previously registered
   ------------------------------------------------------------------------*/
  if ( 0 == pTLCb->tlMgmtFrmClient.ucExists )
  {
    pTLCb->tlMgmtFrmClient.ucExists++;
    TLLOGP(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_FATAL,
          "WLAN TL:Management Frame client not register on WLANTL_TxMgmtFrm"));
    return VOS_STATUS_E_EXISTS;
  }

   /*------------------------------------------------------------------------
    Check if any Mgmt Frm is pending
   ------------------------------------------------------------------------*/
  //vosTempBuff = pTLCb->tlMgmtFrmClient.vosPendingDataBuff;
  if ( NULL != pTLCb->tlMgmtFrmClient.vosPendingDataBuff )
  {

    TLLOGP(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_FATAL,
        "WLAN TL:Management Frame already pending tx in TL: failing old one"));


    /*Failing the tx for the previous packet enqued by PE*/
    //vos_atomic_set_U32( (v_U32_t*)&pTLCb->tlMgmtFrmClient.vosPendingDataBuff,
    //                    (v_U32_t)NULL);

    //vos_pkt_get_user_data_ptr( vosTempBuff, VOS_PKT_USER_DATA_ID_TL,
    //                           (v_PVOID_t)&pfnTxComp);

    /*it should never be NULL - default handler should be registered if none*/
    //if ( NULL == pfnTxComp )
    //{
    //  VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
    //            "NULL pointer to Tx Complete on WLANTL_TxMgmtFrm");
    //  VOS_ASSERT(0);
    //  return VOS_STATUS_E_FAULT;
    //}

    //pfnTxComp( pvosGCtx, vosTempBuff, VOS_STATUS_E_RESOURCES );
    //return VOS_STATUS_E_BUSY;


    //pfnCompTxFunc( pvosGCtx, vosFrmBuf, VOS_STATUS_E_RESOURCES);
    return VOS_STATUS_E_RESOURCES;
  }


  /*------------------------------------------------------------------------
    Check if BD header was build, if not construct
   ------------------------------------------------------------------------*/
  if ( NULL == pvBDHeader )
  {
     v_MACADDR_t*     pvAddr2MacAddr;
     v_U8_t   uQosHdr = VOS_FALSE;

     /* Get address 2 of Mangement Frame to give to WLANHAL_FillTxBd */
     vosStatus = vos_pkt_peek_data( vosFrmBuf, 
                                    WLANTL_MAC_ADDR_ALIGN(1) + VOS_MAC_ADDR_SIZE,
                                    (v_PVOID_t)&pvAddr2MacAddr, VOS_MAC_ADDR_SIZE);

     if ( VOS_STATUS_SUCCESS != vosStatus )
     {
       TLLOGP(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_FATAL,
                "WLAN TL:Failed while attempting to get addr2 %d", vosStatus));
       return vosStatus;
     }
#ifdef FEATURE_WLAN_CCX
    /* CCX IAPP Frame which are data frames but technically used
     * for management functionality comes through route.
     */
    if (WLANTL_IS_QOS_DATA_FRAME(wFrmType))                                      \
    {
        uQosHdr = VOS_TRUE;
    }
#endif
    /*----------------------------------------------------------------------
      Call WDA to build TX header
     ----------------------------------------------------------------------*/
    vosStatus = WDA_DS_BuildTxPacketInfo( pvosGCtx, vosFrmBuf , &vDestMacAddr, 
                   1 /* always 802.11 frames*/, &usPktLen, uQosHdr /*qos not enabled !!!*/, 
                   0 /* WDS off */, 0, wFrmType, pvAddr2MacAddr, ucTid, 
                   ucAckResponse, usTimeStamp, 0, 0 );


    if ( !VOS_IS_STATUS_SUCCESS(vosStatus) )
    {
      TLLOGP(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_FATAL,
                "WLAN TL:Failed while attempting to build TX header %d", vosStatus));
      return vosStatus;
    }
   }/* if BD header not present */

  /*------------------------------------------------------------------------
    Save buffer and notify BAL; no lock is needed if the above restriction
    is met
    Save the tx complete fnct pointer as tl specific data in the vos buffer
   ------------------------------------------------------------------------*/
  if ( NULL != pfnCompTxFunc )
  {
    vos_pkt_set_user_data_ptr( vosFrmBuf, VOS_PKT_USER_DATA_ID_TL,
                               (v_PVOID_t)pfnCompTxFunc);
  }
  else
  {
    vos_pkt_set_user_data_ptr( vosFrmBuf, VOS_PKT_USER_DATA_ID_TL,
                               (v_PVOID_t)WLANTL_TxCompDefaultCb);

  }

  vos_atomic_set_U32( (v_U32_t*)&pTLCb->tlMgmtFrmClient.vosPendingDataBuff,
                      (v_U32_t)vosFrmBuf);

  /*------------------------------------------------------------------------
    Check if thre are enough resources for transmission and tx is not
    suspended.
   ------------------------------------------------------------------------*/
  if ( pTLCb->uResCount >=  WDA_TLI_MIN_RES_MF )
  {
    TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
              "WLAN TL:Issuing Xmit start request to BAL for MGMT"));
    vosStatus = WDA_DS_StartXmit(pvosGCtx);
    if(VOS_STATUS_SUCCESS != vosStatus)
    {
       TLLOGP(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_FATAL,
              "WLAN TL:WDA_DS_StartXmit fails. vosStatus %d", vosStatus));
       vos_atomic_set_U32( (v_U32_t*)&pTLCb->tlMgmtFrmClient.vosPendingDataBuff,0); 
    }
    return vosStatus;
    
  }
  else
  {
    /*---------------------------------------------------------------------
      No error code is sent because TL will resume tx autonomously if
      resources become available or tx gets resumed
     ---------------------------------------------------------------------*/
    TLLOGW(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_WARN,
       "WLAN TL:Request to send for Mgmt Frm but condition not met. Res: %d",
               pTLCb->uResCount));
  }

  return VOS_STATUS_SUCCESS;
}/* WLANTL_TxMgmtFrm */

/*----------------------------------------------------------------------------
    INTERACTION WITH HAL
 ---------------------------------------------------------------------------*/

/*==========================================================================

  FUNCTION    WLANTL_ResetNotification

  DESCRIPTION
    HAL notifies TL when the module is being reset.
    Currently not used.

  DEPENDENCIES

  PARAMETERS

    IN
    pvosGCtx:       pointer to the global vos context; a handle to TL's
                    control block can be extracted from its context


  RETURN VALUE
    The result code associated with performing the operation

    VOS_STATUS_E_FAULT:  pointer to TL cb is NULL ; access would cause a
                         page fault
    VOS_STATUS_SUCCESS:  Everything is good :)

  SIDE EFFECTS

============================================================================*/
VOS_STATUS
WLANTL_ResetNotification
(
  v_PVOID_t   pvosGCtx
)
{
  WLANTL_CbType*  pTLCb = NULL;
  /*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

  /*------------------------------------------------------------------------
    Sanity check
   Extract TL control block
   ------------------------------------------------------------------------*/
  pTLCb = VOS_GET_TL_CB(pvosGCtx);
  if ( NULL == pTLCb )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
      "WLAN TL:Invalid TL pointer from pvosGCtx on WLANTL_ResetNotification"));
    return VOS_STATUS_E_FAULT;
  }

  WLANTL_CleanCB(pTLCb, 1 /*empty all queues and pending packets*/);
  return VOS_STATUS_SUCCESS;
}/* WLANTL_ResetNotification */

/*==========================================================================

  FUNCTION    WLANTL_SuspendDataTx

  DESCRIPTION
    HAL calls this API when it wishes to suspend transmission for a
    particular STA.

  DEPENDENCIES
    The STA for which the request is made must be first registered with
    TL by HDD.

    RESTRICTION:  In case of a suspend, the flag write and read will not be
                  locked: worst case scenario one more packet can get
                  through before the flag gets updated (we can make this
                  write atomic as well to guarantee consistency)

  PARAMETERS

    IN
    pvosGCtx:       pointer to the global vos context; a handle to TL's
                    control block can be extracted from its context
    pucSTAId:       identifier of the station for which the request is made;
                    a value of NULL assumes suspend on all active station
    pfnSuspendTxCB: pointer to the suspend result notification in case the
                    call is asynchronous


  RETURN VALUE
    The result code associated with performing the operation

    VOS_STATUS_E_FAULT:   Station ID is outside array boundaries or pointer
                          to TL cb is NULL ; access would cause a page fault
    VOS_STATUS_E_EXISTS:  Station was not registered
    VOS_STATUS_SUCCESS:   Everything is good :)

  SIDE EFFECTS

============================================================================*/

VOS_STATUS
WLANTL_SuspendDataTx
(
  v_PVOID_t              pvosGCtx,
  v_U8_t*                pucSTAId,
  WLANTL_SuspendCBType   pfnSuspendTx
)
{
  WLANTL_CbType*  pTLCb = NULL;
  vos_msg_t       vosMsg;
  v_U8_t          ucTxSuspendReq, ucTxSuspended;
  v_U32_t         STAId = 0;

  /*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

  /*------------------------------------------------------------------------
    Sanity check
    Extract TL control block
   ------------------------------------------------------------------------*/
  pTLCb = VOS_GET_TL_CB(pvosGCtx);
  if ( NULL == pTLCb || NULL == pucSTAId )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
          "WLAN TL:Invalid TL pointer from pvosGCtx on WLANTL_SuspendDataTx"));
    return VOS_STATUS_E_FAULT;
  }

  /*------------------------------------------------------------------------
    Check the type of request: generic suspend, or per station suspend
   ------------------------------------------------------------------------*/
  /* Station IDs for Suspend request are received as bitmap                  */
  ucTxSuspendReq = *pucSTAId;
  ucTxSuspended = pTLCb->ucTxSuspended;

  if (WLAN_ALL_STA == ucTxSuspended)
  {
    /* All Stations are in Suspend mode. Nothing to do */
    TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
              "WLAN TL:All stations already suspended"));
    return VOS_STATUS_E_EXISTS;
  }

  if (WLAN_ALL_STA == *pucSTAId)
  {
    /* General Suspend Request received */
    TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
              "WLAN TL:General suspend requested"));
    vos_atomic_set_U8( &pTLCb->ucTxSuspended, WLAN_ALL_STA);
    vosMsg.reserved = WLAN_MAX_STA_COUNT;
  }
  else
  {
    /* Station specific Suspend Request received */
    /* Update Station Id Bit map for suspend request */
    do
    {
       /* If Bit set for this station with STAId */
      if (ucTxSuspendReq >> (STAId +1) )
      {
        /* If it is Not a valid station ID */
        if ( WLANTL_STA_ID_INVALID( STAId ) )
        {
          TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
                 "WLAN TL:Invalid station id requested on WLANTL_SuspendDataTx"));
          STAId++;
          continue;
        }
        /* If this station is Not registered with TL */
        if ( 0 == pTLCb->atlSTAClients[STAId].ucExists )
        {
          TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
                 "WLAN TL:Station was not previously registered on WLANTL_SuspendDataTx"));
          STAId++;
          continue;
        }

        TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
               "WLAN TL:Suspend request for station: %d", STAId));
        vos_atomic_set_U8( &pTLCb->atlSTAClients[STAId].ucTxSuspended, 1);
      }
      STAId++;
    } while ( STAId < WLAN_MAX_STA_COUNT );
    vosMsg.reserved = *pucSTAId;
  }

  /*------------------------------------------------------------------------
    Serialize request through TX thread
   ------------------------------------------------------------------------*/
  vosMsg.type     = WLANTL_TX_SIG_SUSPEND;
  vosMsg.bodyptr     = (v_PVOID_t)pfnSuspendTx;

  if(!VOS_IS_STATUS_SUCCESS(vos_tx_mq_serialize( VOS_MQ_ID_TL, &vosMsg)))
  {
    VOS_TRACE(VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
       " %s fails to post message", __FUNCTION__);
  }

  return VOS_STATUS_SUCCESS;
}/* WLANTL_SuspendDataTx */

/*==========================================================================

  FUNCTION    WLANTL_ResumeDataTx

  DESCRIPTION
    Called by HAL to resume data transmission for a given STA.

    WARNING: If a station was individually suspended a global resume will
             not resume that station

  DEPENDENCIES

  PARAMETERS

    IN
    pvosGCtx:       pointer to the global vos context; a handle to TL's
                    control block can be extracted from its context
    pucSTAId:       identifier of the station which is being resumed; NULL
                    translates into global resume

  RETURN VALUE
    The result code associated with performing the operation

    VOS_STATUS_E_FAULT:   Station ID is outside array boundaries or pointer
                          to TL cb is NULL ; access would cause a page fault
    VOS_STATUS_E_EXISTS:  Station was not registered
    VOS_STATUS_SUCCESS:   Everything is good :)

  SIDE EFFECTS

============================================================================*/

VOS_STATUS
WLANTL_ResumeDataTx
(
  v_PVOID_t      pvosGCtx,
  v_U8_t*        pucSTAId
)
{
  WLANTL_CbType*  pTLCb = NULL;
  v_U8_t          ucTxResumeReq;
  v_U32_t         STAId = 0;
  /*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

  /*------------------------------------------------------------------------
    Sanity check
    Extract TL control block
   ------------------------------------------------------------------------*/
  pTLCb = VOS_GET_TL_CB(pvosGCtx);
  if ( NULL == pTLCb || NULL == pucSTAId)
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
           "WLAN TL:Invalid TL pointer from pvosGCtx on WLANTL_ResumeDataTx"));
    return VOS_STATUS_E_FAULT;
  }

  ucTxResumeReq = *pucSTAId;
  /*------------------------------------------------------------------------
    Check to see the type of resume
   ------------------------------------------------------------------------*/
  if ( WLAN_ALL_STA == *pucSTAId)
  {
    TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
              "WLAN TL:General resume requested"));
    vos_atomic_set_U8( &pTLCb->ucTxSuspended, 0);

    /* Set to Resume for all stations */
    for (STAId = 0; STAId < WLAN_MAX_STA_COUNT; STAId++)
         vos_atomic_set_U8( &pTLCb->atlSTAClients[STAId].ucTxSuspended, 0);
  }
  else
  {
    do
    {
      /* If Bit Set for this station with STAId */
      if (ucTxResumeReq >> (STAId + 1))
      {
        /* If it is Not a valid station ID */
        if ( WLANTL_STA_ID_INVALID( STAId ))
        {
          TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
                "WLAN TL:Invalid station id requested on WLANTL_ResumeDataTx"));
          STAId++;
          continue;
        }
        /* If this station is Not registered with TL */
        if ( 0 == pTLCb->atlSTAClients[STAId].ucExists )
        {
          TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
                 "WLAN TL:Station was not previously registered on WLANTL_ResumeDataTx"));
          STAId++;
          continue;
        }

        TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
               "WLAN TL:Resume request for station: %d", STAId));
        vos_atomic_set_U8( &pTLCb->atlSTAClients[STAId].ucTxSuspended, 0);
      }
      STAId++;
    } while ( STAId < WLAN_MAX_STA_COUNT );
  }

  /*------------------------------------------------------------------------
    Resuming transmission
   ------------------------------------------------------------------------*/
  if ( pTLCb->uResCount >=  WDA_TLI_MIN_RES_MF )
  {
    TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
               "WLAN TL:Resuming transmission"));
    return WDA_DS_StartXmit(pvosGCtx);
  }

  return VOS_STATUS_SUCCESS;
}/* WLANTL_ResumeDataTx */

/*==========================================================================
  FUNCTION    WLANTL_SuspendCB

  DESCRIPTION
    Callback function for serializing Suspend signal through Tx thread

  DEPENDENCIES
    Just notify HAL that suspend in TL is complete.

  PARAMETERS

   IN
   pvosGCtx:       pointer to the global vos context; a handle to TL's
                   control block can be extracted from its context
   pUserData:      user data sent with the callback

  RETURN VALUE
    The result code associated with performing the operation

    VOS_STATUS_E_INVAL:   invalid input parameters
    VOS_STATUS_E_FAULT:   pointer to TL cb is NULL ; access would cause a
                          page fault
    VOS_STATUS_SUCCESS:   Everything is good :)


  SIDE EFFECTS

============================================================================*/
VOS_STATUS
WLANTL_SuspendCB
(
  v_PVOID_t             pvosGCtx,
  WLANTL_SuspendCBType  pfnSuspendCB,
  v_U16_t               usReserved
)
{
  WLANTL_CbType*  pTLCb   = NULL;
  v_U8_t          ucSTAId = (v_U8_t)usReserved;
  /*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

  /*------------------------------------------------------------------------
    Sanity check
   ------------------------------------------------------------------------*/
  if ( NULL == pfnSuspendCB )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO,
               "WLAN TL: No Call back processing requested WLANTL_SuspendCB"));
    return VOS_STATUS_SUCCESS;
  }

  /*------------------------------------------------------------------------
    Extract TL control block
   ------------------------------------------------------------------------*/
  pTLCb = VOS_GET_TL_CB(pvosGCtx);
  if ( NULL == pTLCb )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
               "WLAN TL:Invalid TL pointer from pvosGCtx on WLANTL_SuspendCB"));
    return VOS_STATUS_E_FAULT;
  }

  if ( WLANTL_STA_ID_INVALID( ucSTAId ) )
  {
    pfnSuspendCB(pvosGCtx, NULL, VOS_STATUS_SUCCESS);
  }
  else
  {
    pfnSuspendCB(pvosGCtx, &ucSTAId, VOS_STATUS_SUCCESS);
  }

  return VOS_STATUS_SUCCESS;
}/*WLANTL_SuspendCB*/


/*----------------------------------------------------------------------------
    CLIENT INDEPENDENT INTERFACE
 ---------------------------------------------------------------------------*/

/*==========================================================================

  FUNCTION    WLANTL_GetTxPktCount

  DESCRIPTION
    TL will provide the number of transmitted packets counted per
    STA per TID.

  DEPENDENCIES

  PARAMETERS

    IN
    pvosGCtx:       pointer to the global vos context; a handle to TL's
                    control block can be extracted from its context
    ucSTAId:        identifier of the station
    ucTid:          identifier of the tspec

    OUT
    puTxPktCount:   the number of packets tx packet for this STA and TID

  RETURN VALUE
    The result code associated with performing the operation

    VOS_STATUS_E_INVAL:   Input parameters are invalid
    VOS_STATUS_E_FAULT:   Station ID is outside array boundaries or pointer
                          to TL cb is NULL ; access would cause a page fault
    VOS_STATUS_E_EXISTS:  Station was not registered
    VOS_STATUS_SUCCESS:   Everything is good :)

  SIDE EFFECTS

============================================================================*/
VOS_STATUS
WLANTL_GetTxPktCount
(
  v_PVOID_t      pvosGCtx,
  v_U8_t         ucSTAId,
  v_U8_t         ucTid,
  v_U32_t*       puTxPktCount
)
{
  WLANTL_CbType*  pTLCb = NULL;
  /*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

  /*------------------------------------------------------------------------
    Sanity check
   ------------------------------------------------------------------------*/
  if ( NULL == puTxPktCount )
  {
     TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
              "WLAN TL:Invalid parameter sent on WLANTL_GetTxPktCount"));
    return VOS_STATUS_E_INVAL;
  }

  if ( WLANTL_STA_ID_INVALID( ucSTAId ) || WLANTL_TID_INVALID( ucTid) )
  {
     TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
    "WLAN TL:Invalid station id %d/tid %d requested on WLANTL_GetTxPktCount",
            ucSTAId, ucTid));
    return VOS_STATUS_E_FAULT;
  }

  /*------------------------------------------------------------------------
    Extract TL control block and check if station exists
   ------------------------------------------------------------------------*/
  pTLCb = VOS_GET_TL_CB(pvosGCtx);
  if ( NULL == pTLCb )
  {
     TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
          "WLAN TL:Invalid TL pointer from pvosGCtx on WLANTL_GetTxPktCount"));
    return VOS_STATUS_E_FAULT;
  }

  if ( 0 == pTLCb->atlSTAClients[ucSTAId].ucExists )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
    "WLAN TL:Station was not previously registered on WLANTL_GetTxPktCount %d",
     ucSTAId));
    return VOS_STATUS_E_EXISTS;
  }

  /*------------------------------------------------------------------------
    Return data
   ------------------------------------------------------------------------*/
  //VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_MED,
    //         "WLAN TL:Requested tx packet count for STA: %d, TID: %d", 
      //         ucSTAId, ucTid);

  *puTxPktCount = pTLCb->atlSTAClients[ucSTAId].auTxCount[ucTid];

  return VOS_STATUS_SUCCESS;
}/* WLANTL_GetTxPktCount */

/*==========================================================================

  FUNCTION    WLANTL_GetRxPktCount

  DESCRIPTION
    TL will provide the number of received packets counted per
    STA per TID.

  DEPENDENCIES

  PARAMETERS

    IN
    pvosGCtx:       pointer to the global vos context; a handle to TL's
                    control block can be extracted from its context
    ucSTAId:        identifier of the station
    ucTid:          identifier of the tspec

   OUT
    puTxPktCount:   the number of packets rx packet for this STA and TID

  RETURN VALUE
    The result code associated with performing the operation

    VOS_STATUS_E_INVAL:   Input parameters are invalid
    VOS_STATUS_E_FAULT:   Station ID is outside array boundaries or pointer
                          to TL cb is NULL ; access would cause a page fault
    VOS_STATUS_E_EXISTS:  Station was not registered
    VOS_STATUS_SUCCESS:   Everything is good :)

  SIDE EFFECTS

============================================================================*/
VOS_STATUS
WLANTL_GetRxPktCount
(
  v_PVOID_t      pvosGCtx,
  v_U8_t         ucSTAId,
  v_U8_t         ucTid,
  v_U32_t*       puRxPktCount
)
{
  WLANTL_CbType*  pTLCb = NULL;
  /*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

  /*------------------------------------------------------------------------
    Sanity check
   ------------------------------------------------------------------------*/
  if ( NULL == puRxPktCount )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
               "WLAN TL:Invalid parameter sent on WLANTL_GetRxPktCount"));
    return VOS_STATUS_E_INVAL;
  }

  if ( WLANTL_STA_ID_INVALID( ucSTAId ) || WLANTL_TID_INVALID( ucTid) )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
    "WLAN TL:Invalid station id %d/tid %d requested on WLANTL_GetRxPktCount",
             ucSTAId, ucTid));
    return VOS_STATUS_E_FAULT;
  }

  /*------------------------------------------------------------------------
    Extract TL control block and existance
   ------------------------------------------------------------------------*/
  pTLCb = VOS_GET_TL_CB(pvosGCtx);
  if ( NULL == pTLCb )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
          "WLAN TL:Invalid TL pointer from pvosGCtx on WLANTL_GetRxPktCount"));
    return VOS_STATUS_E_FAULT;
  }

  if ( 0 == pTLCb->atlSTAClients[ucSTAId].ucExists )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
    "WLAN TL:Station was not previously registered on WLANTL_GetRxPktCount"));
    return VOS_STATUS_E_EXISTS;
  }

  /*------------------------------------------------------------------------
    Return data
   ------------------------------------------------------------------------*/
  TLLOG3(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_MED,
            "WLAN TL:Requested rx packet count for STA: %d, TID: %d",
             ucSTAId, ucTid));

  *puRxPktCount = pTLCb->atlSTAClients[ucSTAId].auRxCount[ucTid];

  return VOS_STATUS_SUCCESS;
}/* WLANTL_GetRxPktCount */

#ifdef WLAN_SOFTAP_FEATURE
VOS_STATUS
WLANTL_TxFCFrame
(
  v_PVOID_t       pvosGCtx
);
#endif
/*============================================================================
                      TL INTERNAL API DEFINITION
============================================================================*/

/*==========================================================================

  FUNCTION    WLANTL_GetFrames

  DESCRIPTION

    BAL calls this function at the request of the lower bus interface.
    When this request is being received TL will retrieve packets from HDD
    in accordance with the priority rules and the count supplied by BAL.

  DEPENDENCIES

    HDD must have registered with TL at least one STA before this function
    can be called.

  PARAMETERS

    IN
    pvosGCtx:       pointer to the global vos context; a handle to TL's
                    or BAL's control block can be extracted from its context
    uSize:          maximum size accepted by the lower layer
    uFlowMask       TX flow control mask for Prima. Each bit is defined as 
                    WDA_TXFlowEnumType

    OUT
    vosDataBuff:   it will contain a pointer to the first buffer supplied
                    by TL, if there is more than one packet supplied, TL
                    will chain them through vOSS buffers

  RETURN VALUE

    The result code associated with performing the operation

    1 or more: number of required resources if there are still frames to fetch
    0 : error or HDD queues are drained

  SIDE EFFECTS

  NOTE
    
    Featurized uFlowMask. If we want to remove featurization, we need to change
    BAL on Volans.

============================================================================*/
v_U32_t
WLANTL_GetFrames
(
  v_PVOID_t       pvosGCtx,
  vos_pkt_t     **ppFrameDataBuff,
  v_U32_t         uSize,
#if defined( FEATURE_WLAN_INTEGRATED_SOC )
  v_U8_t          uFlowMask,
#endif
  v_BOOL_t*       pbUrgent
)
{
   vos_pkt_t**         pvosDataBuff = (vos_pkt_t**)ppFrameDataBuff;
   WLANTL_CbType*      pTLCb = NULL;
   v_U32_t             uRemaining = uSize;
   vos_pkt_t*          vosRoot;
   vos_pkt_t*          vosTempBuf;
   WLANTL_STAFuncType  pfnSTAFsm;
   v_U16_t             usPktLen;
   v_U32_t             uResLen;
   v_U8_t              ucSTAId;
   v_U8_t              ucAC;
   vos_pkt_t*          vosDataBuff;
   v_U32_t             uTotalPktLen;
   v_U32_t             i=0;
   v_U32_t             ucResult = 0;
   VOS_STATUS          vosStatus;
   WLANTL_STAEventType   wSTAEvent;
   tBssSystemRole       systemRole;
   tpAniSirGlobal pMac;
  /*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

  /*------------------------------------------------------------------------
    Sanity check
    Extract TL control block
   ------------------------------------------------------------------------*/
  pTLCb = VOS_GET_TL_CB(pvosGCtx);
  if (( NULL == pTLCb ) || ( NULL == pvosDataBuff ))
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
         "WLAN TL:Invalid TL pointer from pvosGCtx on WLANTL_ChangeSTAState"));
    return ucResult;
  }

  pMac = vos_get_context(VOS_MODULE_ID_PE, pvosGCtx);
  if ( NULL == pMac )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
                      "%s: Invalid pMac", __FUNCTION__));
    return ucResult;
  }

  vosDataBuff = pTLCb->vosDummyBuf; /* Just to avoid checking for NULL at
                                         each iteration */

#if defined( FEATURE_WLAN_INTEGRATED_SOC )
  pTLCb->uResCount = uSize;
#endif

  /*-----------------------------------------------------------------------
    Save the root as we will walk this chain as we fill it
   -----------------------------------------------------------------------*/
  vosRoot = vosDataBuff;
 
  /*-----------------------------------------------------------------------
    There is still data - until FSM function says otherwise
   -----------------------------------------------------------------------*/
  pTLCb->bUrgent      = FALSE;

#ifdef WLAN_SOFTAP_FEATURE
  while (( pTLCb->tlConfigInfo.uMinFramesProcThres < pTLCb->uResCount ) &&
         ( 0 < uRemaining ))
#else
  while (( 0 < pTLCb->uResCount ) &&
         ( 0 < uRemaining ))
#endif
  {
    systemRole = wdaGetGlobalSystemRole(pMac);
#ifdef WLAN_SOFTAP_FEATURE
#ifdef WLAN_SOFTAP_FLOWCTRL_EN
/* FIXME: The code has been disabled since it is creating issues in power save */
    if (eSYSTEM_AP_ROLE == systemRole)
    {
       if (pTLCb->done_once == 0 && NULL == pTLCb->vosTxFCBuf)
       {
          WLANTL_TxFCFrame (pvosGCtx);
          pTLCb->done_once ++;
       }
    } 
    if ( NULL != pTLCb->vosTxFCBuf )
    {
       //there is flow control packet waiting to be sent
       WDA_TLI_PROCESS_FRAME_LEN( pTLCb->vosTxFCBuf, usPktLen, uResLen, uTotalPktLen);
    
       if ( ( pTLCb->uResCount > uResLen ) &&
           ( uRemaining > uTotalPktLen )
#if defined( FEATURE_WLAN_INTEGRATED_SOC )
           && ( uFlowMask & ( 1 << WDA_TXFLOW_FC ) )
#endif
          )
       {
          TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
                   "WLAN TL:Chaining FC frame first on GetFrame"));

          vos_pkt_chain_packet( vosDataBuff, pTLCb->vosTxFCBuf, 1 /*true*/ );

          vos_atomic_set_U32( (v_U32_t*)&pTLCb->vosTxFCBuf, (v_U32_t)NULL);

          /*FC frames cannot be delayed*/
          pTLCb->bUrgent      = TRUE;

          /*Update remaining len from SSC */
          uRemaining        -= (usPktLen + WDA_DXE_HEADER_SIZE);

          /*Update resource count */
          pTLCb->uResCount -= uResLen;
       }
       else
       {
          TLLOGW(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_WARN,
                "WLAN TL:send fc out of source %s", __FUNCTION__));
          ucResult = ( pTLCb->uResCount > uResLen )?VOS_TRUE:VOS_FALSE;
          break; /* Out of resources or reached max len */
       }
   }
   else 
#endif //WLAN_SOFTAP_FLOWCTRL_EN
#endif //#ifdef WLAN_SOFTAP_FEATURE

    if ( NULL != pTLCb->tlMgmtFrmClient.vosPendingDataBuff )
    {
      WDA_TLI_PROCESS_FRAME_LEN( pTLCb->tlMgmtFrmClient.vosPendingDataBuff,
                          usPktLen, uResLen, uTotalPktLen);

      VOS_ASSERT(usPktLen <= WLANTL_MAX_ALLOWED_LEN);

      if ( ( pTLCb->uResCount > uResLen ) &&
           ( uRemaining > uTotalPktLen )
#if defined( FEATURE_WLAN_INTEGRATED_SOC )
           && ( uFlowMask & ( 1 << WDA_TXFLOW_MGMT ) )
#endif
         )
      {
        TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
                   "WLAN TL:Chaining management frame on GetFrame"));

        vos_pkt_chain_packet( vosDataBuff,
                              pTLCb->tlMgmtFrmClient.vosPendingDataBuff,
                              1 /*true*/ );

        vos_atomic_set_U32( (v_U32_t*)&pTLCb->tlMgmtFrmClient.
                                  vosPendingDataBuff, (v_U32_t)NULL);

        /*management frames cannot be delayed*/
        pTLCb->bUrgent      = TRUE;

        /*Update remaining len from SSC */
        uRemaining        -= (usPktLen + WDA_DXE_HEADER_SIZE);

        /*Update resource count */
        pTLCb->uResCount -= uResLen;
      }
      else
      {
        ucResult = ( pTLCb->uResCount > uResLen )?VOS_TRUE:VOS_FALSE;
        break; /* Out of resources or reached max len */
      }
    }
    else if (( pTLCb->tlBAPClient.vosPendingDataBuff ) &&
             ( WDA_TLI_MIN_RES_BAP <= pTLCb->uResCount ) &&
             ( 0 == pTLCb->ucTxSuspended )
#if defined( FEATURE_WLAN_INTEGRATED_SOC )
           && ( uFlowMask & ( 1 << WDA_TXFLOW_BAP ) )
#endif
          )
    {
      WDA_TLI_PROCESS_FRAME_LEN( pTLCb->tlBAPClient.vosPendingDataBuff,
                          usPktLen, uResLen, uTotalPktLen);

      VOS_ASSERT(usPktLen <= WLANTL_MAX_ALLOWED_LEN);

      if ( ( pTLCb->uResCount > (uResLen + WDA_TLI_MIN_RES_MF ) ) &&
           ( uRemaining > uTotalPktLen ))
      {
        TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
                   "WLAN TL:Chaining BT-AMP frame on GetFrame"));

        vos_pkt_chain_packet( vosDataBuff,
                              pTLCb->tlBAPClient.vosPendingDataBuff,
                              1 /*true*/ );

        /*BAP frames cannot be delayed*/
        pTLCb->bUrgent      = TRUE;

        vos_atomic_set_U32( (v_U32_t*)&pTLCb->tlBAPClient.vosPendingDataBuff,
                            (v_U32_t)NULL);

        /*Update remaining len from SSC */
        uRemaining        -=  (usPktLen + WDA_DXE_HEADER_SIZE);

        /*Update resource count */
        pTLCb->uResCount  -= uResLen;
      }
      else
      {
#if defined( FEATURE_WLAN_INTEGRATED_SOC )
        ucResult = uResLen + WDA_TLI_MIN_RES_MF;
#else
        ucResult = ( pTLCb->uResCount > ( uResLen + WDA_TLI_MIN_RES_MF ) )?
                     VOS_TRUE:VOS_FALSE;
#endif
        break; /* Out of resources or reached max len */
      }
    }
    else if (( WDA_TLI_MIN_RES_DATA <= pTLCb->uResCount )&&
             ( 0 == pTLCb->ucTxSuspended )
#if defined( FEATURE_WLAN_INTEGRATED_SOC )
           && (( uFlowMask & ( 1 << WDA_TXFLOW_AC_BK ) ) || 
               ( uFlowMask & ( 1 << WDA_TXFLOW_AC_BE ) ) ||
               ( uFlowMask & ( 1 << WDA_TXFLOW_AC_VI ) ) || 
               ( uFlowMask & ( 1 << WDA_TXFLOW_AC_VO ) ))
#endif
          )
    {
      /*---------------------------------------------------------------------
        Check to see if there was any packet left behind previously due to
        size constraints
       ---------------------------------------------------------------------*/
      vosTempBuf = NULL;

      if ( NULL != pTLCb->vosTempBuf ) 
      {
        vosTempBuf          = pTLCb->vosTempBuf;
        pTLCb->vosTempBuf   = NULL;
        ucSTAId             = pTLCb->ucCachedSTAId; 
        ucAC                = pTLCb->ucCachedAC;
        pTLCb->atlSTAClients[ucSTAId].ucNoMoreData = 0;

        TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
                   "WLAN TL:Chaining cached data frame on GetFrame"));
      }
      else
      {
        WLAN_TLGetNextTxIds( pvosGCtx, &ucSTAId);
        if (ucSTAId >= WLAN_MAX_STA_COUNT)
        {
         /* Packets start coming in even after insmod Without *
            starting Hostapd or Interface being up            *
            During which cases STAID is invaled and hence 
            the check. HalMsg_ScnaComplete Triggers */

            break;
        }
        /* ucCurrentAC should have correct AC to be served by calling
           WLAN_TLGetNextTxIds */
        ucAC = pTLCb->atlSTAClients[ucSTAId].ucCurrentAC;

        pTLCb->atlSTAClients[ucSTAId].ucNoMoreData = 1;
        TLLOG4(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_LOW,
                   "WLAN TL: %s get one data frame, station ID %d ", __FUNCTION__, ucSTAId));
        /*-------------------------------------------------------------------
        Check to see that STA is valid and tx is not suspended
         -------------------------------------------------------------------*/
        if ( ( ! WLANTL_STA_ID_INVALID( ucSTAId ) ) &&
           ( 0 == pTLCb->atlSTAClients[ucSTAId].ucTxSuspended ) &&
           ( 0 == pTLCb->atlSTAClients[ucSTAId].fcStaTxDisabled) )
        {
          TLLOG4(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_LOW,
                   "WLAN TL: %s sta id valid and not suspended ",__FUNCTION__));
          wSTAEvent = WLANTL_TX_EVENT;

          pfnSTAFsm = tlSTAFsm[pTLCb->atlSTAClients[ucSTAId].tlState].
                        pfnSTATbl[wSTAEvent];

          if ( NULL != pfnSTAFsm )
          {
            pTLCb->atlSTAClients[ucSTAId].ucNoMoreData = 0;
            vosStatus  = pfnSTAFsm( pvosGCtx, ucSTAId, &vosTempBuf);

            if (( VOS_STATUS_SUCCESS != vosStatus ) &&
                ( NULL != vosTempBuf ))
            {
                 pTLCb->atlSTAClients[ucSTAId].pfnSTATxComp( pvosGCtx,
                                                             vosTempBuf,
                                                             vosStatus );
                 vosTempBuf = NULL;
            }/* status success*/
          }/*NULL function state*/
        }/* valid STA id and ! suspended*/
        else
        {
           if ( ! WLANTL_STA_ID_INVALID( ucSTAId ) ) 
           {
                TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
                  "WLAN TL:Not fetching frame because suspended for sta ID %d", 
                   ucSTAId));
           }
        }
      }/* data */

      if ( NULL != vosTempBuf )
      {
        WDA_TLI_PROCESS_FRAME_LEN( vosTempBuf, usPktLen, uResLen, uTotalPktLen);

        VOS_ASSERT( usPktLen <= WLANTL_MAX_ALLOWED_LEN);

        TLLOG4(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_LOW,
                  "WLAN TL:Resources needed by frame: %d", uResLen));

        if ( ( pTLCb->uResCount >= (uResLen + WDA_TLI_MIN_RES_BAP ) ) &&
           ( uRemaining > uTotalPktLen )
#if defined( FEATURE_WLAN_INTEGRATED_SOC )
           && ( uFlowMask & ( 1 << ucAC ) )
#endif
           )
        {
          TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
                    "WLAN TL:Chaining data frame on GetFrame"));

          vos_pkt_chain_packet( vosDataBuff, vosTempBuf, 1 /*true*/ );
          vosTempBuf =  NULL;

          /*Update remaining len from SSC */
          uRemaining        -= (usPktLen + WDA_DXE_HEADER_SIZE);

           /*Update resource count */
          pTLCb->uResCount  -= uResLen;

#ifdef WLAN_SOFTAP_FEATURE
          //fow control update
          pTLCb->atlSTAClients[ucSTAId].uIngress_length += uResLen;
          pTLCb->atlSTAClients[ucSTAId].uBuffThresholdMax = (pTLCb->atlSTAClients[ucSTAId].uBuffThresholdMax >= uResLen) ?
            (pTLCb->atlSTAClients[ucSTAId].uBuffThresholdMax - uResLen) : 0;
#endif

        }
        else
        {
          /* Store this for later tx - already fetched from HDD */
          pTLCb->vosTempBuf = vosTempBuf;
          pTLCb->ucCachedSTAId = ucSTAId;
          pTLCb->ucCachedAC    = ucAC;
#if defined( FEATURE_WLAN_INTEGRATED_SOC )
          ucResult = uResLen + WDA_TLI_MIN_RES_BAP;
          TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
               "min %d res required by TL.", ucResult ));
#else
          ucResult = ( pTLCb->uResCount >= (uResLen + WDA_TLI_MIN_RES_BAP ))?
                       VOS_TRUE:VOS_FALSE;
#endif
          break; /* Out of resources or reached max len */
        }
      }
      else
      {
           for ( i = 0; i < WLAN_MAX_STA_COUNT; i++)
           {
              if ((pTLCb->atlSTAClients[i].ucExists) && 
                  (pTLCb->atlSTAClients[i].ucPktPending))
              {
                  /* There is station to be Served */
                  break;
              }
           }
           if (i >= WLAN_MAX_STA_COUNT)
           {
              /* No More to Serve Exit Get Frames */
              break;
           }
           else
           {
              /* More to be Served */
              continue;
           }
        } 
      }
    else
    {
      TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
             "WLAN TL:Returning from GetFrame: resources = %d suspended = %d",
                 pTLCb->uResCount, pTLCb->ucTxSuspended));
#if defined( FEATURE_WLAN_INTEGRATED_SOC )
      /* TL is starving even when DXE is not in low resource condition 
         Return min resource number required and Let DXE deceide what to do */
      if(( 0 == pTLCb->ucTxSuspended ) && 
         (( uFlowMask & ( 1 << WDA_TXFLOW_AC_BK ) ) || 
          ( uFlowMask & ( 1 << WDA_TXFLOW_AC_BE ) ) ||
          ( uFlowMask & ( 1 << WDA_TXFLOW_AC_VI ) ) || 
          ( uFlowMask & ( 1 << WDA_TXFLOW_AC_VO ) )))
      {
         TLLOGE(VOS_TRACE(VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
             "WLAN TL:Returning from GetFrame: resources = %d",
                 pTLCb->uResCount));
         ucResult = WDA_TLI_MIN_RES_DATA;
      }
#endif
       break; /*out of min data resources*/
    }

    pTLCb->usPendingTxCompleteCount++;
    /* Move data buffer up one packet */
    vos_pkt_walk_packet_chain( vosDataBuff, &vosDataBuff, 0/*false*/ );
  }

  /*----------------------------------------------------------------------
    Packet chain starts at root + 1
   ----------------------------------------------------------------------*/
  vos_pkt_walk_packet_chain( vosRoot, &vosDataBuff, 1/*true*/ );

  *pvosDataBuff = vosDataBuff;
  VOS_ASSERT( pbUrgent );
  *pbUrgent     = pTLCb->bUrgent;
  return ucResult;
}/* WLANTL_GetFrames */


/*==========================================================================

  FUNCTION    WLANTL_TxComp

  DESCRIPTION
    It is being called by BAL upon asynchronous notification of the packet
    or packets  being sent over the bus.

  DEPENDENCIES
    Tx complete cannot be called without a previous transmit.

  PARAMETERS

    IN
    pvosGCtx:       pointer to the global vos context; a handle to TL's
                    or BAL's control block can be extracted from its context
    vosDataBuff:   it will contain a pointer to the first buffer for which
                    the BAL report is being made, if there is more then one
                    packet they will be chained using vOSS buffers.
    wTxStatus:      the status of the transmitted packet, see above chapter
                    on HDD interaction for a list of possible values

  RETURN VALUE
    The result code associated with performing the operation

    VOS_STATUS_E_INVAL:   Input parameters are invalid
    VOS_STATUS_E_FAULT:   pointer to TL cb is NULL ; access would cause a
                          page fault
    VOS_STATUS_E_EXISTS:  Station was not registered
    VOS_STATUS_SUCCESS:   Everything is good :)

  SIDE EFFECTS

============================================================================*/
VOS_STATUS
WLANTL_TxComp
(
  v_PVOID_t       pvosGCtx,
  vos_pkt_t      *pFrameDataBuff,
  VOS_STATUS      wTxStatus
)
{
  vos_pkt_t*           vosDataBuff = (vos_pkt_t*)pFrameDataBuff;
  WLANTL_CbType*       pTLCb     = NULL;
  WLANTL_TxCompCBType  pfnTxComp = NULL;
  VOS_STATUS           vosStatus = VOS_STATUS_SUCCESS;
#if !defined( FEATURE_WLAN_INTEGRATED_SOC )
  vos_msg_t            vosMsg;
#endif
  vos_pkt_t*           vosTempTx = NULL;

  /*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

  /*------------------------------------------------------------------------
    Sanity check
   ------------------------------------------------------------------------*/
  if ( NULL == vosDataBuff )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
               "WLAN TL:Extraneous NULL data pointer on WLANTL_TxComp"));
    return VOS_STATUS_E_INVAL;
  }

  /*------------------------------------------------------------------------
   Extract TL control block
   ------------------------------------------------------------------------*/
  pTLCb = VOS_GET_TL_CB(pvosGCtx);
  if ( NULL == pTLCb )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
              "WLAN TL:Invalid TL pointer from pvosGCtx on WLANTL_TxComp"));
    return VOS_STATUS_E_FAULT;
  }

  while ((0 < pTLCb->usPendingTxCompleteCount) &&
         ( VOS_STATUS_SUCCESS == vosStatus ) &&
         ( NULL !=  vosDataBuff))
  {
    vos_pkt_get_user_data_ptr(  vosDataBuff, VOS_PKT_USER_DATA_ID_TL,
                               (v_PVOID_t)&pfnTxComp);

    /*it should never be NULL - default handler should be registered if none*/
    if ( NULL == pfnTxComp )
    {
      TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
                "WLAN TL:NULL pointer to Tx Complete on WLANTL_TxComp"));
      VOS_ASSERT(0);
      return VOS_STATUS_E_FAULT;
    }

    TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
               "WLAN TL:Calling Tx complete for pkt %x in function %x",
               vosDataBuff, pfnTxComp));

    vosTempTx = vosDataBuff;
    vosStatus = vos_pkt_walk_packet_chain( vosDataBuff,
                                           &vosDataBuff, 1/*true*/);

    pfnTxComp( pvosGCtx, vosTempTx, wTxStatus );

    pTLCb->usPendingTxCompleteCount--;
  }

#if !defined( FEATURE_WLAN_INTEGRATED_SOC ) 
  if (( 0 == pTLCb->usPendingTxCompleteCount ) &&
      ( pTLCb->uResCount <= WDA_TLI_BD_PDU_RESERVE_THRESHOLD ))
  {
    vosMsg.reserved = 0;
    vosMsg.bodyptr  = NULL;
    vosMsg.type     = WLANTL_TX_RES_NEEDED;
    vos_tx_mq_serialize( VOS_MQ_ID_TL, &vosMsg);
  }
#endif
 
  TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
            "WLAN TL: current TL values are: resources = %d "
            "pTLCb->usPendingTxCompleteCount = %d",
              pTLCb->uResCount, pTLCb->usPendingTxCompleteCount));

  return VOS_STATUS_SUCCESS;
}/* WLANTL_TxComp */

/*==========================================================================

  FUNCTION    WLANTL_CacheSTAFrame

  DESCRIPTION
    Internal utility function for for caching incoming data frames that do 
    not have a registered station yet. 

  DEPENDENCIES
    TL must be initiailized before this function gets called.
    In order to benefit from thsi caching, the components must ensure that
    they will only register with TL at the moment when they are fully setup
    and ready to receive incoming data 
   
  PARAMETERS

    IN
    
    pTLCb:                  TL control block
    ucSTAId:                station id
    vosTempBuff:            the data packet
    uDPUSig:                DPU signature of the incoming packet
    bBcast:                 true if packet had the MC/BC bit set 

  RETURN VALUE
    The result code associated with performing the operation

    VOS_STATUS_E_FAULT:   pointer to TL cb is NULL or STA Id invalid ; access
                          would cause a page fault
    VOS_STATUS_SUCCESS:   Everything is good :)

  SIDE EFFECTS

============================================================================*/
static VOS_STATUS
WLANTL_CacheSTAFrame
(
  WLANTL_CbType*    pTLCb,
  v_U8_t            ucSTAId,
  vos_pkt_t*        vosTempBuff,
  v_U32_t           uDPUSig,
  v_U8_t            bBcast,
  v_U8_t            ucFrmType
)
{
  v_U8_t    ucUcastSig;
  v_U8_t    ucBcastSig;
  v_BOOL_t  bOldSTAPkt;
  /*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

  /*-------------------------------------------------------------------------  
     Sanity check 
   -------------------------------------------------------------------------*/ 
  if (( NULL == pTLCb ) || ( NULL == vosTempBuff ) )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
               "WLAN TL: Invalid input pointer on WLANTL_CacheSTAFrame TL %x"
               " Packet %x", pTLCb, vosTempBuff ));
    return VOS_STATUS_E_FAULT;
  }

  if ( WLANTL_STA_ID_INVALID( ucSTAId ) )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
         "WLAN TL:Invalid station id requested on WLANTL_CacheSTAFrame"));
    return VOS_STATUS_E_FAULT;
  }

  TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
             "WLAN TL:Attempting to cache pkt for STA %d, BD DPU Sig: %d with sig UC: %d, BC: %d", 
             ucSTAId, uDPUSig,
             pTLCb->atlSTAClients[ucSTAId].wSTADesc.ucUcastSig,
             pTLCb->atlSTAClients[ucSTAId].wSTADesc.ucBcastSig));

  if(WLANTL_IS_CTRL_FRAME(ucFrmType))
  {
      TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
                 "WLAN TL: No need to cache CTRL frame. Dropping"));
      vos_pkt_return_packet(vosTempBuff); 
      return VOS_STATUS_SUCCESS;
  }

  /*-------------------------------------------------------------------------
    Check if the packet that we are trying to cache belongs to the old
    registered station (if any) or the new (potentially)upcoming station
    
    - If the STA with this Id was never registered with TL - the signature
    will be invalid;
    - If the STA was previously registered TL will have cached the former
    set of DPU signatures
  -------------------------------------------------------------------------*/
  if ( bBcast )
  {
    ucBcastSig = (v_U8_t)uDPUSig;
    bOldSTAPkt = (( WLAN_TL_INVALID_B_SIG != 
                  pTLCb->atlSTAClients[ucSTAId].wSTADesc.ucBcastSig ) &&
      ( ucBcastSig == pTLCb->atlSTAClients[ucSTAId].wSTADesc.ucBcastSig ));
  }
  else
  {
    ucUcastSig = (v_U8_t)uDPUSig;
    bOldSTAPkt = (( WLAN_TL_INVALID_U_SIG != 
                    pTLCb->atlSTAClients[ucSTAId].wSTADesc.ucUcastSig ) &&
        ( ucUcastSig == pTLCb->atlSTAClients[ucSTAId].wSTADesc.ucUcastSig ));
  }

  /*------------------------------------------------------------------------
    If the value of the DPU SIG matches the old, this packet will not
    be cached as it belonged to the former association
    In case the SIG does not match - this is a packet for a potentially new
    associated station 
  -------------------------------------------------------------------------*/
  if ( bOldSTAPkt )
  {
    TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
               "WLAN TL:Data packet matches old sig for sig DPU: %d UC: %d, "
               "BC: %d - dropping", 
               uDPUSig, 
               pTLCb->atlSTAClients[ucSTAId].wSTADesc.ucUcastSig, 
               pTLCb->atlSTAClients[ucSTAId].wSTADesc.ucBcastSig));
    vos_pkt_return_packet(vosTempBuff); 
  }
  else
  {
    if ( NULL == pTLCb->atlSTAClients[ucSTAId].vosBegCachedFrame ) 
    {
      /*this is the first frame that we are caching */
      pTLCb->atlSTAClients[ucSTAId].vosBegCachedFrame = vosTempBuff;   
    }
    else
    {
      /*this is a subsequent frame that we are caching: chain to the end */
      vos_pkt_chain_packet(pTLCb->atlSTAClients[ucSTAId].vosEndCachedFrame, 
                           vosTempBuff, VOS_TRUE);
    }
    pTLCb->atlSTAClients[ucSTAId].vosEndCachedFrame = vosTempBuff; 
  }/*else new packet*/

  return VOS_STATUS_SUCCESS; 
}/*WLANTL_CacheSTAFrame*/

/*==========================================================================

  FUNCTION    WLANTL_FlushCachedFrames

  DESCRIPTION
    Internal utility function used by TL to flush the station cache

  DEPENDENCIES
    TL must be initiailized before this function gets called.
    
  PARAMETERS

    IN

    vosDataBuff:   it will contain a pointer to the first cached buffer
                   received,

  RETURN VALUE
    The result code associated with performing the operation

    VOS_STATUS_SUCCESS:   Everything is good :)

  SIDE EFFECTS

  NOTE
    This function doesn't re-initialize vosDataBuff to NULL. It's caller's 
    responsibility to do so, if required, after this function call.
    Because of this restriction, we decide to make this function to static
    so that upper layer doesn't need to be aware of this restriction. 
    
============================================================================*/
static VOS_STATUS
WLANTL_FlushCachedFrames
(
  vos_pkt_t*      vosDataBuff
)
{
  /*----------------------------------------------------------------------
    Return the entire chain to vos if there are indeed cache frames 
  ----------------------------------------------------------------------*/
  if ( NULL != vosDataBuff )
  {
    vos_pkt_return_packet(vosDataBuff);
  }

  return VOS_STATUS_SUCCESS;  
}/*WLANTL_FlushCachedFrames*/

/*==========================================================================

  FUNCTION    WLANTL_ForwardSTAFrames

  DESCRIPTION
    Internal utility function for either forwarding cached data to the station after
    the station has been registered, or flushing cached data if the station has not 
    been registered. 
     

  DEPENDENCIES
    TL must be initiailized before this function gets called.
   
  PARAMETERS

    IN
    
    pTLCb:                  TL control block
    ucSTAId:                station id

  RETURN VALUE
    The result code associated with performing the operation

    VOS_STATUS_E_FAULT:   pointer to TL cb is NULL ; access would cause a
                          page fault
    VOS_STATUS_SUCCESS:   Everything is good :)

  SIDE EFFECTS
    This function doesn't re-initialize vosDataBuff to NULL. It's caller's 
    responsibility to do so, if required, after this function call.
    Because of this restriction, we decide to make this function to static
    so that upper layer doesn't need to be aware of this restriction. 

============================================================================*/
static VOS_STATUS
WLANTL_ForwardSTAFrames
(
  void*             pvosGCtx,
  v_U8_t            ucSTAId,
  v_U8_t            ucUcastSig,
  v_U8_t            ucBcastSig
)
{
  WLANTL_CbType*  pTLCb = NULL; 
  /*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

  /*-------------------------------------------------------------------------  
     Sanity check 
   -------------------------------------------------------------------------*/ 
  pTLCb = VOS_GET_TL_CB(pvosGCtx);
  if ( NULL == pTLCb )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
        "WLAN TL: Invalid input pointer on WLANTL_ForwardSTAFrames TL %x",
         pTLCb ));
    return VOS_STATUS_E_FAULT;
  }

  if ( WLANTL_STA_ID_INVALID( ucSTAId ) )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
         "WLAN TL:Invalid station id requested on WLANTL_ForwardSTAFrames"));
    return VOS_STATUS_E_FAULT;
  }

  //WLAN_TL_LOCK_STA_CACHE(pTLCb->atlSTAClients[ucSTAId]); 

  /*------------------------------------------------------------------------
     Check if station has not been registered in the mean while
     if not registered, flush cached frames.
   ------------------------------------------------------------------------*/ 
  if ( 0 == pTLCb->atlSTAClients[ucSTAId].ucExists )
  {
    TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
       "WLAN TL:Station has been deleted for STA %d - flushing cache", ucSTAId));
    WLANTL_FlushCachedFrames(pTLCb->atlSTAClients[ucSTAId].vosBegCachedFrame);
    goto done; 
  }

  /*------------------------------------------------------------------------
    Forwarding cache frames received while the station was in the process   
    of being registered with the rest of the SW components   

    Access to the cache must be locked; similarly updating the signature and   
    the existence flag must be synchronized because these values are checked   
    during cached  
  ------------------------------------------------------------------------*/
  TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
       "WLAN TL:Preparing to fwd packets for STA %d", ucSTAId));

  /*-----------------------------------------------------------------------
    Save the new signature values
  ------------------------------------------------------------------------*/
  pTLCb->atlSTAClients[ucSTAId].wSTADesc.ucUcastSig  = ucUcastSig;
  pTLCb->atlSTAClients[ucSTAId].wSTADesc.ucBcastSig  = ucBcastSig;

  TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
       "WLAN TL:Fwd-ing packets for STA %d UC %d BC %d",
       ucSTAId, ucUcastSig, ucBcastSig));

  /*-------------------------------------------------------------------------  
     Check to see if we have any cached data to forward 
   -------------------------------------------------------------------------*/ 
  if ( NULL != pTLCb->atlSTAClients[ucSTAId].vosBegCachedFrame ) 
  {
    TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
               "WLAN TL: Fwd-ing Cached packets for station %d", ucSTAId ));

    WLANTL_RxCachedFrames( pTLCb, 
                           ucSTAId, 
                           pTLCb->atlSTAClients[ucSTAId].vosBegCachedFrame);
  }
  else
  {
     TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
               "WLAN TL: NO cached packets for station %d", ucSTAId ));
  }

done:
  /*-------------------------------------------------------------------------  
   Clear the station cache 
   -------------------------------------------------------------------------*/
  pTLCb->atlSTAClients[ucSTAId].vosBegCachedFrame = NULL;   
  pTLCb->atlSTAClients[ucSTAId].vosEndCachedFrame = NULL; 

    /*-----------------------------------------------------------------------
    After all the init is complete we can mark the existance flag 
    ----------------------------------------------------------------------*/
  pTLCb->atlSTAClients[ucSTAId].ucRxBlocked = 0;

  //WLAN_TL_UNLOCK_STA_CACHE(pTLCb->atlSTAClients[ucSTAId]); 
  return VOS_STATUS_SUCCESS; 

}/*WLANTL_ForwardSTAFrames*/


#ifdef FEATURE_WLAN_CCX
/*==========================================================================

  FUNCTION    WLANTL_IsIAPPFrame

  DESCRIPTION
    Internal utility function for detecting incoming CCX IAPP frames

  DEPENDENCIES

  PARAMETERS

    IN
    
    pvBDHeader:             pointer to the BD header
    vosTempBuff:            the data packet

    IN/OUT
    pFirstDataPktArrived:   static from caller function; used for rssi 
                            computation
  RETURN VALUE
    The result code associated with performing the operation

    VOS_TRUE:   It is a IAPP frame
    VOS_FALSE:  It is NOT IAPP frame

  SIDE EFFECTS

============================================================================*/
v_BOOL_t
WLANTL_IsIAPPFrame
(
  v_PVOID_t         pvBDHeader,
  vos_pkt_t*        vosTempBuff
)
{
  v_U16_t             usMPDUDOffset;
  v_U8_t              ucOffset;
  v_U8_t              ucSnapHdr[WLANTL_LLC_SNAP_SIZE];
  v_SIZE_t            usSnapHdrSize = WLANTL_LLC_SNAP_SIZE;
  VOS_STATUS          vosStatus;

  /*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

  /*------------------------------------------------------------------------
    Check if OUI field is present.
  -------------------------------------------------------------------------*/
  if ( VOS_FALSE == WDA_IS_RX_LLC_PRESENT(pvBDHeader) )
  {
      TLLOGW(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_WARN,
                  "WLAN TL:LLC header removed, cannot determine BT-AMP type -"
                  "dropping pkt"));
      /* Drop packet */
      vos_pkt_return_packet(vosTempBuff);
      return VOS_TRUE;
  }
  usMPDUDOffset = (v_U8_t)WDA_GET_RX_MPDU_DATA_OFFSET(pvBDHeader);
  ucOffset      = (v_U8_t)usMPDUDOffset + WLANTL_LLC_SNAP_OFFSET;

  vosStatus = vos_pkt_extract_data( vosTempBuff, ucOffset,
                                (v_PVOID_t)ucSnapHdr, &usSnapHdrSize);

  if (( VOS_STATUS_SUCCESS != vosStatus)) 
  {
    TLLOGW(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_WARN,
                "Unable to extract Snap Hdr of data  packet -"
                "dropping pkt"));
    return VOS_FALSE;
  }

 /*------------------------------------------------------------------------
    Check if this is IAPP frame by matching Aironet Snap hdr.
  -------------------------------------------------------------------------*/
  // Compare returns 1 if values are same and 0
  // if not the same.
  if (( WLANTL_LLC_SNAP_SIZE != usSnapHdrSize ) ||
     ( 0 == vos_mem_compare(ucSnapHdr, (v_PVOID_t)WLANTL_AIRONET_SNAP_HEADER,
                            WLANTL_LLC_SNAP_SIZE ) ))
  {
    return VOS_FALSE;
  }

  return VOS_TRUE;

}
#endif //FEATURE_WLAN_CCX

/*==========================================================================

  FUNCTION    WLANTL_ProcessBAPFrame

  DESCRIPTION
    Internal utility function for processing incoming BT-AMP frames

  DEPENDENCIES
    TL must be initiailized before this function gets called.
    Bothe the BT-AMP station and the BAP Ctrl path must have been previously 
    registered with TL.

  PARAMETERS

    IN
    
    pvBDHeader:             pointer to the BD header
    vosTempBuff:            the data packet
    pTLCb:                  TL control block
    ucSTAId:                station id

    IN/OUT
    pFirstDataPktArrived:   static from caller function; used for rssi 
                            computation
  RETURN VALUE
    The result code associated with performing the operation

    VOS_STATUS_E_INVAL:   Input parameters are invalid
    VOS_STATUS_E_FAULT:   pointer to TL cb is NULL ; access would cause a
                          page fault
    VOS_STATUS_SUCCESS:   Everything is good :)

  SIDE EFFECTS

============================================================================*/
v_BOOL_t
WLANTL_ProcessBAPFrame
(
  v_PVOID_t         pvBDHeader,
  vos_pkt_t*        vosTempBuff,
  WLANTL_CbType*    pTLCb,
  v_U8_t*           pFirstDataPktArrived,
  v_U8_t            ucSTAId
)
{
  v_U16_t             usMPDUDOffset;
  v_U8_t              ucOffset;
  v_U8_t              ucOUI[WLANTL_LLC_OUI_SIZE];
  v_SIZE_t            usOUISize = WLANTL_LLC_OUI_SIZE;
  VOS_STATUS          vosStatus;
  v_U16_t             usType;
  v_SIZE_t            usTypeLen = sizeof(usType);
  v_U8_t              ucMPDUHOffset;
  v_U8_t              ucMPDUHLen = 0;
  v_U16_t             usActualHLen = 0;

  /*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

  /*------------------------------------------------------------------------
    Extract OUI and type from LLC and validate; if non-data send to BAP
  -------------------------------------------------------------------------*/
  if ( VOS_FALSE == WDA_IS_RX_LLC_PRESENT(pvBDHeader) )
  {
    TLLOGW(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_WARN,
          "WLAN TL:LLC header removed, cannot determine BT-AMP type -"
              "dropping pkt"));
    /* Drop packet */
    vos_pkt_return_packet(vosTempBuff);
    return VOS_TRUE; 
  }

  usMPDUDOffset = (v_U8_t)WDA_GET_RX_MPDU_DATA_OFFSET(pvBDHeader);
  ucMPDUHOffset = (v_U8_t)WDA_GET_RX_MPDU_HEADER_OFFSET(pvBDHeader);
  ucMPDUHLen    = (v_U8_t)WDA_GET_RX_MPDU_HEADER_LEN(pvBDHeader);
  ucOffset      = (v_U8_t)usMPDUDOffset + WLANTL_LLC_OUI_OFFSET;

  vosStatus = vos_pkt_extract_data( vosTempBuff, ucOffset,
                                (v_PVOID_t)ucOUI, &usOUISize);

#if 0
  // Compare returns 1 if values are same and 0
  // if not the same.
  if (( WLANTL_LLC_OUI_SIZE != usOUISize ) ||
     ( 0 == vos_mem_compare(ucOUI, (v_PVOID_t)WLANTL_BT_AMP_OUI,
                            WLANTL_LLC_OUI_SIZE ) ))
  {
    TLLOGW(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_WARN,
                "LLC header points to diff OUI in BT-AMP station -"
                "dropping pkt"));
    /* Drop packet */
    vos_pkt_return_packet(vosTempBuff);
    return VOS_TRUE;
  }
#endif
  /*------------------------------------------------------------------------
    Extract LLC OUI and ensure that this is indeed a BT-AMP frame
   ------------------------------------------------------------------------*/
  vosStatus = vos_pkt_extract_data( vosTempBuff,
                                 ucOffset + WLANTL_LLC_OUI_SIZE,
                                (v_PVOID_t)&usType, &usTypeLen);

  if (( VOS_STATUS_SUCCESS != vosStatus) ||
      ( sizeof(usType) != usTypeLen ))
  {
    TLLOGW(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_WARN,
                "Unable to extract type on incoming BAP packet -"
                "dropping pkt"));
    /* Drop packet */
    vos_pkt_return_packet(vosTempBuff);
    return VOS_TRUE;
  }

  /*------------------------------------------------------------------------
    Check if this is BT-AMP data or ctrl packet(RSN, LinkSvision, ActivityR)
   ------------------------------------------------------------------------*/
  usType = vos_be16_to_cpu(usType);

  if (WLANTL_BAP_IS_NON_DATA_PKT_TYPE(usType))
  {
    TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
      "WLAN TL:Non-data packet received over BT-AMP link: %d, => BAP",
               usType));

    /*Flatten packet as BAP expects to be able to peek*/
    if ( VOS_STATUS_SUCCESS != vos_pkt_flatten_rx_pkt(&vosTempBuff))
    {
      TLLOGW(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_WARN,
                 "WLAN TL:Cannot flatten BT-AMP packet - dropping"));
      /* Drop packet */
      vos_pkt_return_packet(vosTempBuff);
      return VOS_TRUE;
    }

    /* Send packet to BAP client*/

    VOS_ASSERT(pTLCb->tlBAPClient.pfnTlBAPRx != NULL);

    if ( VOS_STATUS_SUCCESS != WDA_DS_TrimRxPacketInfo( vosTempBuff ) )
    {
      TLLOGW(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_WARN,
        "WLAN TL:BD header corrupted - dropping packet"));
      /* Drop packet */
      vos_pkt_return_packet(vosTempBuff);
      return VOS_TRUE;
    }

    if ( 0 == WDA_GET_RX_FT_DONE(pvBDHeader) )
    {
      TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
          "Non-data packet received over BT-AMP link: Sending it for "
          "frame Translation"));

      if (usMPDUDOffset > ucMPDUHOffset)
      {
        usActualHLen = usMPDUDOffset - ucMPDUHOffset;
      }

      /* software frame translation for BTAMP WDS.*/
      WLANTL_Translate80211To8023Header( vosTempBuff, &vosStatus, usActualHLen,
                                         ucMPDUHLen, pTLCb,ucSTAId );
      
    }
    if (pTLCb->tlBAPClient.pfnTlBAPRx)
        pTLCb->tlBAPClient.pfnTlBAPRx( vos_get_global_context(VOS_MODULE_ID_TL,pTLCb),
                                       vosTempBuff,
                                       (WLANTL_BAPFrameEnumType)usType );

    return VOS_TRUE;
  }
  else
  {
    TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
      "WLAN TL: BAP DATA packet received over BT-AMP link: %d, => BAP",
               usType));
   /*!!!FIX ME!!*/
 #if 0
    /*--------------------------------------------------------------------
     For data packet collect phy stats RSSI and Link Quality
     Calculate the RSSI average and save it. Continuous average is done.
    --------------------------------------------------------------------*/
    if ( *pFirstDataPktArrived == 0)
    {
      pTLCb->atlSTAClients[ucSTAId].rssiAvg =
         WLANHAL_GET_RSSI_AVERAGE( pvBDHeader );
      pTLCb->atlSTAClients[ucSTAId].uLinkQualityAvg = 
        WLANHAL_RX_BD_GET_SNR( pvBDHeader );

      // Rcvd 1st pkt, start average from next time
      *pFirstDataPktArrived = 1;
    }
    else
    {
      pTLCb->atlSTAClients[ucSTAId].rssiAvg =
          (WLANHAL_GET_RSSI_AVERAGE( pvBDHeader ) + 
           pTLCb->atlSTAClients[ucSTAId].rssiAvg)/2;
      pTLCb->atlSTAClients[ucSTAId].uLinkQualityAvg =
          (WLANHAL_RX_BD_GET_SNR( pvBDHeader ) +  
           pTLCb->atlSTAClients[ucSTAId].uLinkQualityAvg)/2;
    }/*Else, first data packet*/
 #endif
  }/*BT-AMP data packet*/

  return VOS_FALSE; 
}/*WLANTL_ProcessBAPFrame*/

#ifdef WLAN_SOFTAP_FEATURE

/*==========================================================================

  FUNCTION    WLANTL_ProcessFCFrame

  DESCRIPTION
    Internal utility function for processing incoming Flow Control frames. Enable
    or disable LWM mode based on the information.

  DEPENDENCIES
    TL must be initiailized before this function gets called.
    FW sends up special flow control frame.

  PARAMETERS

    IN
    pvosGCtx                pointer to vos global context
    pvBDHeader:             pointer to the BD header
    pTLCb:                  TL control block
    pvBDHeader              pointer to BD header.

    IN/OUT
    pFirstDataPktArrived:   static from caller function; used for rssi 
                            computation
  RETURN VALUE
    The result code associated with performing the operation

    VOS_STATUS_E_INVAL:   Input frame are invalid
    VOS_STATUS_E_FAULT:   pointer to TL cb is NULL ; access would cause a
                          page fault
    VOS_STATUS_SUCCESS:   Everything is good :)

  SIDE EFFECTS
    The ingress and egress of each station will be updated. If needed, LWM mode will
    be enabled or disabled based on the flow control algorithm.

============================================================================*/
v_BOOL_t
WLANTL_ProcessFCFrame
(
  v_PVOID_t         pvosGCtx,
  vos_pkt_t*        pvosDataBuff,
  v_PVOID_t         pvBDHeader
)
{
#if 1 //enable processing of only fcStaTxDisabled bitmap for now. the else part is old better qos code.
      // need to revisit the old code for full implementation.
  v_U8_t               ucTxSuspended = 0, ucTxSuspendReq = 0, ucTxResumeReq = 0;
  WLANTL_CbType*       pTLCb = NULL;

  /*------------------------------------------------------------------------
     Extract TL control block
  ------------------------------------------------------------------------*/
  pTLCb = VOS_GET_TL_CB(pvosGCtx);
  if ( NULL == pTLCb )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
          "WLAN TL:Invalid TL pointer from pvosGCtx on WLANTL_SuspendDataTx"));
    return VOS_STATUS_E_FAULT;
  }

  /* Existing Stations with Tx suspended */
  ucTxSuspended = pTLCb->ucTxSuspended;

  /* Suspend Request Received */
  ucTxSuspendReq = (v_U8_t) WDA_GET_RX_FC_STA_TX_DISABLED_BITMAP(pvBDHeader);
  TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
         "WLANTL_ProcessFCFrame called for Stations:: Current: %x Requested: %x ", ucTxSuspended, ucTxSuspendReq));

  ucTxResumeReq = ucTxSuspendReq ^ ( ucTxSuspended | ucTxSuspendReq );
  ucTxSuspendReq = ucTxSuspendReq ^ ( ucTxSuspended & ucTxSuspendReq );
  TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
         "Station Suspend request processed :: Suspend: %x :Resume: %x ", ucTxSuspendReq, ucTxResumeReq));

  if ( 0 != ucTxSuspendReq )
  {
    WLANTL_SuspendDataTx(pvosGCtx, &ucTxSuspendReq, NULL);
  }
  if ( 0 != ucTxResumeReq )
  {
    WLANTL_ResumeDataTx(pvosGCtx, &ucTxResumeReq);
  }

#else
  VOS_STATUS          vosStatus;
  tpHalFcRxBd         pvFcRxBd = NULL;
  v_U8_t              ucBitCheck = 0x1;
  v_U8_t              ucStaValid = 0;
  v_U8_t              ucSTAId = 0;

      VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO,
                 "Received FC Response");
  if ( (NULL == pTLCb) || (NULL == pvosDataBuff))
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
        "WLAN TL:Invalid pointer in %s \n", __FUNCTION__));
    return VOS_STATUS_E_FAULT;
  }
  vosStatus = vos_pkt_peek_data( pvosDataBuff, 0, (v_PVOID_t)&pvFcRxBd,
                                   sizeof(tHalFcRxBd));

  if ( (VOS_STATUS_SUCCESS != vosStatus) || (NULL == pvFcRxBd) )
  {
      TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
                 "WLAN TL:wrong FC Rx packet"));
      return VOS_STATUS_E_INVAL;
  }
  
  // need to swap bytes in the FC contents.  
  WLANHAL_SwapFcRxBd(&pvFcRxBd->fcSTATxQLen[0]);

  //logic to enable/disable LWM mode for each station
  for( ucStaValid = (v_U8_t)pvFcRxBd->fcSTAValidMask; ucStaValid; ucStaValid >>= 1, ucBitCheck <<= 1, ucSTAId ++)
  {
    if ( (0 == (ucStaValid & 0x1)) || (0 == pTLCb->atlSTAClients[ucSTAId].ucExists) )
    {
      continue;
    }

    if ( pvFcRxBd->fcSTAThreshIndMask & ucBitCheck )
    {
      //LWM event is reported by FW. Able to fetch more packet
      if( pTLCb->atlSTAClients[ucSTAId].ucLwmModeEnabled )
      {
        //Now memory usage is below LWM. Station can send more packets.
        pTLCb->atlSTAClients[ucSTAId].ucLwmEventReported = TRUE;
      }
      else
      {
        TLLOG4(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_LOW,
                 "WLAN TL: FW report LWM event but the station %d is not in LWM mode \n", ucSTAId));
      }
    }

    //calculate uEgress_length/uIngress_length only after receiving enough packets
    if (WLANTL_LWM_INGRESS_SAMPLE_THRESHOLD <= pTLCb->atlSTAClients[ucSTAId].uIngress_length)
    {
      //check memory usage info to see whether LWM mode should be enabled for the station
      v_U32_t uEgress_length = pTLCb->atlSTAClients[ucSTAId].uIngress_length + 
        pTLCb->atlSTAClients[ucSTAId].bmuMemConsumed - pvFcRxBd->fcSTATxQLen[ucSTAId];

      //if ((float)uEgress_length/(float)pTLCb->atlSTAClients[ucSTAId].uIngress_length 
      //      <= WLANTL_LWM_EGRESS_INGRESS_THRESHOLD)
      if ( (pTLCb->atlSTAClients[ucSTAId].uIngress_length > uEgress_length) &&
           ((pTLCb->atlSTAClients[ucSTAId].uIngress_length - uEgress_length ) >= 
            (pTLCb->atlSTAClients[ucSTAId].uIngress_length >> 2))
         )
      {   
         TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
                    "WLAN TL:Enable LWM mode for station %d\n", ucSTAId));
         pTLCb->atlSTAClients[ucSTAId].ucLwmModeEnabled = TRUE;
      }
      else
      {
        if( pTLCb->atlSTAClients[ucSTAId].ucLwmModeEnabled )
        {
          TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
                    "WLAN TL:Disable LWM mode for station %d\n", ucSTAId));
          pTLCb->atlSTAClients[ucSTAId].ucLwmModeEnabled = FALSE;
        }

      }

      //remember memory usage in FW starting from this round
      pTLCb->atlSTAClients[ucSTAId].bmuMemConsumed = pvFcRxBd->fcSTATxQLen[ucSTAId];
      pTLCb->atlSTAClients[ucSTAId].uIngress_length = 0;
    } //(WLANTL_LWM_INGRESS_SAMPLE_THRESHOLD <= pTLCb->atlSTAClients[ucSTAId].uIngress_length)

    if( pTLCb->atlSTAClients[ucSTAId].ucLwmModeEnabled )
    {
      //always update current maximum allowed memeory usage
      pTLCb->atlSTAClients[ucSTAId].uBuffThresholdMax =  WLANTL_STA_BMU_THRESHOLD_MAX -
        pvFcRxBd->fcSTATxQLen[ucSTAId];
    }

  }
#endif

  return VOS_STATUS_SUCCESS;
}
#endif


/*==========================================================================

  FUNCTION    WLANTL_RxFrames

  DESCRIPTION
    Callback registered by TL and called by BAL when a packet is received
    over the bus. Upon the call of this function TL will make the necessary
    decision with regards to the forwarding or queuing of this packet and
    the layer it needs to be delivered to.

  DEPENDENCIES
    TL must be initiailized before this function gets called.
    If the frame carried is a data frame then the station for which it is
    destined to must have been previously registered with TL.

  PARAMETERS

    IN
    pvosGCtx:       pointer to the global vos context; a handle to TL's
                    or BAL's control block can be extracted from its context

    vosDataBuff:   it will contain a pointer to the first buffer received,
                    if there is more then one packet they will be chained
                    using vOSS buffers.

  RETURN VALUE
    The result code associated with performing the operation

    VOS_STATUS_E_INVAL:   Input parameters are invalid
    VOS_STATUS_E_FAULT:   pointer to TL cb is NULL ; access would cause a
                          page fault
    VOS_STATUS_SUCCESS:   Everything is good :)

  SIDE EFFECTS

============================================================================*/
VOS_STATUS
WLANTL_RxFrames
(
  v_PVOID_t      pvosGCtx,
  vos_pkt_t     *pFrameDataBuff
)
{
  vos_pkt_t*          vosDataBuff = (vos_pkt_t*)pFrameDataBuff;
  WLANTL_CbType*      pTLCb = NULL;
  WLANTL_STAFuncType  pfnSTAFsm;
  vos_pkt_t*          vosTempBuff;
  v_U8_t              ucSTAId;
  VOS_STATUS          vosStatus;
  v_U8_t              ucFrmType;
  v_PVOID_t           pvBDHeader = NULL;
  WLANTL_STAEventType wSTAEvent  = WLANTL_RX_EVENT;
  v_U8_t              ucTid      = 0;
  v_BOOL_t            broadcast  = VOS_FALSE;
  v_BOOL_t            selfBcastLoopback = VOS_FALSE;
  static v_U8_t       first_data_pkt_arrived = 0;
  v_U32_t             uDPUSig; 
#ifdef WLAN_SOFTAP_FEATURE
  v_U16_t             usPktLen;
#endif
  /*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

  TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
             "WLAN TL:TL Receive Frames called"));

  /*------------------------------------------------------------------------
    Sanity check
    ------------------------------------------------------------------------*/
  if ( NULL == vosDataBuff )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
               "WLAN TL:Invalid parameter sent on WLANTL_RxFrames"));
    return VOS_STATUS_E_INVAL;
  }

  /*------------------------------------------------------------------------
    Extract TL control block
   ------------------------------------------------------------------------*/
  pTLCb = VOS_GET_TL_CB(pvosGCtx);
  if ( NULL == pTLCb )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
        "WLAN TL:Invalid TL pointer from pvosGCtx on WLANTL_ChangeSTAState"));
    return VOS_STATUS_E_FAULT;
  }

  /*---------------------------------------------------------------------
    Save the initial buffer - this is the first received buffer
   ---------------------------------------------------------------------*/
  vosTempBuff = vosDataBuff;

  while ( NULL != vosTempBuff )
  {
    vos_pkt_walk_packet_chain( vosDataBuff, &vosDataBuff, 1/*true*/ );

    /*---------------------------------------------------------------------
      Peek at BD header - do not remove
      !!! Optimize me: only part of header is needed; not entire one
     ---------------------------------------------------------------------*/
    vosStatus = WDA_DS_PeekRxPacketInfo( vosTempBuff, (v_PVOID_t)&pvBDHeader, 1/*Swap BD*/ );

    if ( NULL == pvBDHeader )
    {
      TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
                 "WLAN TL:Cannot extract BD header"));
      /* Drop packet */
      vos_pkt_return_packet(vosTempBuff);
      vosTempBuff = vosDataBuff;
      continue;
    }

#ifdef WLAN_SOFTAP_FEATURE
    /*---------------------------------------------------------------------
      Check if FC frame reported from FW
    ---------------------------------------------------------------------*/
    if(WDA_IS_RX_FC(pvBDHeader))
    {
      TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
                 "WLAN TL:receive one FC frame"));

      WLANTL_ProcessFCFrame(pvosGCtx, vosTempBuff, pvBDHeader);
      /* Drop packet */
      vos_pkt_return_packet(vosTempBuff);
      vosTempBuff = vosDataBuff;
      continue;
    }
#endif

    /* AMSDU HW bug fix
     * After 2nd AMSDU subframe HW could not handle BD correctly
     * HAL workaround is needed */
    if(WDA_GET_RX_ASF(pvBDHeader))
    {
      WDA_DS_RxAmsduBdFix(pvosGCtx, pvBDHeader);
    }

    /*---------------------------------------------------------------------
      Extract frame control field from 802.11 header if present 
      (frame translation not done) 
    ---------------------------------------------------------------------*/

    vosStatus = WDA_DS_GetFrameTypeSubType( pvosGCtx, vosTempBuff,
                         pvBDHeader, &ucFrmType );
    if ( VOS_STATUS_SUCCESS != vosStatus )
    {
      TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
                   "WLAN TL:Cannot extract Frame Control Field"));
      /* Drop packet */
      vos_pkt_return_packet(vosTempBuff);
      vosTempBuff = vosDataBuff;
      continue;
    }

#ifdef WLAN_SOFTAP_FEATURE
    vos_pkt_get_packet_length(vosTempBuff, &usPktLen);
#endif

    /*---------------------------------------------------------------------
      Check if management and send to PE
    ---------------------------------------------------------------------*/

    if ( WLANTL_IS_MGMT_FRAME(ucFrmType) )
    {
      TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
                 "WLAN TL:Sending packet to management client"));
      if ( VOS_STATUS_SUCCESS != vos_pkt_flatten_rx_pkt(&vosTempBuff))
      {
        TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
                   "WLAN TL:Cannot flatten packet - dropping"));
        /* Drop packet */
        vos_pkt_return_packet(vosTempBuff);
        vosTempBuff = vosDataBuff;
        continue;
      }
      ucSTAId = (v_U8_t)WDA_GET_RX_STAID( pvBDHeader );
      /* Read RSSI and update */
      if(!WLANTL_STA_ID_INVALID(ucSTAId))
      {
#if defined WLAN_FEATURE_NEIGHBOR_ROAMING
        /* Read RSSI and update */
        vosStatus = WLANTL_HSHandleRXFrame(pvosGCtx,
                                           WLANTL_MGMT_FRAME_TYPE,
                                           pvBDHeader,
                                           ucSTAId,
                                           VOS_FALSE,
                                           NULL);
#else
        vosStatus = WLANTL_ReadRSSI(pvosGCtx, pvBDHeader, ucSTAId);
#endif
      }

      if(!VOS_IS_STATUS_SUCCESS(vosStatus))
      {
        TLLOGW(VOS_TRACE(VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_WARN,
          "Handle RX Management Frame fail within Handoff support module"));
        /* Do Not Drop packet at here 
         * Revisit why HO module return fail
        vos_pkt_return_packet(vosTempBuff);
        vosTempBuff = vosDataBuff;
        continue;
         */
      }
      pTLCb->tlMgmtFrmClient.pfnTlMgmtFrmRx( pvosGCtx, vosTempBuff); 
    }
    else /* Data Frame */
    {
      ucSTAId = (v_U8_t)WDA_GET_RX_STAID( pvBDHeader );
      ucTid   = (v_U8_t)WDA_GET_RX_TID( pvBDHeader );

      TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
                 "WLAN TL:Data packet received for STA %d", ucSTAId));

      /*------------------------------------------------------------------
        This should be corrected when multipe sta support is added !!!
        for now bcast frames will be sent to the last registered STA
       ------------------------------------------------------------------*/
      if ( WDA_IS_RX_BCAST(pvBDHeader))
      {
        TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
        "WLAN TL:TL rx Bcast frame - sending to last registered station"));
        broadcast = VOS_TRUE;
        
        /*-------------------------------------------------------------------
          If Addr1 is b/mcast, but Addr3 is our own self MAC, it is a b/mcast
          pkt we sent  looping back to us. To be dropped if we are non BTAMP  
         -------------------------------------------------------------------*/ 
        if( WLANHAL_RX_BD_ADDR3_SELF_IDX == 
            (v_U8_t)WDA_GET_RX_ADDR3_IDX( pvBDHeader )) 
        {
          selfBcastLoopback = VOS_TRUE; 
        }
      }/*if bcast*/

      if ( WLANTL_STA_ID_INVALID(ucSTAId) )
      {
        TLLOGW(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_WARN,
                   "WLAN TL:STA ID invalid - dropping pkt"));
        /* Drop packet */
        vos_pkt_return_packet(vosTempBuff);
        vosTempBuff = vosDataBuff;
        continue;
      }

      /*----------------------------------------------------------------------
        No need to lock cache access because cache manipulation only happens
        in the transport thread/task context
        - These frames are to be forwarded to the station upon registration
          which happens in the main thread context
          The caching here can happen in either Tx or Rx thread depending
          on the current SSC scheduling
        - also we need to make sure that the frames in the cache are fwd-ed to
          the station before the new incoming ones 
      -----------------------------------------------------------------------*/
      if ((( 0 == pTLCb->atlSTAClients[ucSTAId].ucExists ) ||
          ( (0 != pTLCb->atlSTAClients[ucSTAId].ucRxBlocked)
#ifdef WLAN_SOFTAP_FEATURE
            ///@@@: xg: no checking in SOFTAP for now, will revisit later
            && (WLAN_STA_SOFTAP != pTLCb->atlSTAClients[ucSTAId].wSTADesc.wSTAType)
#endif
          ) ||
          ( WLANTL_STA_DISCONNECTED == pTLCb->atlSTAClients[ucSTAId].tlState)) &&
            /*Dont buffer Broadcast/Multicast frames. If AP transmits bursts of Broadcast/Multicast data frames, 
             * libra buffers all Broadcast/Multicast packets after authentication with AP, 
             * So it will lead to low resource condition in Rx Data Path.*/
          ((WDA_IS_RX_BCAST(pvBDHeader) == 0)))
      {
        uDPUSig = WDA_GET_RX_DPUSIG( pvBDHeader );
          //Station has not yet been registered with TL - cache the frame
        WLANTL_CacheSTAFrame( pTLCb, ucSTAId, vosTempBuff, uDPUSig, broadcast, ucFrmType);
        vosTempBuff = vosDataBuff;
        continue;
      }


#ifdef FEATURE_WLAN_CCX
      if ((pTLCb->atlSTAClients[ucSTAId].wSTADesc.ucIsCcxSta)|| broadcast)
      {
        /*--------------------------------------------------------------------
          Filter the IAPP frames for CCX connection; 
          if data it will return false and it 
          will be routed through the regular data path
        --------------------------------------------------------------------*/
        if ( WLANTL_IsIAPPFrame(pvBDHeader,
                                vosTempBuff))
        {
            if ( VOS_STATUS_SUCCESS != vos_pkt_flatten_rx_pkt(&vosTempBuff))
            {
               TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
                        "WLAN TL:Cannot flatten packet - dropping"));
               /* Drop packet */
               vos_pkt_return_packet(vosTempBuff);
            } else {

               TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO,
                        "WLAN TL: Received CCX IAPP Frame"));

               pTLCb->tlMgmtFrmClient.pfnTlMgmtFrmRx( pvosGCtx, vosTempBuff); 
            }
            vosTempBuff = vosDataBuff;
            continue;
        }
      }
#endif

      if ( WLAN_STA_BT_AMP == pTLCb->atlSTAClients[ucSTAId].wSTADesc.wSTAType )
      {
        /*--------------------------------------------------------------------
          Process the ctrl BAP frame; if data it will return false and it 
          will be routed through the regular data path
        --------------------------------------------------------------------*/
        if ( WLANTL_ProcessBAPFrame( pvBDHeader,
                                     vosTempBuff,
                                     pTLCb,
                                    &first_data_pkt_arrived,
                                     ucSTAId))
        {
          vosTempBuff = vosDataBuff;
          continue;
        }
      }/*if BT-AMP station*/
      else if(selfBcastLoopback == VOS_TRUE)
      { 
        /* Drop packet */ 
        vos_pkt_return_packet(vosTempBuff); 
        vosTempBuff = vosDataBuff; 
        continue; 
      } 
      
      /*---------------------------------------------------------------------
        Data packet received, send to state machine
      ---------------------------------------------------------------------*/
      wSTAEvent = WLANTL_RX_EVENT;

      pfnSTAFsm = tlSTAFsm[pTLCb->atlSTAClients[ucSTAId].tlState].
                      pfnSTATbl[wSTAEvent];

      if ( NULL != pfnSTAFsm )
      {
#if defined WLAN_FEATURE_NEIGHBOR_ROAMING
        /* Read RSSI and update */
        vosStatus = WLANTL_HSHandleRXFrame(pvosGCtx,
                                           WLANTL_DATA_FRAME_TYPE,
                                           pvBDHeader,
                                           ucSTAId,
                                           broadcast,
                                           vosTempBuff);
        broadcast = VOS_FALSE;
#else
        vosStatus = WLANTL_ReadRSSI(pvosGCtx, pvBDHeader, ucSTAId);
#endif /*FEATURE_WLAN_GEN6_ROAMING*/
        if(!VOS_IS_STATUS_SUCCESS(vosStatus))
        {
          TLLOGW(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_WARN,
            "Handle RX Data Frame fail within Handoff support module"));
          /* Do Not Drop packet at here 
           * Revisit why HO module return fail
          vos_pkt_return_packet(vosTempBuff);
          vosTempBuff = vosDataBuff;
          continue;
           */
        }
        pfnSTAFsm( pvosGCtx, ucSTAId, &vosTempBuff);
      }
      else
        {
          TLLOGW(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_WARN,
            "WLAN TL:NULL state function, STA:%d, State: %d- dropping packet",
                   ucSTAId, pTLCb->atlSTAClients[ucSTAId].tlState));
          /* Drop packet */
          vos_pkt_return_packet(vosTempBuff);
          vosTempBuff = vosDataBuff;
          continue;
        }

#ifdef WLAN_SOFTAP_FEATURE
    /* RX Statistics Data */
      /* This is RX UC data frame */
      pTLCb->atlSTAClients[ucSTAId].trafficStatistics.rxUCFcnt++;
      pTLCb->atlSTAClients[ucSTAId].trafficStatistics.rxUCBcnt += usPktLen;
#endif

    }/* else data frame*/

    vosTempBuff = vosDataBuff;
  }/*while chain*/

  return VOS_STATUS_SUCCESS;
}/* WLANTL_RxFrames */


/*==========================================================================

  FUNCTION    WLANTL_RxCachedFrames

  DESCRIPTION
    Utility function used by TL to forward the cached frames to a particular
    station; 

  DEPENDENCIES
    TL must be initiailized before this function gets called.
    If the frame carried is a data frame then the station for which it is
    destined to must have been previously registered with TL.

  PARAMETERS

    IN
    pTLCb:   pointer to TL handle 
   
    ucSTAId:    station for which we need to forward the packets

    vosDataBuff:   it will contain a pointer to the first cached buffer
                   received, if there is more then one packet they will be
                   chained using vOSS buffers.

  RETURN VALUE
    The result code associated with performing the operation

    VOS_STATUS_E_INVAL:   Input parameters are invalid
    VOS_STATUS_E_FAULT:   pointer to TL cb is NULL ; access would cause a
                          page fault
    VOS_STATUS_SUCCESS:   Everything is good :)

  SIDE EFFECTS

============================================================================*/
VOS_STATUS
WLANTL_RxCachedFrames
(
  WLANTL_CbType*  pTLCb,
  v_U8_t          ucSTAId,
  vos_pkt_t*      vosDataBuff
)
{
  WLANTL_STAFuncType  pfnSTAFsm;
  vos_pkt_t*          vosTempBuff;
  VOS_STATUS          vosStatus;
  v_PVOID_t           pvBDHeader = NULL;
  WLANTL_STAEventType wSTAEvent  = WLANTL_RX_EVENT;
  v_U8_t              ucTid      = 0;
  v_BOOL_t            broadcast  = VOS_FALSE;
  v_BOOL_t            bSigMatch  = VOS_FALSE; 
  v_BOOL_t            selfBcastLoopback = VOS_FALSE;
  static v_U8_t       first_data_pkt_arrived = 0;
  v_U32_t             uDPUSig; 
  v_U8_t              ucUcastSig; 
  v_U8_t              ucBcastSig; 
  /*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

  TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
             "WLAN TL:TL Receive Cached Frames called"));

  /*------------------------------------------------------------------------
    Sanity check
    ------------------------------------------------------------------------*/
  if ( NULL == vosDataBuff )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
               "WLAN TL:Invalid parameter sent on WLANTL_RxFrames"));
    return VOS_STATUS_E_INVAL;
  }

  /*---------------------------------------------------------------------
    Save the initial buffer - this is the first received buffer
   ---------------------------------------------------------------------*/
  vosTempBuff = vosDataBuff;

  while ( NULL != vosTempBuff )
  {
    vos_pkt_walk_packet_chain( vosDataBuff, &vosDataBuff, 1/*true*/ );

          TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
               "WLAN TL:Sending new cached packet to station %d", ucSTAId));
    /*---------------------------------------------------------------------
      Peek at BD header - do not remove
      !!! Optimize me: only part of header is needed; not entire one
     ---------------------------------------------------------------------*/
    vosStatus = WDA_DS_PeekRxPacketInfo( vosTempBuff, (v_PVOID_t)&pvBDHeader, 0 );

    if ( NULL == pvBDHeader )
          {
            TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
                 "WLAN TL:Cannot extract BD header"));
          /* Drop packet */
          vos_pkt_return_packet(vosTempBuff);
          vosTempBuff = vosDataBuff;
          continue;
        }

    uDPUSig = WDA_GET_RX_DPUSIG( pvBDHeader );

    /* AMSDU HW bug fix
     * After 2nd AMSDU subframe HW could not handle BD correctly
     * HAL workaround is needed */
    if(WDA_GET_RX_ASF(pvBDHeader))
    {
      WDA_DS_RxAmsduBdFix(vos_get_global_context(VOS_MODULE_ID_TL,pTLCb), 
                           pvBDHeader);
    }

    ucTid   = (v_U8_t)WDA_GET_RX_TID( pvBDHeader );

    VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
               "WLAN TL:Data packet cached for STA %d", ucSTAId);

    /*------------------------------------------------------------------
      This should be corrected when multipe sta support is added !!!
      for now bcast frames will be sent to the last registered STA
     ------------------------------------------------------------------*/
    if ( WDA_IS_RX_BCAST(pvBDHeader))
    {
          TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
                 "WLAN TL:TL rx Bcast frame "));
      broadcast = VOS_TRUE;

      /* If Addr1 is b/mcast, but Addr3 is our own self MAC, it is a b/mcast 
       * pkt we sent looping back to us. To be dropped if we are non BTAMP  
       */ 
      if( WLANHAL_RX_BD_ADDR3_SELF_IDX == 
          (v_U8_t)WDA_GET_RX_ADDR3_IDX( pvBDHeader )) 
      {
        selfBcastLoopback = VOS_TRUE; 
      }
    }/*if bcast*/

     /*-------------------------------------------------------------------------
      Check if the packet that we cached matches the DPU signature of the
      newly added station 
    -------------------------------------------------------------------------*/
    if ( broadcast )
    {
      ucBcastSig = (v_U8_t)uDPUSig;
      bSigMatch = (( WLAN_TL_INVALID_B_SIG != 
                    pTLCb->atlSTAClients[ucSTAId].wSTADesc.ucBcastSig ) &&
        ( ucBcastSig == pTLCb->atlSTAClients[ucSTAId].wSTADesc.ucBcastSig ));
    }
    else
    {
      ucUcastSig = (v_U8_t)uDPUSig;
      bSigMatch = (( WLAN_TL_INVALID_U_SIG != 
                      pTLCb->atlSTAClients[ucSTAId].wSTADesc.ucUcastSig ) &&
          ( ucUcastSig == pTLCb->atlSTAClients[ucSTAId].wSTADesc.ucUcastSig ));
    }

    /*-------------------------------------------------------------------------
      If the packet doesn't match - drop it 
    -------------------------------------------------------------------------*/
    if ( !bSigMatch )
    {
            TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
        "WLAN TL: Cached packet does not match DPU Sig of the new STA - drop "
        " DPU Sig %d  UC %d BC %d B %d",
        uDPUSig,
        pTLCb->atlSTAClients[ucSTAId].wSTADesc.ucUcastSig,
        pTLCb->atlSTAClients[ucSTAId].wSTADesc.ucUcastSig,
        broadcast));

      /* Drop packet */ 
      vos_pkt_return_packet(vosTempBuff); 
      vosTempBuff = vosDataBuff; 
      continue; 

    }/*if signature mismatch*/

    /*------------------------------------------------------------------------
      Check if BT-AMP frame:
      - additional processing needed in this case to separate BT-AMP date
        from BT-AMP Ctrl path 
    ------------------------------------------------------------------------*/
    if ( WLAN_STA_BT_AMP == pTLCb->atlSTAClients[ucSTAId].wSTADesc.wSTAType )
    {
      /*--------------------------------------------------------------------
        Process the ctrl BAP frame; if data it will return false and it 
        will be routed through the regular data path
      --------------------------------------------------------------------*/
      if ( WLANTL_ProcessBAPFrame( pvBDHeader,
                                   vosTempBuff,
                                   pTLCb,
                                  &first_data_pkt_arrived,
                                   ucSTAId))
      {
          vosTempBuff = vosDataBuff;
          continue;
        }
      }/*if BT-AMP station*/
      else if(selfBcastLoopback == VOS_TRUE)
      { 
        /* Drop packet */ 
        vos_pkt_return_packet(vosTempBuff); 
        vosTempBuff = vosDataBuff; 
        continue; 
      } 
      
      /*---------------------------------------------------------------------
        Data packet received, send to state machine
      ---------------------------------------------------------------------*/
      wSTAEvent = WLANTL_RX_EVENT;

      pfnSTAFsm = tlSTAFsm[pTLCb->atlSTAClients[ucSTAId].tlState].
                      pfnSTATbl[wSTAEvent];

      if ( NULL != pfnSTAFsm )
      {
#if defined WLAN_FEATURE_NEIGHBOR_ROAMING
        /* Read RSSI and update */
        vosStatus = WLANTL_HSHandleRXFrame(vos_get_global_context(
                                         VOS_MODULE_ID_TL,pTLCb),
                                           WLANTL_DATA_FRAME_TYPE,
                                           pvBDHeader,
                                           ucSTAId,
                                           broadcast,
                                           vosTempBuff);
        broadcast = VOS_FALSE;
#else
        vosStatus = WLANTL_ReadRSSI(vos_get_global_context(VOS_MODULE_ID_TL,pTLCb), pvBDHeader, ucSTAId);
#endif /*FEATURE_WLAN_GEN6_ROAMING*/
        if(!VOS_IS_STATUS_SUCCESS(vosStatus))
        {
          TLLOGE(VOS_TRACE(VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
            "Handle RX Data Frame fail within Handoff support module"));
          /* Do Not Drop packet at here 
           * Revisit why HO module return fail
          vos_pkt_return_packet(vosTempBuff);
          vosTempBuff = vosDataBuff;
          continue;
           */
        }
        pfnSTAFsm( vos_get_global_context(VOS_MODULE_ID_TL,pTLCb), ucSTAId, 
                 &vosTempBuff);
      }
      else
      {
        TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
            "WLAN TL:NULL state function, STA:%d, State: %d- dropping packet",
                   ucSTAId, pTLCb->atlSTAClients[ucSTAId].tlState));
        /* Drop packet */
        vos_pkt_return_packet(vosTempBuff);
        vosTempBuff = vosDataBuff;
        continue;
      }

    vosTempBuff = vosDataBuff;
  }/*while chain*/

  return VOS_STATUS_SUCCESS;
}/* WLANTL_RxCachedFrames */

/*==========================================================================
  FUNCTION    WLANTL_ResourceCB

  DESCRIPTION
    Called by the TL when it has packets available for transmission.

  DEPENDENCIES
    The TL must be registered with BAL before this function can be called.

  PARAMETERS

    IN
    pvosGCtx:       pointer to the global vos context; a handle to TL's
                    or BAL's control block can be extracted from its context

  RETURN VALUE
    The result code associated with performing the operation

    VOS_STATUS_E_FAULT:   pointer to TL cb is NULL ; access would cause a
                          page fault
    VOS_STATUS_SUCCESS:   Everything is good :)

  SIDE EFFECTS

============================================================================*/
VOS_STATUS
WLANTL_ResourceCB
(
  v_PVOID_t       pvosGCtx,
  v_U32_t         uCount
)
{
   WLANTL_CbType*  pTLCb = NULL;
  /*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

  /*------------------------------------------------------------------------
    Sanity check
    Extract TL control block
   ------------------------------------------------------------------------*/
  pTLCb = VOS_GET_TL_CB(pvosGCtx);
  if ( NULL == pTLCb )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
        "WLAN TL:Invalid TL pointer from pvosGCtx on WLANTL_ChangeSTAState"));
    return VOS_STATUS_E_FAULT;
  }

  pTLCb->uResCount = uCount;


  /*-----------------------------------------------------------------------
    Resume Tx if enough res and not suspended
   -----------------------------------------------------------------------*/
  if (( pTLCb->uResCount >=  WDA_TLI_MIN_RES_MF ) &&
      ( 0 == pTLCb->ucTxSuspended ))
  {
    TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
         "WLAN TL:Issuing Xmit start request to BAL for avail res ASYNC"));
    return WDA_DS_StartXmit(pvosGCtx);
  }

  return VOS_STATUS_SUCCESS;
}/* WLANTL_ResourceCB */



/*============================================================================
                           TL STATE MACHINE
============================================================================*/

/*==========================================================================
  FUNCTION    WLANTL_STATxConn

  DESCRIPTION
    Transmit in connected state - only EAPOL and WAI packets allowed

  DEPENDENCIES
    The STA must be registered with TL before this function can be called.

  PARAMETERS

   IN
   pvosGCtx:       pointer to the global vos context; a handle to TL's
                   control block can be extracted from its context
   ucSTAId:        identifier of the station being processed
   vosDataBuff:   pointer to the tx vos buffer

  RETURN VALUE
    The result code associated with performing the operation

    VOS_STATUS_E_FAULT:   pointer to TL cb is NULL ; access would cause a
                          page fault
    VOS_STATUS_SUCCESS:   Everything is good :)

   Other return values are possible coming from the called functions.
   Please check API for additional info.

  SIDE EFFECTS

============================================================================*/
VOS_STATUS
WLANTL_STATxConn
(
  v_PVOID_t     pvosGCtx,
  v_U8_t        ucSTAId,
  vos_pkt_t**   pvosDataBuff
)
{
   v_U16_t              usPktLen;
   VOS_STATUS           vosStatus;
   v_MACADDR_t          vDestMacAddr;
   vos_pkt_t*           vosDataBuff = NULL;
   WLANTL_CbType*       pTLCb       = NULL;
   WLANTL_MetaInfoType  tlMetaInfo;
   v_U8_t               ucTypeSubtype = 0;
   v_U8_t               ucTid;
   v_U8_t               extraHeadSpace = 0;
   v_U8_t               ucWDSEnabled = 0;
  /*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

  /*------------------------------------------------------------------------
    Sanity check
    Extract TL control block
   ------------------------------------------------------------------------*/
  pTLCb = VOS_GET_TL_CB(pvosGCtx);
  if ( NULL == pTLCb )
  {
   TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
             "WLAN TL:Invalid TL pointer from pvosGCtx on WLANTL_STATxConn"));
   *pvosDataBuff = NULL;
    return VOS_STATUS_E_FAULT;
  }

  /*-------------------------------------------------------------------
      Disable AC temporary - if successfull retrieve re-enable
      The order is justified because of the possible scenario
       - TL tryes to fetch packet for AC and it returns NULL
       - TL analyzes the data it has received to see if there are
       any more pkts available for AC -> if not TL will disable AC
       - however it is possible that while analyzing results TL got
       preempted by a pending indication where the mask was again set
       TL will not check again and as a result when it resumes
       execution it will disable AC
       To prevent this the AC will be disabled here and if retrieve
       is successfull it will be re-enabled
  -------------------------------------------------------------------*/

  pTLCb->atlSTAClients[ucSTAId].
     aucACMask[pTLCb->atlSTAClients[ucSTAId].ucCurrentAC] = 0; 

    /*You make an initial assumption that HDD has no more data and if the 
      assumption was wrong you reset the flags to their original state
     This will prevent from exposing a race condition between checking with HDD 
     for packets and setting the flags to false*/
  vos_atomic_set_U8( &pTLCb->atlSTAClients[ucSTAId].ucPktPending, 0);
  pTLCb->atlSTAClients[ucSTAId].ucNoMoreData = 1;

  /*------------------------------------------------------------------------
    Fetch tx packet from HDD
   ------------------------------------------------------------------------*/
#ifdef WLAN_SOFTAP_FEATURE
  if (WLAN_STA_SOFTAP != pTLCb->atlSTAClients[ucSTAId].wSTADesc.wSTAType && 
     (!vos_concurrent_sessions_running()))
  {
#endif
    // don't set 0. 
    //vos_atomic_set_U8( &pTLCb->atlSTAClients[ucSTAId].ucPktPending, 0);
    vosStatus = pTLCb->atlSTAClients[ucSTAId].pfnSTAFetchPkt( pvosGCtx,
                                  &ucSTAId,
                                  pTLCb->atlSTAClients[ucSTAId].ucCurrentAC,
                                  &vosDataBuff, &tlMetaInfo );
#ifdef WLAN_SOFTAP_FEATURE
  }
  else
  {
    //softap case
    WLANTL_ACEnumType ucAC = pTLCb->uCurServedAC;
    vosStatus = pTLCb->atlSTAClients[ucSTAId].pfnSTAFetchPkt( pvosGCtx, 
                               &ucSTAId,
                               ucAC,
                                                &vosDataBuff, &tlMetaInfo );
  }
#endif

  if (( VOS_STATUS_SUCCESS != vosStatus ) || ( NULL == vosDataBuff ))
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
               "WLAN TL:No more data at HDD status %d", vosStatus));
    *pvosDataBuff = NULL;

    /*--------------------------------------------------------------------
    Reset AC for the serviced station to the highest priority AC
    -> due to no more data at the station
    Even if this AC is not supported by the station, correction will be
    made in the main TL loop
    --------------------------------------------------------------------*/
    pTLCb->atlSTAClients[ucSTAId].ucCurrentAC     = WLANTL_AC_VO;
    pTLCb->atlSTAClients[ucSTAId].ucCurrentWeight = 0;

    return vosStatus;
  }

  /*There are still packets in HDD - set back the pending packets and 
   the no more data assumption*/
  vos_atomic_set_U8( &pTLCb->atlSTAClients[ucSTAId].ucPktPending, 1);
  pTLCb->atlSTAClients[ucSTAId].ucNoMoreData = 0;

   pTLCb->atlSTAClients[ucSTAId].
     aucACMask[pTLCb->atlSTAClients[ucSTAId].ucCurrentAC] = 1; 
#ifdef WLAN_PERF 
  vos_pkt_set_user_data_ptr( vosDataBuff, VOS_PKT_USER_DATA_ID_BAL, 
                             (v_PVOID_t)0);

#endif /*WLAN_PERF*/


#ifdef FEATURE_WLAN_WAPI
   /*------------------------------------------------------------------------
    If the packet is neither an Eapol packet nor a WAI packet then drop it
   ------------------------------------------------------------------------*/
   if ( 0 == tlMetaInfo.ucIsEapol && 0 == tlMetaInfo.ucIsWai )
   {
     TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
                "WLAN TL:Only EAPOL or WAI packets allowed before authentication"));

     /* Fail tx for packet */
     pTLCb->atlSTAClients[ucSTAId].pfnSTATxComp( pvosGCtx, vosDataBuff,
                                                VOS_STATUS_E_BADMSG);
     vosDataBuff = NULL;
     *pvosDataBuff = NULL;
     return VOS_STATUS_SUCCESS;
  }
#else
   if ( 0 == tlMetaInfo.ucIsEapol )
  {
     TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
                "WLAN TL:Received non EAPOL packet before authentication"));

    /* Fail tx for packet */
    pTLCb->atlSTAClients[ucSTAId].pfnSTATxComp( pvosGCtx, vosDataBuff,
                                                VOS_STATUS_E_BADMSG);
    vosDataBuff = NULL;
    *pvosDataBuff = NULL;
    return VOS_STATUS_SUCCESS;
  }
#endif /* FEATURE_WLAN_WAPI */

  /*-------------------------------------------------------------------------
   Check TID
  -------------------------------------------------------------------------*/
  ucTid     = tlMetaInfo.ucTID;

  /*Make sure TID is valid*/
  if ( WLANTL_TID_INVALID(ucTid)) 
  {
     TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
            "WLAN TL:Invalid TID sent in meta info %d - defaulting to 0 (BE)", 
             ucTid));
     ucTid = 0; 
  }

  TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
             "WLAN TL:Attaching BD header to pkt on WLANTL_STATxConn"));

#ifdef FEATURE_WLAN_WAPI
  /*------------------------------------------------------------------------
    Translate 802.3 frame to 802.11 if Frame translation is enabled or if 
    frame is a WAI frame.
   ------------------------------------------------------------------------*/
  if ( ( 1 == tlMetaInfo.ucIsWai ) ||
       ( 0 == tlMetaInfo.ucDisableFrmXtl ) )
#else
  /*------------------------------------------------------------------------
    Translate 802.3 frame to 802.11 if Frame translation is enabled 
   ------------------------------------------------------------------------*/
  if ( ( 0 == tlMetaInfo.ucDisableFrmXtl ) &&
      ( 0 != pTLCb->atlSTAClients[ucSTAId].wSTADesc.ucSwFrameTXXlation) )
#endif //#ifdef FEATURE_WLAN_WAPI
  {
    vosStatus =  WLANTL_Translate8023To80211Header( vosDataBuff, &vosStatus,
                                                    pTLCb, ucSTAId,
                                                    tlMetaInfo.ucUP, &ucWDSEnabled, &extraHeadSpace);

    if ( VOS_STATUS_SUCCESS != vosStatus )
    {
      TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
                "WLAN TL:Error when translating header WLANTL_STATxConn"));

      return vosStatus;
    }

    tlMetaInfo.ucDisableFrmXtl = 1;
  }

  /*-------------------------------------------------------------------------
    Call HAL to fill BD header
   -------------------------------------------------------------------------*/
  ucTypeSubtype |= (WLANTL_80211_DATA_TYPE << 4);

  if ( pTLCb->atlSTAClients[ucSTAId].wSTADesc.ucQosEnabled )
  {
    ucTypeSubtype |= (WLANTL_80211_DATA_QOS_SUBTYPE);
  }


  vosStatus = (VOS_STATUS)WDA_DS_BuildTxPacketInfo( pvosGCtx, vosDataBuff , &vDestMacAddr,
                          tlMetaInfo.ucDisableFrmXtl, &usPktLen,
                          pTLCb->atlSTAClients[ucSTAId].wSTADesc.ucQosEnabled, ucWDSEnabled, 
                          extraHeadSpace,
                          ucTypeSubtype, &pTLCb->atlSTAClients[ucSTAId].wSTADesc.vSelfMACAddress,
                          ucTid, HAL_TX_NO_ENCRYPTION_MASK,
                          tlMetaInfo.usTimeStamp, tlMetaInfo.ucIsEapol, tlMetaInfo.ucUP );

  if ( VOS_STATUS_SUCCESS != vosStatus )
  {
     TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
                "WLAN TL:Failed while attempting to fill BD %d", vosStatus));
    *pvosDataBuff = NULL;
    return vosStatus;
  }

  /*-----------------------------------------------------------------------
    Update tx counter for BA session query for tx side
    !1 - should this be done for EAPOL frames?
    -----------------------------------------------------------------------*/
  pTLCb->atlSTAClients[ucSTAId].auTxCount[ucTid]++;

  vos_pkt_set_user_data_ptr( vosDataBuff, VOS_PKT_USER_DATA_ID_TL,
               (v_PVOID_t)pTLCb->atlSTAClients[ucSTAId].pfnSTATxComp );

  /*------------------------------------------------------------------------
    Save data to input pointer for TL core
  ------------------------------------------------------------------------*/
  *pvosDataBuff = vosDataBuff;
  /*security frames cannot be delayed*/
  pTLCb->bUrgent      = TRUE;

#ifdef WLAN_SOFTAP_FEATURE
  /* TX Statistics */
  if (!(tlMetaInfo.ucBcast || tlMetaInfo.ucMcast))
  {
    /* This is TX UC frame */
    pTLCb->atlSTAClients[ucSTAId].trafficStatistics.txUCFcnt++;
    pTLCb->atlSTAClients[ucSTAId].trafficStatistics.txUCBcnt += usPktLen;
  }
#endif

  return VOS_STATUS_SUCCESS;
}/* WLANTL_STATxConn */


/*==========================================================================
  FUNCTION    WLANTL_STATxAuth

  DESCRIPTION
    Transmit in authenticated state - all data allowed

  DEPENDENCIES
    The STA must be registered with TL before this function can be called.

  PARAMETERS

   IN
   pvosGCtx:       pointer to the global vos context; a handle to TL's
                   control block can be extracted from its context
   ucSTAId:        identifier of the station being processed
   vosDataBuff:   pointer to the tx vos buffer

  RETURN VALUE
    The result code associated with performing the operation

    VOS_STATUS_E_FAULT:   pointer to TL cb is NULL ; access would cause a
                          page fault
    VOS_STATUS_SUCCESS:   Everything is good :)

   Other return values are possible coming from the called functions.
   Please check API for additional info.

  SIDE EFFECTS

============================================================================*/
VOS_STATUS
WLANTL_STATxAuth
(
  v_PVOID_t     pvosGCtx,
  v_U8_t        ucSTAId,
  vos_pkt_t**   pvosDataBuff
)
{
   v_U16_t               usPktLen;
   VOS_STATUS            vosStatus;
   v_MACADDR_t           vDestMacAddr;
   vos_pkt_t*            vosDataBuff = NULL;
   WLANTL_CbType*        pTLCb       = NULL;
   WLANTL_MetaInfoType   tlMetaInfo;
   v_U8_t                ucTypeSubtype = 0;
   WLANTL_ACEnumType     ucAC;
   WLANTL_ACEnumType     ucNextAC;
   v_U8_t                ucTid;
   v_U8_t                ucSwFrmXtl = 0;
   v_U8_t                extraHeadSpace = 0;
   WLANTL_STAClientType *pStaClient;
   v_U8_t                ucWDSEnabled = 0;
   v_U8_t                ucTxFlag   = 0; 
   v_U8_t                ucACMask, i; 
  /*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

  /*------------------------------------------------------------------------
    Sanity check
   Extract TL control block
   ------------------------------------------------------------------------*/
  pTLCb = VOS_GET_TL_CB(pvosGCtx);
  if (( NULL == pTLCb ) || ( NULL == pvosDataBuff ))
  {
     TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
            "WLAN TL:Invalid input params on WLANTL_STATxAuth TL %x DB %x",
             pTLCb, pvosDataBuff));
    if (NULL != pvosDataBuff)
    {
        *pvosDataBuff = NULL;
    }
    if(NULL != pTLCb)
    {
        pTLCb->atlSTAClients[ucSTAId].ucNoMoreData = 1;
    }
    return VOS_STATUS_E_FAULT;
  }

  pStaClient = &pTLCb->atlSTAClients[ucSTAId];

  vos_mem_zero(&tlMetaInfo, sizeof(tlMetaInfo));
  /*------------------------------------------------------------------------
    Fetch packet from HDD
   ------------------------------------------------------------------------*/
#ifdef WLAN_SOFTAP_FEATURE
  if ((WLAN_STA_SOFTAP != pStaClient->wSTADesc.wSTAType) &&
      (!vos_concurrent_sessions_running()))
  {
#endif
  ucAC = pStaClient->ucCurrentAC;

  /*-------------------------------------------------------------------
      Disable AC temporary - if successfull retrieve re-enable
      The order is justified because of the possible scenario
       - TL tryes to fetch packet for AC and it returns NULL
       - TL analyzes the data it has received to see if there are
       any more pkts available for AC -> if not TL will disable AC
       - however it is possible that while analyzing results TL got
       preempted by a pending indication where the mask was again set
       TL will not check again and as a result when it resumes
       execution it will disable AC
       To prevent this the AC will be disabled here and if retrieve
       is successfull it will be re-enabled
  -------------------------------------------------------------------*/
  pStaClient->aucACMask[pStaClient->ucCurrentAC] = 0; 

  // don't reset it, as other AC queues in HDD may have packets
  //vos_atomic_set_U8( &pStaClient->ucPktPending, 0);
#ifdef WLAN_SOFTAP_FEATURE
  }
  else
  {
    //softap case
    ucAC = pTLCb->uCurServedAC;
    pStaClient->aucACMask[ucAC] = 0; 

    //vos_atomic_set_U8( &pStaClient->ucPktPending, 0);
  }
#endif

  WLAN_TL_AC_ARRAY_2_MASK( pStaClient, ucACMask, i); 
#ifdef WLAN_SOFTAP_FEATURE
    /*You make an initial assumption that HDD has no more data and if the 
      assumption was wrong you reset the flags to their original state
     This will prevent from exposing a race condition between checking with HDD 
     for packets and setting the flags to false*/
  if ( 0 == ucACMask )
  {
    vos_atomic_set_U8( &pStaClient->ucPktPending, 0);
    pStaClient->ucNoMoreData = 1;
  }
#endif

  vosStatus = pStaClient->pfnSTAFetchPkt( pvosGCtx, 
                               &ucSTAId,
                               ucAC,
                               &vosDataBuff, &tlMetaInfo );


  if (( VOS_STATUS_SUCCESS != vosStatus ) || ( NULL == vosDataBuff ))
  {

    VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO,
               "WLAN TL:Failed while attempting to fetch pkt from HDD %d",
                   vosStatus);
    *pvosDataBuff = NULL;
    /*--------------------------------------------------------------------
      Reset AC for the serviced station to the highest priority AC
      -> due to no more data at the station
      Even if this AC is not supported by the station, correction will be
      made in the main TL loop
    --------------------------------------------------------------------*/
    pStaClient->ucCurrentAC     = WLANTL_AC_VO;
    pStaClient->ucCurrentWeight = 0;

    return vosStatus;
  }

#ifdef WLAN_SOFTAP_FEATURE
  /*There are still packets in HDD - set back the pending packets and 
   the no more data assumption*/
  vos_atomic_set_U8( &pStaClient->ucPktPending, 1);
  pStaClient->ucNoMoreData = 0;
#endif

#ifdef WLAN_SOFTAP_FEATURE
  if (WLAN_STA_SOFTAP != pStaClient->wSTADesc.wSTAType)
  {
#endif
  // don't need to set it, as we don't reset it in this function.
  //vos_atomic_set_U8( &pTLCb->atlSTAClients[ucSTAId].ucPktPending, 1);
#ifdef WLAN_SOFTAP_FEATURE
  }
#endif

#ifdef WLAN_PERF 
   vos_pkt_set_user_data_ptr( vosDataBuff, VOS_PKT_USER_DATA_ID_BAL, 
                       (v_PVOID_t)0);
#endif /*WLAN_PERF*/

   /*-------------------------------------------------------------------------
    Check TID
   -------------------------------------------------------------------------*/
   ucTid     = tlMetaInfo.ucTID;

  /*Make sure TID is valid*/
  if ( WLANTL_TID_INVALID(ucTid)) 
  {
     TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
            "WLAN TL:Invalid TID sent in meta info %d - defaulting to 0 (BE)", 
             ucTid));
     ucTid = 0; 
  }

  /*Save for UAPSD timer consideration*/
  pStaClient->ucServicedAC = ucAC; 

  if ( ucAC == pStaClient->ucCurrentAC ) 
  {
    pStaClient->aucACMask[pStaClient->ucCurrentAC] = 1;
    pStaClient->ucCurrentWeight--;
  }
  else
  {
    pStaClient->ucCurrentAC     = ucAC;
    pStaClient->ucCurrentWeight = 
                         pTLCb->tlConfigInfo.ucAcWeights[ucAC] - 1;

    pStaClient->aucACMask[pStaClient->ucCurrentAC] = 1;

  }

#ifdef WLAN_SOFTAP_FEATURE
  if (WLAN_STA_SOFTAP != pStaClient->wSTADesc.wSTAType)
  {
#endif
  if ( 0 == pStaClient->ucCurrentWeight ) 
  {
    WLANTL_ACEnumType tempAC = ucAC;
    /*-----------------------------------------------------------------------
       Choose next AC - !!! optimize me
    -----------------------------------------------------------------------*/
    while ( 0 != ucACMask ) 
    {
      ucNextAC = (WLANTL_ACEnumType)(( tempAC - 1 ) & WLANTL_MASK_AC); 
      if ( 0 != pStaClient->aucACMask[ucNextAC] )
      {
         pStaClient->ucCurrentAC     = ucNextAC;
         pStaClient->ucCurrentWeight = 
                         pTLCb->tlConfigInfo.ucAcWeights[ucNextAC];

         TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
                    "WLAN TL: Changing serviced AC to: %d with Weight: %d",
                    pStaClient->ucCurrentAC , 
                    pStaClient->ucCurrentWeight));
         break;
      }
      tempAC = ucNextAC;
    }
  }
#ifdef WLAN_SOFTAP_FEATURE
  }
#endif

  TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
             "WLAN TL:Attaching BD header to pkt on WLANTL_STATxAuth"));

  /*------------------------------------------------------------------------
    Translate 802.3 frame to 802.11
   ------------------------------------------------------------------------*/
  if ( 0 == tlMetaInfo.ucDisableFrmXtl )
  {
     /* Needs frame translation */
     // if the client has not enabled SW-only frame translation
     // and if the frame is a unicast frame
     //   (HW frame translation does not support multiple broadcast domains
     //    so we use SW frame translation for broadcast/multicast frames)
#ifdef FEATURE_WLAN_WAPI
     // and if the frame is not a WAPI frame
#endif
     // then use HW_based frame translation

     if ( ( 0 == pStaClient->wSTADesc.ucSwFrameTXXlation ) &&
          ( 0 == tlMetaInfo.ucBcast ) &&
          ( 0 == tlMetaInfo.ucMcast )
#ifdef FEATURE_WLAN_WAPI
          && ( tlMetaInfo.ucIsWai != 1 )
#endif
        )
     {
#ifdef WLAN_PERF 
        v_U32_t uFastFwdOK = 0;

        /* HW based translation. See if the frame could be fast forwarded */
        WDA_TLI_FastHwFwdDataFrame( pvosGCtx, vosDataBuff , &vosStatus, 
                                   &uFastFwdOK, &tlMetaInfo, &pStaClient->wSTADesc);

        if( VOS_STATUS_SUCCESS == vosStatus )
        {
            if(uFastFwdOK)
            {
                /* Packet could be fast forwarded now */
                vos_pkt_set_user_data_ptr( vosDataBuff, VOS_PKT_USER_DATA_ID_TL, 
                               (v_PVOID_t)pStaClient->pfnSTATxComp );

                *pvosDataBuff = vosDataBuff;

                /* TODO: Do we really need to update WLANTL_HSHandleTXFrame() 
                   stats for every pkt? */
                pStaClient->auTxCount[tlMetaInfo.ucTID]++;
                return vosStatus;
             }
             /* can't be fast forwarded, fall through normal (slow) path. */
        }
        else
        {

            TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
                       "WLAN TL:Failed while attempting to fastFwd BD %d", vosStatus));
            *pvosDataBuff = NULL;
            return vosStatus;
        }
#endif /*WLAN_PERF*/
     }
     else
     {
        /* SW based translation */

#ifdef FEATURE_WLAN_WAPI
       gUcIsWai = tlMetaInfo.ucIsWai,
#endif

       vosStatus = WLANTL_Translate8023To80211Header( vosDataBuff, &vosStatus,
                                                   pTLCb, ucSTAId,
                                                   tlMetaInfo.ucUP, &ucWDSEnabled, &extraHeadSpace);
       if ( VOS_STATUS_SUCCESS != vosStatus )
       {
          TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
                    "WLAN TL:Error when translating header WLANTL_STATxAuth"));
          return vosStatus;
       }

       TLLOG4(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_LOW,
                    "WLAN TL software translation success \n"));
       ucSwFrmXtl = 1;
       tlMetaInfo.ucDisableFrmXtl = 1;
    }
  }

  /*-------------------------------------------------------------------------
    Call HAL to fill BD header
   -------------------------------------------------------------------------*/
  ucTypeSubtype |= (WLANTL_80211_DATA_TYPE << 4);

  if ( pStaClient->wSTADesc.ucQosEnabled ) 
  {
    ucTypeSubtype |= (WLANTL_80211_DATA_QOS_SUBTYPE);
  }

  ucTxFlag  = (0 != pStaClient->wUAPSDInfo[ucAC].ucSet)?
              HAL_TRIGGER_ENABLED_AC_MASK:0;

#ifdef FEATURE_WLAN_WAPI
  if ( pStaClient->wSTADesc.ucIsWapiSta == 1 )
  {
#ifdef LIBRA_WAPI_SUPPORT
    ucTxFlag = ucTxFlag | HAL_WAPI_STA_MASK;
#endif //LIBRA_WAPI_SUPPORT
    if ( tlMetaInfo.ucIsWai == 1 ) 
    {
      ucTxFlag = ucTxFlag | HAL_TX_NO_ENCRYPTION_MASK;
    }
  }
#endif /* FEATURE_WLAN_WAPI */

  vosStatus = (VOS_STATUS)WDA_DS_BuildTxPacketInfo( pvosGCtx, 
                     vosDataBuff , &vDestMacAddr,
                     tlMetaInfo.ucDisableFrmXtl, &usPktLen,
                     pStaClient->wSTADesc.ucQosEnabled, ucWDSEnabled, 
                     extraHeadSpace,
                     ucTypeSubtype, &pTLCb->atlSTAClients[ucSTAId].wSTADesc.vSelfMACAddress,
                     ucTid, ucTxFlag, tlMetaInfo.usTimeStamp, 
                     tlMetaInfo.ucIsEapol, tlMetaInfo.ucUP );

  if(!VOS_IS_STATUS_SUCCESS(vosStatus))
  {
    TLLOGE(VOS_TRACE(VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
              "Fill TX BD Error status %d", vosStatus));

    return vosStatus;
  }

#ifdef WLAN_SOFTAP_FEATURE
  /* TX Statistics */
  if (!(tlMetaInfo.ucBcast || tlMetaInfo.ucMcast))
  {
    /* This is TX UC frame */
    pStaClient->trafficStatistics.txUCFcnt++;
    pStaClient->trafficStatistics.txUCBcnt += usPktLen;
  }
#endif

  /*-----------------------------------------------------------------------
    Update tx counter for BA session query for tx side
    -----------------------------------------------------------------------*/
  pStaClient->auTxCount[ucTid]++;

  /* This code is to send traffic with lower priority AC when we does not 
     get admitted to send it. Today HAL does not downgrade AC so this code 
     does not get executed.(In other words, HAL doesn�t change tid. The if 
     statement is always false.)
     NOTE: In the case of LA downgrade occurs in HDD (that was the change 
     Phani made during WMM-AC plugfest). If WM & BMP also took this approach, 
     then there will be no need for any AC downgrade logic in TL/WDI.   */
#if 0
  if (( ucTid != tlMetaInfo.ucTID ) &&
      ( 0 != pStaClient->wSTADesc.ucQosEnabled ) && 
      ( 0 != ucSwFrmXtl ))
  {
    /*---------------------------------------------------------------------
      !! FIX me: Once downgrading is clear put in the proper change
    ---------------------------------------------------------------------*/
    ucQCOffset = WLANHAL_TX_BD_HEADER_SIZE + WLANTL_802_11_HEADER_LEN;

    //!!!Fix this replace peek with extract 
    vos_pkt_peek_data( vosDataBuff, ucQCOffset,(v_PVOID_t)&pucQosCtrl,
                       sizeof(*pucQosCtrl));
    *pucQosCtrl = ucTid; //? proper byte order
  }
#endif

  if ( VOS_STATUS_SUCCESS != vosStatus )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
               "WLAN TL:Failed while attempting to fill BD %d", vosStatus));
    *pvosDataBuff = NULL;
    return vosStatus;
  }

  vos_pkt_set_user_data_ptr( vosDataBuff, VOS_PKT_USER_DATA_ID_TL,
                   (v_PVOID_t)pStaClient->pfnSTATxComp );

  *pvosDataBuff = vosDataBuff;

  /*BE & BK can be delayed, VO and VI not frames cannot be delayed*/
  if ( pStaClient->ucServicedAC > WLANTL_AC_BE ) 
  {
    pTLCb->bUrgent= TRUE;
  }

  return VOS_STATUS_SUCCESS;
}/* WLANTL_STATxAuth */

/*==========================================================================
  FUNCTION    WLANTL_STATxDisc

  DESCRIPTION
    Transmit in disconnected state - no data allowed

  DEPENDENCIES
    The STA must be registered with TL before this function can be called.

  PARAMETERS

   IN
   pvosGCtx:       pointer to the global vos context; a handle to TL's
                   control block can be extracted from its context
   ucSTAId:        identifier of the station being processed
   vosDataBuff:   pointer to the tx vos buffer

  RETURN VALUE
    The result code associated with performing the operation

    VOS_STATUS_SUCCESS:   Everything is good :)

  SIDE EFFECTS

============================================================================*/
VOS_STATUS
WLANTL_STATxDisc
(
  v_PVOID_t     pvosGCtx,
  v_U8_t        ucSTAId,
  vos_pkt_t**   pvosDataBuff
)
{
   WLANTL_CbType*        pTLCb       = NULL;
  /*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/


  /*------------------------------------------------------------------------
    Sanity check
    Extract TL control block
   ------------------------------------------------------------------------*/
  pTLCb = VOS_GET_TL_CB(pvosGCtx);
  if ( NULL == pTLCb )
  {
     TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
            "WLAN TL:Invalid TL pointer from pvosGCtx on WLANTL_STATxAuth"));
    *pvosDataBuff = NULL;
    return VOS_STATUS_E_FAULT;
  }

  /*------------------------------------------------------------------------
    Error
   ------------------------------------------------------------------------*/
  TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
    "WLAN TL:Packet should not be transmitted in state disconnected ignoring"
            " request"));

  *pvosDataBuff = NULL;
   pTLCb->atlSTAClients[ucSTAId].ucNoMoreData = 1;
   
   //Should not be anything pending in disconnect state
   vos_atomic_set_U8( &pTLCb->atlSTAClients[ucSTAId].ucPktPending, 0);

  return VOS_STATUS_SUCCESS;
}/* WLANTL_STATxDisc */

/*==========================================================================
  FUNCTION    WLANTL_STARxConn

  DESCRIPTION
    Receive in connected state - only EAPOL

  DEPENDENCIES
    The STA must be registered with TL before this function can be called.

  PARAMETERS

   IN
   pvosGCtx:       pointer to the global vos context; a handle to TL's
                   control block can be extracted from its context
   ucSTAId:        identifier of the station being processed
   vosDataBuff:   pointer to the tx/rx vos buffer

  RETURN VALUE
    The result code associated with performing the operation

    VOS_STATUS_E_INVAL:   invalid input parameters
    VOS_STATUS_E_FAULT:   pointer to TL cb is NULL ; access would cause a
                          page fault
    VOS_STATUS_SUCCESS:   Everything is good :)

  SIDE EFFECTS

============================================================================*/
VOS_STATUS
WLANTL_STARxConn
(
  v_PVOID_t     pvosGCtx,
  v_U8_t        ucSTAId,
  vos_pkt_t**   pvosDataBuff
)
{
   WLANTL_CbType*           pTLCb = NULL;
   v_U16_t                  usEtherType = 0;
   v_U16_t                  usPktLen;
   v_U8_t                   ucMPDUHOffset;
   v_U16_t                  usMPDUDOffset;
   v_U16_t                  usMPDULen;
   v_U8_t                   ucMPDUHLen;
   v_U16_t                  usActualHLen = 0;
   VOS_STATUS               vosStatus  = VOS_STATUS_SUCCESS;
   vos_pkt_t*               vosDataBuff;
   v_PVOID_t                aucBDHeader;
   v_U8_t                   ucTid;
   WLANTL_RxMetaInfoType    wRxMetaInfo;
  /*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

  /*------------------------------------------------------------------------
    Sanity check
   ------------------------------------------------------------------------*/
  if (( NULL == pvosDataBuff ) || ( NULL == ( vosDataBuff = *pvosDataBuff )))
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
               "WLAN TL:Invalid parameter sent on WLANTL_STARxConn"));
    return VOS_STATUS_E_INVAL;
  }

  /*------------------------------------------------------------------------
    Extract TL control block
   ------------------------------------------------------------------------*/
  pTLCb = VOS_GET_TL_CB(pvosGCtx);
  if ( NULL == pTLCb )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
        "WLAN TL:Invalid TL pointer from pvosGCtx on WLANTL_ChangeSTAState"));
    return VOS_STATUS_E_FAULT;
  }

  /*------------------------------------------------------------------------
    Extract BD header and check if valid
   ------------------------------------------------------------------------*/
  vosStatus = WDA_DS_PeekRxPacketInfo( vosDataBuff, (v_PVOID_t)&aucBDHeader, 0/*Swap BD*/ );

  if ( NULL == aucBDHeader )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
               "WLAN TL:Cannot extract BD header"));
    VOS_ASSERT( 0 );
    return VOS_STATUS_E_FAULT;
  }


  ucMPDUHOffset = (v_U8_t)WDA_GET_RX_MPDU_HEADER_OFFSET(aucBDHeader);
  usMPDUDOffset = (v_U16_t)WDA_GET_RX_MPDU_DATA_OFFSET(aucBDHeader);
  usMPDULen     = (v_U16_t)WDA_GET_RX_MPDU_LEN(aucBDHeader);
  ucMPDUHLen    = (v_U8_t)WDA_GET_RX_MPDU_HEADER_LEN(aucBDHeader);
  ucTid         = (v_U8_t)WDA_GET_RX_TID(aucBDHeader);

  vos_pkt_get_packet_length( vosDataBuff, &usPktLen);

  TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
             "WLAN TL:BD header processing data: HO %d DO %d Len %d HLen %d",
             ucMPDUHOffset, usMPDUDOffset, usMPDULen, ucMPDUHLen));

    /*It will cut out the 802.11 header if not used*/
  if ( VOS_STATUS_SUCCESS != WDA_DS_TrimRxPacketInfo( vosDataBuff ) )
  {
    TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
               "WLAN TL:BD header corrupted - dropping packet"));
    /* Drop packet */
    vos_pkt_return_packet(vosDataBuff);
    return VOS_STATUS_SUCCESS;
  }

  vosStatus = WLANTL_GetEtherType(aucBDHeader,vosDataBuff,ucMPDUHLen,&usEtherType);
  
  if( VOS_IS_STATUS_SUCCESS(vosStatus) )
  {
#ifdef FEATURE_WLAN_WAPI
    /* If frame is neither an EAPOL frame nor a WAI frame then we drop the frame*/
    /* TODO: Do we need a check to see if we are in WAPI mode? If not is it possible */
    /* that we get an EAPOL packet in WAPI mode or vice versa? */
    if ( WLANTL_LLC_8021X_TYPE  != usEtherType && WLANTL_LLC_WAI_TYPE  != usEtherType )
    {
      TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
                 "WLAN TL:Frame not EAPOL or WAI - dropping"));
      /* Drop packet */
      vos_pkt_return_packet(vosDataBuff);
    }
#else
    if ( WLANTL_LLC_8021X_TYPE  != usEtherType )
    {
      TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
                 "WLAN TL:Frame not EAPOL - dropping"));
      /* Drop packet */
      vos_pkt_return_packet(vosDataBuff);
    }
#endif /* FEATURE_WLAN_WAPI */
    else /* Frame is an EAPOL frame or a WAI frame*/  
    {
      if (( 0 == WDA_GET_RX_FT_DONE(aucBDHeader) ) &&
         ( 0 != pTLCb->atlSTAClients[ucSTAId].wSTADesc.ucSwFrameRXXlation))
      {
      if (usMPDUDOffset > ucMPDUHOffset)
      {
         usActualHLen = usMPDUDOffset - ucMPDUHOffset;
      }

      vosStatus = WLANTL_Translate80211To8023Header( vosDataBuff, &vosStatus, usActualHLen, 
                      ucMPDUHLen, pTLCb, ucSTAId);

        if ( VOS_STATUS_SUCCESS != vosStatus ) 
        {
          TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
            "WLAN TL:Failed to translate from 802.11 to 802.3 - dropping"));
          /* Drop packet */
          vos_pkt_return_packet(vosDataBuff);
          return vosStatus;
        }
      }
      /*-------------------------------------------------------------------
      Increment receive counter
      -------------------------------------------------------------------*/
      pTLCb->atlSTAClients[ucSTAId].auRxCount[ucTid]++;

      TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
               "WLAN TL:Sending EAPoL frame to station %d AC %d", ucSTAId, ucTid));

      /*-------------------------------------------------------------------
      !!!Assuming TID = UP mapping 
      -------------------------------------------------------------------*/
      wRxMetaInfo.ucUP = ucTid;

#ifdef WLAN_SOFTAP_FEATURE
      TLLOG4(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_LOW,
               "WLAN TL %s:Sending data chain to station \n", __FUNCTION__));
      if ( WLAN_STA_SOFTAP == pTLCb->atlSTAClients[ucSTAId].wSTADesc.wSTAType )
      {
        wRxMetaInfo.ucDesSTAId = WLAN_RX_SAP_SELF_STA_ID;
        pTLCb->atlSTAClients[ucSTAId].pfnSTARx( pvosGCtx, vosDataBuff, ucSTAId,
                                            &wRxMetaInfo );
      }
      else
#endif
      pTLCb->atlSTAClients[ucSTAId].pfnSTARx( pvosGCtx, vosDataBuff, ucSTAId,
                                            &wRxMetaInfo );
    }/*EAPOL frame or WAI frame*/
  }/*vos status success*/

  return VOS_STATUS_SUCCESS;
}/* WLANTL_STARxConn */

#ifdef WLAN_SOFTAP_FEATURE
/*==========================================================================
  FUNCTION    WLANTL_FwdPktToHDD

  DESCRIPTION
    Determine the Destation Station ID and route the Frame to Upper Layer

  DEPENDENCIES

  PARAMETERS

   IN
   pvosGCtx:       pointer to the global vos context; a handle to TL's
                   control block can be extracted from its context
   ucSTAId:        identifier of the station being processed
   vosDataBuff:   pointer to the rx vos buffer

  RETURN VALUE
    The result code associated with performing the operation

    VOS_STATUS_E_INVAL:   invalid input parameters
    VOS_STATUS_E_FAULT:   pointer to TL cb is NULL ; access would cause a
                          page fault
    VOS_STATUS_SUCCESS:   Everything is good :)

  SIDE EFFECTS

============================================================================*/

VOS_STATUS
WLANTL_FwdPktToHDD
(
  v_PVOID_t       pvosGCtx,
  vos_pkt_t*     pvosDataBuff,
  v_U8_t          ucSTAId
)
{
   v_MACADDR_t DestMacAddress;
   v_MACADDR_t *pDestMacAddress = &DestMacAddress;
   v_SIZE_t usMacAddSize = VOS_MAC_ADDR_SIZE;
   WLANTL_CbType*           pTLCb = NULL;
   vos_pkt_t*               vosDataBuff ;
   VOS_STATUS               vosStatus = VOS_STATUS_SUCCESS;
   v_U32_t                 STAMetaInfo;
   vos_pkt_t*              vosNextDataBuff ;
   v_U8_t                  ucDesSTAId;
   WLANTL_RxMetaInfoType    wRxMetaInfo;


  /*------------------------------------------------------------------------
    Sanity check
   ------------------------------------------------------------------------*/
  if (( NULL == pvosDataBuff ) || ( NULL == ( vosDataBuff = pvosDataBuff )))
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
              "WLAN TL:Invalid parameter sent on WLANTL_FwdPktToHdd"));
    return VOS_STATUS_E_INVAL;
  }

  /*------------------------------------------------------------------------
    Extract TL control block
   ------------------------------------------------------------------------*/
  pTLCb = VOS_GET_TL_CB(pvosGCtx);
  if ( NULL == pTLCb )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
              "WLAN TL:Invalid TL pointer from pvosGCtx on WLANTL_FwdPktToHdd"));
    return VOS_STATUS_E_FAULT;
  }
   /* This the change required for SoftAp to handle Reordered Buffer. Since a STA
      may have packets destined to multiple destinations we have to process each packet
      at a time and determine its Destination. So the Voschain provided by Reorder code
      is unchain and forwarded to Upper Layer after Determining the Destination */

   vosDataBuff = pvosDataBuff;
   while (vosDataBuff != NULL)
   {
      vos_pkt_walk_packet_chain( vosDataBuff, &vosNextDataBuff, 1/*true*/ );
      vos_pkt_get_user_data_ptr( vosDataBuff, VOS_PKT_USER_DATA_ID_TL,
                                 (v_PVOID_t *)&STAMetaInfo );
      wRxMetaInfo.ucUP = (v_U8_t)(STAMetaInfo & WLANTL_AC_MASK);
      ucDesSTAId = ((v_U8_t)STAMetaInfo) >> WLANTL_STAID_OFFSET; 
       
      vosStatus = vos_pkt_extract_data( vosDataBuff, 0, (v_VOID_t *)pDestMacAddress, &usMacAddSize);
      if ( VOS_STATUS_SUCCESS != vosStatus )
      {
         TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
                "WLAN TL: recv corrupted data packet\n"));
         vos_pkt_return_packet(vosDataBuff);
         return vosStatus;
      }

      TLLOG4(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_LOW,"station mac 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x \n",
                       pDestMacAddress->bytes[0], pDestMacAddress->bytes[1], pDestMacAddress->bytes[2],
                       pDestMacAddress->bytes[3], pDestMacAddress->bytes[4], pDestMacAddress->bytes[5]));

      if (vos_is_macaddr_broadcast( pDestMacAddress ) || vos_is_macaddr_group(pDestMacAddress))
      {
          // destination is mc/bc station
          ucDesSTAId = WLAN_RX_BCMC_STA_ID;
          TLLOG4(VOS_TRACE( VOS_MODULE_ID_HDD_SOFTAP, VOS_TRACE_LEVEL_INFO_LOW,
                    "%s: BC/MC packet, id %d\n", __FUNCTION__, WLAN_RX_BCMC_STA_ID));
      }
      else
      {
         if (vos_is_macaddr_equal(pDestMacAddress, &pTLCb->atlSTAClients[ucSTAId].wSTADesc.vSelfMACAddress))
         {
            // destination is AP itself
            ucDesSTAId = WLAN_RX_SAP_SELF_STA_ID;
            TLLOG4(VOS_TRACE( VOS_MODULE_ID_HDD_SOFTAP, VOS_TRACE_LEVEL_INFO_LOW,
                     "%s: packet to AP itself, id %d\n", __FUNCTION__, WLAN_RX_SAP_SELF_STA_ID));
         }
         else if ( WLAN_MAX_STA_COUNT <= ucDesSTAId )
         {
            // destination station is something else
            TLLOG4(VOS_TRACE( VOS_MODULE_ID_HDD_SOFTAP, VOS_TRACE_LEVEL_INFO_LOW,
                 "%s: get an station index larger than WLAN_MAX_STA_COUNT %d\n", __FUNCTION__, ucDesSTAId));
            ucDesSTAId = WLAN_RX_SAP_SELF_STA_ID;
         }

         
         //loopback unicast station comes here
      }

      wRxMetaInfo.ucUP = (v_U8_t)(STAMetaInfo & WLANTL_AC_MASK);
      wRxMetaInfo.ucDesSTAId = ucDesSTAId;
     
   vosStatus = pTLCb->atlSTAClients[ucSTAId].pfnSTARx( pvosGCtx, vosDataBuff, ucDesSTAId,
                                            &wRxMetaInfo );
  if ( VOS_STATUS_SUCCESS != vosStatus )
  {
     TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
                "WLAN TL: failed to send pkt to HDD \n"));
     vos_pkt_return_packet(vosDataBuff);
     return vosStatus;
   }
      vosDataBuff = vosNextDataBuff;
   }
   return VOS_STATUS_SUCCESS;
}
#endif /* WLANTL_SOFTAP_FEATURE */ 

/*==========================================================================
  FUNCTION    WLANTL_STARxAuth

  DESCRIPTION
    Receive in authenticated state - all data allowed

  DEPENDENCIES
    The STA must be registered with TL before this function can be called.

  PARAMETERS

   IN
   pvosGCtx:       pointer to the global vos context; a handle to TL's
                   control block can be extracted from its context
   ucSTAId:        identifier of the station being processed
   vosDataBuff:   pointer to the rx vos buffer

  RETURN VALUE
    The result code associated with performing the operation

    VOS_STATUS_E_INVAL:   invalid input parameters
    VOS_STATUS_E_FAULT:   pointer to TL cb is NULL ; access would cause a
                          page fault
    VOS_STATUS_SUCCESS:   Everything is good :)

  SIDE EFFECTS

============================================================================*/
VOS_STATUS
WLANTL_STARxAuth
(
  v_PVOID_t     pvosGCtx,
  v_U8_t        ucSTAId,
  vos_pkt_t**   pvosDataBuff
)
{
   WLANTL_CbType*           pTLCb = NULL;
   v_U8_t                   ucAsf; /* AMSDU sub frame */
   v_U16_t                  usMPDUDOffset;
   v_U8_t                   ucMPDUHOffset;
   v_U16_t                  usMPDULen;
   v_U8_t                   ucMPDUHLen;
   v_U16_t                  usActualHLen = 0;   
   v_U8_t                   ucTid;
#ifdef FEATURE_WLAN_WAPI
   v_U16_t                  usEtherType;
#endif
   v_U16_t                  usPktLen;
   vos_pkt_t*               vosDataBuff ;
   v_PVOID_t                aucBDHeader;
   VOS_STATUS               vosStatus;
   WLANTL_RxMetaInfoType    wRxMetaInfo;
   static v_U8_t            ucPMPDUHLen = 0;
#ifdef WLAN_SOFTAP_FEATURE
   v_U8_t*                  STAMetaInfoPtr;
#endif
#ifdef ANI_CHIPSET_VOLANS
   v_U8_t                   ucEsf=0; /* first subframe of AMSDU flag */
   v_U64_t                  ullcurrentReplayCounter=0; /*current replay counter*/
   v_U64_t                  ullpreviousReplayCounter=0; /*previous replay counter*/
   v_U16_t                  ucUnicastBroadcastType=0; /*It denotes whether received frame is UC or BC*/
#endif
  /*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

  /*------------------------------------------------------------------------
    Sanity check
   ------------------------------------------------------------------------*/
  if (( NULL == pvosDataBuff ) || ( NULL == ( vosDataBuff = *pvosDataBuff )))
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
              "WLAN TL:Invalid parameter sent on WLANTL_STARxAuth"));
    return VOS_STATUS_E_INVAL;
  }

  /*------------------------------------------------------------------------
    Extract TL control block
   ------------------------------------------------------------------------*/
  pTLCb = VOS_GET_TL_CB(pvosGCtx);
  if ( NULL == pTLCb )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
              "WLAN TL:Invalid TL pointer from pvosGCtx on WLANTL_STARxAuth"));
    return VOS_STATUS_E_FAULT;
  }

  /*------------------------------------------------------------------------
    Extract BD header and check if valid
   ------------------------------------------------------------------------*/
  WDA_DS_PeekRxPacketInfo( vosDataBuff, (v_PVOID_t)&aucBDHeader, 0 );

  ucMPDUHOffset = (v_U8_t)WDA_GET_RX_MPDU_HEADER_OFFSET(aucBDHeader);
  usMPDUDOffset = (v_U16_t)WDA_GET_RX_MPDU_DATA_OFFSET(aucBDHeader);
  usMPDULen     = (v_U16_t)WDA_GET_RX_MPDU_LEN(aucBDHeader);
  ucMPDUHLen    = (v_U8_t)WDA_GET_RX_MPDU_HEADER_LEN(aucBDHeader);
  ucTid         = (v_U8_t)WDA_GET_RX_TID(aucBDHeader);

#ifdef ANI_CHIPSET_VOLANS
  /*Host based replay check is needed for unicast data frames*/
  ucUnicastBroadcastType  = (v_U16_t)WDA_IS_RX_BCAST(aucBDHeader);
#endif
  if(0 != ucMPDUHLen)
  {
    ucPMPDUHLen = ucMPDUHLen;
  }

  TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
             "WLAN TL:BD header processing data: HO %d DO %d Len %d HLen %d"
             " Tid %d BD %d",
             ucMPDUHOffset, usMPDUDOffset, usMPDULen, ucMPDUHLen, ucTid,
             WLANHAL_RX_BD_HEADER_SIZE));

  vos_pkt_get_packet_length( vosDataBuff, &usPktLen);

  if ( VOS_STATUS_SUCCESS != WDA_DS_TrimRxPacketInfo( vosDataBuff ) )
  {
    if((WDA_GET_RX_ASF(aucBDHeader) && !WDA_GET_RX_ESF(aucBDHeader)))
  {
    /* AMSDU case, ucMPDUHOffset = 0
     * it should be hancdled seperatly */
    if(( usMPDUDOffset >  ucMPDUHOffset ) &&
       ( usMPDULen     >= ucMPDUHLen ) && ( usPktLen >= usMPDULen ) &&
       ( !WLANTL_TID_INVALID(ucTid) ))
    {
        ucMPDUHOffset = usMPDUDOffset - WLANTL_MPDU_HEADER_LEN; 
    }
    else
    {
      TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
                "WLAN TL:BD header corrupted - dropping packet"));
      /* Drop packet */
      vos_pkt_return_packet(vosDataBuff);
      return VOS_STATUS_SUCCESS;
    }
  }
  else
  {
    TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
              "WLAN TL:BD header corrupted - dropping packet"));
    /* Drop packet */
    vos_pkt_return_packet(vosDataBuff);
    return VOS_STATUS_SUCCESS;
  }
  }

#ifdef FEATURE_WLAN_WAPI
  if ( pTLCb->atlSTAClients[ucSTAId].wSTADesc.ucIsWapiSta )
  {
    vosStatus = WLANTL_GetEtherType(aucBDHeader, vosDataBuff, ucMPDUHLen, &usEtherType);
    if( VOS_IS_STATUS_SUCCESS(vosStatus) )
    {
      if ( WLANTL_LLC_WAI_TYPE  == usEtherType )
      {        
        if ( !( WLANHAL_RX_IS_UNPROTECTED_WPI_FRAME(aucBDHeader)) )
        {
          TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
                     "WLAN TL:WAI frame was received encrypted - dropping"));
          /* Drop packet */
          /*Temporary fix added to fix wapi rekey issue*/
          //vos_pkt_return_packet(vosDataBuff);
          //return vosStatus; //returning success
        }
      }
      else
      {
        if (  WLANHAL_RX_IS_UNPROTECTED_WPI_FRAME(aucBDHeader) ) 
        {
          TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
                     "WLAN TL:Non-WAI frame was received unencrypted - dropping"));
          /* Drop packet */
          vos_pkt_return_packet(vosDataBuff); 
          return vosStatus; //returning success
        }
      }
    }
    else //could not extract EtherType - this should not happen
    {
      TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
                 "WLAN TL:Could not extract EtherType"));
      //Packet is already freed
      return vosStatus; //returning failure
    }
  }
#endif /* FEATURE_WLAN_WAPI */

  /*----------------------------------------------------------------------
    Increment receive counter
    !! not sure this is the best place to increase this - pkt might be
    dropped below or delayed in TL's queues
    - will leave it here for now
   ------------------------------------------------------------------------*/
  pTLCb->atlSTAClients[ucSTAId].auRxCount[ucTid]++;

  /*------------------------------------------------------------------------
    Check if AMSDU and send for processing if so
   ------------------------------------------------------------------------*/
  ucAsf = (v_U8_t)WDA_GET_RX_ASF(aucBDHeader);

  if ( 0 != ucAsf )
  {
    TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
               "WLAN TL:Packet is AMSDU sub frame - sending for completion"));
    vosStatus = WLANTL_AMSDUProcess( pvosGCtx, &vosDataBuff, aucBDHeader, ucSTAId,
                         ucMPDUHLen, usMPDULen );
    if(NULL == vosDataBuff)
    {
       //Packet is already freed
       return VOS_STATUS_SUCCESS;
    }
  }
  /* After AMSDU header handled
   * AMSDU frame just same with normal frames */
    /*-------------------------------------------------------------------
      Translating header if necesary
       !! Fix me: rmv comments below
    ----------------------------------------------------------------------*/
  if (( 0 == WDA_GET_RX_FT_DONE(aucBDHeader) ) &&
      ( 0 != pTLCb->atlSTAClients[ucSTAId].wSTADesc.ucSwFrameRXXlation) &&
      ( WLANTL_IS_DATA_FRAME(WDA_GET_RX_TYPE_SUBTYPE(aucBDHeader)) ))
  {
    if(0 == ucMPDUHLen)
    {
      ucMPDUHLen = ucPMPDUHLen;
    }
    if (usMPDUDOffset > ucMPDUHOffset)
    {
      usActualHLen = usMPDUDOffset - ucMPDUHOffset;
    }
    vosStatus = WLANTL_Translate80211To8023Header( vosDataBuff, &vosStatus, usActualHLen, 
                        ucMPDUHLen, pTLCb, ucSTAId);

      if ( VOS_STATUS_SUCCESS != vosStatus )
      {
        TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
               "WLAN TL:Failed to translate from 802.11 to 802.3 - dropping"));
        /* Drop packet */
        vos_pkt_return_packet(vosDataBuff);
        return vosStatus;
      }
    }
    /* Softap requires additional Info such as Destination STAID and Access
       Category. Voschain or Buffer returned by BA would be unchain and this
       Meta Data would help in routing the packets to appropriate Destination */
#ifdef WLAN_SOFTAP_FEATURE
    if( WLAN_STA_SOFTAP == pTLCb->atlSTAClients[ucSTAId].wSTADesc.wSTAType)
    {
       STAMetaInfoPtr = (v_U8_t *)(ucTid | (WDA_GET_RX_ADDR3_IDX(aucBDHeader) << WLANTL_STAID_OFFSET));
       vos_pkt_set_user_data_ptr( vosDataBuff, VOS_PKT_USER_DATA_ID_TL,
                                 (v_PVOID_t)STAMetaInfoPtr);
    }
#endif

  /*------------------------------------------------------------------------
    Check to see if re-ordering session is in place
   ------------------------------------------------------------------------*/
  if ( 0 != pTLCb->atlSTAClients[ucSTAId].atlBAReorderInfo[ucTid].ucExists )
  {
    WLANTL_MSDUReorder( pTLCb, &vosDataBuff, aucBDHeader, ucSTAId, ucTid );
  }

#ifdef ANI_CHIPSET_VOLANS
if(0 == ucUnicastBroadcastType
#ifdef FEATURE_ON_CHIP_REORDERING
   && (WLANHAL_IsOnChipReorderingEnabledForTID(pvosGCtx, ucSTAId, ucTid) != TRUE)
#endif
)
{
  /* replay check code : check whether replay check is needed or not */
  if(VOS_TRUE == pTLCb->atlSTAClients[ucSTAId].ucIsReplayCheckValid)
  {
      /* replay check is needed for the station */

      /* check whether frame is AMSDU frame */
      if ( 0 != ucAsf )
      {
          /* Since virgo can't send AMSDU frames this leg of the code 
             was not tested properly, it needs to be tested properly*/
          /* Frame is AMSDU frame. As per 802.11n only first
             subframe will have replay counter */
          ucEsf =  WDA_GET_RX_ESF( aucBDHeader );
          if( 0 != ucEsf )
          {
              v_BOOL_t status;
              /* Getting 48-bit replay counter from the RX BD */
              ullcurrentReplayCounter = WDA_DS_GetReplayCounter(aucBDHeader);
 
              VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO,
               "WLAN TL: AMSDU currentReplayCounter [0x%llX]\n",ullcurrentReplayCounter);
              
              /* Getting 48-bit previous replay counter from TL control  block */
              ullpreviousReplayCounter = pTLCb->atlSTAClients[ucSTAId].ullReplayCounter[ucTid];

              VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO,
               "WLAN TL: AMSDU previousReplayCounter [0x%llX]\n",ullpreviousReplayCounter);

              /* It is first subframe of AMSDU thus it
                 conatains replay counter perform the
                 replay check for this first subframe*/
              status =  WLANTL_IsReplayPacket( ullcurrentReplayCounter, ullpreviousReplayCounter);
              if(VOS_FALSE == status)
              {
                   /* Not a replay paket, update previous replay counter in TL CB */    
                   pTLCb->atlSTAClients[ucSTAId].ullReplayCounter[ucTid] = ullcurrentReplayCounter;
              }
              else
              {
                  VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
                  "WLAN TL: AMSDU Drop the replay packet with PN : [0x%llX]\n",ullcurrentReplayCounter);

                  pTLCb->atlSTAClients[ucSTAId].ulTotalReplayPacketsDetected++;
                  VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
                  "WLAN TL: AMSDU total dropped replay packets on STA ID  %X is [0x%lX]\n",
                  ucSTAId,  pTLCb->atlSTAClients[ucSTAId].ulTotalReplayPacketsDetected);

                  /* Drop the packet */
                  vos_pkt_return_packet(vosDataBuff);
                  return VOS_STATUS_SUCCESS;
              }
          }
      }
      else
      {
           v_BOOL_t status;

           /* Getting 48-bit replay counter from the RX BD */
           ullcurrentReplayCounter = WDA_DS_GetReplayCounter(aucBDHeader);

           VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO,
             "WLAN TL: Non-AMSDU currentReplayCounter [0x%llX]\n",ullcurrentReplayCounter);

           /* Getting 48-bit previous replay counter from TL control  block */
           ullpreviousReplayCounter = pTLCb->atlSTAClients[ucSTAId].ullReplayCounter[ucTid];

           VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO,
               "WLAN TL: Non-AMSDU previousReplayCounter [0x%llX]\n",ullpreviousReplayCounter);

           /* It is not AMSDU frame so perform 
              reaply check for each packet, as
              each packet contains valid replay counter*/ 
           status =  WLANTL_IsReplayPacket( ullcurrentReplayCounter, ullpreviousReplayCounter);
           if(VOS_FALSE == status)
           {
                /* Not a replay paket, update previous replay counter in TL CB */    
                pTLCb->atlSTAClients[ucSTAId].ullReplayCounter[ucTid] = ullcurrentReplayCounter;
           }
           else
           {
              VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
               "WLAN TL: Non-AMSDU Drop the replay packet with PN : [0x%llX]\n",ullcurrentReplayCounter);

               pTLCb->atlSTAClients[ucSTAId].ulTotalReplayPacketsDetected++;
               VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
                "WLAN TL: Non-AMSDU total dropped replay packets on STA ID %X is [0x%lX]\n",
                ucSTAId, pTLCb->atlSTAClients[ucSTAId].ulTotalReplayPacketsDetected);

               /* Repaly packet, drop the packet */
               vos_pkt_return_packet(vosDataBuff);
               return VOS_STATUS_SUCCESS;
           }
      }
  }
}
/*It is a broadast packet DPU has already done replay check for 
  broadcast packets no need to do replay check of these packets*/
#endif /*End of #ifdef ANI_CHIPSET_VOLANS*/

  if ( NULL != vosDataBuff )
  {
#ifdef WLAN_SOFTAP_FEATURE
    if( WLAN_STA_SOFTAP == pTLCb->atlSTAClients[ucSTAId].wSTADesc.wSTAType)
    {
      WLANTL_FwdPktToHDD( pvosGCtx, vosDataBuff, ucSTAId );
    }
    else
#endif
    {
      wRxMetaInfo.ucUP = ucTid;
      pTLCb->atlSTAClients[ucSTAId].pfnSTARx( pvosGCtx, vosDataBuff, ucSTAId,
                                            &wRxMetaInfo );
    }
  }/* if not NULL */

  return VOS_STATUS_SUCCESS;
}/* WLANTL_STARxAuth */


/*==========================================================================
  FUNCTION    WLANTL_STARxDisc

  DESCRIPTION
    Receive in disconnected state - no data allowed

  DEPENDENCIES
    The STA must be registered with TL before this function can be called.

  PARAMETERS

   IN
   pvosGCtx:       pointer to the global vos context; a handle to TL's
                   control block can be extracted from its context
   ucSTAId:        identifier of the station being processed
   vosDataBuff:   pointer to the rx vos buffer

  RETURN VALUE
    The result code associated with performing the operation

    VOS_STATUS_SUCCESS:   Everything is good :)

  SIDE EFFECTS

============================================================================*/
VOS_STATUS
WLANTL_STARxDisc
(
  v_PVOID_t     pvosGCtx,
  v_U8_t        ucSTAId,
  vos_pkt_t**   pvosDataBuff
)
{
  /*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

  /*------------------------------------------------------------------------
    Sanity check
   ------------------------------------------------------------------------*/
  if (( NULL == pvosDataBuff ) || ( NULL ==  *pvosDataBuff ))
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
              "WLAN TL:Invalid parameter sent on WLANTL_STARxDisc"));
    return VOS_STATUS_E_INVAL;
  }

  /*------------------------------------------------------------------------
    Error - drop packet
   ------------------------------------------------------------------------*/
  TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
             "WLAN TL:Packet should not be received in state disconnected"
             " - dropping"));
  vos_pkt_return_packet(*pvosDataBuff);
  *pvosDataBuff = NULL;

  return VOS_STATUS_SUCCESS;
}/* WLANTL_STARxDisc */

/*==========================================================================
      Processing main loops for MAIN and TX threads
  ==========================================================================*/

/*==========================================================================
  FUNCTION    WLANTL_McProcessMsg

  DESCRIPTION
    Called by VOSS when a message was serialized for TL through the
    main thread/task.

  DEPENDENCIES
    The TL must be initialized before this function can be called.

  PARAMETERS

    IN
    pvosGCtx:       pointer to the global vos context; a handle to TL's
                    control block can be extracted from its context
    message:        type and content of the message


  RETURN VALUE
    The result code associated with performing the operation

    VOS_STATUS_E_INVAL:   invalid input parameters
    VOS_STATUS_E_FAULT:   pointer to TL cb is NULL ; access would cause a
                          page fault
    VOS_STATUS_SUCCESS:   Everything is good :)

  SIDE EFFECTS

============================================================================*/
VOS_STATUS
WLANTL_McProcessMsg
(
  v_PVOID_t        pvosGCtx,
  vos_msg_t*       message
)
{
   WLANTL_CbType*  pTLCb = NULL;
   tAddBAInd*      ptAddBaInd = NULL;
   tDelBAInd*      ptDelBaInd = NULL;
   tAddBARsp*      ptAddBaRsp = NULL;
   vos_msg_t       vosMessage;
   VOS_STATUS      vosStatus;
   tpFlushACRsp FlushACRspPtr;
  /*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

  /*------------------------------------------------------------------------
    Sanity check
   ------------------------------------------------------------------------*/
  if ( NULL == message )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
               "WLAN TL:Invalid parameter sent on WLANTL_ProcessMainMessage"));
    return VOS_STATUS_E_INVAL;
  }

  /*------------------------------------------------------------------------
    Extract TL control block
   ------------------------------------------------------------------------*/
  pTLCb = VOS_GET_TL_CB(pvosGCtx);
  if ( NULL == pTLCb )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
     "WLAN TL:Invalid TL pointer from pvosGCtx on WLANTL_ProcessMainMessage"));
    return VOS_STATUS_E_FAULT;
  }

  TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
        "WLAN TL:Received message: %d through main flow", message->type));

  switch( message->type )
  {
  case WDA_TL_FLUSH_AC_RSP:
    // Extract the message from the message body
    FlushACRspPtr = (tpFlushACRsp)(message->bodyptr);
    // Make sure the call back function is not null.
    VOS_ASSERT(pTLCb->tlBAPClient.pfnFlushOpCompleteCb != NULL);

    TLLOG2(VOS_TRACE(VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
            "Received message:  Flush complete received by TL"));

    // Since we have the response back from HAL, just call the BAP client
    // registered call back from TL. There is only 1 possible
    // BAP client. So directly reference tlBAPClient
    pTLCb->tlBAPClient.pfnFlushOpCompleteCb( pvosGCtx,
            FlushACRspPtr->ucSTAId,
            FlushACRspPtr->ucTid, FlushACRspPtr->status );

    // Free the PAL memory, we are done with it.
    TLLOG2(VOS_TRACE(VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
        "Flush complete received by TL: Freeing %p", FlushACRspPtr));
    vos_mem_free((v_VOID_t *)FlushACRspPtr);
    break;

  case WDA_HDD_ADDBA_REQ:
   ptAddBaInd = (tAddBAInd*)(message->bodyptr);
    vosStatus = WLANTL_BaSessionAdd( pvosGCtx,
                                 ptAddBaInd->baSession.baSessionID,
                                     ptAddBaInd->baSession.STAID,
                                     ptAddBaInd->baSession.baTID,
                                 (v_U32_t)ptAddBaInd->baSession.baBufferSize,
                                 ptAddBaInd->baSession.winSize,
                                 ptAddBaInd->baSession.SSN);
    ptAddBaRsp = vos_mem_malloc(sizeof(*ptAddBaRsp));

    if ( NULL == ptAddBaRsp )
    {
      TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
                "WLAN TL: fatal failure, cannot allocate BA Rsp structure"));
      VOS_ASSERT(0);
      return VOS_STATUS_E_NOMEM;
    }

    if ( VOS_STATUS_SUCCESS == vosStatus )
    {
      TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
                "WLAN TL: Sending success indication to HAL for ADD BA"));
      /*Send success*/
      ptAddBaRsp->mesgType    = WDA_HDD_ADDBA_RSP;
      vosMessage.type         = WDA_HDD_ADDBA_RSP;
    }
    else
    {
      TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
                 "WLAN TL: Sending failure indication to HAL for ADD BA"));

      /*Send failure*/
      ptAddBaRsp->mesgType    = WDA_BA_FAIL_IND;
      vosMessage.type         = WDA_BA_FAIL_IND;
    }

    ptAddBaRsp->mesgLen     = sizeof(tAddBARsp);
    ptAddBaRsp->baSessionID = ptAddBaInd->baSession.baSessionID;
      /* This is default, reply win size has to be handled BA module, FIX THIS */
      ptAddBaRsp->replyWinSize = WLANTL_MAX_WINSIZE;
    vosMessage.bodyptr = ptAddBaRsp;

    vos_mq_post_message( VOS_MQ_ID_WDA, &vosMessage );
    WLANTL_McFreeMsg (pvosGCtx, message);
  break;
  case WDA_DELETEBA_IND:
    ptDelBaInd = (tDelBAInd*)(message->bodyptr);
    vosStatus  = WLANTL_BaSessionDel(pvosGCtx,
                                 ptDelBaInd->staIdx,
                                 ptDelBaInd->baTID);

    if ( VOS_STATUS_SUCCESS != vosStatus )
    {
      TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
              "WLAN TL: Failed to del BA session STA:%d TID:%d Status :%d",
               ptDelBaInd->staIdx,
               ptDelBaInd->baTID,
               vosStatus));
    }
    WLANTL_McFreeMsg (pvosGCtx, message);
    break;
  default:
    /*no processing for now*/
    break;
  }

  return VOS_STATUS_SUCCESS;
}/* WLANTL_ProcessMainMessage */

/*==========================================================================
  FUNCTION    WLANTL_McFreeMsg

  DESCRIPTION
    Called by VOSS to free a given TL message on the Main thread when there
    are messages pending in the queue when the whole system is been reset.
    For now, TL does not allocate any body so this function shout translate
    into a NOOP

  DEPENDENCIES
    The TL must be initialized before this function can be called.

  PARAMETERS

    IN
    pvosGCtx:       pointer to the global vos context; a handle to TL's
                    control block can be extracted from its context
    message:        type and content of the message


  RETURN VALUE
    The result code associated with performing the operation

    VOS_STATUS_SUCCESS:   Everything is good :)

  SIDE EFFECTS

============================================================================*/
VOS_STATUS
WLANTL_McFreeMsg
(
  v_PVOID_t        pvosGCtx,
  vos_msg_t*       message
)
{
  WLANTL_CbType*  pTLCb = NULL;
  /*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

   /*------------------------------------------------------------------------
    Sanity check
   ------------------------------------------------------------------------*/
  if ( NULL == message )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
               "WLAN TL:Invalid parameter sent on WLANTL_McFreeMsg"));
    return VOS_STATUS_E_INVAL;
  }

  /*------------------------------------------------------------------------
    Extract TL control block
   ------------------------------------------------------------------------*/
  pTLCb = VOS_GET_TL_CB(pvosGCtx);
  if ( NULL == pTLCb )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
     "WLAN TL:Invalid TL pointer from pvosGCtx on WLANTL_McFreeMsg"));
    return VOS_STATUS_E_FAULT;
  }

  TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
        "WLAN TL:Received message: %d through main free", message->type));

  switch( message->type )
  {
  case WDA_HDD_ADDBA_REQ:
  case WDA_DELETEBA_IND:
    /*vos free body pointer*/
    vos_mem_free(message->bodyptr);
    message->bodyptr = NULL;
    break;
  default:
    /*no processing for now*/
    break;
  }

  return VOS_STATUS_SUCCESS;
}/*WLANTL_McFreeMsg*/

/*==========================================================================
  FUNCTION    WLANTL_TxProcessMsg

  DESCRIPTION
    Called by VOSS when a message was serialized for TL through the
    tx thread/task.

  DEPENDENCIES
    The TL must be initialized before this function can be called.

  PARAMETERS

    IN
    pvosGCtx:       pointer to the global vos context; a handle to TL's
                    control block can be extracted from its context
    message:        type and content of the message


  RETURN VALUE
    The result code associated with performing the operation

    VOS_STATUS_E_INVAL:   invalid input parameters
    VOS_STATUS_E_FAULT:   pointer to TL cb is NULL ; access would cause a
                          page fault
    VOS_STATUS_SUCCESS:   Everything is good :)

  Other values can be returned as a result of a function call, please check
  corresponding API for more info.
  SIDE EFFECTS

============================================================================*/
VOS_STATUS
WLANTL_TxProcessMsg
(
  v_PVOID_t        pvosGCtx,
  vos_msg_t*       message
)
{
   VOS_STATUS      vosStatus = VOS_STATUS_SUCCESS;
   v_U32_t         uData;
   v_U8_t          ucSTAId; 
   v_U8_t          ucUcastSig;
   v_U8_t          ucBcastSig;
#ifdef WLAN_SOFTAP_FEATURE
   WLANTL_CbType*  pTLCb = NULL;
   WLANTL_ACEnumType    ucAC;
#endif
#if defined( FEATURE_WLAN_INTEGRATED_SOC )
   void (*callbackRoutine) (void *callbackContext);
   void *callbackContext;
#endif
  /*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

  /*------------------------------------------------------------------------
    Sanity check
   ------------------------------------------------------------------------*/
  if ( NULL == message )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
               "WLAN TL:Invalid parameter sent on WLANTL_ProcessTxMessage"));
    return VOS_STATUS_E_INVAL;
  }

  /*------------------------------------------------------------------------
    Process message
   ------------------------------------------------------------------------*/
  TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
             "WLAN TL:Received message: %d through tx flow", message->type));

  switch( message->type )
  {
  case WLANTL_TX_SIG_SUSPEND:
    vosStatus = WLANTL_SuspendCB( pvosGCtx,
                                 (WLANTL_SuspendCBType)message->bodyptr,
                                 message->reserved);
    break;
  case WLANTL_TX_RES_NEEDED:
    vosStatus = WLANTL_GetTxResourcesCB( pvosGCtx );
     break;
  
  case WLANTL_TX_FWD_CACHED:
    /*---------------------------------------------------------------------
     The data sent with the message has the following structure: 
       | 00 | ucBcastSignature | ucUcastSignature | ucSTAID |
       each field above is one byte
    ---------------------------------------------------------------------*/
    uData       = (v_U32_t)message->bodyptr; 
    ucSTAId     = ( uData & 0x000000FF); 
    ucUcastSig  = ( uData & 0x0000FF00)>>8; 
    ucBcastSig  = (v_U8_t)(( uData & 0x00FF0000)>>16); 
    vosStatus   = WLANTL_ForwardSTAFrames( pvosGCtx, ucSTAId, 
                                           ucUcastSig, ucBcastSig);
    break;
#ifdef WLAN_SOFTAP_FEATURE
  case WLANTL_TX_STAID_AC_IND:
      pTLCb = VOS_GET_TL_CB(pvosGCtx);
      if ( NULL == pTLCb )
      {
         TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
              "WLAN TL:Invalid TL pointer from pvosGCtx on WLANTL_STAPktPending"));
         return VOS_STATUS_E_FAULT;
      }

      ucAC = message->bodyval &  WLANTL_AC_MASK;
      ucSTAId = (v_U8_t)message->bodyval >> WLANTL_STAID_OFFSET;  
      pTLCb->atlSTAClients[ucSTAId].aucACMask[ucAC] = 1; 

      vos_atomic_set_U8( &pTLCb->atlSTAClients[ucSTAId].ucPktPending, 1);
      vosStatus = WDA_DS_StartXmit(pvosGCtx);
      break;
#endif 
#if defined( FEATURE_WLAN_INTEGRATED_SOC )
  case WDA_DS_TX_START_XMIT:

    vosStatus = WDA_DS_TxFrames( pvosGCtx );

      break;

  case WDA_DS_FINISH_ULA:
    callbackContext = (void *)message->bodyval;
   
    callbackRoutine = message->bodyptr;
    callbackRoutine(callbackContext);
    break;
#endif

  default:
    /*no processing for now*/
    break;
  }

  return vosStatus;
}/* WLANTL_TxProcessMsg */

/*==========================================================================
  FUNCTION    WLANTL_McFreeMsg

  DESCRIPTION
    Called by VOSS to free a given TL message on the Main thread when there
    are messages pending in the queue when the whole system is been reset.
    For now, TL does not allocate any body so this function shout translate
    into a NOOP

  DEPENDENCIES
    The TL must be initialized before this function can be called.

  PARAMETERS

    IN
    pvosGCtx:       pointer to the global vos context; a handle to TL's
                    control block can be extracted from its context
    message:        type and content of the message


  RETURN VALUE
    The result code associated with performing the operation

    VOS_STATUS_SUCCESS:   Everything is good :)

  SIDE EFFECTS

============================================================================*/
VOS_STATUS
WLANTL_TxFreeMsg
(
  v_PVOID_t        pvosGCtx,
  vos_msg_t*       message
)
{
  /*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

  /*Nothing to do for now!!!*/
  return VOS_STATUS_SUCCESS;
}/*WLANTL_TxFreeMsg*/

#ifdef WLAN_SOFTAP_FEATURE
/*==========================================================================

  FUNCTION    WLANTL_TxFCFrame

  DESCRIPTION
    Internal utility function to send FC frame. Enable
    or disable LWM mode based on the information.

  DEPENDENCIES
    TL must be initiailized before this function gets called.
    FW sends up special flow control frame.

  PARAMETERS

    IN
    pvosGCtx:       pointer to the global vos context; a handle to TL's
                    control block can be extracted from its context

  RETURN VALUE
    The result code associated with performing the operation

    VOS_STATUS_E_INVAL:   Input pointers are NULL.
    VOS_STATUS_E_FAULT:   Something is wrong.
    VOS_STATUS_SUCCESS:   Everything is good.

  SIDE EFFECTS
    Newly formed FC frame is generated and waits to be transmitted. Previously unsent frame will
    be released.

============================================================================*/
VOS_STATUS
WLANTL_TxFCFrame
(
  v_PVOID_t       pvosGCtx
)
{
#if 0
  WLANTL_CbType*      pTLCb = NULL;
  VOS_STATUS          vosStatus;
  tpHalFcTxBd         pvFcTxBd = NULL;
  vos_pkt_t *         pPacket = NULL;
  v_U8_t              ucSTAId = 0;
  v_U8_t              ucBitCheck = 1;

  VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO,
               "WLAN TL: Send FC frame %s", __FUNCTION__);

  /*------------------------------------------------------------------------
    Sanity check
    ------------------------------------------------------------------------*/
  if ( NULL == pvosGCtx )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
               "WLAN TL:Invalid parameter %s", __FUNCTION__));
    return VOS_STATUS_E_INVAL;
  }
  /*------------------------------------------------------------------------
    Extract TL control block
   ------------------------------------------------------------------------*/
  pTLCb = VOS_GET_TL_CB(pvosGCtx);

  if (NULL == pTLCb)
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
        "WLAN TL:Invalid pointer in %s \n", __FUNCTION__));
    return VOS_STATUS_E_INVAL;
  }
  
  //Get one voss packet
  vosStatus = vos_pkt_get_packet( &pPacket, VOS_PKT_TYPE_TX_802_11_MGMT, sizeof(tHalFcTxBd), 1, 
                                    VOS_FALSE, NULL, NULL );

  if ( VOS_STATUS_SUCCESS != vosStatus )
  {
    return VOS_STATUS_E_INVAL;
  }

  vosStatus = vos_pkt_reserve_head( pPacket, (void *)&pvFcTxBd, sizeof(tHalFcTxBd));

  if( VOS_STATUS_SUCCESS != vosStatus )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
                 "%s: failed to reserve FC TX BD %d\n",__FUNCTION__, sizeof(tHalFcTxBd)));
    vos_pkt_return_packet( pPacket );
    return VOS_STATUS_E_FAULT;
  }

  //Generate most recent tlFCInfo. Most fields are correct.
  pTLCb->tlFCInfo.fcSTAThreshEnabledMask = 0;
  pTLCb->tlFCInfo.fcSTATxMoreDataMask = 0;
  for( ucSTAId = 0, ucBitCheck = 1 ; ucSTAId < WLAN_MAX_STA_COUNT; ucBitCheck <<= 1, ucSTAId ++)
  {
    if (0 == pTLCb->atlSTAClients[ucSTAId].ucExists)
    {
      continue;
    }

    if (pTLCb->atlSTAClients[ucSTAId].ucPktPending)
    {
      pTLCb->tlFCInfo.fcSTATxMoreDataMask |= ucBitCheck;
    }

    if ( (pTLCb->atlSTAClients[ucSTAId].ucLwmModeEnabled) &&
         (pTLCb->atlSTAClients[ucSTAId].bmuMemConsumed > pTLCb->atlSTAClients[ucSTAId].uLwmThreshold))
    {
      pTLCb->tlFCInfo.fcSTAThreshEnabledMask |= ucBitCheck;

      pTLCb->tlFCInfo.fcSTAThresh[ucSTAId] = (tANI_U8)pTLCb->atlSTAClients[ucSTAId].uLwmThreshold;

      pTLCb->atlSTAClients[ucSTAId].ucLwmEventReported = FALSE;
    }

  }
  
  //request immediate feedback
  pTLCb->tlFCInfo.fcConfig |= 0x4;                               

  //fill in BD to sent
  vosStatus = WLANHAL_FillFcTxBd(pvosGCtx, &pTLCb->tlFCInfo, (void *)pvFcTxBd);

  if( VOS_STATUS_SUCCESS != vosStatus )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
                 "%s: Fill FC TX BD unsuccessful\n", __FUNCTION__));
    vos_pkt_return_packet( pPacket );
    return VOS_STATUS_E_FAULT;
  }

  if (NULL != pTLCb->vosTxFCBuf)
  {
    TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
                 "%s: Previous FC TX BD not sent\n", __FUNCTION__));
    vos_pkt_return_packet(pTLCb->vosTxFCBuf);
  }

  pTLCb->vosTxFCBuf = pPacket;

  vos_pkt_set_user_data_ptr( pPacket, VOS_PKT_USER_DATA_ID_TL,
                               (v_PVOID_t)WLANTL_TxCompDefaultCb);
  vosStatus = WDA_DS_StartXmit(pvosGCtx);

  TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
               "WLAN TL: send FC frame leave %s", __FUNCTION__));
#endif
  return VOS_STATUS_SUCCESS;
}

#endif

/*==========================================================================
  FUNCTION    WLANTL_GetTxResourcesCB

  DESCRIPTION
    Processing function for Resource needed signal. A request will be issued
    to BAL to get more tx resources.

  DEPENDENCIES
    The TL must be initialized before this function can be called.

  PARAMETERS

    IN
    pvosGCtx:       pointer to the global vos context; a handle to TL's
                    control block can be extracted from its context


  RETURN VALUE
    The result code associated with performing the operation

    VOS_STATUS_E_FAULT:   pointer to TL cb is NULL ; access would cause a
                          page fault
    VOS_STATUS_SUCCESS:   Everything is good :)

  Other values can be returned as a result of a function call, please check
  corresponding API for more info.
  SIDE EFFECTS

============================================================================*/
VOS_STATUS
WLANTL_GetTxResourcesCB
(
  v_PVOID_t        pvosGCtx
)
{
  WLANTL_CbType*  pTLCb      = NULL;
  v_U32_t         uResCount  = WDA_TLI_MIN_RES_DATA;
  VOS_STATUS      vosStatus  = VOS_STATUS_SUCCESS;
  v_U8_t          ucMgmt     = 0;
  v_U8_t          ucBAP      = 0;
  v_U8_t          ucData     = 0;
#ifdef WLAN_SOFTAP_FEATURE
#ifdef WLAN_SOFTAP_FLOWCTRL_EN
  tBssSystemRole systemRole;
  tpAniSirGlobal pMac;
#endif
#endif
  /*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/
  /*------------------------------------------------------------------------
    Extract TL control block
   ------------------------------------------------------------------------*/
  pTLCb = VOS_GET_TL_CB(pvosGCtx);
  if ( NULL == pTLCb )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
               "WLAN TL:Invalid TL pointer from pvosGCtx on"
               " WLANTL_ProcessTxMessage"));
    return VOS_STATUS_E_FAULT;
  }

  /*------------------------------------------------------------------------
    Get tx resources from BAL
   ------------------------------------------------------------------------*/
  vosStatus = WDA_DS_GetTxResources( pvosGCtx, &uResCount );

  if ( (VOS_STATUS_SUCCESS != vosStatus) && (VOS_STATUS_E_RESOURCES != vosStatus))
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
               "WLAN TL:TL failed to get resources from BAL, Err: %d",
               vosStatus));
    return vosStatus;
  }

  /* Currently only Linux BAL returns the E_RESOURCES error code when it is running 
     out of BD/PDUs. To make use of this interrupt for throughput enhancement, similar
     changes should be done in BAL code of AMSS and WM */
  if (VOS_STATUS_E_RESOURCES == vosStatus)
  {
#ifdef VOLANS_PERF
     WLANHAL_EnableIdleBdPduInterrupt(pvosGCtx, (tANI_U8)bdPduInterruptGetThreshold);
     VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
               "WLAN TL: Enabling Idle BD/PDU interrupt, Current resources = %d", uResCount);
#else
    return VOS_STATUS_E_FAILURE;
#endif
  }

  pTLCb->uResCount = uResCount;
  

#ifdef WLAN_SOFTAP_FEATURE
#ifdef WLAN_SOFTAP_FLOWCTRL_EN
  /* FIXME: disabled since creating issues in power-save, needs to be addressed */ 
  pTLCb->sendFCFrame ++;
  pMac = vos_get_context(VOS_MODULE_ID_WDA, pvosGCtx);
  systemRole = wdaGetGlobalSystemRole(pMac);
  if (eSYSTEM_AP_ROLE == systemRole)
  {
     if (pTLCb->sendFCFrame % 16 == 0)
     {
         TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
                "Transmit FC"));
         WLANTL_TxFCFrame (pvosGCtx);
     } 
  }
#endif //WLAN_SOFTAP_FLOWCTRL_EN 
#endif //WLAN_SOFTAP_FEATURE

  ucData = ( pTLCb->uResCount >=  WDA_TLI_MIN_RES_DATA );
  ucBAP  = ( pTLCb->uResCount >=  WDA_TLI_MIN_RES_BAP ) &&
           ( NULL != pTLCb->tlBAPClient.vosPendingDataBuff );
  ucMgmt = ( pTLCb->uResCount >=  WDA_TLI_MIN_RES_MF ) &&
           ( NULL != pTLCb->tlMgmtFrmClient.vosPendingDataBuff );

  TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
             "WLAN TL: Eval Resume tx Res: %d DATA: %d BAP: %d MGMT: %d",
             pTLCb->uResCount, ucData, ucBAP, ucMgmt));

  if (( 0 == pTLCb->ucTxSuspended ) &&
      (( 0 != ucData ) || ( 0 != ucMgmt ) || ( 0 != ucBAP ) ) )
  {
    TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
                 "Issuing Xmit start request to BAL for avail res SYNC"));
    vosStatus =WDA_DS_StartXmit(pvosGCtx);
  }
  return vosStatus;
}/*WLANTL_GetTxResourcesCB*/

/*==========================================================================
      Utility functions
  ==========================================================================*/

/*==========================================================================
  FUNCTION    WLANTL_Translate8023To80211Header

  DESCRIPTION
    Inline function for translating and 802.11 header into an 802.3 header.

  DEPENDENCIES


  PARAMETERS

   IN
    pTLCb:            TL control block
    ucStaId:          station ID

   IN/OUT
    vosDataBuff:      vos data buffer, will contain the new header on output

   OUT
    pvosStatus:       status of the operation

  RETURN VALUE

    VOS_STATUS_SUCCESS:  Everything is good :)

    Other error codes might be returned from the vos api used in the function
    please check those return values.

  SIDE EFFECTS

============================================================================*/
VOS_STATUS
WLANTL_Translate8023To80211Header
(
  vos_pkt_t*      vosDataBuff,
  VOS_STATUS*     pvosStatus,
  WLANTL_CbType*  pTLCb,
  v_U8_t          ucStaId,
  v_U8_t          ucUP,
  v_U8_t          *ucWDSEnabled,
  v_U8_t          *extraHeadSpace
)
{
  WLANTL_8023HeaderType  w8023Header;
  WLANTL_80211HeaderType *pw80211Header; // Allocate an aligned BD and then fill it. 
  VOS_STATUS             vosStatus;
  v_U8_t                 MandatoryucHeaderSize = WLAN80211_MANDATORY_HEADER_SIZE;
  v_U8_t                 ucHeaderSize = 0;
  v_VOID_t               *ppvBDHeader = NULL;
#ifdef WLAN_SOFTAP_FEATURE
  v_U8_t                 ucQoSOffset = WLAN80211_MANDATORY_HEADER_SIZE;
#endif

  *ucWDSEnabled = 0; // default WDS off.
  vosStatus = vos_pkt_pop_head( vosDataBuff, &w8023Header,
                                sizeof(w8023Header));

  if ( VOS_STATUS_SUCCESS != vosStatus )
  {
     TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
     "WLAN TL: Packet pop header fails on WLANTL_Translate8023To80211Header"));
     return vosStatus;
  }


  if ( 0 != pTLCb->atlSTAClients[ucStaId].wSTADesc.ucAddRmvLLC )
  {
    /* Push the length */
    vosStatus = vos_pkt_push_head(vosDataBuff,
                    &w8023Header.usLenType, sizeof(w8023Header.usLenType));

    if ( VOS_STATUS_SUCCESS != vosStatus )
    {
       TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
                  "WLAN TL: Packet push ether type fails on"
                  " WLANTL_Translate8023To80211Header"));
       return vosStatus;
    }

#ifdef BTAMP_TEST
    // The STA side will execute this, a hack to test BTAMP by using the
    // infra setup. On real BTAMP this will come from BAP itself.
    {
    static v_U8_t WLANTL_BT_AMP_LLC_HEADER[] =  {0xAA, 0xAA, 0x03, 0x00, 0x19, 0x58 };
    vosStatus = vos_pkt_push_head(vosDataBuff, WLANTL_BT_AMP_LLC_HEADER,
                       sizeof(WLANTL_BT_AMP_LLC_HEADER));

    if ( VOS_STATUS_SUCCESS != vosStatus )
    {
       TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
                 "WLAN TL: Packet push LLC header fails on"
                  " WLANTL_Translate8023To80211Header"));
       return vosStatus;
    }
    }
#else
    vosStatus = vos_pkt_push_head(vosDataBuff, WLANTL_LLC_HEADER,
                       sizeof(WLANTL_LLC_HEADER));

    if ( VOS_STATUS_SUCCESS != vosStatus )
    {
       TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
                 "WLAN TL: Packet push LLC header fails on"
                  " WLANTL_Translate8023To80211Header"));
       return vosStatus;
    }
#endif
  }/*If add LLC is enabled*/
  else
  {
       TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
                 "WLAN TL: STA Client registered to not remove LLC"
                  " WLANTL_Translate8023To80211Header"));
  }

#ifdef BTAMP_TEST
  pTLCb->atlSTAClients[ucStaId].wSTADesc.wSTAType = WLAN_STA_BT_AMP;
#endif

  // Find the space required for the 802.11 header format
  // based on the frame control fields.
  ucHeaderSize = MandatoryucHeaderSize;
  if (pTLCb->atlSTAClients[ucStaId].wSTADesc.ucQosEnabled)
  {  
    ucHeaderSize += sizeof(pw80211Header->usQosCtrl);
  }
  if (pTLCb->atlSTAClients[ucStaId].wSTADesc.wSTAType == WLAN_STA_BT_AMP)
  {  
    ucHeaderSize += sizeof(pw80211Header->optvA4);
#ifdef WLAN_SOFTAP_FEATURE
    ucQoSOffset += sizeof(pw80211Header->optvA4);
#endif
  }

  TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
      " WLANTL_Translate8023To80211Header : Header size = %d ", ucHeaderSize));

  vos_pkt_reserve_head( vosDataBuff, &ppvBDHeader, ucHeaderSize );
  if ( NULL == ppvBDHeader )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
                "WLAN TL:VOSS packet corrupted "));
    *pvosStatus = VOS_STATUS_E_INVAL;
    return *pvosStatus;
  }

#ifndef FEATURE_WLAN_INTEGRATED_SOC
  /* Check for alignment */
  *extraHeadSpace = (v_U8_t)((((v_U32_t)(ppvBDHeader)) & 0x7) % 0x4);
  if (*extraHeadSpace)
  {

    TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
                "WLAN TL:VOSS reserving extra header space = %d", *extraHeadSpace));
    vos_pkt_reserve_head( vosDataBuff, &ppvBDHeader, *extraHeadSpace );
    if ( NULL == ppvBDHeader )
    {
      TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
                  "WLAN TL:VOSS packet corrupted on Attach BD header while reserving extra header space"));
      *pvosStatus = VOS_STATUS_E_INVAL;
      return *pvosStatus;
    }
  }
#endif /* FEATURE_WLAN_INTEGRATED_SOC */

  // OK now we have the space. Fill the 80211 header
  /* Fill A2 */
  pw80211Header = (WLANTL_80211HeaderType *)(ppvBDHeader);
  // only clear the required space.
  vos_mem_set( pw80211Header, ucHeaderSize, 0 );
  vos_mem_copy( pw80211Header->vA2, w8023Header.vSA,  VOS_MAC_ADDR_SIZE);


#ifdef FEATURE_WLAN_WAPI
  if ( WLANTL_STA_AUTHENTICATED == pTLCb->atlSTAClients[ucStaId].tlState && gUcIsWai != 1 )
#else
  if ( WLANTL_STA_AUTHENTICATED == pTLCb->atlSTAClients[ucStaId].tlState )
#endif
  {
    pw80211Header->wFrmCtrl.wep =
                 pTLCb->atlSTAClients[ucStaId].wSTADesc.ucProtectedFrame;
  }

  pw80211Header->usDurationId = 0;
  pw80211Header->usSeqCtrl    = 0;

  pw80211Header->wFrmCtrl.type     = WLANTL_80211_DATA_TYPE;



  if(pTLCb->atlSTAClients[ucStaId].wSTADesc.ucQosEnabled)
  {
      pw80211Header->wFrmCtrl.subType  = WLANTL_80211_DATA_QOS_SUBTYPE;

#ifdef WLAN_SOFTAP_FEATURE
      *((v_U16_t *)((v_U8_t *)ppvBDHeader + ucQoSOffset)) = ucUP;
#else
      pw80211Header->usQosCtrl = ucUP;
#endif

  }
  else
  {
      pw80211Header->wFrmCtrl.subType  = 0;

  // NO NO NO - there is not enough memory allocated to write the QOS ctrl  
  // field, it will overwrite the first 2 bytes of the data packet(LLC header)
  // pw80211Header->usQosCtrl         = 0;
  }


  switch( pTLCb->atlSTAClients[ucStaId].wSTADesc.wSTAType )
  {
      case WLAN_STA_IBSS:
        pw80211Header->wFrmCtrl.toDS          = 0;
        pw80211Header->wFrmCtrl.fromDS        = 0;
        /*Fix me*/
        vos_copy_macaddr( (v_MACADDR_t*)&pw80211Header->vA1,
              &pTLCb->atlSTAClients[ucStaId].wSTADesc.vSTAMACAddress);
        vos_mem_copy( pw80211Header->vA3,
              &pTLCb->atlSTAClients[ucStaId].wSTADesc.vBSSIDforIBSS ,
              VOS_MAC_ADDR_SIZE);
        break;

      case WLAN_STA_BT_AMP:
        *ucWDSEnabled = 1; // WDS on.
        pw80211Header->wFrmCtrl.toDS          = 1;
        pw80211Header->wFrmCtrl.fromDS        = 1;
        vos_copy_macaddr( (v_MACADDR_t*)&pw80211Header->vA1,
              &pTLCb->atlSTAClients[ucStaId].wSTADesc.vSTAMACAddress);
        vos_mem_copy( pw80211Header->vA2,
                w8023Header.vSA, VOS_MAC_ADDR_SIZE);
        vos_copy_macaddr( (v_MACADDR_t*)&pw80211Header->vA3,
              &pTLCb->atlSTAClients[ucStaId].wSTADesc.vSTAMACAddress);
        /* fill the optional A4 header */
        vos_mem_copy( pw80211Header->optvA4,
                w8023Header.vSA, VOS_MAC_ADDR_SIZE);
        TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
            "BTAMP CASE NOW ---------staid=%d\n",
            ucStaId));
        break;

#ifdef WLAN_SOFTAP_FEATURE
      case WLAN_STA_SOFTAP:
        *ucWDSEnabled = 0; // WDS off.
        pw80211Header->wFrmCtrl.toDS          = 0;
        pw80211Header->wFrmCtrl.fromDS        = 1;
        /*Copy the DA to A1*/
        vos_mem_copy( pw80211Header->vA1, w8023Header.vDA , VOS_MAC_ADDR_SIZE);   
        vos_copy_macaddr( (v_MACADDR_t*)&pw80211Header->vA2,
              &pTLCb->atlSTAClients[ucStaId].wSTADesc.vSelfMACAddress);
        vos_mem_copy( pw80211Header->vA3,
                w8023Header.vSA, VOS_MAC_ADDR_SIZE);
        TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
            "sw 802 to 80211 softap case  ---------staid=%d\n",
            ucStaId));
        break;
#endif

      case WLAN_STA_INFRA:
      default:
        pw80211Header->wFrmCtrl.toDS          = 1;
        pw80211Header->wFrmCtrl.fromDS        = 0;
        vos_copy_macaddr( (v_MACADDR_t*)&pw80211Header->vA1,
              &pTLCb->atlSTAClients[ucStaId].wSTADesc.vSTAMACAddress);
        vos_mem_copy( pw80211Header->vA3, w8023Header.vDA , VOS_MAC_ADDR_SIZE);
        TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
            "REGULAR INFRA LINK CASE---------staid=%d\n",
            ucStaId));
        break;
  }
  // OK now we have the space. Fill the 80211 header
  /* Fill A2 */
  pw80211Header = (WLANTL_80211HeaderType *)(ppvBDHeader);
  return VOS_STATUS_SUCCESS;
}/*WLANTL_Translate8023To80211Header*/


/*=============================================================================
   BEGIN LOG FUNCTION    !!! Remove me or clean me
=============================================================================*/
#ifdef WLANTL_DEBUG 

#define WLANTL_DEBUG_FRAME_BYTE_PER_LINE    16
#define WLANTL_DEBUG_FRAME_BYTE_PER_BYTE    4

static v_VOID_t WLANTL_DebugFrame
(
   v_PVOID_t   dataPointer,
   v_U32_t     dataSize
)
{
   v_U8_t   lineBuffer[WLANTL_DEBUG_FRAME_BYTE_PER_LINE];
   v_U32_t  numLines;
   v_U32_t  numBytes;
   v_U32_t  idx;
   v_U8_t   *linePointer;

   numLines = dataSize / WLANTL_DEBUG_FRAME_BYTE_PER_LINE;
   numBytes = dataSize % WLANTL_DEBUG_FRAME_BYTE_PER_LINE;
   linePointer = (v_U8_t *)dataPointer;

   TLLOGE(VOS_TRACE(VOS_MODULE_ID_SAL, VOS_TRACE_LEVEL_ERROR, "WLAN TL:Frame Debug Frame Size %d, Pointer 0x%p", dataSize, dataPointer));
   for(idx = 0; idx < numLines; idx++)
   {
      memset(lineBuffer, 0, WLANTL_DEBUG_FRAME_BYTE_PER_LINE);
      memcpy(lineBuffer, linePointer, WLANTL_DEBUG_FRAME_BYTE_PER_LINE);
      TLLOGE(VOS_TRACE(VOS_MODULE_ID_SAL, VOS_TRACE_LEVEL_ERROR,
                "WLAN TL:0x%2x 0x%2x 0x%2x 0x%2x 0x%2x 0x%2x 0x%2x 0x%2x 0x%2x 0x%2x 0x%2x 0x%2x 0x%2x 0x%2x 0x%2x 0x%2x",
                 lineBuffer[0], lineBuffer[1], lineBuffer[2], lineBuffer[3], lineBuffer[4], lineBuffer[5], lineBuffer[6], lineBuffer[7],
                 lineBuffer[8], lineBuffer[9], lineBuffer[10], lineBuffer[11], lineBuffer[12], lineBuffer[13], lineBuffer[14], lineBuffer[15]));
      linePointer += WLANTL_DEBUG_FRAME_BYTE_PER_LINE;
   }

   if(0 == numBytes)
      return;

   memset(lineBuffer, 0, WLANTL_DEBUG_FRAME_BYTE_PER_LINE);
   memcpy(lineBuffer, linePointer, numBytes);
   for(idx = 0; idx < WLANTL_DEBUG_FRAME_BYTE_PER_LINE / WLANTL_DEBUG_FRAME_BYTE_PER_BYTE; idx++)
   {
      TLLOGE(VOS_TRACE(VOS_MODULE_ID_SAL, VOS_TRACE_LEVEL_ERROR, "WLAN TL:0x%2x 0x%2x 0x%2x 0x%2x",
                lineBuffer[idx * WLANTL_DEBUG_FRAME_BYTE_PER_BYTE], lineBuffer[1 + idx * WLANTL_DEBUG_FRAME_BYTE_PER_BYTE],
                lineBuffer[2 + idx * WLANTL_DEBUG_FRAME_BYTE_PER_BYTE], lineBuffer[3 + idx * WLANTL_DEBUG_FRAME_BYTE_PER_BYTE]));
      if(((idx + 1) * WLANTL_DEBUG_FRAME_BYTE_PER_BYTE) >= numBytes)
         break;
   }

   return;
}
#endif

/*=============================================================================
   END LOG FUNCTION
=============================================================================*/

/*==========================================================================
  FUNCTION    WLANTL_Translate80211To8023Header

  DESCRIPTION
    Inline function for translating and 802.11 header into an 802.3 header.

  DEPENDENCIES


  PARAMETERS

   IN
    pTLCb:            TL control block
    ucStaId:          station ID
    ucHeaderLen:      Length of the header from BD
    ucActualHLen:     Length of header including padding or any other trailers

   IN/OUT
    vosDataBuff:      vos data buffer, will contain the new header on output

   OUT
    pvosStatus:       status of the operation

   RETURN VALUE

    The result code associated with performing the operation
    VOS_STATUS_SUCCESS:  Everything is good :)

  SIDE EFFECTS

============================================================================*/
VOS_STATUS
WLANTL_Translate80211To8023Header
(
  vos_pkt_t*      vosDataBuff,
  VOS_STATUS*     pvosStatus,
  v_U16_t         usActualHLen,
  v_U8_t          ucHeaderLen,
  WLANTL_CbType*  pTLCb,
  v_U8_t          ucSTAId
)
{
  WLANTL_8023HeaderType  w8023Header;
  WLANTL_80211HeaderType w80211Header;
  v_U8_t                 aucLLCHeader[WLANTL_LLC_HEADER_LEN];
  VOS_STATUS             vosStatus;
  v_U16_t                usDataStartOffset = 0;
  /*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

  if ( sizeof(w80211Header) < ucHeaderLen )
  {
     TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
       "Warning !: Check the header size for the Rx frame structure=%d received=%dn",
       sizeof(w80211Header), ucHeaderLen));
     ucHeaderLen = sizeof(w80211Header);
  }

  // This will take care of headers of all sizes, 3 address, 3 addr QOS,
  // WDS non-QOS and WDS QoS etc. We have space for all in the 802.11 header structure.
  vosStatus = vos_pkt_pop_head( vosDataBuff, &w80211Header, ucHeaderLen);

  if ( VOS_STATUS_SUCCESS != vosStatus )
  {
     TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
                "WLAN TL: Failed to pop 80211 header from packet %d",
                vosStatus));

     return vosStatus;
  }

  switch ( w80211Header.wFrmCtrl.fromDS )
  {
  case 0:
#ifdef WLAN_SOFTAP_FEATURE
    if ( w80211Header.wFrmCtrl.toDS )
    {
      //SoftAP AP mode
      vos_mem_copy( w8023Header.vDA, w80211Header.vA3, VOS_MAC_ADDR_SIZE);
      vos_mem_copy( w8023Header.vSA, w80211Header.vA2, VOS_MAC_ADDR_SIZE);
      TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
                  "WLAN TL SoftAP: 802 3 DA %08x SA %08x \n",
                  w8023Header.vDA, w8023Header.vSA));
    }
    else 
    {
      /* IBSS */
      vos_mem_copy( w8023Header.vDA, w80211Header.vA1, VOS_MAC_ADDR_SIZE);
      vos_mem_copy( w8023Header.vSA, w80211Header.vA2, VOS_MAC_ADDR_SIZE);
    }
#else
    /* IBSS */
    vos_mem_copy( w8023Header.vDA, w80211Header.vA1, VOS_MAC_ADDR_SIZE);
    vos_mem_copy( w8023Header.vSA, w80211Header.vA2, VOS_MAC_ADDR_SIZE);
#endif
    break;
  case 1:
    if ( w80211Header.wFrmCtrl.toDS )
    {
      /* BT-AMP case */
      vos_mem_copy( w8023Header.vDA, w80211Header.vA1, VOS_MAC_ADDR_SIZE);
      vos_mem_copy( w8023Header.vSA, w80211Header.vA2, VOS_MAC_ADDR_SIZE);
    }
    else
    { /* Infra */
      vos_mem_copy( w8023Header.vDA, w80211Header.vA1, VOS_MAC_ADDR_SIZE);
      vos_mem_copy( w8023Header.vSA, w80211Header.vA3, VOS_MAC_ADDR_SIZE);
    }
    break;
  }

  if( usActualHLen > ucHeaderLen )
  {
     usDataStartOffset = usActualHLen - ucHeaderLen;
  }

  if ( 0 < usDataStartOffset )
  {
    vosStatus = vos_pkt_trim_head( vosDataBuff, usDataStartOffset );

    if ( VOS_STATUS_SUCCESS != vosStatus )
    {
      TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
                  "WLAN TL: Failed to trim header from packet %d",
                  vosStatus));
      return vosStatus;
    }
  }

  if ( 0 != pTLCb->atlSTAClients[ucSTAId].wSTADesc.ucAddRmvLLC )
  {
    // Extract the LLC header
    vosStatus = vos_pkt_pop_head( vosDataBuff, aucLLCHeader,
                                  WLANTL_LLC_HEADER_LEN);

    if ( VOS_STATUS_SUCCESS != vosStatus )
    {
       TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
                  "WLAN TL: Failed to pop LLC header from packet %d",
                  vosStatus));

       return vosStatus;
    }

    //Extract the length
    vos_mem_copy(&w8023Header.usLenType,
      &aucLLCHeader[WLANTL_LLC_HEADER_LEN - sizeof(w8023Header.usLenType)],
      sizeof(w8023Header.usLenType) );
  }
  else
  {
    vosStatus = vos_pkt_get_packet_length(vosDataBuff,
                                        &w8023Header.usLenType);

    if ( VOS_STATUS_SUCCESS != vosStatus )
    {
       TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
                  "WLAN TL: Failed to get packet length %d",
                  vosStatus));

       return vosStatus;
    }

    TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
        "WLAN TL: BTAMP len (ethertype) fld = %d",
        w8023Header.usLenType));
    w8023Header.usLenType = vos_cpu_to_be16(w8023Header.usLenType);
  }

  vos_pkt_push_head(vosDataBuff, &w8023Header, sizeof(w8023Header));

#ifdef BTAMP_TEST
  {
  // AP side will execute this.
  v_U8_t *temp_w8023Header = NULL;
  vosStatus = vos_pkt_peek_data( vosDataBuff, 0,
    &temp_w8023Header, sizeof(w8023Header) );
  }
#endif
#if 0 /*TL_DEBUG*/
  vos_pkt_get_packet_length(vosDataBuff, &usLen);
  vos_pkt_pop_head( vosDataBuff, aucData, usLen);

  WLANTL_DebugFrame(aucData, usLen);

  vos_pkt_push_head(vosDataBuff, aucData, usLen);

#endif

  *pvosStatus = VOS_STATUS_SUCCESS;

  return VOS_STATUS_SUCCESS;
}/*WLANTL_Translate80211To8023Header*/

#if 0
#ifdef WLAN_PERF 
/*==========================================================================
  FUNCTION    WLANTL_FastHwFwdDataFrame

  DESCRIPTION 
    Fast path function to quickly forward a data frame if HAL determines BD 
    signature computed here matches the signature inside current VOSS packet. 
    If there is a match, HAL and TL fills in the swapped packet length into 
    BD header and DxE header, respectively. Otherwise, packet goes back to 
    normal (slow) path and a new BD signature would be tagged into BD in this
    VOSS packet later by the WLANHAL_FillTxBd() function.

  DEPENDENCIES 
     
  PARAMETERS 

   IN
        pvosGCtx    VOS context
        vosDataBuff Ptr to VOSS packet
        pMetaInfo   For getting frame's TID
        pStaInfo    For checking STA type
    
   OUT
        pvosStatus  returned status
        puFastFwdOK Flag to indicate whether frame could be fast forwarded
   
  RETURN VALUE
    No return.   

  SIDE EFFECTS 
  
============================================================================*/
static void
WLANTL_FastHwFwdDataFrame
( 
  v_PVOID_t     pvosGCtx,
  vos_pkt_t*    vosDataBuff,
  VOS_STATUS*   pvosStatus,
  v_U32_t*       puFastFwdOK,
  WLANTL_MetaInfoType*  pMetaInfo,
  WLAN_STADescType*  pStaInfo
 
)
{
    v_PVOID_t   pvPeekData;
    v_U8_t      ucDxEBDWLANHeaderLen = WLANTL_BD_HEADER_LEN(0) + sizeof(WLANBAL_sDXEHeaderType); 
    v_U8_t      ucIsUnicast;
    WLANBAL_sDXEHeaderType  *pDxEHeader;
    v_PVOID_t   pvBDHeader;
    v_PVOID_t   pucBuffPtr;
    v_U16_t      usPktLen;

   /*-----------------------------------------------------------------------
    Extract packet length
    -----------------------------------------------------------------------*/

    vos_pkt_get_packet_length( vosDataBuff, &usPktLen);

   /*-----------------------------------------------------------------------
    Extract MAC address
    -----------------------------------------------------------------------*/
    *pvosStatus = vos_pkt_peek_data( vosDataBuff, 
                                 WLANTL_MAC_ADDR_ALIGN(0), 
                                 (v_PVOID_t)&pvPeekData, 
                                 VOS_MAC_ADDR_SIZE );

    if ( VOS_STATUS_SUCCESS != *pvosStatus ) 
    {
       TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
                  "WLAN TL:Failed while attempting to extract MAC Addr %d", 
                  *pvosStatus));
       *pvosStatus = VOS_STATUS_E_INVAL;
       return;
    }

   /*-----------------------------------------------------------------------
    Reserve head room for DxE header, BD, and WLAN header
    -----------------------------------------------------------------------*/

    vos_pkt_reserve_head( vosDataBuff, &pucBuffPtr, 
                        ucDxEBDWLANHeaderLen );
    if ( NULL == pucBuffPtr )
    {
        TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
                    "WLAN TL:No enough space in VOSS packet %p for DxE/BD/WLAN header", vosDataBuff));
       *pvosStatus = VOS_STATUS_E_INVAL;
        return;
    }
    pDxEHeader = (WLANBAL_sDXEHeaderType  *)pucBuffPtr;
    pvBDHeader = (v_PVOID_t) &pDxEHeader[1];

    /* UMA Tx acceleration is enabled. 
     * UMA would help convert frames to 802.11, fill partial BD fields and 
     * construct LLC header. To further accelerate this kind of frames,
     * HAL would attempt to reuse the BD descriptor if the BD signature 
     * matches to the saved BD descriptor.
     */
     if(pStaInfo->wSTAType == WLAN_STA_IBSS)
        ucIsUnicast = !(((tANI_U8 *)pvPeekData)[0] & 0x01);
     else
        ucIsUnicast = 1;
 
     *puFastFwdOK = (v_U32_t) WLANHAL_TxBdFastFwd(pvosGCtx, pvPeekData, pMetaInfo->ucTID, ucIsUnicast, pvBDHeader, usPktLen );
    
      /* Can't be fast forwarded. Trim the VOS head back to original location. */
      if(! *puFastFwdOK){
          vos_pkt_trim_head(vosDataBuff, ucDxEBDWLANHeaderLen);
      }else{
        /* could be fast forwarded. Now notify BAL DxE header filling could be completely skipped
         */
        v_U32_t uPacketSize = WLANTL_BD_HEADER_LEN(0) + usPktLen;
        vos_pkt_set_user_data_ptr( vosDataBuff, VOS_PKT_USER_DATA_ID_BAL, 
                       (v_PVOID_t)uPacketSize);
        pDxEHeader->size  = SWAP_ENDIAN_UINT32(uPacketSize);
      }
     *pvosStatus = VOS_STATUS_SUCCESS;
      return;
}
#endif /*WLAN_PERF*/
#endif

#if 0
/*==========================================================================
   FUNCTION    WLANTL_PrepareBDHeader

  DESCRIPTION
    Inline function for preparing BD header before HAL processing.

  DEPENDENCIES
    Just notify HAL that suspend in TL is complete.

  PARAMETERS

   IN
    vosDataBuff:      vos data buffer
    ucDisableFrmXtl:  is frame xtl disabled

   OUT
    ppvBDHeader:      it will contain the BD header
    pvDestMacAdddr:   it will contain the destination MAC address
    pvosStatus:       status of the combined processing
    pusPktLen:        packet len.

  RETURN VALUE
    No return.

  SIDE EFFECTS

============================================================================*/
void
WLANTL_PrepareBDHeader
(
  vos_pkt_t*      vosDataBuff,
  v_PVOID_t*      ppvBDHeader,
  v_MACADDR_t*    pvDestMacAdddr,
  v_U8_t          ucDisableFrmXtl,
  VOS_STATUS*     pvosStatus,
  v_U16_t*        pusPktLen,
  v_U8_t          ucQosEnabled,
  v_U8_t          ucWDSEnabled,
  v_U8_t          extraHeadSpace
)
{
  v_U8_t      ucHeaderOffset;
  v_U8_t      ucHeaderLen;
#ifndef WLAN_SOFTAP_FEATURE
  v_PVOID_t   pvPeekData;
#endif
  v_U8_t      ucBDHeaderLen = WLANTL_BD_HEADER_LEN(ucDisableFrmXtl);

  /*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/
  /*-------------------------------------------------------------------------
    Get header pointer from VOSS
    !!! make sure reserve head zeros out the memory
   -------------------------------------------------------------------------*/
  vos_pkt_get_packet_length( vosDataBuff, pusPktLen);

  if ( WLANTL_MAC_HEADER_LEN(ucDisableFrmXtl) > *pusPktLen )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
               "WLAN TL: Length of the packet smaller than expected network"
               " header %d", *pusPktLen ));

    *pvosStatus = VOS_STATUS_E_INVAL;
    return;
  }

  vos_pkt_reserve_head( vosDataBuff, ppvBDHeader,
                        ucBDHeaderLen );
  if ( NULL == *ppvBDHeader )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
                "WLAN TL:VOSS packet corrupted on Attach BD header"));
    *pvosStatus = VOS_STATUS_E_INVAL;
    return;
  }

  /*-----------------------------------------------------------------------
    Extract MAC address
   -----------------------------------------------------------------------*/
#ifdef WLAN_SOFTAP_FEATURE
  {
   v_SIZE_t usMacAddrSize = VOS_MAC_ADDR_SIZE;
   *pvosStatus = vos_pkt_extract_data( vosDataBuff,
                                     ucBDHeaderLen +
                                     WLANTL_MAC_ADDR_ALIGN(ucDisableFrmXtl),
                                     (v_PVOID_t)pvDestMacAdddr,
                                     &usMacAddrSize );
  }
#else
  *pvosStatus = vos_pkt_peek_data( vosDataBuff,
                                     ucBDHeaderLen +
                                     WLANTL_MAC_ADDR_ALIGN(ucDisableFrmXtl),
                                     (v_PVOID_t)&pvPeekData,
                                     VOS_MAC_ADDR_SIZE );

  /*Fix me*/
  vos_copy_macaddr(pvDestMacAdddr, (v_MACADDR_t*)pvPeekData);
#endif
  if ( VOS_STATUS_SUCCESS != *pvosStatus )
  {
     TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
                "WLAN TL:Failed while attempting to extract MAC Addr %d",
                *pvosStatus));
  }
  else
  {
    /*---------------------------------------------------------------------
        Fill MPDU info fields:
          - MPDU data start offset
          - MPDU header start offset
          - MPDU header length
          - MPDU length - this is a 16b field - needs swapping
    --------------------------------------------------------------------*/
    ucHeaderOffset = ucBDHeaderLen;
    ucHeaderLen    = WLANTL_MAC_HEADER_LEN(ucDisableFrmXtl);

    if ( 0 != ucDisableFrmXtl )
    {
      if ( 0 != ucQosEnabled )
      {
        ucHeaderLen += WLANTL_802_11_HEADER_QOS_CTL;
      }

      // Similar to Qos we need something for WDS format !
      if ( ucWDSEnabled != 0 )
      {
        // If we have frame translation enabled
        ucHeaderLen    += WLANTL_802_11_HEADER_ADDR4_LEN;
      }
      if ( extraHeadSpace != 0 )
      {
        // Decrease the packet length with the extra padding after the header
        *pusPktLen = *pusPktLen - extraHeadSpace;
      }
    }

    WLANHAL_TX_BD_SET_MPDU_HEADER_LEN( *ppvBDHeader, ucHeaderLen);
    WLANHAL_TX_BD_SET_MPDU_HEADER_OFFSET( *ppvBDHeader, ucHeaderOffset);
    WLANHAL_TX_BD_SET_MPDU_DATA_OFFSET( *ppvBDHeader,
                                          ucHeaderOffset + ucHeaderLen + extraHeadSpace);
    WLANHAL_TX_BD_SET_MPDU_LEN( *ppvBDHeader, *pusPktLen);

    TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
                "WLAN TL: VALUES ARE HLen=%x Hoff=%x doff=%x len=%x ex=%d",
                ucHeaderLen, ucHeaderOffset, 
                (ucHeaderOffset + ucHeaderLen + extraHeadSpace), 
                *pusPktLen, extraHeadSpace));
  }/* if peek MAC success*/

}/* WLANTL_PrepareBDHeader */
#endif

#ifdef WLAN_SOFTAP_FEATURE
//THIS IS A HACK AND NEEDS TO BE FIXED FOR CONCURRENCY
/*==========================================================================
  FUNCTION    WLAN_TLGetNextTxIds

  DESCRIPTION
    Gets the next station and next AC in the list that should be served by the TL.

    Multiple Station Scheduling and TL queue management. 

    4 HDD BC/MC data packet queue status is specified as Station 0's status. Weights used
    in WFQ algorith are initialized in WLANTL_OPEN and contained in tlConfigInfo field.
    Each station has fields of ucPktPending and AC mask to tell whether a AC has traffic
    or not.
      
    Stations are served in a round-robin fashion from highest priority to lowest priority.
    The number of round-robin times of each prioirty equals to the WFQ weights and differetiates
    the traffic of different prioirty. As such, stations can not provide low priority packets if
    high priority packets are all served.

  DEPENDENCIES

  PARAMETERS

   IN
   pvosGCtx:     pointer to the global vos context; a handle to TL's
                 control block can be extracted from its context

   OUT
   pucSTAId:    Station ID

  RETURN VALUE
    The result code associated with performing the operation

    VOS_STATUS_SUCCESS:   Everything is good

  SIDE EFFECTS
   
   TL context contains currently served station ID in ucCurrentSTA field, currently served AC
   in uCurServedAC field, and unserved weights of current AC in uCurLeftWeight.
   When existing from the function, these three fields are changed accordingly.

============================================================================*/
VOS_STATUS
WLAN_TLAPGetNextTxIds
(
  v_PVOID_t    pvosGCtx,
  v_U8_t*      pucSTAId
)
{
  WLANTL_CbType*  pTLCb;
  v_U8_t          ucACFilter = 1;
  v_U8_t          ucNextSTA ; 
  v_BOOL_t        isServed = TRUE;  //current round has find a packet or not
  v_U8_t          ucACLoopNum = WLANTL_AC_VO + 1; //number of loop to go
  v_U8_t          uFlowMask; // TX FlowMask from WDA
  uint8           ucACMask; 
  uint8           i; 
  /*------------------------------------------------------------------------
    Extract TL control block
  ------------------------------------------------------------------------*/
  //ENTER();

  pTLCb = VOS_GET_TL_CB(pvosGCtx);
  if ( NULL == pTLCb )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
      "WLAN TL:Invalid TL pointer from pvosGCtx on WLAN_TLAPGetNextTxIds"));
    return VOS_STATUS_E_FAULT;
  }

  if ( VOS_STATUS_SUCCESS != WDA_DS_GetTxFlowMask( pvosGCtx, &uFlowMask ) )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
      "WLAN TL:Failed to retrieve Flow control mask from WDA"));
    return VOS_STATUS_E_FAULT;
  }

  ucNextSTA = pTLCb->ucCurrentSTA;

  ++ucNextSTA;

  if ( WLAN_MAX_STA_COUNT <= ucNextSTA )
  {
    //one round is done.
    ucNextSTA = 0;
    pTLCb->ucCurLeftWeight--;
    isServed = FALSE;
    if ( 0 == pTLCb->ucCurLeftWeight )
    {
      //current prioirty is done
      if ( WLANTL_AC_BK == (WLANTL_ACEnumType)pTLCb->uCurServedAC )
      {
        //end of current VO, VI, BE, BK loop. Reset priority.
        pTLCb->uCurServedAC = WLANTL_AC_VO;
      }
      else 
      {
        pTLCb->uCurServedAC --;
      }

      pTLCb->ucCurLeftWeight =  pTLCb->tlConfigInfo.ucAcWeights[pTLCb->uCurServedAC];
 
    } // (0 == pTLCb->ucCurLeftWeight)
  } //( WLAN_MAX_STA_COUNT == ucNextSTA )

  //decide how many loops to go. if current loop is partial, do one extra to make sure
  //we cover every station
  if ((1 == pTLCb->ucCurLeftWeight) && (ucNextSTA != 0))
  {
    ucACLoopNum ++; // now is 5 loops
  }

  /* Start with highest priority. ucNextSTA, pTLCb->uCurServedAC, pTLCb->ucCurLeftWeight
     all have previous values.*/
  for (; ucACLoopNum > 0;  ucACLoopNum--)
  {

    ucACFilter = 1 << pTLCb->uCurServedAC;

    // pTLCb->ucCurLeftWeight keeps previous results.
    for (; (pTLCb->ucCurLeftWeight > 0) && (uFlowMask & ucACFilter); pTLCb->ucCurLeftWeight-- )
    {

      for ( ; ucNextSTA < WLAN_MAX_STA_COUNT; ucNextSTA ++ )
      {
        WLAN_TL_AC_ARRAY_2_MASK (&pTLCb->atlSTAClients[ucNextSTA], ucACMask, i); 

        if ( (0 == pTLCb->atlSTAClients[ucNextSTA].ucExists) ||
             ((0 == pTLCb->atlSTAClients[ucNextSTA].ucPktPending) && !(ucACMask)) ||
             (0 == (ucACMask & ucACFilter)) )

        {
          //current statioin does not exist or have any packet to serve.
          continue;
        }

        //go to next station if current station can't send due to flow control
        //Station is allowed to send when it is not in LWM mode. When station is in LWM mode,
        //station is allowed to send only after FW reports FW memory is below threshold and on-fly
        //packets are less then allowed value
        if ( (TRUE == pTLCb->atlSTAClients[ucNextSTA].ucLwmModeEnabled) && 
             ((FALSE == pTLCb->atlSTAClients[ucNextSTA].ucLwmEventReported) || 
                 (0 < pTLCb->atlSTAClients[ucNextSTA].uBuffThresholdMax))
           )
        {
          continue;
        }


        // Find a station. Weight is updated already.
        *pucSTAId = ucNextSTA;
        pTLCb->ucCurrentSTA = ucNextSTA;
        pTLCb->atlSTAClients[*pucSTAId].ucCurrentAC = pTLCb->uCurServedAC;
  
        TLLOG4(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_LOW,
                   " TL serve one station AC: %d  W: %d StaId: %d",
                   pTLCb->uCurServedAC, pTLCb->ucCurLeftWeight, pTLCb->ucCurrentSTA ));
      
        return VOS_STATUS_SUCCESS;
      } //STA loop

      ucNextSTA = 0;
      if ( FALSE == isServed )
      {
        //current loop finds no packet.no need to repeat for the same priority
        break;
      }
      //current loop is partial loop. go for one more loop.
      isServed = FALSE;

    } //Weight loop

    if (WLANTL_AC_BK == pTLCb->uCurServedAC)
    {
      pTLCb->uCurServedAC = WLANTL_AC_VO;
    }
    else
    {
      pTLCb->uCurServedAC--;
    }
    pTLCb->ucCurLeftWeight =  pTLCb->tlConfigInfo.ucAcWeights[pTLCb->uCurServedAC];

  }// AC loop

  TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
                   " TL can't find one station to serve \n" ));

  pTLCb->uCurServedAC = WLANTL_AC_BK;
  pTLCb->ucCurLeftWeight = 1;
  //invalid number will be captured by caller
  pTLCb->ucCurrentSTA = WLAN_MAX_STA_COUNT; 

  *pucSTAId = pTLCb->ucCurrentSTA;
  pTLCb->atlSTAClients[*pucSTAId].ucCurrentAC = pTLCb->uCurServedAC;
  return VOS_STATUS_E_FAULT;
}


/*==========================================================================
  FUNCTION    WLAN_TLGetNextTxIds

  DESCRIPTION
    Gets the next station and next AC in the list

  DEPENDENCIES

  PARAMETERS

   IN
   pvosGCtx:     pointer to the global vos context; a handle to TL's
                 control block can be extracted from its context

   OUT
   pucSTAId:    Station ID


  RETURN VALUE
    The result code associated with performing the operation

    VOS_STATUS_SUCCESS:   Everything is good :)

  SIDE EFFECTS

============================================================================*/
VOS_STATUS
WLAN_TLGetNextTxIds
(
  v_PVOID_t    pvosGCtx,
  v_U8_t*      pucSTAId
)
{
  WLANTL_CbType*  pTLCb;
  v_U8_t          ucNextAC;
  v_U8_t          ucNextSTA; 
  v_U8_t          ucCount; 
  v_U8_t          uFlowMask; // TX FlowMask from WDA
  v_U8_t          ucACMask = 0;
  v_U8_t          i = 0; 

  tBssSystemRole systemRole; //RG HACK to be removed
  tpAniSirGlobal pMac;

  pMac = vos_get_context(VOS_MODULE_ID_PE, pvosGCtx);
  if ( NULL == pMac )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
                      "%s: Invalid pMac", __FUNCTION__));
    return VOS_STATUS_E_FAULT;
  }

  systemRole = wdaGetGlobalSystemRole(pMac);
  if ((eSYSTEM_AP_ROLE == systemRole) || (vos_concurrent_sessions_running()))
  {
    return WLAN_TLAPGetNextTxIds(pvosGCtx,pucSTAId);
  }

  
  /*------------------------------------------------------------------------
    Extract TL control block
  ------------------------------------------------------------------------*/
  pTLCb = VOS_GET_TL_CB(pvosGCtx);
  if ( NULL == pTLCb )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
      "WLAN TL:Invalid TL pointer from pvosGCtx on WLAN_TLGetNextTxIds"));
    return VOS_STATUS_E_FAULT;
  }

  if ( VOS_STATUS_SUCCESS != WDA_DS_GetTxFlowMask( pvosGCtx, &uFlowMask ) )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
      "WLAN TL:Failed to retrieve Flow control mask from WDA"));
    return VOS_STATUS_E_FAULT;
  }

  /*STA id - no priority yet implemented */
  /*-----------------------------------------------------------------------
    Choose the next STA for tx - for now go in a round robin fashion
    through all the stations that have pending packets     
  -------------------------------------------------------------------------*/
  ucNextSTA = pTLCb->ucCurrentSTA;
  
  pTLCb->ucCurrentSTA = WLAN_MAX_STA_COUNT; 
  for ( ucCount = 0; 
        ucCount < WLAN_MAX_STA_COUNT;
        ucCount++ )
  {
    ucNextSTA = ( (ucNextSTA+1) >= WLAN_MAX_STA_COUNT )?0:(ucNextSTA+1);
    
    if (( pTLCb->atlSTAClients[ucNextSTA].ucExists ) &&
        ( pTLCb->atlSTAClients[ucNextSTA].ucPktPending ))
    {
      TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
      "STA ID: %d on WLAN_TLGetNextTxIds", *pucSTAId));
      pTLCb->ucCurrentSTA = ucNextSTA; 
      break;
    }
  }

  *pucSTAId = pTLCb->ucCurrentSTA;

  if ( WLANTL_STA_ID_INVALID( *pucSTAId ) )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
      "WLAN TL:No station registered with TL at this point"));

    return VOS_STATUS_E_FAULT;

  }

  /*Convert the array to a mask for easier operation*/
  WLAN_TL_AC_ARRAY_2_MASK( &pTLCb->atlSTAClients[*pucSTAId], ucACMask, i); 
  
  if ( 0 == ucACMask )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
      "WLAN TL: Mask 0 "
      "STA ID: %d on WLAN_TLGetNextTxIds", *pucSTAId));

     /*setting STA id to invalid if mask is 0*/
     *pucSTAId = WLAN_MAX_STA_COUNT;

     return VOS_STATUS_E_FAULT;
  }

  /*-----------------------------------------------------------------------
    AC is updated whenever a packet is fetched from HDD -> the current
    weight of such an AC cannot be 0 -> in this case TL is expected to
    exit this function at this point during the main Tx loop
  -----------------------------------------------------------------------*/
  if ( 0 < pTLCb->atlSTAClients[*pucSTAId].ucCurrentWeight  )
  {
    TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
                  "WLAN TL: Maintaining serviced AC to: %d for Weight: %d",
                  pTLCb->atlSTAClients[*pucSTAId].ucCurrentAC ,
                  pTLCb->atlSTAClients[*pucSTAId].ucCurrentWeight));
    return VOS_STATUS_SUCCESS;
  }

  /*-----------------------------------------------------------------------
     Choose highest priority AC - !!! optimize me
  -----------------------------------------------------------------------*/
  ucNextAC = pTLCb->atlSTAClients[*pucSTAId].ucCurrentAC;
    TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
             "Next AC: %d", ucNextAC));

  while ( 0 != ucACMask )
  {
    TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
             " AC Mask: %d Next: %d Res : %d",
               ucACMask, ( 1 << ucNextAC ), ( ucACMask & ( 1 << ucNextAC ))));

    if ( 0 != ( ucACMask & ( 1 << ucNextAC ) & uFlowMask ))
    {
       pTLCb->atlSTAClients[*pucSTAId].ucCurrentAC     = 
                                   (WLANTL_ACEnumType)ucNextAC;
       pTLCb->atlSTAClients[*pucSTAId].ucCurrentWeight =
                       pTLCb->tlConfigInfo.ucAcWeights[ucNextAC];

        TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
                  "WLAN TL: Switching serviced AC to: %d with Weight: %d",
                  pTLCb->atlSTAClients[*pucSTAId].ucCurrentAC ,
                  pTLCb->atlSTAClients[*pucSTAId].ucCurrentWeight));
       break;
    }

    ucNextAC = ( ucNextAC - 1 ) & WLANTL_MASK_AC;

    TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
               "Next AC %d", ucNextAC));

  }

  TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
             " C AC: %d C W: %d",
             pTLCb->atlSTAClients[*pucSTAId].ucCurrentAC,
             pTLCb->atlSTAClients[*pucSTAId].ucCurrentWeight));

  return VOS_STATUS_SUCCESS;
}/* WLAN_TLGetNextTxIds */

#else

/*==========================================================================
  FUNCTION    WLAN_TLGetNextTxIds

  DESCRIPTION
    Gets the next station and next AC in the list

  DEPENDENCIES

  PARAMETERS

   IN
   pvosGCtx:     pointer to the global vos context; a handle to TL's
                 control block can be extracted from its context

   OUT
   pucSTAId:    Station ID


  RETURN VALUE
    The result code associated with performing the operation

    VOS_STATUS_SUCCESS:   Everything is good :)

  SIDE EFFECTS

============================================================================*/
VOS_STATUS
WLAN_TLGetNextTxIds
(
  v_PVOID_t    pvosGCtx,
  v_U8_t*      pucSTAId
)
{
  WLANTL_CbType*  pTLCb;
  v_U8_t          ucNextAC;
  v_U8_t          ucNextSTA; 
  v_U8_t          ucCount; 
  /*------------------------------------------------------------------------
    Extract TL control block
  ------------------------------------------------------------------------*/
  pTLCb = VOS_GET_TL_CB(pvosGCtx);
  if ( NULL == pTLCb )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
      "WLAN TL:Invalid TL pointer from pvosGCtx on WLAN_TLGetNextTxIds"));
    return VOS_STATUS_E_FAULT;
  }

  /*STA id - no priority yet implemented */
  /*-----------------------------------------------------------------------
    Choose the next STA for tx - for now go in a round robin fashion
    through all the stations that have pending packets     
  -------------------------------------------------------------------------*/
  ucNextSTA = pTLCb->ucCurrentSTA;
  
  pTLCb->ucCurrentSTA = WLAN_MAX_STA_COUNT; 
  for ( ucCount = 0; 
        ucCount < WLAN_MAX_STA_COUNT;
        ucCount++ )
  {
    ucNextSTA = ( (ucNextSTA+1) >= WLAN_MAX_STA_COUNT )?0:(ucNextSTA+1);
    
    if (( pTLCb->atlSTAClients[ucNextSTA].ucExists ) &&
        ( pTLCb->atlSTAClients[ucNextSTA].ucPktPending ))
    {
      TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
      "WLAN TL:No station registered with TL at this point or Mask 0"
      "STA ID: %d on WLAN_TLGetNextTxIds", *pucSTAId));
      pTLCb->ucCurrentSTA = ucNextSTA; 
      break;
    }
  }

  *pucSTAId = pTLCb->ucCurrentSTA;

   if ( ( WLANTL_STA_ID_INVALID( *pucSTAId ) ) ||
        ( 0 == ucACMask ) )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
      "WLAN TL:No station registered with TL at this point or Mask 0"
      "STA ID: %d on WLAN_TLGetNextTxIds", *pucSTAId));

     /*setting STA id to invalid if mask is 0*/
     *pucSTAId = WLAN_MAX_STA_COUNT;

     return VOS_STATUS_E_FAULT;
  }

  /*-----------------------------------------------------------------------
    AC is updated whenever a packet is fetched from HDD -> the current
    weight of such an AC cannot be 0 -> in this case TL is expected to
    exit this function at this point during the main Tx loop
  -----------------------------------------------------------------------*/
  if ( 0 < pTLCb->atlSTAClients[*pucSTAId].ucCurrentWeight  )
  {
    TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
                  "WLAN TL: Maintaining serviced AC to: %d for Weight: %d",
                  pTLCb->atlSTAClients[*pucSTAId].ucCurrentAC ,
                  pTLCb->atlSTAClients[*pucSTAId].ucCurrentWeight));
    return VOS_STATUS_SUCCESS;
  }

  /*-----------------------------------------------------------------------
     Choose highest priority AC - !!! optimize me
  -----------------------------------------------------------------------*/
  ucNextAC = pTLCb->atlSTAClients[*pucSTAId].ucCurrentAC;
    TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
             "Next AC: %d", ucNextAC));

  while ( 0 != ucACMask )
  {
    TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
             " AC Mask: %d Next: %d Res : %d",
               ucACMask, ( 1 << ucNextAC ), ( ucACMask & ( 1 << ucNextAC ))));

    if ( 0 !=  pTLCb->atlSTAClients[*pucSTAId].aucACMask[ucNextAC] )
    {
       pTLCb->atlSTAClients[*pucSTAId].ucCurrentAC     = 
                                   (WLANTL_ACEnumType)ucNextAC;
       pTLCb->atlSTAClients[*pucSTAId].ucCurrentWeight =
                       pTLCb->tlConfigInfo.ucAcWeights[ucNextAC];

        TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
                  "WLAN TL: Switching serviced AC to: %d with Weight: %d",
                  pTLCb->atlSTAClients[*pucSTAId].ucCurrentAC ,
                  pTLCb->atlSTAClients[*pucSTAId].ucCurrentWeight));
       break;
    }

    ucNextAC = ( ucNextAC - 1 ) & WLANTL_MASK_AC;

    TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
               "Next AC %d", ucNextAC));

  }

  TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
             " C AC: %d C W: %d",
             pTLCb->atlSTAClients[*pucSTAId].ucCurrentAC,
             pTLCb->atlSTAClients[*pucSTAId].ucCurrentWeight));

  return VOS_STATUS_SUCCESS;
}/* WLAN_TLGetNextTxIds */
#endif //WLAN_SOFTAP_FEATURE


/*==========================================================================
      DEFAULT HANDLERS: Registered at initialization with TL
  ==========================================================================*/

/*==========================================================================

  FUNCTION    WLANTL_MgmtFrmRxDefaultCb

  DESCRIPTION
    Default Mgmt Frm rx callback: asserts all the time. If this function gets
    called  it means there is no registered rx cb pointer for Mgmt Frm.

  DEPENDENCIES

  PARAMETERS
    Not used.

  RETURN VALUE

   VOS_STATUS_E_FAILURE: Always FAILURE.

============================================================================*/
VOS_STATUS
WLANTL_MgmtFrmRxDefaultCb
(
  v_PVOID_t  pvosGCtx,
  v_PVOID_t  vosBuff
)
{
  if ( NULL != vosBuff )
  {
    TLLOGP(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_FATAL,
             "WLAN TL:Fatal failure: No registered Mgmt Frm client on pkt RX"));
    /* Drop packet */
    vos_pkt_return_packet((vos_pkt_t *)vosBuff);
  }

#if !defined( FEATURE_WLAN_INTEGRATED_SOC )
  if(!vos_is_load_unload_in_progress(VOS_MODULE_ID_TL, NULL))
  {
      TLLOGP(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_FATAL,
                 "WLAN TL:Fatal failure: No registered Mgmt Frm client on pkt RX"));
      VOS_ASSERT(0);
  }
  else
  {
#endif
      TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
                 "WLAN TL: No registered Mgmt Frm client on pkt RX. Load/Unload in progress, Ignore"));
#if !defined( FEATURE_WLAN_INTEGRATED_SOC )
  }
#endif

  return VOS_STATUS_E_FAILURE;
}/*WLANTL_MgmtFrmRxDefaultCb*/

/*==========================================================================

  FUNCTION    WLANTL_STARxDefaultCb

  DESCRIPTION
    Default BAP rx callback: asserts all the time. If this function gets
    called  it means there is no registered rx cb pointer for BAP.

  DEPENDENCIES

  PARAMETERS
    Not used.

  RETURN VALUE

   VOS_STATUS_E_FAILURE: Always FAILURE.

============================================================================*/
VOS_STATUS
WLANTL_BAPRxDefaultCb
(
  v_PVOID_t  pvosGCtx,
  vos_pkt_t*       vosDataBuff,
  WLANTL_BAPFrameEnumType frameType
)
{
  TLLOGP(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_FATAL,
             "WLAN TL:Fatal failure: No registered BAP client on BAP pkt RX"));
#ifndef BTAMP_TEST
  VOS_ASSERT(0);
#endif
  return VOS_STATUS_E_FAILURE;
}/*WLANTL_MgmtFrmRxDefaultCb*/

/*==========================================================================

  FUNCTION    WLANTL_STARxDefaultCb

  DESCRIPTION
    Default STA rx callback: asserts all the time. If this function gets
    called  it means there is no registered rx cb pointer for station.
    (Mem corruption most likely, it should never happen)

  DEPENDENCIES

  PARAMETERS
    Not used.

  RETURN VALUE

    VOS_STATUS_E_FAILURE: Always FAILURE.

============================================================================*/
VOS_STATUS
WLANTL_STARxDefaultCb
(
  v_PVOID_t               pvosGCtx,
  vos_pkt_t*              vosDataBuff,
  v_U8_t                  ucSTAId,
  WLANTL_RxMetaInfoType*  pRxMetaInfo
)
{
  TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
       "WLAN TL: No registered STA client rx cb for STAID: %d dropping pkt",
               ucSTAId));
  vos_pkt_return_packet(vosDataBuff);
  return VOS_STATUS_E_FAILURE;
}/*WLANTL_MgmtFrmRxDefaultCb*/


/*==========================================================================

  FUNCTION    WLANTL_STAFetchPktDefaultCb

  DESCRIPTION
    Default fetch callback: asserts all the time. If this function gets
    called  it means there is no registered fetch cb pointer for station.
    (Mem corruption most likely, it should never happen)

  DEPENDENCIES

  PARAMETERS
    Not used.

  RETURN VALUE

    VOS_STATUS_E_FAILURE: Always FAILURE.

============================================================================*/
VOS_STATUS
WLANTL_STAFetchPktDefaultCb
(
  v_PVOID_t              pvosGCtx,
  v_U8_t*                pucSTAId,
  WLANTL_ACEnumType      ucAC,
  vos_pkt_t**            vosDataBuff,
  WLANTL_MetaInfoType*   tlMetaInfo
)
{
  TLLOGP(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_FATAL,
             "WLAN TL:Fatal failure: No registered STA client on data pkt RX"));
  VOS_ASSERT(0);
  return VOS_STATUS_E_FAILURE;
}/*WLANTL_MgmtFrmRxDefaultCb*/

/*==========================================================================

  FUNCTION    WLANTL_TxCompDefaultCb

  DESCRIPTION
    Default tx complete handler. It will release the completed pkt to
    prevent memory leaks.

  PARAMETERS

    IN
    pvosGCtx:       pointer to the global vos context; a handle to
                    TL/HAL/PE/BAP/HDD control block can be extracted from
                    its context
    vosDataBuff:   pointer to the VOSS data buffer that was transmitted
    wTxSTAtus:      status of the transmission


  RETURN VALUE
    The result code associated with performing the operation; please
    check vos_pkt_return_packet for possible error codes.

    Please check  vos_pkt_return_packet API for possible return values.

============================================================================*/
VOS_STATUS
WLANTL_TxCompDefaultCb
(
 v_PVOID_t      pvosGCtx,
 vos_pkt_t*     vosDataBuff,
 VOS_STATUS     wTxSTAtus
)
{
  TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
         "WLAN TL:TXComp not registered, releasing pkt to prevent mem leak"));
  return vos_pkt_return_packet(vosDataBuff);
}/*WLANTL_TxCompDefaultCb*/


/*==========================================================================
      Cleanup functions
  ==========================================================================*/

/*==========================================================================

  FUNCTION    WLANTL_CleanCB

  DESCRIPTION
    Cleans TL control block

  DEPENDENCIES

  PARAMETERS

    IN
    pTLCb:       pointer to TL's control block
    ucEmpty:     set if TL has to clean up the queues and release pedning pkts

  RETURN VALUE
    The result code associated with performing the operation

     VOS_STATUS_E_INVAL:   invalid input parameters
     VOS_STATUS_SUCCESS:   Everything is good :)

  SIDE EFFECTS

============================================================================*/
VOS_STATUS
WLANTL_CleanCB
(
  WLANTL_CbType*  pTLCb,
  v_U8_t      ucEmpty
)
{
  v_U8_t ucIndex;
  /*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

  /*-------------------------------------------------------------------------
    Sanity check
   -------------------------------------------------------------------------*/
  if ( NULL == pTLCb )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
               "WLAN TL:Invalid parameter sent on WLANTL_CleanCB"));
    return VOS_STATUS_E_INVAL;
  }

  /* number of packets sent to BAL waiting for tx complete confirmation */
  pTLCb->usPendingTxCompleteCount = 0;

  /* global suspend flag */
   vos_atomic_set_U8( &pTLCb->ucTxSuspended, 1);

  /* resource flag */
  pTLCb->uResCount = 0;


  /*-------------------------------------------------------------------------
    Client stations
   -------------------------------------------------------------------------*/
  for ( ucIndex = 0; ucIndex < WLAN_MAX_STA_COUNT ; ucIndex++)
  {
    WLANTL_CleanSTA( &pTLCb->atlSTAClients[ucIndex], ucEmpty);
  }

  /*-------------------------------------------------------------------------
    Management Frame client
   -------------------------------------------------------------------------*/
  pTLCb->tlMgmtFrmClient.ucExists = 0;

  if ( ( 0 != ucEmpty) &&
       ( NULL != pTLCb->tlMgmtFrmClient.vosPendingDataBuff ))
  {
    vos_pkt_return_packet(pTLCb->tlMgmtFrmClient.vosPendingDataBuff);
  }

  pTLCb->tlMgmtFrmClient.vosPendingDataBuff  = NULL;

  /* set to a default cb in order to prevent constant checking for NULL */
  pTLCb->tlMgmtFrmClient.pfnTlMgmtFrmRx = WLANTL_MgmtFrmRxDefaultCb;

  /*-------------------------------------------------------------------------
    BT AMP client
   -------------------------------------------------------------------------*/
  pTLCb->tlBAPClient.ucExists = 0;

  if (( 0 != ucEmpty) &&
      ( NULL != pTLCb->tlBAPClient.vosPendingDataBuff ))
  {
    vos_pkt_return_packet(pTLCb->tlBAPClient.vosPendingDataBuff);
  }
  
  if (( 0 != ucEmpty) &&
      ( NULL != pTLCb->vosDummyBuf ))
  {
    vos_pkt_return_packet(pTLCb->vosDummyBuf);
  }

  pTLCb->tlBAPClient.vosPendingDataBuff  = NULL;

  pTLCb->vosDummyBuf = NULL;
  pTLCb->vosTempBuf  = NULL;
  pTLCb->ucCachedSTAId = WLAN_MAX_STA_COUNT;

  /* set to a default cb in order to prevent constant checking for NULL */
  pTLCb->tlBAPClient.pfnTlBAPRx = WLANTL_BAPRxDefaultCb;

  pTLCb->ucRegisteredStaId = WLAN_MAX_STA_COUNT;

  return VOS_STATUS_SUCCESS;

}/* WLANTL_CleanCB*/

/*==========================================================================

  FUNCTION    WLANTL_CleanSTA

  DESCRIPTION
    Cleans a station control block.

  DEPENDENCIES

  PARAMETERS

    IN
    pvosGCtx:       pointer to the global vos context; a handle to TL's
                    control block can be extracted from its context
    ucEmpty:        if set the queues and pending pkts will be emptyed

  RETURN VALUE
    The result code associated with performing the operation

    VOS_STATUS_E_INVAL:   invalid input parameters
    VOS_STATUS_SUCCESS:   Everything is good :)

  SIDE EFFECTS

============================================================================*/
VOS_STATUS
WLANTL_CleanSTA
(
  WLANTL_STAClientType*  ptlSTAClient,
  v_U8_t             ucEmpty
)
{
  v_U8_t  ucIndex;
  /*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

  /*-------------------------------------------------------------------------
    Sanity check
   -------------------------------------------------------------------------*/
  if ( NULL == ptlSTAClient )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
               "WLAN TL:Invalid parameter sent on WLANTL_CleanSTA"));
    return VOS_STATUS_E_INVAL;
  }

  /*------------------------------------------------------------------------
    Clear station from TL
   ------------------------------------------------------------------------*/
  TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
             "WLAN TL: Clearing STA Client ID: %d, Empty flag: %d",
             ptlSTAClient->wSTADesc.ucSTAId, ucEmpty ));

  ptlSTAClient->pfnSTARx          = WLANTL_STARxDefaultCb;
  ptlSTAClient->pfnSTATxComp      = WLANTL_TxCompDefaultCb;
  ptlSTAClient->pfnSTAFetchPkt    = WLANTL_STAFetchPktDefaultCb;

  ptlSTAClient->tlState           = WLANTL_STA_INIT;
  ptlSTAClient->tlPri             = WLANTL_STA_PRI_NORMAL;

  vos_zero_macaddr( &ptlSTAClient->wSTADesc.vSTAMACAddress );
  vos_zero_macaddr( &ptlSTAClient->wSTADesc.vBSSIDforIBSS );
  vos_zero_macaddr( &ptlSTAClient->wSTADesc.vSelfMACAddress );

  ptlSTAClient->wSTADesc.ucSTAId  = 0;
  ptlSTAClient->wSTADesc.wSTAType = WLAN_STA_MAX;

  ptlSTAClient->wSTADesc.ucQosEnabled     = 0;
  ptlSTAClient->wSTADesc.ucAddRmvLLC      = 0;
  ptlSTAClient->wSTADesc.ucSwFrameTXXlation = 0;
  ptlSTAClient->wSTADesc.ucSwFrameRXXlation = 0;
  ptlSTAClient->wSTADesc.ucProtectedFrame = 0;

  /*-------------------------------------------------------------------------
    AMSDU information for the STA
   -------------------------------------------------------------------------*/
  if ( ( 0 != ucEmpty ) &&
       ( NULL != ptlSTAClient->vosAMSDUChainRoot ))
  {
    vos_pkt_return_packet(ptlSTAClient->vosAMSDUChainRoot);
  }

  ptlSTAClient->vosAMSDUChain     = NULL;
  ptlSTAClient->vosAMSDUChainRoot = NULL;

  vos_mem_zero( (v_PVOID_t)ptlSTAClient->aucMPDUHeader,
                 WLANTL_MPDU_HEADER_LEN);
  ptlSTAClient->ucMPDUHeaderLen    = 0;

  /*-------------------------------------------------------------------------
    Reordering information for the STA
   -------------------------------------------------------------------------*/
  for ( ucIndex = 0; ucIndex < WLAN_MAX_TID ; ucIndex++)
  {
    if(0 == ptlSTAClient->atlBAReorderInfo[ucIndex].ucExists)
    {
      continue;
    }
    if(NULL != ptlSTAClient->atlBAReorderInfo[ucIndex].reorderBuffer)
    {
      ptlSTAClient->atlBAReorderInfo[ucIndex].reorderBuffer->isAvailable = VOS_TRUE;
      memset(&ptlSTAClient->atlBAReorderInfo[ucIndex].reorderBuffer->arrayBuffer[0], 0, WLANTL_MAX_WINSIZE * sizeof(v_PVOID_t));
    }
    vos_timer_destroy(&ptlSTAClient->atlBAReorderInfo[ucIndex].agingTimer);
    memset(&ptlSTAClient->atlBAReorderInfo[ucIndex], 0, sizeof(WLANTL_BAReorderType));
  }

  /*-------------------------------------------------------------------------
     QOS information for the STA
    -------------------------------------------------------------------------*/
   ptlSTAClient->ucCurrentAC     = WLANTL_AC_VO;
   ptlSTAClient->ucCurrentWeight = 0;
   ptlSTAClient->ucServicedAC    = WLANTL_AC_BK;

   vos_mem_zero( ptlSTAClient->aucACMask, sizeof(ptlSTAClient->aucACMask));
   vos_mem_zero( &ptlSTAClient->wUAPSDInfo, sizeof(ptlSTAClient->wUAPSDInfo));


  /*--------------------------------------------------------------------
    Stats info
    --------------------------------------------------------------------*/
   vos_mem_zero( ptlSTAClient->auRxCount,
                 sizeof(ptlSTAClient->auRxCount[0])* WLAN_MAX_TID);
   vos_mem_zero( ptlSTAClient->auTxCount,
                 sizeof(ptlSTAClient->auTxCount[0])* WLAN_MAX_TID);
   ptlSTAClient->rssiAvg = 0;

   /*Tx not suspended and station fully registered*/
   vos_atomic_set_U8( &ptlSTAClient->ucTxSuspended, 0);
   vos_atomic_set_U8( &ptlSTAClient->ucNoMoreData, 1);

  if ( 0 == ucEmpty )
  {
    ptlSTAClient->wSTADesc.ucUcastSig       = WLAN_TL_INVALID_U_SIG;
    ptlSTAClient->wSTADesc.ucBcastSig       = WLAN_TL_INVALID_B_SIG;
  }

  ptlSTAClient->ucExists       = 0;

  /*--------------------------------------------------------------------
    Statistics info 
    --------------------------------------------------------------------*/
  memset(&ptlSTAClient->trafficStatistics,
         0,
         sizeof(WLANTL_TRANSFER_STA_TYPE));

  /*fix me!!: add new values from the TL Cb for cleanup */
  return VOS_STATUS_SUCCESS;
}/* WLANTL_CleanSTA */


/*==========================================================================
  FUNCTION    WLANTL_EnableUAPSDForAC

  DESCRIPTION
   Called by HDD to enable UAPSD. TL in turn calls WDA API to enable the
   logic in FW/SLM to start sending trigger frames. Previously TL had the
   trigger frame logic which later moved down to FW. Hence
   HDD -> TL -> WDA -> FW call flow.

  DEPENDENCIES
    The TL must be initialized before this function can be called.

  PARAMETERS

    IN
    pvosGCtx:       pointer to the global vos context; a handle to TL's
                    control block can be extracted from its context
    ucSTAId:        station Id
    ucAC:           AC for which U-APSD is being enabled
    ucTid:          TID for which U-APSD is setup
    ucUP:           used to place in the trigger frame generation
    ucServiceInt:   service interval used by TL to send trigger frames
    ucSuspendInt:   suspend interval used by TL to determine that an
                    app is idle and should start sending trigg frms less often
    wTSDir:         direction of TSpec

  RETURN VALUE
    The result code associated with performing the operation

    VOS_STATUS_SUCCESS:   Everything is good :)

  SIDE EFFECTS

============================================================================*/
VOS_STATUS
WLANTL_EnableUAPSDForAC
(
  v_PVOID_t          pvosGCtx,
  v_U8_t             ucSTAId,
  WLANTL_ACEnumType  ucAC,
  v_U8_t             ucTid,
  v_U8_t             ucUP,
  v_U32_t            uServiceInt,
  v_U32_t            uSuspendInt,
  WLANTL_TSDirType   wTSDir
)
{

  WLANTL_CbType*      pTLCb      = NULL;
  VOS_STATUS          vosStatus   = VOS_STATUS_SUCCESS;
  tUapsdInfo          halUAPSDInfo; 
 /*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

  /*------------------------------------------------------------------------
    Sanity check
    Extract TL control block
   ------------------------------------------------------------------------*/
  pTLCb = VOS_GET_TL_CB(pvosGCtx);
  if (( NULL == pTLCb ) || WLANTL_STA_ID_INVALID( ucSTAId )
      ||   WLANTL_AC_INVALID(ucAC) || ( 0 == uServiceInt ) )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
               "WLAN TL:Invalid input params on WLANTL_EnableUAPSDForAC"
               " TL: %x  STA: %d  AC: %d SI: %d", 
               pTLCb, ucSTAId, ucAC, uServiceInt ));
    return VOS_STATUS_E_FAULT;
  }

  /*Set this flag in order to remember that this is a trigger enabled AC*/
  pTLCb->atlSTAClients[ucSTAId].wUAPSDInfo[ucAC].ucSet = 1; 
  
  TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
             "WLAN TL:Enabling U-APSD in FW for STA: %d AC: %d SI: %d SPI: %d "
             "DI: %d",
             ucSTAId, ucAC, uServiceInt, uSuspendInt,
             pTLCb->tlConfigInfo.uDelayedTriggerFrmInt));

  /*Save all info for HAL*/
  halUAPSDInfo.staidx         = ucSTAId; 
  halUAPSDInfo.ac             = ucAC;   
  halUAPSDInfo.up             = ucUP;   
  halUAPSDInfo.srvInterval    = uServiceInt;  
  halUAPSDInfo.susInterval    = uSuspendInt;
  halUAPSDInfo.delayInterval  = pTLCb->tlConfigInfo.uDelayedTriggerFrmInt; 

  /*Notify HAL*/
  vosStatus = WDA_EnableUapsdAcParams(pvosGCtx, ucSTAId, &halUAPSDInfo);

  return vosStatus;

}/*WLANTL_EnableUAPSDForAC*/


/*==========================================================================
  FUNCTION    WLANTL_DisableUAPSDForAC

  DESCRIPTION
   Called by HDD to disable UAPSD. TL in turn calls WDA API to disable the
   logic in FW/SLM to stop sending trigger frames. Previously TL had the
   trigger frame logic which later moved down to FW. Hence
   HDD -> TL -> WDA -> FW call flow.

  DEPENDENCIES
    The TL must be initialized before this function can be called.

  PARAMETERS

    IN
    pvosGCtx:       pointer to the global vos context; a handle to TL's
                    control block can be extracted from its context
    ucSTAId:        station Id
    ucAC:         AC for which U-APSD is being enabled


  RETURN VALUE
    The result code associated with performing the operation

    VOS_STATUS_SUCCESS:   Everything is good :)

  SIDE EFFECTS

============================================================================*/
VOS_STATUS
WLANTL_DisableUAPSDForAC
(
  v_PVOID_t          pvosGCtx,
  v_U8_t             ucSTAId,
  WLANTL_ACEnumType  ucAC
)
{
  WLANTL_CbType* pTLCb;
  /*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

  /*------------------------------------------------------------------------
    Sanity check
    Extract TL control block
   ------------------------------------------------------------------------*/
  pTLCb = VOS_GET_TL_CB(pvosGCtx);
  if (( NULL == pTLCb ) || WLANTL_STA_ID_INVALID( ucSTAId )
      ||   WLANTL_AC_INVALID(ucAC) )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
               "WLAN TL:Invalid input params on WLANTL_DisableUAPSDForAC"
               " TL: %x  STA: %d  AC: %d", pTLCb, ucSTAId, ucAC ));
    return VOS_STATUS_E_FAULT;
  }

  /*Reset this flag as this is no longer a trigger enabled AC*/
  pTLCb->atlSTAClients[ucSTAId].wUAPSDInfo[ucAC].ucSet = 1; 

  TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
             "WLAN TL:Disabling U-APSD in FW for STA: %d AC: %d ",
             ucSTAId, ucAC));

  /*Notify HAL*/
  WDA_DisableUapsdAcParams(pvosGCtx, ucSTAId, ucAC);

  return VOS_STATUS_SUCCESS;
}/* WLANTL_DisableUAPSDForAC */

#if defined WLAN_FEATURE_NEIGHBOR_ROAMING
/*==========================================================================
  FUNCTION     WLANTL_RegRSSIIndicationCB

  DESCRIPTION  Registration function to get notification if RSSI cross
               threshold.
               Client should register threshold, direction, and notification
               callback function pointer

  DEPENDENCIES NONE
    
  PARAMETERS   in pAdapter - Global handle
               in rssiValue - RSSI threshold value
               in triggerEvent - Cross direction should be notified
                                 UP, DOWN, and CROSS
               in crossCBFunction - Notification CB Function
               in usrCtxt - user context

  RETURN VALUE VOS_STATUS

  SIDE EFFECTS NONE
  
============================================================================*/
VOS_STATUS WLANTL_RegRSSIIndicationCB
(
   v_PVOID_t                       pAdapter,
   v_S7_t                          rssiValue,
   v_U8_t                          triggerEvent,
   WLANTL_RSSICrossThresholdCBType crossCBFunction,
   VOS_MODULE_ID                   moduleID,
   v_PVOID_t                       usrCtxt
)
{
   VOS_STATUS                     status = VOS_STATUS_SUCCESS;

   status = WLANTL_HSRegRSSIIndicationCB(pAdapter,
                                         rssiValue,
                                         triggerEvent,
                                         crossCBFunction,
                                         moduleID,
                                         usrCtxt);

   return status;
}

/*==========================================================================
  FUNCTION     WLANTL_DeregRSSIIndicationCB

  DESCRIPTION  Remove specific threshold from list

  DEPENDENCIES NONE
    
  PARAMETERS   in pAdapter - Global handle
               in rssiValue - RSSI threshold value
               in triggerEvent - Cross direction should be notified
                                 UP, DOWN, and CROSS
   
  RETURN VALUE VOS_STATUS

  SIDE EFFECTS NONE
  
============================================================================*/
VOS_STATUS WLANTL_DeregRSSIIndicationCB
(
   v_PVOID_t                       pAdapter,
   v_S7_t                          rssiValue,
   v_U8_t                          triggerEvent,
   WLANTL_RSSICrossThresholdCBType crossCBFunction,
   VOS_MODULE_ID                   moduleID
)
{
   VOS_STATUS                     status = VOS_STATUS_SUCCESS;

   status = WLANTL_HSDeregRSSIIndicationCB(pAdapter,
                                           rssiValue,
                                           triggerEvent,
                                           crossCBFunction,
                                           moduleID);
   return status;
}

/*==========================================================================
  FUNCTION     WLANTL_SetAlpha

  DESCRIPTION  ALPLA is weight value to calculate AVG RSSI
               avgRSSI = (ALPHA * historyRSSI) + ((10 - ALPHA) * newRSSI)
               avgRSSI has (ALPHA * 10)% of history RSSI weight and
               (10 - ALPHA)% of newRSSI weight
               This portion is dynamically configurable.
               Default is ?

  DEPENDENCIES NONE
    
  PARAMETERS   in pAdapter - Global handle
               in valueAlpah - ALPHA
   
  RETURN VALUE VOS_STATUS

  SIDE EFFECTS NONE
  
============================================================================*/
VOS_STATUS WLANTL_SetAlpha
(
   v_PVOID_t pAdapter,
   v_U8_t    valueAlpha
)
{
   VOS_STATUS                     status = VOS_STATUS_SUCCESS;

   status = WLANTL_HSSetAlpha(pAdapter, valueAlpha);
   return status;
}

/*==========================================================================

   FUNCTION

   DESCRIPTION 
    
   PARAMETERS 

   RETURN VALUE

============================================================================*/
VOS_STATUS WLANTL_BMPSRSSIRegionChangedNotification
(
   v_PVOID_t             pAdapter,
   tpSirRSSINotification pRSSINotification
)
{
   VOS_STATUS                     status = VOS_STATUS_SUCCESS;

   status = WLANTL_HSBMPSRSSIRegionChangedNotification(pAdapter, pRSSINotification);
   return status;
}

/*==========================================================================
  FUNCTION     WLANTL_RegGetTrafficStatus

  DESCRIPTION  Registration function for traffic status monitoring
               During measure period count data frames.
               If frame count is larger then IDLE threshold set as traffic ON
               or OFF.
               And traffic status is changed send report to client with
               registered callback function

  DEPENDENCIES NONE
    
  PARAMETERS   in pAdapter - Global handle
               in idleThreshold - Traffic on or off threshold
               in measurePeriod - Traffic state check period
               in trfficStatusCB - traffic status changed notification
                                   CB function
               in usrCtxt - user context
   
  RETURN VALUE VOS_STATUS

  SIDE EFFECTS NONE
  
============================================================================*/
VOS_STATUS WLANTL_RegGetTrafficStatus
(
   v_PVOID_t                          pAdapter,
   v_U32_t                            idleThreshold,
   v_U32_t                            measurePeriod,
   WLANTL_TrafficStatusChangedCBType  trfficStatusCB,
   v_PVOID_t                          usrCtxt
)
{
   VOS_STATUS                     status = VOS_STATUS_SUCCESS;

   status = WLANTL_HSRegGetTrafficStatus(pAdapter,
                                idleThreshold,
                                measurePeriod,
                                trfficStatusCB,
                                usrCtxt);
   return status;
}
#endif
/*==========================================================================
  FUNCTION      WLANTL_GetStatistics

  DESCRIPTION   Get traffic statistics for identified station 

  DEPENDENCIES  NONE
    
  PARAMETERS    in pAdapter - Global handle
                in statType - specific statistics field to reset
                out statBuffer - traffic statistics buffer
   
  RETURN VALUE  VOS_STATUS

  SIDE EFFECTS  NONE
  
============================================================================*/
VOS_STATUS WLANTL_GetStatistics
(
   v_PVOID_t                  pAdapter,
   WLANTL_TRANSFER_STA_TYPE  *statBuffer,
   v_U8_t                     STAid
)
{
  WLANTL_CbType            *pTLCb  = VOS_GET_TL_CB(pAdapter);
  VOS_STATUS                status = VOS_STATUS_SUCCESS;
  WLANTL_TRANSFER_STA_TYPE *statistics = NULL;

  /*------------------------------------------------------------------------
    Sanity check
    Extract TL control block 
   ------------------------------------------------------------------------*/
  if (NULL == pTLCb) 
  {
    TLLOGE(VOS_TRACE(VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
            "WLAN TL:Invalid TL pointer on WLANTL_GetStatistics"));
    return VOS_STATUS_E_FAULT;
  }

  if(0 == pTLCb->atlSTAClients[STAid].ucExists)
  {
    TLLOGE(VOS_TRACE(VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
    "WLAN TL: %d STA ID does not exist", STAid));
    return VOS_STATUS_E_INVAL;
  }

  if(NULL == statBuffer)
  {
    TLLOGE(VOS_TRACE(VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
    "WLAN TL:Invalid TL statistics buffer pointer on WLANTL_GetStatistics"));
    return VOS_STATUS_E_INVAL;
  }

  statistics = &pTLCb->atlSTAClients[STAid].trafficStatistics;
  memcpy(statBuffer, statistics, sizeof(WLANTL_TRANSFER_STA_TYPE));

  return status;
}

/*==========================================================================
  FUNCTION      WLANTL_ResetStatistics

  DESCRIPTION   Reset statistics structure for identified station ID
                Reset means set values as 0

  DEPENDENCIES  NONE
    
  PARAMETERS    in pAdapter - Global handle
                in statType - specific statistics field to reset
   
  RETURN VALUE  VOS_STATUS

  SIDE EFFECTS  NONE
  
============================================================================*/
VOS_STATUS WLANTL_ResetStatistics
(
   v_PVOID_t                  pAdapter,
   v_U8_t                     STAid
)
{
  WLANTL_CbType            *pTLCb  = VOS_GET_TL_CB(pAdapter);
  VOS_STATUS                status = VOS_STATUS_SUCCESS;
  WLANTL_TRANSFER_STA_TYPE *statistics = NULL;

  /*------------------------------------------------------------------------
    Sanity check
    Extract TL control block 
   ------------------------------------------------------------------------*/
  if (NULL == pTLCb) 
  {
    TLLOGE(VOS_TRACE(VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
            "WLAN TL:Invalid TL pointer on WLANTL_GetStatistics"));
    return VOS_STATUS_E_FAULT;
  }

  if(0 == pTLCb->atlSTAClients[STAid].ucExists)
  {
    TLLOGE(VOS_TRACE(VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
    "WLAN TL: %d STA ID does not exist", STAid));
    return VOS_STATUS_E_INVAL;
  }

  statistics = &pTLCb->atlSTAClients[STAid].trafficStatistics;
  vos_mem_zero((v_VOID_t *)statistics, sizeof(WLANTL_TRANSFER_STA_TYPE));

  return status;
}

/*==========================================================================
  FUNCTION      WLANTL_GetSpecStatistic

  DESCRIPTION   Get specific field within statistics structure for
                identified station ID 

  DEPENDENCIES  NONE

  PARAMETERS    in pAdapter - Global handle
                in statType - specific statistics field to reset
                in STAid    - Station ID
                out buffer  - Statistic value
   
  RETURN VALUE  VOS_STATUS

  SIDE EFFECTS  NONE
  
============================================================================*/
VOS_STATUS WLANTL_GetSpecStatistic
(
   v_PVOID_t                    pAdapter,
   WLANTL_TRANSFER_STATIC_TYPE  statType,
   v_U32_t                     *buffer,
   v_U8_t                       STAid
)
{
  WLANTL_CbType            *pTLCb  = VOS_GET_TL_CB(pAdapter);
  VOS_STATUS                status = VOS_STATUS_SUCCESS;
  WLANTL_TRANSFER_STA_TYPE *statistics = NULL;

  /*------------------------------------------------------------------------
    Sanity check
    Extract TL control block 
   ------------------------------------------------------------------------*/
  if (NULL == pTLCb) 
  {
    TLLOGE(VOS_TRACE(VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
            "WLAN TL:Invalid TL pointer on WLANTL_GetStatistics"));
    return VOS_STATUS_E_FAULT;
  }

  if(0 == pTLCb->atlSTAClients[STAid].ucExists)
  {
    TLLOGE(VOS_TRACE(VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
    "WLAN TL: %d STA ID does not exist", STAid));
    return VOS_STATUS_E_INVAL;
  }

  if(NULL == buffer)
  {
    TLLOGE(VOS_TRACE(VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
    "WLAN TL:Invalid TL statistic buffer pointer on WLANTL_GetStatistics"));
    return VOS_STATUS_E_INVAL;
  }

  statistics = &pTLCb->atlSTAClients[STAid].trafficStatistics;
  switch(statType)
  {
    case WLANTL_STATIC_TX_UC_FCNT:
      *buffer = statistics->txUCFcnt;
      break;

    case WLANTL_STATIC_TX_MC_FCNT:
      *buffer = statistics->txMCFcnt;
      break;

    case WLANTL_STATIC_TX_BC_FCNT:
      *buffer = statistics->txBCFcnt;
      break;

    case WLANTL_STATIC_TX_UC_BCNT:
      *buffer = statistics->txUCBcnt;
      break;

    case WLANTL_STATIC_TX_MC_BCNT:
      *buffer = statistics->txMCBcnt;
      break;

    case WLANTL_STATIC_TX_BC_BCNT:
      *buffer = statistics->txBCBcnt;
      break;

    case WLANTL_STATIC_RX_UC_FCNT:
      *buffer = statistics->rxUCFcnt;
      break;

    case WLANTL_STATIC_RX_MC_FCNT:
      *buffer = statistics->rxMCFcnt;
      break;

    case WLANTL_STATIC_RX_BC_FCNT:
      *buffer = statistics->rxBCFcnt;
      break;

    case WLANTL_STATIC_RX_UC_BCNT:
      *buffer = statistics->rxUCBcnt;
      break;

    case WLANTL_STATIC_RX_MC_BCNT:
      *buffer = statistics->rxMCBcnt;
      break;

    case WLANTL_STATIC_RX_BC_BCNT:
      *buffer = statistics->rxBCBcnt;
      break;

    case WLANTL_STATIC_RX_BCNT:
      *buffer = statistics->rxBcnt;
      break;

    case WLANTL_STATIC_RX_BCNT_CRC_OK:
      *buffer = statistics->rxBcntCRCok;
      break;

    case WLANTL_STATIC_RX_RATE:
      *buffer = statistics->rxRate;
      break;

    default:
      *buffer = 0;
      status = VOS_STATUS_E_INVAL;
      break;
  }


  return status;
}

/*==========================================================================
  FUNCTION      WLANTL_ResetSpecStatistic

  DESCRIPTION   Reset specific field within statistics structure for
                identified station ID
                Reset means set as 0

  DEPENDENCIES  NONE
    
  PARAMETERS    in pAdapter - Global handle
                in statType - specific statistics field to reset
                in STAid    - Station ID

  RETURN VALUE  VOS_STATUS

  SIDE EFFECTS  NONE
  
============================================================================*/
VOS_STATUS WLANTL_ResetSpecStatistic
(
   v_PVOID_t                    pAdapter,
   WLANTL_TRANSFER_STATIC_TYPE  statType,
   v_U8_t                       STAid
)
{
  WLANTL_CbType            *pTLCb  = VOS_GET_TL_CB(pAdapter);
  VOS_STATUS                status = VOS_STATUS_SUCCESS;
  WLANTL_TRANSFER_STA_TYPE *statistics = NULL;

  /*------------------------------------------------------------------------
    Sanity check
    Extract TL control block 
   ------------------------------------------------------------------------*/
  if (NULL == pTLCb) 
  {
    TLLOGE(VOS_TRACE(VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
            "WLAN TL:Invalid TL pointer on WLANTL_GetStatistics"));
    return VOS_STATUS_E_FAULT;
  }

  if(0 == pTLCb->atlSTAClients[STAid].ucExists)
  {
    TLLOGE(VOS_TRACE(VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
    "WLAN TL: %d STA ID does not exist", STAid));
    return VOS_STATUS_E_INVAL;
  }

  statistics = &pTLCb->atlSTAClients[STAid].trafficStatistics;
  switch(statType)
  {
    case WLANTL_STATIC_TX_UC_FCNT:
      statistics->txUCFcnt = 0;
      break;

    case WLANTL_STATIC_TX_MC_FCNT:
      statistics->txMCFcnt = 0;
      break;

    case WLANTL_STATIC_TX_BC_FCNT:
      statistics->txBCFcnt = 0;
      break;

    case WLANTL_STATIC_TX_UC_BCNT:
      statistics->txUCBcnt = 0;
      break;

    case WLANTL_STATIC_TX_MC_BCNT:
      statistics->txMCBcnt = 0;
      break;

    case WLANTL_STATIC_TX_BC_BCNT:
      statistics->txBCBcnt = 0;
      break;

    case WLANTL_STATIC_RX_UC_FCNT:
      statistics->rxUCFcnt = 0;
      break;

    case WLANTL_STATIC_RX_MC_FCNT:
      statistics->rxMCFcnt = 0;
      break;

    case WLANTL_STATIC_RX_BC_FCNT:
      statistics->rxBCFcnt = 0;
      break;

    case WLANTL_STATIC_RX_UC_BCNT:
      statistics->rxUCBcnt = 0;
      break;

    case WLANTL_STATIC_RX_MC_BCNT:
      statistics->rxMCBcnt = 0;
      break;

    case WLANTL_STATIC_RX_BC_BCNT:
      statistics->rxBCBcnt = 0;
      break;

    case WLANTL_STATIC_RX_BCNT:
      statistics->rxBcnt = 0;
      break;

    case WLANTL_STATIC_RX_BCNT_CRC_OK:
      statistics->rxBcntCRCok = 0;
      break;

    case WLANTL_STATIC_RX_RATE:
      statistics->rxRate = 0;
      break;

    default:
      status = VOS_STATUS_E_INVAL;
      break;
  }

  return status;
}


/*==========================================================================

   FUNCTION

   DESCRIPTION   Read RSSI value out of a RX BD
    
   PARAMETERS:  Caller must validate all parameters 

   RETURN VALUE

============================================================================*/
VOS_STATUS WLANTL_ReadRSSI
(
   v_PVOID_t        pAdapter,
   v_PVOID_t        pBDHeader,
   v_U8_t           STAid
)
{
   WLANTL_CbType   *tlCtxt = VOS_GET_TL_CB(pAdapter);
   v_S7_t           currentRSSI, currentRSSI0, currentRSSI1;


   if(NULL == tlCtxt)
   {
      TLLOGE(VOS_TRACE(VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR, "%s Invalid TL handle", __FUNCTION__));
      return VOS_STATUS_E_INVAL;
   }

   currentRSSI0 = WLANTL_GETRSSI0(pBDHeader);
   currentRSSI1 = WLANTL_GETRSSI1(pBDHeader);
   currentRSSI  = (currentRSSI0 > currentRSSI1) ? currentRSSI0 : currentRSSI1;

   tlCtxt->atlSTAClients[STAid].rssiAvg = currentRSSI;

   return VOS_STATUS_SUCCESS;
}


/*
 DESCRIPTION 
    TL returns the weight currently maintained in TL.
 IN
    pvosGCtx:       pointer to the global vos context; a handle to TL's 
                    or SME's control block can be extracted from its context 

 OUT
    pACWeights:     Caller allocated memory for filling in weights

 RETURN VALUE  VOS_STATUS
*/
VOS_STATUS  
WLANTL_GetACWeights 
( 
  v_PVOID_t             pvosGCtx,
  v_U8_t*               pACWeights
)
{
   WLANTL_CbType*  pTLCb = NULL;
   v_U8_t          ucIndex; 
  /*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

  /*------------------------------------------------------------------------
    Sanity check
   ------------------------------------------------------------------------*/
  if ( NULL == pACWeights )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
               "WLAN TL:Invalid parameter sent on WLANTL_GetACWeights"));
    return VOS_STATUS_E_INVAL;
  }

  /*------------------------------------------------------------------------
    Extract TL control block and check existance
   ------------------------------------------------------------------------*/
  pTLCb = VOS_GET_TL_CB(pvosGCtx);
  if ( NULL == pTLCb )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
              "WLAN TL:Invalid TL pointer from pvosGCtx on WLANTL_GetACWeights"));
    return VOS_STATUS_E_FAULT;
  }
  for ( ucIndex = 0; ucIndex < WLANTL_MAX_AC ; ucIndex++)
  {
    pACWeights[ucIndex] = pTLCb->tlConfigInfo.ucAcWeights[ucIndex];
  }

  return VOS_STATUS_SUCCESS;
}



/*
 DESCRIPTION 
    Change the weight currently maintained by TL.
 IN
    pvosGCtx:       pointer to the global vos context; a handle to TL's 
                    or SME's control block can be extracted from its context 
    pACWeights:     Caller allocated memory contain the weights to use


 RETURN VALUE  VOS_STATUS
*/
VOS_STATUS  
WLANTL_SetACWeights 
( 
  v_PVOID_t             pvosGCtx,
  v_U8_t*               pACWeights
)
{
   WLANTL_CbType*  pTLCb = NULL;
   v_U8_t          ucIndex; 
  /*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

  /*------------------------------------------------------------------------
    Sanity check
   ------------------------------------------------------------------------*/
  if ( NULL == pACWeights )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
               "WLAN TL:Invalid parameter sent on WLANTL_GetACWeights"));
    return VOS_STATUS_E_INVAL;
  }

  /*------------------------------------------------------------------------
    Extract TL control block and check existance
   ------------------------------------------------------------------------*/
  pTLCb = VOS_GET_TL_CB(pvosGCtx);
  if ( NULL == pTLCb )
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
              "WLAN TL:Invalid TL pointer from pvosGCtx on WLANTL_GetACWeights"));
    return VOS_STATUS_E_FAULT;
  }
  for ( ucIndex = 0; ucIndex < WLANTL_MAX_AC ; ucIndex++)
  {
    pTLCb->tlConfigInfo.ucAcWeights[ucIndex] = pACWeights[ucIndex];
  }

  return VOS_STATUS_SUCCESS;
}


/*==========================================================================

   FUNCTION

   DESCRIPTION 
    
   PARAMETERS 

   RETURN VALUE

============================================================================*/
void WLANTL_PowerStateChangedCB
(
   v_PVOID_t pAdapter,
   tPmcState newState
)
{
   WLANTL_CbType                *tlCtxt = VOS_GET_TL_CB(pAdapter);

   if (NULL == tlCtxt)
   {
     VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
                "Invalid TL Control Block", __FUNCTION__ );
     return;
   }

   VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO, "Power state changed, new state is %d", newState );
   switch(newState)
   {
      case FULL_POWER:
         tlCtxt->isBMPS = VOS_FALSE;
         break;

      case BMPS:
#if defined WLAN_FEATURE_NEIGHBOR_ROAMING
         WLANTL_SetFWRSSIThresholds(pAdapter);
#endif

         tlCtxt->isBMPS = VOS_TRUE;
         break;

      case IMPS:
      case LOW_POWER:
      case REQUEST_BMPS:
      case REQUEST_FULL_POWER:
      case REQUEST_IMPS:
      case STOPPED:
      case REQUEST_START_UAPSD:
      case REQUEST_STOP_UAPSD:
      case UAPSD:
      case REQUEST_STANDBY:
      case STANDBY:
      case REQUEST_ENTER_WOWL:
      case REQUEST_EXIT_WOWL:
      case WOWL:
         TLLOGW(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_WARN, "Not handle this events %d", newState ));
         break;

      default:
         TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR, "Not a valid event %d", newState ));
         break;
   }

   return;
}
/*==========================================================================
  FUNCTION      WLANTL_GetEtherType

  DESCRIPTION   Extract Ether type information from the BD

  DEPENDENCIES  NONE
    
  PARAMETERS    in aucBDHeader - BD header
                in vosDataBuff - data buffer
                in ucMPDUHLen  - MPDU header length
                out pUsEtherType - pointer to Ethertype

  RETURN VALUE  VOS_STATUS_SUCCESS : if the EtherType is successfully extracted
                VOS_STATUS_FAILURE : if the EtherType extraction failed and
                                     the packet was dropped

  SIDE EFFECTS  NONE
  
============================================================================*/
static VOS_STATUS WLANTL_GetEtherType
(
   v_U8_t               * aucBDHeader,
   vos_pkt_t            * vosDataBuff,
   v_U8_t                 ucMPDUHLen,
   v_U16_t              * pUsEtherType
)
{
  v_U8_t                   ucOffset;
  v_U16_t                  usEtherType = *pUsEtherType;
  v_SIZE_t                 usLLCSize = sizeof(usEtherType);
  VOS_STATUS               vosStatus  = VOS_STATUS_SUCCESS;
  
  /*------------------------------------------------------------------------
    Check if LLC is present - if not, TL is unable to determine type
   ------------------------------------------------------------------------*/
  if ( VOS_FALSE == WDA_IS_RX_LLC_PRESENT( aucBDHeader ) )
  {
    ucOffset = WLANTL_802_3_HEADER_LEN - sizeof(usEtherType); 
  }
  else
  {
    ucOffset = ucMPDUHLen + WLANTL_LLC_PROTO_TYPE_OFFSET;
  }

  /*------------------------------------------------------------------------
    Extract LLC type 
  ------------------------------------------------------------------------*/
  vosStatus = vos_pkt_extract_data( vosDataBuff, ucOffset, 
                                    (v_PVOID_t)&usEtherType, &usLLCSize); 

  if (( VOS_STATUS_SUCCESS != vosStatus ) || 
      ( sizeof(usEtherType) != usLLCSize ))
      
  {
    TLLOGE(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
               "WLAN TL:Error extracting Ether type from data packet"));
    /* Drop packet */
    vos_pkt_return_packet(vosDataBuff);
    vosStatus = VOS_STATUS_E_FAILURE;
  }
  else
  {
    TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
               "WLAN TL:Ether type retrieved before endianess conv: %d", 
               usEtherType));

    usEtherType = vos_be16_to_cpu(usEtherType);
    *pUsEtherType = usEtherType;

    TLLOG2(VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
               "WLAN TL:Ether type retrieved: %d", usEtherType));
  }
  
  return vosStatus;
}

#ifdef WLAN_SOFTAP_FEATURE
/*==========================================================================
  FUNCTION      WLANTL_GetSoftAPStatistics

  DESCRIPTION   Collect the cumulative statistics for all Softap stations

  DEPENDENCIES  NONE
    
  PARAMETERS    in pvosGCtx  - Pointer to the global vos context
                   bReset    - If set TL statistics will be cleared after reading
                out statsSum - pointer to collected statistics

  RETURN VALUE  VOS_STATUS_SUCCESS : if the Statistics are successfully extracted

  SIDE EFFECTS  NONE

============================================================================*/
VOS_STATUS WLANTL_GetSoftAPStatistics(v_PVOID_t pAdapter, WLANTL_TRANSFER_STA_TYPE *statsSum, v_BOOL_t bReset)
{
    v_U8_t i = 0;
    VOS_STATUS  vosStatus = VOS_STATUS_SUCCESS;
    WLANTL_CbType *pTLCb  = VOS_GET_TL_CB(pAdapter);
    WLANTL_TRANSFER_STA_TYPE statBufferTemp;
    vos_mem_zero((v_VOID_t *)&statBufferTemp, sizeof(WLANTL_TRANSFER_STA_TYPE));
    vos_mem_zero((v_VOID_t *)statsSum, sizeof(WLANTL_TRANSFER_STA_TYPE));


    if ( NULL == pTLCb )
    {
       return VOS_STATUS_E_FAULT;
    } 

    // Sum up all the statistics for stations of Soft AP from TL
    for (i = 0; i < WLAN_MAX_STA_COUNT; i++)
    {
        if (pTLCb->atlSTAClients[i].wSTADesc.wSTAType == WLAN_STA_SOFTAP)
        {
           vosStatus = WLANTL_GetStatistics(pAdapter, &statBufferTemp, i);// Can include staId 1 because statistics not collected for it

           if (!VOS_IS_STATUS_SUCCESS(vosStatus))
                return VOS_STATUS_E_FAULT;

            // Add to the counters
           statsSum->txUCFcnt += statBufferTemp.txUCFcnt;
           statsSum->txMCFcnt += statBufferTemp.txMCFcnt;
           statsSum->txBCFcnt += statBufferTemp.txBCFcnt;
           statsSum->txUCBcnt += statBufferTemp.txUCBcnt;
           statsSum->txMCBcnt += statBufferTemp.txMCBcnt;
           statsSum->txBCBcnt += statBufferTemp.txBCBcnt;
           statsSum->rxUCFcnt += statBufferTemp.rxUCFcnt;
           statsSum->rxMCFcnt += statBufferTemp.rxMCFcnt;
           statsSum->rxBCFcnt += statBufferTemp.rxBCFcnt;
           statsSum->rxUCBcnt += statBufferTemp.rxUCBcnt;
           statsSum->rxMCBcnt += statBufferTemp.rxMCBcnt;
           statsSum->rxBCBcnt += statBufferTemp.rxBCBcnt;

           if (bReset)
           {
              vosStatus = WLANTL_ResetStatistics(pAdapter, i);
              if (!VOS_IS_STATUS_SUCCESS(vosStatus))
                return VOS_STATUS_E_FAULT;               
          }
        }
    }

    return vosStatus;
}
#endif
#ifdef ANI_CHIPSET_VOLANS
/*===============================================================================
  FUNCTION      WLANTL_IsReplayPacket
     
  DESCRIPTION   This function does replay check for valid stations
 
  DEPENDENCIES  Validity of replay check must be done before the function 
                is called
                          
  PARAMETERS    currentReplayCounter    current replay counter taken from RX BD 
                previousReplayCounter   previous replay counter taken from TL CB
                                       
  RETRUN        VOS_TRUE    packet is a replay packet
                VOS_FALSE   packet is not a replay packet

  SIDE EFFECTS   none
 ===============================================================================*/
v_BOOL_t
WLANTL_IsReplayPacket
(
  v_U64_t    ullcurrentReplayCounter,
  v_U64_t    ullpreviousReplayCounter
)
{
   /* Do the replay check by comparing previous received replay counter with
      current received replay counter*/
    if(ullpreviousReplayCounter < ullcurrentReplayCounter)
    {
        /* Valid packet not replay */
        return VOS_FALSE;
    }
    else
    {

        /* Current packet number is less than or equal to previuos received 
           packet no, this means current packet is replay packet */
        VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
        "WLAN TL: Replay packet found with replay counter :[0x%llX]",ullcurrentReplayCounter);
           
        return VOS_TRUE;
    }
}

#if 0
/*===============================================================================
  FUNCTION      WLANTL_GetReplayCounterFromRxBD
     
  DESCRIPTION   This function extracts 48-bit replay packet number from RX BD 
 
  DEPENDENCIES  Validity of replay check must be done before the function 
                is called
                          
  PARAMETERS    pucRxHeader pointer to RX BD header
                                       
  RETRUN        v_U64_t    Packet number extarcted from RX BD

  SIDE EFFECTS   none
 ===============================================================================*/
v_U64_t
WLANTL_GetReplayCounterFromRxBD
(
   v_U8_t *pucRxBDHeader
)
{
/* 48-bit replay counter is created as follows
   from RX BD 6 byte PMI command:
   Addr : AES/TKIP
   0x38 : pn3/tsc3
   0x39 : pn2/tsc2
   0x3a : pn1/tsc1
   0x3b : pn0/tsc0

   0x3c : pn5/tsc5
   0x3d : pn4/tsc4 */

#ifdef ANI_BIG_BYTE_ENDIAN
    v_U64_t ullcurrentReplayCounter = 0;
    /* Getting 48-bit replay counter from the RX BD */
    ullcurrentReplayCounter = WLANHAL_RX_BD_GET_PMICMD_20TO23(pucRxBDHeader); 
    ullcurrentReplayCounter <<= 16;
    ullcurrentReplayCounter |= (( WLANHAL_RX_BD_GET_PMICMD_24TO25(pucRxBDHeader) & 0xFFFF0000) >> 16);
    return ullcurrentReplayCounter;
#else
    v_U64_t ullcurrentReplayCounter = 0;
    /* Getting 48-bit replay counter from the RX BD */
    ullcurrentReplayCounter = (WLANHAL_RX_BD_GET_PMICMD_24TO25(pucRxBDHeader) & 0x0000FFFF); 
    ullcurrentReplayCounter <<= 32; 
    ullcurrentReplayCounter |= WLANHAL_RX_BD_GET_PMICMD_20TO23(pucRxBDHeader); 
    return ullcurrentReplayCounter;
#endif
}
#endif
#endif

/*===============================================================================
  FUNCTION      WLANTL_PostResNeeded
     
  DESCRIPTION   This function posts message to TL to reserve BD/PDU memory
 
  DEPENDENCIES  None
                          
  PARAMETERS    pvosGCtx
                                       
  RETURN        None

  SIDE EFFECTS   none
 ===============================================================================*/

void WLANTL_PostResNeeded(v_PVOID_t pvosGCtx)
{
  vos_msg_t            vosMsg;

  vosMsg.reserved = 0;
  vosMsg.bodyptr  = NULL;
  vosMsg.type     = WLANTL_TX_RES_NEEDED;
  VOS_TRACE( VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_INFO_HIGH,
        "WLAN TL: BD/PDU available interrupt received, Posting message to TL");
  if(!VOS_IS_STATUS_SUCCESS(vos_tx_mq_serialize( VOS_MQ_ID_TL, &vosMsg)))
  {
    VOS_TRACE(VOS_MODULE_ID_TL, VOS_TRACE_LEVEL_ERROR,
       " %s fails to post message", __FUNCTION__);
  }
}

/*===============================================================================
  FUNCTION       WLANTL_UpdateRssiBmps

  DESCRIPTION    This function updates the TL's RSSI (in BMPS mode)

  DEPENDENCIES   None

  PARAMETERS

    pvosGCtx         VOS context          VOS Global context
    staId            Station ID           Station ID
    rssi             RSSI (BMPS mode)     RSSI in BMPS mode

  RETURN         None

  SIDE EFFECTS   none
 ===============================================================================*/

void WLANTL_UpdateRssiBmps(v_PVOID_t pvosGCtx, v_U8_t staId, v_S7_t rssi)
{
  WLANTL_CbType* pTLCb = VOS_GET_TL_CB(pvosGCtx);

  if (NULL != pTLCb)
  {
    pTLCb->atlSTAClients[staId].rssiAvgBmps = rssi;
  }
}
