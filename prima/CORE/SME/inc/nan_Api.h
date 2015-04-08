/*
 * Copyright (c) 2014 Qualcomm Atheros, Inc.
 * All Rights Reserved.
 * Qualcomm Atheros Confidential and Proprietary.
 *
 */

/******************************************************************************
*
* Name:  nan_Api.h
*
* Description: NAN FSM defines.
*
******************************************************************************/

#ifndef __NAN_API_H__
#define __NAN_API_H__

#include "vos_types.h"
#include "halTypes.h"

typedef struct sNanRequestReq
{
    tANI_U16 request_data_len;
    const tANI_U8* request_data;
} tNanRequestReq, *tpNanRequestReq;

/******************************************************************************
 * Function: Pointer NanCallback
 *
 * Description:
 * this function pointer is used hold nan response callback. When ever driver
 * receives nan response, this callback will be used.
 *
 * Args:
 * first argument to pass hHal pointer and second argument
 * to pass the nan response data.
 *
 * Returns:
 * void
******************************************************************************/
typedef void (*NanCallback)(void*, tSirNanEvent*);

/******************************************************************************
 * Function: sme_NanRegisterCallback
 *
 * Description:
 * This function gets called when HDD wants register nan rsp callback with
 * sme layer.
 *
 * Args:
 * hHal and callback which needs to be registered.
 *
 * Returns:
 * void
******************************************************************************/
void sme_NanRegisterCallback(tHalHandle hHal, NanCallback callback);

/******************************************************************************
 * Function: sme_NanRequest
 *
 * Description:
 * This function gets called when HDD receives NAN vendor command
 * from userspace
 *
 * Args:
 * Nan Request structure ptr
 *
 * Returns:
 * VOS_STATUS
******************************************************************************/
VOS_STATUS sme_NanRequest(tpNanRequestReq input);

/******************************************************************************
  \fn sme_NanEvent

  \brief
  a callback function called when SME received eWNI_SME_NAN_EVENT
  event from WDA

  \param hHal - HAL handle for device
  \param pMsg - Message body passed from WDA; includes NAN header

  \return VOS_STATUS
******************************************************************************/
VOS_STATUS sme_NanEvent(tHalHandle hHal, void* pMsg);

#endif /* __NAN_API_H__ */
