#ifndef HLSTYPES_H
#define HLSTYPES_H

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
 * @file hlsTypes.h @date February 9, 2012
 * 
 * @author Patryk Prus (pprus@cisco.com)  
 *
 * Defines new types used by HLS plugin
 */

/**
 * @addtogroup hlsGlobals HLS Globals
 * @{
 *
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <pthread.h>
#include <time.h>

#include <curl/curl.h>

#include "llUtils.h"
#include "sourcePlugin.h"

#ifdef ANDROID
/*! Bionic C doesn't define pthread_condattr_setclock() in pthread.h to
   force the condition to use CLOCK_MONOTONIC.  Rather, they define
   pthread_cond_timedwait_monotonic() to accomplish absolute timed waits. */
#define PTHREAD_COND_TIMEDWAIT pthread_cond_timedwait_monotonic
#else
#define PTHREAD_COND_TIMEDWAIT pthread_cond_timedwait
#endif

#ifdef ANDROID
/*! Android Froyo doesn't have support for pthread_rwlock...
   Until we move to Gingerbread, replace the rwlock with
   a recursive mutex. */
#define pthread_rwlock_t pthread_mutex_t
#define pthread_rwlock_wrlock(x) pthread_mutex_lock(x)
#define pthread_rwlock_rdlock(x) pthread_mutex_lock(x)
#define pthread_rwlock_unlock(x) pthread_mutex_unlock(x)
#define pthread_rwlock_destroy(x) pthread_mutex_destroy(x)
#endif

/*! Max PTS value */
#define PTS_LIMIT (1ll << 33)

/*! Max FPS during trick modes */
#define MAX_TRICK_FPS 4

/*! Maximum number of times to redownload an invalid playlist */
#define MAX_PL_PARSE_REDL_RETRIES 5

/*! Maximum number of times to attempt to redownload a playlist */
#define MAX_PL_DL_RETRIES 10

/*! Nanoseconds to wait between download retries */
#define DOWNLOAD_RETRY_WAIT_NSECS 100000000

/*! Seconds to wait for the plugin to move to HLS_PREPARED state */
// Increasing the timeout from 5 secs to 30 secs.Will be reverted back to 5 secs
// once the async implementation is done
#define HLS_PREPARED_TIMEOUT_SECS 5

/*! Seconds to wait for the plugin to move to HLS_PLAYING state */
#define HLS_PLAYING_TIMEOUT_SECS 5

/*! Safely print a NULL string */
#define PRINTNULL(x) ((x == NULL) ? "(null)" : x)

/*! Maximum m3u8 playlist length */
#define PL_LINE_LENGTH (2*1024)

/*! Maximum supported HLS playlist version */
#define MAX_SUPPORTED_PL_VERSION 4

/*! Maximum number of simultaneous HLS sessions */
#define MAX_SESSIONS 3

/*! Local storage path */
#define LOCAL_PATH "/tmp/HLS/"

/*! EXT-X_MEDIA group type - one for discrete audio/video */
#define MAX_NUM_MEDIA_GROUPS (1)

/*! \enum hlsStatus_t
 * Enumeration of available return status HLS functions
 */
typedef enum {
    HLS_OK,                 /*!< Operation successful */
    HLS_CANCELLED,          /*!< Function was externally signalled to stop */
    HLS_INVALID_PARAMETER,  /*!< Invalid parameter passed to function */
    HLS_FILE_ERROR,         /*!< File I/O error */
    HLS_MEMORY_ERROR,       /*!< Memory allocation/free error */
    HLS_STATE_ERROR,        /*!< Function called while in incorrect state */
    HLS_UNSUPPORTED,        /*!< Requested operation not supported at this time */
    HLS_DL_ERROR,           /*!< Download error */
    HLS_ERROR               /*!< Generic error */
} hlsStatus_t;

/*
 
!!!!! ADD ANY NEW GLOBALS ABOVE THIS LINE TO KEEP DOCUMENTATION SANE !!!!!
 
*/

/**
 * 
 *  
 * @} 
 */

/*! \enum hlsSessionState_t
 * Internal HLS session states
 * 
 */
typedef enum {
    HLS_INVALID_STATE,  /*!< Invalid state */
    HLS_INITIALIZED,    /*!< Session has been initialized */
    HLS_PREPARED,       /*!< Session is prepared for playback */
    HLS_PLAYING         /*!< Session is actively downloading and playing data */
} hlsSessionState_t;

/*! \enum hlsPlaylistType_t
 * hlsPlaylist_t types
 */
typedef enum {
    PL_INVALID,     /*!< Invalid playlist */
    PL_VARIANT,     /*!< Variant playlist */
    PL_MEDIA,       /*!< Standard segment or I-frame playlist */
    PL_WRONGVER     /*!< Playlist version is unsupported by the plugin */
} hlsPlaylistType_t;

/*! \enum hlsPlaylistType_t
 * Types of bitrate limit.  Used to identify parameter for hlsSession_setBitrateLimit() function
 */
typedef enum {
    BR_LIMIT_MAX,   /*!< Maximum bitrate limit */
    BR_LIMIT_MIN,   /*!< Minimum bitrate limit */
    BR_LIMIT_TARGET /*!< Target bitrate */
} hlsBitrateLimit_t;

/*! \enum hlsContentType_t
 * Playlist content type, specified by by #EXT-X-PLAYLIST-TYPE tag
 */
typedef enum {
    HLS_UNSPECIFIED,    /*!< #EXT-X-PLAYLIST-TYPE tag not present */
    HLS_EVENT,          /*!< #EXT-X-PLAYLIST-TYPE tag with EVENT value */
    HLS_VOD             /*!< #EXT-X-PLAYLIST-TYPE tag with VOD value */
} hlsContentType_t;

/*! \enum m3u8Tag_t
 * Supported m3u8 playlist tags
 */
typedef enum {
    EXTM3U,                     
    EXTINF,                     /*!< In media playlists only */
    EXT_X_STREAM_INF,           /*!< In variant playlists only */
    EXT_X_VERSION,              
    EXT_X_TARGETDURATION,       /*!< In media playlists only */
    EXT_X_MEDIA_SEQUENCE,       /*!< In media playlists only */
    EXT_X_ALLOW_CACHE,          /*!< In media playlists only */
    EXT_X_PLAYLIST_TYPE,        /*!< In media playlists only */
    EXT_X_KEY,                  /*!< In media playlists only */
    EXT_X_PROGRAM_DATE_TIME,    /*!< In media playlists only */
    EXT_X_DISCONTINUITY,        /*!< In media playlists only */
    EXT_X_ENDLIST,              /*!< In media playlists only */
    EXT_X_BYTERANGE,            /*!< In media playlists only */
    EXT_X_CISCO_KEY,            /*!< In media playlists only */
    EXT_X_CISCO_PROT_HEADER,    
    EXT_X_I_FRAME_STREAM_INF,   /*!< In variant playlists only */
    EXT_X_I_FRAMES_ONLY,        /*!< In media playlists only */
    EXT_X_MEDIA,                /*!< In variant playlists only */
    NUM_SUPPORTED_TAGS
} m3u8Tag_t;

/*! \enum hlsMediaType_t
 * Supported media types
 */
typedef enum {
   HLS_MEDIA_TYPE_INVALID,

   HLS_MEDIA_TYPE_AUDIO,
   HLS_MEDIA_TYPE_VIDEO,

   HLS_MEDIA_TYPE_MAX

} hlsMediaType_t;

/*! \enum hlsYesNo_t
 * HLS yes/no enumerated-string 
 */
typedef enum {
   HLS_NO,
   HLS_YES
}hlsYesNo_t;

/*! \struct hlsMediaPlaylistData_t
 * Structure representing additional playlist data when playlist type == HLS_MEDIA
 */
typedef struct {
    /*! Sum of all segment durations in playlist (seconds) */
    double duration;

    /*! Current playback position in the playlist, 
        starting from the end of the playlist (seconds) */
    double positionFromEnd; 

    /*! Offset from the start of the playlist of the last first valid play position. 
        For VoD this is 0; for live it is 2*TARGET_DURATION of the playlist. */
    double startOffset;

    /*! Offset from the end of the playlist of the last valid play position. 
        For VoD this is 0; for live it is 3*TARGET_DURATION of the playlist. */
    double endOffset;

    /*! Pointer to the last downloaded segment node 
        in the hlsPlaylist_t::pList linked segment list */
    llNode_t* pLastDownloadedSegmentNode;

    int bitrate;    /*!< Playlist bitrate (from EXT-X-STREAM-INF) */
    int width;      /*!< Video width (from EXT-X-STREAM-INF) */
    int height;     /*!< Video height (from EXT-X-STREAM-INF) */
    char* codecs;   /*!< Codecs string (from EXT-X-STREAM-INF) */
    char* audio;    /*!< Audio string (from EXT-X-STREAM-INF) */
    char* video;    /*!< Video string (from EXT-X-STREAM-INF) */

    /*! Playlist target duration (from EXT-X-TARGETDURATION) */
    int targetDuration; 

    /*! Playlist starting sequence number (from EXT-X-MEDIA-SEQUENCE) */
    int startingSequenceNumber; 

    /*! TRUE if EXT-X-ENDLIST tag present in playlist, FALSE otherwise */
    int bHaveCompletePlaylist;

    /*! TRUE if EXT-X-ALLOW-CACHE tag present with value of YES, 
        FALSE otherwise */
    int bCacheable;

    /*! Determined by presence and value of EXT-X-PLAYLIST-TYPE tag */
    hlsContentType_t mutability;

    /*! TRUE if EXT-X-I-FRAMES-ONLY tag is present in playlist, FALSE otherwise */
    int bIframesOnly;
} hlsMediaPlaylistData_t;

/*! \struct hlsPlaylist_t
 * Structure representing generic HLS playlist of any type
 */
typedef struct {
    char* playlistURL;  /*!< The original URL of the playlist */
    char* redirectURL;  /*!< The actual URL of the playlist after redirection */
    char* baseURL;      /*!< The original URL of the playlist minus the filename */

    hlsPlaylistType_t type; /*!< Playlist type */

    int version;    /*!< Playlist version (from EXT-X-VERSION) */
    
    /*! Linked list playlist contents. 
        If #type == HLS_VARIANT --> linked list of #hlsProgram_t
        If #type == HLS_MEDIA --> linked list of #hlsSegment_t */
    llist_t* pList;

    /*! Linked list of group(EXT-X-MEDIA) playlist contents. 
        If #type == HLS_VARIANT --> linked list of #hlsGroup_t 
        else NULL */
    llist_t* pGroupList;

    /*! The absolute time value that should pass 
        before attempting to redownload the playlist
        (for rotating playlists) */
    struct timespec nextReloadTime;
    
    /*! Number of playlist updates that have not carried any new data */
    int unchangedReloads; 

    /*! Pointer to the parent node when this structure is contained in the 
        llNode_t::pData field */
    llNode_t* pParentNode;

    /*! Additional playlist information that only applies when #type == HLS_MEDIA */
    hlsMediaPlaylistData_t* pMediaData;
} hlsPlaylist_t;

/*! \struct hlsSegment_t 
 * Describes an HLS segment contained in a media playlist
 */
typedef struct {
    char* URL;          /*!< Segment URL */
    char* programName;  /*!< Program name (from EXTINF) */
    int seqNum;         /*!< Segment sequence number */
    double duration;     /*!< Segment duration (from EXTINF) */

    /*! TRUE if segment URL preceded by an EXT-X-DISCONTINUITY tag, 
        FALSE otherwise */
    int bDiscontinuity;

    /*! Date and time values parsed from an EXT-X-PROGRAM-DATE-TIME tag */
    struct tm* pProgramTime;

    srcEncType_t encType;   /*!< Encryption type (from EXT-X-KEY or EXT-X-CISCO-KEY) */
    char* keyURI;           /*!< Key URI (from EXT-X-KEY or EXT-X-CISCO-KEY) */
    char iv [16];               /*!< Initialization vector for segment decryption
                                 (from EXT-X-KEY or EXT-X-CISCO-KEY) */

    char key[16];          /*!< Actual key that has been parsed from the keyURI*/
    long byteLength;     /*!< Byte length of segment (from EXT-X-BYTERANGE) */
    long byteOffset;     /*!< Byte offset of segment (from EXT-X-BYTERANGE) */

    /*! Pointer to the parent node when this structure is contained in the 
        llNode_t::pData field */
    llNode_t* pParentNode;
} hlsSegment_t;

/*! \struct hlsProgram_t 
 * Describes an HLS program contained in a variant playlist
 */
typedef struct {
    int programID;  /*!< Program ID number (from EXT-X-STREAM-INF)*/

    /*! Linked list of hlsPlaylist_t structures representing 
        available segment streams */
    llist_t* pStreams;      

    /*! Linked list of hlsPlaylist_t structures representing 
        available I-frame streams */
    llist_t* pIFrameStreams;

    /*! Array of the available stream bitrates contained in #pStreams */
    int* pAvailableBitrates;

    /*! Array of the available stream bitrates contained in #pIFrameStreams */
    int* pAvailableIFrameBitrates;

    /*! Pointer to the parent node when this structure is contained in the 
        llNode_t::pData field */
    llNode_t* pParentNode;
} hlsProgram_t;

/*! \struct hlsGroup_t 
 * Describes an HLS Group (EXT-X-MEDIA) contained in a variant playlist
 */
typedef struct {
    char *groupID;  /*!< Group ID number (from EXT-X-MEDIA)*/

    /*! hlsPlaylist_t structure pointer */ 
    hlsPlaylist_t* pPlaylist;      
    
    hlsMediaType_t type;   /*!< type string (from EXT-X-MEDIA) */ 
    char *language;        /*!< language string (from EXT-X-MEDIA) */ 
    char *name;            /*!< name string (from EXT-X-MEDIA) */
    hlsYesNo_t def;        /*!< default (yes/no) (from EXT-X-MEDIA) */
    hlsYesNo_t autoSelect; /*!< autoSelect (yes/no) (from EXT-X-MEDIA) */

    /*! Pointer to the parent node when this structure is contained in the 
        llNode_t::pData field */
    llNode_t* pParentNode;
} hlsGroup_t;

/*! \enum playbackControllerSignal_t
 * Playback controller thread signals
 */
typedef enum {
    PBC_STARTING_PLAYBACK,      /*!< hlsSession_play() has been called -- playback is starting */      
    PBC_STOPPING_PLAYBACK,      /*!< hlsSession_stop() has been called -- playback is stopping */
    PBC_DOWNLOAD_COMPLETE,      /*!< Downloader thread has finished downloading the asset */
    PBC_PLAYER_AUDIO_UNDERRUN,  /*!< Player audio FIFO is starving */
    PBC_NUM_SIGNALS
} playbackControllerSignal_t;

/*! \struct hlsGrpDwnldData_t
 * Data passed to the media group downloader threads 
 */
typedef struct
{
   int mediaGrpIdx;
   void *pSession;
}hlsGrpDwnldData_t;

/*! \struct hlsSession_t 
 * Describes one HLS plugin session 
 */
typedef struct {
    /*! Unique session handle passed by player in session open() call */
    void* pHandle;

    char* sessionName;  /*!< Human-readable unique session name */

    /*! Handle to a CURL object which will be used for all CURL operations. */
    CURL* pCurl;

    /*! Mutex that ensures that the curl handle isn't used by 
        multiple threads at the same time. */
    pthread_mutex_t curlMutex;
   
    /*! Curl object which will be used by the media group downloader thread */
    CURL* pMediaGroupCurl[MAX_NUM_MEDIA_GROUPS];

    /*! Mutex to protect the mediagroup curl object */
    pthread_mutex_t mediaGroupCurlMutex[MAX_NUM_MEDIA_GROUPS];
    
    //TODO: clarify the below...

    /* Read/write lock to protect access to:
        pPlaylist
        pCurrentProgram
        pCurrentPlaylist
        pCurrentGroup
      */
    pthread_rwlock_t playlistRWLock;

    hlsPlaylist_t* pPlaylist;
    hlsProgram_t *pCurrentProgram;
    hlsPlaylist_t* pCurrentPlaylist;
    hlsGroup_t *pCurrentGroup[MAX_NUM_MEDIA_GROUPS];

    /*! Number of active alternative group media */
    unsigned int currentGroupCount;

    /*! Rate at which last segment was downloaded (bps) */
    float lastSegmentDldRate;   
    /*! Average segment download rate (bps) */
    float avgSegmentDldRate;
   
    /*! Mutex to protect lastSegmentDldRate and avgSegmentDldRate */
    pthread_mutex_t dldRateMutex;

    /*! Absolute time at which last bitrate change occured */
    struct timespec lastBitrateChange;
    /*! Absolute time at which playback started */
    struct timespec playbackStart;

    /*! Mutex that ensures that the current player event 
        is fully acted upon before starting to process
        additional player events */
    pthread_mutex_t playerEvtMutex;

    /*! Mutex for writng session state. RECURSIVE -- may 
        want to change state several times without anyone jumping
        in and changing it in between (see seek()) */
    pthread_mutex_t stateMutex;

	/*! Current session state, protected by #stateMutex */
    hlsSessionState_t state;

    /*! Mutex for session set functions. RECURSIVE -- may 
        want to set multiple things without anyone jumping
        in and setting anything in between */
    pthread_mutex_t setMutex;

    /*! Current playback speed.  Should only be changed using 
        hlsSession_setSpeed() function and so is protected by
        the #setMutex */
    float speed;        

    int maxBitrate;     /*!< Maximum bitrate limit (bps) */
    int minBitrate;     /*!< Minimum bitrate limit (bps) */
    int targetBitrate;  /*!< Target bitrate to begin playback at (bps) */

    /*! Last pts value received in a call back from the player.  Used to 
        calculate playback position.  MUST be 64 bits (PTSes are 33 bits,
        and we use -1 as an invalid value) */
    long long lastPTS;

    double timeBuffered; /*!< Current buffer level for this session (seconds) */

    /* Parser thread */
    pthread_t parser;                   /*!< Parser thread handle */
    hlsStatus_t parserStatus;           /*!< Parser thread status */
    int bKillParser;                    /*!< Parser thread kill signal*/
    pthread_mutex_t parserWakeMutex;    /*!< Parser thread wake mutex */
    pthread_cond_t parserWakeCond;      /*!< Parser thread wake condition */
    
    /* Downloader thread */
    pthread_t downloader;                   /*!< Downloader thread handle */
    hlsStatus_t downloaderStatus;           /*!< Downloader thread status */
    int bKillDownloader;                    /*!< Downloader thread kill signal */
    pthread_mutex_t downloaderWakeMutex;    /*!< Downloader thread wake mutex */
    pthread_cond_t downloaderWakeCond;      /*!< Downloader thread wake condition */
    
    /* Media group downloader thread(s) */
    pthread_t groupDownloader[MAX_NUM_MEDIA_GROUPS];          /*!< Media group downloader thread handle */          
    hlsStatus_t groupDownloaderStatus[MAX_NUM_MEDIA_GROUPS];  /*!< Media group downloader thread status */
    hlsGrpDwnldData_t grpThreadData[MAX_NUM_MEDIA_GROUPS];    /*!< Media group downloader thread param */

    /* Playback Controller thread */
    pthread_t playbackController;                   /*!< Playback controller thread handle */
    hlsStatus_t playbackControllerStatus;           /*!< Playback controller thread status */
    int bKillPlaybackController;                    /*!< Playback controller thread kill signal */
    pthread_mutex_t playbackControllerWakeMutex;    /*!< Playback controller thread wake mutex */
    pthread_cond_t playbackControllerWakeCond;      /*!< Playback controller thread wake condition */

    msgQueue_t* playbackControllerMsgQueue; /*!< Playback controller thread message queue */

    int eofCount; /*!< count of eof recieved from the downloader thread(s) */
    int boundaryReached /*!< Boolean - whether playback reached the boundary */

} hlsSession_t;

/*! \struct hlsPlugin_t 
 * Describes an instance of the HLS plugin 
 *  
 * @ingroup hlsPlugin
 */
typedef struct {
    /*! TRUE if initialize() has been called for this plugin, 
        FALSE otherwise */
    int bInitialized;

    /*! Player function pointer structure passed in srcPluginLoad() */
    srcPlayerFunc_t* pHlsPlayerFuncTable;

    /*! Callback function for plugin events registered via hlsPlugin_registerCB() */
    pluginEvtCallback_t pluginEvtCallback;

    /*! Callback function for plugin errors registered via hlsPlugin_registerCB() */
    pluginErrCallback_t pluginErrCallback;

    /*! Array of session handles */
    hlsSession_t* hlsSessions[MAX_SESSIONS];

    /*! Number of active sessions*/
    int activeSessions; 
} hlsPlugin_t;

#ifdef __cplusplus
}
#endif

/**
 *
 * @}
 */

#endif //end of HLSTYPES_H
