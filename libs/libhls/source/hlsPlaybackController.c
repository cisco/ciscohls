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
 * @file hlsPlaybackController.c @date February 9, 2012
 *  
 * @author Patryk Prus (pprus@cisco.com) 
 *  
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "hlsPlayerInterface.h"
#include "hlsSession.h"
#include "hlsSessionUtils.h"
#include "hlsPlaybackController.h"

#include "debug.h"

/* Playback Control loop duration in nanoseconds */
#define PLAYBACK_CONTROL_LOOP_NSECS 500000000

/**
 * 
 * 
 * @param pSession - pointer to pre-allocated hlsSession_t
 */
void hlsPlaybackControllerThread(hlsSession_t* pSession)
{
    hlsStatus_t status = HLS_OK;

    int pthread_status = 0;

    struct timespec wakeTime;

    double playPosition = 0;

    int bRestartPlayback = 0;

    srcPluginEvt_t event;

    playbackControllerSignal_t* pSignal = NULL;

    int numMsgs = 0;

    llStatus_t llerror = LL_OK;

    int bWaitForPlaybackCompletion = 0;

    srcPluginErr_t error;

    if(pSession == NULL)
    {
        ERROR("pSession == NULL");
        pthread_exit(NULL);
    }

    TIMESTAMP(DBG_INFO,"Starting %s", __FUNCTION__);

    while(status == HLS_OK)
    {
        /* If the playback controller was signalled to exit, return HLS_CANCELLED */
        if(pSession->bKillPlaybackController) 
        {
            DEBUG(DBG_WARN, "playback controller signalled to stop");
            status = HLS_CANCELLED;
            break;
        }

        /* Get loop start time */
        if(clock_gettime(CLOCK_MONOTONIC, &wakeTime) != 0) 
        {
            ERROR("failed to get current time");
            status = HLS_ERROR;
            break;
        }

        /* If we've started playback and are currently paused on a stream with a floating start point,
         * we need to monitor to ensure that the current pause position doesn't roll off the playlist.
         * When the pause position starts nearing the top of the playlist, we need to kick the
         * player to restart playback
         */
        if(pSession->state == HLS_PLAYING)
        {
    
            /* Get playlist READ lock */
            pthread_rwlock_rdlock(&(pSession->playlistRWLock));
    
            /* Validate current playlist */
            if((pSession->pCurrentPlaylist == NULL) || 
               (pSession->pCurrentPlaylist->type != PL_MEDIA) || 
               (pSession->pCurrentPlaylist->pMediaData == NULL)) 
            {
                ERROR("current playlist invalid");
                status = HLS_ERROR;
                /* Release playlist lock */
                pthread_rwlock_unlock(&(pSession->playlistRWLock));
                break;
            }

            /* Are we paused on a stream with a floating start point? */
            if((pSession->speed == 0) && // PAUSED
               (pSession->pCurrentPlaylist->pMediaData->startOffset != 0)) // floating start point 
            {
                /* Get our current externally visible play position */
                status = getExternalPosition(pSession->pCurrentPlaylist, &playPosition);
                if(status != HLS_OK)
                {
                    ERROR("failed to get current position");
                    /* Release playlist lock */
                    pthread_rwlock_unlock(&(pSession->playlistRWLock));
                    break;
                }

                DEBUG(DBG_INFO,"Current position: %f", playPosition);
                            
                if(playPosition == 0) 
                {
                    /* Send SRC_PLUGIN_BOS message to player */
                    event.eventCode = SRC_PLUGIN_BOS;
                    event.pData = NULL;
                    hlsPlayer_pluginEvtCallback(pSession->pHandle, &event);

                    bRestartPlayback = 1;
                }

            }
    
            /* Release playlist lock */
            pthread_rwlock_unlock(&(pSession->playlistRWLock));
                
            if(bRestartPlayback) 
            {
                if(pSession->speed == 0) 
                {
                    DEBUG(DBG_WARN,"Paused at position %f -- attempting to restart playback", playPosition);

                    /* Set the player back into PLAY mode */
                    if(hlsSession_setSpeed(pSession, 1.0) != HLS_OK) 
                    {
                        DEBUG(DBG_WARN, "failed to force playback, will try again later");
                    }
                    else
                    {
                        bRestartPlayback = 0;
        
                        /* Send SRC_PLUGIN_FORCED_RESUME message to player */
                        event.eventCode = SRC_PLUGIN_FORCED_RESUME;
                        event.pData = NULL;
                        hlsPlayer_pluginEvtCallback(pSession->pHandle, &event);
                    }
                }
                else
                {
                    /* Someone changed the speed already, cancel speed change here. */
                    bRestartPlayback = 0;
                }
            }
        }

		numMsgs = 0;

        /* Check for new messages */
        llerror = getMsgCount(pSession->playbackControllerMsgQueue, &numMsgs);
        if(llerror != LL_OK) 
        {
            ERROR("failed to get message count");
            status = HLS_ERROR;
            break;
        }

        /* Do we have any messages? */
        while(numMsgs > 0) 
        {
            /* Pop off top message */
            llerror = popMsg(pSession->playbackControllerMsgQueue, (void**)(&pSignal));
            if(llerror != LL_OK) 
            {
                ERROR("failed to pop message from queue");
                status = HLS_ERROR;
                break;
            }

            numMsgs--;

            /* Process the message */
            switch(*pSignal) 
            {
                case PBC_DOWNLOAD_COMPLETE:
                    DEBUG(DBG_INFO, "got PBC_DOWNLOAD_COMPLETE");

                    /* Are we downloading regular segments or I-frames? */
                    if((pSession->speed >= 0) && (pSession->speed <= 1)) /* Regular playback */
                    {
                        DEBUG(DBG_INFO, "sending SRC_PLUGIN_EOF to player");
                        event.eventCode = SRC_PLUGIN_EOF;
                        event.pData = NULL;
                        hlsPlayer_pluginEvtCallback(pSession->pHandle, &event);

                        bWaitForPlaybackCompletion = 1;
                    }
                    else /* Trickplay */
                    {
                        /* Get playlist READ lock */
                        pthread_rwlock_rdlock(&(pSession->playlistRWLock));
    
                        /* Validate current playlist */
                        if((pSession->pCurrentPlaylist == NULL) || 
                           (pSession->pCurrentPlaylist->type != PL_MEDIA) || 
                           (pSession->pCurrentPlaylist->pMediaData == NULL)) 
                        {
                            ERROR("current playlist invalid");
                            status = HLS_ERROR;
                            /* Release playlist lock */
                            pthread_rwlock_unlock(&(pSession->playlistRWLock));
                            break;
                        }

                        /* Form player message */
                        if(pSession->speed > 0) // FF
                        {
                            /* Does the current playlist have a fixed end point? */
                            if(pSession->pCurrentPlaylist->pMediaData->bHaveCompletePlaylist)
                            {
                                /* Have fixed end point, send "end of file" */
                                DEBUG(DBG_INFO, "sending SRC_PLUGIN_EOF to player");
                                event.eventCode = SRC_PLUGIN_EOF;
                            }
                            else
                            {
                                /* Have floating end point, send "end of stream" */
                                DEBUG(DBG_INFO, "sending SRC_PLUGIN_EOS to player");
                                event.eventCode = SRC_PLUGIN_EOS;
                            }
                        }
                        else // REW
                        {
                            /* Do we have a fixed start point? */
                            if(pSession->pCurrentPlaylist->pMediaData->startOffset == 0)
                            {
                                /* Have fixed start point, send "beginning of file" */
                                DEBUG(DBG_INFO, "sending SRC_PLUGIN_BOF to player");
                                event.eventCode = SRC_PLUGIN_BOF;
                            }
                            else
                            {
                                /* Have floating start point, send "beginning of stream" */
                                DEBUG(DBG_INFO, "sending SRC_PLUGIN_BOS to player");
                                event.eventCode = SRC_PLUGIN_BOS;
                            }
                        }

                        /* Release playlist lock */
                        pthread_rwlock_unlock(&(pSession->playlistRWLock));

                        event.pData = NULL;
                        
                        /* Pause the player */
                        // TODO: should we quit if we fail, or just roll with it?
                        if(hlsSession_setSpeed(pSession, 0.0) != HLS_OK) 
                        {
                            DEBUG(DBG_WARN, "failed to pause playback");
                        }

                        /* Signal EOF/BOF to player */
                        hlsPlayer_pluginEvtCallback(pSession->pHandle, &event);
                    }
                    break;
                case PBC_PLAYER_AUDIO_UNDERRUN:
                    DEBUG(DBG_INFO, "got PBC_PLAYER_AUDIO_UNDERRUN");

                    /* We have received a PBC_DOWNLOAD_COMPLETE message during 1x PLAY
                       and are waiting for the player to play through everything
                       that it has buffered before sending an EOF signal */
                    if(bWaitForPlaybackCompletion) 
                    {
                        /* Pause the player */
                        // TODO: should we quit if we fail, or just roll with it?
                        if(hlsSession_setSpeed(pSession, 0.0) != HLS_OK) 
                        {
                            DEBUG(DBG_WARN, "failed to pause playback");
                        }
                            
                        /* Signal EOF to player */
                        /*
                        DEBUG(DBG_INFO, "sending SRC_PLUGIN_EOF to player");
                        event.eventCode = SRC_PLUGIN_EOF;
                        event.pData = NULL;
                        hlsPlayer_pluginEvtCallback(pSession->pHandle, &event);
                        */
            
                        bWaitForPlaybackCompletion = 0;
                    }
                    else
                    {
                       /* If we got this event and weren't waiting for playback to complete,
                          then the player is starving.  Not really anything we can do about this,
                          since the bitrate logic should already have us at the lowest bitrate...
                          Send a network error to the player */
                          error.errCode = SRC_PLUGIN_ERR_NETWORK;
                          snprintf(error.errMsg, SRC_ERR_MSG_LEN, DEBUG_MSG("session %p - player starving even @ lowest bitrate", pSession));
                          hlsPlayer_pluginErrCallback(pSession->pHandle, &error);
                    }
                    break;
                case PBC_STARTING_PLAYBACK:
                    DEBUG(DBG_INFO, "got PBC_STARTING_PLAYBACK");

                    /* hlsSession_play() has been called, and playback is starting. */

                    break;
                case PBC_STOPPING_PLAYBACK:
                    DEBUG(DBG_INFO, "got PBC_STOPPING_PLAYBACK");

                    /* hlsSession_stop() has been called and playback is stopping. */

                    /* stop() can be called in three instances:
                       1) player wants to stop playback
                       2) setSpeed() has been called
                       3) seek() has been called
                     
                       In all three cases we no longer need to wait for a PBC_PLAYER_AUDIO_UNDERRUN
                       to signify EOF, so reset the bWaitForPlaybackCompletion flag */
                    bWaitForPlaybackCompletion = 0;

                    break;
                default:
                    /* Ignore unknown signals */
                    DEBUG(DBG_WARN, "got unknown signal: %d", *pSignal);
                    break;
            }

            /* Free the message */
            free(pSignal);
            pSignal = NULL;
        }

        /* Sleep for the rest of the loop */

        /* Lock the playback controller wake mutex */
        if(pthread_mutex_lock(&(pSession->playbackControllerWakeMutex)) != 0)
        {
            ERROR("failed to lock playback controller wake mutex");
            status = HLS_ERROR;
            break;
        }
                        
        /* Wait for PLAYBACK_CONTROL_LOOP_NSECS before going again */
        wakeTime.tv_nsec += PLAYBACK_CONTROL_LOOP_NSECS;

        /* Handle a rollover of the nanosecond portion of wakeTime */
        while(wakeTime.tv_nsec >= 1000000000) 
        {
            wakeTime.tv_sec += 1;
            wakeTime.tv_nsec -= 1000000000;
        }
                    
        DEBUG(DBG_NOISE,"sleeping until: %f", ((wakeTime.tv_sec)*1.0) + (wakeTime.tv_nsec/1000000000.0));
                    
        /* Wait until wakeTime */
        pthread_status = PTHREAD_COND_TIMEDWAIT(&(pSession->playbackControllerWakeCond), &(pSession->playbackControllerWakeMutex), &wakeTime);
                    
        /* Unlock the playback controller wake mutex */
        if(pthread_mutex_unlock(&(pSession->playbackControllerWakeMutex)) != 0)
        {
            ERROR("failed to unlock playback controller wake mutex");
            status = HLS_ERROR;
            break;
        }
                    
        /* If the timedwait call failed we need to bail */
        if((pthread_status != ETIMEDOUT) && (pthread_status != 0))
        {
            ERROR("failed to timedwait on the playback controller wake condition");
            status = HLS_ERROR;
            break;
        }

        /* Make sure we're still in a valid state */
        if(pSession->state == HLS_INVALID_STATE) 
        {
            status = HLS_STATE_ERROR;
            break;
        }
    }

    if((status != HLS_OK) && (status != HLS_CANCELLED))
    {
        /* Send asynchronous error message to the player */
        srcPluginErr_t error;
        error.errCode = SRC_PLUGIN_ERR_SESSION_RESOURCE_FAILED;
        snprintf(error.errMsg, SRC_ERR_MSG_LEN, DEBUG_MSG("session %p playback controller thread quit with status: %d", pSession, status));
        hlsPlayer_pluginErrCallback(pSession->pHandle, &error);

        ERROR("session %p playback controller thread quit with status: %d", pSession, status);

        /* Put session in INVALID state */

        /* Block state changes */
        pthread_mutex_lock(&(pSession->stateMutex));

        pSession->state = HLS_INVALID_STATE;

        /* Leave critical section */
        pthread_mutex_unlock(&(pSession->stateMutex));
    }

    pSession->playbackControllerStatus = status;
    
    DEBUG(DBG_INFO,"session %p playback controller thread exiting with status %d", pSession, status);
    pthread_exit(NULL);
}

#ifdef __cplusplus
}
#endif
