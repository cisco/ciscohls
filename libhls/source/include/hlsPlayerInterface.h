#ifndef HLSPLAYERINTERFACE_H
#define HLSPLAYERINTERFACE_H


/* ****************************************************************************
*
*                   Copyright 2012 Cisco Systems, Inc.
*
*                              CHS Engineering
*                           5030 Sugarloaf Parkway
*                               P.O. Box 465447
*                          Lawrenceville, GA 30042
*
*                        Proprietary and Confidential
*              Unauthorized distribution or copying is prohibited
*                            All rights reserved
*
* No part of this computer software may be reprinted, reproduced or utilized
* in any form or by any electronic, mechanical, or other means, now known or
* hereafter invented, including photocopying and recording, or using any
* information storage and retrieval system, without permission in writing
* from Cisco Systems, Inc.
*
******************************************************************************/

/**
 * @file hlsPlayerInterface.h @date February 9, 2012
 * 
 * @author Patryk Prus (pprus@cisco.com) 
 *
 * Defines new types used by HLS plugin
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "hlsTypes.h"

void hlsPlayer_pluginEvtCallback(void* pHandle, srcPluginEvt_t* pEvt);
void hlsPlayer_pluginErrCallback(void* pHandle, srcPluginErr_t* pErr);
srcStatus_t hlsPlayer_registerCB(void* pHandle, playerEvtCallback_t evtCb);
srcStatus_t hlsPlayer_getBuffer(void* pHandle, char** buffer, int* size, void **ppPrivate);
srcStatus_t hlsPlayer_sendBuffer(void* pHandle, char* buffer, int size, srcBufferMetadata_t* metadata, void *pPrivate);
srcStatus_t hlsPlayer_set(void *pHandle, srcPlayerSetData_t *pSetData);
srcStatus_t hlsPlayer_get(void *pHandle, srcPlayerGetData_t *pGetData);

#ifdef __cplusplus
}
#endif

#endif // HLSPLAYERINTERFACE_H


