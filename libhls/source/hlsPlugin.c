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
 * @file hlsPlugin.c @date February 9, 2012 
 *  
 * @author Patryk Prus (pprus@cisco.com)  
 *
 * Implements the HLS source plugin interface defined by 
 * sourcePlugin.h 
 *
 * @addtogroup hlsPlugin HLS Plugin
 * @{
 *
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>

#include "sourcePlugin.h"

#include "debug.h"
#include "hlsTypes.h"

#include "hlsPlugin.h"

#include "hlsSession.h"

#include "hlsPlayerInterface.h"

#include "curlUtils.h"

/*! Global plugin instance */
hlsPlugin_t thePlugin;

/**
 * Load the source plugin
 *
 * This API is called by the player to load a given source 
 * plugin.  Each plugin MUST expose a function of this type 
 * named srcPluginLoad(). 
 *
 * @param pluginTable - pointer to empty #srcPluginFunc_t to be 
 *                    populated by the plugin with appropriate
 *                    functions
 * @param playerTable - pointer to #srcPlayerFunc_t populated 
 *                    with function pointers to valid player
 *                    functions
 * @param pErr - Pointer to #srcPluginErr_t error structure if 
 *             applicable.  May be NULL.
 *  
 * @pre 
 *       - none
 *  
 * @post 
 *       - pluginTable populated with valid plugin functions, or
 *         NULL
 *
 * @return #srcStatus_t
 */
srcStatus_t srcPluginLoad(srcPluginFunc_t* pluginTable, srcPlayerFunc_t* playerTable, srcPluginErr_t* pErr)
{
    srcStatus_t rval = SRC_SUCCESS;
#if 0

    DIR *pDir = NULL;
    struct dirent *pDirEntry = NULL;
    char* filename = NULL;
#endif

    /* Open the logging module/file */
    OPEN_LOG;

    if((pluginTable == NULL) || (playerTable == NULL)) 
    {
        ERROR("invalid paramater");

        if(pErr != NULL) 
        {
            pErr->errCode = SRC_PLUGIN_ERR_INVALID_PARAM;
            snprintf(pErr->errMsg, SRC_ERR_MSG_LEN, DEBUG_MSG("invalid parameter"));
        }

        return SRC_ERROR;
    }

    do
    {
#if 0
  /* I'm bypassing all the dll load for this particular build.  I'm doing this because all I want to do is load
   * the HLS plugin and glue it into GST 
   */


        //rms DEBUG(DBG_INFO,"Loading HLS Plug-in version %s", ABR_VERSION);
         #warning FIX ME
        /* Clear out the HLS cache folder.  This needs to be removed and become the
           responsibility of the application. */
        pDir = opendir(LOCAL_PATH);
        if(pDir != NULL) 
        {
            while((pDirEntry = readdir(pDir)) != NULL) 
            {
                if(getFileName(pDirEntry->d_name, &filename, LOCAL_PATH) == HLS_OK)
                {
                    if(unlink(filename))
                    {
                        ERROR("unlink() failed on file %s -- %s", filename, strerror(errno));
                    }
    
                    free(filename);
                    filename = NULL;    
                }
            }

            closedir(pDir);
        }
        else
        {
            ERROR("opendir() failed on directory %s -- %s", LOCAL_PATH, strerror(errno));
        }

#endif 
        /* Initialize our plugin structure */
        memset(&thePlugin, 0, sizeof(hlsPlugin_t));

        /* Fill plugin function table */
        memset(pluginTable, 0, sizeof(*pluginTable));
        pluginTable->initialize = hlsPlugin_initialize;
        pluginTable->finalize = hlsPlugin_finalize;
        pluginTable->registerCB = hlsPlugin_registerCB;
        pluginTable->open = hlsPlugin_open;
        pluginTable->close = hlsPlugin_close;
        pluginTable->prepare = hlsPlugin_prepare;
        pluginTable->set = hlsPlugin_set;
        pluginTable->get = hlsPlugin_get;

        /* Save off player function table */
        thePlugin.pHlsPlayerFuncTable = malloc(sizeof(*playerTable));
        if(thePlugin.pHlsPlayerFuncTable == NULL) 
        {
            ERROR("malloc error");
            if(pErr != NULL) 
            {
                pErr->errCode = SRC_PLUGIN_ERR_GENERAL;
                snprintf(pErr->errMsg, SRC_ERR_MSG_LEN, DEBUG_MSG("malloc error"));
            }
            rval = SRC_ERROR;
            break;
        }
        memcpy(thePlugin.pHlsPlayerFuncTable, playerTable, sizeof(*playerTable));

    } while(0);

    return rval;
}

/**
 * Unload the source plugin
 *
 * This API is called by the player to unload a given source 
 * plugin. Each plugin MUST expose a function of this type 
 * named srcPluginUnload().
 *
 * @param pErr - Pointer to #srcPluginErr_t error structure if 
 *             applicable.  May be NULL.
 *  
 * @pre 
 *       - none
 *  
 * @post 
 *       - none
 *
 * @return #srcStatus_t
 */
srcStatus_t srcPluginUnload( srcPluginErr_t* pErr )
{
    srcStatus_t rval = SRC_SUCCESS;

    free(thePlugin.pHlsPlayerFuncTable);
    thePlugin.pHlsPlayerFuncTable = NULL;

    /* Close the logging module/file */
    CLOSE_LOG;

    return rval;
}

/**
 * Initialization function for the HLS plugin
 *
 * This API is called once the plugin is loaded with srcPluginLoad() to initialize the plugin.  This must be 
 * called prior to any other calls to the plugin.
 *
 * @param pErr - Pointer to #srcPluginErr_t error structure if 
 *             applicable.  May be NULL.
 *  
 * @pre 
 *       - plugin loaded via srcPluginLoad() 
 *  
 * @post 
 *       - plugin is initialized 
 *  
 * @return #srcStatus_t
 */
srcStatus_t hlsPlugin_initialize(srcPluginErr_t* pErr)
{   
    srcStatus_t rval = SRC_SUCCESS;

    do
    {
        DEBUG(DBG_INFO,"initializing HLS plugin");

        /* Check if we're already initialized */
        if(thePlugin.bInitialized) 
        {
            ERROR("HLS plugin already initialized");
            if(pErr != NULL) 
            {
                pErr->errCode = SRC_PLUGIN_ERR_INITIALIZED;
                snprintf(pErr->errMsg, SRC_ERR_MSG_LEN, DEBUG_MSG("HLS plugin already initialized"));
            }
            rval = SRC_ERROR;
            break;
        }

        /* Set initialized flag */
        thePlugin.bInitialized = 1;

    } while(0);

    return rval;
}

/**
 * Finalize function for the source plugin
 *
 * This API is called to shutdown the source plugin.  Once this 
 * is called no other calls to the plugin are allowed. 
 *
 * @param pErr - Pointer to #srcPluginErr_t error structure if 
 *             applicable.  May be NULL.
 *  
 * @pre 
 *       - plugin initialized via hlsPlugin_initialize()
 *  
 * @post 
 *       - all plugin sessions terminated and all plugin
 *         resources freed
 *  
 * @return #srcStatus_t
 */
srcStatus_t hlsPlugin_finalize(srcPluginErr_t* pErr)
{
    srcStatus_t rval = SRC_SUCCESS;
    int i;

    DEBUG(DBG_INFO,"finalizing HLS plugin");

    do
    {
        /* Make sure we've been initialized */
        if(!(thePlugin.bInitialized))
        {
            ERROR("HLS plugin not initialized");
            if(pErr != NULL) 
            {
                pErr->errCode = SRC_PLUGIN_ERR_NOT_INITIALIZED;
                snprintf(pErr->errMsg, SRC_ERR_MSG_LEN, DEBUG_MSG("HLS plugin not initialized"));
            }
            rval = SRC_ERROR;
            break;
        }

        /* Close any active sessions */
        for(i = 0; i < MAX_SESSIONS; i++) 
        {
            if(thePlugin.hlsSessions[i] != NULL) 
            {
                rval = hlsPlugin_close(thePlugin.hlsSessions[i], pErr);
                if(rval != SRC_SUCCESS) 
                {
                    break;
                }
            }
        }
        if(rval != SRC_SUCCESS) 
        {
            if(pErr != NULL) 
            {
                pErr->errCode = SRC_PLUGIN_ERR_FINALIZE_FAILED;
                snprintf(pErr->errMsg, SRC_ERR_MSG_LEN, DEBUG_MSG("failed to close all active sessions"));
            }
            break;
        }
    
        if(thePlugin.activeSessions != 0) 
        {
            ERROR("failed to close all active sessions");
            if(pErr != NULL) 
            {
                pErr->errCode = SRC_PLUGIN_ERR_FINALIZE_FAILED;
                snprintf(pErr->errMsg, SRC_ERR_MSG_LEN, DEBUG_MSG("failed to close all active sessions"));
            }
            rval = SRC_ERROR;
            break;
        }
    
        thePlugin.activeSessions = 0;
        thePlugin.pluginErrCallback = NULL;
        thePlugin.pluginEvtCallback = NULL;
        thePlugin.bInitialized = 0;
        
    } while(0);

    return rval;
}
   
/**
 * Register callbacks with plugin
 *
 * This API is called by the player to register callbacks with the plugin for asynchronous events
 * and errors.
 *
 * @param evtCb - #pluginEvtCallback_t event callback function 
 * @param errCb - #pluginErrCallback_t error callback function 
 * @param pErr  - Pointer to #srcPluginErr_t error structure if 
 *              applicable.  May be NULL.
 *  
 * @pre 
 *       - plugin initialized via hlsPlugin_initialize() 
 *  
 * @post 
 *       - plugin sessions will use provided functions to signal
 *         asynchronous events and errors
 *
 * @return #srcStatus_t
 */
srcStatus_t hlsPlugin_registerCB(pluginEvtCallback_t evtCb, pluginErrCallback_t errCb, srcPluginErr_t* pErr)
{   
    srcStatus_t rval = SRC_SUCCESS;

    if((evtCb == NULL) || (errCb == NULL)) 
    {
        ERROR("invalid paramater");
        if(pErr != NULL) 
        {
            pErr->errCode = SRC_PLUGIN_ERR_INVALID_PARAM;
            snprintf(pErr->errMsg, SRC_ERR_MSG_LEN, DEBUG_MSG("invalid parameter"));
        }
        return SRC_ERROR;
    }

    do
    {
        DEBUG(DBG_INFO,"registering plugin callbacks");

        /* Make sure we've been initialized */
        if(!(thePlugin.bInitialized))
        {
            ERROR("HLS plugin not initialized");
            if(pErr != NULL) 
            {
                pErr->errCode = SRC_PLUGIN_ERR_NOT_INITIALIZED;
                snprintf(pErr->errMsg, SRC_ERR_MSG_LEN, DEBUG_MSG("HLS plugin not initialized"));
            }
            rval = SRC_ERROR;
            break;
        }

        /* Save off the callbacks */
        thePlugin.pluginErrCallback = errCb;
        thePlugin.pluginEvtCallback = evtCb;

    } while(0);

    return rval;
}

/**
 * Opens a session with the plugin
 *
 * This API is called by the player to create a new playback session with the plugin.
 *
 * @param pSessionId - Session Id created by plugin.  Used by 
 *                   player in all other API calls.
 * @param pHandle - Opaque handle returned by the player. Plugin
 *                must use this when calling player.
 * @param pErr - Pointer to #srcPluginErr_t error structure if 
 *             applicable. May be NULL.
 *  
 * @pre 
 *       - plugin initialized via hlsPlugin_initialize() 
 *  
 * @post 
 *       - plugin session created 
 *       - callback for player events registered via
 *         hlsPlayer_registerCB()
 *       - sessionId points to a unique session identfier
 *  
 * @return #srcStatus_t
 */
srcStatus_t hlsPlugin_open(srcSessionId_t *pSessionId, void* pHandle, srcPluginErr_t* pErr)
{   
    srcStatus_t rval = SRC_SUCCESS;
    hlsStatus_t status = HLS_OK;

    int sessionIndex = 0;

    if(pHandle == NULL) 
    {
        ERROR("invalid paramater");
        if(pErr != NULL) 
        {
            pErr->errCode = SRC_PLUGIN_ERR_INVALID_PARAM;
            snprintf(pErr->errMsg, SRC_ERR_MSG_LEN, DEBUG_MSG("invalid parameter"));
        }
        return SRC_ERROR;
    }

    do
    {
        /* Make sure we've been initialized */
        if(!(thePlugin.bInitialized))
        {
            ERROR("HLS plugin not initialized");
            if(pErr != NULL) 
            {
                pErr->errCode = SRC_PLUGIN_ERR_NOT_INITIALIZED;
                snprintf(pErr->errMsg, SRC_ERR_MSG_LEN, DEBUG_MSG("HLS plugin not initialized"));
            }
            rval = SRC_ERROR;
            break;
        }

        /* Get an open session slot */
        sessionIndex = getSessionIndex(NULL);

        /* Make sure we can support another session */
        if(sessionIndex == -1)
        {
            ERROR("no more HLS sessions available");
            if(pErr != NULL) 
            {
                pErr->errCode = SRC_PLUGIN_ERR_NO_SESSION_AVAILABLE;
                snprintf(pErr->errMsg, SRC_ERR_MSG_LEN, DEBUG_MSG("no more HLS sessions available"));
            }
            rval = SRC_ERROR;
            break;
        }
        
        status = hlsSession_init(&(thePlugin.hlsSessions[sessionIndex]), pHandle);
        if(status != HLS_OK) 
        {
            ERROR("hlsSession_init failed with status: %d", status);
            if(pErr != NULL) 
            {
                pErr->errCode = SRC_PLUGIN_ERR_SESSION_RESOURCE_FAILED;
                snprintf(pErr->errMsg, SRC_ERR_MSG_LEN, DEBUG_MSG("hlsSession_init failed with status: %d", status));
            }
            rval = SRC_ERROR;
            break;
        }
        
        thePlugin.activeSessions++;
        
        *pSessionId = (srcSessionId_t)(thePlugin.hlsSessions[sessionIndex]);

        /* Register session callback with player */
        status = hlsPlayer_registerCB(pHandle, hlsPlugin_playerEvtCallback);
        if(status != HLS_OK) 
        {
            ERROR("registerCB failed with status: %d", status);
            if(pErr != NULL) 
            {
                pErr->errCode = SRC_PLUGIN_ERR_GENERAL;
                snprintf(pErr->errMsg, SRC_ERR_MSG_LEN, DEBUG_MSG("registerCB failed with status: %d", status));
            }
            rval = SRC_ERROR;
            break;
        }

        DEBUG(DBG_INFO,"opened session %p at index %d", (void*)*pSessionId, sessionIndex);

    } while(0);

    return rval;
}

/**
 * Closes a session with the plugin
 *
 * This API is called by the player to destroy an existing session.
 *
 * @param sessionId - Session to release.
 * @param pErr - Pointer to #srcPluginErr_t error structure if 
 *             applicable.  May be NULL.
 *  
 * @pre 
 *       - plugin initialized via hlsPlugin_initialize() 
 *       - session specified by sessionId has been created via
 *         hlsPlugin_open()
 *  
 * @post 
 *       - session closed and all session resources released
 *  
 * @return #srcStatus_t
 */
srcStatus_t hlsPlugin_close(srcSessionId_t sessionId, srcPluginErr_t* pErr)
{
    srcStatus_t rval = SRC_SUCCESS;

    int sessionIndex = 0;

    if(sessionId == NULL)
    {
        ERROR("invalid paramater");
        if(pErr != NULL) 
        {
            pErr->errCode = SRC_PLUGIN_ERR_INVALID_PARAM;
            snprintf(pErr->errMsg, SRC_ERR_MSG_LEN, DEBUG_MSG("invalid parameter"));
        }
        return SRC_ERROR;
    }

    do
    {
        /* Make sure we've been initialized */
        if(!(thePlugin.bInitialized))
        {
            ERROR("HLS plugin not initialized");
            if(pErr != NULL) 
            {
                pErr->errCode = SRC_PLUGIN_ERR_NOT_INITIALIZED;
                snprintf(pErr->errMsg, SRC_ERR_MSG_LEN, DEBUG_MSG("HLS plugin not initialized"));
            }
            rval = SRC_ERROR;
            break;
        }

        sessionIndex = getSessionIndex(sessionId);

        /* Make sure we were called on a valid session */
        if(sessionIndex == -1)
        {
            ERROR("invalid session: %p", (void*)sessionId);
            if(pErr != NULL) 
            {
                pErr->errCode = SRC_PLUGIN_ERR_INVALID_SESSION;
                snprintf(pErr->errMsg, SRC_ERR_MSG_LEN, DEBUG_MSG("invalid session: %p", (void*)sessionId));
            }
            rval = SRC_ERROR;
            break;
        }
        
        /* Terminate the session */
        hlsSession_term(thePlugin.hlsSessions[sessionIndex]);

        DEBUG(DBG_INFO,"closed session %p", (void*)sessionId);

        thePlugin.hlsSessions[sessionIndex] = NULL;
        thePlugin.activeSessions--;

    } while(0);

    return rval;
}

/**
 * Prepare the session and its associated URL
 *
 * This API is called by the player to indicate to the plugin that playback on the session is about to be started.  The plugin can
 * use this to perform any addition setup, pre-fetch content, etc.  Operation may also be a no-op depending on the plugin implementation.
 *
 * @param sessionId - Session to prepare
 * @param pErr - Pointer to #srcPluginErr_t error structure if 
 *             applicable.  May be NULL.
 *  
 * @pre 
 *       - plugin initialized via hlsPlugin_initialize() 
 *       - session specified by sessionId has been created via
 *         hlsPlugin_open()
 *       - source URL set using SRC_PLUGIN_SET_DATA_SOURCE
 *         option of hlsPlugin_set() function
 *  
 * @post 
 *       - session is ready to begin playback 
 *  
 * @return #srcStatus_t
 */
srcStatus_t hlsPlugin_prepare( srcSessionId_t sessionId, srcPluginErr_t* pErr )
{
    srcStatus_t rval = SRC_SUCCESS;
    hlsStatus_t status = HLS_OK;

    int sessionIndex = 0;

    if(sessionId == NULL)
    {
        ERROR("invalid paramater");
        if(pErr != NULL) 
        {
            pErr->errCode = SRC_PLUGIN_ERR_INVALID_PARAM;
            snprintf(pErr->errMsg, SRC_ERR_MSG_LEN, DEBUG_MSG("invalid parameter"));
        }
        return SRC_ERROR;
    }

    do
    {
        /* Make sure we've been initialized */
        if(!(thePlugin.bInitialized))
        {
            ERROR("HLS plugin not initialized");
            if(pErr != NULL) 
            {
                pErr->errCode = SRC_PLUGIN_ERR_NOT_INITIALIZED;
                snprintf(pErr->errMsg, SRC_ERR_MSG_LEN, DEBUG_MSG("HLS plugin not initialized"));
            }
            rval = SRC_ERROR;
            break;
        }

        sessionIndex = getSessionIndex(sessionId);

        /* Make sure we were called on a valid session */
        if(sessionIndex == -1)
        {
            ERROR("invalid session: %p", (void*)sessionId);
            if(pErr != NULL) 
            {
                pErr->errCode = SRC_PLUGIN_ERR_INVALID_SESSION;
                snprintf(pErr->errMsg, SRC_ERR_MSG_LEN, DEBUG_MSG("invalid session: %p", (void*)sessionId));
            }
            rval = SRC_ERROR;
            break;
        }
        
        DEBUG(DBG_INFO,"%s on session %p", __FUNCTION__, (void*)sessionId);

        /* prepare on the session */
        status = hlsSession_prepare(thePlugin.hlsSessions[sessionIndex]);
        if(status != HLS_OK) 
        {
            ERROR("hlsSession_prepare failed on session %p with status: %d", (void*)sessionId, status);
            if(pErr != NULL) 
            {
                pErr->errCode = SRC_PLUGIN_ERR_GENERAL;
                snprintf(pErr->errMsg, SRC_ERR_MSG_LEN, DEBUG_MSG("hlsSession_prepare failed on session %p with status: %d", (void*)sessionId, status));
            }
            rval = SRC_ERROR;
            break;
        }

    } while(0);

    return rval;
}

/**
 * Set a desired value in the plugin
 *
 * This API is called by the player to set settings in the 
 * plugin. 
 *
 * @param sessionId - Session to send the request to 
 * @param pSetData  - Pointer to #srcPluginSetData_t which 
 *                  contains the data to set
 * @param pErr - Pointer to #srcPluginErr_t error structure if 
 *             applicable.  May be NULL.
 *  
 * @pre 
 *       - plugin initialized via hlsPlugin_initialize() 
 *       - session specified by sessionId has been created via
 *         hlsPlugin_open()
 *       - SRC_PLUGIN_SET_DATA_SOURCE -- session has NOT been
 *         prepared via hlsPlugin_prepare()
 *       - SRC_PLUGIN_SET_SPEED -- session has been prepared
 *         via hlsPlugin_prepare()
 *       - SRC_PLUGIN_SET_POSITION -- session has been
 *         prepared via hlsPlugin_prepare()
 *       - SRC_PLUGIN_SET_TARGET_BITRATE -- session has not
 *         started playback
 * 
 * @post 
 *       - SRC_PLUGIN_SET_DATA_SOURCE -- session will use
 *         provided URL as the data source
 *       - SRC_PLUGIN_SET_SPEED -- session will play content at
 *         the requested speed
 *       - SRC_PLUGIN_SET_POSITION -- session will start
 *         playback from the requested position at the same
 *         speed as before this hlsPlugin_set() call
 *       - SRC_PLUGIN_SET_MAX_BITRATE -- session will only fetch
 *         data at bitrates below this limit, if possible
 *       - SRC_PLUGIN_SET_MIN_BITRATE -- session will only fetch
 *         data at bitrates above this limit, if possible
 *       - SRC_PLUGIN_SET_TARGET_BITRATE -- session will begin
 *         playback at the bitrate closest to this target
 *         bitrate
 *
 * @return #srcStatus_t
 */
srcStatus_t hlsPlugin_set(srcSessionId_t sessionId, srcPluginSetData_t* pSetData, srcPluginErr_t* pErr)
{
    srcStatus_t rval = SRC_SUCCESS;
    hlsStatus_t status = HLS_OK;

    int sessionIndex = 0;

    if((sessionId == NULL) || (pSetData == NULL) || (pSetData->pData == NULL))
    {
        ERROR("invalid paramater");
        if(pErr != NULL) 
        {
            pErr->errCode = SRC_PLUGIN_ERR_INVALID_PARAM;
            snprintf(pErr->errMsg, SRC_ERR_MSG_LEN, DEBUG_MSG("invalid parameter"));
        }
        return SRC_ERROR;
    }

    do
    {
        /* Make sure we've been initialized */
        if(!(thePlugin.bInitialized))
        {
            ERROR("HLS plugin not initialized");
            if(pErr != NULL) 
            {
                pErr->errCode = SRC_PLUGIN_ERR_NOT_INITIALIZED;
                snprintf(pErr->errMsg, SRC_ERR_MSG_LEN, DEBUG_MSG("HLS plugin not initialized"));
            }
            rval = SRC_ERROR;
            break;
        }

        sessionIndex = getSessionIndex(sessionId);

        /* Make sure we were called on a valid session */
        if(sessionIndex == -1)
        {
            ERROR("invalid session: %p", (void*)sessionId);
            if(pErr != NULL) 
            {
                pErr->errCode = SRC_PLUGIN_ERR_INVALID_SESSION;
                snprintf(pErr->errMsg, SRC_ERR_MSG_LEN, DEBUG_MSG("invalid session: %p", (void*)sessionId));
            }
            rval = SRC_ERROR;
            break;
        }

        switch(pSetData->setCode) 
        {
            case SRC_PLUGIN_SET_DATA_SOURCE:
                DEBUG(DBG_INFO,"setting URL = %s for session %p", (char*)(pSetData->pData), (void *)sessionId);

                /* setDataSource on the session */
                status = hlsSession_setDataSource(thePlugin.hlsSessions[sessionIndex], (char*)(pSetData->pData));
                if(status != HLS_OK) 
                {
                    ERROR("hlsSession_setDataSource failed on session %p with status: %d", (void*)sessionId, status);
                    if(pErr != NULL) 
                    {
                        pErr->errCode = SRC_PLUGIN_ERR_GENERAL;
                        snprintf(pErr->errMsg, SRC_ERR_MSG_LEN, DEBUG_MSG("hlsSession_setDataSource failed on session %p with status: %d", (void*)sessionId, status));
                    }
                    rval = SRC_ERROR;
                    break;
                }
                break;
            case SRC_PLUGIN_SET_SPEED:
                DEBUG(DBG_INFO,"setting speed = %f on session %p", *(float*)(pSetData->pData), (void*)sessionId);
        
		        /* Call the session setSpeed function */
        		status = hlsSession_setSpeed(thePlugin.hlsSessions[sessionIndex], *(float*)(pSetData->pData));
                if(status == HLS_UNSUPPORTED)
                {
                    ERROR("setting speed %f on session %p not supported by this stream at this time", *(float*)(pSetData->pData), (void*)sessionId);
                    if(pErr != NULL) 
            		{
                		pErr->errCode = SRC_PLUGIN_ERR_UNSUPPORTED;
                		snprintf(pErr->errMsg, SRC_ERR_MSG_LEN, DEBUG_MSG("setting speed %f on session %p not supported by this stream at this time", *(float*)(pSetData->pData), (void*)sessionId));
            		}
            		rval = SRC_ERROR;
            		break;
                }
                else if(status != HLS_OK) 
        		{
            		ERROR("hlsSession_setSpeed failed on session %p with status: %d", (void*)sessionId, status);
            		if(pErr != NULL) 
            		{
                		pErr->errCode = SRC_PLUGIN_ERR_GENERAL;
                		snprintf(pErr->errMsg, SRC_ERR_MSG_LEN, DEBUG_MSG("hlsSession_setSpeed failed on session %p with status: %d", (void*)sessionId, status));
            		}
            		rval = SRC_ERROR;
            		break;
        		}
                break;
            case SRC_PLUGIN_SET_POSITION:
               DEBUG(DBG_INFO,"seeking to position = %f on session %p", *(float*)(pSetData->pData), (void*)sessionId);

               /* Call the session seek function */
               status = hlsSession_seek(thePlugin.hlsSessions[sessionIndex], *(float*)(pSetData->pData));
               if(status == HLS_UNSUPPORTED)
               {
                  ERROR("setting position %f on session %p not supported by this stream at this time", *(float*)(pSetData->pData), (void*)sessionId);
                  if(pErr != NULL)
                  {
                     pErr->errCode = SRC_PLUGIN_ERR_UNSUPPORTED;
                     snprintf(pErr->errMsg, SRC_ERR_MSG_LEN, DEBUG_MSG("setting position %f on session %p not supported by this stream at this time", *(float*)(pSetData->pData), (void*)sessionId));
                  }
                  rval = SRC_ERROR;
                  break;
               }
               else if(status != HLS_OK)
               {
                  ERROR("hlsSession_seek failed on session %p with status: %d", (void*)sessionId, status);
                  if(pErr != NULL)
                  {
                     pErr->errCode = SRC_PLUGIN_ERR_GENERAL;
                     snprintf(pErr->errMsg, SRC_ERR_MSG_LEN, DEBUG_MSG("hlsSession_seek failed on session %p with status: %d", (void*)sessionId, status));
                  }
                  rval = SRC_ERROR;
                  break;
               }
               else
               {
                  DEBUG(DBG_INFO, "resetting EOF count");
                  thePlugin.hlsSessions[sessionIndex]->eofCount = 0;
               }
               break;
            case SRC_PLUGIN_SET_MAX_BITRATE:
                DEBUG(DBG_INFO,"setting max bitrate = %d on session %p", *(int*)(pSetData->pData), (void*)sessionId);
        
                /* setBitrateRange on the session */
                status = hlsSession_setBitrateLimit(thePlugin.hlsSessions[sessionIndex], BR_LIMIT_MAX, *(int*)(pSetData->pData));
                if(status != HLS_OK) 
                {
                    ERROR("hlsSession_setBitrateLimit failed on session %p with status: %d", (void*)sessionId, status);
                    if(pErr != NULL) 
                    {
                        pErr->errCode = SRC_PLUGIN_ERR_GENERAL;
                        snprintf(pErr->errMsg, SRC_ERR_MSG_LEN, DEBUG_MSG("hlsSession_setBitrateLimit failed on session %p with status: %d", (void*)sessionId, status));
                    }
                    rval = SRC_ERROR;
                    break;
                }
                break;
            case SRC_PLUGIN_SET_MIN_BITRATE:
                DEBUG(DBG_INFO,"setting min bitrate = %d on session %p", *(int*)(pSetData->pData), (void*)sessionId);
        
                /* setBitrateRange on the session */
                status = hlsSession_setBitrateLimit(thePlugin.hlsSessions[sessionIndex], BR_LIMIT_MIN, *(int*)(pSetData->pData));
                if(status != HLS_OK) 
                {
                    ERROR("hlsSession_setBitrateLimit failed on session %p with status: %d", (void*)sessionId, status);
                    if(pErr != NULL) 
                    {
                        pErr->errCode = SRC_PLUGIN_ERR_GENERAL;
                        snprintf(pErr->errMsg, SRC_ERR_MSG_LEN, DEBUG_MSG("hlsSession_setBitrateLimit failed on session %p with status: %d", (void*)sessionId, status));
                    }
                    rval = SRC_ERROR;
                    break;
                }
                break;
            case SRC_PLUGIN_SET_TARGET_BITRATE:
                DEBUG(DBG_INFO,"setting target bitrate = %d on session %p", *(int*)(pSetData->pData), (void*)sessionId);
        
                /* setBitrateRange on the session */
                status = hlsSession_setBitrateLimit(thePlugin.hlsSessions[sessionIndex], BR_LIMIT_TARGET, *(int*)(pSetData->pData));
                if(status != HLS_OK) 
                {
                    ERROR("hlsSession_setBitrateLimit failed on session %p with status: %d", (void*)sessionId, status);
                    if(pErr != NULL) 
                    {
                        pErr->errCode = SRC_PLUGIN_ERR_GENERAL;
                        snprintf(pErr->errMsg, SRC_ERR_MSG_LEN, DEBUG_MSG("hlsSession_setBitrateLimit failed on session %p with status: %d", (void*)sessionId, status));
                    }
                    rval = SRC_ERROR;
                    break;
                }
                break;
            default:
                ERROR("unknown srcPlayerSetCode_t value: %d", pSetData->setCode);
                if(pErr != NULL) 
                {
                    pErr->errCode = SRC_PLUGIN_ERR_GENERAL;
                    snprintf(pErr->errMsg, SRC_ERR_MSG_LEN, DEBUG_MSG("unknown srcPlayerSetCode_t value: %d", pSetData->setCode));
                }
                rval = SRC_ERROR;
                break;
        }
        if(rval != SRC_SUCCESS) 
        {
            break;
        }

    } while(0);

    return rval;
}

/**
 * Retrieve a desired value from the plugin
 *
 * This API is called by the player to get information from the 
 * plugin. 
 *
 * @param sessionId - Session to send the request to 
 * @param pGetData - Pointer to #srcPluginGetData_t which 
 *                 contains the data to get
 * @param pErr - Pointer to #srcPluginErr_t error structure if 
 *             applicable.  May be NULL.
 *  
 * @pre 
 *       - plugin initialized via hlsPlugin_initialize() 
 *       - session specified by sessionId has been created via
 *         hlsPlugin_open()
 *       - session has been prepared via hlsPlugin_prepare()
 *       - pGetData->pData has been allocated to hold the
 *         requested data
 *  
 * @post 
 *       - on SRC_SUCCESS, pGetData->pData contains the desired
 *         information
 *
 * @return #srcStatus_t
 */
srcStatus_t hlsPlugin_get(srcSessionId_t sessionId, srcPluginGetData_t* pGetData, srcPluginErr_t* pErr)
{
    srcStatus_t rval = SRC_SUCCESS;
    hlsStatus_t status = HLS_OK;

    int sessionIndex = 0;

    if((sessionId == NULL) || (pGetData == NULL) || (pGetData->pData == NULL))
    {
        ERROR("invalid paramater");
        if(pErr != NULL) 
        {
            pErr->errCode = SRC_PLUGIN_ERR_INVALID_PARAM;
            snprintf(pErr->errMsg, SRC_ERR_MSG_LEN, DEBUG_MSG("invalid parameter"));
        }
        return SRC_ERROR;
    }

    do
    {
        /* Make sure we've been initialized */
        if(!(thePlugin.bInitialized))
        {
            ERROR("HLS plugin not initialized");
            if(pErr != NULL) 
            {
                pErr->errCode = SRC_PLUGIN_ERR_NOT_INITIALIZED;
                snprintf(pErr->errMsg, SRC_ERR_MSG_LEN, DEBUG_MSG("HLS plugin not initialized"));
            }
            rval = SRC_ERROR;
            break;
        }

        sessionIndex = getSessionIndex(sessionId);

        /* Make sure we were called on a valid session */
        if(sessionIndex == -1)
        {
            ERROR("invalid session: %p", (void*)sessionId);
            if(pErr != NULL) 
            {
                pErr->errCode = SRC_PLUGIN_ERR_INVALID_SESSION;
                snprintf(pErr->errMsg, SRC_ERR_MSG_LEN, DEBUG_MSG("invalid session: %p", (void*)sessionId));
            }
            rval = SRC_ERROR;
            break;
        }

        switch(pGetData->getCode) 
        {
            case SRC_PLUGIN_GET_NUM_BITRATES:
                DEBUG(DBG_INFO,"getting number of bitrates for session %p", (void*)sessionId);
        
                /* getNumBitrates on the session */
                status = hlsSession_getNumBitrates(thePlugin.hlsSessions[sessionIndex], (int*)(pGetData->pData));
                if(status != HLS_OK) 
                {
                    ERROR("hlsSession_getNumBitrates failed on session %p with status: %d", (void*)sessionId, status);
                    if(pErr != NULL) 
                    {
                        pErr->errCode = SRC_PLUGIN_ERR_GENERAL;
                        snprintf(pErr->errMsg, SRC_ERR_MSG_LEN, DEBUG_MSG("hlsSession_getNumBitrates failed on session %p with status: %d", (void*)sessionId, status));
                    }
                    rval = SRC_ERROR;
                    break;
                }
                break;
            case SRC_PLUGIN_GET_BITRATES:
                DEBUG(DBG_INFO,"getting bitrates for session %p", (void*)sessionId);
        
                /* getBitrates on the session */
                status = hlsSession_getBitrates(thePlugin.hlsSessions[sessionIndex], *(int*)(pGetData->pData), (int*)(pGetData->pData));
                if(status != HLS_OK) 
                {
                    ERROR("hlsSession_getBitrates failed on session %p with status: %d", (void*)sessionId, status);
                    if(pErr != NULL) 
                    {
                        pErr->errCode = SRC_PLUGIN_ERR_GENERAL;
                        snprintf(pErr->errMsg, SRC_ERR_MSG_LEN, DEBUG_MSG("hlsSession_getBitrates failed on session %p with status: %d", (void*)sessionId, status));
                    }
                    rval = SRC_ERROR;
                    break;
                }
                break;
            case SRC_PLUGIN_GET_CURRENT_BITRATE:
                DEBUG(DBG_INFO,"getting current bitrate for session %p", (void*)sessionId);

                /* getBitrates on the session */
                status = hlsSession_getCurrentBitrate(thePlugin.hlsSessions[sessionIndex], (int*)(pGetData->pData));
                if(status != HLS_OK) 
                {
                    ERROR("hlsSession_getCurrentBitrate failed on session %p with status: %d", (void*)sessionId, status);
                    if(pErr != NULL) 
                    {
                        pErr->errCode = SRC_PLUGIN_ERR_GENERAL;
                        snprintf(pErr->errMsg, SRC_ERR_MSG_LEN, DEBUG_MSG("hlsSession_getCurrentBitrate failed on session %p with status: %d", (void*)sessionId, status));
                    }
                    rval = SRC_ERROR;
                    break;
                }
                break;
            case SRC_PLUGIN_GET_DURATION:
                DEBUG(DBG_INFO,"getting duration for session %p", (void*)sessionId);
        
                /* getDuration on the session */
                status = hlsSession_getDuration(thePlugin.hlsSessions[sessionIndex], (float*)(pGetData->pData));
                if(status != HLS_OK) 
                {
                    ERROR("hlsSession_getDuration failed on session %p with status: %d", (void*)sessionId, status);
                    if(pErr != NULL) 
                    {
                        pErr->errCode = SRC_PLUGIN_ERR_GENERAL;
                        snprintf(pErr->errMsg, SRC_ERR_MSG_LEN, DEBUG_MSG("hlsSession_getDuration failed on session %p with status: %d", (void*)sessionId, status));
                    }
                    rval = SRC_ERROR;
                    break;
                }
                break;
            case SRC_PLUGIN_GET_POSITION:
                DEBUG(DBG_INFO,"getting current position for session %p", (void*)sessionId);
        
                /* getCurrentPosition on the session */
                status = hlsSession_getCurrentPosition(thePlugin.hlsSessions[sessionIndex], (float*)(pGetData->pData));
                if(status != HLS_OK) 
                {
                    ERROR("hlsSession_getCurrentPosition failed on session %p with status: %d", (void*)sessionId, status);
                    if(pErr != NULL) 
                    {
                        pErr->errCode = SRC_PLUGIN_ERR_GENERAL;
                        snprintf(pErr->errMsg, SRC_ERR_MSG_LEN, DEBUG_MSG("hlsSession_getCurrentPosition failed on session %p with status: %d", (void*)sessionId, status));
                    }
                    rval = SRC_ERROR;
                    break;
                }
                break;
            case SRC_PLUGIN_GET_SPEED:
                DEBUG(DBG_INFO,"getting current speed for session %p", (void*)sessionId);
        
                status = hlsSession_getSpeed(thePlugin.hlsSessions[sessionIndex], (float*)(pGetData->pData));
                if(status != HLS_OK) 
                {
                    ERROR("hlsSession_getSpeed failed on session %p with status: %d", (void*)sessionId, status);
                    if(pErr != NULL) 
                    {
                        pErr->errCode = SRC_PLUGIN_ERR_GENERAL;
                        snprintf(pErr->errMsg, SRC_ERR_MSG_LEN, DEBUG_MSG("hlsSession_getSpeed failed on session %p with status: %d", (void*)sessionId, status));
                    }
                    rval = SRC_ERROR;
                    break;
                }
                break;
            case SRC_PLUGIN_GET_TRICK_SUPPORTED:
                DEBUG(DBG_INFO,"getting trick supported for session %p", (void*)sessionId);
        
                status = hlsSession_getTrickSupported(thePlugin.hlsSessions[sessionIndex], (int*)(pGetData->pData));
                if(status != HLS_OK) 
                {
                    ERROR("hlsSession_getTrickSupported failed on session %p with status: %d", (void*)sessionId, status);
                    if(pErr != NULL) 
                    {
                        pErr->errCode = SRC_PLUGIN_ERR_GENERAL;
                        snprintf(pErr->errMsg, SRC_ERR_MSG_LEN, DEBUG_MSG("hlsSession_getTrickSupported failed on session %p with status: %d", (void*)sessionId, status));
                    }
                    rval = SRC_ERROR;
                    break;
                }
                break;
            case SRC_PLUGIN_GET_CONTENT_TYPE:
                {
                   hlsContentType_t contentType = HLS_UNSPECIFIED;
                   DEBUG(DBG_INFO,"getting content type for session %p", (void*)sessionId);

                   status = hlsSession_getContentType(thePlugin.hlsSessions[sessionIndex], &contentType);
                   if(status != HLS_OK) 
                   {
                      ERROR("hlsSession_getContentType failed on session %p with status: %d", (void*)sessionId, status);
                      if(pErr != NULL) 
                      {
                         pErr->errCode = SRC_PLUGIN_ERR_GENERAL;
                         snprintf(pErr->errMsg, SRC_ERR_MSG_LEN, 
                                  DEBUG_MSG("hlsSession_getContentType failed on session %p with status: %d", 
                                  (void*)sessionId, status));
                      }
                      rval = SRC_ERROR;
                      break;
                   }
                   else
                   {
                      if(HLS_EVENT == contentType)
                      {
                         *((hlsContentType_t *)pGetData->pData) = SRC_PLUGIN_CONTENT_TYPE_LIVE;
                      }
                      else
                      {
                         *((hlsContentType_t *)pGetData->pData) = SRC_PLUGIN_CONTENT_TYPE_VOD;
                      }
                   }
                }
                break;
            default:
                ERROR("unknown srcPlayerGetCode_t value: %d", pGetData->getCode);
                if(pErr != NULL) 
                {
                    pErr->errCode = SRC_PLUGIN_ERR_GENERAL;
                    snprintf(pErr->errMsg, SRC_ERR_MSG_LEN, DEBUG_MSG("unknown srcPlayerGetCode_t value: %d", pGetData->getCode));
                }
                rval = SRC_ERROR;
                break;
        }
        if(rval != SRC_SUCCESS) 
        {
            break;
        }

    } while(0);

    return rval;
}

/** 
 * Sends player events to the correct plugin session.
 * 
 * @param sessionId - Session to send the event to 
 * @param pEvt - pointer to #srcPlayerEvt_t containing the 
 *             player event
 *  
 * @pre 
 *       - plugin initialized via hlsPlugin_initialize() 
 *       - session specified by sessionId has been created via
 *         hlsPlugin_open()
 *  
 * @post 
 *       - none
 */
void hlsPlugin_playerEvtCallback(srcSessionId_t sessionId, srcPlayerEvt_t* pEvt)
{
    int sessionIndex = -1;

    /* Make sure we've been initialized */
    if(thePlugin.bInitialized)
    {
        sessionIndex = getSessionIndex(sessionId);

        /* Make sure we were called on a valid session */
        if(sessionIndex != -1)
        {
            hlsSession_playerEvtCallback((hlsSession_t*)sessionId, pEvt);
        }
    }
}

/** 
 * Helper function which translates a srcSessionId_t into an 
 * index in the plugin structure's hlsPlugin_t::hlsSessions 
 * array. 
 * 
 * @param sessionId - the desired session Id
 * 
 * @return int - the index in the hlsPlugin_t::hlsSessions array 
 *         that sessionId is located at; -1 on failure
 */
int getSessionIndex(srcSessionId_t sessionId)
{
    int rval = -1;

    int i = 0;
    
    for(i = 0; i < MAX_SESSIONS; i++) 
    {
        DEBUG(DBG_NOISE,"Looking for sessionId %p, found %p", (void *)sessionId, (void *)(thePlugin.hlsSessions[i]));

        if(sessionId == (srcSessionId_t)(thePlugin.hlsSessions[i])) 
        {
            rval = i;
            break;
        }
    }

    return rval;
}

#ifdef __cplusplus
}
#endif

/**
 *
 * @}
 */
