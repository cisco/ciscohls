#ifndef HLSSESSION_H
#define HLSSESSION_H
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
 * @file hlsSession.h @date February 9, 2012
 *
 * @author Patryk Prus (pprus@cisco.com)
 *
 * Defines new types used by HLS plugin
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "hlsTypes.h"

hlsStatus_t hlsSession_init(hlsSession_t** ppSession, void* pHandle);
void hlsSession_term(hlsSession_t* pSession);

hlsStatus_t hlsSession_setDataSource(hlsSession_t* pSession, char* playlistURL);
hlsStatus_t hlsSession_prepare(hlsSession_t* pSession);

hlsStatus_t hlsSession_play(hlsSession_t* pSession);

hlsStatus_t hlsSession_getNumBitrates(hlsSession_t* pSession, int* numBitrates);
hlsStatus_t hlsSession_getBitrates(hlsSession_t* pSession, int numBitrates, int* bitrates);
hlsStatus_t hlsSession_getCurrentBitrate(hlsSession_t* pSession, int* pBitrate);
hlsStatus_t hlsSession_setBitrateLimit(hlsSession_t* pSession, hlsBitrateLimit_t limitType, int limit);
hlsStatus_t hlsSession_setSpeed(hlsSession_t* pSession, float speed);
hlsStatus_t hlsSession_stop(hlsSession_t* pSession, int bFlush);
hlsStatus_t hlsSession_seek(hlsSession_t* pSession, float position);
hlsStatus_t hlsSession_setAudioLanguage(hlsSession_t* pSession, char audioLangISOCode[]);
hlsStatus_t hlsSession_getDuration(hlsSession_t* pSession, float* pDuration);
hlsStatus_t hlsSession_getCurrentPosition(hlsSession_t* pSession, float* pPosition);
hlsStatus_t hlsSession_getSpeed(hlsSession_t* pSession, float* pSpeed);
hlsStatus_t hlsSession_getTrickSupported(hlsSession_t* pSession, int *bTrickSupported);
hlsStatus_t hlsSession_getContentType(hlsSession_t* pSession, hlsContentType_t *contentType);
hlsStatus_t hlsSession_getNumAudioLanguages(hlsSession_t* pSession, int *pNumAudioLanguages);
hlsStatus_t hlsSession_getAudioLanguagesInfo(hlsSession_t* pSession,
                                             srcPluginAudioLangInfo_t audioLangInfoArr[],
                                             int *pAudioLangInfoArrSize);
hlsStatus_t hlsSession_getAudioLanguage(hlsSession_t* pSession, char audioLanguage[]);
void hlsSession_playerEvtCallback(hlsSession_t* pSession, srcPlayerEvt_t* pEvt);

void hlsSession_printInfo(hlsSession_t* pSession);

#ifdef __cplusplus
}
#endif

#endif
