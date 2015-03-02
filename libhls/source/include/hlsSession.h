#ifndef HLSSESSION_H
#define HLSSESSION_H

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
hlsStatus_t hlsSession_getDuration(hlsSession_t* pSession, float* pDuration);
hlsStatus_t hlsSession_getCurrentPosition(hlsSession_t* pSession, float* pPosition);
hlsStatus_t hlsSession_getSpeed(hlsSession_t* pSession, float* pSpeed);
hlsStatus_t hlsSession_getTrickSupported(hlsSession_t* pSession, int *bTrickSupported);
hlsStatus_t hlsSession_getContentType(hlsSession_t* pSession, hlsContentType_t *contentType);
hlsStatus_t hlsSession_getNumAudioLanguages(hlsSession_t* pSession, int *pNumAudioLanguages);
hlsStatus_t hlsSession_getAudioLanguagesInfo(hlsSession_t* pSession,
                                             srcPluginAudioLangInfo_t audioLangInfoArr[],
                                             int *pAudioLangInfoArrSize);
void hlsSession_playerEvtCallback(hlsSession_t* pSession, srcPlayerEvt_t* pEvt);

void hlsSession_printInfo(hlsSession_t* pSession);

#ifdef __cplusplus
}
#endif

#endif
