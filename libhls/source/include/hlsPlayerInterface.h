#ifndef HLSPLAYERINTERFACE_H
#define HLSPLAYERINTERFACE_H
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


