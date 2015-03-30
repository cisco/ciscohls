#ifndef CURLUTILS_H
#define CURLUTILS_H
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
 * @file curlUtils.h @date February 9, 2012
 *
 * @author Patryk Prus (pprus@cisco.com)
 *
 * Defines new types used by HLS plugin
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <curl/curl.h>
#include <curl/easy.h>

#include "hlsTypes.h"

/*! \struct downloadHandle_t
 * Structure for curlDownloadFile() function
 */
typedef struct {
    FILE* fpTarget;                 /*!< File descriptor where downloaded data is sent */
    pthread_mutex_t* pFileMutex;    /*!< Mutex to lock before performing any operations on fpTarget; can be NULL */
    int* pbAbortDownload;           /*!< Pointer to a flag which will terminate the download when TRUE; can be NULL */
} downloadHandle_t;

hlsStatus_t curlInit(CURL** ppCurl);
hlsStatus_t curlTerm(CURL* pCurl);

hlsStatus_t curlDownloadFile(CURL* pCurl, char* URL, downloadHandle_t* pHandle, long byteOffset, long byteLength);
hlsStatus_t getCurlTransferInfo(CURL* pCurl, char** ppRedirectURL, float* pThroughput, long* pDownloadSize);

hlsStatus_t getBaseURL(char* URL, char** pBaseURL);
hlsStatus_t getFileName(char* URL, char** pFileName, char* prefix);
hlsStatus_t getLocalPath(char* URL, char** pLocalPath, char* prefix);
hlsStatus_t createFullURL(char** pUrl, char* baseURL);

#ifdef __cplusplus
}
#endif

#endif
