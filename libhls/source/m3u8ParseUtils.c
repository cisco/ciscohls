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
 * @file m3u8ParseUtils.c @date February 9, 2012
 *  
 * @author Patryk Prus (pprus@cisco.com) 
 *  
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>

#include "config.h"
#ifndef __USE_XOPEN
#define __USE_XOPEN
#endif
#include <time.h>

#include "hlsPlayerInterface.h"
#include "hlsSessionUtils.h"

#include "m3u8ParseUtils.h"
#include "curlUtils.h"

#include "debug.h"

/* Needs to match m3u8Tag enum in hlsSession.h */
const char *m3u8TagStrings[NUM_SUPPORTED_TAGS] =
{
    "#EXTM3U",
    "#EXTINF",
    "#EXT-X-STREAM-INF",
    "#EXT-X-VERSION",
    "#EXT-X-TARGETDURATION",
    "#EXT-X-MEDIA-SEQUENCE",
    "#EXT-X-ALLOW-CACHE",
    "#EXT-X-PLAYLIST-TYPE",
    "#EXT-X-KEY",
    "#EXT-X-PROGRAM-DATE-TIME",
    "#EXT-X-DISCONTINUITY",
    "#EXT-X-ENDLIST",
    "#EXT-X-BYTERANGE",
    "#EXT-X-CISCO-KEY",
    "#EXT-X-CISCO-PROT-HEADER",
    "#EXT-X-I-FRAME-STREAM-INF",
    "#EXT-X-I-FRAMES-ONLY",
    "#EXT-X-MEDIA"
};

/* Local function prototypes */
hlsStatus_t m3u8ParsePlaylist(hlsPlaylist_t* pPlaylist, hlsSession_t* pSession);

static hlsStatus_t m3u8DownloadPlaylist(char* URL, char* filePath, char** pRedirectURL, hlsSession_t* pSession);
static hlsStatus_t m3u8PreprocessPlaylist(FILE* fpPlaylist, hlsPlaylist_t* pPlaylist, hlsSession_t* pSession);

static hlsStatus_t m3u8ProcessVariantPlaylist(FILE* fpPlaylist, hlsPlaylist_t* pPlaylist);
static hlsStatus_t m3u8ProcessMediaPlaylist(FILE* fpPlaylist, hlsPlaylist_t* pPlaylist);

static hlsStatus_t m3u8UpdatePlaylist(hlsPlaylist_t* pPlaylist, hlsSession_t* pSession);
static hlsStatus_t m3u8UpdateMediaPlaylist(FILE* fpPlaylist, hlsPlaylist_t* pPlaylist);

static hlsStatus_t m3u8GetLine(FILE* fpPlaylist, char* dest, int length);
static void m3u8NormalizeString(char *pString);
static m3u8Tag_t m3u8GetTag(char* pString);
static hlsStatus_t m3u8findURL(char* buffer, int bufferLength, FILE* fpPlaylist);

static hlsStatus_t m3u8ParseStreamInf(char *tagLine, char* urlLine, llist_t* pProgramList);
static hlsStatus_t m3u8ParseInf(char *tagLine, char* urlLine, llist_t* pSegmentList);
static hlsStatus_t m3u8ParseDateTime(char* tagLine, hlsSegment_t* pSegment);
static hlsStatus_t m3u8ParseKey(char* tagLine, srcEncType_t* pEncType, char** pIV, char** pKeyURI);
static hlsStatus_t m3u8ParseByteRange(char* tagLine, hlsSegment_t* pSegment, long* pNextSegmentOffset);
static hlsStatus_t m3u8ParseProtHeader(char* tagLine, hlsSession_t* pSession);
static hlsStatus_t m3u8ParseIFrameStreamInf(char *tagLine, char* baseURL, llist_t* pProgramList);
static hlsStatus_t m3u8ParseMedia(char *tagLine, char* baseURL, llist_t* pGroupList);

static hlsStatus_t addSegmentEncInfo(hlsSegment_t* pSegment, srcEncType_t encType, char* iv, char* keyURI);
static hlsStatus_t incCtrIv(char ** pIV);
static hlsStatus_t decCtrIv(char ** pIV);

typedef struct 
{
   char key[16];
}tmemkey;

static void strToHex( const char *pString,  char *pHex, int arraySize )
{
   int ii = 0, jj = 0;
   int numPairs = strnlen( pString, (arraySize*2) )/2;
   unsigned int output;

   if( (NULL == pString) || (NULL == pHex) )
   {
      return;
   }
   else if( numPairs != arraySize )
   {
      return;
   }
   else
   {
      /* Loop through and convert */
      for( ii=0, jj=0; ii < numPairs; ii++, jj+=2 )
      {
         sscanf( &pString[jj], "%2x", &output );
         pHex[ii] = output;
      }  
   }
   return;
}
#ifdef ENABLE_KEY_RETRIEVAL
// This function does one simple thing, it pulls down the key that is 16 bytes long
// that is it.
static size_t WriteMemoryCallback( void *contents, size_t size, size_t nmemb, void *userp)
{
   //size_t realsize = size *nmemb;
   tmemkey  *pkey = (tmemkey*)userp;


   memcpy(pkey->key, contents, sizeof(tmemkey));
   return sizeof(tmemkey);

}
#if 0
static void hexdump(void *ptr, int buflen) 
{
   ptr = ptr;
   buflen = buflen;
   unsigned char *buf = (unsigned char*)ptr;
   int i, j;
   for (i=0; i<buflen; i+=16) {
      printf("%06x: ", i);
      for (j=0; j<16; j++) 
         if (i+j < buflen)
            printf("%02x ", buf[i+j]);
         else
            printf("   ");
      printf(" ");
      for (j=0; j<16; j++) 
         if (i+j < buflen)
            printf("%c", isprint(buf[i+j]) ? buf[i+j] : '.');
      printf("\n");
   }
}
#endif



static int dwnld_parse_keyURI(char *pOutKey, const char *pInURI)
{
   int      status =0;
   CURL     *curl_h;
   tmemkey  chunk;
   // do some error checking
   //
   do
   {
      if (pInURI == NULL){status = 1; break;}
      if (pOutKey == NULL){status = 1; break;}
      memset(pOutKey, 0, 16);
      memset(chunk.key, 0, 16);

      curl_h = curl_easy_init();
      curl_easy_setopt(curl_h, CURLOPT_URL, pInURI);
      /* send all data to this function*/
      curl_easy_setopt(curl_h, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);

      curl_easy_setopt(curl_h, CURLOPT_WRITEDATA, (void*)&chunk);
      curl_easy_setopt(curl_h, CURLOPT_USERAGENT, "libcurl-agent/1.0");

      // now get the key file.
      curl_easy_perform(curl_h);

      // we now have the key file
      // TODO: HANDLE ERROR CASE

      curl_easy_cleanup(curl_h);

      // chunk now holds the key
      memcpy(pOutKey, chunk.key, 16);
   }while(0);

   return status;
}

#endif // ENABLE_KEY_RETRIEVAL
/**
 * Downloads and parses playlist file pointed to by URL in
 * pPlaylist->playlistURL.
 * Fills in:    redirectURL
 *              baseURL
 *              type
 *              version
 *              nextReloadTime
 * TODO: update...
 * But leaves all other fields as they are.
 * Assumes calling thread has playlist WRITE lock
 * 
 * @param pPlaylist - pointer to hlsPlaylist structure to parse
 * @param pSession
 * 
 * @return #hlsStatus_t
 */
hlsStatus_t m3u8ParsePlaylist(hlsPlaylist_t* pPlaylist, hlsSession_t* pSession)
{
    hlsStatus_t rval = HLS_OK;

    char* filePath = NULL;

    FILE* fpPlaylist = NULL;

    int preParseErrors = 0;

    if((pPlaylist == NULL) || (pSession == NULL))
    {
        ERROR("invalid parameter");
        return HLS_INVALID_PARAMETER;
    }

    /* If this playlist has been parsed before, call m3u8UpdatePlaylist() instead */
    if((pPlaylist->nextReloadTime.tv_sec != 0) && (pPlaylist->nextReloadTime.tv_nsec != 0))
    {
        return m3u8UpdatePlaylist(pPlaylist, pSession);
    }


    do
    {
        /* Generate the local download path */
        rval = getLocalPath(pPlaylist->playlistURL, &filePath, pSession->sessionName);
        if(rval != HLS_OK) 
        {
            ERROR("error generating local path");
            break;
        }
        DEBUG(DBG_INFO, "Local playlist path: %s", filePath);

        do
        {
            /* Download the playlist */
            rval = m3u8DownloadPlaylist(pPlaylist->playlistURL, filePath, &(pPlaylist->redirectURL), pSession);
            if(rval != HLS_OK) 
            {
                ERROR("error downloading playlist");
                break;
            }

            /* Store the current download time (we will add the wait offset later) */
            if(clock_gettime(CLOCK_MONOTONIC, &(pPlaylist->nextReloadTime)) != 0) 
            {
                ERROR("failed to get current time");
                rval = HLS_ERROR;
                break;
            }
    
            DEBUG(DBG_INFO,"downloaded playlist @ %d", (int)pPlaylist->nextReloadTime.tv_sec);
    
            DEBUG(DBG_NOISE,"redirect URL: %s", pPlaylist->redirectURL);
    
            /* Generate base URL based on redirect URL */
            rval = getBaseURL(pPlaylist->redirectURL, &(pPlaylist->baseURL));
            if(rval)
            {
                ERROR("failed to generate base URL");
                break;
            }
    
            DEBUG(DBG_NOISE,"base URL: %s", pPlaylist->baseURL);
    
            /* Open downloaded file */
            fpPlaylist = fopen(filePath, "rb");
            if(fpPlaylist == NULL) 
            {
                ERROR("fopen() failed on file %s -- %s", filePath, strerror(errno));
                rval = HLS_FILE_ERROR;
                break;
            }
    
            /* TODO: Comment... */ 
            rval = m3u8PreprocessPlaylist(fpPlaylist, pPlaylist, pSession);
            if(rval != HLS_OK) 
            {
                /* If we errored on pre-processing, it is possible
                   that we will recover if we try again in a bit */
                if(preParseErrors < MAX_PL_PARSE_REDL_RETRIES) 
                {
                    DEBUG(DBG_WARN,"error pre-processing playlist -- will retry %d times", MAX_PL_PARSE_REDL_RETRIES - preParseErrors);
                    preParseErrors++;

                    /* Things are OK for now... */
                    rval = HLS_OK;
    
                    /* Clean up and start over */
                    /* Close file */
                    if(fpPlaylist) 
                    {
                        fclose(fpPlaylist);
                        fpPlaylist = NULL;
                    }
                    
                    /* Free allocated memory */                    
                    free(pPlaylist->redirectURL);
                    pPlaylist->redirectURL = NULL;
                    free(pPlaylist->baseURL);
                    pPlaylist->baseURL = NULL;
                }
                else
                {
                    ERROR("error pre-processing playlist after %d retries", MAX_PL_PARSE_REDL_RETRIES);
                    break;
                }
            }
            else
            {
                /* Reset the error count */
                preParseErrors = 0;
            }

        } while(preParseErrors != 0);
        if(rval != HLS_OK) 
        {
            break;
        }

        /* Now that we know the playlist type, start parsing */
        switch(pPlaylist->type) 
        {
            case PL_VARIANT:
                DEBUG(DBG_INFO,"got version %d variant playlist", pPlaylist->version);
                rval = m3u8ProcessVariantPlaylist(fpPlaylist, pPlaylist);
                break;
            case PL_MEDIA:
                DEBUG(DBG_INFO,"got version %d media playlist", pPlaylist->version);
                rval = m3u8ProcessMediaPlaylist(fpPlaylist, pPlaylist);
                if(rval == HLS_OK)
                {
                    /* Set the time until the next reload of the playlist */
                    if((pPlaylist->pList != NULL) &&
                       (pPlaylist->pList->pTail != NULL) &&
                       (pPlaylist->pList->pTail->pData != NULL))
                    {
                        /* Initial reload time is the length of the last segment in the playlist */
                        pPlaylist->nextReloadTime.tv_sec += (int)(((hlsSegment_t*)(pPlaylist->pList->pTail->pData))->duration);
                        DEBUG(DBG_INFO,"next update @ %d", (int)pPlaylist->nextReloadTime.tv_sec);
                    }
                    // TODO: ERROR?
                }
                break;
            case PL_WRONGVER:
                ERROR("unsupported playlist version: %d", pPlaylist->version);
                rval = HLS_ERROR;
                break;
            case PL_INVALID:
            default:
                rval = HLS_ERROR;
                ERROR("invalid playlist type: %d", pPlaylist->type);
                break;
        }
        if(rval) 
        {
            break;
        }

    }while(0);

    /* Close file */
    if(fpPlaylist) 
    {
        fclose(fpPlaylist);
        fpPlaylist = NULL;
    }

    /* Delete downloaded file */
    // TODO: check for file deletion errors?
    if(filePath != NULL) 
    {
        unlink(filePath);
        free(filePath);
        filePath = NULL;
    }

    return rval;
}

/**
 * Downloads an m3u8 playlist file. 
 *  
 * If the initial download fails because of a network error, 
 * this function will retry MAX_PL_DL_RETRIES times before 
 * returning an error. 
 * 
 * @param URL - URL of the playlist to download
 * @param filePath - location to store the downloaded playlist
 * @param pRedirectURL - 
 * @param pSession - HLS session handle
 *  
 * @return #hlsStatus_t 
 */
static hlsStatus_t m3u8DownloadPlaylist(char* URL, char* filePath, char** pRedirectURL, hlsSession_t* pSession)
{
    hlsStatus_t rval = HLS_OK;

    FILE* fpPlaylist = NULL;

    int pthread_status = 0;

    struct timespec wakeTime;

    int* pbStopDownload = NULL;
    pthread_mutex_t*  pDownloadWakeMutex = NULL;
    pthread_cond_t*  pDownloadWakeCond = NULL;

    downloadHandle_t dlHandle;

    int attempts = 0;

    long downloadSize = 0l;

    srcPluginErr_t error;

    if((URL == NULL) || (filePath == NULL) || (pSession == NULL) || ((pRedirectURL != NULL) && (*pRedirectURL != NULL)))
    {
        ERROR("invalid parameter");
        return HLS_INVALID_PARAMETER;
    }

    do
    {
        /* Determine which thread we're being called from we can use the correct exit condition and sleep mutex */
        if(pthread_equal(pthread_self(), pSession->downloader)) 
        {
            DEBUG(DBG_NOISE, "called from downloader thread");

            pbStopDownload = &(pSession->bKillDownloader);
            pDownloadWakeCond = &(pSession->downloaderWakeCond);
            pDownloadWakeMutex = &(pSession->downloaderWakeMutex);
        }
        else if(pthread_equal(pthread_self(), pSession->parser)) 
        {
            DEBUG(DBG_NOISE, "called from parser thread");

            pbStopDownload = &(pSession->bKillParser);
            pDownloadWakeCond = &(pSession->parserWakeCond);
            pDownloadWakeMutex = &(pSession->parserWakeMutex);
        }

        while(rval == HLS_OK)
        {
            /* Get loop start time */
            if(clock_gettime(CLOCK_MONOTONIC, &wakeTime) != 0) 
            {
                ERROR("failed to get current time");
                rval = HLS_ERROR;
                break;
            }

            /* Open local file for download */
            fpPlaylist = fopen(filePath, "wb");
            if(fpPlaylist == NULL) 
            {
                ERROR("fopen() failed on file %s -- %s", filePath, strerror(errno));
                rval = HLS_FILE_ERROR;
                break;
            }
    
            /* Populate download handle struct */
            dlHandle.fpTarget = fpPlaylist;
            dlHandle.pFileMutex = NULL;
            dlHandle.pbAbortDownload = pbStopDownload;

            /* Lock cURL mutex */
            pthread_mutex_lock(&(pSession->curlMutex));

            /* Download playlist file */
            rval = curlDownloadFile(pSession->pCurl, URL, &dlHandle, 0, 0);
            if(rval == HLS_OK)
            {
                /* Get transfer information */
                if(pRedirectURL != NULL)
                {
                    rval = getCurlTransferInfo(pSession->pCurl, pRedirectURL, NULL, &downloadSize);
                    if(rval != HLS_OK)
                    {
                        ERROR("failed to get segment download rate");
                        /* Unlock cURL mutex */
                        pthread_mutex_unlock(&(pSession->curlMutex));
                        break;
                    }
                }

                /* Make sure we actually downloaded some data */
                if(downloadSize > 0) 
                {
                    /* Unlock cURL mutex */
                    pthread_mutex_unlock(&(pSession->curlMutex));
    
                    /* Break out of the loop */
                    break;
                }
                else
                {
                    DEBUG(DBG_WARN,"downloaded playlist of length 0 -- will retry");

                    /* Free the redirect URL in case it was allocated by the call to getCurlTransferInfo */
                    free(*pRedirectURL);
                    *pRedirectURL = NULL;
                }
            }
            else if(rval != HLS_DL_ERROR) 
            {
                /* If we didn't fail because of a network error, quit here and report it (this includes
                   HLS_CANCELLED) */

                /* Unlock cURL mutex */
                pthread_mutex_unlock(&(pSession->curlMutex));

                if(rval == HLS_CANCELLED) 
                {
                    DEBUG(DBG_WARN, "playlist download cancelled");
                }
                else
                {
                    ERROR("playlist download failed, but not because of network issues");
                }

                break;
            }

            /* Unlock cURL mutex */
            pthread_mutex_unlock(&(pSession->curlMutex));
        
            if(++attempts > MAX_PL_DL_RETRIES) 
            {   
                ERROR("playlist download failed after %d retries", MAX_PL_DL_RETRIES);
                break;
            }

            /* Things are OK for now... */
            rval = HLS_OK;

            /* Close file so we can re-open it on the next iteration */
            if(fpPlaylist) 
            {
                fclose(fpPlaylist);
                fpPlaylist = NULL;
            }

            /* Send asynchronous network error message to player */
            error.errCode = SRC_PLUGIN_ERR_NETWORK;
            snprintf(error.errMsg, SRC_ERR_MSG_LEN, DEBUG_MSG("session %p network error during playlist download -- will retry", pSession));
            hlsPlayer_pluginErrCallback(pSession->pHandle, &error);

            DEBUG(DBG_WARN,"session %p network error during playlist download -- will retry", pSession);

            /* Sleep before retrying */

            if((pDownloadWakeMutex != NULL) && (pDownloadWakeCond != NULL))
            {
                /* Lock the download wake mutex */
                if(pthread_mutex_lock(pDownloadWakeMutex) != 0)
                {
                    ERROR("failed to lock download wake mutex");
                    rval = HLS_ERROR;
                    break;
                }
                                    
                /* Wait for DOWNLOAD_RETRY_WAIT_NSECS before going again */
                wakeTime.tv_nsec += DOWNLOAD_RETRY_WAIT_NSECS;
                
                /* Handle a rollover of the nanosecond portion of wakeTime */
                while(wakeTime.tv_nsec >= 1000000000) 
                {
                    wakeTime.tv_sec += 1;
                    wakeTime.tv_nsec -= 1000000000;
                }
                                
                DEBUG(DBG_NOISE,"sleeping until: %f", ((wakeTime.tv_sec)*1.0) + (wakeTime.tv_nsec/1000000000.0));
                                
                /* Wait until wakeTime */
                pthread_status = PTHREAD_COND_TIMEDWAIT(pDownloadWakeCond, pDownloadWakeMutex, &wakeTime);
                                    
                /* Unlock the playback controller wake mutex */
                if(pthread_mutex_unlock(pDownloadWakeMutex) != 0)
                {
                    ERROR("failed to unlock download wake mutex");
                    rval = HLS_ERROR;
                    break;
                }
                                
                /* If the timedwait call failed we need to bail */
                if((pthread_status != ETIMEDOUT) && (pthread_status != 0))
                {
                    ERROR("failed to timedwait on the download wake condition");
                    rval = HLS_ERROR;
                    break;
                }
            }
            else
            {
                /* Sleep for a bit, then try again */

                usleep(DOWNLOAD_RETRY_WAIT_NSECS/1000);
            }

            /* If we were told to stop trying, return HLS_CANCELLED */
            if((pbStopDownload) && (*pbStopDownload))
            {
                rval = HLS_CANCELLED;
                break;
            }
        }
        if(rval != HLS_OK) 
        {
            break;
        }

    }while(0);
 
    /* Close file */
    if(fpPlaylist) 
    {
        fclose(fpPlaylist);
        fpPlaylist = NULL;
    }

    return rval;
}

/**
 * hls_ok == valid playlist
 * Assumes calling thread has playlist WRITE lock
 * 
 * 
 * @param fpPlaylist
 * @param pPlaylist
 * @param pSession
 * 
 * @return #hlsStatus_t
 */
static hlsStatus_t m3u8PreprocessPlaylist(FILE* fpPlaylist, hlsPlaylist_t* pPlaylist, hlsSession_t* pSession)
{
    hlsStatus_t rval = HLS_OK;

    int targetDuration = 0;
    int startingSequenceNumber = 0;
    int bHaveCompletePlaylist = 0;
    int bCacheable = 1;
    int bIframesOnly = 0;
    hlsContentType_t mutability = HLS_UNSPECIFIED;

    char parseLine[PL_LINE_LENGTH];
    char* pTemp = NULL;

    if((fpPlaylist == NULL) || (pPlaylist == NULL) || (pSession == NULL))
    {
        ERROR("invalid parameter");
        return HLS_INVALID_PARAMETER;
    }
    
    do
    {
        /* Set initial values */
        pPlaylist->type = PL_INVALID;
        pPlaylist->version = 1;

        /* Grab first line from the file */
        rval = m3u8GetLine(fpPlaylist, parseLine, PL_LINE_LENGTH);
        if(rval) 
        {
            ERROR("problem reading from file");
            break;    
        }

        /* Check for EXTM3U tag in first line of file */    
        if(strstr(parseLine, "#EXTM3U") == NULL) 
        {
            ERROR("invalid playlist");
            rval = HLS_ERROR;
            break;
        }

        /* Parse the rest of the playlist filling in the relevant fields */

        /* Get a line from the file */
        rval = m3u8GetLine(fpPlaylist, parseLine, PL_LINE_LENGTH);
        while(!rval && strlen(parseLine) != 0)
        {
            /* Ignore lines without tags */
            if(strncmp(parseLine, "#EXT", strlen("#EXT")) == 0) 
            {
                switch(m3u8GetTag(parseLine)) 
                {
                    case EXTM3U:
                        /* ignore */
                        break;
                    case EXTINF:
                        pPlaylist->type = PL_MEDIA;
                        break;
                    case EXT_X_MEDIA:
                    case EXT_X_STREAM_INF:
                        pPlaylist->type = PL_VARIANT;
                        break;
                    case EXT_X_VERSION:
                        pTemp = parseLine + strlen("#EXT-X-VERSION:");
                        pPlaylist->version = atoi(pTemp);
                        if(pPlaylist->version > MAX_SUPPORTED_PL_VERSION) 
                        {
                            pPlaylist->type = PL_WRONGVER;
                            ERROR("got version %d playlist -- max supported version: %d", 
                                  pPlaylist->version, MAX_SUPPORTED_PL_VERSION);
                            rval = HLS_ERROR;
                        }
        
                        DEBUG(DBG_NOISE,"got version %d playlist -- max supported version: %d", 
                              pPlaylist->version, MAX_SUPPORTED_PL_VERSION);

                        break;
                    case EXT_X_TARGETDURATION:
                        pTemp = parseLine + strlen("#EXT-X-TARGETDURATION:");
                        targetDuration = atoi(pTemp);
                        break;
                    case EXT_X_MEDIA_SEQUENCE:
                        pTemp = parseLine + strlen("#EXT-X-MEDIA-SEQUENCE:");
                        startingSequenceNumber = atoi(pTemp);
                        break;
                    case EXT_X_ENDLIST:
                        bHaveCompletePlaylist = 1;
                        break;
                    case EXT_X_ALLOW_CACHE:
                        pTemp = parseLine + strlen("#EXT-X-ALLOW-CACHE:");
                        if(strncmp(pTemp, "NO", strlen("NO")) == 0)
                        {
                            bCacheable = 0;
                        }
                        break;
                    case EXT_X_PLAYLIST_TYPE:
                        pTemp = parseLine + strlen("#EXT-X-PLAYLIST-TYPE:");
                        if(strncmp(pTemp, "EVENT", strlen("EVENT")) == 0)
                        {
                            mutability = HLS_EVENT;
                        }
                        else if(strncmp(pTemp, "VOD", strlen("VOD")) == 0) 
                        {
                            mutability = HLS_VOD;
                        }
                        break;
                    case EXT_X_CISCO_PROT_HEADER:
                        rval = m3u8ParseProtHeader(parseLine, pSession);
                        if(rval != HLS_OK) 
                        {
                            ERROR("failed to parse #EXT-X-CISCO-PROT-HEADER");
                            break;
                        }
                        break;
                    case EXT_X_I_FRAMES_ONLY:
                        bIframesOnly = 1;
                        break;
                    default:
                        break;                    
                }   

            }

            if(rval) 
            {
                ERROR("error parsing tags");
                break;
            }

            rval = m3u8GetLine(fpPlaylist, parseLine, PL_LINE_LENGTH);
        }
        if(rval) 
        {
            break;
        }

        switch(pPlaylist->type) 
        {
            case PL_MEDIA:
                /* If we have a media playlist, we need to allocate the pMediaData structure to
                   hold all the additional media playlist data, if it doesn't already exist */
                if(pPlaylist->pMediaData == NULL) 
                {
                    pPlaylist->pMediaData = (hlsMediaPlaylistData_t*)malloc(sizeof(hlsMediaPlaylistData_t));
                    if(pPlaylist->pMediaData == NULL) 
                    {
                        ERROR("malloc error");
                        rval = HLS_MEMORY_ERROR;
                        break;
                    }
                    memset(pPlaylist->pMediaData, 0, sizeof(hlsMediaPlaylistData_t));
                }

                /* Fill the extra data */
                pPlaylist->pMediaData->targetDuration = targetDuration;
                pPlaylist->pMediaData->startingSequenceNumber = startingSequenceNumber;
                pPlaylist->pMediaData->bHaveCompletePlaylist = bHaveCompletePlaylist;
                pPlaylist->pMediaData->bCacheable = bCacheable;
                pPlaylist->pMediaData->mutability = mutability;
                pPlaylist->pMediaData->bIframesOnly = bIframesOnly;

                /* For live playlists the last valid play position is 3*TARGET_DURATION from the end of the playlist */
                if(!(bHaveCompletePlaylist))
                {
                    pPlaylist->pMediaData->endOffset = 3*targetDuration;
                }
                else
                {
                    pPlaylist->pMediaData->endOffset = 0;
                }

                /* For playlists with a floating start position, the first valid play position is 2*TARGET_DURATION
                   from the start of the playlist.  Playlists have a floating start position if they are incomplete
                   and their mutability is neither VOD nor EVENT. */
                if(!(bHaveCompletePlaylist) && (mutability == HLS_UNSPECIFIED))
                {
                    pPlaylist->pMediaData->startOffset = 2*targetDuration;
                }
                else
                {
                    pPlaylist->pMediaData->startOffset = 0;
                }

                break;
            case PL_VARIANT:
                /* If we have a variant playlist and the pMediaData structure was previously allocated,
                   we need to free it here */
                if(pPlaylist->pMediaData != NULL) 
                {
                    free(pPlaylist->pMediaData);
                    pPlaylist->pMediaData = NULL;
                }
                break;
            default:
                break;
        }
        if(rval) 
        {
            break;
        }

    } while (0);

    /* Leave the file how we found it */
    rewind(fpPlaylist);

    return rval;
}

/**
 * Parses a variant playlist.  This function should be run after 
 * m3u8PreprocessPlaylist(). 
 *  
 * Assumes calling thread has playlist WRITE lock 
 * 
 * @param fpPlaylist - pointer to playlist file to parse
 * @param pPlaylist - pointer to pre-allocated hlsPlaylist 
 *                  structure to datafill
 * 
 * @return #hlsStatus_t
 */
static hlsStatus_t m3u8ProcessVariantPlaylist(FILE* fpPlaylist, hlsPlaylist_t* pPlaylist)
{
    hlsStatus_t rval = HLS_OK;
    char parseLine[PL_LINE_LENGTH];
    char urlLine[PL_LINE_LENGTH];

    char* tempURL = NULL;

    if((fpPlaylist == NULL) || (pPlaylist == NULL))
    {
        ERROR("invalid parameter");
        return HLS_INVALID_PARAMETER;
    }

    do
    {
        /* Check that we have the right type of playlist */
        if(pPlaylist->type != PL_VARIANT) 
        {
            ERROR("wrong playlist type");
            rval = HLS_ERROR;
            break;
        }

        /* Get a line from the file */
        rval = m3u8GetLine(fpPlaylist, parseLine, PL_LINE_LENGTH);
        while(!rval && strlen(parseLine) != 0)
        {
            /* Ignore lines without tags */
            if(strncmp(parseLine, "#EXT", strlen("#EXT")) == 0) 
            {
                switch(m3u8GetTag(parseLine)) 
                {
                    case EXTM3U:
                        /* ignore this tag */
                        break;
                    case EXT_X_VERSION:
                        /* should have already been parsed */
                        break;
                    case EXT_X_STREAM_INF:
                        /* Create program list if it doesn't exist */
                        if(pPlaylist->pList == NULL) 
                        {
                            pPlaylist->pList = newLinkedList();
                            if(pPlaylist->pList == NULL) 
                            {
                                ERROR("problem allocating program list");
                                rval = HLS_ERROR;
                                break;
                            }
                        }
    
                        /* find next URL in file */
                        rval = m3u8findURL(urlLine, PL_LINE_LENGTH, fpPlaylist);
                        if(rval) 
                        {
                            ERROR("problem finding URL in file");
                            break;
                        }
    
                        /* Copy off the URL */
                        tempURL = (char*)malloc(strlen(urlLine)+1);
                        if(tempURL == NULL) 
                        {
                            ERROR("malloc error");
                            rval = HLS_MEMORY_ERROR;
                            break;
                        }
                        memset(tempURL, 0, strlen(urlLine)+1);
                        strcpy(tempURL, urlLine);
                
                        /* Prepend the baseURL, if necessary */
                        rval = createFullURL(&tempURL, pPlaylist->baseURL);
                        if(rval != HLS_OK) 
                        {
                            ERROR("error creating full URL");
                            break;
                        }

                        rval = m3u8ParseStreamInf(parseLine, tempURL, pPlaylist->pList);
                        free(tempURL);
                        tempURL = NULL;
                        break;
                    case EXT_X_I_FRAME_STREAM_INF:
                        /* Create program list if it doesn't exist */
                        if(pPlaylist->pList == NULL) 
                        {
                            pPlaylist->pList = newLinkedList();
                            if(pPlaylist->pList == NULL) 
                            {
                                ERROR("problem allocating program list");
                                rval = HLS_ERROR;
                                break;
                            }
                        }
    
                        /* Parse the tag */    
                        rval = m3u8ParseIFrameStreamInf(parseLine, pPlaylist->baseURL, pPlaylist->pList);
                        break;
                    case EXT_X_MEDIA:
                        DEBUG(DBG_INFO,"got EXT-X-MEDIA tag");
                        /* Create program list if it doesn't exist */
                        if(pPlaylist->pGroupList == NULL) 
                        {
                            pPlaylist->pGroupList = newLinkedList();
                            if(pPlaylist->pGroupList == NULL) 
                            {
                                ERROR("problem allocating group list");
                                rval = HLS_ERROR;
                                break;
                            }
                        }
                        
                        rval = m3u8ParseMedia(parseLine, pPlaylist->baseURL, pPlaylist->pGroupList);
                        break;
                    default:
                        DEBUG(DBG_WARN,"got unexpected tag, ignoring");
                        break;
                }
            }

            if(rval) 
            {
                ERROR("error parsing tags");
                break;
            }

            rval = m3u8GetLine(fpPlaylist, parseLine, PL_LINE_LENGTH);
        }

        /* Did we error out or hit EOF? */
        if(rval)
        {
            ERROR("problem reading from file");
            break;
        }

    }while (0);
    
    return rval;
}

/**
 * Parses a media playlist.  If pPlaylist->pList is not empty, 
 * then it assumes we are updating the playlist with a new 
 * version, and will add/remove segments as needed. 
 *  
 * Assumes calling thread has playlist WRITE lock 
 * 
 * @param fpPlaylist - pointer to playlist file to parse
 * @param pPlaylist - pointer to pre-allocated hlsPlaylist 
 *                  structure to datafill
 * 
 * @return #hlsStatus_t
 */
static hlsStatus_t m3u8ProcessMediaPlaylist(FILE* fpPlaylist, hlsPlaylist_t* pMediaPlaylist)
{
    hlsStatus_t rval = HLS_OK;
    char parseLine[PL_LINE_LENGTH];
    m3u8Tag_t tag = NUM_SUPPORTED_TAGS;
    char urlLine[PL_LINE_LENGTH];
    int currSeqNum = 0; // The sequence number of the segment we are currently processing. 
                        // Incremented each time we read a URL line (non-empty line that
                        // doesn't start with '#').

    hlsSegment_t* pSegment = NULL;

    int bSignalDiscontinuity = 0;
    int bSignalDateTime = 0;
    char dateLine[PL_LINE_LENGTH];
    int bSignalRangeFound = 0;
    char rangeLine[PL_LINE_LENGTH];

    int bKeyFound = 0;
    srcEncType_t encType = SRC_ENC_NONE;
    char* keyURI = NULL;
    char* iv = NULL;

    long nextSegmentOffset = 0;

    int lastSeqNum = -1;

    int firstKeySeqNum = -1;

    if((fpPlaylist == NULL) || (pMediaPlaylist == NULL))
    {
        ERROR("invalid parameter");
        return HLS_INVALID_PARAMETER;
    }

    do
    {
        /* Check that we have the right type of playlist */
        if(pMediaPlaylist->type != PL_MEDIA) 
        {
            ERROR("wrong playlist type");
            rval = HLS_ERROR;
            break;
        }

        if(pMediaPlaylist->pMediaData == NULL) 
        {
            ERROR("media playlist data is NULL");
            rval = HLS_ERROR;
            break;
        }

        /* Set our starting sequence number */
        currSeqNum = pMediaPlaylist->pMediaData->startingSequenceNumber;

        /* If we already have some segments in our list, this is an update */
        if(pMediaPlaylist->pList != NULL) 
        {
            /* Save off the last sequence number in our existing list */
            lastSeqNum = currSeqNum + pMediaPlaylist->pList->numElements - 1;
        }

        /* Grab a line from the file */
        rval = m3u8GetLine(fpPlaylist, parseLine, PL_LINE_LENGTH);
        while(!rval && strlen(parseLine) != 0)
        {
            /* Tags start with #EXT */
            if(strncmp(parseLine, "#EXT", strlen("#EXT")) == 0) 
            {
                tag = m3u8GetTag(parseLine);
                switch(tag) 
                {
                    case EXTM3U:
                        /* ignore this tag */
                        break;
                    case EXT_X_VERSION:
                        /* should have already been parsed */
                        break;
                    case EXTINF:
                        /* Skip over segments that should already be in
                           our segment list */
                        if(currSeqNum > lastSeqNum) 
                        {
                            /* We're adding segments, so reset this counter */
                            pMediaPlaylist->unchangedReloads = -1;

                            /* Create segment list if it doesn't exist */
                            if(pMediaPlaylist->pList == NULL) 
                            {
                                pMediaPlaylist->pList = newLinkedList();
                                if(pMediaPlaylist->pList == NULL) 
                                {
                                    ERROR("problem allocating segment list");
                                    rval = HLS_ERROR;
                                    break;
                                }
                            }
        
                            /* find next URL in file */
                            rval = m3u8findURL(urlLine, PL_LINE_LENGTH, fpPlaylist);
                            if(rval) 
                            {
                                ERROR("problem finding URL in file");
                                break;
                            }
    
                            /* Parse the tag -- this should add a segment node
                               to the segment linked list */
                            rval = m3u8ParseInf(parseLine, urlLine, pMediaPlaylist->pList);
                            if(rval) 
                            {
                                break;
                            }
    
                            /* Get last added segment */
                            if(pMediaPlaylist->pList != NULL &&
                               pMediaPlaylist->pList->pTail != NULL) 
                            {
                                pSegment = (hlsSegment_t*)(pMediaPlaylist->pList->pTail->pData);
                            }
                            else
                            {
                                ERROR("invalid segment list");
                                rval = HLS_ERROR;
                            }

                            /* Fill in additional segment information */
                            if(pSegment != NULL) 
                            {
                                /* Set the sequence number */
                                pSegment->seqNum = currSeqNum;

                                /* Update playlist duration */
                                pMediaPlaylist->pMediaData->duration += pSegment->duration;
    
                                /* Increment the current playlist position from 'end' */
                                pMediaPlaylist->pMediaData->positionFromEnd += pSegment->duration;
    
                                /* If we have hit a key tag, all the subsequent
                                   segments should use the information in that
                                   tag */
                                if(bKeyFound) 
                                {
                                    /* For AES-128-CTR encryption where a KEY tag
                                     * specifies an IV:
                                     *  - first segment IV == IV in key tag
                                     *  - IV for subsequent segments specified using
                                     *    BYTERANGE tag == IV of previous segment
                                     *  - IV for subsequent segments NOT specified using
                                     *    BYTERANGE tag == [IV of previous segment] + *2^64
                                     */ 
                                    if((iv != NULL) && 
                                       (encType == SRC_ENC_AES128_CTR) && 
                                       (!bSignalRangeFound) &&  // no BYTERANGE tag
                                       (pSegment->seqNum != firstKeySeqNum))  // not first segment after KEY tag
                                    {
                                        rval = incCtrIv(&iv);
                                        if(rval != HLS_OK) 
                                        {
                                            ERROR("failed to increment AES-128-CTR IV");
                                            break;
                                        }
                                    }

                                    rval = addSegmentEncInfo(pSegment, encType, iv, keyURI);
                                    if(rval != HLS_OK) 
                                    {
                                        ERROR("failed to add key info to segment");
                                        break;
                                    }
    
                                }
    
                                /* A discontinuity was signalled prior to
                                   this segment */
                                if(bSignalDiscontinuity) 
                                {
                                    pSegment->bDiscontinuity = 1;
                                    bSignalDiscontinuity = 0;
                                }
    
                                /* A program date/time tag was encountered
                                   prior to this segment */
                                if(bSignalDateTime) 
                                {
                                    rval = m3u8ParseDateTime(dateLine, pSegment);
                                    memset(dateLine, 0, PL_LINE_LENGTH);
                                    bSignalDateTime = 0;
                                    if(rval != HLS_OK) 
                                    {
                                        break;
                                    }
                                }

                                /* A byterange tag was encountered
                                   prior to this segment */
                                if(bSignalRangeFound) 
                                {
                                    rval = m3u8ParseByteRange(rangeLine, pSegment, &nextSegmentOffset);
                                    memset(rangeLine, 0, PL_LINE_LENGTH);
                                    bSignalRangeFound = 0;
                                    if(rval != HLS_OK) 
                                    {
                                        break;
                                    }
                                }
                            }
                            else
                            {
                                ERROR("invalid segment");
                                rval = HLS_ERROR;
                            }
                            DEBUG(DBG_NOISE, "segment %d added to list", currSeqNum);
                        }
                        else
                        {
                            DEBUG(DBG_NOISE, "segment %d already in list", currSeqNum);
                        }
                        break;
                    case EXT_X_TARGETDURATION:
                        /* should have already been parsed */
                        break;
                    case EXT_X_MEDIA_SEQUENCE:
                        /* should have already been parsed */
                        break;
                    case EXT_X_ENDLIST:
                        /* should have already been parsed */
                        break;
                    case EXT_X_ALLOW_CACHE:
                        /* should have already been parsed */
                        break;
                    case EXT_X_PLAYLIST_TYPE:
                        /* should have already been parsed */
                        break;

                        /* These next four tags apply to the 'next media file' in the playlist.
                         * However, the spec is not clear about the ordering of tags, so there is
                         * no way of knowing whether the tags appear like this:
                         *
                         * #EXT-X-DISCONTINUITY
                         * #EXTINF 10,
                         * http://foo.bar.com/segment1.ts
                         *
                         * or:
                         *
                         * #EXTINF 10,
                         * #EXT-X-DISCONTINUITY
                         * http://foo.bar.com/segment1.ts
                         *
                         * So we need to figure out if the segment we want to apply the setting to
                         * has already been processed and added to the list.
                         */
                    case EXT_X_DISCONTINUITY:
                        /* Skip over segments that should already be in
                           our segment list */
                        if(currSeqNum > lastSeqNum) 
                        {
                            /* Get last added segment */
                            if(pMediaPlaylist->pList != NULL &&
                               pMediaPlaylist->pList->pTail != NULL) 
                            {
                                pSegment = (hlsSegment_t*)(pMediaPlaylist->pList->pTail->pData);
                            }
                            else
                            {
                                /* No segments in the list, so signal discontinuity in the next one */
                                bSignalDiscontinuity = 1;
                                break;
                            }
    
                            /* Compare the current sequence number to the last-added sequence number */
                            if(currSeqNum == pSegment->seqNum) 
                            {
                                pSegment->bDiscontinuity = 1;
                            }
                            else
                            {
                                bSignalDiscontinuity = 1;
                            }
                        }
                        break;
                    case EXT_X_PROGRAM_DATE_TIME:
                        /* Skip over segments that should already be in
                           our segment list */
                        if(currSeqNum > lastSeqNum) 
                        {
                            /* Get last added segment */
                            if(pMediaPlaylist->pList != NULL &&
                               pMediaPlaylist->pList->pTail != NULL) 
                            {
                                pSegment = (hlsSegment_t*)(pMediaPlaylist->pList->pTail->pData);
                            }
                            else
                            {
                                /* No segments in the list, so process the date/time
                                   when we process the next one */
                                strcpy(dateLine, parseLine);
                                bSignalDateTime = 1;
                                break;
                            }
    
                            /* Compare the current sequence number to the last-added sequence number */
                            if(currSeqNum == pSegment->seqNum) 
                            {
                                rval = m3u8ParseDateTime(parseLine, pSegment);
                                bSignalDateTime = 0;
                                if(rval) 
                                {
                                    break;
                                }
                            }
                            else
                            {
                                strcpy(dateLine, parseLine);
                                bSignalDateTime = 1;
                            }
                        }
                        break;
                    case EXT_X_KEY:
                    case EXT_X_CISCO_KEY:
                        bKeyFound = 1;

                        encType = SRC_ENC_NONE;
                        free(iv);
                        iv = NULL;
                        free(keyURI);
                        keyURI = NULL;

                        /* Parse the key tag to get the relevant information */
                        rval = m3u8ParseKey(parseLine, &encType, &iv, &keyURI);
                        if(rval || (NULL == keyURI)) 
                        {
                            break;
                        }

                        //
                        // if the keyURI is an offset we have to generate a full length URI
                        //

                        if (0 != strncmp("htt", keyURI, 3))
                        {
                           // We are not starting at a full url we will have to 
                           // generate the full url


                              int newStrSize=0;
                              char *pNew, *pOld;

                              pOld = keyURI;
                              newStrSize += strlen(keyURI);
                              newStrSize += strlen(pMediaPlaylist->baseURL);
                              newStrSize++;
                              pNew = malloc(newStrSize);
                              memset(pNew, 0, newStrSize);
                              strncpy(pNew, pMediaPlaylist->baseURL, strlen(pMediaPlaylist->baseURL));
                              strncat(pNew, keyURI, strlen(keyURI));

                              // now switch the urls
                              keyURI = pNew;
                              free(pOld);
                              pNew = NULL;
                              pOld = NULL;
                        }
                        /* Save the current sequence number as the first
                           sequence number this key tag was applied to. */
                        firstKeySeqNum = currSeqNum;

                        /* Skip over segments that should already be in
                           our segment list */
                        if(currSeqNum > lastSeqNum) 
                        {
                            /* We need to see if we've already processed a "following"
                               segment */
    
                            /* Get last added segment */
                            if(pMediaPlaylist->pList != NULL &&
                               pMediaPlaylist->pList->pTail != NULL) 
                            {
                                pSegment = (hlsSegment_t*)(pMediaPlaylist->pList->pTail->pData);
                            }
                            else
                            {
                                /* No segments in the list */
                                break;
                            }
    
                            /* Compare the current sequence number to the last-added sequence number */
                            if(currSeqNum == pSegment->seqNum) 
                            {
                                /* The first segment this applies to was already added to the playlist,
                                   so add key info to it */
                                rval = addSegmentEncInfo(pSegment, encType, iv, keyURI);
                                if(rval != HLS_OK) 
                                {
                                    ERROR("failed to add key info to segment");
                                    break;
                                }
                            }
                        }
                        break;
                    case EXT_X_BYTERANGE:
                        /* Skip over segments that should already be in
                           our segment list */
                        if(currSeqNum > lastSeqNum) 
                        {
                            /* Get last added segment */
                            if(pMediaPlaylist->pList != NULL &&
                               pMediaPlaylist->pList->pTail != NULL) 
                            {
                                pSegment = (hlsSegment_t*)(pMediaPlaylist->pList->pTail->pData);
                            }
                            else
                            {
                                /* No segments in the list, so process the byterange
                                   when we process the next one */
                                strcpy(rangeLine, parseLine);
                                bSignalRangeFound = 1;
                                break;
                            }
    
                            /* Compare the current sequence number to the last-added sequence number */
                            if(currSeqNum == pSegment->seqNum) 
                            {
                                /* The segment this BYTERANGE applies to has already been added to the playlist,
                                   so apply the range to it. */
                                rval = m3u8ParseByteRange(parseLine, pSegment, &nextSegmentOffset);
                                bSignalRangeFound = 0;
                                if(rval) 
                                {
                                    break;
                                }

                                /* For AES-128-CTR encryption where a KEY tag
                                 * specifies an IV:
                                 *  - first segment IV == IV in key tag
                                 *  - IV for subsequent segments specified using
                                 *    BYTERANGE tag == IV of previous segment
                                 *  - IV for subsequent segments NOT specified using
                                 *    BYTERANGE tag == [IV of previous segment] + *2^64
                                 *
                                 *  If we have already added the segment this BYTERANGE applies to
                                 *  to the playlist, and we have applied an incremented IV, we need to
                                 *  decrement the IV to correct it.
                                 */ 
                                if((bKeyFound) && (iv != NULL) &&
                                   (pSegment->encType == SRC_ENC_AES128_CTR) &&
                                   (pSegment->seqNum != firstKeySeqNum)) // Don't decrement if this is the first segment after KEY tag
                                {
                                   //
                                   // NOTE: TODO: RMS - I changed IV to a 16 byte value  for basic HLS
                                   // these changes have not been tested with AES-128-CTR.
                                   //
                                    /* Decrement the segment's IV */
                                    rval = decCtrIv(&(pSegment->iv));
                                    if(rval != HLS_OK) 
                                    {
                                        ERROR("failed to decrement AES-128-CTR IV");
                                        break;
                                    }

                                    /* Decrement the stored IV for the last segment (this segment). */
                                    rval = decCtrIv(&iv);
                                    if(rval != HLS_OK) 
                                    {
                                        ERROR("failed to decrement AES-128-CTR IV");
                                        break;
                                    }
                                }

                            }
                            else
                            {
                                strcpy(rangeLine, parseLine);
                                bSignalRangeFound = 1;
                            }
                        }
                        break;
                    default:
                        DEBUG(DBG_WARN,"got unexpected tag, ignoring");
                        break;
                }
            }
            else if(strncmp(parseLine, "#", strlen("#")) != 0) 
            {
                /* Lines that don't start with '#' are URIs -- when
                 * we hit one, increment the current sequence number.
                 * Ignore empty lines... 
                 */ 
                if(strlen(parseLine) != strlen("")) 
                {
                    currSeqNum++;
                }
            }

            if(rval) 
            {
                ERROR("error parsing tags");
                break;
            }

            rval = m3u8GetLine(fpPlaylist, parseLine, PL_LINE_LENGTH);
        }

        /* Did we error out or hit EOF? */
        if(rval)
        {
            ERROR("problem reading from file");
            break;
        }

    }while (0);
    
    free(iv);
    iv = NULL;
    free(keyURI);
    keyURI = NULL;

    /* Update our unchange reload counter -- it will have been set to -1 if we changed anything */
    pMediaPlaylist->unchangedReloads += 1;

    return rval;
}

/**
 * Re-downloads and re-parses a playlists.  This function will
 * re-check for redirects and will update the playlist structure
 * as necessary.
 * Assumes calling thread has playlist WRITE lock
 * 
 * 
 * @param pPlaylist - pointer to hlsPlaylist structure to update
 * @param pSession
 * 
 * @return #hlsStatus_t
 */
static hlsStatus_t m3u8UpdatePlaylist(hlsPlaylist_t* pPlaylist, hlsSession_t* pSession)
{
    hlsStatus_t rval = HLS_OK;

    char* filePath = NULL;

    FILE* fpPlaylist = NULL;

    char* tempRedirectURL = NULL;

    hlsPlaylist_t* pTempPlaylist = NULL;

    int preParseErrors = 0;

    if((pPlaylist == NULL) || (pSession == NULL))
    {
        ERROR("invalid parameter");
        return HLS_INVALID_PARAMETER;
    }

    // TODO: Do we want to support PL_VARIANT updates?

    /* Currently we don't support updating variant playlists.
       This shouldn't be an error, though. */
    if(pPlaylist->type != PL_MEDIA) 
    {
        DEBUG(DBG_WARN,"we don't support updating playlists of type %d", pPlaylist->type);
        return HLS_OK;
    }

    /* If this playlist has never been parsed before, call m3u8ParsePlaylist() instead */
    if((pPlaylist->nextReloadTime.tv_sec == 0) && (pPlaylist->nextReloadTime.tv_nsec == 0))
    {
        return m3u8ParsePlaylist(pPlaylist, pSession);
    }

    do
    {
        /* Generate the local download path */
        rval = getLocalPath(pPlaylist->playlistURL, &filePath, pSession->sessionName);
        if(rval != HLS_OK) 
        {
            ERROR("error generating local path");
            break;
        }

        do
        {
            /* Download the playlist */
            rval = m3u8DownloadPlaylist(pPlaylist->playlistURL, filePath, &tempRedirectURL, pSession);
            if(rval != HLS_OK) 
            {
                ERROR("error downloading playlist");
                break;
            }
    
            /* Store the current download time (we will add the wait offset later) */
            if(clock_gettime(CLOCK_MONOTONIC, &(pPlaylist->nextReloadTime)) != 0) 
            {
                ERROR("failed to get current time");
                rval = HLS_ERROR;
                break;
            }
    
            DEBUG(DBG_INFO,"downloaded playlist @ %d", (int)pPlaylist->nextReloadTime.tv_sec);
    
            /* Open downloaded file */
            fpPlaylist = fopen(filePath, "r");
            if(fpPlaylist == NULL) 
            {
                ERROR("fopen() failed on file %s -- %s", filePath, strerror(errno));
                rval = HLS_FILE_ERROR;
                break;
            }
    
            pTempPlaylist = newHlsPlaylist();
            if(pTempPlaylist == NULL) 
            {
                ERROR("newHlsPlaylist() failed");
                rval = HLS_MEMORY_ERROR;
                break;
            }
    
            /* TODO: Comment... */ 
            rval = m3u8PreprocessPlaylist(fpPlaylist, pTempPlaylist, pSession);
            if(rval != HLS_OK) 
            {
                /* If we errored on pre-processing the update, it is possible
                   that we will recover if we try again in a bit */
                if(preParseErrors < MAX_PL_PARSE_REDL_RETRIES) 
                {
                    DEBUG(DBG_WARN,"error pre-processing playlist -- will retry %d times", MAX_PL_PARSE_REDL_RETRIES - preParseErrors);
                    preParseErrors++;

                    /* Things are OK for now... */
                    rval = HLS_OK;
    
                    /* Clean up and start over */
                    /* Close file */
                    if(fpPlaylist) 
                    {
                        fclose(fpPlaylist);
                        fpPlaylist = NULL;
                    }
                    
                    free(tempRedirectURL);
                    tempRedirectURL = NULL;
                    freePlaylist(pTempPlaylist);
                    pTempPlaylist = NULL;
                }
                else
                {
                    ERROR("error pre-processing playlist after %d retries", MAX_PL_PARSE_REDL_RETRIES);
                    break;
                }
            }
            else
            {
                /* Reset the error count */
                preParseErrors = 0;
            }

        } while(preParseErrors != 0);
        if(rval != HLS_OK) 
        {
            break;
        }

        /* If this playlist isn't the same type and version as the previous one, quit */
        if((pTempPlaylist->type != pPlaylist->type) || 
           (pTempPlaylist->version != pPlaylist->version)) 
        {
            ERROR("type or version mismatch in updated playlist");
            rval = HLS_ERROR;
            break;
        }

#if 0
// TODO:
                    case EXT_X_TARGETDURATION:
                        // check this against the currently saved target duration?
                        // if mismatch, then error
                        break;
#endif

        if(pPlaylist->type == PL_MEDIA) 
        {
            if((pPlaylist->pMediaData == NULL) || (pTempPlaylist->pMediaData == NULL))
            {
                ERROR("media playlist data is NULL");
                rval = HLS_ERROR;
                break;
            }

            /* Copy any updated values from the new playlist. */
            pPlaylist->pMediaData->targetDuration = pTempPlaylist->pMediaData->targetDuration;
            pPlaylist->pMediaData->startingSequenceNumber = pTempPlaylist->pMediaData->startingSequenceNumber;
            pPlaylist->pMediaData->bHaveCompletePlaylist = pTempPlaylist->pMediaData->bHaveCompletePlaylist;
            pPlaylist->pMediaData->bCacheable = pTempPlaylist->pMediaData->bCacheable;
            pPlaylist->pMediaData->mutability = pTempPlaylist->pMediaData->mutability;
            pPlaylist->pMediaData->startOffset = pTempPlaylist->pMediaData->startOffset;
            pPlaylist->pMediaData->endOffset = pTempPlaylist->pMediaData->endOffset;
        }

        /* Check if redirection stuff has changed since the last time we downloaded */
        if(strcmp(tempRedirectURL, pPlaylist->redirectURL) != 0) 
        {
            /* Copy off the new redirect URL */
            pPlaylist->redirectURL = (char*)realloc(pPlaylist->redirectURL, strlen(tempRedirectURL)+1);
            if(pPlaylist->redirectURL == NULL) 
            {
                ERROR("realloc error");
                rval = HLS_MEMORY_ERROR;
                break;
            }
            memset(pPlaylist->redirectURL, 0, strlen(tempRedirectURL)+1);
            strcpy(pPlaylist->redirectURL, tempRedirectURL);
    
            DEBUG(DBG_NOISE,"redirect URL: %s", pPlaylist->redirectURL);
    
            free(pPlaylist->baseURL);
            pPlaylist->baseURL = NULL;

            /* Generate base URL based on new redirect URL */
            rval = getBaseURL(pPlaylist->redirectURL, &(pPlaylist->baseURL));
            if(rval)
            {
                ERROR("failed to generate base URL");
                break;
            }
        
            DEBUG(DBG_NOISE,"base URL: %s", pPlaylist->baseURL);
        }

        /* Now that we know the playlist type, start parsing */
        switch(pPlaylist->type) 
        {
            case PL_VARIANT:
                /* This should have been caught above -- if we're here
                   we might be in trouble, so error. */
                ERROR("updating variant playlist unsupported");
                rval = HLS_ERROR;
                break;
            case PL_MEDIA:
                DEBUG(DBG_INFO,"updating media playlist");
                rval = m3u8UpdateMediaPlaylist(fpPlaylist, pPlaylist);
                if(rval == HLS_OK)
                {
                    /* Set the time until the next reload of the playlist */
                    switch(pPlaylist->unchangedReloads) 
                    {
                        case 0:
                            /* If playlist has changed, the minimum wait time is the length
                               of the last segment in the PL */
                            if((pPlaylist->pList != NULL) &&
                               (pPlaylist->pList->pTail != NULL) &&
                               (pPlaylist->pList->pTail->pData != NULL))
                            {
                                pPlaylist->nextReloadTime.tv_sec += (int)(((hlsSegment_t*)(pPlaylist->pList->pTail->pData))->duration);
                                DEBUG(DBG_INFO,"next update @ %d", (int)pPlaylist->nextReloadTime.tv_sec);
                            }
                            // TODO: ERROR?
                            break;

                        /* if a playlist has NOT changed, the minimum wait time is:
                                      first .5*TARGET_DURATION
                                      then 1.5*TARGET_DURATION
                                      and 3*TARGET_DURATION thereafter */
                        case 1:
                            pPlaylist->nextReloadTime.tv_sec += (pPlaylist->pMediaData->targetDuration)/2;
                            DEBUG(DBG_INFO,"no change, backing off -- next update @ %d", (int)pPlaylist->nextReloadTime.tv_sec);
                            break;
                        case 2:
                            pPlaylist->nextReloadTime.tv_sec += (3*(pPlaylist->pMediaData->targetDuration))/2;
                            DEBUG(DBG_INFO,"no change, backing off -- next update @ %d", (int)pPlaylist->nextReloadTime.tv_sec);
                            break;
                        default:
                            pPlaylist->nextReloadTime.tv_sec += 3*(pPlaylist->pMediaData->targetDuration);
                            DEBUG(DBG_INFO,"no change, backing off -- next update @ %d", (int)pPlaylist->nextReloadTime.tv_sec);
                            break;
                    }
                }
                break;
            case PL_WRONGVER:
                ERROR("unsupported playlist version: %d", pPlaylist->version);
                rval = HLS_ERROR;
                break;
            case PL_INVALID:
            default:
                rval = HLS_ERROR;
                ERROR("invalid playlist type: %d", pPlaylist->type);
                break;
        }
        if(rval) 
        {
            break;
        }

    }while(0);

    /* Close file */
    if(fpPlaylist) 
    {
        fclose(fpPlaylist);
        fpPlaylist = NULL;
    }

    /* Delete downloaded file */
    // TODO: check for file deletion errors?
    if(filePath != NULL) 
    {
        unlink(filePath);
        free(filePath);
        filePath = NULL;
    }

    free(tempRedirectURL);
    tempRedirectURL = NULL;
    freePlaylist(pTempPlaylist);
    pTempPlaylist = NULL;

    return rval;
}

/**
 * Update the playlist structure pointed to by pPlaylist using 
 * the information in new playlist file pointed to by 
 * fpPlaylist.  Assumes that m3u8PreprocessPlaylist() has 
 * already been called on the new playlist and the relevant 
 * values have been updated (startingSequenceNumber, etc.). 
 *  
 * Assumes calling thread has playlist WRITE lock 
 *  
 * @param fpPlaylist - file pointer to new playlist file
 * @param pMediaPlaylist - pointer to hlsPlaylist structure to 
 *                       update
 * 
 * @return #hlsStatus_t
 */
static hlsStatus_t m3u8UpdateMediaPlaylist(FILE* fpPlaylist, hlsPlaylist_t* pMediaPlaylist)
{
    hlsStatus_t rval = HLS_OK;
    int seqNum = 0;

    llStatus_t llstat = LL_OK;

    llNode_t* pSegmentNode = NULL;
    hlsSegment_t* pSegment = NULL;

    if((fpPlaylist == NULL) || (pMediaPlaylist == NULL))
    {
        ERROR("invalid parameter");
        return HLS_INVALID_PARAMETER;
    }

    do
    {
        /* Check that we have the right type of playlist */
        if(pMediaPlaylist->type != PL_MEDIA) 
        {
            ERROR("wrong playlist type");
            rval = HLS_ERROR;
            break;
        }

        if(pMediaPlaylist->pMediaData == NULL) 
        {
            ERROR("media playlist data is NULL");
            rval = HLS_ERROR;
            break;
        }        

        /* Compare the current segment list to the contents of the new playlist,
           and remove any segments that have been dropped */
        if(pMediaPlaylist->pList != NULL) 
        {
            /* Get the first sequence number in our existing list.
               Can't use pPlaylist->startingSequenceNumber as
               it will have been updated by m3u8PreprocessPlaylist() */
            pSegmentNode = pMediaPlaylist->pList->pHead;
            if(pSegmentNode != NULL) 
            {
                pSegment = (hlsSegment_t*)(pSegmentNode->pData);
                if(pSegment != NULL) 
                {
                    seqNum = pSegment->seqNum;
                }
                else
                {
                    ERROR("malformed segment list");
                    rval = HLS_ERROR;
                    break;
                }
            }
            else
            {
                ERROR("malformed segment list");
                rval = HLS_ERROR;
                break;
            }
            
            /* If there was no change in the starting sequence number, don't drop any segments
               If oldSeq < newSeq, remove segments with seqNum < newSeq
               if oldSeq > newSeq, something weird is going on -- dump the existing playlist and start from scratch */
            if(seqNum < pMediaPlaylist->pMediaData->startingSequenceNumber) 
            {
                /* We're deleting segments, so reset this counter */
                pMediaPlaylist->unchangedReloads = -1;

                while(pSegmentNode != NULL) 
                {
                    pSegment = (hlsSegment_t*)(pSegmentNode->pData);

                    if(pSegment == NULL) 
                    {
                        ERROR("NULL segment in linked list");
                        rval = HLS_ERROR;
                        break;
                    }
                    
                    if(pSegment->seqNum < pMediaPlaylist->pMediaData->startingSequenceNumber)
                    {
                        pSegment = NULL;

                        /* Drop the segment */
                        llstat = removeHead(pMediaPlaylist->pList, (void**)(&pSegment));
                        if(llstat != LL_OK)
                        {
                            ERROR("failed to remove head node from list");
                            rval = HLS_ERROR;
                            break;
                        }

                        if(pSegment != NULL)
                        {
                            /* Decrement the current playlist duration */
                            pMediaPlaylist->pMediaData->duration -= pSegment->duration;
                            DEBUG(DBG_NOISE,"segment %d removed from list", pSegment->seqNum);
                            freeSegment(pSegment);
                            pSegment = NULL;
                        }
                    }
                    else
                    {
                        break;
                    }
    
                    pSegmentNode = pMediaPlaylist->pList->pHead;
                }
                if(rval != HLS_OK) 
                {
                    break;
                }

            }
            else if(seqNum > pMediaPlaylist->pMediaData->startingSequenceNumber) 
            {
                /* We're starting over, so reset this counter */
                pMediaPlaylist->unchangedReloads = -1;

                DEBUG(DBG_WARN,"starting sequence number has DECREASED! dumping existing playlist...");

                pSegment = NULL;

                while(pMediaPlaylist->pList->numElements > 0) 
                {
                    llstat = removeHead(pMediaPlaylist->pList, (void**)(&pSegment));
                    if(llstat != LL_OK)
                    {
                        ERROR("failed to remove head node from list");
                        rval = HLS_ERROR;
                        break;
                    }

                    if(pSegment != NULL) 
                    {
                        freeSegment(pSegment);
                        pSegment = NULL;
                    }
                }
                if(rval != HLS_OK) 
                {
                    break;
                }

                freeLinkedList(pMediaPlaylist->pList);
                pMediaPlaylist->pList = NULL;
            }
        }

        /* Parse the new list, appending new segments to our existing segment list */
        rval = m3u8ProcessMediaPlaylist(fpPlaylist, pMediaPlaylist);

    } while (0);
    
    return rval;
}

/**
 * Grabs one line from the source file, removes any '\n' or '\r'
 * characters, and writes it into a buffer. 
 *  
 * Any empty lines are skipped.
 *  
 * On EOF, returns HLS_OK but pDest will be empty (all 0s)
 *  
 * @param fpPlaylist - file pointer to source file
 * @param pDest - buffer to write data to
 * @param length - size of pDest
 * 
 * @return #hlsStatus_t
 */
static hlsStatus_t m3u8GetLine(FILE* fpPlaylist, char* pDest, int length)
{
    hlsStatus_t rval = HLS_OK;

    if((fpPlaylist == NULL) || (pDest == NULL))
    {
        ERROR("invalid parameter");
        return HLS_INVALID_PARAMETER;
    }

    memset(pDest, 0, length);

    if(NULL != fgets(pDest, length, fpPlaylist))
    {
        /* Strip off '\n' and '\r' */
        m3u8NormalizeString(pDest);

        /* If after normalizing the strlen == 0, we got an empty line
           and should try again to grab a valid one. */
        if(strlen(pDest) == 0) 
        {
            DEBUG(DBG_NOISE, "got empty line, try again");
            m3u8GetLine(fpPlaylist, pDest, length);
        }
        else
        {
            DEBUG(DBG_NOISE,"got line: %s", pDest);
        }
    }
    else if(ferror(fpPlaylist))
    {
        ERROR("problem reading from file");
        rval = HLS_FILE_ERROR;
    }
    else
    {
        DEBUG(DBG_NOISE,"got EOF");
    }

    return rval;
}

/**
 * Parses input string and returns the first supported tag.
 * 
 * @param pString - string to parse
 * 
 * @return m3u8Tag
 */
static m3u8Tag_t m3u8GetTag(char* pString)
{
    m3u8Tag_t tag = NUM_SUPPORTED_TAGS;
    int i = 0;

    if(pString == NULL)
    {
        ERROR("invalid parameter");
        return tag;
    }

    for(i = 0; i < NUM_SUPPORTED_TAGS; i++)
    {
        if(strncmp(m3u8TagStrings[i], pString, strlen(m3u8TagStrings[i])) == 0)
        {
            DEBUG(DBG_NOISE,"found tag \'%s\'", m3u8TagStrings[i]);
            tag = i;
            break;  
        }
    }

    return tag;
}

/**
 * Removes trailing '\n' and '\r' from string
 * 
 * @param pString - string to normalize
 */
static void m3u8NormalizeString(char *pString)
{
    int strLen = 0;
    char *strTmp = NULL;
    int i = 0;
   
    if(pString != NULL) 
    {
        DEBUG(DBG_NOISE, "pString = %s", pString);

        strLen = strlen(pString);
       
        /* Loop in the string and look for /r, /n, etc */
        for(i = 1; i <= strLen; i++)
        {
            strTmp = pString + (strLen - i);
          
            if(*strTmp == '\n')
            {
                DEBUG(DBG_NOISE,"replacing '\\n' with '\\0'");
                *strTmp = 0;
            }
            else if(*strTmp == '\r')
            {
                DEBUG(DBG_NOISE,"replacing '\\r' with '\\0'");
                *strTmp = 0;
            }
            else
            {
                break;
            }
        }
    }
}

/** 
 * Finds the next URL in the file pointed to by fpPlaylist. 
 *  
 * A URL is defined as any line not starting with '#'. 
 *  
 * If no URL is found, return HLS_OK but buffer is empty. 
 *  
 * File pointed to by fpPlaylist is unchanged and the stream is 
 * reset to where it was at call time. 
 * 
 * @param buffer - pre-allocated buffer to write found URL into
 * @param bufferLength - length of buffer
 * @param fpPlaylist - pointer to playlist file to parse
 * 
 * @return #hlsStatus_t
 */
static hlsStatus_t m3u8findURL(char* buffer, int bufferLength, FILE* fpPlaylist)
{
    hlsStatus_t rval = HLS_OK;
    long filepos = 0L;

    if((buffer == NULL) || (fpPlaylist == NULL))
    {
        ERROR("invalid parameter");
        return HLS_INVALID_PARAMETER;
    }

    do
    {
        /* get current file position */
        filepos = ftell(fpPlaylist);
                                
        /* find next URL in file (line not starting with '#') */
        rval = m3u8GetLine(fpPlaylist, buffer, bufferLength);
        while(!rval && strlen(buffer) != 0)
        {
            if(strncmp(buffer, "#", 1) != 0)
            {
                DEBUG(DBG_NOISE,"found URL: %s", buffer);
                break;
            }

            rval = m3u8GetLine(fpPlaylist, buffer, bufferLength);
        }

        /* Did we error out or hit EOF? */
        if(rval) 
        {
            ERROR("problem reading from file");
            break;
        }
            
        /* reset the file position to before the URL search */
        if(fseek(fpPlaylist, filepos, SEEK_SET))
        {
            ERROR("problem setting file position");
            rval = HLS_FILE_ERROR;
            break;
        }
    
    } while (0);

    return rval;
}

/** 
 * 
 * 
 * @param tagLine
 * @param urlLine
 * @param pProgramList
 * 
 * @return #hlsStatus_t
 */
static hlsStatus_t m3u8ParseStreamInf(char *tagLine, char* urlLine, llist_t* pProgramList)
{
    hlsStatus_t rval = HLS_OK;
    llStatus_t llerror = LL_OK;
    int programID = 0;
    int bitrate = 0;
    int width = 0;
    int height = 0;
    int i = 0;
    int sortTemp = 0;
    char* codecs = NULL;
    char* audio = NULL;
    char* video = NULL;

    llNode_t* pProgramNode = NULL;
    hlsProgram_t* pProgram = NULL;
    hlsPlaylist_t* pStreamPL = NULL;
    
    char* pTemp = NULL;
    char* pTok = NULL;
    char* pIndex = NULL;

    if((tagLine == NULL) || (urlLine == NULL) || (pProgramList == NULL))
    {
        ERROR("invalid parameter");
        return HLS_INVALID_PARAMETER;
    }

    DEBUG(DBG_NOISE,"parsing: %s", tagLine);

    do
    {
        /* Get to the start of the attribute list */
        pTemp = tagLine + strlen("#EXT-X-STREAM-INF:");

        /* Tokenize and parse attributes */
        pTok = strtok(pTemp, ",");
        while(pTok != NULL) 
        {

            /* Because attributes can include whitespace, we don't want to
               tokenzie using the ' ' character.  Because of this, we have
               to remove any leading whitespace following a ',' in the
               attribute list */
            while(pTok[0] == ' ') 
            {
                pTok++;
            }
            DEBUG(DBG_NOISE,"got token \"%s\"", pTok);

            if(strncmp(pTok, "PROGRAM-ID", strlen("PROGRAM-ID")) == 0) 
            {
                pTok += strlen("PROGRAM-ID=");
                programID = atoi(pTok);                
            }
            else if(strncmp(pTok, "BANDWIDTH", strlen("BANDWIDTH")) == 0) 
            {
                pTok += strlen("BANDWIDTH=");
                bitrate = atoi(pTok);                
            }
            else if(strncmp(pTok, "RESOLUTION", strlen("RESOLUTION")) == 0) 
            {
                pTok += strlen("RESOLUTION=");
                // get values out of "WIDTHxHEIGHT"
                for(pIndex = pTok; pIndex < (pTok+strlen(pTok)); pIndex++) 
                {
                    /* Locate 'x' separator */
                    if(*pIndex == 'x' || *pIndex == 'X') 
                    {
                        *pIndex = '\0';
                        width = atoi(pTok);

                        pIndex++;
                        height = atoi(pIndex);
                    }
                }
            }
            else if(strncmp(pTok, "CODECS", strlen("CODECS")) == 0) 
            {
                /* the attribute will be a quoted-string -- we want
                   to strip off the " characters */

                pTok += strlen("CODECS=");
                pTok++; // Jump over leading " character
                
                codecs = malloc(strlen(pTok)); // Don't need to copy trailing "
                if(codecs == NULL) 
                {
                    ERROR("malloc error");
                    rval = HLS_MEMORY_ERROR;
                    break;
                }
                memset(codecs, 0, strlen(pTok));
                strncpy(codecs, pTok, strlen(pTok)-1); // Ignore trailing " character
            }
            else if(strncmp(pTok, "AUDIO", strlen("AUDIO")) == 0) 
            {
                /* the attribute will be a quoted-string -- we want
                   to strip off the " characters */

                pTok += strlen("AUDIO=");
                pTok++; // Jump over leading " character
                
                audio = malloc(strlen(pTok)); // Don't need to copy trailing "
                if(audio == NULL) 
                {
                    ERROR("malloc error");
                    rval = HLS_MEMORY_ERROR;
                    break;
                }
                memset(audio, 0, strlen(pTok));
                strncpy(audio, pTok, strlen(pTok)-1); // Ignore trailing " character
                DEBUG(DBG_INFO,"audio = %s", audio);
            }
            else if(strncmp(pTok, "VIDEO", strlen("VIDEO")) == 0) 
            {
                /* the attribute will be a quoted-string -- we want
                   to strip off the " characters */

                pTok += strlen("VIDEO=");
                pTok++; // Jump over leading " character
                
                video = malloc(strlen(pTok)); // Don't need to copy trailing "
                if(video == NULL) 
                {
                    ERROR("malloc error");
                    rval = HLS_MEMORY_ERROR;
                    break;
                }
                memset(video, 0, strlen(pTok));
                strncpy(video, pTok, strlen(pTok)-1); // Ignore trailing " character
                DEBUG(DBG_INFO,"video = %s", video);
            }
            
            pTok = strtok(NULL, ",");
        }
        if(rval) 
        {
            break;
        }

        if(bitrate == 0) 
        {
            ERROR("no valid bandwidth parameter in EXT-X-STREAM-INF tag");
            rval = HLS_ERROR;
            break;
        }

        DEBUG(DBG_NOISE,"program ID = %d", programID);
        DEBUG(DBG_NOISE,"bitrate = %d", bitrate);
        DEBUG(DBG_NOISE,"resolution = %d x %d", width, height);
        DEBUG(DBG_NOISE,"codecs = %s", codecs);
        DEBUG(DBG_NOISE,"audio = %s", audio);
        DEBUG(DBG_NOISE,"video = %s", video);
        
        /* Look for a program node with matching program ID */
        pProgramNode = pProgramList->pHead;
        while(pProgramNode != NULL) 
        {
            pProgram = (hlsProgram_t*)(pProgramNode->pData);
            if(pProgram != NULL) 
            {
                if(pProgram->programID == programID) 
                {
                    DEBUG(DBG_NOISE,"found program node with programID %d", programID);
                    break;
                }
                else
                {
                    pProgram = NULL;
                    pProgramNode = pProgramNode->pNext;
                }
            }
            else
            {
                /* We have a node but empty data -- shouldn't happen... */
                ERROR("empty data in program node");
                rval = HLS_ERROR;
            }
        }
        if(rval) 
        {
            break;
        }

        /* If matching program node wasn't found, allocate a new one */
        if(pProgramNode == NULL) 
        {
            DEBUG(DBG_NOISE,"program node with ID %d not found", programID);

            pProgram = newHlsProgram();
            if(pProgram == NULL) 
            {
                ERROR("newHlsProgram() failed");
                rval = HLS_MEMORY_ERROR;
                break;
            }

            /* Set program ID of new node */
            pProgram->programID = programID;

            /* Insert new node at end of list */
            llerror = insertTail(pProgramList, pProgram);
            if(llerror != LL_OK) 
            {
                ERROR("problem adding program node");
                /* Clean up before quitting */
                freeProgram(pProgram);
                pProgram = NULL;
                rval = HLS_ERROR;
                break;
            }

            /* Save reference to parent node */
            pProgram->pParentNode = pProgramList->pTail;
        }

        /* Add new stream list if needed */
        if(pProgram->pStreams == NULL) 
        {
            pProgram->pStreams = newLinkedList();
            if(pProgram->pStreams == NULL) 
            {
                ERROR("problem allocating stream list");
                rval = HLS_ERROR;
                break;
            }            
        }

        /* Create new stream playlist structure */
        pStreamPL = newHlsMediaPlaylist();
        if(pStreamPL == NULL) 
        {
            ERROR("newHlsMediaPlaylist() failed");
            rval = HLS_MEMORY_ERROR;
            break;
        }

        pStreamPL->pMediaData->bitrate = bitrate;
        pStreamPL->pMediaData->width = width;
        pStreamPL->pMediaData->height = height;

        /* Copy codecs into our structure */
        pStreamPL->pMediaData->codecs = codecs;
        /* Release the local reference to codecs */
        codecs = NULL;

        /* Copy audio into our structure */
        pStreamPL->pMediaData->audio = audio;
        /* Release the local reference to audio */
        audio = NULL;
        
        /* Copy video into our structure */
        pStreamPL->pMediaData->video = video;
        /* Release the local reference to video */
        video = NULL;

        /* Copy URL into our structure */
        pStreamPL->playlistURL = (char*)malloc(strlen(urlLine)+1);
        if(pStreamPL->playlistURL == NULL) 
        {
            ERROR("malloc error");
            rval = HLS_MEMORY_ERROR;
            /* Clean up before quitting */
            freePlaylist(pStreamPL);
            pStreamPL = NULL;
            break;
        }
        memset(pStreamPL->playlistURL, 0, strlen(urlLine)+1);
        strcpy(pStreamPL->playlistURL, urlLine);

        // TODO: Do we want to sort the streams by bitrate?

        /* Insert new node at end of list */
        llerror = insertTail(pProgram->pStreams, pStreamPL);
        if(llerror != LL_OK) 
        {
            ERROR("problem adding stream playlist node");
            /* Clean up before quitting */
            freePlaylist(pStreamPL);
            pStreamPL = NULL;
            rval = HLS_ERROR;
            break;
        }

        /* Save reference to parent node */
        pStreamPL->pParentNode = pProgram->pStreams->pTail;

        /* Add new bitrate to program node's list of available ones */
        pProgram->pAvailableBitrates = realloc(pProgram->pAvailableBitrates,
                                                (pProgram->pStreams->numElements)*sizeof(*(pProgram->pAvailableBitrates)));
        if(pProgram->pAvailableBitrates == NULL) 
        {
            ERROR("malloc error");
            rval = HLS_MEMORY_ERROR;
            break;
        }
        pProgram->pAvailableBitrates[(pProgram->pStreams->numElements)-1] = bitrate;

        /* Sort the new bitrate into the list (order is lowest to highest) */
        for(i = pProgram->pStreams->numElements-1; i > 0; i--)
        {
            if(pProgram->pAvailableBitrates[i] < pProgram->pAvailableBitrates[i-1])
            {
                sortTemp = pProgram->pAvailableBitrates[i];
                pProgram->pAvailableBitrates[i] = pProgram->pAvailableBitrates[i-1];
                pProgram->pAvailableBitrates[i-1] = sortTemp;
            }
            else
            {
                break;
            }
        }

    } while (0);

    /* Clean up if we errored */
    if(rval != HLS_OK) 
    {
        free(codecs);
        free(audio);
        free(video);
    }

    return rval;    
}

/** 
 * 
 * 
 * @param tagLine
 * @param urlLine
 * @param pSegmentList
 * 
 * @return #hlsStatus_t
 */
static hlsStatus_t m3u8ParseInf(char *tagLine, char* urlLine, llist_t* pSegmentList)
{
    hlsStatus_t rval = HLS_OK;
    llStatus_t llerror = LL_OK;
    double duration = 0;
    char* name = NULL;

    hlsSegment_t* pSegment = NULL;

    char* pTemp = NULL;
    char* pTok = NULL;

    if((tagLine == NULL) || (urlLine == NULL) || (pSegmentList == NULL))
    {
        ERROR("invalid parameter");
        return HLS_INVALID_PARAMETER;
    }

    DEBUG(DBG_NOISE,"parsing: %s", tagLine);

    do
    {
        /* Get to the start of the attribute list */
        pTemp = tagLine + strlen("#EXTINF:");

        /* Tokenize and parse attributes */
        pTok = strtok(pTemp, ",");

        if(pTok != NULL) 
        {
            /* Remove leading whitespace */
            while(pTok[0] == ' ') 
            {
                pTok++;
            }
    
            /* First token is the duration */
            duration = atof(pTok);
        }
        else
        {
            ERROR("no duration info in #EXTINF tag");
            rval = HLS_ERROR;
            break;
        }

        /* Get next token (name) */
        pTok = strtok(NULL, ",");

        if(pTok != NULL) 
        {
            /* Remove leading whitespace */
            while(pTok[0] == ' ') 
            {
                pTok++;
            }
    
            /* Second token is optional name */    
            name = malloc(strlen(pTok)+1);
            if(name == NULL) 
            {
                ERROR("malloc error");
                rval = HLS_MEMORY_ERROR;
                break;
            }
            memset(name, 0, strlen(pTok)+1);
            strcpy(name, pTok);
        }

        DEBUG(DBG_NOISE,"duration = %f", duration);
        DEBUG(DBG_NOISE,"name = %s", name);

        /* Create new segment structure */
        pSegment = newHlsSegment();
        if(pSegment == NULL) 
        {
            ERROR("newHlsSegment() failed");
            rval = HLS_MEMORY_ERROR;
            break;
        }

        /* Fill stream information */
        pSegment->duration = duration;

        /* Copy segment name into our structure */
        pSegment->programName = name;
        /* Release local reference to name */
        name = NULL;

        /* Copy URL into our structure */
        pSegment->URL = (char*)malloc(strlen(urlLine)+1);
        if(pSegment->URL == NULL) 
        {
            ERROR("malloc error");
            /* Clean up before quitting */
            freeSegment(pSegment);
            pSegment = NULL;
            rval = HLS_MEMORY_ERROR;
            break;
        }
        memset(pSegment->URL, 0, strlen(urlLine)+1);
        strcpy(pSegment->URL, urlLine);

        /* Insert new node at end of list */
        llerror = insertTail(pSegmentList, pSegment);
        if(llerror != LL_OK) 
        {
            ERROR("problem adding stream playlist node");
            /* Clean up before quitting */
            freeSegment(pSegment);
            pSegment = NULL;
            rval = HLS_ERROR;
            break;
        }

        /* Save reference to parent node */
        pSegment->pParentNode = pSegmentList->pTail;

    }while (0);

    /* Clean up if we errored */
    if(rval != HLS_OK) 
    {
        free(name);
    }

    return rval;
}

/** 
 * 
 * 
 * @param tagLine
 * @param pSegment
 * 
 * @return #hlsStatus_t
 */
static hlsStatus_t m3u8ParseDateTime(char* tagLine, hlsSegment_t* pSegment)
{
    hlsStatus_t rval = HLS_OK;
    char* pTemp = NULL;

    if((tagLine == NULL) || (pSegment == NULL))
    {
        ERROR("invalid parameter");
        return HLS_INVALID_PARAMETER;
    }

    DEBUG(DBG_NOISE,"parsing: %s", tagLine);

    do
    {
        /* Get to the start of the attribute list */
        pTemp = tagLine + strlen("#EXT-X-PROGRAM-DATE-TIME:");

        /* Allocate new struct tm to hold our date/time information. */
        pSegment->pProgramTime = malloc(sizeof(struct tm));
        if(pSegment->pProgramTime == NULL) 
        {
            ERROR("malloc error");
            rval = HLS_MEMORY_ERROR;
            break;
        }
		memset(pSegment->pProgramTime, 0, sizeof(struct tm));

        /* Initialize to invalid values */
        pSegment->pProgramTime->tm_hour = -1;
        pSegment->pProgramTime->tm_isdst = -1;
        pSegment->pProgramTime->tm_mday = -1;
        pSegment->pProgramTime->tm_min = -1;
        pSegment->pProgramTime->tm_mon = -1;
        pSegment->pProgramTime->tm_sec = -1;
        pSegment->pProgramTime->tm_wday = -1;
        pSegment->pProgramTime->tm_yday = -1;
        pSegment->pProgramTime->tm_year = -1;

        DEBUG(DBG_NOISE, "parsing date/time from: %s", pTemp);

        /*  See en.wikipedia.org/wiki/ISO_8601 for info on the date/time format
         
            There are countless combinations of date/time specified on in the spec,
            but we only support:
         
            YYYY-MM-DDTHH:MM:SS
         
            There is currently no handling for the timezone...
         
            */

        /* Parse the date/time string and store the values in a struct tm */
        pTemp = strptime(pTemp, "%FT%T", pSegment->pProgramTime);
        if(pTemp == NULL) 
        {
            DEBUG(DBG_WARN, "date/time in unsupported format (should be YYYY-MM-DDTHH:MM:SS)");

            /* We don't actually use this tag for anything (yet) and we don't
               want to kill playback if the tag doesn't match our parser
               (but is still ISO 8601 compliant), so just free the memory we
               allocated and exit. */

            free(pSegment->pProgramTime);
            pSegment->pProgramTime = NULL;
            
            break;
        }

        DEBUG(DBG_NOISE, "not parsed: %s", pTemp);

        DEBUG(DBG_NOISE, "tm_hour: %d", pSegment->pProgramTime->tm_hour);
        DEBUG(DBG_NOISE, "tm_isdst: %d", pSegment->pProgramTime->tm_isdst);
        DEBUG(DBG_NOISE, "tm_mday: %d", pSegment->pProgramTime->tm_mday);
        DEBUG(DBG_NOISE, "tm_min: %d", pSegment->pProgramTime->tm_min);
        DEBUG(DBG_NOISE, "tm_mon: %d", pSegment->pProgramTime->tm_mon);
        DEBUG(DBG_NOISE, "tm_sec: %d", pSegment->pProgramTime->tm_sec);
        DEBUG(DBG_NOISE, "tm_wday: %d", pSegment->pProgramTime->tm_wday);
        DEBUG(DBG_NOISE, "tm_yday: %d", pSegment->pProgramTime->tm_yday);
        DEBUG(DBG_NOISE, "tm_year: %d", pSegment->pProgramTime->tm_year);

    } while (0);

    return rval;
}

/**
 * *pIV and *pKeyURI MUST be NULL, as they will be allocated by 
 *  this function.
 * 
 * @param tagLine
 * @param pEncType
 * @param pIV
 * @param pKeyURI 
 *  
 * @return #hlsStatus_t
 */
static hlsStatus_t m3u8ParseKey(char* tagLine, srcEncType_t* pEncType, char** pIV, char** pKeyURI)
{
    hlsStatus_t rval = HLS_OK;
    char* pTemp = NULL;
    char* pTok = NULL;

    if((tagLine == NULL) || (pEncType == NULL) || (pIV == NULL) || 
       (pKeyURI == NULL) || (*pIV != NULL) || (*pKeyURI != NULL))
    {
        ERROR("invalid parameter");
        return HLS_INVALID_PARAMETER;
    }

    DEBUG(DBG_NOISE,"parsing: %s", tagLine);

    do
    {
        /* Get to the start of the attribute list */
        if(strncmp(tagLine, "#EXT-X-CISCO-KEY:", strlen("#EXT-X-CISCO-KEY:")) == 0) 
        {
            pTemp = tagLine + strlen("#EXT-X-CISCO-KEY:");
        }
        else
        {
            pTemp = tagLine + strlen("#EXT-X-KEY:");
        }

        *pEncType = SRC_ENC_NONE;

        /* Tokenize and parse attributes */
        pTok = strtok(pTemp, ",");
        while(pTok != NULL) 
        {
            /* Because attributes can include whitespace, we don't want to
               tokenzie using the ' ' character.  Because of this, we have
               to remove any leading whitespace following a ',' in the
               attribute list */
            while(pTok[0] == ' ') 
            {
                pTok++;
            }
            DEBUG(DBG_NOISE,"got token \"%s\"", pTok);

            if(strncmp(pTok, "METHOD", strlen("METHOD")) == 0) 
            {
                pTok += strlen("METHOD=");
                if(strncmp(pTok, "AES-128-CTR", strlen("AES-128-CTR")) == 0) 
                {
                    *pEncType = SRC_ENC_AES128_CTR;
                }
                else if(strncmp(pTok, "AES-128", strlen("AES-128")) == 0) 
                {
                    *pEncType = SRC_ENC_AES128_CBC;
                }
            }
            else if(strncmp(pTok, "URI", strlen("URI")) == 0) 
            {
                /* the attribute will be a quoted-string -- we want
                   to strip off the " characters */

                pTok += strlen("URI=");
                pTok++; // Jump over leading " character

                *pKeyURI = (char*)malloc(strlen(pTok));
                if(*pKeyURI == NULL) 
                {
                    ERROR("malloc error");
                    rval = HLS_MEMORY_ERROR;
                    break;
                }
                memset(*pKeyURI, 0, strlen(pTok));
                /* Don't want to copy the trailing " character */
                strncpy(*pKeyURI, pTok, strlen(pTok)-1);
            }
            else if(strncmp(pTok, "IV", strlen("IV")) == 0) 
            {
                /* the attribute will be a hexadecimal integer
                   prefixed with "0x" */

                pTok += strlen("IV=0x");

                *pIV = (char*)malloc(strlen(pTok)+1);
                if(*pIV == NULL) 
                {
                    ERROR("malloc error");
                    rval = HLS_MEMORY_ERROR;
                    break;
                }
                memset(*pIV, 0, strlen(pTok)+1);
                strcpy(*pIV, pTok);
            }
            
            pTok = strtok(NULL, ",");
        }
        if(rval) 
        {
            break;
        }
                    
    } while (0);
    
    return rval;
}

/** 
 * 
 * 
 * @param tagLine
 * @param pProgramList
 * 
 * @return #hlsStatus_t
 */
static hlsStatus_t m3u8ParseMedia(char *tagLine, char* baseURL, llist_t* pGroupList)
{
   hlsStatus_t rval = HLS_OK;
   llStatus_t llerror = LL_OK;

   int i = 0;
   int sortTemp = 0;

   char* uri = NULL;
   hlsMediaType_t type = HLS_MEDIA_TYPE_INVALID;
   char* groupID = NULL;
   char* language = NULL;
   char* name = NULL;
   int def = HLS_NO;
   int autoSelect = HLS_NO;

   llNode_t* pGroupNode = NULL;
   hlsGroup_t* pGroup = NULL;
   hlsPlaylist_t* pStreamPL = NULL;

   char* pTemp = NULL;
   char* pTok = NULL;
   char* pIndex = NULL;

   if((tagLine == NULL) || (baseURL == NULL) || (pGroupList == NULL))
   {
      ERROR("invalid parameter");
      return HLS_INVALID_PARAMETER;
   }

   DEBUG(DBG_NOISE,"parsing: %s", tagLine);

   do
   {
      /* Get to the start of the attribute list */
      pTemp = tagLine + strlen("#EXT-X-MEDIA:");

      /* Tokenize and parse attributes */
      pTok = strtok(pTemp, ",");
      while(pTok != NULL) 
      {

         /* Because attributes can include whitespace, we don't want to
            tokenzie using the ' ' character.  Because of this, we have
            to remove any leading whitespace following a ',' in the
            attribute list */
         while(pTok[0] == ' ') 
         {
            pTok++;
         }
         DEBUG(DBG_NOISE,"got token \"%s\"", pTok);

         if(strncmp(pTok, "URI", strlen("URI")) == 0) 
         {
            /* the attribute will be a quoted-string -- we want
               to strip off the " characters */

            pTok += strlen("URI=");
            pTok++; // Jump over leading " character

            uri = malloc(strlen(pTok)); // Don't need to copy trailing "
            if(uri == NULL) 
            {
               ERROR("malloc error");
               rval = HLS_MEMORY_ERROR;
               break;
            }
            memset(uri, 0, strlen(pTok));
            strncpy(uri, pTok, strlen(pTok)-1); // Ignore trailing " character
            
            /* Prepend the baseURL, if necessary */
            rval = createFullURL(&uri, baseURL);
            if(rval != HLS_OK) 
            {
               ERROR("error creating full URL");
               break;
            }
         }
         else if(strncmp(pTok, "TYPE", strlen("TYPE")) == 0) 
         {
            pTok += strlen("TYPE=");
            if(!strncmp(pTok, "AUDIO", strlen(pTok)))
            {
               type = HLS_MEDIA_TYPE_AUDIO;
            }
            else if(!strncmp(pTok, "VIDEO", strlen(pTok)))
            {
               type = HLS_MEDIA_TYPE_VIDEO;
            }
            else
            {
               ERROR("Type attribute of EXT-X-MEDIA has invalid value: %s", pTok);
               rval = HLS_ERROR;
               break;
            }
         }
         else if(strncmp(pTok, "GROUP-ID", strlen("GROUP-ID")) == 0) 
         {
            /* the attribute will be a quoted-string -- we want
               to strip off the " characters */

            pTok += strlen("GROUP-ID=");
            pTok++; // Jump over leading " character

            groupID = malloc(strlen(pTok)); // Don't need to copy trailing "
            if(groupID == NULL) 
            {
               ERROR("malloc error");
               rval = HLS_MEMORY_ERROR;
               break;
            }
            memset(groupID, 0, strlen(pTok));
            strncpy(groupID, pTok, strlen(pTok)-1); // Ignore trailing " character
         }
         else if(strncmp(pTok, "LANGUAGE", strlen("LANGUAGE")) == 0) 
         {
            /* the attribute will be a quoted-string -- we want
               to strip off the " characters */

            pTok += strlen("LANGUAGE=");
            pTok++; // Jump over leading " character

            language = malloc(strlen(pTok)); // Don't need to copy trailing "
            if(language == NULL) 
            {
               ERROR("malloc error");
               rval = HLS_MEMORY_ERROR;
               break;
            }
            memset(language, 0, strlen(pTok));
            strncpy(language, pTok, strlen(pTok)-1); // Ignore trailing " character
         }
         else if(strncmp(pTok, "NAME", strlen("NAME")) == 0) 
         {
            /* the attribute will be a quoted-string -- we want
               to strip off the " characters */

            pTok += strlen("NAME=");
            pTok++; // Jump over leading " character

            name = malloc(strlen(pTok)); // Don't need to copy trailing "
            if(name == NULL) 
            {
               ERROR("malloc error");
               rval = HLS_MEMORY_ERROR;
               break;
            }
            memset(name, 0, strlen(pTok));
            strncpy(name, pTok, strlen(pTok)-1); // Ignore trailing " character
         }
         else if(strncmp(pTok, "DEFAULT", strlen("DEFAULT")) == 0) 
         {
            pTok += strlen("DEFAULT=");
            if(!strncmp(pTok, "YES", strlen(pTok)))
            {
               def = HLS_YES;
            }
            else if(!strncmp(pTok, "NO", strlen(pTok)))
            {
               def = HLS_NO;
            }
            else
            {
               ERROR("Default attribute of EXT-X-MEDIA has invalid value: %s", pTok);
               rval = HLS_ERROR;
               break;
            }
         }
         else if(strncmp(pTok, "AUTOSELECT", strlen("AUTOSELECT")) == 0) 
         {
            pTok += strlen("AUTOSELECT=");
            if(!strncmp(pTok, "YES", strlen(pTok)))
            {
               autoSelect = HLS_YES;
            }
            else if(!strncmp(pTok, "NO", strlen(pTok)))
            {
               autoSelect = HLS_NO;
            }
            else
            {
               ERROR("Autoselect attribute of EXT-X-MEDIA has invalid value: %s", pTok);
               rval = HLS_ERROR;
               break;
            }
         }

         pTok = strtok(NULL, ",");
      }
      if(rval) 
      {
         break;
      }

      DEBUG(DBG_INFO,"uri = %s", uri);
      DEBUG(DBG_INFO,"type = %d", type);
      DEBUG(DBG_INFO,"group ID = %s", groupID);
      DEBUG(DBG_INFO,"language = %s", language);
      DEBUG(DBG_INFO,"name = %s", name);
      DEBUG(DBG_INFO,"default = %s", (def == HLS_YES)?"YES":"NO");
      DEBUG(DBG_INFO,"autoselect = %s", (autoSelect == HLS_YES)?"YES":"NO");

      /* Allocate a new group */ 
      pGroup = newHlsGroup();
      if(pGroup == NULL) 
      {
         ERROR("newHlsGroup() failed");
         rval = HLS_MEMORY_ERROR;
         break;
      }

      /* Insert new node at end of list */
      llerror = insertTail(pGroupList, pGroup);
      if(llerror != LL_OK) 
      {
         ERROR("problem adding Group node");
         /* Clean up before quitting */
         freeGroup(pGroup);
         pGroup = NULL;
         rval = HLS_ERROR;
         break;
      }
      
      /* Save reference to parent node */
      pGroup->pParentNode = pGroupList->pTail;
      
      /* Set Group ID of new node */
      pGroup->groupID = groupID;
      groupID = NULL;

      if(NULL != uri)
      {
         /* Create new stream playlist structure */
         pStreamPL = newHlsMediaPlaylist();
         if(pStreamPL == NULL) 
         {
            ERROR("newHlsMediaPlaylist() failed");
            rval = HLS_MEMORY_ERROR;
            break;
         }

         /* Copy URL into our structure */
         pStreamPL->playlistURL = uri;
         /* Release the local reference to uri */
         uri = NULL;

         pGroup->pPlaylist = pStreamPL;
      }
      
      pGroup->type = type;

      /* Copy language into our structure */
      pGroup->language = language;
      /* Release the local reference to codecs */
      language = NULL;

      /* Copy name into our structure */
      pGroup->name = name;
      /* Release the local reference to codecs */
      name = NULL;

      pGroup->def = def;
      pGroup->autoSelect = autoSelect;

   } while (0);

   /* Clean up if we errored */
   if(rval != HLS_OK) 
   {
      free(uri);
      free(language);
      free(name);
      free(groupID);
   }

   return rval;    
}

/** 
 * 
 * 
 * @param pSegment
 * @param encType
 * @param iv
 * @param keyURI
 * 
 * @return #hlsStatus_t
 */
static hlsStatus_t addSegmentEncInfo(hlsSegment_t* pSegment, srcEncType_t encType, char* iv, char* keyURI)
{
    hlsStatus_t rval = HLS_OK;

    if(pSegment == NULL)
    {
        ERROR("invalid parameter");
        return HLS_INVALID_PARAMETER;
    }

    do
    {
        /* If we are supposed to be encrypted we MUST have a key URI */
        if((encType != SRC_ENC_NONE) && (keyURI == NULL)) 
        {
            ERROR("invalid #EXT-X-KEY or #EXT-X-CISCO-KEY tag");
            rval = HLS_ERROR;
            break;
        }

        /* Set segment encryption type */
        pSegment->encType = encType;

        /* Clear out any old values */
        //RMS TODO fix.
        free(pSegment->keyURI);
        pSegment->keyURI = NULL;

        /* Write new key and IV values */
        if(encType != SRC_ENC_NONE) 
        {
            if(keyURI != NULL) 
            {
                pSegment->keyURI = (char*)malloc(strlen(keyURI)+1);
                if(pSegment->keyURI == NULL) 
                {
                    ERROR("malloc error");
                    rval = HLS_MEMORY_ERROR;
                    break;
                }
                memset(pSegment->keyURI, 0, strlen(keyURI)+1);
                strcpy(pSegment->keyURI, keyURI);
                // Download and get the key

#ifdef ENABLE_KEY_RETRIEVAL
                dwnld_parse_keyURI(pSegment->key, keyURI);
#endif
            }
    
            if(iv != NULL) 
            {
                strToHex(iv,pSegment->iv, 16);
            }
    
            /* If we are encrypted but no IV is specified,
               we need to use the sequence number */
            if((encType != SRC_ENC_NONE) && (iv == NULL))
            {
               memset(pSegment->iv, 0, 16);
            }
    
            DEBUG(DBG_INFO,"encType = %s", (pSegment->encType == SRC_ENC_AES128_CBC ? "AES-128-CBC" : 
                                             (pSegment->encType == SRC_ENC_AES128_CTR ? "AES-128-CTR" : "NONE")));
            DEBUG(DBG_INFO,"keyURI = %s", pSegment->keyURI);
        }
                    
    } while (0);

    return rval;
}

/** 
 * Increments IV provided in *pIV by 2^64
 * 
 * @param pIV
 * 
 * @return #hlsStatus_t
 */
static hlsStatus_t incCtrIv(char ** pIV)
{
    hlsStatus_t rval = HLS_OK;

    char* tempIV = NULL;
    unsigned long long ivTop;

    if((pIV == NULL) && (*pIV == NULL))
    {
        ERROR("invalid parameter");
        return HLS_INVALID_PARAMETER;
    }

    do
    {
        /* Need to modify top 64 bits only */
        tempIV = malloc(17);
        if(tempIV == NULL) 
        {
            ERROR("malloc error");
            rval = HLS_MEMORY_ERROR;
            break;
        }
        memset(tempIV, 0, 17);

        /* Copy top 64 bits */
        memcpy(tempIV, *pIV, 16);

        /* Convert from string to long long */
        ivTop = strtoll(tempIV, NULL, 16);

        /* Increment by the offset from the KEY tag */
        ivTop += 1;

        /* Convert from long long back to string */
        sprintf(tempIV, "%016llx", ivTop);

        /* Write new value back to iv */
        memcpy(*pIV, tempIV, 16);

        DEBUG(DBG_INFO, "new AES-128-CTR iv: %s", *pIV);                    

    } while (0);

    free(tempIV);
    tempIV = NULL;

    return rval;
}

/** 
 * Decrements IV provided in *pIV by 2^64
 * 
 * @param pIV
 * 
 * @return #hlsStatus_t
 */
static hlsStatus_t decCtrIv(char ** pIV)
{
    hlsStatus_t rval = HLS_OK;

    char* tempIV = NULL;
    unsigned long long ivTop;

    if((pIV == NULL) && (*pIV == NULL))
    {
        ERROR("invalid parameter");
        return HLS_INVALID_PARAMETER;
    }

    do
    {
        /* Need to modify top 64 bits only */
        tempIV = malloc(17);
        if(tempIV == NULL) 
        {
            ERROR("malloc error");
            rval = HLS_MEMORY_ERROR;
            break;
        }
        memset(tempIV, 0, 17);

        /* Copy top 64 bits */
        memcpy(tempIV, *pIV, 16);

        /* Convert from string to long long */
        ivTop = strtoll(tempIV, NULL, 16);

        /* Increment by the offset from the KEY tag */
        ivTop -= 1;

        /* Convert from long long back to string */
        sprintf(tempIV, "%016llx", ivTop);

        /* Write new value back to iv */
        memcpy(*pIV, tempIV, 16);

        DEBUG(DBG_INFO, "new AES-128-CTR iv: %s", *pIV);                    

    } while (0);

    free(tempIV);
    tempIV = NULL;

    return rval;
}

/**
 * 
 * 
 * @param tagLine
 * @param pSegment
 * @param lastSegmentEnd
 * 
 * @return #hlsStatus_t
 */
static hlsStatus_t m3u8ParseByteRange(char* tagLine, hlsSegment_t* pSegment, long* pNextSegmentOffset)
{
    hlsStatus_t rval = HLS_OK;

    char* pTemp = NULL;
    char* pTok = NULL;

    long byteLength;
    long byteOffset;

    if((tagLine == NULL) || (pSegment == NULL) || (pNextSegmentOffset == NULL))
    {
        ERROR("invalid parameter");
        return HLS_INVALID_PARAMETER;
    }

    DEBUG(DBG_NOISE,"parsing: %s", tagLine);

    do
    {
        /* Get to the start of the attribute list */
        pTemp = tagLine + strlen("#EXT-X-BYTERANGE:");

        /* Tokenize and parse attributes */
        pTok = strtok(pTemp, "@");

        if(pTok != NULL) 
        {
            /* Remove leading whitespace */
            while(pTok[0] == ' ') 
            {
                pTok++;
            }
    
            /* First token is the byte length */
            byteLength = atoi(pTok);
        }
        else
        {
            ERROR("no byte length info in #EXT-X-BYTERANGE tag");
            rval = HLS_ERROR;
            break;
        }

        /* Get next token (byte offset) */
        pTok = strtok(NULL, "@");

        if(pTok != NULL) 
        {
            /* Remove leading whitespace */
            while(pTok[0] == ' ') 
            {
                pTok++;
            }
    
            /* Second token is optional byte offset */    
            byteOffset = atoi(pTok);
        }
        else
        {
            /* If offset not specified, it is taken to be
               the end of the previous byte range */
            byteOffset = *pNextSegmentOffset;
        }

        DEBUG(DBG_NOISE,"byteLength = %ld", byteLength);
        DEBUG(DBG_NOISE,"offset = %ld", byteOffset);

        /* Fill byte range information */
        pSegment->byteLength = byteLength;
        pSegment->byteOffset = byteOffset;

        /* Calculate start of next segment */
        *pNextSegmentOffset = byteOffset + byteLength;

    }while (0);

    return rval;
}

/**
 * 
 * 
 * @param tagLine
 * @param pSession
 * 
 * @return #hlsStatus_t
 */
static hlsStatus_t m3u8ParseProtHeader(char* tagLine, hlsSession_t *pSession)
{
    hlsStatus_t rval = HLS_OK;    
    srcDrmLicenseInfo_t licenseInfo;

    srcPluginEvt_t event;

    char* pTemp = NULL;
    char* pTok = NULL;

    if((tagLine == NULL) || (pSession == NULL))
    {
        ERROR("invalid parameter");
        return HLS_INVALID_PARAMETER;
    }

    DEBUG(DBG_NOISE,"parsing: %s", tagLine);

    do
    {
        memset(&licenseInfo, 0, sizeof(srcDrmLicenseInfo_t));

        /* Initialize to invalid DRM-TYPE */
        licenseInfo.drmType = -1;

        pTemp = tagLine + strlen("#EXT-X-CISCO-PROT-HEADER:");

        /* Tokenize and parse attributes */
        pTok = strtok(pTemp, ",");
        while(pTok != NULL) 
        {

            /* Because attributes can include whitespace, we don't want to
               tokenzie using the ' ' character.  Because of this, we have
               to remove any leading whitespace following a ',' in the
               attribute list */
            while(pTok[0] == ' ') 
            {
                pTok++;
            }
            DEBUG(DBG_NOISE,"got token \"%s\"", pTok);

            if(strncmp(pTok, "PROGRAM-ID", strlen("PROGRAM-ID")) == 0) 
            {
                pTok += strlen("PROGRAM-ID=");
                licenseInfo.programID = atoi(pTok);                
            }
            else if(strncmp(pTok, "DRM-TYPE", strlen("DRM-TYPE")) == 0) 
            {
                pTok += strlen("DRM-TYPE=");

                if(strncmp(pTok, "PLAYREADY", strlen("PLAYREADY")) == 0) 
                {
                    licenseInfo.drmType = SRC_DRM_PLAYREADY;
                }
            }
            else if(strncmp(pTok, "DRM_TYPE", strlen("DRM_TYPE")) == 0) 
            {
                pTok += strlen("DRM_TYPE=");

                if(strncmp(pTok, "PLAYREADY", strlen("PLAYREADY")) == 0) 
                {
                    licenseInfo.drmType = SRC_DRM_PLAYREADY;
                }
            }
            else if(strncmp(pTok, "KID", strlen("KID")) == 0) 
            {
                /* the attribute will be a quoted-string -- we want
                   to strip off the " characters */

                pTok += strlen("KID=");
                pTok++; // Jump over leading " character

                licenseInfo.keyID = (char*)malloc(strlen(pTok));
                if(licenseInfo.keyID == NULL) 
                {
                    ERROR("malloc error");
                    rval = HLS_MEMORY_ERROR;
                    break;
                }
                memset(licenseInfo.keyID, 0, strlen(pTok));
                /* Don't want to copy the trailing " character */
                strncpy(licenseInfo.keyID, pTok, strlen(pTok)-1);
            }
            else if(strncmp(pTok, "DRM", strlen("DRM")) == 0) 
            {
                /* the attribute will be a quoted-string -- we want
                   to strip off the " characters */

                pTok += strlen("DRM=");
                pTok++; // Jump over leading " character

                licenseInfo.drm = (char*)malloc(strlen(pTok));
                if(licenseInfo.drm == NULL) 
                {
                    ERROR("malloc error");
                    rval = HLS_MEMORY_ERROR;
                    break;
                }
                memset(licenseInfo.drm, 0, strlen(pTok));
                /* Don't want to copy the trailing " character */
                strncpy(licenseInfo.drm, pTok, strlen(pTok)-1);
            }
            
            pTok = strtok(NULL, ",");
        }
        if(rval) 
        {
            break;
        }

        /* Check that we got a valid DRM-TYPE */
        if(licenseInfo.drmType == -1) 
        {
            ERROR("invalid or missing DRM-TYPE in tag");
            rval = HLS_ERROR;
            break;
        }

        /* Check that we have a valid KID and DRM blob */
        if((licenseInfo.drmType == SRC_DRM_PLAYREADY) &&
           ((licenseInfo.keyID == NULL) || (licenseInfo.drm == NULL)))
        {
            ERROR("invalid CISCO-PROT-HEADER tag");
            rval = HLS_ERROR;
            break;
        }

        DEBUG(DBG_INFO,"DRM-TYPE: %s", licenseInfo.drmType == SRC_DRM_PLAYREADY ? "PLAYREADY" : "UNKNOWN");
        DEBUG(DBG_INFO,"PROGRAM-ID: %d", licenseInfo.programID);
        DEBUG(DBG_INFO,"KID: %s", licenseInfo.keyID);
        DEBUG(DBG_INFO,"DRM: %s", licenseInfo.drm);

        /* Signal drm license to player */
        event.eventCode = SRC_PLUGIN_DRM_LICENSE;
        event.pData = (srcDrmLicenseInfo_t*)&licenseInfo;
        hlsPlayer_pluginEvtCallback(pSession->pHandle, &event);
            
    } while (0);

    free(licenseInfo.drm);
    licenseInfo.drm = NULL;
    free(licenseInfo.keyID);
    licenseInfo.keyID = NULL;

    return rval;
}

/**
 * 
 * 
 * @param tagLine
 * @param baseURL
 * @param pProgramList
 * 
 * @return #hlsStatus_t
 */
static hlsStatus_t m3u8ParseIFrameStreamInf(char *tagLine, char* baseURL, llist_t* pProgramList)
{
    hlsStatus_t rval = HLS_OK;
    llStatus_t llerror = LL_OK;
    int programID = 0;
    int bitrate = 0;
    int width = 0;
    int height = 0;
    int i = 0;
    int sortTemp = 0;
    char* codecs = NULL;
    char* uri = NULL;

    llNode_t* pProgramNode = NULL;
    hlsProgram_t* pProgram = NULL;
    hlsPlaylist_t* pStreamPL = NULL;
    
    char* pTemp = NULL;
    char* pTok = NULL;
    char* pIndex = NULL;

    if((tagLine == NULL) || (baseURL == NULL) || (pProgramList == NULL))
    {
        ERROR("invalid parameter");
        return HLS_INVALID_PARAMETER;
    }

    DEBUG(DBG_NOISE,"parsing: %s", tagLine);

    do
    {
        /* Get to the start of the attribute list */
        pTemp = tagLine + strlen("#EXT-X-I-FRAME-STREAM-INF:");

        /* Tokenize and parse attributes */
        pTok = strtok(pTemp, ",");
        while(pTok != NULL) 
        {

            /* Because attributes can include whitespace, we don't want to
               tokenzie using the ' ' character.  Because of this, we have
               to remove any leading whitespace following a ',' in the
               attribute list */
            while(pTok[0] == ' ') 
            {
                pTok++;
            }
            DEBUG(DBG_NOISE,"got token \"%s\"", pTok);

            if(strncmp(pTok, "PROGRAM-ID", strlen("PROGRAM-ID")) == 0) 
            {
                pTok += strlen("PROGRAM-ID=");
                programID = atoi(pTok);                
            }
            else if(strncmp(pTok, "BANDWIDTH", strlen("BANDWIDTH")) == 0) 
            {
                pTok += strlen("BANDWIDTH=");
                bitrate = atoi(pTok);                
            }
            else if(strncmp(pTok, "RESOLUTION", strlen("RESOLUTION")) == 0) 
            {
                pTok += strlen("RESOLUTION=");
                // get values out of "WIDTHxHEIGHT"
                for(pIndex = pTok; pIndex < (pTok+strlen(pTok)); pIndex++) 
                {
                    /* Locate 'x' separator */
                    if(*pIndex == 'x' || *pIndex == 'X') 
                    {
                        *pIndex = '\0';
                        width = atoi(pTok);

                        pIndex++;
                        height = atoi(pIndex);
                    }
                }
            }
            else if(strncmp(pTok, "CODECS", strlen("CODECS")) == 0) 
            {
                /* the attribute will be a quoted-string -- we want
                   to strip off the " characters */

                pTok += strlen("CODECS=");
                pTok++; // Jump over leading " character
                
                codecs = malloc(strlen(pTok)); // Don't need to copy trailing "
                if(codecs == NULL) 
                {
                    ERROR("malloc error");
                    rval = HLS_MEMORY_ERROR;
                    break;
                }
                memset(codecs, 0, strlen(pTok));
                strncpy(codecs, pTok, strlen(pTok)-1); // Ignore trailing " character
            }
            else if(strncmp(pTok, "URI", strlen("URI")) == 0) 
            {
                /* the attribute will be a quoted-string -- we want
                   to strip off the " characters */

                pTok += strlen("URI=");
                pTok++; // Jump over leading " character

                uri = malloc(strlen(pTok)); // Don't need to copy trailing "
                if(uri == NULL) 
                {
                    ERROR("malloc error");
                    rval = HLS_MEMORY_ERROR;
                    break;
                }
                memset(uri, 0, strlen(pTok));
                strncpy(uri, pTok, strlen(pTok)-1); // Ignore trailing " character
            }
            
            pTok = strtok(NULL, ",");
        }
        if(rval) 
        {
            break;
        }

        if(bitrate == 0) 
        {
            ERROR("no valid bandwidth parameter in EXT-X-I-FRAME-STREAM-INF tag");
            rval = HLS_ERROR;
            break;
        }

        if(uri == NULL) 
        {
            ERROR("no valid URI parameter in EXT-X-I-FRAME-STREAM tag");
            rval = HLS_ERROR;
            break;
        }

        DEBUG(DBG_NOISE,"program ID = %d", programID);
        DEBUG(DBG_NOISE,"bitrate = %d", bitrate);
        DEBUG(DBG_NOISE,"resolution = %d x %d", width, height);
        DEBUG(DBG_NOISE,"codecs = %s", codecs);
        DEBUG(DBG_NOISE,"uri = %s", uri);

        /* Look for a program node with matching program ID */
        pProgramNode = pProgramList->pHead;
        while(pProgramNode != NULL) 
        {
            pProgram = (hlsProgram_t*)(pProgramNode->pData);
            if(pProgram != NULL) 
            {
                if(pProgram->programID == programID) 
                {
                    DEBUG(DBG_NOISE,"found program node with programID %d", programID);
                    break;
                }
                else
                {
                    pProgram = NULL;
                    pProgramNode = pProgramNode->pNext;
                }
            }
            else
            {
                /* We have a node but empty data -- shouldn't happen... */
                ERROR("empty data in program node");
                rval = HLS_ERROR;
            }
        }
        if(rval) 
        {
            break;
        }

        /* If matching program node wasn't found, allocate a new one */
        if(pProgramNode == NULL) 
        {
            DEBUG(DBG_NOISE,"program node with ID %d not found", programID);

            pProgram = newHlsProgram();
            if(pProgram == NULL) 
            {
                ERROR("newHlsProgram() failed");
                rval = HLS_MEMORY_ERROR;
                break;
            }

            /* Set program ID of new node */
            pProgram->programID = programID;

            /* Insert new node at end of list */
            llerror = insertTail(pProgramList, pProgram);
            if(llerror != LL_OK) 
            {
                ERROR("problem adding program node");
                /* Clean up before quitting */
                freeProgram(pProgram);
                pProgram = NULL;
                rval = HLS_ERROR;
                break;
            }

            /* Save reference to parent node */
            pProgram->pParentNode = pProgramList->pTail;
        }

        /* Add new stream list if needed */
        if(pProgram->pIFrameStreams == NULL) 
        {
            pProgram->pIFrameStreams = newLinkedList();
            if(pProgram->pIFrameStreams == NULL) 
            {
                ERROR("problem allocating stream list");
                rval = HLS_ERROR;
                break;
            }            
        }

        /* Create new stream playlist structure */
        pStreamPL = newHlsMediaPlaylist();
        if(pStreamPL == NULL) 
        {
            ERROR("newHlsMediaPlaylist() failed");
            rval = HLS_MEMORY_ERROR;
            break;
        }

        pStreamPL->pMediaData->bitrate = bitrate;
        pStreamPL->pMediaData->width = width;
        pStreamPL->pMediaData->height = height;

        /* Copy codecs into our structure */
        pStreamPL->pMediaData->codecs = codecs;
        /* Release the local reference to codecs */
        codecs = NULL;

        /* Prepend the baseURL, if necessary */
        rval = createFullURL(&uri, baseURL);
        if(rval != HLS_OK) 
        {
            ERROR("error creating full URL");
            /* Clean up before quitting */
            freePlaylist(pStreamPL);
            pStreamPL = NULL;
            break;
        }

        /* Copy URL into our structure */
        pStreamPL->playlistURL = uri;
        /* Release the local reference to uri */
        uri = NULL;

        // TODO: Do we want to sort the streams by bitrate?

        /* Insert new node at end of list */
        llerror = insertTail(pProgram->pIFrameStreams, pStreamPL);
        if(llerror != LL_OK) 
        {
            ERROR("problem adding stream playlist node");
            /* Clean up before quitting */
            freePlaylist(pStreamPL);
            pStreamPL = NULL;
            rval = HLS_ERROR;
            break;
        }

        /* Save reference to parent node */
        pStreamPL->pParentNode = pProgram->pIFrameStreams->pTail;

        /* Add new bitrate to program node's list of available ones */
        pProgram->pAvailableIFrameBitrates = realloc(pProgram->pAvailableIFrameBitrates, 
                                                     (pProgram->pIFrameStreams->numElements)*sizeof(*(pProgram->pAvailableIFrameBitrates)));
        if(pProgram->pAvailableIFrameBitrates == NULL) 
        {
            ERROR("malloc error");
            rval = HLS_MEMORY_ERROR;
            break;
        }
        pProgram->pAvailableIFrameBitrates[(pProgram->pIFrameStreams->numElements)-1] = bitrate;

        /* Sort the new bitrate into the list (order is lowest to highest) */
        for(i = pProgram->pIFrameStreams->numElements-1; i > 0; i--)
        {
            if(pProgram->pAvailableIFrameBitrates[i] < pProgram->pAvailableIFrameBitrates[i-1])
            {
                sortTemp = pProgram->pAvailableIFrameBitrates[i];
                pProgram->pAvailableIFrameBitrates[i] = pProgram->pAvailableIFrameBitrates[i-1];
                pProgram->pAvailableIFrameBitrates[i-1] = sortTemp;
            }
            else
            {
                break;
            }
        }

    } while (0);

    /* Clean up if we errored */
    if(rval != HLS_OK) 
    {
        free(codecs);
        free(uri);
    }

    return rval;    
}

#ifdef __cplusplus
}
#endif
