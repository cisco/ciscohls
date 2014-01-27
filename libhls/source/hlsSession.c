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
 * @file hlsSession.c @date February 9, 2012
 *  
 * @author Patryk Prus (pprus@cisco.com) 
 *  
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <string.h>
#include <stdlib.h>
#include <limits.h>

#include "hlsSession.h"
#include "hlsSessionUtils.h"

#include "hlsPlayerInterface.h"

#include "m3u8ParseUtils.h"

#include "hlsDownloaderUtils.h"

#include "m3u8Parser.h"
#include "hlsDownloader.h"
#include "hlsPlaybackController.h"

#include "curlUtils.h"

#include "debug.h"

/* 
 *  TODO: Add commentary about call flow...
 * 
 * 
 * 
 */ 

/**
 * Initializes hls streaming module.  *ppSession MUST be NULL, 
 * as it will be allocated and needs to be freed by the caller.
 * 
 * @param ppSession
 * 
 * @return #hlsStatus_t
 */
hlsStatus_t hlsSession_init(hlsSession_t** ppSession, void* pHandle)
{
    hlsStatus_t rval = HLS_OK;

    pthread_condattr_t condAttr;
    pthread_mutexattr_t mutexAttr;

    if((ppSession == NULL) || (pHandle == NULL) || (*ppSession != NULL))
    {
        ERROR("invalid parameter");
        return HLS_INVALID_PARAMETER;
    }

    TIMESTAMP(DBG_INFO,"%s", __FUNCTION__);

    do
    {
        /* Allocate new hlsSession */
        *ppSession = malloc(sizeof(hlsSession_t));
        if(*ppSession == NULL) 
        {
            ERROR("malloc error");
            rval = HLS_MEMORY_ERROR;
            break;
        }

        /* Initialize members */
        memset(*ppSession, 0, sizeof(hlsSession_t));

        (*ppSession)->maxBitrate = INT_MAX;
        (*ppSession)->lastPTS = -1ll;

        (*ppSession)->playbackControllerMsgQueue = newMsgQueue();
        if((*ppSession)->playbackControllerMsgQueue == NULL) 
        {
            ERROR("failed to allocate message queue");
            rval = HLS_MEMORY_ERROR;
            break;
        }

        /* Generate the session name.  We use the address of the
           hlsSession_t we allocated above.  Since pointers are longs,
           we need 2 bytes to display each byte of a long, plus 1 byte for '\0' */
        (*ppSession)->sessionName = malloc((2*sizeof(long))+1);
        if((*ppSession)->sessionName == NULL) 
        {
            ERROR("malloc error");
            rval = HLS_MEMORY_ERROR;
            break;
        }
        memset((*ppSession)->sessionName, 0, (2*sizeof(long))+1);
        sprintf((*ppSession)->sessionName, "%lx", (unsigned long)(*ppSession));

        /* Save parameters */
        (*ppSession)->pHandle = pHandle;

        /* Initialize player event mutex */
        if(pthread_mutex_init(&((*ppSession)->playerEvtMutex), NULL) != 0)
        {
            ERROR("failed to initialize player event mutex");
            rval = HLS_ERROR;
            break;
        }

        /* Initialize cURL mutex */
        if(pthread_mutex_init(&((*ppSession)->curlMutex), NULL) != 0)
        {
            ERROR("failed to initialize cURL mutex");
            rval = HLS_ERROR;
            break;
        }

        /* Initialize mutex attribute */
        if(pthread_mutexattr_init(&mutexAttr) != 0)
        {
            ERROR("failed to initialize mutex attribute");
            rval = HLS_ERROR;
            break;
        }

        /* Set the mutex type to RECURSIVE */
        if(pthread_mutexattr_settype(&mutexAttr, PTHREAD_MUTEX_RECURSIVE) != 0)
        {
            ERROR("failed to set type of mutex attribute");
            rval = HLS_ERROR;
            break;
        }

        /* Initialize RECURSIVE state mutex */
        if(pthread_mutex_init(&((*ppSession)->stateMutex), &mutexAttr) != 0)
        {
            ERROR("failed to initialize set mutex");
            rval = HLS_ERROR;
            break;
        }

        /* Initialize RECURSIVE set mutex */
        if(pthread_mutex_init(&((*ppSession)->setMutex), &mutexAttr) != 0)
        {
            ERROR("failed to initialize set mutex");
            rval = HLS_ERROR;
            break;
        }

/* Android Froyo doesn't support pthread_rwlock, so use a recursive mutex instead */
#ifdef ANDROID
        /* Initialize RECURSIVE playlist mutex */
        if(pthread_mutex_init(&((*ppSession)->playlistRWLock), &mutexAttr) != 0)
        {
            ERROR("failed to initialize playlist mutex");
            rval = HLS_ERROR;
            break;
        }
#else
        /* Initialize playlist rwlock */
        if(pthread_rwlock_init(&((*ppSession)->playlistRWLock), NULL) != 0)
        {
            ERROR("failed to initialize playlist rwlock");
            rval = HLS_ERROR;
            break;
        }
#endif

        /* Initialize pthread condition attribute */
        if(pthread_condattr_init(&condAttr) != 0) 
        {
            ERROR("failed to initialize condition attribute");
            rval = HLS_ERROR;
            break;
        }

/* Bionic C doesn't define this in pthread.h -- see definition of
   PTHREAD_COND_TIMEDWAIT in hlsTypes.h for more info */
#ifndef ANDROID
        /* Set the clock to use for pthread conditions to CLOCK_MONOTONIC */
        if(pthread_condattr_setclock(&condAttr, CLOCK_MONOTONIC) != 0) 
        {
            ERROR("failed to set clock on condition attribute");
            rval = HLS_ERROR;
            break;
        }
#endif

        /* Initialize parser thread wake mutex/condition */
        if(pthread_mutex_init(&((*ppSession)->parserWakeMutex), NULL) != 0)
        {
            ERROR("failed to initialize parser wake mutex");
            rval = HLS_ERROR;
            break;
        }
        if(pthread_cond_init(&((*ppSession)->parserWakeCond), &condAttr) != 0) 
        {
            ERROR("failed to initialize parser wake condition");
            rval = HLS_ERROR;
            break;
        }

        /* Initialize downloader thread wake mutex/condition */
        if(pthread_mutex_init(&((*ppSession)->downloaderWakeMutex), NULL) != 0)
        {
            ERROR("failed to initialize downloader wake mutex");
            rval = HLS_ERROR;
            break;
        }
        if(pthread_cond_init(&((*ppSession)->downloaderWakeCond), &condAttr) != 0) 
        {
            ERROR("failed to initialize downloader wake condition");
            rval = HLS_ERROR;
            break;
        }

        /* Initialize playback controller thread wake mutex/condition */
        if(pthread_mutex_init(&((*ppSession)->playbackControllerWakeMutex), NULL) != 0)
        {
            ERROR("failed to initialize playback controller wake mutex");
            rval = HLS_ERROR;
            break;
        }
        if(pthread_cond_init(&((*ppSession)->playbackControllerWakeCond), &condAttr) != 0) 
        {
            ERROR("failed to initialize playback controller wake condition");
            rval = HLS_ERROR;
            break;
        }

        /* Initialize our CURL handle */
        rval = curlInit(&((*ppSession)->pCurl));
        if(rval != HLS_OK) 
        {
            ERROR("failed to initialize CURL handle");
            break;
        }

    } while(0);

    pthread_condattr_destroy(&condAttr);

    if(!rval) 
    {
        (*ppSession)->state = HLS_INITIALIZED;
    }
    else
    {
        hlsSession_term(*ppSession);
        *ppSession = NULL;
    }

    return rval;
}

/**
 * Terminates and cleans up after hls streaming module.
 * 
 * @param pSession - handle to hlsSession object to terminate
 */
void hlsSession_term(hlsSession_t* pSession)
{
    DEBUG(DBG_INFO,"%s", __FUNCTION__);
    
    // TODO: remove eventually...
    hlsSession_printInfo(pSession);

    if(pSession != NULL) 
    {
        /* Kill all our threads */
        pSession->bKillDownloader = 1;
        pSession->bKillPlaybackController = 1;
        pSession->bKillParser = 1;

        /* Wake up downloader thread if it is sleeping */
        if(pthread_mutex_lock(&(pSession->downloaderWakeMutex)) == 0)
        {
            if(pthread_cond_signal(&(pSession->downloaderWakeCond)) == 0)
            {
                pthread_mutex_unlock(&(pSession->downloaderWakeMutex));
            }
        }

        /* Wake up playback controller thread if it is sleeping */
        if(pthread_mutex_lock(&(pSession->playbackControllerWakeMutex)) == 0)
        {
            if(pthread_cond_signal(&(pSession->playbackControllerWakeCond)) == 0)
            {
                pthread_mutex_unlock(&(pSession->playbackControllerWakeMutex));
            }
        }
        
        /* Wake up parser thread if it is sleeping */
        if(pthread_mutex_lock(&(pSession->parserWakeMutex)) == 0)
        {
            if(pthread_cond_signal(&(pSession->parserWakeCond)) == 0)
            {
                pthread_mutex_unlock(&(pSession->parserWakeMutex));
            }
        }
                
        if(pSession->downloader != 0) 
        {
            pthread_join(pSession->downloader, NULL);
            pSession->downloader = 0;
        }

        if(pSession->playbackController != 0) 
        {
            pthread_join(pSession->playbackController, NULL);
            pSession->playbackController= 0;
        }

        if(pSession->parser != 0) 
        {
            pthread_join(pSession->parser, NULL);
            pSession->parser= 0;
        }
       
        pthread_cond_destroy(&(pSession->parserWakeCond));
        pthread_mutex_destroy(&(pSession->parserWakeMutex));
        
        pthread_cond_destroy(&(pSession->playbackControllerWakeCond));
        pthread_mutex_destroy(&(pSession->playbackControllerWakeMutex));

        pthread_cond_destroy(&(pSession->downloaderWakeCond));
        pthread_mutex_destroy(&(pSession->downloaderWakeMutex));

        /* Put session in INVALID state */

        /* Block state changes */
        pthread_mutex_lock(&(pSession->stateMutex));

        pSession->state = HLS_INVALID_STATE;

        /* Leave critical section */
        pthread_mutex_unlock(&(pSession->stateMutex));

        pthread_mutex_destroy(&(pSession->stateMutex));
        pthread_mutex_destroy(&(pSession->playerEvtMutex));
        pthread_mutex_destroy(&(pSession->setMutex));
        pthread_mutex_destroy(&(pSession->curlMutex));

        pthread_rwlock_wrlock(&(pSession->playlistRWLock));

        pSession->pCurrentPlaylist = NULL;
        pSession->pCurrentProgram = NULL;

        if(pSession->playbackControllerMsgQueue != NULL) 
        {
            int msgCount = 0;
            void* pMessage = NULL;

            while((getMsgCount(pSession->playbackControllerMsgQueue, &msgCount) == LL_OK) && 
                  (msgCount > 0)) 
            {
                popMsg(pSession->playbackControllerMsgQueue, &pMessage);
                free(pMessage);
                pMessage = NULL;
            }

            freeMsgQueue(pSession->playbackControllerMsgQueue);
            pSession->playbackControllerMsgQueue = NULL;
        }

        freePlaylist(pSession->pPlaylist);
        pSession->pPlaylist = NULL;

        free(pSession->sessionName);
        pSession->sessionName = NULL;

        pthread_rwlock_unlock(&(pSession->playlistRWLock));
        pthread_rwlock_destroy(&(pSession->playlistRWLock));

        if(pSession->pCurl != NULL)
        {
            curlTerm(pSession->pCurl);
            pSession->pCurl = NULL;
        }

        free(pSession);
    }
}

/** 
 * Sets the top level playlist URL.
 *
 * playlistRWLock MUST NOT be held by the calling thread
 * 
 * @param pSession - handle to streamer object
 * @param playlistURL - text string of playlist URL
 * 
 * @return #hlsStatus_t
 */
hlsStatus_t hlsSession_setDataSource(hlsSession_t* pSession, char* playlistURL)
{
    hlsStatus_t rval = HLS_OK;

    if((playlistURL == NULL) || (pSession == NULL))
    {
        ERROR("invalid parameter");
        return HLS_INVALID_PARAMETER;
    }

    TIMESTAMP(DBG_INFO,"%s: playlistURL = %s", __FUNCTION__, playlistURL);

    /* Get playlist WRITE lock */
    pthread_rwlock_wrlock(&(pSession->playlistRWLock));

    do
    {
        /* Check for valid state */
        if(pSession->state != HLS_INITIALIZED) 
        {
            ERROR("%s invalid in state %d", __FUNCTION__, pSession->state);
            rval = HLS_STATE_ERROR;
            break;
        }

        /* Allocate a structure to hold our playlist, if we haven't yet done so */
        if(pSession->pPlaylist == NULL) 
        {
            pSession->pPlaylist = newHlsPlaylist();
            if(pSession->pPlaylist == NULL) 
            {
                ERROR("newHlsPlaylist() failed");
                rval = HLS_MEMORY_ERROR;
                break;
            }
        }
        else
        {
            /* If the playlist structure exists, free the
               old URL to make room for the new one */
            free(pSession->pPlaylist->playlistURL);
            pSession->pPlaylist->playlistURL = NULL;
        }
            
        /* Copy URL into our structure */
        pSession->pPlaylist->playlistURL = (char*)malloc(strlen(playlistURL)+1);
        if(pSession->pPlaylist->playlistURL == NULL) 
        {
            ERROR("malloc error");
            rval = HLS_MEMORY_ERROR;
            break;
        }
        memset(pSession->pPlaylist->playlistURL, 0, strlen(playlistURL)+1);
        strcpy(pSession->pPlaylist->playlistURL, playlistURL);

    } while(0);
    
    /* Clean up if we errored */
    if(rval && (pSession->pPlaylist != NULL))
    {
        free(pSession->pPlaylist->playlistURL);
        pSession->pPlaylist->playlistURL = NULL;
        free(pSession->pPlaylist);
        pSession->pPlaylist = NULL;
    }

    /* Release playlist lock */
    pthread_rwlock_unlock(&(pSession->playlistRWLock));

    return rval;
}

/**
 * Prepares initialized streamer for playback.  This will kick
 * off the parser thread and return once the parser has parsed
 * the top level manifest. 
 *  
 * playlistRWLock MUST NOT be held by the calling thread 
 * 
 * @param pSession - handle to streamer to prepare for playback
 * 
 * @return #hlsStatus_t
 */
hlsStatus_t hlsSession_prepare(hlsSession_t* pSession)
{
    hlsStatus_t rval = HLS_OK;

    struct timespec timeoutTime, currTime;

    if(pSession == NULL)
    {
        ERROR("invalid parameter");
        return HLS_INVALID_PARAMETER;
    }

    TIMESTAMP(DBG_INFO,"%s", __FUNCTION__);

    /* Block state changes */
    pthread_mutex_lock(&(pSession->stateMutex));

    do
    {
        /* If we've already been prepared, do nothing */
        if(pSession->state == HLS_PREPARED)
        {
            break;
        }

        /* Check for valid state */
        if(pSession->state != HLS_INITIALIZED) 
        {
            ERROR("%s invalid in state %d", __FUNCTION__, pSession->state);
            rval = HLS_STATE_ERROR;
            break;
        }

        /* Re-initialize */
        pSession->bKillPlaybackController = 0;
        pSession->playbackControllerStatus = HLS_OK;

        /* Start playback controller thread */
        if(pthread_create(&(pSession->playbackController), NULL, (void*)hlsPlaybackControllerThread, pSession))
        {
            ERROR("failed to create playback controller thread");
            rval = HLS_ERROR;
            break;
        }

        /* Re-initialize */
        pSession->bKillParser = 0;
        pSession->parserStatus = HLS_OK;

        /* Start parsing thread */
        if(pthread_create(&(pSession->parser), NULL, (void*)m3u8ParserThread, pSession))
        {
            ERROR("failed to create parser thread");
            rval = HLS_ERROR;
            break;
        }
    
        /* Get current time */
        if(clock_gettime(CLOCK_MONOTONIC, &timeoutTime) != 0) 
        {
            ERROR("failed to get current time");
            rval = HLS_ERROR;
            break;
        }

        /* Calculate the timeout time for moving to HLS_PREPARED */
        timeoutTime.tv_sec += HLS_PREPARED_TIMEOUT_SECS;
                    
        /* Wait for state change */
        while(pSession->state != HLS_PREPARED)
        {
            /* Get current time */
            if(clock_gettime(CLOCK_MONOTONIC, &currTime) != 0) 
            {
                ERROR("failed to get current time");
                rval = HLS_ERROR;
                break;
            }

            /* If HLS_PREPARED_TIMEOUT_SECS have passed and we are not yet HLS_PREPARED, return an error */
            if((currTime.tv_sec > timeoutTime.tv_sec) || ((currTime.tv_sec == timeoutTime.tv_sec) && (currTime.tv_nsec > timeoutTime.tv_nsec)))
            {
                ERROR("state change to HLS_PREPARED timed out");
                rval = HLS_ERROR;
                break;
            }

            /* If parser was told to stop, return error (since we never went to HLS_PREPARED) */
            if(pSession->bKillParser) 
            {
                ERROR("parser thread was killed");
                rval = HLS_ERROR;
                break;
            }
            
            /* If parser encountered an error, return the error to caller */
            if(pSession->parserStatus) 
            {
                ERROR("parser error");
                rval = pSession->parserStatus;
                break;
            }                        
        }
    
    } while(0);

    /* Leave critical section */
    pthread_mutex_unlock(&(pSession->stateMutex));

    /* If we failed to become prepared, kill the threads, if they were started */
    if(rval != HLS_OK) 
    {
        /* Kill the parser */
        pSession->bKillParser = 1;

        /* Kill the playback controller */
        pSession->bKillPlaybackController = 1;
        
        /* Wake up parser thread if it is sleeping */
        if(pthread_mutex_lock(&(pSession->parserWakeMutex)) == 0)
        {
            if(pthread_cond_signal(&(pSession->parserWakeCond)) == 0)
            {
                pthread_mutex_unlock(&(pSession->parserWakeMutex));
            }
        }

        /* Wake up playback controller thread if it is sleeping */
        if(pthread_mutex_lock(&(pSession->playbackControllerWakeMutex)) == 0)
        {
            if(pthread_cond_signal(&(pSession->playbackControllerWakeCond)) == 0)
            {
                pthread_mutex_unlock(&(pSession->playbackControllerWakeMutex));
            }
        }
                
        /* Wait for parser thread to exit */
        if(pSession->parser != 0) 
        {
            pthread_join(pSession->parser, NULL);
            pSession->parser = 0;
        }
                        
        /* Wait for playback controller thread to exit */
        if(pSession->playbackController != 0) 
        {
            pthread_join(pSession->playbackController, NULL);
            pSession->playbackController= 0;
        }
                    
        /* Make sure we end up back in INITIALIZED state if we didn't
           make it to PREPARED */

        /* Block state changes */
        pthread_mutex_lock(&(pSession->stateMutex));
    
        pSession->state = HLS_INITIALIZED;

        /* Leave critical section */
        pthread_mutex_unlock(&(pSession->stateMutex));
    }
    
    return rval;
}

/**
 * playlistRWLock MUST NOT be held by the calling thread 
 * 
 * @param pSession - handle to streamer to prepare for playback
 * 
 * @return #hlsStatus_t
 */
hlsStatus_t hlsSession_play(hlsSession_t* pSession)
{
    hlsStatus_t rval = HLS_OK;

    playbackControllerSignal_t* pSignal = NULL;

    llStatus_t llerror = LL_OK;

    struct timespec timeoutTime, currTime;

    if(pSession == NULL)
    {
        ERROR("invalid parameter");
        return HLS_INVALID_PARAMETER;
    }

    TIMESTAMP(DBG_INFO,"%s", __FUNCTION__);

    /* Block state changes */
    pthread_mutex_lock(&(pSession->stateMutex));

    do
    {
        /* If we're already playing, do nothing */
        if(pSession->state == HLS_PLAYING)
        {
            break;
        }

        /* Check for valid state */
        if(pSession->state != HLS_PREPARED) 
        {
            ERROR("%s invalid in state %d", __FUNCTION__, pSession->state);
            rval = HLS_STATE_ERROR;
            break;
        }

        /* Re-initialize */
        pSession->bKillDownloader = 0;
        pSession->downloaderStatus = HLS_OK;

        /* Start downloader thread */
        if(pthread_create(&(pSession->downloader), NULL, (void*)hlsDownloaderThread, pSession))
        {
            ERROR("failed to create downloader thread");
            rval = HLS_ERROR;
            break;
        }

        /* Signal that playback is starting to the playback controller thread */

        /* Allocate a new playback controller signal */
        pSignal = malloc(sizeof(playbackControllerSignal_t));
        if(pSignal == NULL) 
        {
            ERROR("malloc error");
            rval = HLS_ERROR;
            break;
        }

        *pSignal = PBC_STARTING_PLAYBACK;

        /* Push the message to the playback controller message queue */
        llerror = pushMsg(pSession->playbackControllerMsgQueue, (void*)pSignal);
        if(llerror != LL_OK) 
        {
            ERROR("failed to signal the playback controller");
            free(pSignal);
            rval = HLS_ERROR;
            break;
        }
                    
        /* Release reference to signal -- playback controller will free */
        pSignal = NULL;

        /* Get current time */
        if(clock_gettime(CLOCK_MONOTONIC, &timeoutTime) != 0) 
        {
            ERROR("failed to get current time");
            rval = HLS_ERROR;
            break;
        }

        /* Calculate the timeout time for moving to HLS_PREPARED */
        timeoutTime.tv_sec += HLS_PLAYING_TIMEOUT_SECS;
                    
        /* Wait for state change */
        while(pSession->state != HLS_PLAYING)
        {
            /* Get current time */
            if(clock_gettime(CLOCK_MONOTONIC, &currTime) != 0) 
            {
                ERROR("failed to get current time");
                rval = HLS_ERROR;
                break;
            }
            // 
            // The original code would do some form of timeout if it wasn't notified to play
            // This really isn't useful and alot of systems will not report back that it is playing
            // So I removed this.
            // RMS.
            //
#if 0
            /* If HLS_PLAYING_TIMEOUT_SECS have passed and we are not yet HLS_PLAYING, return an error */
            if((currTime.tv_sec > timeoutTime.tv_sec) || ((currTime.tv_sec == timeoutTime.tv_sec) && (currTime.tv_nsec > timeoutTime.tv_nsec)))
            {
                ERROR("state change to HLS_PLAYING timed out");
                rval = HLS_ERROR;
                break;
            }
#endif        
            /* If downloader was told to stop, return error (since we never went to HLS_PLAYING) */
            if(pSession->bKillDownloader) 
            {
                ERROR("downloader thread was killed");
                rval = HLS_ERROR;
                break;
            }            

            /* If downloader encountered an error, return the error to caller */
            if(pSession->downloaderStatus) 
            {
                ERROR("downloader error");
                rval = pSession->downloaderStatus;
                break;
            }                        
        }
    
    } while(0);

    /* Leave critical section */
    pthread_mutex_unlock(&(pSession->stateMutex));

    /* If we failed to start playing, kill the downloader thread, if it was started */
    if(rval != HLS_OK) 
    {
        /* Kill the downloader */
        pSession->bKillDownloader = 1;
        
        /* Wake up downloader thread if it is sleeping */
        if(pthread_mutex_lock(&(pSession->downloaderWakeMutex)) == 0)
        {
            if(pthread_cond_signal(&(pSession->downloaderWakeCond)) == 0)
            {
                pthread_mutex_unlock(&(pSession->downloaderWakeMutex));
            }
        }
                    
        /* Wait for downloader thread to exit */
        if(pSession->downloader != 0) 
        {
            pthread_join(pSession->downloader, NULL);
            pSession->downloader = 0;
        }

        /* Make sure we end up back in PREPARED state if we didn't
           make it to PLAYING */

        /* Block state changes */
        pthread_mutex_lock(&(pSession->stateMutex));
    
        pSession->state = HLS_PREPARED;

        /* Leave critical section */
        pthread_mutex_unlock(&(pSession->stateMutex));
    }

    return rval;
}

/** 
 * 
 * 
 * @param pSession
 * @param numBitrates
 * 
 * @return #hlsStatus_t
 */
hlsStatus_t hlsSession_getNumBitrates(hlsSession_t* pSession, int* numBitrates)
{
    hlsStatus_t rval = HLS_OK;

    if((pSession == NULL) || (numBitrates == NULL))
    {
        ERROR("invalid parameter");
        return HLS_INVALID_PARAMETER;
    }

    /* Get playlist READ lock */
    pthread_rwlock_rdlock(&(pSession->playlistRWLock));

    do
    {
        /* Check for valid state */
        if(pSession->state < HLS_PREPARED) 
        {
            ERROR("%s invalid in state %d", __FUNCTION__, pSession->state);
            rval = HLS_STATE_ERROR;
            break;
        }

        if(pSession->pPlaylist == NULL) 
        {
            ERROR("invalid playlist");
            rval = HLS_ERROR;
            break;
        }

        if(pSession->pPlaylist->type == PL_MEDIA) 
        {
            /* If we only have a media playlist, we only have 1 bitrate */
            *numBitrates = 1;
        }
        else if(pSession->pPlaylist->type == PL_VARIANT) 
        {
            /* If we have a variant playlist, return the number of streams */
            if((pSession->pCurrentProgram == NULL) ||
               (pSession->pCurrentProgram->pStreams == NULL))
            {
                ERROR("current program invalid");
                rval = HLS_ERROR;
                break;
            }

            *numBitrates = pSession->pCurrentProgram->pStreams->numElements;
        }
        else
        {
            ERROR("invalid playlist type");
            rval = HLS_ERROR;
            break;
        }

    } while(0);
    
    /* Release playlist lock */
    pthread_rwlock_unlock(&(pSession->playlistRWLock));

    return rval;
}

/** 
 * 
 * 
 * @param pSession
 * @param numBitrates
 * @param bitrates
 * 
 * @return #hlsStatus_t
 */
hlsStatus_t hlsSession_getBitrates(hlsSession_t* pSession, int numBitrates, int* bitrates)
{
    hlsStatus_t rval = HLS_OK;
    int i = 0;
    int numAvailableBitrates = 0;

    if((pSession == NULL) || (bitrates == NULL))
    {
        ERROR("invalid parameter");
        return HLS_INVALID_PARAMETER;
    }

    /* Get playlist READ lock */
    pthread_rwlock_rdlock(&(pSession->playlistRWLock));

    do
    {
        /* Check for valid state */
        if(pSession->state < HLS_PREPARED) 
        {
            ERROR("%s invalid in state %d", __FUNCTION__, pSession->state);
            rval = HLS_STATE_ERROR;
            break;
        }

        if(pSession->pPlaylist == NULL) 
        {
            ERROR("invalid playlist");
            rval = HLS_ERROR;
            break;
        }

        if(pSession->pPlaylist->type == PL_MEDIA) 
        {
            if(pSession->pPlaylist->pMediaData == NULL) 
            {
                ERROR("media playlist data is NULL");
                rval = HLS_ERROR;
                break;
            }

            /* If we only have a media playlist, return its bitrate */
            bitrates[0] = pSession->pPlaylist->pMediaData->bitrate;
        }
        else if(pSession->pPlaylist->type == PL_VARIANT) 
        {
            /* If we have a variant playlist, return the stream bitrates */
            if((pSession->pCurrentProgram == NULL) ||
               (pSession->pCurrentProgram->pStreams == NULL) ||
               (pSession->pCurrentProgram->pAvailableBitrates == NULL))
            {
                ERROR("current program invalid");
                rval = HLS_ERROR;
                break;
            }
    
            numAvailableBitrates = pSession->pCurrentProgram->pStreams->numElements;
    
            for(i = 0; i < numBitrates && i < numAvailableBitrates; i++) 
            {
                bitrates[i] = pSession->pCurrentProgram->pAvailableBitrates[i];
            }
        }
        else
        {
            ERROR("invalid playlist type");
            rval = HLS_ERROR;
            break;
        }

    } while(0);

    /* Release playlist lock */
    pthread_rwlock_unlock(&(pSession->playlistRWLock));
    
    return rval;
}

/**
 * 
 * 
 * @param pSession
 * @param bitrate
 * @return #hlsStatus_t
 */
hlsStatus_t hlsSession_getCurrentBitrate(hlsSession_t* pSession, int* pBitrate)
{
    hlsStatus_t rval = HLS_OK;

    if((pSession == NULL) || (pBitrate == NULL))
    {
        ERROR("invalid parameter");
        return HLS_INVALID_PARAMETER;
    }

    /* Get playlist READ lock */
    pthread_rwlock_rdlock(&(pSession->playlistRWLock));

    do
    {
        /* Check for valid state */
        if(pSession->state < HLS_PREPARED) 
        {
            ERROR("%s invalid in state %d", __FUNCTION__, pSession->state);
            rval = HLS_STATE_ERROR;
            break;
        }

        if((pSession->pCurrentPlaylist == NULL) || 
           (pSession->pCurrentPlaylist->type != PL_MEDIA) || 
           (pSession->pCurrentPlaylist->pMediaData == NULL)) 
        {
            ERROR("invalid current playlist");
            rval = HLS_ERROR;
            break;
        }

        *pBitrate = pSession->pCurrentPlaylist->pMediaData->bitrate;

    } while(0);

    /* Release playlist lock */
    pthread_rwlock_unlock(&(pSession->playlistRWLock));
    
    return rval;
}

/**
 * 
 * playlistRWLock MUST NOT be held by the calling thread 
 * 
 * @param pSession
 * @param limitType
 * @param bitrate
 * @return #hlsStatus_t
 */
hlsStatus_t hlsSession_setBitrateLimit(hlsSession_t* pSession, hlsBitrateLimit_t limitType, int limit)
{
    hlsStatus_t rval = HLS_OK;

    int bitrate = 0;

    if(pSession == NULL || limit < 0)
    {
        ERROR("invalid parameter");
        return HLS_INVALID_PARAMETER;
    }

    /* Block setting changes */
    pthread_mutex_lock(&(pSession->setMutex));

    do
    {
        /* Check for valid state */
        if(pSession->state < HLS_INITIALIZED) 
        {
            ERROR("%s invalid in state %d", __FUNCTION__, pSession->state);
            rval = HLS_STATE_ERROR;
            break;
        }

        switch(limitType)
        {
            case BR_LIMIT_MIN:
                pSession->minBitrate = limit;
                break;
            case BR_LIMIT_MAX:
                pSession->maxBitrate = limit;
                break;
            case BR_LIMIT_TARGET:
                pSession->targetBitrate = limit;
                break;
            default:
                ERROR("unknown hlsBitrateLimit_t value: %d", limitType);
                rval = HLS_ERROR;
                break;
        }
        if(rval != HLS_OK) 
        {
            break;
        }

        /* If we are prepared but haven't started playback yet,
           we can go ahead and change the starting bitrate based
           on the new range/target */
        if(pSession->state == HLS_PREPARED) 
        {
            /* Get playlist WRITE lock */
            pthread_rwlock_wrlock(&(pSession->playlistRWLock));

            if((pSession->pCurrentPlaylist == NULL) || 
               (pSession->pCurrentPlaylist->type != PL_MEDIA) || 
               (pSession->pCurrentPlaylist->pMediaData == NULL)) 
            {
                ERROR("invalid current playlist");
                rval = HLS_ERROR;
                /* Release playlist lock */
                pthread_rwlock_unlock(&(pSession->playlistRWLock));
                break;
            }

            /* Find the best bitrate based on range/target*/
            rval = getBestBitrate(pSession, &bitrate);
            if(rval != HLS_OK)    
            {
                ERROR("problem getting initial bitrate");
                /* Release playlist lock */
                pthread_rwlock_unlock(&(pSession->playlistRWLock));
                break;
            }

            /* If we got a different 'best' bitrate, change to it */
            if(bitrate != pSession->pCurrentPlaylist->pMediaData->bitrate) 
            {
                rval = changeBitrate(pSession, bitrate);
                if(rval != HLS_OK) 
                {
                    ERROR("problem changing bitrate");
                    /* Release playlist lock */
                    pthread_rwlock_unlock(&(pSession->playlistRWLock));
                    break;
                }
            }

            /* Release playlist lock */
            pthread_rwlock_unlock(&(pSession->playlistRWLock));
        }

    } while(0);
    
    /* Leave critical section */
    pthread_mutex_unlock(&(pSession->setMutex));

    return rval;
}

/** 
 * playlistRWLock MUST NOT be held by the calling thread 
 *  
 * On return, session will be in HLS_PLAYING state 
 * 
 * @param pSession
 * @param speed
 * 
 * @return #hlsStatus_t -- HLS_UNSUPPORTED if speed is an 
 *         unsupported value for this stream
 */
hlsStatus_t hlsSession_setSpeed(hlsSession_t* pSession, float speed)
{
    hlsStatus_t rval = HLS_OK;
    srcStatus_t status = SRC_SUCCESS;

    double currentPosition = 0;
    double currentDuration = 0;

    srcPlayerSetData_t playerSetData;
    srcPlayerMode_t playerMode;

    if(pSession == NULL)
    {
        ERROR("invalid parameter");
        return HLS_INVALID_PARAMETER;
    }

    /* Block setting changes */
    pthread_mutex_lock(&(pSession->setMutex));

    /* Block state changes */
    pthread_mutex_lock(&(pSession->stateMutex));

    do
    {
        /* Check for valid state */
        if(pSession->state < HLS_INITIALIZED) 
        {
            ERROR("%s invalid in state %d", __FUNCTION__, pSession->state);
            rval = HLS_STATE_ERROR;
            break;
        }

        /* If for some reason prepare() has not yet been called, have the session prepare itself */
        if(pSession->state == HLS_INITIALIZED) 
        {
            rval = hlsSession_prepare(pSession);
            if(rval != HLS_OK) 
            {
                ERROR("hlsSession_prepare failed");
                break;
            }
        }

        // TODO: should the player be in SRC_PLAYER_MODE_PAUSE by default???  Do we need to set this???

        /* Get playlist READ lock */
        pthread_rwlock_rdlock(&(pSession->playlistRWLock));

        /* Validate current playlist */
        if((pSession->pCurrentPlaylist == NULL) || 
           (pSession->pCurrentPlaylist->type != PL_MEDIA) || 
           (pSession->pCurrentPlaylist->pMediaData == NULL)) 
        {
            ERROR("invalid current playlist");
            rval = HLS_ERROR;
			/* Release playlist lock */
            pthread_rwlock_unlock(&(pSession->playlistRWLock));
            break;
        }

        /* Make sure trickmodes are possible at this time */
        if((speed < 0) || (speed > 1)) 
        {
            /* If we don't have any I-frame playlists, we can't do trickmodes */
            if((pSession->pCurrentProgram == NULL) ||
               (pSession->pCurrentProgram->pIFrameStreams == NULL) ||
               (pSession->pCurrentProgram->pIFrameStreams->numElements == 0))
            {
                ERROR("no I-frame streams -- can't trickplay");
                rval = HLS_UNSUPPORTED;
                /* Release playlist lock */
                pthread_rwlock_unlock(&(pSession->playlistRWLock));
                break;
            }
            
            /* Get the current position */
            rval = getExternalPosition(pSession->pCurrentPlaylist, &currentPosition);
            if(rval != HLS_OK) 
            {
                ERROR("failed to get exernal position");
                /* Release playlist lock */
                pthread_rwlock_unlock(&(pSession->playlistRWLock));
                break;
            }

            /* If we're already at the end of playlist, speeds > 1x aren't supported */
            if(speed > 1) 
            {
                /* Get the current duration */
                rval = getExternalDuration(pSession->pCurrentPlaylist, &currentDuration);
                if(rval != HLS_OK) 
                {
                    ERROR("failed to get exernal duration");
                    /* Release playlist lock */
                    pthread_rwlock_unlock(&(pSession->playlistRWLock));
                    break;
                }

                if(currentPosition == currentDuration) 
                {
                    ERROR("cannot FF -- already at end of playlist");
                    rval = HLS_UNSUPPORTED;
                    /* Release playlist lock */
                    pthread_rwlock_unlock(&(pSession->playlistRWLock));
                    break;
                }
            }
    
            /* If we're already at the beginning of the playlist, speeds < 0 aren't supported */
            if(speed < 0) 
            {
                if(currentPosition == 0) 
                {
                    ERROR("cannot REW -- already at start of playlist");
                    rval = HLS_UNSUPPORTED;
                    /* Release playlist lock */
                    pthread_rwlock_unlock(&(pSession->playlistRWLock));
                    break;
                }
            }
        }

        /* Release playlist lock */
        pthread_rwlock_unlock(&(pSession->playlistRWLock));

        /* Slow trick speeds are treated as 1x play by the plugin */
        if((speed > 0) && (speed < 1))
        {
            speed = 1.0;
        }

        /* Are we changing the speed? */
        if(speed == pSession->speed) 
        {
            DEBUG(DBG_INFO,"session %p speed %f -> %f: no change", pSession, pSession->speed, speed);
        }
        else
        {
            // TODO: right now, going from 2x -> PAUSE -> 2x will cause two playlist switches and two thread kills
            //       because it is assumed that the only valid transition is from PAUSE -> 1X play
            //       is this OK??
    
            /* What are we switching from/to? */
            if((pSession->speed == 0) && (speed == 1)) /* PAUSE -> 1x PLAY */
            {
                DEBUG(DBG_INFO,"session %p speed %f -> %f: start player playback", pSession, pSession->speed, speed);
    
                /* If we are already in HLS_PLAYING state, tell the player to PLAY */
                if(pSession->state == HLS_PLAYING) 
                {
                    playerSetData.setCode = SRC_PLAYER_SET_MODE;
                    playerMode = SRC_PLAYER_MODE_NORMAL;
                    playerSetData.pData = &playerMode;
                    status = hlsPlayer_set(pSession->pHandle, &playerSetData);
                    if(status != SRC_SUCCESS) 
                    {
                        ERROR("failed to set player mode SRC_PLAYER_MODE_NORMAL");
                        rval = HLS_ERROR;
                        break;
                    }
                }
            }
            else if((pSession->speed == 1) && (speed == 0)) /* 1x PLAY -> PAUSE */ 
            {
                DEBUG(DBG_INFO,"session %p speed %f -> %f: pause player", pSession, pSession->speed, speed);
                
                playerSetData.setCode = SRC_PLAYER_SET_MODE;
                playerMode = SRC_PLAYER_MODE_PAUSE;
                playerSetData.pData = &playerMode;
                status = hlsPlayer_set(pSession->pHandle, &playerSetData);
                if(status != SRC_SUCCESS) 
                {
                    ERROR("failed to set player mode SRC_PLAYER_MODE_PAUSE");
                    rval = HLS_ERROR;
                    break;
                }
            }
            else if(((pSession->speed < 0) || (pSession->speed > 1)) &&
                    ((speed < 0) || (speed > 1))) /* TRICKPLAY -> TRICKPLAY */
            {
                DEBUG(DBG_INFO,"session %p speed %f -> %f: change trickplay speed", pSession, pSession->speed, speed);
            }
            else /* ((1x PLAY || PAUSE) -> TRICKPLAY) || (TRICKPLAY -> (1x PLAY || PAUSE)) */
            {
                DEBUG(DBG_INFO,"session %p speed %f -> %f: stop playback and switch playlists", pSession, pSession->speed, speed);
    
                /* Stop playback */
                rval = hlsSession_stop(pSession);
                if(rval != HLS_OK) 
                {
                    ERROR("failed to stop playback");
                    break;
                }
    
                if((pSession->speed == 1) || (pSession->speed == 0)) /* ((1x PLAY || PAUSE) -> TRICKPLAY) */
                {
                    /* Get playlist WRITE lock */
                    pthread_rwlock_wrlock(&(pSession->playlistRWLock));

                    /* Flush current playlist */
                    rval = flushPlaylist(pSession->pCurrentPlaylist);
                    if(rval != HLS_OK) 
                    {
                        ERROR("problem flushing playlist");
                        /* Release playlist lock */
                        pthread_rwlock_unlock(&(pSession->playlistRWLock));
                        break;
                    }

                    /* Switch playlists */
                    rval = switchToIFramePlaylists(pSession);
                    if(rval != HLS_OK) 
                    {
                        ERROR("problem switching to I-frame playlists");
                        /* Release playlist lock */
                        pthread_rwlock_unlock(&(pSession->playlistRWLock));
                        break;
                    }

                    /* Release playlist lock */
                    pthread_rwlock_unlock(&(pSession->playlistRWLock));
                }
                else /* (TRICKPLAY -> (1x PLAY || PAUSE)) */
                {
                    /* Get playlist WRITE lock */
                    pthread_rwlock_wrlock(&(pSession->playlistRWLock));

                    /* Flush current playlist */
                    rval = flushPlaylist(pSession->pCurrentPlaylist);
                    if(rval != HLS_OK) 
                    {
                        ERROR("problem flushing playlist");
                        /* Release playlist lock */
                        pthread_rwlock_unlock(&(pSession->playlistRWLock));
                        break;
                    }

                    /* Switch playlists */
                    rval = switchToNormalPlaylists(pSession);
                    if(rval != HLS_OK) 
                    {
                        ERROR("problem switching to normal playlists");
                        /* Release playlist lock */
                        pthread_rwlock_unlock(&(pSession->playlistRWLock));
                        break;
                    }

                    /* Release playlist lock */
                    pthread_rwlock_unlock(&(pSession->playlistRWLock));
                }
            }
            
            /* Set the new speed */
            pSession->speed = speed;
        }
                
        /* Start playback to get back into HLS_PLAYING state
           We do this even if we are NOT changing speed, since the expectation
           is that when this function returns we are in HLS_PLAYING state */
        if(pSession->state != HLS_PLAYING) 
        {
            /* Restart the downloader -- this will kick the player back into PLAY mode */
            rval = hlsSession_play(pSession);
            if(rval != HLS_OK) 
            {
                ERROR("hlsSession_play failed");
                break;
            }
        }

    } while(0);

    /* Leave critical section */
    pthread_mutex_unlock(&(pSession->stateMutex));

    /* Leave critical section */
    pthread_mutex_unlock(&(pSession->setMutex));

    return rval;
}

/**
 * Stops playback.  Will kill the downloader thread and flush 
 * the decoder buffer.  On return, the session will be in 
 * HLS_PREPARED state. 
 *  
 * @param pSession
 * 
 * @return #hlsStatus_t
 */
hlsStatus_t hlsSession_stop(hlsSession_t* pSession)
{
    hlsStatus_t rval = HLS_OK;
    srcStatus_t status = SRC_SUCCESS;

    srcPlayerSetData_t playerSetData;
    srcPlayerMode_t playerMode;

    playbackControllerSignal_t* pSignal = NULL;

    llStatus_t llerror = LL_OK;

    if(pSession == NULL)
    {
        ERROR("invalid parameter");
        return HLS_INVALID_PARAMETER;
    }

    TIMESTAMP(DBG_INFO,"%s", __FUNCTION__);

    /* Block state changes */
    pthread_mutex_lock(&(pSession->stateMutex));

    do
    {
        /* Check for valid state */
        if(pSession->state < HLS_PREPARED) 
        {
            ERROR("%s invalid in state %d", __FUNCTION__, pSession->state);
            rval = HLS_STATE_ERROR;
            break;
        }

        /* If we're already prepared, do nothing */
        if(pSession->state == HLS_PREPARED) 
        {
            break;
        }

        /* Signal that playback is stopping to the playback controller thread */

        /* Allocate a new playback controller signal */
        pSignal = malloc(sizeof(playbackControllerSignal_t));
        if(pSignal == NULL) 
        {
            ERROR("malloc error");
            rval = HLS_ERROR;
            break;
        }

        *pSignal = PBC_STOPPING_PLAYBACK;

        /* Push the message to the playback controller message queue */
        llerror = pushMsg(pSession->playbackControllerMsgQueue, (void*)pSignal);
        if(llerror != LL_OK) 
        {
            ERROR("failed to signal the playback controller");
            free(pSignal);
            rval = HLS_ERROR;
            break;
        }
                    
        /* Release reference to signal -- playback controller will free */
        pSignal = NULL;

        /* Pause decoder */
        playerSetData.setCode = SRC_PLAYER_SET_MODE;
        playerMode = SRC_PLAYER_MODE_PAUSE;
        playerSetData.pData = &playerMode;
        status = hlsPlayer_set(pSession->pHandle, &playerSetData);
        if(status != SRC_SUCCESS) 
        {
            ERROR("failed to set player mode SRC_PLAYER_MODE_PAUSE");
            rval = HLS_ERROR;
            break;
        }

        /* Kill the downloader */
        pSession->bKillDownloader = 1;
        
        DEBUG(DBG_INFO, "Stopping download thread");

        /* Wake up downloader thread if it is sleeping */
        if(pthread_mutex_lock(&(pSession->downloaderWakeMutex)) == 0)
        {
            if(pthread_cond_signal(&(pSession->downloaderWakeCond)) == 0)
            {
                pthread_mutex_unlock(&(pSession->downloaderWakeMutex));
            }
        }
                                
        if(pSession->downloader != 0) 
        {
            /* Wait for the downloader to quit */
            pthread_join(pSession->downloader, NULL);
            pSession->downloader = 0;
        }
        
        pSession->bKillDownloader = 0;

        /* Check if downloader exited cleanly */
        if((pSession->downloaderStatus != HLS_OK) && (pSession->downloaderStatus != HLS_CANCELLED))
        {
            ERROR("downloader exited with status: %d", pSession->downloaderStatus);
            rval = pSession->downloaderStatus;
            break;
        }

        /* Flush the decoder cache */
        playerSetData.setCode = SRC_PLAYER_SET_BUFFER_FLUSH;
        playerSetData.pData = NULL;
        status = hlsPlayer_set(pSession->pHandle, &playerSetData);
        if(status != SRC_SUCCESS) 
        {
            ERROR("failed to flush player buffers");
            rval = HLS_ERROR;
            break;
        }

        /* Set session state to HLS_PREPARED */
        pSession->state = HLS_PREPARED;

        /* Block PTS events */
        pthread_mutex_lock(&(pSession->playerEvtMutex));

        /* Reset our buffer length and lastPTS */
        pSession->timeBuffered = 0;
        pSession->lastPTS = -1ll;
        
        /* Unblock player events */
        pthread_mutex_unlock(&(pSession->playerEvtMutex));

    } while(0);

    /* Leave critical section */
    pthread_mutex_unlock(&(pSession->stateMutex));
   
    return rval;
}

/** 
 * 
 * 
 * @param pSession
 * @param position
 * 
 * @return #hlsStatus_t
 */
hlsStatus_t hlsSession_seek(hlsSession_t* pSession, float position)
{
    hlsStatus_t rval = HLS_OK;

    float duration = 0;

    hlsPlaylist_t* pMediaPlaylist = NULL;

    hlsSegment_t* pSegment = NULL;

    llNode_t* pSegmentNode = NULL;

    double positionFromEnd = 0;

    if(pSession == NULL)
    {
        ERROR("invalid parameter");
        return HLS_INVALID_PARAMETER;
    }

    /* Block setting changes */
    pthread_mutex_lock(&(pSession->setMutex));

    /* Block state changes */
    pthread_mutex_lock(&(pSession->stateMutex));

    do
    {
        /* Check for valid state */
        if(pSession->state < HLS_PREPARED) 
        {
            ERROR("%s invalid in state %d", __FUNCTION__, pSession->state);
            rval = HLS_STATE_ERROR;
            break;
        }

        /* Get asset duration */
        rval = hlsSession_getDuration(pSession, &duration);
        if(rval != HLS_OK) 
        {
            ERROR("failed to get duration");
            break;
        }

        if((position < 0) || (position > duration))
        {
            ERROR("position %f is out of bounds [0, %f]", position, duration);
            rval = HLS_UNSUPPORTED;
            break;
        }

        /* Convert position from milliseconds to seconds */
        position /= 1000;

        /* Stop playback */
        if(pSession->state == HLS_PLAYING) 
        {
            /* Stop downloading and flush decoder cache */
            rval = hlsSession_stop(pSession);
            if(rval != HLS_OK) 
            {
                ERROR("failed to stop playback");
                break;
            }
        }

        /* Get playlist WRITE lock */
        pthread_rwlock_wrlock(&(pSession->playlistRWLock));

        if((pSession->pCurrentPlaylist == NULL) ||
           (pSession->pCurrentPlaylist->type != PL_MEDIA) ||
           (pSession->pCurrentPlaylist->pMediaData == NULL))
        {
            ERROR("current playlist invalid");
            rval = HLS_ERROR;
            /* Release playlist lock */
            pthread_rwlock_unlock(&(pSession->playlistRWLock));
            break;
        }

        pMediaPlaylist = pSession->pCurrentPlaylist;

        /* Need to account for the startOffset of the playlist when determining absolute
           position. */
        position += pMediaPlaylist->pMediaData->startOffset;
        
        /* Get the segment which contains the desired position */
        rval = getSegmentXSecFromStart(pMediaPlaylist, position, &pSegment);
        if(rval != HLS_OK) 
        {
            ERROR("failed to find segment in playlist");
            /* Release playlist lock */
            pthread_rwlock_unlock(&(pSession->playlistRWLock));
            break;
        }

        DEBUG(DBG_INFO,"Playback will resume at segment %d", pSegment->seqNum);

        /* Reset our positionFromEnd to be the starting position from end of the above segment */
        rval = getPositionFromEnd(pMediaPlaylist, pSegment, &(positionFromEnd));
        if(rval != HLS_OK) 
        {
            ERROR("problem getting initial playlist position");
            /* Release playlist lock */
            pthread_rwlock_unlock(&(pSession->playlistRWLock));
            break;
        }
        
        /* Get the segment's node */
        if(pSegment->pParentNode != NULL) 
        {
            pSegmentNode = pSegment->pParentNode;
        }
        else
        {
            ERROR("segment has no parent node");
            rval = HLS_ERROR;
            /* Release playlist lock */
            pthread_rwlock_unlock(&(pSession->playlistRWLock));
            break;
        }

        /* pSegmentNode is the first node we want to download, so we need to set
           pLastDownloadedSegmentNode to pSegmentNode->pPrev */
        pSegmentNode = pSegmentNode->pPrev;

        /* Write new playlist values */
        pMediaPlaylist->pMediaData->positionFromEnd = positionFromEnd;
        pMediaPlaylist->pMediaData->pLastDownloadedSegmentNode = pSegmentNode;

        /* Release playlist lock */
        pthread_rwlock_unlock(&(pSession->playlistRWLock));

        /* Restart playback */
        rval = hlsSession_play(pSession);
        if(rval != HLS_OK) 
        {
            ERROR("failed to restart playback");
            break;
        }
        
    } while(0);

    /* Leave critical section */
    pthread_mutex_unlock(&(pSession->stateMutex));

    /* Leave critical section */
    pthread_mutex_unlock(&(pSession->setMutex));

    return rval;
}

/** 
 * 
 * 
 * @param pSession
 * @param pDuration
 * 
 * @return #hlsStatus_t
 */
hlsStatus_t hlsSession_getDuration(hlsSession_t* pSession, float* pDuration)
{
    hlsStatus_t rval = HLS_OK;

    double duration = 0;

    if((pSession == NULL) || (pDuration == NULL))
    {
        ERROR("invalid parameter");
        return HLS_INVALID_PARAMETER;
    }

    /* Get playlist READ lock */
    pthread_rwlock_rdlock(&(pSession->playlistRWLock));

    do
    {
        /* Check for valid state */
        if(pSession->state < HLS_PREPARED) 
        {
            ERROR("%s invalid in state %d", __FUNCTION__, pSession->state);
            rval = HLS_STATE_ERROR;
            break;
        }

        if((pSession->pCurrentPlaylist == NULL) ||
           (pSession->pCurrentPlaylist->type != PL_MEDIA) ||
           (pSession->pCurrentPlaylist->pMediaData == NULL))
        {
            ERROR("current playlist invalid");
            rval = HLS_ERROR;
            break;
        }

        /* Get the externally visible playlist duration */
        rval = getExternalDuration(pSession->pCurrentPlaylist, &duration);
        if(rval != HLS_OK) 
        {
            ERROR("failed to get exernal duration");
            break;
        }

        /* Convert to milliseconds */
        *pDuration = duration*1000;

        DEBUG(DBG_INFO,"current duration = %5.2f milliseconds", *pDuration);

    } while(0);
    
    /* Release playlist lock */
    pthread_rwlock_unlock(&(pSession->playlistRWLock));

    return rval;
}

/** 
 * 
 * 
 * @param pSession
 * @param pPosition
 * 
 * @return #hlsStatus_t
 */
hlsStatus_t hlsSession_getCurrentPosition(hlsSession_t* pSession, float* pPosition)
{
    hlsStatus_t rval = HLS_OK;

    double position = 0;

    if((pSession == NULL) || (pPosition == NULL))
    {
        ERROR("invalid parameter");
        return HLS_INVALID_PARAMETER;
    }

    /* Get playlist READ lock */
    pthread_rwlock_rdlock(&(pSession->playlistRWLock));

    do
    {
        /* Check for valid state */
        if(pSession->state < HLS_PREPARED) 
        {
            ERROR("%s invalid in state %d", __FUNCTION__, pSession->state);
            rval = HLS_STATE_ERROR;
            break;
        }

        if((pSession->pCurrentPlaylist == NULL) ||
           (pSession->pCurrentPlaylist->type != PL_MEDIA) ||
           (pSession->pCurrentPlaylist->pMediaData == NULL))
        {
            ERROR("current playlist invalid");
            rval = HLS_ERROR;
            break;
        }

        //TODO: what about EVENT playlists?  Initial position is 0 or duration?

        /* There is a special case if we haven't yet started playback and have a floating start point.
           In this case we will begin playback at the last valid position in the playlist
           (aka the 'live point'), so return that as our current position. */
        if((pSession->state == HLS_PREPARED) && (pSession->pCurrentPlaylist->pMediaData->startOffset > 0))
        {
            rval = getExternalDuration(pSession->pCurrentPlaylist, &position);
            if(rval != HLS_OK) 
            {
                ERROR("failed to get exernal duration");
                break;
            }
        }
        else
        {
            /* Else, get the externally visible playlist position */
            rval = getExternalPosition(pSession->pCurrentPlaylist, &position);
            if(rval != HLS_OK) 
            {
                ERROR("failed to get exernal position");
                break;
            }
        }

        /* Convert to milliseconds */
        *pPosition = position*1000;

        DEBUG(DBG_INFO,"current position = %5.2f milliseconds", *pPosition);

    } while(0);
    
    /* Release playlist lock */
    pthread_rwlock_unlock(&(pSession->playlistRWLock));

    return rval;
}

/** 
 * 
 * 
 * @param pSession
 * @param pEvt
 */
void hlsSession_playerEvtCallback(hlsSession_t* pSession, srcPlayerEvt_t* pEvt)
{
    hlsStatus_t status = HLS_OK;
    double time = 0;

    hlsSegment_t* pSegment;
    llNode_t* pSegmentNode;

    long long tempPTS;

    playbackControllerSignal_t* pSignal = NULL;

    llStatus_t llerror = LL_OK;

    if((pSession == NULL) || (pEvt == NULL))
    {
        ERROR("invalid parameter");
        return;
    }

    /* Block player events until this one is processed */
    pthread_mutex_lock(&(pSession->playerEvtMutex));

    switch(pEvt->evtCode) 
    {
        case SRC_PLAYER_LAST_PTS:
            tempPTS = *((long long*)(pEvt->pData));

            /* Ignore PTS events if we're not currently PLAYING or doing trickmodes */
            if((pSession->state == HLS_PLAYING) && (pSession->speed >= 0) && (pSession->speed <=1))
            {
	            /* If we haven't gotten a PTS yet, set the initial values */
	            if(pSession->lastPTS == -1ll) 
    	        {
        	        pSession->lastPTS = tempPTS;

                    DEBUG(DBG_INFO,"got first PTS %lld -- current position: %5.2f seconds", pSession->lastPTS, 0.0);
	            }
    	        else
        	    {
	                /* Get playlist WRITE lock */
    	            pthread_rwlock_wrlock(&(pSession->playlistRWLock));

        	        if((pSession->pCurrentPlaylist == NULL) ||
            	       (pSession->pCurrentPlaylist->type != PL_MEDIA) ||
                       (pSession->pCurrentPlaylist->pMediaData == NULL))
                	{
	                    // TODO: do something more drastic??
    	                ERROR("current playlist is not a valid media playlist");
        	            /* Release playlist lock */
            	        pthread_rwlock_unlock(&(pSession->playlistRWLock));
                	    break;
	                }

                    // TODO: is PTS_LIMIT correct???

    	            /* We need to deal with PTS rollovers */
        	        if(pSession->speed >= 0) /* 1x PLAY or FFWD */
            	    {
                        /* If we're playing forward and we get a PTS smaller than our last one, assume we rolled over */
                        if(tempPTS < pSession->lastPTS)
                        {
                            //TODO: noise...
                            DEBUG(DBG_INFO, "possible PTS roll-over...");

                            /* If the PTS difference is greater than .5 the entire PTS range, assume a roll over */
                            if((pSession->lastPTS - tempPTS) > PTS_LIMIT/2)
                            {
                                DEBUG(DBG_WARN, "Got a PTS %lld from the past while playing forwards -- rolling over", tempPTS);

                                /* If we rolled over, subtract the maximum value (2^33) from
                                   our last PTS and continue from there. */
                                pSession->lastPTS -= PTS_LIMIT;
                            }
                            else
                            {
                                /* If we don't roll-over, assume this is a spurious PTS from the past, and just ignore it. */
                                DEBUG(DBG_WARN, "Got a PTS %lld from the past while playing forwards -- ignoring", tempPTS);
                                /* Release playlist lock */
                                pthread_rwlock_unlock(&(pSession->playlistRWLock));
                                break;
                            }
                        }
                	}
                    else /* REW */
                    {
                        /* If we're playing backwards and we get a PTS larger than our last one, assume we rolled over */
                        if(tempPTS > pSession->lastPTS)
                        {
                            //TODO: noise...
                            DEBUG(DBG_INFO, "possible PTS roll-over...");

                            /* If the PTS difference is greater than .5 the entire PTS range, assume a roll over */
                            if((tempPTS - pSession->lastPTS) > PTS_LIMIT/2)
                            {
                                DEBUG(DBG_WARN, "Got a PTS %lld from the future while playing backwards -- rolling over", tempPTS);

                                /* If we rolled over, add the maximum value (2^33) from
                                   our last PTS and continue from there. */
                                pSession->lastPTS += PTS_LIMIT;
                            }
                            else
                            {
                                /* If we don't roll-over, assume this is a spurious PTS from the future, and just ignore it. */
                                DEBUG(DBG_WARN, "Got a PTS %lld from the future while playing backwards -- ignoring", tempPTS);
                                /* Release playlist lock */
                                pthread_rwlock_unlock(&(pSession->playlistRWLock));
                                break;
                            }
                        }
                    }

	                DEBUG(DBG_NOISE,"%5.2f since last PTS", ptsToSeconds(tempPTS) - ptsToSeconds(pSession->lastPTS));

    	            /* Decrease our total time buffered */
        	        pSession->timeBuffered -= ptsToSeconds(tempPTS) - ptsToSeconds(pSession->lastPTS);

            	    /* Update our position */
                	pSession->pCurrentPlaylist->pMediaData->positionFromEnd -= ptsToSeconds(tempPTS) - ptsToSeconds(pSession->lastPTS);

	                /* Save the new PTS */
    	            pSession->lastPTS = tempPTS;

                    DEBUG(DBG_INFO,"got PTS %lld -- current position: %5.2f seconds", pSession->lastPTS, 
                          pSession->pCurrentPlaylist->pMediaData->duration - pSession->pCurrentPlaylist->pMediaData->positionFromEnd - pSession->pCurrentPlaylist->pMediaData->startOffset);

        	        /* Release playlist lock */
                	pthread_rwlock_unlock(&(pSession->playlistRWLock));
	            }

    	        DEBUG(DBG_INFO,"%f seconds left in buffer", pSession->timeBuffered);
			}
            break;
        case SRC_PLAYER_DISCONTINUITY:
            /* Ignore discontinuity events if we're not PLAYING or doing trickmodes */
            if((pSession->state == HLS_PLAYING) && (pSession->speed >= 0) && (pSession->speed <=1))
            {
	            /* Get playlist WRITE lock */
    	        pthread_rwlock_wrlock(&(pSession->playlistRWLock));

        	    /* Verify currentPlaylist */
            	if((pSession->pCurrentPlaylist == NULL) ||
	               (pSession->pCurrentPlaylist->type != PL_MEDIA) ||
                   (pSession->pCurrentPlaylist->pMediaData == NULL))
    	        {
        	        // TODO: do something more drastic??
            	    ERROR("current playlist is not a media playlist");
                	/* Release playlist lock */
	                pthread_rwlock_unlock(&(pSession->playlistRWLock));
    	            break;
        	    }

	            /* Get the current aboslute playlist position (duration - positionFromEnd) */
    	        time = pSession->pCurrentPlaylist->pMediaData->duration;
                time -= pSession->pCurrentPlaylist->pMediaData->positionFromEnd;
    
            	/* Find the segment the media player is currently playing */
	            status = getSegmentXSecFromStart(pSession->pCurrentPlaylist, time, &pSegment);
    	        if(status != HLS_OK) 
        	    {
            	    // TODO: do something more drastic??
                	ERROR("failed to find segment in playlist");
	                /* Release playlist lock */
    	            pthread_rwlock_unlock(&(pSession->playlistRWLock));
        	        break;
            	}
            
                /* Get the segment's node */
                if(pSegment->pParentNode != NULL) 
                {
                    pSegmentNode = pSegment->pParentNode;
                }
                else
                {
                    // TODO: do something more drastic??
                    ERROR("segment has no parent node");
                    /* Release playlist lock */
                    pthread_rwlock_unlock(&(pSession->playlistRWLock));
                    break;
                }

	            // TODO: If we don't find a discontinuity, do we want to kill playback??

    	        /* Find the next node that has a discontinuity */
        	    while(status == HLS_OK) 
            	{
                	/* Did we run out of segments or hit an empty one? */
	                if((pSegmentNode == NULL) || (pSegmentNode->pData == NULL))
    	            {
        	            ERROR("NULL or empty node in segment linked list");
            	        status = HLS_ERROR;
                	    break;
	                }
    
    	            pSegment = (hlsSegment_t*)(pSegmentNode->pData);
                
        	        /* Does this segment have a discontinuity signalled? */
            	    if(pSegment->bDiscontinuity) 
                	{
                    	/* Get the current position from end of this segment */
	                    status = getPositionFromEnd(pSession->pCurrentPlaylist, pSegment, &time);
    	                if(status != HLS_OK) 
        	            {
            	            ERROR("failed to get segment position");
                	        break;
                    	}
    
	                    /* We're done searching -- break */
    	                break;
        	        }
    
            	    pSegmentNode = pSegmentNode->pNext;
	            }
    	        if(status != HLS_OK) 
        	    {
            	    // TODO: do something more drastic??
                	/* Release playlist lock */
	                pthread_rwlock_unlock(&(pSession->playlistRWLock));
    	            break;
            	}
    
	            /* Update our startingOffsetFromEnd to be the current offset from end of the discontinuous segment */
    	        pSession->pCurrentPlaylist->pMediaData->positionFromEnd = time;
	
        	    /* Release playlist lock */
    	        pthread_rwlock_unlock(&(pSession->playlistRWLock));
            
	            // TODO: need to update buffer length...how? -- above: positionFromEnd - time???

	            /* Reset the PTS value */
	            pSession->lastPTS = *((long long*)(pEvt->pData));

	            DEBUG(DBG_INFO,"got DISCONTINUITY with PTS %lld", pSession->lastPTS);
			}
            break;
        case SRC_PLAYER_AUDIO_FIFO_UNDERRUN:
            /* Signal the player audio FIFO underrun to the playback controller thread */

            /* Allocate a new playback controller signal */
            pSignal = malloc(sizeof(playbackControllerSignal_t));
            if(pSignal == NULL) 
            {
                ERROR("malloc error");
                break;
            }

            *pSignal = PBC_PLAYER_AUDIO_UNDERRUN;

            /* Push the message to the playback controller message queue */
            llerror = pushMsg(pSession->playbackControllerMsgQueue, (void*)pSignal);
            if(llerror != LL_OK) 
            {
                ERROR("failed to signal the playback controller");
                free(pSignal);
                break;
            }
                    
            /* Release reference to signal -- playback controller will free */
            pSignal = NULL;
            break;
        default:
            ERROR("unknown event code: %d", (int)pEvt->evtCode);
            break;
    }

    /* Leave critical section */
    pthread_mutex_unlock(&(pSession->playerEvtMutex));
}

/**
 * Prints information about hlsSession object.
 * 
 * @param pSession - pointer to hlsSession to display 
 *                  information about
 */
void hlsSession_printInfo(hlsSession_t* pSession)
{
    int i = 0;

    if(pSession != NULL) 
    {
        /* Get playlist READ lock */
        pthread_rwlock_rdlock(&(pSession->playlistRWLock));

        printf("------------------------\n");
        printf("**** Streamer Info  ****\n");
        printf("------------------------\n");

        printf("last PTS of current playback: %lld\n", pSession->lastPTS);

        if(pSession->pCurrentPlaylist != NULL &&
           pSession->pCurrentPlaylist->type == PL_MEDIA) 
        {
            printf("position in current playlist: %5.2f seconds\n", pSession->pCurrentPlaylist->pMediaData->duration -
                   pSession->pCurrentPlaylist->pMediaData->positionFromEnd);
        }

        printf("time buffered: %5.2f seconds\n", pSession->timeBuffered);
        printf("bitrate of last download: %f\n", pSession->lastSegmentDldRate);
        printf("avg download bitrate: %f\n", pSession->avgSegmentDldRate);

        if(pSession->pCurrentProgram != NULL) 
        {
            printf("Current programID: %d\n", pSession->pCurrentProgram->programID);
            if(pSession->pCurrentProgram->pStreams != NULL) 
            {
                printf("%d available bitrates (bps): ", pSession->pCurrentProgram->pStreams->numElements);
                for(i = 0; i < pSession->pCurrentProgram->pStreams->numElements; i++) 
                {
                    printf("%d ", pSession->pCurrentProgram->pAvailableBitrates[i]);
                }
            }
            printf("\n");
        }
                
        if(pSession->pCurrentPlaylist != NULL) 
        {
            printf("------------------------\n");
            printf("*** Current Playlist ***\n");
            printf("------------------------\n");
            printPlaylist(pSession->pCurrentPlaylist);
        }

        printf("------------------------\n");
        printf("** Full Playlist Info **\n");
        printf("------------------------\n");
        
        printPlaylist(pSession->pPlaylist);
        
        /* Release playlist lock */
        pthread_rwlock_unlock(&(pSession->playlistRWLock));
    }   
}

#ifdef __cplusplus
}
#endif
