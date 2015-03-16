#ifndef M3U8PARSER_H
#define M3U8PARSER_H

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
 * @file m3u8Parser.h @date February 9, 2012
 * 
 * @author Patryk Prus (pprus@cisco.com) 
 *
 * Defines new types used by HLS plugin
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "hlsTypes.h"
/*! \enum mediaAttrib_t 
 * Media group attributes 
 * 
 */
typedef enum
{
   ATTRIB_LANGUAGE,
   ATTRIB_DEFAULT,
   ATTRIB_AUTOSELECT
}mediaAttrib_t;

void m3u8ParserThread(hlsSession_t* pSession);

hlsStatus_t findAudioGroup(hlsSession_t *pSession, hlsGroup_t **ppGroupOut);

hlsStatus_t findAudioGroupByAttrib(hlsSession_t *pSession,
                                   mediaAttrib_t attrib,
                                   void *pData,
                                   hlsGroup_t **ppGroupOut);

#ifdef __cplusplus
}
#endif

#endif
