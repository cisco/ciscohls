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
 * @file m3u8Parser.c @date February 9, 2012
 *
 * @author Patryk Prus (pprus@cisco.com)
 *
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <unistd.h>
#include <time.h>
#include <errno.h>

#include "hlsSession.h"
#include "hlsSessionUtils.h"
#include "hlsPlayerInterface.h"

#include "m3u8Parser.h"
#include "m3u8ParseUtils.h"

#include "debug.h"

// TODO: put in errors for overly nested playlists...

/* Loop duration in seconds */
#define PARSER_LOOP_SECS 1

/**
 * Thread body responsible for playlist parsing.  Kicked off by
 * hlsSession_prepare().
 *
 * Order of operations:
 *
 * 1) Parse playlist pointed to by pSession->pPlaylist
 *
 * 2)
 *
 * .
 * .
 * .
 *
 * @param pSession - pointer to pre-allocated hlsSession
 *                  structure
 */
void m3u8ParserThread(hlsSession_t* pSession)
{
    hlsStatus_t status = HLS_OK;

    int pthread_status = 0;

    llNode_t* pProgramNode;

    int bitrate = 0;

    struct timespec wakeTime;

    int ii = 0;

    hlsGroup_t *pGroup = NULL;
    srcPlayerSetData_t playerSetData = {};
    srcStatus_t rval = SRC_ERROR;

    if(pSession == NULL)
    {
        ERROR("pSession == NULL");
        pthread_exit(NULL);
    }

    TIMESTAMP(DBG_INFO, "Starting %s", __FUNCTION__);

    do
    {
        /* Get playlist WRITE lock */
        pthread_rwlock_wrlock(&(pSession->playlistRWLock));

        /* Parse top level playlist */
        status = m3u8ParsePlaylist(pSession->pPlaylist, pSession);
        if(status)
        {
            if(status == HLS_CANCELLED)
            {
                DEBUG(DBG_WARN, "parser signalled to stop");
                /* Release playlist lock */
                pthread_rwlock_unlock(&(pSession->playlistRWLock));
                break;
            }
            else
            {
                ERROR("problem parsing playlist");
                /* Release playlist lock */
                pthread_rwlock_unlock(&(pSession->playlistRWLock));
                break;
            }
        }

        /* Set our intial program to the first available, and
           our initial playlist to the one in that program that
           is within the range set and closest to the targetBitrate */
        pSession->pCurrentProgram = NULL;
        pSession->pCurrentPlaylist = NULL;
        for(ii = 0; ii < MAX_NUM_MEDIA_GROUPS; ii++)
        {
           pSession->pCurrentGroup[ii] = NULL;
        }

        if(pSession->pPlaylist == NULL)
        {
            ERROR("NULL playlist");
            status = HLS_ERROR;
            /* Release playlist lock */
            pthread_rwlock_unlock(&(pSession->playlistRWLock));
            break;
        }

        if(pSession->pPlaylist->type == PL_MEDIA)
        {
            /* This is the only playlist */
            pSession->pCurrentPlaylist = pSession->pPlaylist;
        }
        else if(pSession->pPlaylist->type == PL_VARIANT)
        {
            if((pSession->pPlaylist->pList == NULL) ||
               (pSession->pPlaylist->pList->pHead == NULL) ||
               (pSession->pPlaylist->pList->pHead->pData ==NULL))
            {
                ERROR("malformed variant playlist");
                status = HLS_ERROR;
                /* Release playlist lock */
                pthread_rwlock_unlock(&(pSession->playlistRWLock));
                break;
            }

            /* For now, just use the first program we run across */
            pProgramNode = pSession->pPlaylist->pList->pHead;
            pSession->pCurrentProgram = (hlsProgram_t*)(pProgramNode->pData);

            if(pSession->pCurrentProgram->pStreams == NULL)
            {
                ERROR("malformed variant playlist");
                status = HLS_ERROR;
                /* Release playlist lock */
                pthread_rwlock_unlock(&(pSession->playlistRWLock));
                break;
            }

            /* Find the best starting bitrate */
            status = getBestBitrate(pSession, &bitrate);
            if(status != HLS_OK)
            {
                ERROR("problem getting initial bitrate");
                status = HLS_ERROR;
                /* Release playlist lock */
                pthread_rwlock_unlock(&(pSession->playlistRWLock));
                break;
            }

            /* Set our starting playlist */
            status = getPlaylistByBitrate(pSession->pCurrentProgram->pStreams, bitrate,
                                      &(pSession->pCurrentPlaylist));
            if(status != HLS_OK)
            {
                ERROR("problem setting initial playlist");
                status = HLS_ERROR;
                /* Release playlist lock */
                pthread_rwlock_unlock(&(pSession->playlistRWLock));
                break;
            }

            DEBUG(DBG_INFO,"Starting bitrate: %d", pSession->pCurrentPlaylist->pMediaData->bitrate);

            /* Parse our current playlist */
            status = m3u8ParsePlaylist(pSession->pCurrentPlaylist, pSession);
            if(status != HLS_OK)
            {
                if(status == HLS_CANCELLED)
                {
                    DEBUG(DBG_WARN, "parser signalled to stop");
                    /* Release playlist lock */
                    pthread_rwlock_unlock(&(pSession->playlistRWLock));
                    break;
                }
                else
                {
                    ERROR("problem parsing playlist");
                    /* Release playlist lock */
                    pthread_rwlock_unlock(&(pSession->playlistRWLock));
                    break;
                }
            }

            if(NULL != pSession->pCurrentPlaylist->pMediaData->audio)
            {
               DEBUG(DBG_INFO, "Current program has audio: %s",
                     pSession->pCurrentPlaylist->pMediaData->audio);

               status = findAudioGroup(pSession, &pGroup);
               if((HLS_OK != status) || (NULL == pGroup))
               {
                  DEBUG(DBG_WARN, "Failed to find audio group: %s",
                        pSession->pCurrentPlaylist->pMediaData->audio);
                  /* Release playlist lock */
                  pthread_rwlock_unlock(&(pSession->playlistRWLock));
                  break;
               }

               strncpy(pSession->audioLanguageISOCode, pGroup->language,
                       ISO_LANG_CODE_LEN);
               pSession->audioLanguageISOCode[ISO_LANG_CODE_LEN] = '\0';

               if(NULL != pGroup->pPlaylist)
               {
                  pSession->pCurrentGroup[pSession->currentGroupCount++] = pGroup;

                  playerSetData.setCode = SRC_PLAYER_SET_DISABLE_MAIN_STREAM_AUDIO;
                  rval = hlsPlayer_set(pSession->pHandle, &playerSetData);
                  if(rval != SRC_SUCCESS)
                  {
                     ERROR("failed to set SRC_PLAYER_SET_DISABLE_MAIN_STREAM_AUDIO");
                     status = HLS_ERROR;
                     break;
                  }
               }
            }

            DEBUG(DBG_INFO, "currentGroupCount = %u", pSession->currentGroupCount);

            if(pSession->currentGroupCount > MAX_NUM_MEDIA_GROUPS)
            {
               ERROR("currentGroupCount(%u) > MAX_NUM_MEDIA_GROUPS(%d)",
                     pSession->currentGroupCount, MAX_NUM_MEDIA_GROUPS);
               /* Release playlist lock */
               pthread_rwlock_unlock(&(pSession->playlistRWLock));
               break;
            }

            for(ii = 0; ii < pSession->currentGroupCount; ii++)
            {
               status = m3u8ParsePlaylist(pSession->pCurrentGroup[ii]->pPlaylist, pSession);
               if(status != HLS_OK)
               {
                  if(status == HLS_CANCELLED)
                  {
                     DEBUG(DBG_WARN, "parser signalled to stop");
                     /* Release playlist lock */
                     pthread_rwlock_unlock(&(pSession->playlistRWLock));
                     break;
                  }
                  else
                  {
                     ERROR("problem parsing alternative playlist");
                     /* Release playlist lock */
                     pthread_rwlock_unlock(&(pSession->playlistRWLock));
                     break;
                  }
               }

               if(pSession->pCurrentGroup[ii]->pPlaylist->type != PL_MEDIA)
               {
                  ERROR("Only media playlist type is supported for media groups");
                  status = HLS_ERROR;
                  /* Release playlist lock */
                  pthread_rwlock_unlock(&(pSession->playlistRWLock));
                  break;
               }
            }
        }
        else
        {
            ERROR("unsupported playlist type");
            status = HLS_ERROR;
            /* Release playlist lock */
            pthread_rwlock_unlock(&(pSession->playlistRWLock));
            break;
        }

        /* Release playlist lock */
        pthread_rwlock_unlock(&(pSession->playlistRWLock));

        /* At this point we've done an initial pass on our current playlist -- we are PREPARED */
        pSession->state = HLS_PREPARED;

        TIMESTAMP(DBG_INFO, "PREPARED");

        /* Loop and update the current playlist */
        while(status == HLS_OK)
        {
             /* If the parser was signalled to exit, return HLS_CANCELLED */
            if(pSession->bKillParser)
            {
                DEBUG(DBG_WARN, "parser signalled to stop");
                status = HLS_CANCELLED;
                break;
            }

            /* Get current time */
            if(clock_gettime(CLOCK_MONOTONIC, &wakeTime) != 0)
            {
                ERROR("failed to get current time");
                status = HLS_ERROR;
                break;
            }

            /* Get playlist WRITE lock */
            pthread_rwlock_wrlock(&(pSession->playlistRWLock));

            /* Check playlist validity */
            if((pSession->pCurrentPlaylist == NULL) ||
               (pSession->pCurrentPlaylist->type != PL_MEDIA) ||
               (pSession->pCurrentPlaylist->pMediaData == NULL))
            {
                ERROR("invalid current playlist");
                status = HLS_ERROR;
                /* Release playlist lock */
                pthread_rwlock_unlock(&(pSession->playlistRWLock));
                break;
            }

            /* Reload the current playlist if we don't have all the data */
            if(!(pSession->pCurrentPlaylist->pMediaData->bHaveCompletePlaylist))
            {
                /* Check to make sure it is time for an update */
                if(wakeTime.tv_sec > pSession->pCurrentPlaylist->nextReloadTime.tv_sec)
                {
                    /* Update our current playlist */
                    status = m3u8ParsePlaylist(pSession->pCurrentPlaylist, pSession);
                    if(status)
                    {
                        if(status == HLS_CANCELLED)
                        {
                            /* If the playlist download was cancelled, exit */
                            DEBUG(DBG_WARN, "parser signalled to stop");
                            /* Release playlist lock */
                            pthread_rwlock_unlock(&(pSession->playlistRWLock));
                            break;
                        }
                        else if (status == HLS_DL_ERROR)
                        {
                            /* If we encountered a download error, but we might have enough
                               buffer to get over whatever network issue, so pretent everything
                               is OK and try again later */

                            DEBUG(DBG_WARN, "problem downloading playlist, will retry");
                            status = HLS_OK;
                        }
                        else
                        {
                            ERROR("problem updating playlist");
                            /* Release playlist lock */
                            pthread_rwlock_unlock(&(pSession->playlistRWLock));
                            break;
                        }
                    }
                }
            }

            /* Release playlist lock (WRITE) */
            pthread_rwlock_unlock(&(pSession->playlistRWLock));

            /* Lock the parser wake mutex */
            if(pthread_mutex_lock(&(pSession->parserWakeMutex)) != 0)
            {
                ERROR("failed to lock parser wake mutex");
                status = HLS_ERROR;
                break;
            }

            /* Wait for LOOP_SECS before going again */
            wakeTime.tv_sec += PARSER_LOOP_SECS;

            DEBUG(DBG_NOISE,"sleeping %d seconds until %d", (int)PARSER_LOOP_SECS, (int)wakeTime.tv_sec);

            /* Wait until wakeTime */
            pthread_status = PTHREAD_COND_TIMEDWAIT(&(pSession->parserWakeCond), &(pSession->parserWakeMutex), &wakeTime);

            /* Unlock the parser wake mutex */
            if(pthread_mutex_unlock(&(pSession->parserWakeMutex)) != 0)
            {
                ERROR("failed to unlock parser wake mutex");
                status = HLS_ERROR;
                break;
            }

            /* If the timedwait call failed we need to bail */
            if((pthread_status != ETIMEDOUT) && (pthread_status != 0))
            {
                ERROR("failed to timedwait on the parser wake condition");
                status = HLS_ERROR;
                break;
            }

            /* Make sure we're still in a valid state */
            if(pSession->state == HLS_INVALID_STATE)
            {
                status = HLS_STATE_ERROR;
                break;
            }
        }
        if(status != HLS_OK)
        {
            break;
        }

    } while(0);

    pSession->parserStatus = status;

    if((status != HLS_OK) && (status != HLS_CANCELLED))
    {
        /* Send asynchronous error message to the player */
        srcPluginErr_t error;
        error.errCode = SRC_PLUGIN_ERR_SESSION_RESOURCE_FAILED;
        snprintf(error.errMsg, SRC_ERR_MSG_LEN, DEBUG_MSG("session %p parser thread quit with status: %d", pSession, status));
        hlsPlayer_pluginErrCallback(pSession->pHandle, &error);

        ERROR("session %p parser thread quit with status: %d", pSession, status);

        /* Put session in INVALID state */

        /* Block state changes */
        /*pthread_mutex_lock(&(pSession->stateMutex));*/

        /*pSession->state = HLS_INVALID_STATE;*/

        /* Leave critical section */
        /*pthread_mutex_unlock(&(pSession->stateMutex));*/
    }

    DEBUG(DBG_INFO,"session %p parser exiting with status %d", pSession, status);
    pthread_exit(NULL);
}

/**
 * Find the audio group associated with the current program
 *
 * @param pSession - pointer to the HLS session
 * @param ppGroupOut - pointer to the media group if found or NULL
 *
 * @return #hlsStatus_t
 */
hlsStatus_t findAudioGroup(hlsSession_t *pSession, hlsGroup_t **ppGroupOut)
{
   hlsStatus_t status = HLS_OK;
   hlsYesNo_t yesOrNo = HLS_YES;

   do
   {
      if((NULL == pSession) || (NULL == ppGroupOut))
      {
         ERROR("Invalid param");
         status = HLS_INVALID_PARAMETER;
         break;
      }

      status = findAudioGroupByAttrib(pSession, ATTRIB_LANGUAGE,
            (void *)pSession->audioLanguageISOCode, ppGroupOut);
      if(HLS_OK == status)
      {
         DEBUG(DBG_INFO, "Found audio group by attrib language\n");
         break;
      }

      status = findAudioGroupByAttrib(pSession, ATTRIB_DEFAULT, (void *)&yesOrNo, ppGroupOut);
      if(HLS_OK == status)
      {
         DEBUG(DBG_INFO, "Found audio group by attrib default\n");
         break;
      }

      status = findAudioGroupByAttrib(pSession, ATTRIB_AUTOSELECT, (void *)&yesOrNo, ppGroupOut);
      if(HLS_OK == status)
      {
         DEBUG(DBG_INFO, "Found audio group by attrib autoselect\n");
         break;
      }

   }while(0);

   return status;
}

/**
 * Find the audio group associated with the current program by attribute
 *
 * @param pSession - pointer to the HLS session
 * @param attrib   - Media group attribute
 * @param pData    - pointer to data based on attrib parameter
 * @param ppGroupOut - pointer to the media group if found or NULL
 *
 * @return #hlsStatus_t
 */
hlsStatus_t findAudioGroupByAttrib(hlsSession_t *pSession,
                                   mediaAttrib_t attrib,
                                   void *pData,
                                   hlsGroup_t **ppGroupOut)
{
   hlsStatus_t status = HLS_NOT_FOUND;
   int ii = 0;
   char *uri = NULL;
   hlsGroup_t *pGroup = NULL;
   llNode_t* pGroupNode = NULL;
   char *audioLanguage = NULL;
   hlsYesNo_t yesOrNo = HLS_YES;

   do {
   if((NULL == pSession) || (NULL == ppGroupOut) || (NULL == pData))
   {
      ERROR("Invalid param");
      status = HLS_INVALID_PARAMETER;
      break;
   }

   *ppGroupOut = NULL;

   if(NULL == pSession->pPlaylist->pGroupList)
   {
      ERROR("Empty group list");
      status = HLS_NOT_FOUND;
      break;
   }

   if(ATTRIB_LANGUAGE == attrib)
   {
      audioLanguage = (char *)pData;
   }
   else if((ATTRIB_DEFAULT == attrib) || (ATTRIB_AUTOSELECT == attrib))
   {
      yesOrNo = *((hlsYesNo_t *)pData);
   }

   pGroupNode = pSession->pPlaylist->pGroupList->pHead;

   for(ii = 0;
       ((ii < pSession->pPlaylist->pGroupList->numElements) && (NULL != pGroupNode));
       ii++, pGroupNode = pGroupNode->pNext)
   {
      pGroup = (hlsGroup_t*)(pGroupNode->pData);
      if(NULL == pGroup)
      {
         ERROR("Invalid Group in grouplist");
         status = HLS_ERROR;
         break;
      }

      if(0 == strcmp(pSession->pCurrentPlaylist->pMediaData->audio,
                     pGroup->groupID))
      {
         if((ATTRIB_LANGUAGE == attrib) && ((pGroup->language != NULL) &&
                  (0 == strcmp(pGroup->language, audioLanguage))) ||
            (ATTRIB_DEFAULT == attrib) && (yesOrNo == pGroup->def) ||
            (ATTRIB_AUTOSELECT == attrib) && (yesOrNo == pGroup->autoSelect))
         {
            *ppGroupOut = pGroup;
            if(NULL != pGroup->pPlaylist)
            {
               uri = pGroup->pPlaylist->playlistURL;
            }

            status = HLS_OK;
            DEBUG(DBG_INFO, "selecting audio group with name: %s, language: %s, playlistURL: %s",
                  pGroup->name, pGroup->language, PRINTNULL(uri));
            break;
         }
      }
   }
   }while(0);

   return status;
}

#ifdef __cplusplus
}
#endif
