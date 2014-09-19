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
 * @file hlsDownloaderUtils.c @date February 9, 2012
 *  
 * @author Patryk Prus (pprus@cisco.com) 
 *  
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>

#include "hlsPlayerInterface.h"
#include "hlsSessionUtils.h"

#include "m3u8ParseUtils.h"

#include "adaptech.h"

#include "llUtils.h"
#include "curlUtils.h"
#include "hlsDownloaderUtils.h"
#include "debug.h"

/* Number of seconds to wait for a buffer from the player */
#define BUFFER_WAIT_SECS 1

/* Number of seconds to wait for more data to be downloaded */
#define DATA_WAIT_MSECS (10)

/* Local types */

/*! \struct asyncDlDesc_t
 * Structure for asyncSegmentDownloadThread() function
 */
typedef struct 
{
    hlsSession_t* pSession;         /*!< The session handle to operate on */
    hlsSegment_t* pSegment;         /*!< Pointer to the segment we want to download */
    int* pbDownloadComplete;        /*!< Pointer to flag which will be set to TRUE once the download completes */
    long* pBytesDownloaded;         /*!< Pointer to integer into which to write the number of bytes downloaded */
    int* pbKillThread;              /*!< Pointer to flag which will signal the thread to terminate when it is TRUE */
    hlsStatus_t* pDownloadStatus;   /*!< Pointer to #hlsStatus_t which will contain the thread's exit status */
    CURL* pCurl;                    /*!< Curl handle to use to download */ 
    pthread_mutex_t *curlMutex;     /*!< mutex to protect the curl handle */
} asyncDlDesc_t;

/* Local function prototypes */
void asyncSegmentDownloadThread(asyncDlDesc_t* pDesc);

/** 
 * Assumes calling thread has AT LEAST playlist READ lock 
 *  
 * (Actually, this function does write 
 * pLastDownloadedSegmentNode.  However, since this function is 
 * only ever called from the DL thread, and the only other place 
 * pLastDownloadedSegmentNode is used is changeBitrate, which 
 * requires a WRITE lock, we shouldn't ever be stepping on 
 * anyone's toes... )
 *  
 * TODO: may want to split out the get functionality separate 
 * from the setting of the pLastDownloadedSegmentNode... 
 *  
 * This function returns the next segment given the 
 * pLastDownloadedSegmentNode of *pMediaPlaylist.  If 
 * pLastDownloadedSegmentNode is NULL, returns either the first 
 * segment (VoD) or the segment 3*TARGET_DURATION from the end 
 * of the playlist (live). 
 *  
 * On return the pLastDownloadedSegmentNode and positionFromEnd 
 * for *pMediaPlaylist will be updated to new values.
 *  
 * If a new segment can't be found, the function assumes we are 
 * at EOF and returns HLS_OK and sets *ppSegment = NULL to 
 * signify. 
 *  
 * @param pMediaPlaylist - pointer to media playlist to pick 
 *                       segments from
 * @param ppSegment - on return will be either NULL (EOF or 
 *                  error) or point to the next segment to
 *                  download
 * 
 * @return #hlsStatus_t - HLS_OK on success or EOF, standard 
 *         error codes otherwise
 */
hlsStatus_t getNextSegment(hlsPlaylist_t* pMediaPlaylist, hlsSegment_t** ppSegment)
{
    hlsStatus_t rval = HLS_OK;
   
    llNode_t* pNode = NULL;
    double targetTime = 0;
    hlsSegment_t *pSeg = NULL;

    if((pMediaPlaylist == NULL) || (ppSegment == NULL) || (*ppSegment != NULL))
    {
        ERROR("invalid parameter");
        return HLS_INVALID_PARAMETER;
    }

    do
    {
        if(pMediaPlaylist->type != PL_MEDIA) 
        {
            ERROR("not a media playlist");
            rval = HLS_ERROR;
            break;
        }

        if(pMediaPlaylist->pMediaData == NULL) 
        {
            ERROR("media playlist data is NULL");
            rval = HLS_ERROR;
            break;
        }

        if(pMediaPlaylist->pList == NULL)
        {
            ERROR("invalid or empty segment list");
            rval = HLS_ERROR;
            break;
        }

        //TODO: what about EVENT playlists?  Start from live or beginning?

        /* If pLastDownloadedSegmentNode is NULL we want the 'first' segment.
           For VoD content this is the first segment in the playlist.
           For live content this is the segment that contains the last valid
           play position for the playlist (endOffset from the end). */
        if(pMediaPlaylist->pMediaData->pLastDownloadedSegmentNode == NULL) 
        {
            if(pMediaPlaylist->pMediaData->bHaveCompletePlaylist)
            {
                printf("Pulling the head for the segment\n");
                pNode = pMediaPlaylist->pList->pHead;
                if((pNode != NULL) && (pNode->pData != NULL))
                {
                    *ppSegment = (hlsSegment_t*)(pNode->pData);
                    //RMS ok have the first segment.
                    //
                    pSeg = *ppSegment;
                    printf("does this segment have any metadata in it? \n");
                    printf("%s \n",pSeg->keyURI);

                    /* Update the pLastDownloadedSegment */
                    pMediaPlaylist->pMediaData->pLastDownloadedSegmentNode = pNode;
                }
                else
                {
                    ERROR("invalid segment node");
                    rval = HLS_ERROR;
                    break;
                }
            }
            else
            {
                targetTime = pMediaPlaylist->pMediaData->endOffset;

                rval = getSegmentXSecFromEnd(pMediaPlaylist, targetTime, ppSegment);
                if(rval) 
                {
                    ERROR("problem getting initial segment");
                    break;
                }

                /* Update the pLastDownloadedSegmentNode */  
                if((*ppSegment)->pParentNode != NULL) 
                {
                    pMediaPlaylist->pMediaData->pLastDownloadedSegmentNode = (*ppSegment)->pParentNode;
                }
                else
                {
                    ERROR("segment has no parent node");
                    rval = HLS_ERROR;
                    break;
                }
            }

            /* Initialize the position from the end of the playlist */
            rval = getPositionFromEnd(pMediaPlaylist, *ppSegment, &(pMediaPlaylist->pMediaData->positionFromEnd));
            if(rval != HLS_OK) 
            {
                ERROR("problem setting initial playlist position");
                break;
            }
        }
        else
        {
            // TODO: do we need this check?
            if(pMediaPlaylist->pMediaData->pLastDownloadedSegmentNode->pData == NULL) 
            {
                ERROR("empty segment node");
                rval = HLS_ERROR;
                break;
            }

            if(pMediaPlaylist->pMediaData->pLastDownloadedSegmentNode->pNext == NULL)
            {
                /* We are at EOF. Return *ppSegment = NULL and HLS_OK
                   and assume the caller will handle it. */
                //TODO: noise...
                *ppSegment = NULL;
                DEBUG(DBG_INFO,"EOF!");
                break;
            }
            else
            {
                 DEBUG(DBG_NOISE,"this is the next segment");
                /* We want the next segment */
                *ppSegment = (hlsSegment_t*)(pMediaPlaylist->pMediaData->pLastDownloadedSegmentNode->pNext->pData);

                /* Ensure validity */
                if(*ppSegment == NULL) 
                {
                    ERROR("empty segment node");
                    rval = HLS_ERROR;
                    break;
                }

                /* Update the pLastDownloadedSegmentNode */
                pMediaPlaylist->pMediaData->pLastDownloadedSegmentNode = pMediaPlaylist->pMediaData->pLastDownloadedSegmentNode->pNext;
            }
        }

    } while (0);

    if(*ppSegment != NULL) 
    {
        // TODO: should probably be noise...
        DEBUG(DBG_INFO,"got segment: %d",(*ppSegment)->seqNum);
    }

    return rval;
}

/** 
 * This function calculates the duration of time that an I-frame 
 * should be displayed on the screen given the duration of the 
 * I-frame and the current playback speed.  The return value is 
 * limited to a minimum of 1/MAX_TRICK_FPS. 
 *  
 * If given a negative speed the return will still be a positive 
 * value. 
 * 
 * @param duration - original I-frame duration
 * @param speed - playback speed
 * 
 * @return double - I-frame duration at current speed -- always 
 *         positive
 */
double iFrameTrickDuration(double duration, float speed)
{
    double result = -1;

    if(duration < 0) 
    {
        ERROR("negative duration");
        return result;
    }

    if(speed < 0) 
    {
        speed *= -1;
    }

    result = duration/speed;

    if(result < (1.0/MAX_TRICK_FPS)) 
    {
        DEBUG(DBG_INFO,"limiting I-frame duration to 1/FPS");
        result = (1.0/MAX_TRICK_FPS);
    }

    DEBUG(DBG_INFO,"I-frame duration: %f", result);

    return result;
}

/**
 * Assumes calling thread has AT LEAST playlist READ lock
 *  
 * (Actually, this function does write
 * pLastDownloadedSegmentNode.  However, since this function is
 * only ever called from the DL thread, and the only other place
 * pLastDownloadedSegmentNode is used is changeBitrate, which
 * requires a WRITE lock, we shouldn't ever be stepping on
 * anyone's toes... )
 *  
 * TODO: may want to split out the get functionality separate
 * from the setting of the pLastDownloadedSegmentNode... 
 *  
 * This function returns the next I-frame which should be 
 * displayed given the pLastDownloadedSegmentNode of 
 * pMediaPlaylist and the current playback speed. If 
 * pLastDownloadedSegmentNode is NULL, returns either the fist 
 * I-frame (VoD) or the I-frame 3*TARGET_DURATION from the end 
 * of the playlist (live). 
 *  
 * On return the pLastDownloadedSegmentNode and positionFromEnd 
 * for *pMediaPlaylist will be updated to new values.
 *  
 * If a new I-frame can't be found, the function assumes we are 
 * at EOF or BOF (depending on playback direction) and returns 
 * HLS_OK and sets *ppSegment = NULL to signify.
 *  
 * @param pMediaPlaylist - pointer to media playlist to pick 
 *                       I-frames from
 * @param ppSegment - on return will be either NULL (error or 
 *                  EOF/BOF) or point to the hlsSegment_t for
 *                  the next I-frame
 * @param speed - playback speed
 * 
 * @return #hlsStatus_t - HLS_OK on success or EOF/BOF, standard
 *         error codes otherwise
 */
hlsStatus_t getNextIFrame(hlsPlaylist_t* pMediaPlaylist, hlsSegment_t** ppSegment, float speed)
{
    hlsStatus_t rval = HLS_OK;
   
    llNode_t* pNode = NULL;
    double targetPositionFromEnd = 0;
    double prevDuration = 0;

    if((pMediaPlaylist == NULL) || (ppSegment == NULL) || (*ppSegment != NULL))
    {
        ERROR("invalid parameter");
        return HLS_INVALID_PARAMETER;
    }

    do
    {
        if(pMediaPlaylist->type != PL_MEDIA) 
        {
            ERROR("not a media playlist");
            rval = HLS_ERROR;
            break;
        }

        if(pMediaPlaylist->pMediaData == NULL) 
        {
            ERROR("media playlist data is NULL");
            rval = HLS_ERROR;
            break;
        }

        if(!(pMediaPlaylist->pMediaData->bIframesOnly)) 
        {
            ERROR("media playlist is not an I-frame playlist");
            rval = HLS_ERROR;
            break;
        }

        if((pMediaPlaylist->pList == NULL) ||
           (pMediaPlaylist->pList->pHead == NULL) ||
           (pMediaPlaylist->pList->pTail == NULL))
        {
            ERROR("invalid or empty I-frame list");
            rval = HLS_ERROR;
            break;
        }

        /* If pLastDownloadedSegmentNode is NULL we want the 'first' I-frame.
           For VoD content this is the first I-frame in the playlist.
           For live content this is the I-frame that contains the last valid
           play position for the playlist (endOffset from the end). */
        if(pMediaPlaylist->pMediaData->pLastDownloadedSegmentNode == NULL) 
        {
            if(pMediaPlaylist->pMediaData->bHaveCompletePlaylist)
            {
                pNode = pMediaPlaylist->pList->pHead;
                if((pNode != NULL) && (pNode->pData != NULL))
                {
                    *ppSegment = (hlsSegment_t*)(pNode->pData);

                    /* Update the pLastDownloadedSegment */
                    pMediaPlaylist->pMediaData->pLastDownloadedSegmentNode = pNode;
                }
                else
                {
                    ERROR("invalid segment node");
                    rval = HLS_ERROR;
                    break;
                }
            }
            else
            {
                targetPositionFromEnd = pMediaPlaylist->pMediaData->endOffset;

                rval = getSegmentXSecFromEnd(pMediaPlaylist, targetPositionFromEnd, ppSegment);
                if(rval) 
                {
                    ERROR("problem getting initial I-frame");
                    break;
                }

                /* Update the pLastDownloadedSegmentNode */  
                if((*ppSegment)->pParentNode != NULL) 
                {
                    pMediaPlaylist->pMediaData->pLastDownloadedSegmentNode = (*ppSegment)->pParentNode;
                }
                else
                {
                    ERROR("segment has no parent node");
                    rval = HLS_ERROR;
                    break;
                }
            }

            /* Initialize the position from the end of the playlist */
            rval = getPositionFromEnd(pMediaPlaylist, *ppSegment, &(pMediaPlaylist->pMediaData->positionFromEnd));
            if(rval != HLS_OK) 
            {
                ERROR("problem setting initial playlist position");
                break;
            }
        }
        else
        {

            // TODO: need to somehow signal we crossed a discontinuity -- OR do we just send down that PAT/PMT for EACH I-frame?

            /* Are we going backwards or forwards? */
            if(speed > 0) /* FF */
            {
                /* Check node validity */
                if(pMediaPlaylist->pMediaData->pLastDownloadedSegmentNode->pData == NULL) 
                {
                    ERROR("empty segment node");
                    rval = HLS_ERROR;
                    break;
                }

                *ppSegment = pMediaPlaylist->pMediaData->pLastDownloadedSegmentNode->pData;

                /* If we're on the last I-Frame, signal EOF. Return *ppSegment = NULL
                   and HLS_OK and assume the caller will handle it. */
                if(pMediaPlaylist->pMediaData->pLastDownloadedSegmentNode->pNext == NULL) 
                {
                    *ppSegment = NULL;
                    DEBUG(DBG_INFO,"EOF!");
                    break;
                }

                /* Figure out how far we want to jump in the playlist */
                targetPositionFromEnd = iFrameTrickDuration((*ppSegment)->duration, speed);

                if(targetPositionFromEnd < 0) 
                {
                    ERROR("got negative i-frame duration");
                    rval = HLS_ERROR;
                    break;
                }

                targetPositionFromEnd *= speed;

                targetPositionFromEnd = pMediaPlaylist->pMediaData->positionFromEnd - targetPositionFromEnd;

                // TODO: noise...
                DEBUG(DBG_INFO, "current PFE: %f -- target PFE: %f", pMediaPlaylist->pMediaData->positionFromEnd, targetPositionFromEnd);

                /* If the positionFromEnd that we want is less than the playlist's endOffset, return
                 * the "last viable" I-frame. 
                 */
                if(targetPositionFromEnd < pMediaPlaylist->pMediaData->endOffset) 
                {
                    //TODO: noise...
                    DEBUG(DBG_INFO,"Target out of bounds, returning last I-frame within bounds");
                    targetPositionFromEnd = pMediaPlaylist->pMediaData->endOffset;
                }

                /* Find the I-frame at our desired position */
                rval = getSegmentXSecFromEnd(pMediaPlaylist, targetPositionFromEnd, ppSegment);
                if(rval) 
                {
                    ERROR("problem getting I-frame");
                    break;
                }

                /* If our current segment is already the "last valid" I-frame,
                   signal EOS. Return *ppSegment = NULL and HLS_OK and assume the
                   caller will handle it. */
                if(pMediaPlaylist->pMediaData->pLastDownloadedSegmentNode->pData == *ppSegment) 
                {
                    *ppSegment = NULL;
                    DEBUG(DBG_INFO,"EOS!");
                    break;
                }

                /* Update the pLastDownloadedSegmentNode */  
                if((*ppSegment)->pParentNode != NULL) 
                {
                    pMediaPlaylist->pMediaData->pLastDownloadedSegmentNode = (*ppSegment)->pParentNode;
                }
                else
                {
                    ERROR("segment has no parent node");
                    rval = HLS_ERROR;
                    break;
                }
            }
            else /* REW */
            {
                /* Check node validity */
                if(pMediaPlaylist->pMediaData->pLastDownloadedSegmentNode->pData == NULL) 
                {
                    ERROR("empty segment node");
                    rval = HLS_ERROR;
                    break;
                }

                *ppSegment = pMediaPlaylist->pMediaData->pLastDownloadedSegmentNode->pData;

                /* If we're on the first I-Frame, signal BOF. Return *ppSegment = NULL and
                   HLS_OK and assume the caller will handle it.*/
                if(pMediaPlaylist->pMediaData->pLastDownloadedSegmentNode->pPrev == NULL) 
                {
                    *ppSegment = NULL;
                    DEBUG(DBG_INFO,"BOF!");
                    break;
                }

                /* When going backwards the display duration of each I-frame (i.e. the time between the current I-frame and the previous one)
                   is actually the duration of the previous I-frame */

                /* Check previous node validity */
                if(pMediaPlaylist->pMediaData->pLastDownloadedSegmentNode->pPrev->pData == NULL) 
                {
                    ERROR("empty segment node");
                    rval = HLS_ERROR;
                    break;
                }

                prevDuration = ((hlsSegment_t*)(pMediaPlaylist->pMediaData->pLastDownloadedSegmentNode->pPrev->pData))->duration;

                /* Figure out how far we want to jump in the playlist */
                targetPositionFromEnd = iFrameTrickDuration(prevDuration, speed);

                if(targetPositionFromEnd < 0) 
                {
                    ERROR("got negative i-frame duration");
                    rval = HLS_ERROR;
                    break;
                }

                targetPositionFromEnd = targetPositionFromEnd * speed;

                targetPositionFromEnd = pMediaPlaylist->pMediaData->positionFromEnd - targetPositionFromEnd;

                // TODO: noise...
                DEBUG(DBG_INFO, "current PFE: %f -- target PFE: %f", pMediaPlaylist->pMediaData->positionFromEnd, targetPositionFromEnd);

                /* If the positionFromEnd that we want is greater than the playlist's (duration - startOffset), return
                 * the "last viable" I-frame. 
                 */
                if(targetPositionFromEnd > (pMediaPlaylist->pMediaData->duration - pMediaPlaylist->pMediaData->startOffset)) 
                {
                    //TODO: noise...
                    DEBUG(DBG_INFO,"Target out of bounds, returning last I-frame within bounds");
                    targetPositionFromEnd = pMediaPlaylist->pMediaData->duration - pMediaPlaylist->pMediaData->startOffset;
                }

                /* Find the I-frame at our desired position */
                rval = getSegmentXSecFromEnd(pMediaPlaylist, targetPositionFromEnd, ppSegment);
                if(rval) 
                {
                    ERROR("problem getting I-frame");
                    break;
                }

/* TODO: The following check for BOS breaks rewind functionality for HLS VOD content.
 * Need a live HLS content with I-Frames to fix it properly.
 */
#if 0
                /* If our current segment is already the "last valid" I-frame,
                   signal BOS. Return *ppSegment = NULL and HLS_OK and assume the
                   caller will handle it. */
                if(pMediaPlaylist->pMediaData->pLastDownloadedSegmentNode->pData == *ppSegment) 
                {
                    *ppSegment = NULL;
                    DEBUG(DBG_INFO,"BOS!");
                    break;
                }
#endif
                
                /* Update the pLastDownloadedSegmentNode */  
                if((*ppSegment)->pParentNode != NULL) 
                {
                    pMediaPlaylist->pMediaData->pLastDownloadedSegmentNode = (*ppSegment)->pParentNode;
                }
                else
                {
                    ERROR("segment has no parent node");
                    rval = HLS_ERROR;
                    break;
                }
            }
        }

    } while (0);

    if(*ppSegment != NULL) 
    {
        // TODO: should probably be noise...
        DEBUG(DBG_INFO,"got I-frame: %d",(*ppSegment)->seqNum);
    }

    return rval;
}

/**
 * This function updates the download/play position in playlist2 
 * to match playlist1 
 *  
 * @param pSession   - session we are operating on
 * @param pPlaylist1 - source playlist
 * @param pPlaylist2 - destination playlist
 * 
 * @return #hlsStatus_t 
 */
hlsStatus_t matchPlaylistPosition(hlsSession_t *pSession,
                                  hlsPlaylist_t *pPlaylist1, 
                                  hlsPlaylist_t *pPlaylist2)
{
   hlsStatus_t rval = HLS_OK;
   hlsSegment_t* pSegment = NULL;
   double targetPosition = 0;

   do 
   {
      if(pSession == NULL)
      {
         ERROR("invalid pSession parameter");
         rval = HLS_INVALID_PARAMETER;
         break;
      }
      /* Validate playlist1 */
      if((pPlaylist1 == NULL) ||
         (pPlaylist1->type != PL_MEDIA) || 
         (pPlaylist1->pMediaData == NULL)) 
      {
         ERROR("invalid media playlist1");
         rval = HLS_ERROR;
         break;
      }

      /* Validate playlist2 */
      if((pPlaylist2 == NULL) || 
         (pPlaylist2->type != PL_MEDIA) || 
         (pPlaylist2->pMediaData == NULL)) 
      {
         ERROR("invalid media playlist2");
         rval = HLS_ERROR;
         break;
      }

      /* If this is live, we need to update both playlists */ 
      if(!(pPlaylist1->pMediaData->bHaveCompletePlaylist))
      {
         rval = m3u8ParsePlaylist(pPlaylist1, pSession);
         if(rval != HLS_OK) 
         {
            ERROR("problem updating playlist1");
            break;
         }

         rval = m3u8ParsePlaylist(pPlaylist2, pSession);
         if(rval != HLS_OK) 
         {
            ERROR("problem updating playlist2");
            break;
         }
      }
      else if(!(pPlaylist2->pMediaData->bHaveCompletePlaylist)) 
      {
         /* If for some reason we have have all of playlist1
            but not all of the playlist2, update playlist2
            to synchronize them */
         rval = m3u8ParsePlaylist(pPlaylist2, pSession);
         if(rval != HLS_OK) 
         {
            ERROR("problem updating new playlist");
            break;
         }
      }

      /* If one playlist is rotating but the other isn't, we don't have any way of matching them up */
      if(pPlaylist1->pMediaData->bHaveCompletePlaylist != pPlaylist2->pMediaData->bHaveCompletePlaylist) 
      {
         ERROR("one playlist is VoD and one is Live -- can't sync");
         rval = HLS_ERROR;
         break;
      }

      /* If we've started fetching from playlist1, we want to match that in playlist2 */
      if(pPlaylist1->pMediaData->pLastDownloadedSegmentNode != NULL)
      {
         /* If we're currently playing, we want to match the two playlists based on the current DOWNLOAD position,
            i.e. the first data we fetch from the new playlist should directly follow the last piece of data we
            fetched from the playlist1.  So, we match on the positionFromEnd of playlist1's
            pLastDownloadedSegmentNode->next. */
         if(pSession->state == HLS_PLAYING) 
         {
            pSegment = (hlsSegment_t*)(pPlaylist1->pMediaData->pLastDownloadedSegmentNode->pData);

            if(pSegment == NULL) 
            {
               ERROR("invalid segment");
               rval = HLS_ERROR;
               break;
            }

            // TODO: noise
            DEBUG(DBG_INFO,"last downloaded segment %d", pSegment->seqNum);

            /* Get position from end of the last downloaded segment */
            rval = getPositionFromEnd(pPlaylist1, pSegment, &targetPosition);
            if(rval != HLS_OK) 
            {
               ERROR("problem getting position in old playlist");
               break;
            }

            // TODO: noise or delete
            DEBUG(DBG_INFO,"old position from end = %f", targetPosition);

            /* We want to match on the position of the next segment we would download from
               playlist1, so subtract the last downloaded segment's duration */
            targetPosition -= pSegment->duration;
         }
         else
         {
            /* If we're not currently PLAYING, then we want to match the two playlists based
               on the PLAY position, i.e. use the playlist1's current positionFromEnd. */
            targetPosition = pPlaylist1->pMediaData->positionFromEnd;
         }

         // TODO: noise or delete
         DEBUG(DBG_INFO,"matching position from end = %f", targetPosition);

         /* Find the next segment we would download from playlist2 (if targetPosition == 0
            this will return the last node in playlist2). */
         rval = getSegmentXSecFromEnd(pPlaylist2, targetPosition, &pSegment);
         if(rval != HLS_OK) 
         {
            ERROR("problem finding segment in new playlist");
            break;
         }

         if(pSegment == NULL) 
         {
            ERROR("invalid segment");
            rval = HLS_ERROR;
            break;
         }

         /* Update the pLastDownloadedSegmentNode */  
         if(pSegment->pParentNode != NULL)
         {
            /* There is a special case if the next segment we would download hasn't been
               added to the new playlist yet (i.e. targetPosition == 0) -- handle it here. */
            if(targetPosition == 0)
            {
               // TODO: noise
               DEBUG(DBG_INFO,"next segment will be %d", (pSegment->seqNum)+1);

               pPlaylist2->pMediaData->pLastDownloadedSegmentNode = pSegment->pParentNode;
            }
            else
            {
               // TODO: noise
               DEBUG(DBG_INFO,"next segment will be %d", pSegment->seqNum);

               pPlaylist2->pMediaData->pLastDownloadedSegmentNode = pSegment->pParentNode->pPrev;
            }
         }
         else
         {
            ERROR("segment has no parent node");
            rval = HLS_ERROR;
            break;
         }
      }
      else
      {
         /* If we haven't fetched anything from playlist1, treat playlist2 the same */
         pPlaylist2->pMediaData->pLastDownloadedSegmentNode = NULL;
      }

      /* Update the positionFromEnd (we'll need to update this if our guess was off) */
      pPlaylist2->pMediaData->positionFromEnd = pPlaylist1->pMediaData->positionFromEnd;

      /* TODO: verify that our guess was correct...
         compare PTS of old segment and new segment
         if the same --> we were right
         if different --> go back or forward 1 segment, repeat
         --> increment/decrement positionFromEnd in new playlist */
   }while(0);

   return rval;
}

/**
 * Assumes calling thread has playlist WRITE lock 
 *  
 * This function changes the pCurrentPlaylist for pSession to 
 * pNewMediaPlaylist.  The position in the new playlist is 
 * matched to the current position in the old playlist.  If this 
 * is a live asset, both playlist are updated prior to 
 * attempting to match them. 
 * 
 * @param pSession - session to operate on
 * @param pNewMediaPlaylist - playlist to change 
 *                          pCurrentPlaylist to 
 * 
 * @return #hls_status - HLS_OK on success, error code otherwise
 */
hlsStatus_t changeCurrentPlaylist(hlsSession_t* pSession, hlsPlaylist_t* pNewMediaPlaylist)
{
    hlsStatus_t rval = HLS_OK;

    srcPluginEvt_t event;
   
    if((pSession == NULL) || (pNewMediaPlaylist == NULL))
    {
        ERROR("invalid parameter");
        return HLS_INVALID_PARAMETER;
    }

    do
    {
        rval = matchPlaylistPosition(pSession, pSession->pCurrentPlaylist, pNewMediaPlaylist);
        if(HLS_OK != rval)
        {
           ERROR("Failed to match old and new playlist position\n");
           break;
        }
                        
        /* Update pCurrentPlaylist */
        pSession->pCurrentPlaylist = pNewMediaPlaylist;

        /* Signal player that we have switched bitrates */
        event.eventCode = SRC_PLUGIN_SWITCHED_BITRATE;
        event.pData = (long*)&(pSession->pCurrentPlaylist->pMediaData->bitrate);
        hlsPlayer_pluginEvtCallback(pSession->pHandle, &event);

    } while (0);

    return rval;
}

/** 
 * Assumes calling thread has playlist WRITE lock
 * 
 * @param pSession
 * @param newBitrate
 * 
 * @return #hls_status
 */
hlsStatus_t changeBitrate(hlsSession_t* pSession, int newBitrate)
{
    hlsStatus_t rval = HLS_OK;
   
    hlsPlaylist_t* pNewMediaPlaylist = NULL;

    if(pSession == NULL)
    {
        ERROR("invalid parameter");
        return HLS_INVALID_PARAMETER;
    }

    do
    {
        if(pSession->pPlaylist == NULL) 
        {
            ERROR("root playlist is NULL");
            rval = HLS_ERROR;
            break;
        }

        /* If there is only one bitrate, we are already "switched" */
        if(pSession->pPlaylist->type == PL_MEDIA) 
        {
            break;
        }

        /* Validate current playlist */
        if((pSession->pCurrentPlaylist == NULL) || 
           (pSession->pCurrentPlaylist->type != PL_MEDIA) || 
           (pSession->pCurrentPlaylist->pMediaData == NULL)) 
        {
            ERROR("invalid media playlist");
            rval = HLS_ERROR;
            break;
        }
     
        /* If we're already at the target bitrate, quit */
        if(newBitrate == pSession->pCurrentPlaylist->pMediaData->bitrate) 
        {
            break;
        }
        
        /* Validate current program */
        if(pSession->pCurrentProgram == NULL) 
        {
            ERROR("current program invalid");
            rval = HLS_ERROR;
            break;
        }

        /* Get playlist that matches newBitrate */
        rval = getPlaylistByBitrate(pSession->pCurrentProgram->pStreams, newBitrate, &pNewMediaPlaylist);
        if(rval != HLS_OK) 
        {
            ERROR("problem getting playlist with new bitrate");
            break;
        }

        rval = changeCurrentPlaylist(pSession, pNewMediaPlaylist);
        if(rval != HLS_OK) 
        {
            ERROR("problem switching playlists");
            break;
        }

    } while (0);

    return rval;
}

/**
 * This function downloads a segment and pushed the downloaded 
 * data to the player as it becomes available. 
 *  
 * If the download is interrupted by pSession->bKillDownloader 
 * == TRUE, the function returns HLS_CANCELLED. 
 * 
 * @param pSession - session we are operating on
 * @param pSegment - pointer to the #hlsSegment_t to download
 * @param waitTime - struct timespec specifying the delay (if 
 *                 any) before sending any data to the player
 * @param playerMode - #srcPlayerMode_t specifying what mode to 
 *                   put the player into once we have sent it
 *                   some data
 * @param streamNum - stream Number( main, media group streams...)
 * 
 * @return #hlsStatus_t 
 */
hlsStatus_t downloadAndPushSegment(hlsSession_t* pSession, 
                                   hlsSegment_t* pSegment, 
                                   struct timespec waitTime, 
                                   srcPlayerMode_t playerMode, 
                                   int streamNum)
{
    hlsStatus_t rval = HLS_OK;

    pthread_t asyncDlThread = 0;
    asyncDlDesc_t desc;
    int bDownloadComplete = 0;
    long bytesDownloaded = 0;
    hlsStatus_t dlStatus = HLS_OK;
    int bKillThread = 0;

    srcPlayerSetData_t playerSetData;

    char* filePath = NULL;

    FILE* fpRead = NULL;

    int pthread_status = 0;

    srcBufferMetadata_t bufferMeta;

    struct timespec wakeTime;

    srcStatus_t status = SRC_SUCCESS;

    char* buffer = NULL;
    int bufferSize = 0;
    int readSize = 0;

    long fileSize = 0;
    long bytesRead = 0;

    void    *pPrivate;
    char tag[128] = "";
    unsigned char *ptr = NULL;

    if((pSession == NULL) || (pSegment == NULL))
    {
        ERROR("invalid parameter");
        return HLS_INVALID_PARAMETER;
    }

    do
    {
        /* Generate the local download path */
        rval = getLocalPath(pSegment->URL, &(filePath), pSession->sessionName);
        if(rval != HLS_OK) 
        {
            ERROR("error generating local path");
            break;
        }

        DEBUG(DBG_NOISE, "opening %s for reading", filePath);

        /* Though this thread will only read from the file, we open it for writing
           and reading so that the file is created on the disk. */
        fpRead = fopen(filePath,"w+b");
        if(fpRead == NULL) 
        {
            ERROR("fopen() failed on file %s -- %s", filePath, strerror(errno));
            rval = HLS_FILE_ERROR;
            break;
        }

        /* Populate the async download descriptor structure */
        desc.pSession = pSession;
        desc.pSegment = pSegment;
        desc.pbDownloadComplete = &bDownloadComplete;
        desc.pBytesDownloaded = &bytesDownloaded;
        desc.pDownloadStatus = &dlStatus;
        desc.pbKillThread = &bKillThread;
        if(streamNum > SRC_STREAM_NUM_MAIN)
        {
           desc.pCurl = pSession->pMediaGroupCurl[streamNum - 1];
           desc.curlMutex = &pSession->mediaGroupCurlMutex[streamNum - 1];
        }
        else
        {
           desc.pCurl = pSession->pCurl;
           desc.curlMutex = &pSession->curlMutex;
        }

        /* Kick off a separate thread to go off and do the download */
        if(pthread_create(&(asyncDlThread), NULL, (void*)asyncSegmentDownloadThread, &desc))
        {
            ERROR("failed to create file download thread");
            rval = HLS_ERROR;
            break;
        }      

        /* Wait until waitTime to start pushing the data to the player */

        /* Lock the downloader wake mutex */
        if(pthread_mutex_lock(&(pSession->downloaderWakeMutex)) != 0)
        {
            ERROR("failed to lock downloader wake mutex");
            rval = HLS_ERROR;
            break;
        }
                    
        DEBUG(DBG_NOISE,"waiting until: %f", ((waitTime.tv_sec)*1.0) + (waitTime.tv_nsec/1000000000.0));
    
        /* Wait until waitTime */
        pthread_status = PTHREAD_COND_TIMEDWAIT(&(pSession->downloaderWakeCond), &(pSession->downloaderWakeMutex), &waitTime);
    
        /* Unlock the downloader wake mutex */
        if(pthread_mutex_unlock(&(pSession->downloaderWakeMutex)) != 0)
        {
            ERROR("failed to unlock downloader wake mutex");
            rval = HLS_ERROR;
            break;
        }
    
        /* If the timedwait call failed we need to bail */
        if((pthread_status != ETIMEDOUT) && (pthread_status != 0))
        {
            ERROR("failed to timedwait on the downloader wake condition with status %d", pthread_status);
            rval = HLS_ERROR;
            break;
        }
         // we now have the segment do some house keeping.
        /* Datafill buffer metadata struct */
        bufferMeta.encType = pSegment->encType;
        memcpy( bufferMeta.iv,pSegment->iv, 16);
        bufferMeta.keyURI = pSegment->keyURI;
        memcpy( bufferMeta.key,pSegment->key, 16);
        bufferMeta.streamNum = streamNum;
        /* Alternate streams + one main stream */
        bufferMeta.totalNumStreams = pSession->currentGroupCount + 1;
        bufferMeta.pts = INVALID_PTS;
        
        /* Keep going as long as we don't hit an error */
        while(rval == HLS_OK) 
        {
            /* If we were told to stop downloading, return HLS_CANCELLED */
            if(pSession->bKillDownloader) 
            {
                DEBUG(DBG_WARN, "download signalled to stop");
                rval = HLS_CANCELLED;
                break;
            }

            /* Check the download thread status */
            if(dlStatus != HLS_OK) 
            {
                ERROR("download thread reports error %d", dlStatus);
                rval = dlStatus;
                break;
            }

            /* Get current time */
            if(clock_gettime(CLOCK_MONOTONIC, &wakeTime) != 0) 
            {
                ERROR("failed to get current time");
                rval = HLS_ERROR;
                break;
            }

            /* Get a new buffer if we're not currently operating on one */
            if(buffer == NULL) 
            {
                bufferSize = 0;

                /* Get a buffer from the player */
                status = hlsPlayer_getBuffer(pSession->pHandle, &buffer, &bufferSize, &pPrivate);
                if(status != SRC_SUCCESS)
                {
                    ERROR("failed to get buffer from player");
                    rval = HLS_ERROR;
                    break;
                }

                DEBUG(DBG_NOISE, "got %d byte buffer", bufferSize);
            }

            /* Did we get a buffer? */
            if(bufferSize != 0) 
            {
                /* If this is encrypted content, we need to send down 16-byte aligned blocks. */
                if((bufferMeta.encType != SRC_ENC_NONE) && (bufferSize % 16 != 0))
                {
                    bufferSize = bufferSize - (bufferSize % 16);
                    DEBUG(DBG_INFO, "bufferSize adjusted to %d because of encryption", bufferSize);
                }

                /* If the above results in a bufferSize of 0, just send it back empty */
                if(bufferSize == 0)
                {
                    status = hlsPlayer_sendBuffer(pSession->pHandle, buffer, 0, &bufferMeta, pPrivate);
                    if(status != SRC_SUCCESS)
                    {
                        ERROR("failed to send buffer to player");
                        rval = HLS_ERROR;
                        break;
                    }
                }
                else
                {
                    /* If the download is complete, the fileSize is the number of bytes downloaded */
                    if(bDownloadComplete) 
                    {
                        fileSize = bytesDownloaded;
                    }
                    else
                    {
                        /* If the download is currently in progress, we need to find out the current size
                           of the file. */
                        
                        /* Move the pointer to the end of the file */
                        if(fseek(fpRead, 0, SEEK_END) == 0) 
                        {
                            /* Get the current byte offset */
                            fileSize = ftell(fpRead);
                            if(fileSize == -1) 
                            {
                                ERROR("ftell() failed on file %s -- %s", filePath, strerror(errno));
                                rval = HLS_FILE_ERROR;
                                break;
                            }

                            DEBUG(DBG_NOISE, "file size: %ld -- %ld new bytes available", fileSize, fileSize-bytesRead);

                            /* Is there enough new data to fill the buffer? */
                            if((fileSize - bytesRead) < bufferSize) 
                            {
                                /* If not, has the download completed? */
                                if(bDownloadComplete) 
                                {
                                    /* Adjust the amount to be read to match what is available */
                                    bufferSize = fileSize - bytesRead;
                                }
                                else
                                {
                                   /* Wait for more data... */
                                   time_t sec = DATA_WAIT_MSECS / 1000;
                                   long nsec = (DATA_WAIT_MSECS - (1000 * sec)) * 1000000;

                                   wakeTime.tv_sec += sec;

                                   if (wakeTime.tv_nsec + nsec > 1000000000)
                                       wakeTime.tv_sec++;

                                   wakeTime.tv_nsec = (wakeTime.tv_nsec + nsec) % 1000000000;

                                   DEBUG(DBG_NOISE, "wait for %d milliseconds until more data is downloaded", DATA_WAIT_MSECS);
                                   pthread_mutex_lock(&(pSession->downloaderWakeMutex));

                                   PTHREAD_COND_TIMEDWAIT(&(pSession->downloaderWakeCond), &(pSession->downloaderWakeMutex), &wakeTime);
                                   
                                   pthread_mutex_unlock(&(pSession->downloaderWakeMutex));

                                   continue;
                                }
                            }
                        }
                        else
                        {
                            ERROR("fseek() failed on file %s -- %s", filePath, strerror(errno));
                            rval = HLS_FILE_ERROR;
                            break;
                        }

                    } 

                    /* Seek to current read posiiton */
                    if(fseek(fpRead, bytesRead, SEEK_SET) != 0) 
                    {
                        ERROR("fseek() failed on file %s -- %s", filePath, strerror(errno));
                        rval = HLS_FILE_ERROR;
                        break;
                    }

                    /* Read from our input file */
                    readSize = fread(buffer, 1, bufferSize, fpRead);
        
                    DEBUG(DBG_NOISE,"read %d bytes -- wanted %d", readSize, bufferSize);

                    // incase the user does trick modes, we need to store
                    // the last 4 bytes of this segment so that the next
                    // segment can use that as the IV if requested.
                    // FIXME
                    //memcpy(pSegment->last4bytes,(buffer+(readSize -4)) , 4);
                    if(bytesRead == 0)
                    {
                       bufferMeta.bFirstBufferInSegment = 1;
                       /* Search for ID3 tag with com.apple.streaming.transportStreamTimestamp PRIV owner identifier
                         at the beginning of each segment */
                       if(readSize > 128)
                       {
                          memcpy(tag, buffer, 128);
                          tag[127] = 0;
                          DEBUG(DBG_INFO, "Checking for ID3 TAG...\n");
                          if ((ptr = (unsigned char *)strstr(&tag[20], "com.apple.streaming.transportStreamTimestamp")) != NULL)
                          {
                             bufferMeta.pts = ((unsigned long long)ptr[45] << 56) |
                                              ((unsigned long long)ptr[46] << 48) |
                                              ((unsigned long long)ptr[47] << 40) |
                                              ((unsigned long long)ptr[48] << 32) |
                                              ((unsigned long long)ptr[49] << 24) |
                                              ((unsigned long long)ptr[50] << 16) |
                                              ((unsigned long long)ptr[51] << 8) |
                                              ((unsigned long long)ptr[52]);
                             DEBUG(DBG_INFO, "Found com.apple.streaming.transportStreamTimestamp identifier, PTS: %llu\n", bufferMeta.pts);
                          }
                       }
                    }
                    else
                    {
                       bufferMeta.bFirstBufferInSegment = 0;
                    }

                    status = hlsPlayer_sendBuffer(pSession->pHandle, buffer, readSize, &bufferMeta, pPrivate);
                    if(status != SRC_SUCCESS)
                    {
                        ERROR("failed to send buffer to player");
                        rval = HLS_ERROR;
                        break;
                    }
                        
                    /* Check for file read error or EOF*/
                    if(readSize != bufferSize)
                    {
                        if(feof(fpRead) == 0)
                        {
                            ERROR("fread() failed on file %s -- %s", filePath, strerror(errno));
                            rval = HLS_FILE_ERROR;
                            break;
                        }
                    }
                    
                    /* Move to PLAYING state */
                    if(pSession->state == HLS_PREPARED) 
                    {
                       playerSetData.setCode = SRC_PLAYER_SET_MODE;
                       playerSetData.pData = &playerMode;
                       if(hlsPlayer_set(pSession->pHandle, &playerSetData) != SRC_SUCCESS)
                       {
                          ERROR("failed to set player mode to %d", playerMode);
                          status = HLS_ERROR;
                          break;
                       }

                       /* Set the session state to HLS_PLAYING */
                       pSession->state = HLS_PLAYING;

                       TIMESTAMP(DBG_INFO, "PLAYING");
                    }
            
                    /* Set playbackStart timestamp */
                    if(((float)(pSession->playbackStart.tv_sec)) == 0)
                    {
                        clock_gettime(CLOCK_MONOTONIC, &(pSession->playbackStart));
                        DEBUG(DBG_INFO, "Set playback start stamp to: %f", (float)(pSession->playbackStart.tv_sec));
                    }

                    /* Release our reference to the buffer */
                    buffer = NULL;

                    /* Increment the total bytes read */
                    bytesRead += readSize;         

                    DEBUG(DBG_NOISE, "%ld bytes read so far", bytesRead);
                    
                    /* Are we done? */
                    if(bDownloadComplete && (bytesRead == bytesDownloaded)) 
                    {
                        DEBUG(DBG_INFO, "download complete");
                        break;
                    }
                }
            }
            else
            {
               /* If we were told to stop downloading, return HLS_CANCELLED */
                if(pSession->bKillDownloader)
                {
                    DEBUG(DBG_WARN, "download signalled to stop");
                    rval = HLS_CANCELLED;
                    break;
                }

                /* If we get back a buffer of size 0, the player is out of buffers.  Wait for a bit then try again */
                DEBUG(DBG_INFO,"player out of buffers, back off for a second");

                /* Release our reference to the buffer */
                buffer = NULL;

                /* Lock the downloader wake mutex */
                if(pthread_mutex_lock(&(pSession->downloaderWakeMutex)) != 0)
                {
                    ERROR("failed to lock downloader wake mutex");
                    rval = HLS_ERROR;
                    break;
                }
                    
                /* Wait for LOOP_SECS before going again */
                wakeTime.tv_sec += BUFFER_WAIT_SECS;
    
                DEBUG(DBG_NOISE,"sleeping %d seconds until %d", (int)BUFFER_WAIT_SECS, (int)wakeTime.tv_sec);
    
                /* Wait until wakeTime */
                pthread_status = PTHREAD_COND_TIMEDWAIT(&(pSession->downloaderWakeCond), &(pSession->downloaderWakeMutex), &wakeTime);
    
                /* Unlock the downloader wake mutex */
                if(pthread_mutex_unlock(&(pSession->downloaderWakeMutex)) != 0)
                {
                    ERROR("failed to unlock downloader wake mutex");
                    rval = HLS_ERROR;
                    break;
                }
    
                /* If the timedwait call failed we need to bail */
                if((pthread_status != ETIMEDOUT) && (pthread_status != 0))
                {
                    ERROR("failed to timedwait on the downloader wake condition");
                    rval = HLS_ERROR;
                    break;
                }
            }
        }
        if(rval != HLS_OK)
        {
            break;
        }

    } while(0);
    
    /* Tell async thread to stop, if it hasn't already. */
    bKillThread = 1;

    /* Swallow the async thread */
    if(asyncDlThread != 0) 
    {
        pthread_join(asyncDlThread, NULL);
        asyncDlThread = 0;
    }
        
    /* If for some reason we are still holding a buffer (say we errored in the main loop)
       then send it back empty to make sure we don't leak memory. */
    if(buffer != NULL) 
    {
        status = hlsPlayer_sendBuffer(pSession->pHandle, buffer, 0, &bufferMeta, pPrivate);
        if(status != SRC_SUCCESS)
        {
            ERROR("failed to send buffer to player");
            rval = HLS_ERROR;
        }

        buffer = NULL;
    }

    /* Close file */
    if(fpRead != NULL) 
    {
        DEBUG(DBG_NOISE, "closing %s", filePath);                

        if(fclose(fpRead) != 0) 
        {
            ERROR("fclose() failed on file %s -- %s", filePath, strerror(errno));
            rval = HLS_FILE_ERROR;
        }

        fpRead = NULL;
    }

    /* Delete downloaded file */
    /* TODO: do we need to check for deletion errors? */
    unlink(filePath);

    free(filePath);
    filePath = NULL;

    return rval;
}

/**
 * Thread body which downloads a given segment using cURL. 
 *  
 * If the download is stopped via pDesc->pbKillThread, the 
 * thread will exit with pDesc->pDownloadStatus HLS_CANCELLED. 
 * 
 * @param pDesc - pointer to an #asyncDlDesc_t which gives all 
 *              the parameters used for the download
 */
void asyncSegmentDownloadThread(asyncDlDesc_t* pDesc)
{
    hlsStatus_t status = HLS_OK;

    long skipBytes = 0l;
    long dlOffset = 0l;
    long dlLength = 0l;

    char* filePath = NULL;
    FILE* fpWrite = NULL;
    float lastSegmentDldRate = 0.0f;   

    downloadHandle_t dlHandle;
    srcPluginErr_t error;

    if((pDesc == NULL) ||
       (pDesc->pSession == NULL) ||
       (pDesc->pSegment == NULL) ||
       (pDesc->pbDownloadComplete == NULL) ||
       (pDesc->pBytesDownloaded == NULL) ||
       (pDesc->pDownloadStatus == NULL) ||
       (pDesc->pbKillThread == NULL))
    {
        ERROR("invalid parameter");
        pthread_exit(NULL);
    }

    do
    {
        /* Clear download complete flag */
        *(pDesc->pbDownloadComplete) = 0;
      
        /* Generate the local download path */
        status = getLocalPath(pDesc->pSegment->URL, &(filePath), pDesc->pSession->sessionName);
        if(status != HLS_OK) 
        {
            ERROR("error generating local path");
            break;
        }
        
        DEBUG(DBG_NOISE, "opening %s for writing", filePath);

        /* Open file pointer to write downloaded file */
        fpWrite = fopen(filePath,"wb");
        if(fpWrite == NULL) 
        {
            ERROR("fopen() failed on file %s -- %s", filePath, strerror(errno));
            status = HLS_FILE_ERROR;
            break;
        }

        /* Populate download handle struct */
        dlHandle.fpTarget = fpWrite;
        dlHandle.pFileMutex = NULL;
        dlHandle.pbAbortDownload = pDesc->pbKillThread;

        /* Retry the download indefinitely */
        while(status == HLS_OK) 
        {
            if(*(pDesc->pbKillThread)) 
            {
                DEBUG(DBG_WARN, "segment download thread signalled to stop");
                status = HLS_CANCELLED;
                break;
            }

            /* Calculate our download range, accounting for any interrupted downloads */
            dlOffset = pDesc->pSegment->byteOffset + skipBytes;
            dlLength = pDesc->pSegment->byteLength;

            /* If this was supposed to be a byterange download, calculate the new range length */
            if(dlLength > 0) 
            {
                dlLength -= skipBytes;
            }

            /* Lock cURL mutex */
            pthread_mutex_lock(pDesc->curlMutex);

            /* Download segment */
            status = curlDownloadFile(pDesc->pCurl, pDesc->pSegment->URL, &dlHandle, dlOffset, dlLength);
            if(status)
            {
                if(status == HLS_CANCELLED) 
                {
                    DEBUG(DBG_WARN, "download stopped");
                    /* Unlock cURL mutex */
                    pthread_mutex_unlock(pDesc->curlMutex);
                    break;
                }
                else if(status == HLS_DL_ERROR) 
                {
                    /* If we ran into a network error, attempt to recover the download by resuming from where the failure took place.
                       To do that, we need to figure out how much data we have downloaded. */

                    status = HLS_OK;

                    /* Unlock cURL mutex */
                    pthread_mutex_unlock(pDesc->curlMutex);
                
                    /* Flush all data to disk */
                    if(fflush(fpWrite) != 0) 
                    {
                        ERROR("fflush() failed on file %s -- %s", filePath, strerror(errno));
                        status = HLS_FILE_ERROR;

                        break;
                    }

                    /* Get the total number of bytes written to disk */
                    *(pDesc->pBytesDownloaded) = ftell(fpWrite);
                    if(*(pDesc->pBytesDownloaded) == -1) 
                    {
                        ERROR("ftell() failed on file %s -- %s", filePath, strerror(errno));
                        status = HLS_FILE_ERROR;
                        break;
                    }

                    skipBytes = *(pDesc->pBytesDownloaded);

                    DEBUG(DBG_WARN, "ran into a network problem after downloading %ld bytes, will attempt to resume download", skipBytes);
                    error.errCode = SRC_PLUGIN_ERR_NETWORK;
                    snprintf(error.errMsg, SRC_ERR_MSG_LEN, DEBUG_MSG("session %p network error during segment download -- will retry", pDesc->pSession));
                    hlsPlayer_pluginErrCallback(pDesc->pSession->pHandle, &error);
                }
                else
                {
                    ERROR("failed to download segment");
                    /* Unlock cURL mutex */
                    pthread_mutex_unlock(pDesc->curlMutex);

                    break;
                }
            }
            else
            {
                /* Download successful -- leave the loop */

                break;
            }

            /* Sleep for a bit, then try again */
            usleep(DOWNLOAD_RETRY_WAIT_NSECS/1000);
        }
        if(status != HLS_OK) 
        {
            break;
        }

        /* Get the download throughput */
        status = getCurlTransferInfo(pDesc->pCurl, NULL, &lastSegmentDldRate, NULL);
        if(status)
        {
            ERROR("failed to get segment download rate");
            /* Unlock cURL mutex */
            pthread_mutex_unlock(pDesc->curlMutex);
            break;
        }
        
        /* Unlock cURL mutex */
        pthread_mutex_unlock(pDesc->curlMutex);
                
        /* Flush all data to disk */
        if(fflush(fpWrite) != 0) 
        {
            ERROR("fflush() failed on file %s -- %s", filePath, strerror(errno));
            status = HLS_FILE_ERROR;
            break;
        }

        /* Get the total number of bytes downloaded */
        *(pDesc->pBytesDownloaded) = ftell(fpWrite);
        if(*(pDesc->pBytesDownloaded) == -1) 
        {
            ERROR("ftell() failed on file %s -- %s", filePath, strerror(errno));
            status = HLS_FILE_ERROR;
            break;
        }

        DEBUG(DBG_INFO, "%ld bytes downloaded", *(pDesc->pBytesDownloaded));
        
        DEBUG(DBG_NOISE, "closing %s", filePath);                

        /* Close file */
        if(fclose(fpWrite) != 0) 
        {
            ERROR("fclose() failed on file %s -- %s", filePath, strerror(errno));
            status = HLS_FILE_ERROR;
            break;
        }

        fpWrite = NULL;

        pthread_mutex_lock(&(pDesc->pSession->dldRateMutex));
        pDesc->pSession->lastSegmentDldRate = lastSegmentDldRate;

        /* Add bitrate to exponentially weighted moving average for this session */
        if(pDesc->pSession->avgSegmentDldRate == 0)
        {
            pDesc->pSession->avgSegmentDldRate = pDesc->pSession->lastSegmentDldRate;
        }
        else
        {
            pDesc->pSession->avgSegmentDldRate = abrClientAddThroughputToAvg(pDesc->pSession->lastSegmentDldRate, pDesc->pSession->avgSegmentDldRate);
        }
        pthread_mutex_unlock(&(pDesc->pSession->dldRateMutex));

        /* Signal download complete */
        *(pDesc->pbDownloadComplete) = 1;

    } while(0);
    
    /* Close file if it happens to still be open */
    if(fpWrite != NULL) 
    {
        DEBUG(DBG_NOISE, "closing %s", filePath);                

        fclose(fpWrite);
        fpWrite = NULL;
    }

    free(filePath);
    filePath = NULL;

    *(pDesc->pDownloadStatus) = status;

    pthread_exit(NULL);
}

#ifdef __cplusplus
}
#endif
