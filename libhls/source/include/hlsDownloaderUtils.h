#ifndef HLSDOWNLOADERUTILS_H
#define HLSDOWNLOADERUTILS_H

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
 * @file hlsDownloaderUtils.h @date February 9, 2012
 * 
 * @author Patryk Prus (pprus@cisco.com) 
 *
 * Defines new types used by HLS plugin
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "hlsTypes.h"

hlsStatus_t getNextSegment(hlsPlaylist_t* pMediaPlaylist, hlsSegment_t** ppSegment);

hlsStatus_t getNextIFrame(hlsPlaylist_t* pMediaPlaylist, hlsSegment_t** ppSegment, float speed);

double iFrameTrickDuration(double duration, float speed);

hlsStatus_t matchPlaylistPosition(hlsSession_t *pSession,
                                  hlsPlaylist_t *pPlaylist1, 
                                  hlsPlaylist_t *pPlaylist2);

hlsStatus_t changeCurrentPlaylist(hlsSession_t* pSession, hlsPlaylist_t* pNewMediaPlaylist);

hlsStatus_t changeBitrate(hlsSession_t* pSession, int newBitrate);

hlsStatus_t downloadAndPushSegment(hlsSession_t* pSession, 
                                   hlsSegment_t* pSegment, 
                                   struct timespec waitTime, 
                                   srcPlayerMode_t playerMode, 
                                   int streamNum);

#ifdef __cplusplus
}
#endif

#endif
