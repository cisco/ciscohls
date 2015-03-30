#ifndef HLSDOWNLOADERUTILS_H
#define HLSDOWNLOADERUTILS_H
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
