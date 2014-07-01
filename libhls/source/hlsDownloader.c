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
 * @file hlsDownloader.c @date February 9, 2012
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
#include <ctype.h>
#include <math.h>

#include "hlsPlayerInterface.h"

#include "hlsSession.h"
#include "hlsSessionUtils.h"

#include "hlsDownloader.h"
#include "hlsDownloaderUtils.h"

#include "debug.h"
#include "adaptech.h"
#include "curlUtils.h"

/* Loop duration in seconds */
#define DOWNLOADER_LOOP_SECS 1
hlsStatus_t hlsSegmentDownloadLoop(hlsSession_t* pSession)
{
    hlsStatus_t status = HLS_OK;

    int pthread_status = 0;

    hlsPlaylist_t* pMediaPlaylist = NULL;
    hlsSegment_t* pSegment = NULL;

    hlsSegment_t* pSegmentCopy = NULL;

    int proposedBitrateIndex = 0;
    struct timespec wakeTime;
    llStatus_t llerror = LL_OK;
    playbackControllerSignal_t* pSignal = NULL;
    struct timespec oldLastBitrateChange;
    srcPlayerMode_t playerMode;



    if(pSession == NULL)
    {
        ERROR("invalid parameter");
        return HLS_INVALID_PARAMETER;
    }

    TIMESTAMP(DBG_INFO, "Starting %s", __FUNCTION__);

    do
    {
        /* Allocate a segment to keep a local copy of segment information */
        pSegmentCopy = newHlsSegment();
        if(pSegmentCopy == NULL) 
        {
            ERROR("newHlsSegment() failed");
            status = HLS_MEMORY_ERROR;
            break;
        }

        while(status == HLS_OK) 
        {
            /* If the downloader was signalled to exit, return HLS_CANCELLED */
            if(pSession->bKillDownloader) 
            {
                DEBUG(DBG_WARN, "downloader signalled to stop");
                status = HLS_CANCELLED;
                break;
            }

            /* Get current time */
            if(clock_gettime(CLOCK_MONOTONIC, &wakeTime) != 0) 
            {
                ERROR("failed to get current time");
                status = HLS_ERROR;
                break;
            }

            /* Get playlist READ lock */
            pthread_rwlock_rdlock(&(pSession->playlistRWLock));

            /* Get the current playlist */
            pMediaPlaylist = pSession->pCurrentPlaylist;
            if((pMediaPlaylist == NULL) || 
               (pMediaPlaylist->type != PL_MEDIA) || 
               (pMediaPlaylist->pMediaData == NULL)) 
            {
                ERROR("invalid playlist for playback");
                status = HLS_ERROR;
                /* Release playlist lock */
                pthread_rwlock_unlock(&(pSession->playlistRWLock));
                break;
            }

            /* Get the next segment */
            // GET NEXT SEGMENT SHOULD LOAD UP THE DECRYPTION INFOMATION RMS
            status = getNextSegment(pMediaPlaylist, &pSegment);
            if(status != HLS_OK) 
            {
                ERROR("failed to find next segment");
                /* Release playlist lock */
                pthread_rwlock_unlock(&(pSession->playlistRWLock));
                break;
            }

            /* Did we get a valid segment? */
            if(pSegment != NULL) 
            {
                /* Make a local copy of the segment */
                status = copyHlsSegment(pSegment, pSegmentCopy);
                if(status != HLS_OK) 
                {
                    ERROR("failed to make local segment copy");
                    /* Release playlist lock */
                    pthread_rwlock_unlock(&(pSession->playlistRWLock));
                    break;
                }

                /* Prepend the playlist's baseURL, if necessary */
                status = createFullURL(&(pSegmentCopy->URL), pMediaPlaylist->baseURL);
                if(status != HLS_OK) 
                {
                    ERROR("error creating full URL");
                    /* Release playlist lock */
                    pthread_rwlock_unlock(&(pSession->playlistRWLock));
                    break;
                }
                // here is the segment, if it's encrypted we need to attach
                // some decryption infomation here.
               
                // 
                // RMS RETARDED!!!! Key information was in the original pSegment structure
                // but instead of sending that the original author sends a copy.  WTF???
                //
                
                /* We no longer need a reference to the parsed segment */
                pSegment = NULL;
        
                /* Release playlist lock */
                pthread_rwlock_unlock(&(pSession->playlistRWLock));  

                /* Determine the player mode based on speed */
                if(pSession->speed == 0.0)
                {
                   playerMode = SRC_PLAYER_MODE_PAUSE;
                }
                else
                {
                   playerMode = SRC_PLAYER_MODE_NORMAL;
                }
                status = downloadAndPushSegment(pSession, pSegmentCopy, wakeTime, playerMode);
                if(status != HLS_OK) 
                {
                    if(status == HLS_CANCELLED) 
                    {
                        DEBUG(DBG_WARN, "downloader signalled to stop");
                        break;
                    }
                    else
                    {
                        ERROR("Failed to download segment");
                        break;
                    }
                }

                /* timeBuffered is also updated whenever we process a PTS
                   in the playerEvtCallback -- block those callbacks while
                   we update it here */
                pthread_mutex_lock(&(pSession->playerEvtMutex));
        
                /* Increment our buffer count */
                pSession->timeBuffered += pSegmentCopy->duration;
                
                /* Unblock the playerEvtCallback */
                pthread_mutex_unlock(&(pSession->playerEvtMutex)); 
    
                /* Get playlist WRITE lock */
                pthread_rwlock_wrlock(&(pSession->playlistRWLock));

                // Check if we want to switch bitrate, if we have > 1 variant
                if(pSession->pPlaylist->type == PL_VARIANT)
                {
                    if((pSession->pCurrentProgram != NULL) &&
                       (pSession->pCurrentProgram->pAvailableBitrates != NULL) && 
                       (pSession->pCurrentProgram->pStreams != NULL))
                    {
                        /* Save off pSession->lastBitrateChange in case we fail to shift and need to revert to old values */
                        oldLastBitrateChange.tv_sec = pSession->lastBitrateChange.tv_sec;
                        oldLastBitrateChange.tv_nsec = pSession->lastBitrateChange.tv_nsec;

                        proposedBitrateIndex = abrClientGetNewBitrate(pSession->lastSegmentDldRate, pSession->avgSegmentDldRate, (float)(pSession->timeBuffered),
                                                                      pSession->pCurrentProgram->pStreams->numElements, pSession->pCurrentProgram->pAvailableBitrates,
                                                                      pMediaPlaylist->pMediaData->bitrate, pSession->minBitrate, pSession->maxBitrate,
                                                                      &(pSession->lastBitrateChange), &(pSession->playbackStart));
                        if((proposedBitrateIndex < 0) || (proposedBitrateIndex >= pSession->pCurrentProgram->pStreams->numElements))
                        {
                            // TODO: ??? Anything else?
                            ERROR("Problem with bitrate window (rateMin/rateMax) prevented bitrate switching!");
                        }
                        else
                        {
                            status = changeBitrate(pSession, pSession->pCurrentProgram->pAvailableBitrates[proposedBitrateIndex]);
                            if(status != HLS_OK) 
                            {
                                if(status == HLS_DL_ERROR) 
                                {
                                    /* If we failed to switch because of a network error, keep going for now and
                                       try switching again on the next go-around. */
                                    DEBUG(DBG_WARN, "problem downloading new playlist for bitrate switch attempt -- will retry");

                                    /* Since we didn't shift bitrates, revert lastBitrateChange to old value (which was overwritten by abrClientGetNewBitrate() */

                                    // TODO: have the plugin update lastBitrateChange instead of the adaptec code?
                                    pSession->lastBitrateChange.tv_sec = oldLastBitrateChange.tv_sec;
                                    pSession->lastBitrateChange.tv_nsec = oldLastBitrateChange.tv_nsec;

                                    status = HLS_OK;
                                }
                                else
                                {
                                    ERROR("failed to change bitrate");
                                    /* Release playlist lock */
                                    pthread_rwlock_unlock(&(pSession->playlistRWLock));
                                    break;
                                }
                            }
                        }
                    }
                    else
                    {
                        ERROR("current program malformed or invalid");
                        status = HLS_ERROR;
                        /* Release playlist lock */
                        pthread_rwlock_unlock(&(pSession->playlistRWLock));
                        break;
                    }
                }
                else
                {
                    DEBUG(DBG_INFO, "Skipped bitrate-switching logic (playlist type is not PL_VARIANT)");
                }
    
                /* Release playlist lock */
                pthread_rwlock_unlock(&(pSession->playlistRWLock));
            }
            else
            {
                /* If we aren't playing then we never even got the first segment, so quit */
                if(pSession->state != HLS_PLAYING)
                {
                    ERROR("failed to get first segment");
                    status = HLS_ERROR;
                    /* Release playlist lock */
                    pthread_rwlock_unlock(&(pSession->playlistRWLock));
                    break;
                }

                /* Did this happen on a live or a VoD stream? */
                if(pSession->pCurrentPlaylist->pMediaData->bHaveCompletePlaylist) /* VoD stream */
                {
                    /* Release playlist lock */
                    pthread_rwlock_unlock(&(pSession->playlistRWLock));

                    /* If we didn't get a segment we've hit EOF */
                    DEBUG(DBG_NOISE,"EOF -- no more segments in VoD playlist");

                    /* If we've hit EOF on a VoD stream, then the downloader needs to signal the
                       playback controller and exit. The playback controller will monitor the
                       buffer level and signal EOF to the player once timeBuffered reaches 0. */

                    /* Allocate a new playback controller signal */
                    pSignal = malloc(sizeof(playbackControllerSignal_t));
                    if(pSignal == NULL) 
                    {
                        ERROR("malloc error");
                        status = HLS_MEMORY_ERROR;
                        break;
                    }
                    
                    *pSignal = PBC_DOWNLOAD_COMPLETE;
                    
                    /* Push the message to the playback controller message queue */
                    llerror = pushMsg(pSession->playbackControllerMsgQueue, (void*)pSignal);
                    if(llerror != LL_OK) 
                    {
                        ERROR("failed to signal the playback controller");
                        free(pSignal);
                        status = HLS_ERROR;
                        break;
                    }
                            
                    /* Release reference to signal -- playback controller will free */
                    pSignal = NULL;
                    
                    /* Stop the downloader thread */
                    pSession->bKillDownloader = 1;
                }
                else /* LIVE stream */
                {
                    /* Release playlist lock */
                    pthread_rwlock_unlock(&(pSession->playlistRWLock));

                    /* If we didn't get a segment we've hit EOS */
                    DEBUG(DBG_NOISE,"EOS -- no more segments in live playlist");
                    
                    /* If we have hit EOS on a live stream, then we just need
                       to wait for the playlist to update with new segments */

                    /* Lock the downloader wake mutex */
                    if(pthread_mutex_lock(&(pSession->downloaderWakeMutex)) != 0)
                    {
                        ERROR("failed to lock downloader wake mutex");
                        status = HLS_ERROR;
                        break;
                    }
                            
                    /* Wait for LOOP_SECS before going again */
                    wakeTime.tv_sec += DOWNLOADER_LOOP_SECS;
                        
                    DEBUG(DBG_NOISE,"sleeping %d seconds until %d", (int)DOWNLOADER_LOOP_SECS, (int)wakeTime.tv_sec);
                        
                    /* Wait until wakeTime */
                    pthread_status = PTHREAD_COND_TIMEDWAIT(&(pSession->downloaderWakeCond), &(pSession->downloaderWakeMutex), &wakeTime);
                        
                    /* Unlock the downloader wake mutex */
                    if(pthread_mutex_unlock(&(pSession->downloaderWakeMutex)) != 0)
                    {
                        ERROR("failed to unlock downloader wake mutex");
                        status = HLS_ERROR;
                        break;
                    }
                        
                    /* If the timedwait call failed we need to bail */
                    if((pthread_status != ETIMEDOUT) && (pthread_status != 0))
                    {
                        ERROR("failed to timedwait on the downloader wake condition");
                        status = HLS_ERROR;
                        break;
                    }
                }
            }

            /* Make sure we're still in a valid state */
            if(pSession->state == HLS_INVALID_STATE) 
            {
                status = HLS_STATE_ERROR;
                break;
            }
        }
        if(status != HLS_OK) 
        {
            break;
        }

    } while (0);

    /* Clean up */
    freeSegment(pSegmentCopy);

    return status;
}

hlsStatus_t hlsIFrameDownloadLoop(hlsSession_t* pSession)
{
    hlsStatus_t status = HLS_OK;

    hlsPlaylist_t* pMediaPlaylist = NULL;
    hlsSegment_t* pSegment = NULL;

    hlsSegment_t* pSegmentCopy = NULL;

    double trickDuration = 0;

    double segmentPositionFromEnd = 0;

    //int proposedBitrateIndex = 0;

    struct timespec wakeTime;

    playbackControllerSignal_t* pSignal = NULL;

    llStatus_t llerror = LL_OK;

    if(pSession == NULL)
    {
        ERROR("invalid parameter");
        return HLS_INVALID_PARAMETER;
    }

    TIMESTAMP(DBG_INFO, "Starting %s", __FUNCTION__);

    do
    {
        /* Allocate a segment to keep a local copy of segment information */
        pSegmentCopy = newHlsSegment();
        if(pSegmentCopy == NULL) 
        {
            ERROR("newHlsSegment() failed");
            status = HLS_MEMORY_ERROR;
            break;
        }

        /* Get current time as initial wakeTime */
        if(clock_gettime(CLOCK_MONOTONIC, &wakeTime) != 0) 
        {
            ERROR("failed to get current time");
            status = HLS_ERROR;
            break;
        }

        while(status == HLS_OK)
        {
            /* If the downloader was signalled to exit, return HLS_CANCELLED */
            if(pSession->bKillDownloader) 
            {
                DEBUG(DBG_WARN, "downloader signalled to stop");
                status = HLS_CANCELLED;
                break;
            }

            /* Get playlist READ lock */
            pthread_rwlock_rdlock(&(pSession->playlistRWLock));

            /* Get the current playlist */
            pMediaPlaylist = pSession->pCurrentPlaylist;
            if((pMediaPlaylist == NULL) || 
               (pMediaPlaylist->type != PL_MEDIA) || 
               (pMediaPlaylist->pMediaData == NULL)) 
            {
                ERROR("invalid playlist for playback");
                status = HLS_ERROR;
                /* Release playlist lock */
                pthread_rwlock_unlock(&(pSession->playlistRWLock));
                break;
            }

            /* Get the next I-frame */
            status = getNextIFrame(pMediaPlaylist, &pSegment, pSession->speed);
            if(status != HLS_OK) 
            {
                ERROR("failed to find next I-Frame");
                /* Release playlist lock */
                pthread_rwlock_unlock(&(pSession->playlistRWLock));
                break;
            }

            /* Did we get a valid segment? */
            if(pSegment != NULL) 
            {
                /* Make a local copy of the segment */
                status = copyHlsSegment(pSegment, pSegmentCopy);
                if(status != HLS_OK) 
                {
                    ERROR("failed to make local segment copy");
                    /* Release playlist lock */
                    pthread_rwlock_unlock(&(pSession->playlistRWLock));
                    break;
                }

                /* Prepend the playlist's baseURL, if necessary */
                status = createFullURL(&(pSegmentCopy->URL), pMediaPlaylist->baseURL);
                if(status != HLS_OK) 
                {
                    ERROR("error creating full URL");
                    /* Release playlist lock */
                    pthread_rwlock_unlock(&(pSession->playlistRWLock));
                    break;
                }

                DEBUG(DBG_NOISE,"Segment download URL: %s", pSegmentCopy->URL);

                /* If we are rewinding, the segmentDuration (i.e. the duration between
                   the current I-frame and the previous I-frame) is actually the duration
                   of the previous I-frame */
                if(pSession->speed < 0) 
                {
                    if(pSegment->pParentNode == NULL) 
                    {
                        ERROR("NULL parent node");
                        status = HLS_ERROR;
                        /* Release playlist lock */
                        pthread_rwlock_unlock(&(pSession->playlistRWLock));
                        break;
                    }

                    /* If we have the first I-frame in the playlist, then use the current I-frame duration
                       (since there is no previous I-frame). */
                    if(pSegment->pParentNode->pPrev == NULL) 
                    {
                        pSegmentCopy->duration = pSegment->duration;
                    }
                    else
                    {
                        if(pSegment->pParentNode->pPrev->pData == NULL) 
                        {
                            ERROR("empty segment node");
                            status = HLS_ERROR;
                            /* Release playlist lock */
                            pthread_rwlock_unlock(&(pSession->playlistRWLock));
                            break;
                        }
    
                        pSegmentCopy->duration = ((hlsSegment_t*)(pSegment->pParentNode->pPrev->pData))->duration;
                    }
                }

                /* Save this segment's positionFromEnd */
                status = getPositionFromEnd(pMediaPlaylist, pSegment, &segmentPositionFromEnd);
                if(status != HLS_OK) 
                {
                    ERROR("problem getting segment position");
                    break;
                }

                /* We no longer need a reference to the parsed segment */
                pSegment = NULL;
        
                /* Release playlist lock */
                pthread_rwlock_unlock(&(pSession->playlistRWLock));

                status = downloadAndPushSegment(pSession, pSegmentCopy, wakeTime, SRC_PLAYER_MODE_LOW_DELAY);
                if(status != HLS_OK) 
                {
                    if(status == HLS_CANCELLED) 
                    {
                        DEBUG(DBG_WARN, "downloader signalled to stop");
                        break;
                    }
                    else
                    {
                        ERROR("Failed to download segment");
                        break;
                    }
                }

                /* Calculate the display time for the new frame (i.e. how long
                   to wait before displaying the next frame) */

                /* Get current time */
                if(clock_gettime(CLOCK_MONOTONIC, &wakeTime) != 0) 
                {
                    ERROR("failed to get current time");
                    status = HLS_ERROR;
                    break;
                }

                DEBUG(DBG_INFO,"current time: %f", ((wakeTime.tv_sec)*1.0) + (wakeTime.tv_nsec/1000000000.0));
                
                /* Get the display time for the new frame */
                trickDuration = iFrameTrickDuration(pSegmentCopy->duration, pSession->speed);

                /* Set wakeTime to current time + trickDuration */
                wakeTime.tv_sec += floorf(trickDuration);
                wakeTime.tv_nsec += (trickDuration - floorf(trickDuration)) * 1000000000;

                /* Handle a rollover of the nanosecond portion of wakeTime */
                while(wakeTime.tv_nsec >= 1000000000) 
                {
                    wakeTime.tv_sec += 1;
                    wakeTime.tv_nsec -= 1000000000;
                }

                /* Get playlist WRITE lock */
                pthread_rwlock_wrlock(&(pSession->playlistRWLock));

                /* Since the I-frames are displayed as soon as we send them to the player, update our
                   current positionFromEnd here, instead of relying on PTS callbacks */
                pMediaPlaylist->pMediaData->positionFromEnd = segmentPositionFromEnd;

                // TODO: noise...
                DEBUG(DBG_INFO, "new position from end: %f seconds", segmentPositionFromEnd);                

                // TODO: Ignore bitrate switching for I-frames for now...
#if 0
                // Check if we want to switch bitrate, if we have > 1 variant
                if(pSession->pPlaylist->type == PL_VARIANT)
                {
                    if((pSession->pCurrentProgram != NULL) &&
                       (pSession->pCurrentProgram->pAvailableBitrates != NULL) && 
                       (pSession->pCurrentProgram->pStreams != NULL))
                    {
                        proposedBitrateIndex = abrClientGetNewBitrate(pSession->lastSegmentDldRate, pSession->avgSegmentDldRate, pSession->timeBuffered,
                                                                      pSession->pCurrentProgram->pStreams->numElements, pSession->pCurrentProgram->pAvailableBitrates,
                                                                      pMediaPlaylist->pMediaData->bitrate, pSession->minBitrate, pSession->maxBitrate,
                                                                      &(pSession->lastBitrateChange), &(pSession->playbackStart));
                        if((proposedBitrateIndex < 0) || (proposedBitrateIndex >= pSession->pCurrentProgram->pStreams->numElements))
                        {
                            // TODO: ??? Anything else?
                            ERROR("Problem with bitrate window (rateMin/rateMax) prevented bitrate switching!");
                        }
                        else
                        {
                            status = changeBitrate(pSession, pSession->pCurrentProgram->pAvailableBitrates[proposedBitrateIndex]);
                            if(status != HLS_OK) 
                            {
                                ERROR("failed to change bitrate");
                                /* Release playlist lock */
                                pthread_rwlock_unlock(&(pSession->playlistRWLock));
                                break;
                            }
                        }
                    }
                    else
                    {
                        ERROR("current program malformed or invalid");
                        status = HLS_ERROR;
                        /* Release playlist lock */
                        pthread_rwlock_unlock(&(pSession->playlistRWLock));
                        break;
                    }
                }
                else
                {
                    DEBUG(DBG_INFO, "Skipped bitrate-switching logic (playlist type is not PL_VARIANT)");
                }
#endif
    
                /* Release playlist lock */
                pthread_rwlock_unlock(&(pSession->playlistRWLock));
            }
            else
            {
                /* Release playlist lock */
                pthread_rwlock_unlock(&(pSession->playlistRWLock));

                /* If we aren't playing then we never even got the first I-frame, so quit */
                if(pSession->state != HLS_PLAYING)
                {
                    ERROR("failed to get first I-frame");
                    status = HLS_ERROR;
                    break;
                }

                /* If we didn't get an I-frame, signal the playback controller 
				   thread that the download is complete and the downloader is exiting. */

                /* Allocate a new playback controller signal */
                pSignal = malloc(sizeof(playbackControllerSignal_t));
                if(pSignal == NULL) 
                {
                    ERROR("malloc error");
                    status = HLS_MEMORY_ERROR;
                    break;
                }

                *pSignal = PBC_DOWNLOAD_COMPLETE;

                /* Push the message to the playback controller message queue */
                llerror = pushMsg(pSession->playbackControllerMsgQueue, (void*)pSignal);
                if(llerror != LL_OK) 
                {
                    ERROR("failed to signal the playback controller");
                    free(pSignal);
                    status = HLS_ERROR;
                    break;
                }
                    
                /* Release reference to signal -- playback controller will free */
                pSignal = NULL;

                /* Stop the downloader thread */
                pSession->bKillDownloader = 1;
            }

            /* Make sure we're still in a valid state */
            if(pSession->state == HLS_INVALID_STATE) 
            {
                status = HLS_STATE_ERROR;
                break;
            }
        }
        if(status != HLS_OK) 
        {
            break;
        }

    } while (0);

    /* Clean up */
    freeSegment(pSegmentCopy);

    return status;
}

/**
 *  
 * 
 * @param pSession - pointer to pre-allocated hlsSession_t 
 */
void hlsDownloaderThread(hlsSession_t* pSession)
{
    hlsStatus_t status = HLS_OK;

    if(pSession == NULL)
    {
        ERROR("pSession == NULL");
        pthread_exit(NULL);
    }

    TIMESTAMP(DBG_INFO, "Starting %s", __FUNCTION__);

    /* Is this normal playback or I-frame playback? */
    if((pSession->speed >= 0) && (pSession->speed <= 1))
    {
        status = hlsSegmentDownloadLoop(pSession);
    }
    else
    {
        status = hlsIFrameDownloadLoop(pSession);
    }

    pSession->downloaderStatus = status;

    if((status != HLS_OK) && (status != HLS_CANCELLED))
    {
        /* Send asynchronous error message to the player */
        srcPluginErr_t error;
        error.errCode = SRC_PLUGIN_ERR_SESSION_RESOURCE_FAILED;
        snprintf(error.errMsg, SRC_ERR_MSG_LEN, DEBUG_MSG("session %p download thread quit with status: %d", pSession, status));
        hlsPlayer_pluginErrCallback(pSession->pHandle, &error);

        ERROR("session %p download thread quit with status: %d", pSession, status);

        /* Put session in INVALID state */

        /* Block state changes */
        /*  pthread_mutex_lock(&(pSession->stateMutex)); */

        /* pSession->state = HLS_INVALID_STATE; */

        /* Leave critical section */
        /* pthread_mutex_unlock(&(pSession->stateMutex)); */
    }

    DEBUG(DBG_INFO,"session %p download thread exiting with status %d", pSession, status);
    pthread_exit(NULL);
}

#ifdef __cplusplus
}
#endif
