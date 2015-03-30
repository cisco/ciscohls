#ifndef HLSPLUGIN_H
#define HLSPLUGIN_H
/*
    LIBBHLS
    Copyright (C) {2015}  {Cisco System}

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
    USA

    Contributing Authors: Saravanakumar Periyaswamy, Patryk Prus, Tankut Akgul

*/

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
