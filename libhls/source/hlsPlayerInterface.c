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
 * @file hlsPlayerInterface.c @date February 9, 2012
 *
 * @author Patryk Prus (pprus@cisco.com)
 *
 * Implements interface from HLS sessions back to external HLS
 * player that loaded the plugin.  These functions tie into the
 * #srcPlayerFunc_t function table passed as a parameter to
 * srcPluginLoad() and the callbacks registered via
 * hlsPlugin_registerCB().
 *
 * @addtogroup hlsPlayerInterface HLS Player Interface
 * @{
 *
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "debug.h"

#include "hlsTypes.h"

/** The global plugin instance defined in hlsPlugin.c */
extern hlsPlugin_t thePlugin;

/**
 * Passes session events to external player
 *
 * @param pHandle - unique session handle passed by player in
 *                hlsPlugin_open() call
 * @param pEvt - pointer to #srcPluginEvt_t containing the
 *             plugin event
 */
void hlsPlayer_pluginEvtCallback(void* pHandle, srcPluginEvt_t* pEvt)
{
    if(thePlugin.pluginEvtCallback != NULL)
    {
        thePlugin.pluginEvtCallback(pHandle, pEvt);
    }
}

/**
 * Passes session errors to external player
 *
 * @param pHandle - unique session handle passed by player in
 *                hlsPlugin_open() call
 * @param pErr - pointer to #srcPluginErr_t containing the
 *             plugin error
 */
void hlsPlayer_pluginErrCallback(void* pHandle, srcPluginErr_t* pErr)
{
    if(thePlugin.pluginErrCallback != NULL)
    {
        thePlugin.pluginErrCallback(pHandle, pErr);
    }
}

/**
 * Registers session callback for player events
 *
 * @param pHandle - unique session handle passed by player in
 *                hlsPlugin_open() call
 * @param evtCb - #playerEvtCallback_t player event callback
 *              function
 *
 * @return #srcStatus_t
 */
srcStatus_t hlsPlayer_registerCB(void* pHandle, playerEvtCallback_t evtCb)
{
    if(thePlugin.pHlsPlayerFuncTable->registerCB == NULL)
    {
        ERROR("pHlsPlayerFuncTable->registerCB is NULL");
        return SRC_ERROR;
    }

    return thePlugin.pHlsPlayerFuncTable->registerCB(pHandle, evtCb);
}

/**
 * Passes session buffer request to external player
 *
 * @param pHandle - unique session handle passed by player in
 *                hlsPlugin_open() call
 * @param buffer - pointer which will be set to the beginning of
 *               the returned buffer
 * @param size - pointer to integer which will be set to the
 *             size of the returned buffer
 *
 * @pre
 *       - session has been created via hlsPlugin_open()
 *
 * @post
 *       - none
 *
 * @return #srcStatus_t
 */
srcStatus_t hlsPlayer_getBuffer(void* pHandle, char** buffer, int* size, void **ppPrivate)
{
    if(thePlugin.pHlsPlayerFuncTable->getBuffer == NULL)
    {
        ERROR("pHlsPlayerFuncTable->getBuffer is NULL");
        return SRC_ERROR;
    }

    return thePlugin.pHlsPlayerFuncTable->getBuffer(pHandle, buffer, size, ppPrivate);
}

/**
 * Sends full session buffer to external player
 *
 * @param pHandle - unique session handle passed by player in
 *                hlsPlugin_open() call
 * @param buffer - the data buffer to send
 * @param size - amount of data written into buffer (can be
 *             smaller than the actual size of the buffer)
 * @param metadata - pointer to #srcBufferMetadata_t providing
 *                 various information related to this chunk of
 *                 data
 *
 * @pre
 *       - session has been created via hlsPlugin_open()
 *       - buffer MUST be the result of a successful
 *         hlsPlayer_getBuffer() call
 *
 * @post
 *       - none
 *
 * @return #srcStatus_t
 */
srcStatus_t hlsPlayer_sendBuffer(void* pHandle, char* buffer, int size, srcBufferMetadata_t* metadata, void *pPrivate)
{
    if(thePlugin.pHlsPlayerFuncTable->sendBuffer == NULL)
    {
        ERROR("pHlsPlayerFuncTable->sendBuffer is NULL");
        return SRC_ERROR;
    }

    return thePlugin.pHlsPlayerFuncTable->sendBuffer(pHandle, buffer, size, metadata, pPrivate);
}

/**
 * Passes session set request to external player
 *
 * @param pHandle - unique session handle passed by player in
 *                hlsPlugin_open() call
 * @param pSetData - pointer to #srcPlayerSetData_t containing
 *                 the setting to set on the player
 *
 * @pre
 *       - session has been created via hlsPlugin_open()
 *
 * @post
 *       - none
 *
 * @return #srcStatus_t
 */
srcStatus_t hlsPlayer_set(void *pHandle, srcPlayerSetData_t *pSetData)
{
    if(thePlugin.pHlsPlayerFuncTable->set == NULL)
    {
        ERROR("pHlsPlayerFuncTable->set is NULL");
        return SRC_ERROR;
    }

    return thePlugin.pHlsPlayerFuncTable->set(pHandle, pSetData);
}

/**
 * Passes session get request to external player
 *
 * @param pHandle - unique session handle passed by player in
 *                hlsPlugin_open() call
 * @param pGetData - pointer to #srcPlayerGetData_t containing
 *                 the setting to get from the player
 *
 * @pre
 *       - session has been created via hlsPlugin_open()
 *
 * @post
 *       - none
 *
 * @return #srcStatus_t
 */
srcStatus_t hlsPlayer_getOption(void *pHandle, srcPlayerGetData_t *pGetData)
{
    if(thePlugin.pHlsPlayerFuncTable->get == NULL)
    {
        ERROR("pHlsPlayerFuncTable->get is NULL");
        return SRC_ERROR;
    }

    return thePlugin.pHlsPlayerFuncTable->get(pHandle, pGetData);
}

#ifdef __cplusplus
}
#endif

/**
 *
 * @}
 */
