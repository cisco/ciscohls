#ifndef HLSPLUGIN_H
#define HLSPLUGIN_H


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
 * @file hlsPlugin.h @date February 9, 2012
 * 
 * @author Patryk Prus (pprus@cisco.com) 
 *
 * Defines new types used by HLS plugin
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "sourcePlugin.h"

srcStatus_t srcPluginLoad(srcPluginFunc_t* pluginTable, srcPlayerFunc_t* playerTable, srcPluginErr_t* pErr);
srcStatus_t srcPluginUnload(srcPluginErr_t* pErr);

srcStatus_t hlsPlugin_initialize( srcPluginErr_t* pErr );
srcStatus_t hlsPlugin_finalize( srcPluginErr_t* pErr );
   
srcStatus_t hlsPlugin_registerCB( pluginEvtCallback_t evtCb, pluginErrCallback_t errCb, srcPluginErr_t* pErr );
   
srcStatus_t hlsPlugin_open( srcSessionId_t* pSessionId, void* pHandle, srcPluginErr_t* pErr );
srcStatus_t hlsPlugin_close( srcSessionId_t sessionId, srcPluginErr_t* pErr );

srcStatus_t hlsPlugin_prepare( srcSessionId_t sessionId, srcPluginErr_t* pErr );

srcStatus_t hlsPlugin_set(srcSessionId_t sessionId, srcPluginSetData_t* pSetData, srcPluginErr_t* pErr);
srcStatus_t hlsPlugin_get(srcSessionId_t sessionId, srcPluginGetData_t* pGetData, srcPluginErr_t* pErr);

void hlsPlugin_playerEvtCallback(srcSessionId_t sessionId, srcPlayerEvt_t* pEvt);

int getSessionIndex(srcSessionId_t sessionId);

#ifdef __cplusplus
}
#endif

#endif
