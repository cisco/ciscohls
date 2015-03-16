#ifndef HLSSESSIONUTILS_H
#define HLSSESSIONUTILS_H

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
 * @file hlsSessionUtils.h @date February 9, 2012
 * 
 * @author Patryk Prus (pprus@cisco.com) 
 *
 * Defines new types used by HLS plugin
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "hlsTypes.h"

hlsStatus_t getBestBitrate(hlsSession_t* pSession, int* pBitrate);

hlsStatus_t getPlaylistByBitrate(llist_t* pPlaylistList, int bitrate, hlsPlaylist_t** ppMediaPlaylist);

hlsStatus_t getExternalDuration(hlsPlaylist_t* pMediaPlaylist, double* pDuration);
hlsStatus_t getExternalPosition(hlsPlaylist_t* pMediaPlaylist, double* pPosition);

hlsStatus_t getSegmentXSecFromEnd(hlsPlaylist_t* pMediaPlaylist, double x, hlsSegment_t** ppSegment, hlsPlaylist_t* pOldMediaPlaylist);
hlsStatus_t getSegmentXSecFromStart(hlsPlaylist_t* pMediaPlaylist, double x, hlsSegment_t** ppSegment);

hlsStatus_t getPositionFromEnd(hlsPlaylist_t* pMediaPlaylist, hlsSegment_t* pSegment, double* pSeconds);
hlsStatus_t getPositionFromStart(hlsPlaylist_t* pMediaPlaylist, hlsSegment_t* pSegment, double* pSeconds);

hlsStatus_t flushPlaylist(hlsPlaylist_t* pMediaPlaylist);

hlsStatus_t switchToIFramePlaylists(hlsSession_t* pSession);
hlsStatus_t switchToNormalPlaylists(hlsSession_t* pSession);

double ptsToSeconds(long long PTS);

hlsPlaylist_t* newHlsPlaylist();
hlsPlaylist_t* newHlsMediaPlaylist();
void freePlaylist(hlsPlaylist_t* pPlaylist);

hlsProgram_t* newHlsProgram();
void freeProgram(hlsProgram_t* pProgram);

hlsSegment_t* newHlsSegment();
void freeSegment(hlsSegment_t* pSegment);
hlsStatus_t copyHlsSegment(hlsSegment_t* pSrc, hlsSegment_t* pDst);

hlsGroup_t* newHlsGroup();
void freeGroup(hlsGroup_t* pGroup);

hlsStatus_t playlistSeek(hlsPlaylist_t *pMediaPlaylist, float position, int *pSeqNum);

hlsStatus_t updateCurrentGroup(hlsSession_t *pSession, char groupID[], hlsGroup_t *pNewGroup);

void printPlaylist(hlsPlaylist_t* pPlaylist);

#ifdef __cplusplus
}
#endif

#endif
