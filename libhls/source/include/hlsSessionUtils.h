#ifndef HLSSESSIONUTILS_H
#define HLSSESSIONUTILS_H
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
