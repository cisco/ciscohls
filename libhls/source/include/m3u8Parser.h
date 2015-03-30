#ifndef M3U8PARSER_H
#define M3U8PARSER_H
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
