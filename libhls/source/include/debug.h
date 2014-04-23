#ifndef DEBUG_H
#define DEBUG_H

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
 * @file debug.h @date February 9, 2012
 * 
 * @author Patryk Prus (pprus@cisco.com) 
 *
 * Defines new types used by HLS plugin
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <time.h>

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#define DEBUG_MSG(x, ...) "[%s:%d] " x "\n", __FUNCTION__, __LINE__, ##__VA_ARGS__
#define TS_MSG(ts, x, ...) "[TS:%10f]:" x "\n", ((ts.tv_sec)*1.0) + (ts.tv_nsec/1000000000.0), ##__VA_ARGS__
#define DEBUG_TS_MSG(ts, x, ...) TS_MSG(ts, "[%s:%d] " x, __FUNCTION__, __LINE__, ##__VA_ARGS__)

#define GLOBAL_TIMESTAMPS 0

#ifdef ANDROID
#include <android/log.h>

#define DBG_OFF ANDROID_LOG_SILENT
#define DBG_WARN ANDROID_LOG_WARN
#define DBG_INFO ANDROID_LOG_INFO
#define DBG_NOISE ANDROID_LOG_VERBOSE

#define DBG_LEVEL DBG_INFO

/* To change debug levels within specific files, use:
 *
 * #undef DBG_LEVEL
 * #define DBG_LEVEL <new level>
 */
#define OPEN_LOG
#define CLOSE_LOG

#define ERROR(x, ...) __android_log_print(ANDROID_LOG_ERROR, "hls","!!ERROR!! " DEBUG_MSG(x, ##__VA_ARGS__));

#define TIMESTAMP(lvl, x, ...)                                                  \
    do                                                                          \
    {                                                                           \
        if(lvl >= DBG_LEVEL)                                                    \
        {                                                                       \
            struct timespec ts;                                                 \
            if(clock_gettime(CLOCK_MONOTONIC, &ts) == 0)                        \
            {                                                                   \
                __android_log_print(lvl, "hls", DEBUG_TS_MSG(ts, x, ##__VA_ARGS__));  \
            }                                                                   \
        }                                                                       \
    } while (0)
   
#if GLOBAL_TIMESTAMPS
#define DEBUG(lvl, x, ...) TIMESTAMP(lvl, x, ##__VA_ARGS__)
#else
#define DEBUG(lvl, x, ...)                                                      \
    do                                                                          \
    {                                                                           \
        if(lvl >= DBG_LEVEL)                                                    \
            __android_log_print(lvl, "hls", DEBUG_MSG(x, ##__VA_ARGS__));       \
    } while (0)
#endif
  
#elif defined(OPT_USE_SYSLOG)
#include <syslog.h>

/* There is no matching level for DBG_OFF in syslog. So assigning a value outside of 
 * LOG_EMERG(0) - LOG_DEBUG(7)*/
#define DBG_OFF (LOG_EMERG -1) 
#define DBG_WARN LOG_WARNING
#define DBG_INFO LOG_INFO
#define DBG_NOISE LOG_DEBUG

#define DBG_LEVEL DBG_INFO
/* To change debug levels within specific files, use:
 *
 * #undef DBG_LEVEL
 * #define DBG_LEVEL <new level>
 */
#define OPEN_LOG                                                                \
   do                                                                           \
   {                                                                            \
      /* Calling openlog() overrides the calling process(SAIL/gstreamer) openlog() call */ \
      /* openlog("hls", LOG_CONS | LOG_NDELAY, LOG_USER); */                    \
      /* setlogmask(LOG_UPTO(DBG_LEVEL)); */                                    \
   } while(0);

#define CLOSE_LOG                                                               \
   do                                                                           \
   {                                                                            \
      /* closelog(); */                                                         \
   } while(0);

#define ERROR(x, ...) syslog(LOG_ERR, "!!ERROR!! " DEBUG_MSG(x, ##__VA_ARGS__));

#define TIMESTAMP(lvl, x, ...)                                                  \
    do                                                                          \
    {                                                                           \
        if((lvl <= DBG_LEVEL) && (lvl != DBG_OFF))                              \
        {                                                                       \
            struct timespec ts;                                                 \
            if(clock_gettime(CLOCK_MONOTONIC, &ts) == 0)                        \
            {                                                                   \
                syslog(lvl, DEBUG_TS_MSG(ts, x, ##__VA_ARGS__));                \
            }                                                                   \
        }                                                                       \
    } while (0)
   
#if GLOBAL_TIMESTAMPS
#define DEBUG(lvl, x, ...) TIMESTAMP(lvl, x, ##__VA_ARGS__)
#else
#define DEBUG(lvl, x, ...)                                                      \
    do                                                                          \
    {                                                                           \
        if((lvl <= DBG_LEVEL) && (lvl != DBG_OFF))                              \
            syslog(lvl, DEBUG_MSG(x, ##__VA_ARGS__));                           \
    } while (0)
#endif
  

#else // LINUX
#define DBG_OFF 0
#define DBG_WARN 1
#define DBG_INFO 2
#define DBG_NOISE 3

#define OPEN_LOG
#define CLOSE_LOG


#define DBG_LEVEL DBG_INFO

/* To change debug levels within specific files, use:
 *
 * #undef DBG_LEVEL
 * #define DBG_LEVEL <new level>
 */

#define ERROR(x, ...) printf("!!ERROR!! " DEBUG_MSG(x, ##__VA_ARGS__))

#define TIMESTAMP(lvl, x, ...)                                                  \
    do                                                                          \
    {                                                                           \
        if((lvl <= DBG_LEVEL) && (lvl != DBG_OFF))                              \
        {                                                                       \
            struct timespec ts;                                                 \
            if(clock_gettime(CLOCK_MONOTONIC, &ts) == 0)                        \
            {                                                                   \
                printf(DEBUG_TS_MSG(ts, x, ##__VA_ARGS__));                     \
            }                                                                   \
        }                                                                       \
    } while (0)

#if GLOBAL_TIMESTAMPS
#define DEBUG(lvl, x, ...) TIMESTAMP(lvl, x, ##__VA_ARGS__)
#else
#define DEBUG(lvl, x, ...)                                                      \
    do                                                                          \
    {                                                                           \
        if((lvl <= DBG_LEVEL) && (lvl != DBG_OFF))                              \
            printf(DEBUG_MSG(x, ##__VA_ARGS__));                                \
    } while (0)
#endif

#endif

#ifdef __cplusplus
}
#endif

#endif
