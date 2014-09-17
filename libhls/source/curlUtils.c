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
 * @file curlUtils.c @date February 9, 2012
 *  
 * @author Patryk Prus (pprus@cisco.com) 
 *  
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <curl/curl.h>
#include <curl/easy.h>

#include "curlUtils.h"
#include "debug.h"

/**
 * Number of seconds to wait for a response from the server
 * before giving up.
 */
#define CONNECTION_TIMEOUT_SECS 5
/**
 * Minimum transfer speed necessary for us to consider a
 * connection "alive"
 */
#define CONNECTION_MIN_SPEED_BYTESPERSECOND 10000 // 10 kBps
/**
 * Number of seconds that we require the connection to stay below 
 * CONNECTION_MIN_SPEED_BYTESPERSECOND before we consider it "dead"
 */
#define CONNECTION_MIN_SPEED_SECS 3

/* Local function prototypes */
static size_t customFwrite(char* pBuffer, size_t size, size_t nmemb, void* pData);

/** 
 * Initialize and return a CURL handle.
 * 
 * @param ppCurl - pointer to CURL* which will be populated with 
 *               a CURL handle returned by curl_easy_init()
 * 
 * @return #hlsStatus_t
 */
hlsStatus_t curlInit(CURL** ppCurl)
{
    hlsStatus_t rval = HLS_OK;

    CURLcode curlResult;

    if((ppCurl == NULL) || (*ppCurl != NULL))
    {
        ERROR("invalid parameter");
        return HLS_INVALID_PARAMETER;
    }

    do
    {
        /* Get CURL handle */
        *ppCurl = curl_easy_init();
        if(*ppCurl == NULL) 
        {
            ERROR("Failed to init libcurl object");
            rval = HLS_ERROR;
            break;
        }

#if(DBG_LEVEL == DBG_NOISE)
        /* Run in verbose mode */
        curlResult = curl_easy_setopt( *ppCurl, CURLOPT_VERBOSE, 1 );
        if( CURLE_OK != curlResult )
        {
            rval = HLS_ERROR;
            ERROR("Failed to set curl_easy_setop() with CURLOPT_VERBOSE; Error %s", curl_easy_strerror(curlResult) );
            break;
        }
#endif
        /* Follow redirects */
        curlResult = curl_easy_setopt( *ppCurl, CURLOPT_FOLLOWLOCATION, 1 );
        if( CURLE_OK != curlResult )
        {
            rval = HLS_ERROR;
            ERROR("Failed to set curl_easy_setop() with CURLOPT_FOLLOWLOCATION; Error %s", curl_easy_strerror(curlResult) );
            break;
        }
    
        /* Set the user agent */
        curlResult = curl_easy_setopt( *ppCurl, CURLOPT_USERAGENT, "libcurl-agent/1.0" );
        if(  CURLE_OK != curlResult )
        {
            rval = HLS_ERROR;
            ERROR("Failed to set curl_easy_setop() with CURLOPT_USERAGENT; Error %s", curl_easy_strerror(curlResult) );
            break;
        }

        /* Fail on HTTP codes greater than 400, instead of returning the headers */
        curlResult = curl_easy_setopt( *ppCurl, CURLOPT_FAILONERROR, 1);
        if(  CURLE_OK != curlResult )
        {
            rval = HLS_ERROR;
            ERROR("Failed to set curl_easy_setop() with CURLOPT_FAILONERROR; Error %s", curl_easy_strerror(curlResult) );
            break;
        }

        /* Set server connection timeout */
        curlResult = curl_easy_setopt( *ppCurl, CURLOPT_CONNECTTIMEOUT, CONNECTION_TIMEOUT_SECS);
        if(  CURLE_OK != curlResult )
        {
            rval = HLS_ERROR;
            ERROR("Failed to set curl_easy_setop() with CURLOPT_CONNECTTIMEOUT; Error %s", curl_easy_strerror(curlResult) );
            break;
        }

        /* Set the minimum speed we need the transfer to proceed at before we declare the connection "dead" */
        curlResult = curl_easy_setopt( *ppCurl, CURLOPT_LOW_SPEED_LIMIT, CONNECTION_MIN_SPEED_BYTESPERSECOND);
        if(  CURLE_OK != curlResult )
        {
            rval = HLS_ERROR;
            ERROR("Failed to set curl_easy_setop() with CURLOPT_LOW_SPEED_LIMIT; Error %s", curl_easy_strerror(curlResult) );
            break;
        }

        /* Set the minimum time the connection needs to stay below the minimum speed before we declare the connection "dead */
        curlResult = curl_easy_setopt( *ppCurl, CURLOPT_LOW_SPEED_TIME, CONNECTION_MIN_SPEED_SECS);
        if(  CURLE_OK != curlResult )
        {
            rval = HLS_ERROR;
            ERROR("Failed to set curl_easy_setop() with CURLOPT_LOW_SPEED_TIME; Error %s", curl_easy_strerror(curlResult) );
            break;
        }

    } while(0);

    return rval;
}

/** 
 * Cleans up a CURL handle using curl_easy_cleanup()
 * 
 * @param pCurl - CURL handle
 * 
 * @return #hlsStatus_t
 */
hlsStatus_t curlTerm(CURL* pCurl)
{
    hlsStatus_t rval = HLS_OK;

    if(pCurl != NULL)
    {
        curl_easy_cleanup(pCurl);
    }

    return rval;
}

/**
 * Custom write function for use with cURL. 
 *  
 * pData is a downloadHandle_t* 
 *  
 * Uses pData->fpTarget must be non-NULL as it is the file 
 * descriptor passed to fwrite. 
 *  
 * If pData->pFileMutex is non-NULL, the function will lock the 
 * mutex before accessing pData->fpTarget. 
 *  
 * If pData->pbAbortDownload is non-NULL, the function will 
 * immediately return a -1 to stop the cURL transfer when 
 *  pbAbortDownload is TRUE.
 * 
 * @param pBuffer - data to write, should contain size*nmemb 
 *                bytes
 * @param size - size of each item in pBuffer
 * @param nmemb - number of items in pBuffer
 * @param pData - pointer to a #downloadHandle_t
 * 
 * @return size_t - actual number of bytes written, or -1 on 
 *         error
 */
static size_t customFwrite(char* pBuffer, size_t size, size_t nmemb, void* pData)
{
    size_t rval = 0;

    downloadHandle_t* pHandle = (downloadHandle_t*)pData;

    if((pBuffer == NULL) || (pHandle == NULL) || (pHandle->fpTarget == NULL)) 
    {
        ERROR("invalid parameter");
        return -1;
    }

    /* If the download handle contains an abort signal, check if we should
       abort the download */
    if(pHandle->pbAbortDownload != NULL)
    {
        if(*(pHandle->pbAbortDownload) != 0) 
        {
            DEBUG(DBG_WARN, "download cancelled");
            return -1;
        }
    }

    /* If the download handle contains a mutex, lock it before attempting
       the write. */
    if(pHandle->pFileMutex != NULL) 
    {
        pthread_mutex_lock(pHandle->pFileMutex);
    }

    DEBUG(DBG_NOISE, "writing %d bytes", (int)(size*nmemb));

    rval = fwrite(pBuffer, size, nmemb, pHandle->fpTarget);

    /* If the download handle contains a mutex, unlock it */
    if(pHandle->pFileMutex != NULL) 
    {
        pthread_mutex_unlock(pHandle->pFileMutex);
    }

    return rval;
}

/**
 * Downloads a file using cURL.
 *  
 * Downloads from URL and stores data in pHandle->fpTarget. 
 *  
 * If the download is cancelled via pHandle->pbAbortDownload, 
 * this function returns HLS_CANCELLED. 
 *  
 * @param pCurl - CURL handle to use
 * @param URL - URL of file to download
 * @param fpLocalFile - FILE* which has been opened for writing 
 *                    where the downloaded data will be written
 * @param byteOffset - integer specifying the byte offset at 
 *                   which to begin the download
 * @param byteLength - integer specifying the byte length of the
 *                   download; if 0 will download the remainder
 *                   of the file starting at byteOffset
 * @return #hlsStatus_t
 */
hlsStatus_t curlDownloadFile(CURL* pCurl, char* URL, downloadHandle_t* pHandle, long byteOffset, long byteLength)
{
    hlsStatus_t rval = HLS_OK;

    CURLcode curlResult;

    char *tempString = NULL;
    double tempDouble;
    long respondCode;

    if((pCurl == NULL) || (URL == NULL) || (pHandle == NULL) || (pHandle->fpTarget == NULL))
    {
        ERROR("invalid parameter");
        return HLS_INVALID_PARAMETER;
    }

    do
    {
        /* Set the URL */
        curlResult = curl_easy_setopt( pCurl, CURLOPT_URL, URL );
        if( CURLE_OK != curlResult )
        {
            rval = HLS_ERROR;
            ERROR("Failed to set curl_easy_setop() with CURLOPT_URL; Error %d: %s", curlResult, curl_easy_strerror(curlResult) );
            break;
        }

        /* Tell CURL to use custom write function */
        curlResult = curl_easy_setopt(pCurl, CURLOPT_WRITEFUNCTION, customFwrite);
        if( CURLE_OK != curlResult )
        {
            rval = HLS_ERROR;
            ERROR("Failed to set curl_easy_setop() with CURLOPT_WRITEFUNCTION; Error %d: %s", curlResult, curl_easy_strerror(curlResult) );
            break;
        }

        /* Tell CURL to pass our download handle to our custom write function */
        curlResult = curl_easy_setopt(pCurl, CURLOPT_WRITEDATA, pHandle);
        if( CURLE_OK != curlResult )
        {
            rval = HLS_ERROR;
            ERROR("Failed to set curl_easy_setop() with CURLOPT_WRITEDATA; Error %d: %s", curlResult, curl_easy_strerror(curlResult) );
            break;
        }

        /* Do we want the whole file or a byterange? */
        if(byteLength > 0) 
        {
            tempString = malloc(128);
            if(tempString == NULL) 
            {
                ERROR("malloc error");
                rval = HLS_MEMORY_ERROR;
                break;
            }
            memset(tempString, 0, 128);
            sprintf(tempString,"%ld-%ld", byteOffset, byteOffset+byteLength-1);

            DEBUG(DBG_INFO, "downloading range: %s", tempString);

            curlResult = curl_easy_setopt(pCurl, CURLOPT_RANGE, tempString);
            if( CURLE_OK != curlResult )
            {
                rval = HLS_ERROR;
                ERROR("Failed to set curl_easy_setop() with CURLOPT_RANGE; Error %d: %s", curlResult, curl_easy_strerror(curlResult) );
                break;
            }

            free(tempString);
            tempString = NULL;

            /* Clear this option, in case it was set by a previous transfer */
            curlResult = curl_easy_setopt(pCurl, CURLOPT_RESUME_FROM, 0);
            if( CURLE_OK != curlResult )
            {
                rval = HLS_ERROR;
                ERROR("Failed to set curl_easy_setop() with CURLOPT_RESUME_FROM; Error %d: %s", curlResult, curl_easy_strerror(curlResult) );
                break;
            }
        }
        else // byteLength == 0
        {
            /* Disable range downloads */
            curlResult = curl_easy_setopt(pCurl, CURLOPT_RANGE, NULL);
            if( CURLE_OK != curlResult )
            {
                rval = HLS_ERROR;
                ERROR("Failed to set curl_easy_setop() with CURLOPT_RANGE; Error %d: %s", curlResult, curl_easy_strerror(curlResult) );
                break;
            }

            /* Want the whole file starting at byteOffset */
            curlResult = curl_easy_setopt(pCurl, CURLOPT_RESUME_FROM, byteOffset);
            if( CURLE_OK != curlResult )
            {
                rval = HLS_ERROR;
                ERROR("Failed to set curl_easy_setop() with CURLOPT_RESUME_FROM; Error %d: %s", curlResult, curl_easy_strerror(curlResult) );
                break;
            }
        }

        TIMESTAMP(DBG_INFO, "downloading \"%s\"", URL);

        /* Execute libcurl */
        curlResult = curl_easy_perform(pCurl);
        if( CURLE_OK != curlResult )
        {
            if((pHandle->pbAbortDownload) && *(pHandle->pbAbortDownload))
            {
                DEBUG(DBG_WARN, "download cancelled");
                rval = HLS_CANCELLED;
                break;
            }
            else
            {
                // TODO: do we need to go through the various curl results and return HLS_ERROR and HLS_DL_ERROR appropriately?
                curl_easy_getinfo(pCurl, CURLINFO_RESPONSE_CODE, &respondCode);
                if (respondCode == 404)
                {
                    rval = HLS_ERROR;
                } 
                else
                {
                    rval = HLS_DL_ERROR;
                }
                ERROR("Failed to execute curl_easy_perform(); Error %d: '%s': HTTP respond code: %d", curlResult, curl_easy_strerror(curlResult), respondCode);
		
                break;
            }
        }

        /* Get the download size */
        curlResult = curl_easy_getinfo(pCurl, CURLINFO_SIZE_DOWNLOAD, &tempDouble);
        if( CURLE_OK != curlResult )
        {
            rval = HLS_ERROR;
            ERROR("Failed to set curl_easy_getinfo() with CURLINFO_SIZE_DOWNLOAD; Error %d: %s", curlResult, curl_easy_strerror(curlResult) );
            break;
        }

        TIMESTAMP(DBG_INFO, "download complete: %f bytes", tempDouble);

    } while (0);

    return rval;
}

hlsStatus_t getCurlDownloadDuration(CURL* pCurl, float* pDownloadDuration)
{
    hlsStatus_t rval = HLS_OK;
    double tempDouble;
    /* Return the download size, if requested */
    if(pDownloadDuration != NULL)
    {
        CURLcode curlResult;
        curlResult = curl_easy_getinfo(pCurl, CURLINFO_TOTAL_TIME, &tempDouble);
        if( CURLE_OK != curlResult )
        {
            rval = HLS_ERROR;
            ERROR("Failed to set curl_easy_getinfo() with CURLINFO_TOTAL_TIME; Error %d: %s", curlResult, curl_easy_strerror(curlResult) );
        }
        DEBUG(DBG_NOISE,"curl_easy_getinfo(CURLINFO_TOTAL_TIME) download duration: %5.2f seconds", tempDouble);

        *pDownloadDuration = tempDouble;
    }
    return(rval);
}

/** 
 * Retrieves information about the last transfer performed by 
 * the cURL handle pCurl.  If ppRedirectURL is non-NULL, 
 * retrieves the redirect URL.  If pThroughput is non-NULL, 
 * retrieves the throughput of the last transfer, in bits per 
 * second.  If pDownloadSize is non-NULL, retrieves the number 
 * of bytes downloaded. 
 *  
 * If ppRedirectURL is non-NULL, *ppRedirectURL MUST be NULL, as 
 * it will be allocated by this function and needs to be freed 
 * by the caller. 
 * 
 * @param pCurl - cURL handle to retireve information about
 * @param ppRedirectURL - NULL, or pointer to a NULL char* which 
 *                      will receive the final URL of the last
 *                      transfer
 * @param pThroughput - NULL, or pointer to float which will 
 *                    receive the throughput of the last
 *                    transfer, in bits per second
 * @param pDownloadSize - NULL, or pointer to long which will 
 *                      receive the number of bytes downloaded
 * 
 * @return #hlsStatus_t
 */
hlsStatus_t getCurlTransferInfo(CURL* pCurl, char** ppRedirectURL, float* pThroughput, long* pDownloadSize)
{
    hlsStatus_t rval = HLS_OK;

    CURLcode curlResult;

    char *tempString = NULL;
    double tempDouble;

    if((pCurl == NULL) || (ppRedirectURL != NULL && *ppRedirectURL != NULL))
    {
        ERROR("invalid parameter");
        return HLS_INVALID_PARAMETER;
    }

    do
    {
        /* Return the redirect URL, if requested */
        if(ppRedirectURL != NULL)
        {
            curlResult = curl_easy_getinfo(pCurl, CURLINFO_EFFECTIVE_URL, &tempString);
            if( CURLE_OK != curlResult )
            {
                rval = HLS_ERROR;
                ERROR("Failed to set curl_easy_getinfo() with CURLINFO_EFFECTIVE_URL; Error %d: %s", curlResult, curl_easy_strerror(curlResult) );
                break;
            }
            DEBUG(DBG_NOISE,"curl_easy_getinfo(CURLINFO_EFFECTIVE_URL) pString: %s", tempString );
    
            *ppRedirectURL = malloc(strlen(tempString)+1);
            if(*ppRedirectURL == NULL) 
            {
                ERROR("malloc error");
                rval = HLS_MEMORY_ERROR;
                break;
            }
            memset(*ppRedirectURL, 0, strlen(tempString)+1);
            strcpy(*ppRedirectURL, tempString);
        }        

        /* Return the average download speed, if requested */
        if(pThroughput != NULL)
        {
            curlResult = curl_easy_getinfo(pCurl, CURLINFO_SPEED_DOWNLOAD, &tempDouble);
            if( CURLE_OK != curlResult )
            {
                rval = HLS_ERROR;
                ERROR("Failed to set curl_easy_getinfo() with CURLINFO_SPEED_DOWNLOAD; Error %d: %s", curlResult, curl_easy_strerror(curlResult) );
                break;
            }
            DEBUG(DBG_NOISE,"curl_easy_getinfo(CURLINFO_SPEED_DOWNLOAD) throughput: %5.2f Bps", tempDouble);
            
            *pThroughput = tempDouble;

            /* Convert from Bps to bps */
            *pThroughput *= 8;

            DEBUG(DBG_INFO,"throughput: %5.2f Bps = %5.2f bps", tempDouble, *pThroughput);
        }                          
        
        /* Return the download size, if requested */
        if(pDownloadSize != NULL)
        {
            curlResult = curl_easy_getinfo(pCurl, CURLINFO_SIZE_DOWNLOAD, &tempDouble);
            if( CURLE_OK != curlResult )
            {
                rval = HLS_ERROR;
                ERROR("Failed to set curl_easy_getinfo() with CURLINFO_SIZE_DOWNLOAD; Error %d: %s", curlResult, curl_easy_strerror(curlResult) );
                break;
            }
            DEBUG(DBG_NOISE,"curl_easy_getinfo(CURLINFO_SIZE_DOWNLOAD) download size: %5.2f bytes", tempDouble);
            
            *pDownloadSize = tempDouble;
        }                           

    } while (0);

    return rval;
}

/**
 * Generates a base URL from a content URI
 * For example, it turns:
 *  
 * http://www.foo.com/bar/playlist.m3u
 *  
 * to
 *  
 * http://www.foo.com/bar/
 *  
 * If the URL has no base (e.g. "foo.bar"), returns HLS_OK and 
 *  pBaseURL == NULL.
 *  
 * *pBaseURL MUST be NULL, as it will be allocated by this 
 *  function and needs to be freed by the caller.
 * 
 * @param URL - input content URI
 * @param pBaseURL - will point to buffer holding base URL at 
 *                 return time. CALLER MUST FREE.
 * 
 * @return #hlsStatus_t
 */
hlsStatus_t getBaseURL(char* URL, char** pBaseURL)
{
    hlsStatus_t rval = HLS_OK;
    char *pMatch = NULL;
    int baseLength = 0;

    if((URL == NULL) || (pBaseURL == NULL) || (*pBaseURL != NULL))
    {
        ERROR("invalid parameter");
        return HLS_INVALID_PARAMETER;
    }

    do{
        /* Locate last occurance of '/' */
        pMatch = strrchr(URL, '/');
        if(pMatch == NULL) 
        {
            /* String did not exist */
            baseLength = 0;
        }
        else
        {      
            baseLength = (pMatch - URL) + 1;
        }
    
        DEBUG(DBG_NOISE,"Length of base to '/' is %d", baseLength);
    
        if(baseLength > 0) 
        {
            *pBaseURL = malloc(baseLength+1);
            if(*pBaseURL == NULL) 
            {
                ERROR("malloc error");
                rval = HLS_MEMORY_ERROR;
                break;
            }
            strncpy(*pBaseURL, URL, baseLength);
            (*pBaseURL)[baseLength] = '\0';    
        }

    } while (0);

    return rval;
}

/**
 * Generates a file name from a content URI
 *  
 * For example, it turns:
 *  
 * http://www.foo.com/bar/playlist.m3u?thing=thing#other
 *  
 * to
 *  
 * playlist.m3u
 *  
 * *pFileName MUST be NULL, as it will be allocated by this
 *  function and needs to be freed by the caller.
 *  
 * @param URL - input content URI
 * @param pFileName - will point to buffer holding file name at
 *                 return time. CALLER MUST FREE.
 * @param prefix - additional characters to prepend to the file 
 *               name.  Can be NULL.
 * 
 * @return #hlsStatus_t
 */
hlsStatus_t getFileName(char* URL, char** pFileName, char* prefix)
{
    hlsStatus_t rval = HLS_OK;
    char *pMatch = NULL;
    int nameLength = 0;
    int prefixLength = 0;
    
    if((URL == NULL) || (pFileName == NULL) || (*pFileName != NULL))
    {
        ERROR("invalid parameter");
        return HLS_INVALID_PARAMETER;
    }

    do{
        /* Locate last occurance of '/' */
        pMatch = strrchr(URL, '/');
        if(pMatch == NULL) 
        {
            /* String did not exist */
            pMatch = URL;
        }
        else
        {
            /* Point pMatch at the actual first character of the filename */
            pMatch++;
        }

        /* Find the length of the filename until any '?' or '#' characters */
        nameLength = strcspn(pMatch, "?#");

        DEBUG(DBG_NOISE,"Length of file name is %d", nameLength);
    
        /* Get the length of the prefix, if we were given one */
        if(prefix != NULL) 
        {
            prefixLength = strlen(prefix);
        }

        /* Allocate the new string */
        *pFileName = malloc(prefixLength+nameLength+1);
        if(*pFileName == NULL) 
        {
            ERROR("malloc error");
            rval = HLS_MEMORY_ERROR;
            break;
        }

        /* Copy the prefix, if it exists */
        if(prefixLength > 0) 
        {
            strncpy(*pFileName, prefix, prefixLength);
        }

        /* Copy the file name and terminate the string */
        strncpy(*pFileName+prefixLength, pMatch, nameLength);
        (*pFileName)[prefixLength+nameLength] = '\0';    
       
    } while (0);

    return rval;
}

/**
 * Generates a local file path from a content
 * URI and the LOCAL_PATH define.
 *  
 * *pLocalPath MUST be NULL, as it will be allocated by this
 *  function and needs to be freed by the caller.
 *  
 * @param URL - input content URI
 * @param pLocalPath - will point to buffer holding the local
 *                 path at return time. CALLER MUST FREE.
 * @param prefix - additional character to prepend to file name. 
 *                Can be NULL.
 * 
 * @return #hlsStatus_t
 */
hlsStatus_t getLocalPath(char* URL, char** pLocalPath, char* prefix)
{
    hlsStatus_t rval = HLS_OK;
    char* fileName = NULL;
    struct stat exist;

    if((URL == NULL) || (pLocalPath == NULL) || (*pLocalPath != NULL))
    {
        ERROR("invalid parameter");
        return HLS_INVALID_PARAMETER;
    }

    do{
        /* Get the filename */
        rval = getFileName(URL, &fileName, prefix);
        if(rval) 
        {
            ERROR("problem generating filename from URL");
            break;
        }

        /* Create the directory if it doesn't exist */
        if(stat(LOCAL_PATH, &exist) != 0)
        {
            rval = mkdir(LOCAL_PATH, S_IRWXU); /* TODO: Do we need execute perms here? */
        }

        *pLocalPath = malloc(strlen(LOCAL_PATH) + strlen(fileName) + 1);
        if(*pLocalPath == NULL) 
        {
            ERROR("malloc error");
            rval = HLS_MEMORY_ERROR;
            break;
        }
        memset(*pLocalPath, 0, strlen(LOCAL_PATH) + strlen(fileName) + 1);
        strcpy(*pLocalPath, LOCAL_PATH);
        strcpy(*pLocalPath+strlen(LOCAL_PATH), fileName);
            
    } while (0);

    free(fileName);
    fileName = NULL;

    return rval;
}

/**
 * Test if pUrl points to a complete URL.  If so, it leaves it 
 * unchanged.  If it is not a complete URL, it reallocates the 
 * memory *pUrl points to and prepends baseURL. 
 *  
 * In the event of an error, *pUrl and pUrl are unchanged  
 * 
 * @param pUrl - pointer to the input URL.  On successful return
 *             will point to either the original URL or to a new
 *             memory location containing the concatenated
 *             baseURL and original URL
 * @param baseURL - the base URL which may be prepended to pUrl 
 * 
 * @return #hlsStatus_t
 */
hlsStatus_t createFullURL(char** pUrl, char* baseURL)
{
    hlsStatus_t rval = HLS_OK;
    char* tempURL = NULL;

    if((pUrl == NULL) || (baseURL == NULL) || (*pUrl == NULL))
    {
        ERROR("invalid parameter");
        return HLS_INVALID_PARAMETER;
    }

    do
    {
        /* If *pUrl starts with "http://" or "https://" we assume it is a complete URL */
        if((strncmp(*pUrl, "http://", strlen("http://")) != 0) &&
           (strncmp(*pUrl, "https://", strlen("https://")) != 0))
        {
            tempURL = (char*)malloc(strlen(baseURL)+strlen(*pUrl)+1);
            if(tempURL == NULL) 
            {
                ERROR("malloc error");
                rval = HLS_MEMORY_ERROR;
                break;
            }
            memset(tempURL, 0, strlen(baseURL)+strlen(*pUrl)+1);
            strcpy(tempURL, baseURL);
            strcat(tempURL, *pUrl);

            /* Point *pUrl at the new string */
            free(*pUrl);
            *pUrl = tempURL;
            tempURL = NULL;

            DEBUG(DBG_INFO,"Constructed URL: %s", *pUrl);
        }

    } while (0);

    return rval;
}

#ifdef __cplusplus
}
#endif
