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
 * @file hlsSessionUtils.c @date February 9, 2012
 *  
 * @author Patryk Prus (pprus@cisco.com) 
 *  
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <math.h>
#include <string.h>

#include "hlsSessionUtils.h"

#include "hlsDownloaderUtils.h"

#include "AbrAlgorithmInterface.h"

#include "debug.h"

/**
 * Gets the best bitrate for the session based on the min/max
 * and target bitrate.
 *  
 * Assumes calling thread has AT LEAST playlist READ lock 
 *  
 * @param pSession - pointer to session to operate on
 * @param pBitrate - on return will contain the best bitrate
 * 
 * @return #hlsStatus_t
 */
hlsStatus_t getBestBitrate(hlsSession_t* pSession, int* pBitrate)
{
    hlsStatus_t rval = HLS_OK;

    int targetDiff = INT_MAX;
    
    int i = 0;        

    if((pSession == NULL) || (pBitrate == NULL))
    {
        ERROR("invalid parameter");
        return HLS_INVALID_PARAMETER;
    }

    do
    {
        /* Make sure we have a valid root playlist */
        if(pSession->pPlaylist == NULL) 
        {
            ERROR("NULL root playlist");
            rval = HLS_ERROR;
            break;
        }

        *pBitrate = 0;

        switch(pSession->pPlaylist->type) 
        {
            case PL_MEDIA:
                if(pSession->pPlaylist->pMediaData == NULL) 
                {
                    ERROR("media playlist data is NULL");
                    rval = HLS_ERROR;
                    break;
                }

                /* If our root playlist is a media playlist, we only have one bitrate to pick from */
                *pBitrate = pSession->pPlaylist->pMediaData->bitrate;
                break;
            case PL_VARIANT:
                /* Make sure everything is in order... */
                if((pSession->pCurrentProgram == NULL) ||
                   (pSession->pCurrentProgram->pStreams == NULL) ||
                   (pSession->pCurrentProgram->pAvailableBitrates == NULL)) 
                {
                    ERROR("current program is invalid");
                    rval = HLS_ERROR;
                    break;
                }

                /* Loop through our bitrates */
                for(i = 0; i < pSession->pCurrentProgram->pStreams->numElements; i++) 
                {
                    /* Is the bitrate within the range and closer to target? */
                    if(((pSession->pCurrentProgram->pAvailableBitrates[i]) >= pSession->minBitrate) && 
                       ((pSession->pCurrentProgram->pAvailableBitrates[i]) <= pSession->maxBitrate) &&
                       (abs(pSession->pCurrentProgram->pAvailableBitrates[i] - pSession->targetBitrate) < targetDiff)) 
                    {
                        *pBitrate = pSession->pCurrentProgram->pAvailableBitrates[i];
                        targetDiff = abs(*pBitrate - pSession->targetBitrate);
                    }
                }
        
                if(*pBitrate == 0) 
                {
                    ERROR("no valid bitrate found in range");
                    rval = HLS_ERROR;
                    break;
                }

                DEBUG(DBG_INFO, "best bitrate: %d", *pBitrate);
       
                break;
            default:
                break;
        }
        if(rval != HLS_OK) 
        {
            break;
        }

    } while(0);

    return rval;
}

/**
 * Assumes calling thread has AT LEAST playlist READ lock
 * 
 * @param pPlaylistList
 * @param bitrate
 * @param ppMediaPlaylist
 * 
 * @return #hlsStatus_t
 */
hlsStatus_t getPlaylistByBitrate(llist_t* pPlaylistList, int bitrate, hlsPlaylist_t** ppMediaPlaylist)
{
    hlsStatus_t rval = HLS_OK;

    llNode_t* pNode = NULL;

    if((pPlaylistList == NULL) || (ppMediaPlaylist == NULL) || (*ppMediaPlaylist != NULL))
    {
        ERROR("invalid parameter");
        return HLS_INVALID_PARAMETER;
    }

    do
    {
        /* Validate playlist list */
        if(pPlaylistList->numElements == 0) 
        {
            ERROR("no entries in playlist list");
            rval = HLS_ERROR;
            break;
        }

        pNode = pPlaylistList->pHead;
        while(pNode != NULL) 
        {
            *ppMediaPlaylist = (hlsPlaylist_t*)(pNode->pData);
                    
            if((*ppMediaPlaylist != NULL) &&
               ((*ppMediaPlaylist)->type == PL_MEDIA) &&
               ((*ppMediaPlaylist)->pMediaData != NULL))
            {
                if((*ppMediaPlaylist)->pMediaData->bitrate == bitrate) 
                {
                    break;
                }
            }

            pNode = pNode->pNext;
        }
        if(rval) 
        {
            break;
        }

        /* Did we find a matching playlist? */
        if(pNode == NULL) 
        {
            ERROR("Could not find playlist with bitrate %d", bitrate);
            rval = HLS_ERROR;
            break;
        }

    } while (0);

    return rval;
}

/** 
 * Returns the playlist duration (in seconds) as it would be 
 * presented to an external player.  i.e. bounded by the 
 * playlist's startOffset and endOffset, if applicable. 
 *  
 * Assumes calling thread has AT LEAST playlist READ lock 
 * 
 * @param pMediaPlaylist
 * @param pDuration
 * 
 * @return hlsStatus_t
 */
hlsStatus_t getExternalDuration(hlsPlaylist_t* pMediaPlaylist, double* pDuration)
{
    hlsStatus_t rval = HLS_OK;
    
    if((pMediaPlaylist == NULL) || (pDuration == NULL))
    {
        ERROR("invalid parameter");
        return HLS_INVALID_PARAMETER;
    }

    do
    {
        if((pMediaPlaylist->type != PL_MEDIA) ||
           (pMediaPlaylist->pMediaData == NULL))
        {
            ERROR("invalid media playlist");
            rval = HLS_ERROR;
            break;
        }

        DEBUG(DBG_NOISE, "%f = %f - %f - %f", pMediaPlaylist->pMediaData->duration - pMediaPlaylist->pMediaData->startOffset - pMediaPlaylist->pMediaData->endOffset,
              pMediaPlaylist->pMediaData->duration, pMediaPlaylist->pMediaData->startOffset, pMediaPlaylist->pMediaData->endOffset);

        /* The playlist duration is the total playlist duration minus its startOffset and endOffset */
        *pDuration = pMediaPlaylist->pMediaData->duration;
        *pDuration -= pMediaPlaylist->pMediaData->startOffset;
        *pDuration -= pMediaPlaylist->pMediaData->endOffset;

        /* If the total playlist duration is less than the sum of these bounds, the duration is
           effectively 0 and no trick modes are possible*/
        if(*pDuration < 0) 
        {
            *pDuration = 0;
        }

    } while(0);

    return rval;
}

/**
 * Returns the playlist position (in seconds) as it would be 
 * presented to an external player.  i.e. bounded by the 
 * playlist's startOffset and endOffset, if applicable. 
 *  
 * Assumes calling thread has AT LEAST playlist READ lock 
 * 
 * @return hlsStatus_t
 * @param pMediaPlaylist
 * 
 * @param pPosition
 * 
 */
hlsStatus_t getExternalPosition(hlsPlaylist_t* pMediaPlaylist, double* pPosition)
{
    hlsStatus_t rval = HLS_OK;

    double externalDuration = 0;
    
    if((pMediaPlaylist == NULL) || (pPosition == NULL))
    {
        ERROR("invalid parameter");
        return HLS_INVALID_PARAMETER;
    }

    do
    {
        if((pMediaPlaylist->type != PL_MEDIA) ||
           (pMediaPlaylist->pMediaData == NULL))
        {
            ERROR("invalid media playlist");
            rval = HLS_ERROR;
            break;
        }

        DEBUG(DBG_NOISE, "%f = %f - %f - %f", pMediaPlaylist->pMediaData->duration - pMediaPlaylist->pMediaData->positionFromEnd - pMediaPlaylist->pMediaData->startOffset,
              pMediaPlaylist->pMediaData->duration, pMediaPlaylist->pMediaData->positionFromEnd, pMediaPlaylist->pMediaData->startOffset);

        /* Our current position is the playlist duration minus our current positionFromEnd
           minus our startOffset. */
        *pPosition = pMediaPlaylist->pMediaData->duration - pMediaPlaylist->pMediaData->positionFromEnd;
        *pPosition -= pMediaPlaylist->pMediaData->startOffset;

        /* Get the externally visible duration of the playlist */
        rval = getExternalDuration(pMediaPlaylist, &externalDuration);
        if(rval != HLS_OK) 
        {
            ERROR("failed to get exernal duration");
            break;
        }

        /* Because of the playlist structure it is possible that we are playing at an absolute position
           that is outside of the valid range defined by our startOffset and endOffset. If so,
           bound the position to [0, externalDuration] so that we never report an invalid position to the player. */
        if(*pPosition < 0) 
        {
            *pPosition = 0;
        }
        else if(*pPosition > externalDuration) 
        {
            *pPosition = externalDuration;
        }

    } while(0);

    return rval;
}

/** 
 * Assumes calling thread has AT LEAST playlist READ lock 
 * 
 * @param pMediaPlaylist
 * @param x
 * @param ppSegment
 * 
 * @return #hlsStatus_t
 */
hlsStatus_t getSegmentXSecFromEnd(hlsPlaylist_t* pMediaPlaylist, double x, hlsSegment_t** ppSegment)
{
    hlsStatus_t rval = HLS_OK;
    double targetTime;
    llNode_t* pNode = NULL;

    if (x < 0)
       x = 0;

    targetTime = x;
    
    if((pMediaPlaylist == NULL) || (ppSegment == NULL))
    {
        ERROR("invalid parameter, pMediaPlaylist: %p, ppSegment: %p, x: %llf", pMediaPlaylist, ppSegment, x);
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

        *ppSegment = NULL;

        if(pMediaPlaylist->pList == NULL)
        {
            ERROR("invalid or empty segment list");
            rval = HLS_ERROR;
            break;
        }

        /* If we want to jump back more than the duration of the playlist,
           just return the HEAD of the list */
        if(x >= pMediaPlaylist->pMediaData->duration) 
        {
            DEBUG(DBG_INFO,"x = %5.2f s, playlist only %5.2f s long -- returning HEAD", x, pMediaPlaylist->pMediaData->duration);
            pNode = pMediaPlaylist->pList->pHead;

            if(pNode != NULL) 
            {
                *ppSegment = (hlsSegment_t*)(pNode->pData);

                if(*ppSegment == NULL) 
                {
                    ERROR("invalid segment node");
                    rval = HLS_ERROR;
                    break;
                }
            }
            else
            {
                ERROR("invalid or empty segment list");
                rval = HLS_ERROR;
                break;
            }

        }
        else
        {
            /* Start at the end of the list */
            pNode = pMediaPlaylist->pList->pTail;
    
            if(pNode == NULL) 
            {
                ERROR("invalid or empty segment list");
                rval = HLS_ERROR;
                break;
            }
            
            /* Loop  until we get to a segment that starts 'x' or more seconds
               from the end. */
            do
            {
                *ppSegment = (hlsSegment_t*)(pNode->pData);
    
                if(*ppSegment == NULL) 
                {
                    ERROR("invalid segment node");
                    rval = HLS_ERROR;
                    break;
                }


                targetTime -= (*ppSegment)->duration;
                pNode = pNode->pPrev;

                /* If our segment durations are floating point numbers, the
                   floating point error can result in:
                   playlistDuration - (sum of all segment durations) != 0
                   So, use 0.1 millisecond as the smallest resolution.
                   i.e.: if (-0.0001 < x < 0.0001) --> x == 0 (effectively)
                 
                   Once targetTime hits 0, this is the segment we want */
            } while((pNode != NULL) && (targetTime > 0.0001));
            if(rval) 
            {
                break;
            }
        }

    } while(0);

    return rval;
}

/** 
 * Assumes calling thread has AT LEAST playlist READ lock 
 * 
 * @param pMediaPlaylist
 * @param x
 * @param ppSegment
 * 
 * @return #hlsStatus_t
 */
hlsStatus_t getSegmentXSecFromStart(hlsPlaylist_t* pMediaPlaylist, double x, hlsSegment_t** ppSegment)
{
    hlsStatus_t rval = HLS_OK;
    
    llNode_t* pNode = NULL;
    double targetTime;

    if (x < 0)
       x = 0;

    targetTime = x;
    
    if((pMediaPlaylist == NULL) || (ppSegment == NULL))
    {
        ERROR("invalid parameter, pMediaPlaylist: %p, ppSegment: %p, x: %llf", pMediaPlaylist, ppSegment, x);
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

        *ppSegment = NULL;

        if(pMediaPlaylist->pList == NULL)
        {
            ERROR("invalid or empty segment list");
            rval = HLS_ERROR;
            break;
        }

        /* If we want to jump forward more than the duration of the playlist,
           just return the TAIL of the list */
        if(x >= pMediaPlaylist->pMediaData->duration) 
        {
            DEBUG(DBG_INFO,"x = %5.2f s, playlist only %5.2f s long -- returning TAIL", x, pMediaPlaylist->pMediaData->duration);
            pNode = pMediaPlaylist->pList->pTail;

            if(pNode != NULL) 
            {
                *ppSegment = (hlsSegment_t*)(pNode->pData);

                if(*ppSegment == NULL) 
                {
                    ERROR("invalid segment node");
                    rval = HLS_ERROR;
                    break;
                }
            }
            else
            {
                ERROR("invalid or empty segment list");
                rval = HLS_ERROR;
                break;
            }

        }
        else
        {
            /* Start at the beginning of the list */
            pNode = pMediaPlaylist->pList->pHead;
    
            if(pNode == NULL) 
            {
                ERROR("invalid or empty segment list");
                rval = HLS_ERROR;
                break;
            }
            
            /* Loop  until we get to a segment that contains the data
               'x' seconds from the start. */
            do
            {
                *ppSegment = (hlsSegment_t*)(pNode->pData);
    
                if(*ppSegment == NULL) 
                {
                    ERROR("invalid segment node");
                    rval = HLS_ERROR;
                    break;
                }
    
                targetTime -= (*ppSegment)->duration;
                pNode = pNode->pNext;

                /* If our segment durations are floating point numbers, the
                   floating point error can result in:
                   playlistDuration - (sum of all segment durations) != 0
                   So, use 0.1 millisecond as the smallest resolution.
                   i.e.: if (-0.0001 < x < 0.0001) --> x == 0 (effectively)
                 
                   Once targetTime hits 0, the NEXT segment is the one we want. */
            } while((pNode != NULL) && (targetTime > -0.0001));
            if(rval) 
            {
                break;
            }
        }

    } while(0);

    return rval;
}

/** 
 * Assumes calling thread has AT LEAST playlist READ lock 
 * 
 * @param pMediaPlaylist
 * @param pSegment
 * @param pSeconds
 * 
 * @return #hlsStatus_t
 */
hlsStatus_t getPositionFromEnd(hlsPlaylist_t* pMediaPlaylist, hlsSegment_t* pSegment, double* pSeconds)
{
    hlsStatus_t rval = HLS_OK;

    llNode_t* pNode = NULL;

    if((pMediaPlaylist == NULL) || (pSegment == NULL) || (pSeconds == NULL))
    {
        ERROR("invalid parameter");
        return HLS_INVALID_PARAMETER;
    }

    do
    {
        *pSeconds = 0;

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

        /* Start at the end of the list */
        pNode = pMediaPlaylist->pList->pTail;

        /* Loop until we find our segment */
        while(pNode != NULL) 
        {
            *pSeconds += ((hlsSegment_t*)(pNode->pData))->duration;

            if(pSegment->seqNum == ((hlsSegment_t*)(pNode->pData))->seqNum) 
            {
                /* We found the segment */
                break;
            }

            pNode = pNode->pPrev;
        }

        /* Did we find the segment? */
        if(pNode == NULL) 
        {
            ERROR("segment %d not in list", pSegment->seqNum);
            rval = HLS_ERROR;
            break;
        }

    } while(0);

    return rval;
}

/** 
 * Assumes calling thread has AT LEAST playlist READ lock 
 * 
 * @param pMediaPlaylist
 * @param pSegment
 * @param pSeconds
 * 
 * @return #hlsStatus_t
 */
hlsStatus_t getPositionFromStart(hlsPlaylist_t* pMediaPlaylist, hlsSegment_t* pSegment, double* pSeconds)
{
    hlsStatus_t rval = HLS_OK;

    llNode_t* pNode = NULL;

    if((pMediaPlaylist == NULL) || (pSegment == NULL) || (pSeconds == NULL))
    {
        ERROR("invalid parameter");
        return HLS_INVALID_PARAMETER;
    }

    do
    {
        *pSeconds = 0;

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

        /* Start at the beginning of the list */
        pNode = pMediaPlaylist->pList->pHead;

        /* Loop until we find our segment */
        while(pNode != NULL) 
        {
            if(pSegment->seqNum == ((hlsSegment_t*)(pNode->pData))->seqNum) 
            {
                /* We found the segment */
                break;
            }

            *pSeconds += ((hlsSegment_t*)(pNode->pData))->duration;

            pNode = pNode->pNext;
        }

        /* Did we find the segment? */
        if(pNode == NULL) 
        {
            ERROR("segment %d not in list", pSegment->seqNum);
            rval = HLS_ERROR;
            break;
        }

    } while(0);

    return rval;
}

/**
 * Sets the pLastDownloadedSegmentNode to match the current
 * positionFromEnd for the playlist.  Essentially this syncs the
 * last segment downloaded by the plugin to the last segment 
 * played by the media player. 
 *  
 * Assumes calling thread has playlist WRITE lock
 *  
 * @param pMediaPlaylist - media playlist to operate on
 * 
 * @return #hlsStatus_t = HLS_OK on success, error code otherwise
 */
hlsStatus_t flushPlaylist(hlsPlaylist_t* pMediaPlaylist)
{
    hlsStatus_t rval = HLS_OK;

    hlsSegment_t* pSegment = NULL;
    
    if(pMediaPlaylist == NULL)
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

        /* Find the segment which contains the positionFromEnd */
        rval = getSegmentXSecFromEnd(pMediaPlaylist, pMediaPlaylist->pMediaData->positionFromEnd, &pSegment);
        if(rval != HLS_OK) 
        {
            ERROR("problem finding segment in playlist");
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
            // TODO: noise
            DEBUG(DBG_INFO,"flushing playlist: next segment will be %d", (pSegment->seqNum)+1);

            pMediaPlaylist->pMediaData->pLastDownloadedSegmentNode = pSegment->pParentNode;
        }
        else
        {
            ERROR("segment has no parent node");
            rval = HLS_ERROR;
            break;
        }

    } while(0);

    return rval;
}

/**
 * Switches the session from playing from the standard media 
 * playlists to I-frame playlists.  On return, 
 * pSession->pCurrentPlaylist will point to an I-frame only 
 * playlist.  If there are multiple I-frame playlists, will pick 
 * the one that is closest to the avgSegmentDldRate. 
 *  
 * Assumes calling thread has playlist WRITE lock
 * 
 * @param pSession - session to operate on
 * 
 * @return #hlsStatus_t - HLS_OK on success, error code otherwise
 */
hlsStatus_t switchToIFramePlaylists(hlsSession_t* pSession)
{
    hlsStatus_t rval = HLS_OK;
    hlsPlaylist_t* pPlaylist = NULL;

    int proposedBitrateIndex = 0;
   
    if(pSession == NULL)
    {
        ERROR("invalid parameter");
        return HLS_INVALID_PARAMETER;
    }

    do
    {
        /* Make sure we have I-frame playlists to switch to */
        if((pSession->pCurrentProgram == NULL) ||
           (pSession->pCurrentProgram->pAvailableIFrameBitrates == NULL) ||
           (pSession->pCurrentProgram->pIFrameStreams == NULL)) 
        {
            ERROR("No I-frame playlists available");
            rval = HLS_ERROR;
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

        /* Find I-frame playlist bitrate to switch to.  Ignore any max and min bitrate values
           since I-frame playlist bitrates most certainly would fall outside of them. */
        proposedBitrateIndex = abrClientGetBitrateIndex(pSession->avgSegmentDldRate, 0, INT_MAX, 
                                                        pSession->pCurrentProgram->pIFrameStreams->numElements, 
                                                        pSession->pCurrentProgram->pAvailableIFrameBitrates);

        if((proposedBitrateIndex < 0) || (proposedBitrateIndex >= pSession->pCurrentProgram->pIFrameStreams->numElements))
        {
            ERROR("Could not find valid I-frame bitrate");
            rval = HLS_ERROR;
            break;
        }

        DEBUG(DBG_INFO, "Switching to I-frame playlist with bitrate: %d", pSession->pCurrentProgram->pAvailableIFrameBitrates[proposedBitrateIndex]);

        /* Get the playlist */
        rval = getPlaylistByBitrate(pSession->pCurrentProgram->pIFrameStreams, 
                                    pSession->pCurrentProgram->pAvailableIFrameBitrates[proposedBitrateIndex], 
                                    &pPlaylist);
        if(rval != HLS_OK) 
        {
            ERROR("failed to get I-frame playlist");
            break;
        }

        /* Switch to the playlist */
        rval = changeCurrentPlaylist(pSession, pPlaylist);
        if(rval != HLS_OK) 
        {
            ERROR("failed to change to I-frame playlist");
            break;
        }

    } while (0);

    return rval;
}

/**
 * Switches the session from playing from the I-frame media 
 * playlists to standard playlists.  On return, 
 * pSession->pCurrentPlaylist will point to a standard media 
 * playlist.  If there are multiple media playlists, will pick 
 * the one that is closest to the avgSegmentDldRate. 
 *  
 * Assumes calling thread has playlist WRITE lock
 * 
 * @param pSession - session to operate on
 * 
 * @return #hlsStatus_t - HLS_OK on success, error code otherwise
 */
hlsStatus_t switchToNormalPlaylists(hlsSession_t* pSession)
{
    hlsStatus_t rval = HLS_OK;
    hlsPlaylist_t* pPlaylist = NULL;

    int proposedBitrateIndex = 0;
   
    if(pSession == NULL)
    {
        ERROR("invalid parameter");
        return HLS_INVALID_PARAMETER;
    }

    do
    {
        /* Make sure we have normal playlists to switch to */
        if((pSession->pCurrentProgram == NULL) ||
           (pSession->pCurrentProgram->pAvailableBitrates == NULL) ||
           (pSession->pCurrentProgram->pStreams == NULL)) 
        {
            ERROR("No regular playlists available");
            rval = HLS_ERROR;
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

        /* Find regular playlist bitrate to switch to */
        proposedBitrateIndex = abrClientGetBitrateIndex(pSession->avgSegmentDldRate, pSession->minBitrate, pSession->maxBitrate, 
                                                        pSession->pCurrentProgram->pStreams->numElements, 
                                                        pSession->pCurrentProgram->pAvailableBitrates);

        if((proposedBitrateIndex < 0) || (proposedBitrateIndex >= pSession->pCurrentProgram->pStreams->numElements))
        {
            ERROR("Could not find valid bitrate");
            rval = HLS_ERROR;
            break;
        }

        DEBUG(DBG_INFO, "Switching to regular playlist with bitrate: %d", pSession->pCurrentProgram->pAvailableBitrates[proposedBitrateIndex]);

        /* Get the playlist */
        rval = getPlaylistByBitrate(pSession->pCurrentProgram->pStreams, 
                                    pSession->pCurrentProgram->pAvailableBitrates[proposedBitrateIndex], 
                                    &pPlaylist);
        if(rval != HLS_OK) 
        {
            ERROR("failed to get playlist");
            break;
        }

        /* Switch to the playlist */
        rval = changeCurrentPlaylist(pSession, pPlaylist);
        if(rval != HLS_OK) 
        {
            ERROR("failed to change to media playlist");
            break;
        }

    } while (0);

    return rval;
}

/** 
 * Converts PTS values into seconds resolution
 * 
 * @param PTS - PTS value to convert into seconds
 * 
 * @return double - seconds representation of PTS
 */
double ptsToSeconds(long long PTS)
{
    /* PTS has a resolution of 90kHz */
    return PTS/90000.0;
}

/** 
 * Allocates a new hlsPlaylist_t structure and sets entire 
 * strucuture to 0. 
 * 
 * @return hlsPlaylist_t* - pointer to new structure on success, 
 *         NULL on failure
 */
hlsPlaylist_t* newHlsPlaylist()
{
    hlsPlaylist_t* pPlaylist = NULL;

    pPlaylist = (hlsPlaylist_t*)malloc(sizeof(hlsPlaylist_t));

    if(pPlaylist == NULL) 
    {
        ERROR("malloc error");
    }
    else
    {
        memset(pPlaylist, 0, (sizeof(hlsPlaylist_t)));
    }

    return pPlaylist;
}

/** 
 * Allocates a new hlsPlaylist_t structure as well as the 
 * pMediaData member, setting the contents to 0. The type field 
 * of the returned structure will be PL_MEDIA.
 *  
 * @return hlsPlaylist_t* - pointer to new structure on success, 
 *         NULL on failure
 */
hlsPlaylist_t* newHlsMediaPlaylist()
{
    hlsPlaylist_t* pPlaylist = NULL;

    pPlaylist = newHlsPlaylist();

    if(pPlaylist != NULL) 
    {
        /* Allocate media playlist information structure */
        pPlaylist->pMediaData = malloc(sizeof(hlsMediaPlaylistData_t));
        if(pPlaylist->pMediaData == NULL) 
        {
            ERROR("malloc error");
            freePlaylist(pPlaylist);
        }
        else
        {
            memset(pPlaylist->pMediaData, 0, sizeof(hlsMediaPlaylistData_t));

            /* Set playlist type */
            pPlaylist->type = PL_MEDIA;
        }
    }

    return pPlaylist;
}

/**
 * Cleans up and frees hlsPlaylist structure.
 * 
 * @param pPlaylist - pointer to hlsPlaylist to free
 */
void freePlaylist(hlsPlaylist_t* pPlaylist)
{
    void* pData = NULL;

    if(pPlaylist != NULL) 
    {
        pPlaylist->pParentNode = NULL;

        /* Free the base structure */
        free(pPlaylist->baseURL);
        pPlaylist->baseURL = NULL;
        free(pPlaylist->playlistURL);
        pPlaylist->playlistURL = NULL;
        free(pPlaylist->redirectURL);
        pPlaylist->redirectURL = NULL;

        // Free linked list
        if(pPlaylist->pList != NULL) 
        {
            pData = NULL;

            while(pPlaylist->pList->numElements != 0) 
            {
                removeHead(pPlaylist->pList, (void**)(&pData));

                if(pData != NULL) 
                {
                    switch(pPlaylist->type) 
                    {
                        case PL_VARIANT:
                            freeProgram((hlsProgram_t*)pData);
                            break;
                        case PL_MEDIA:
                            freeSegment((hlsSegment_t*)pData);
                            break;
                        default:
                            break;
                    }
    
                    pData = NULL;
                }
            }

            freeLinkedList(pPlaylist->pList);
            pPlaylist->pList = NULL;
        }
        
        if(pPlaylist->pGroupList != NULL) 
        {
            while(pPlaylist->pGroupList->numElements != 0) 
            {
                removeHead(pPlaylist->pGroupList, (void**)(&pData));

                if(pData != NULL) 
                {
                    switch(pPlaylist->type) 
                    {
                        case PL_VARIANT:
                            DEBUG(DBG_INFO, "calling freeGroup\n");
                            freeGroup((hlsGroup_t*)pData);
                            break;
                        default:
                            break;
                    }
                    pData = NULL;
                }
            }
            
            freeLinkedList(pPlaylist->pGroupList);
            pPlaylist->pGroupList = NULL;
        }

        /* Free type-specific data */
        switch(pPlaylist->type) 
        {
            case PL_MEDIA:
                free(pPlaylist->pMediaData->codecs);
                pPlaylist->pMediaData->codecs = NULL;
                
                free(pPlaylist->pMediaData->audio);
                pPlaylist->pMediaData->audio = NULL;
                free(pPlaylist->pMediaData->video);
                pPlaylist->pMediaData->video = NULL;
                
                free(pPlaylist->pMediaData);
                pPlaylist->pMediaData = NULL;
                break;
            default:
                break;
        }

        free(pPlaylist);
    }
}

/** 
 * Allocates new hlsProgram_t structure and sets the contents to 
 * 0. 
 * 
 * @return hlsProgram_t* - pointer to new structure on success, 
 *         NULL on failure
 */
hlsProgram_t* newHlsProgram()
{
    hlsProgram_t* pProgram = NULL;

    pProgram = (hlsProgram_t*)malloc(sizeof(hlsProgram_t));

    if(pProgram == NULL) 
    {
        ERROR("malloc error");
    }
    else
    {
        memset(pProgram, 0, (sizeof(hlsProgram_t)));
    }

    return pProgram;
}

/**
 * Cleans up and frees an hlsProgram structure.
 * 
 * @param pProgram - pointer to hlsProgram to free
 */
void freeProgram(hlsProgram_t* pProgram)
{
    void* pData = NULL;

    if(pProgram != NULL) 
    {
        pProgram->pParentNode = NULL;

        free(pProgram->pAvailableBitrates);
        pProgram->pAvailableBitrates = NULL;

        free(pProgram->pAvailableIFrameBitrates);
        pProgram->pAvailableIFrameBitrates = NULL;

        if(pProgram->pStreams != NULL) 
        {
            pData = NULL;

            while(pProgram->pStreams->numElements != 0) 
            {
                removeHead(pProgram->pStreams, &pData);
                freePlaylist((hlsPlaylist_t*)pData);
                pData = NULL;
            }

            freeLinkedList(pProgram->pStreams);
            pProgram->pStreams = NULL;
        }

        if(pProgram->pIFrameStreams != NULL) 
        {
            pData = NULL;

            while(pProgram->pIFrameStreams->numElements != 0) 
            {
                removeHead(pProgram->pIFrameStreams, (void**)(&pData));
                freePlaylist((hlsPlaylist_t*)pData);
                pData = NULL;
            }

            freeLinkedList(pProgram->pIFrameStreams);
            pProgram->pIFrameStreams = NULL;
        }

        free(pProgram);
    }
}

/** 
 * Allocates new hlsSegment_t structure and sets the contents to
 * 0. 
 * 
 * @return hlsSegment_t* - pointer to new structure on success, 
 *         NULL on failure
 */
hlsSegment_t* newHlsSegment()
{
    hlsSegment_t* pSegment = NULL;

    pSegment = (hlsSegment_t*)malloc(sizeof(hlsSegment_t));

    if(pSegment == NULL) 
    {
        ERROR("malloc error");
    }
    else
    {
        memset(pSegment, 0, (sizeof(hlsSegment_t)));
    }

    return pSegment;
}

/** 
 * Cleans up and frees an hlsSegment structure.
 * 
 * @param pSegment - pointer to hlsSegment to free
 */
void freeSegment(hlsSegment_t* pSegment)
{
    if(pSegment != NULL) 
    {
        pSegment->pParentNode = NULL;

        free(pSegment->URL);
        pSegment->URL = NULL;
    
        free(pSegment->programName);
        pSegment->programName = NULL;
    
        //free(pSegment->iv);
        memset(pSegment->iv, 0, 16);
        //rms pSegment->iv = NULL;


        free(pSegment->keyURI);
        pSegment->keyURI = NULL;
    
        free(pSegment->pProgramTime);
        pSegment->pProgramTime = NULL;
    
        free(pSegment);
    }
}

/** 
 * Copies the contents of one #hlsSegment_t to another.  The 
 * destination structure must be non-NULL. 
 *  
 * Any variable length fields of the destination structure (e.g. 
 * URL) will be reallocated to contain the values from the 
 * source structure. 
 *  
 * The pParentNode field of the destination structue will be set 
 * to the value of same field in the source structure, however 
 * the actual parent node will still contain the source 
 * structure (i.e.: pDst->pParentNode->pData == pSrc). 
 * 
 * @param pSrc - source hlsSegment_t structure
 * @param pDst - destination hlsSegment_t structure
 * 
 * @return #hlsStatus_t
 */
hlsStatus_t copyHlsSegment(hlsSegment_t* pSrc, hlsSegment_t* pDst)
{
    hlsStatus_t status = HLS_OK;

    if((pSrc == NULL) || (pDst == NULL))
    {
        ERROR("invalid parameter");
        return HLS_INVALID_PARAMETER;
    }

    do
    {
        /* Copy the segment duration, sequence number, and byterange */
        pDst->duration = pSrc->duration;
        pDst->seqNum = pSrc->seqNum;
        pDst->byteLength = pSrc->byteLength;
        pDst->byteOffset = pSrc->byteOffset;

        /* Copy the discontinuity flag */
        pDst->bDiscontinuity = pSrc->bDiscontinuity;

        /* Copy the segment URL */
        if(pSrc->URL != NULL) 
        {
            pDst->URL = (char*)realloc(pDst->URL, strlen(pSrc->URL)+1);
            if(pDst->URL == NULL) 
            {
                ERROR("realloc error");
                status = HLS_MEMORY_ERROR;
                break;
            }
            memset(pDst->URL, 0, strlen(pSrc->URL)+1);
            strcpy(pDst->URL, pSrc->URL);
        }
        else
        {
            free(pDst->URL);
            pDst->URL = NULL;
        }

        /* Copy the program name */
        if(pSrc->programName != NULL) 
        {
            pDst->programName = (char*)realloc(pDst->programName, strlen(pSrc->programName)+1);
            if(pDst->programName == NULL) 
            {
                ERROR("realloc error");
                status = HLS_MEMORY_ERROR;
                break;
            }
            memset(pDst->programName, 0, strlen(pSrc->programName)+1);
            strcpy(pDst->programName, pSrc->programName);
        }
        else
        {
            free(pDst->programName);
            pDst->programName = NULL;
        }
        
        /* Copy encryption information */
        pDst->encType = pSrc->encType;
        memcpy(pDst->iv, pSrc->iv, 16);
        memcpy(pDst->key, pSrc->key, 16);

#if 0
        if(pSrc->iv != NULL) 
        {
            pDst->iv = (char*)realloc(pDst->iv, strlen(pSrc->iv)+1);
            if(pDst->iv == NULL) 
            {
                ERROR("realloc error");
                status = HLS_MEMORY_ERROR;
                break;
            }
            memset(pDst->iv, 0, strlen(pSrc->iv)+1);
            strcpy(pDst->iv, pSrc->iv);
        }
        else
        {
            free(pDst->iv);
            pDst->iv = NULL;
        }
#endif    
        if(pSrc->keyURI != NULL) 
        {
            pDst->keyURI = (char*)realloc(pDst->keyURI, strlen(pSrc->keyURI)+1);
            if(pDst->keyURI == NULL) 
            {
                ERROR("realloc error");
                status = HLS_MEMORY_ERROR;
                break;
            }
            memset(pDst->keyURI, 0, strlen(pSrc->keyURI)+1);
            strcpy(pDst->keyURI, pSrc->keyURI);
        }
        else
        {
            free(pDst->keyURI);
            pDst->keyURI = NULL;
        }

        /* Copy program date/time */
        if(pSrc->pProgramTime != NULL) 
        {
            if(pDst->pProgramTime == NULL) 
            {
                pDst->pProgramTime = (struct tm*)malloc(sizeof(struct tm));
                if(pDst->pProgramTime == NULL) 
                {
                    ERROR("malloc error");
                    status = HLS_MEMORY_ERROR;
                    break;
                }
            }

            memcpy(pDst->pProgramTime, pSrc->pProgramTime, sizeof(struct tm));
        }
        else
        {
            free(pDst->pProgramTime);
            pDst->pProgramTime = NULL;
        }

        /* Copy parent node pointer - note that this function
           doesn't change the parent note to point to the new segment.
           In other words, pDst->pParentNode->pData still points to
           pSrc. */
        pDst->pParentNode = pSrc->pParentNode;

    } while (0);

    return status;
}

/** 
 * Allocates new hlsGroup_t structure and sets the contents to 
 * 0. 
 * 
 * @return hlsGroup_t* - pointer to new structure on success, 
 *         NULL on failure
 */
hlsGroup_t* newHlsGroup()
{
    hlsGroup_t* pGroup = NULL;

    pGroup = (hlsGroup_t*)malloc(sizeof(hlsGroup_t));

    if(pGroup == NULL) 
    {
        ERROR("malloc error");
    }
    else
    {
        memset(pGroup, 0, (sizeof(hlsGroup_t)));
    }

    return pGroup;
}

/**
 * Cleans up and frees an hlsGroup structure.
 * 
 * @param pGroup - pointer to hlsGroup to free
 */
void freeGroup(hlsGroup_t* pGroup)
{
    void* pData = NULL;

    if(pGroup != NULL) 
    {
        pGroup->pParentNode = NULL;
               
        DEBUG(DBG_INFO, "Freeing group with ID: %s\n", pGroup->groupID);
        free(pGroup->groupID);
        pGroup->groupID = NULL;
        free(pGroup->language);
        pGroup->language = NULL;
        free(pGroup->name);
        pGroup->name = NULL;

        if(pGroup->pPlaylist != NULL) 
        {
           freePlaylist(pGroup->pPlaylist);
           pGroup->pPlaylist = NULL;
        }

        free(pGroup);
    }
}

/** 
 * Seek by position in the playlist 
 *  
 * @param pMediaPlaylist - Media playlist
 * @param position       - location to jump to 
 * @param pSeqNum        - Sequence of the segment after seek
 * 
 * @return #hlsStatus_t
 */
hlsStatus_t playlistSeek(hlsPlaylist_t *pMediaPlaylist, float position, int *pSeqNum)
{
   hlsStatus_t   rval = HLS_ERROR;
   hlsSegment_t* pSegment = NULL;
   double        positionFromEnd = 0;
   llNode_t*     pSegmentNode = NULL;

   do {

   if((pMediaPlaylist == NULL) ||
      (pMediaPlaylist->type != PL_MEDIA) ||
      (pMediaPlaylist->pMediaData == NULL))
   {
      ERROR("playlist invalid");
      break;
   }

   /* Need to account for the startOffset of the playlist when determining absolute
      position. */
   position += pMediaPlaylist->pMediaData->startOffset;

   /* Get the segment which contains the desired position */
   rval = getSegmentXSecFromStart(pMediaPlaylist, position, &pSegment);
   if(rval != HLS_OK) 
   {
      ERROR("failed to find segment in playlist");
      break;
   }

   *pSeqNum = pSegment->seqNum;

   /* Reset our positionFromEnd to be the starting position from end of the above segment */
   rval = getPositionFromEnd(pMediaPlaylist, pSegment, &(positionFromEnd));
   if(rval != HLS_OK) 
   {
      ERROR("problem getting initial playlist position");
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
      break;
   }

   /* pSegmentNode is the first node we want to download, so we need to set
      pLastDownloadedSegmentNode to pSegmentNode->pPrev */
   pSegmentNode = pSegmentNode->pPrev;

   /* Write new playlist values */
   pMediaPlaylist->pMediaData->positionFromEnd = positionFromEnd;
   pMediaPlaylist->pMediaData->pLastDownloadedSegmentNode = pSegmentNode;
  
   rval = HLS_OK;
   
   }while(0);

   return rval;
}

/**
 * Prints all information in hlsPlaylist structure.
 * 
 * @param pPlaylist - pointer to hlsPlaylist stucture to print
 */
void printPlaylist(hlsPlaylist_t* pPlaylist)
{
    llNode_t* pProgramNode = NULL;
    hlsProgram_t* pProgram = NULL;
    llNode_t* pGroupNode = NULL;
    hlsGroup_t* pGroup = NULL;
    llNode_t* pStreamNode = NULL;
    llNode_t* pSegmentNode = NULL;
    hlsSegment_t* pSegment = NULL;
    int i = 0;

    if(pPlaylist != NULL) 
    {
        if(pPlaylist->type == PL_VARIANT) 
        {
            printf("------------------------\n");
            printf("*** Variant Playlist ***\n");
            printf("------------------------\n");

            printf("version: %d\n", pPlaylist->version);

            printf("URL: %s\n", PRINTNULL(pPlaylist->playlistURL));
            printf("redirectURL: %s\n", PRINTNULL(pPlaylist->redirectURL));
            printf("baseURL: %s\n", PRINTNULL(pPlaylist->baseURL));
            
            printf("\n");

            /* Print program list */
            if(pPlaylist->pList) 
            {
                printf("# of programs: %d\n", pPlaylist->pList->numElements);
                
                /* Print program information */                
                pProgramNode = pPlaylist->pList->pHead;
                while(pProgramNode != NULL)
                {
                    pProgram = pProgramNode->pData;

                    printf("------------------------\n");
                    printf("*** Program ID: %4d ***\n", pProgram->programID);
                    printf("------------------------\n");
    
                    if(pProgram->pStreams != NULL) 
                    {
                        printf("# of streams: %d\n", pProgram->pStreams->numElements);
                        
                        if(pProgram->pAvailableBitrates != NULL) 
                        {
                            printf("Bitrates (bps): ");
    
                            i = 0;
                            printf("%d", pProgram->pAvailableBitrates[i]);
                            i++;

                            while(i < pProgram->pStreams->numElements) 
                            {
                                printf(", %d", pProgram->pAvailableBitrates[i]);
                                i++;
                            }

                            printf("\n\n");
                        }

                        /* Print stream information */
                        pStreamNode = pProgram->pStreams->pHead;
                        while(pStreamNode != NULL) 
                        {
                            printPlaylist((hlsPlaylist_t*)(pStreamNode->pData));
                            pStreamNode = pStreamNode->pNext;
                        }
                    }

                    if(pProgram->pIFrameStreams != NULL) 
                    {
                        printf("# of I-frame streams: %d\n", pProgram->pIFrameStreams->numElements);
                        
                        if(pProgram->pAvailableIFrameBitrates != NULL) 
                        {
                            printf("I-frame Bitrates (bps): ");
    
                            i = 0;
                            printf("%d", pProgram->pAvailableIFrameBitrates[i]);
                            i++;

                            while(i < pProgram->pIFrameStreams->numElements) 
                            {
                                printf(", %d", pProgram->pAvailableIFrameBitrates[i]);
                                i++;
                            }

                            printf("\n\n");
                        }

                        /* Print stream information */
                        pStreamNode = pProgram->pIFrameStreams->pHead;
                        while(pStreamNode != NULL) 
                        {
                            printPlaylist((hlsPlaylist_t*)(pStreamNode->pData));
                            pStreamNode = pStreamNode->pNext;
                        }
                    }

                    pProgramNode = pProgramNode->pNext;
                }
            }
            /* Print group information */
            if(NULL != pPlaylist->pGroupList)
            {
               pGroupNode = pPlaylist->pGroupList->pHead;
               while(pGroupNode != NULL)
               {
                  pGroup = pGroupNode->pData;

                  printf("------------------------\n");
                  printf("*** Group ID: %s ***\n", pGroup->groupID);
                  printf("------------------------\n");
                  printf("EXT-X-MEDIA type: %d\n", pGroup->type);
                  printf("EXT-X-MEDIA language: %s\n", PRINTNULL(pGroup->language));
                  printf("EXT-X-MEDIA name: %s\n", PRINTNULL(pGroup->name));
                  printf("EXT-X-MEDIA default: %d\n", pGroup->def);
                  printf("EXT-X-MEDIA autoselect: %d\n", pGroup->autoSelect);

                  /* Print stream information */
                  if(pGroup->pPlaylist != NULL) 
                  {
                     printPlaylist(pGroup->pPlaylist);
                  }

                  pGroupNode = pGroupNode->pNext;
               }
            }
        }
        else if(pPlaylist->type == PL_MEDIA) 
        {
            printf("------------------------\n");
            printf("*** Stream Playlist ****\n");
            printf("------------------------\n");

            printf("version: %d\n", pPlaylist->version);

            printf("%d updates without change -- next update @ %d\n", pPlaylist->unchangedReloads,
                                                                      (int)pPlaylist->nextReloadTime.tv_sec);
            
            printf("URL: %s\n", PRINTNULL(pPlaylist->playlistURL));
            printf("redirectURL: %s\n", PRINTNULL(pPlaylist->redirectURL));
            printf("baseURL: %s\n", PRINTNULL(pPlaylist->baseURL));

            printf("bitrate: %d\n", pPlaylist->pMediaData->bitrate);
            printf("resolution: %d x %d\n", pPlaylist->pMediaData->width, pPlaylist->pMediaData->height);
            printf("codecs: %s\n", PRINTNULL(pPlaylist->pMediaData->codecs));
            printf("audio group-id: %s\n", PRINTNULL(pPlaylist->pMediaData->audio));
            printf("video group-id: %s\n", PRINTNULL(pPlaylist->pMediaData->video));

            printf("target duration: %d seconds\n", pPlaylist->pMediaData->targetDuration);
            printf("playlist duration: %5.2f seconds\n", pPlaylist->pMediaData->duration);
            printf("play start offset: %5.2f seconds\n", pPlaylist->pMediaData->startOffset);
            printf("play end offset: %5.2f seconds\n", pPlaylist->pMediaData->endOffset);
            printf("last position in playlist: %5.2f seconds\n", pPlaylist->pMediaData->duration - pPlaylist->pMediaData->positionFromEnd);
            printf("starting sequence number: %d\n", pPlaylist->pMediaData->startingSequenceNumber);
            printf("have complete playlist? %s\n", (pPlaylist->pMediaData->bHaveCompletePlaylist ? "YES" : "NO"));
            printf("can cache downloaded data? %s\n", (pPlaylist->pMediaData->bCacheable ? "YES" : "NO"));
            printf("playlist mutability: %s\n", (pPlaylist->pMediaData->mutability == HLS_UNSPECIFIED ? "UNSPECIFIED" :
                                                 (pPlaylist->pMediaData->mutability == HLS_EVENT ? "EVENT" : "VOD")));
            printf("I-frames only? %s\n", (pPlaylist->pMediaData->bIframesOnly ? "YES" : "NO") );

            if(pPlaylist->pMediaData->pLastDownloadedSegmentNode != NULL &&
               pPlaylist->pMediaData->pLastDownloadedSegmentNode->pData != NULL) 
            {
                printf("last downloaded segment: %d\n", ((hlsSegment_t*)(pPlaylist->pMediaData->pLastDownloadedSegmentNode->pData))->seqNum);
            }

            /* Print segment list */
            if(pPlaylist->pList) 
            {
                printf("# of segments: %d\n", pPlaylist->pList->numElements);
                
                if(DBG_LEVEL == DBG_NOISE) 
                {
                    /* Print segment information */                
                    pSegmentNode = pPlaylist->pList->pHead;
                    while(pSegmentNode != NULL)
                    {
                        pSegment = pSegmentNode->pData;
        
                        printf("----------------------------------------------\n");

                        if(pSegment->bDiscontinuity) 
                        {
                            printf("*** DISCONTINUITY ***\n");
                        }
        
                        printf("Seq: %5d Duration: %5.2f Name: %s\n", pSegment->seqNum, 
                                                                      pSegment->duration,
                                                                      PRINTNULL(pSegment->programName));
        
                        printf("offset: %ld bytes, length: %ld bytes\n", pSegment->byteOffset, pSegment->byteLength);
        
                        if(pSegment->encType != SRC_ENC_NONE) 
                        {
                           //rms TODO fix for print.
#if 0
                            printf("Enc: %s IV: %s Key: %s\n", (pSegment->encType == SRC_ENC_AES128_CBC ? "AES-128-CBC" : 
                                                                  (pSegment->encType == SRC_ENC_AES128_CTR ? "AES-128-CTR" : "NONE       ")),
                                                                   pSegment->iv,
                                                                   PRINTNULL(pSegment->keyURI));
#endif
                        }
        
                        printf("URL: %s\n", PRINTNULL(pSegment->URL));
        
                        pSegmentNode = pSegmentNode->pNext;
                    }

                    printf("----------------------------------------------\n");
                }
            }

            printf("\n");
        }
    }
}

#ifdef __cplusplus
}
#endif
