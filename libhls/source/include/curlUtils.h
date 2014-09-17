#ifndef CURLUTILS_H
#define CURLUTILS_H

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
hlsStatus_t getCurlDownloadDuration(CURL* pCurl, float* pDownloadDuration);
hlsStatus_t getCurlTransferInfo(CURL* pCurl, char** ppRedirectURL, float* pThroughput, long* pDownloadSize);

hlsStatus_t getBaseURL(char* URL, char** pBaseURL);
hlsStatus_t getFileName(char* URL, char** pFileName, char* prefix);
hlsStatus_t getLocalPath(char* URL, char** pLocalPath, char* prefix);
hlsStatus_t createFullURL(char** pUrl, char* baseURL);

#ifdef __cplusplus
}
#endif

#endif
