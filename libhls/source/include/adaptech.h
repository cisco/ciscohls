#ifndef ADAPTECH_H
#define ADAPTECH_H

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
 * @file adaptech.h @date February 9, 2012
 *  
 * @author Jake Nunn (janunn@cisco.com)
 *  
 */

#ifdef __cplusplus
extern "C" {
#endif

float abrClientAddThroughputToAvg(float lastSegmentDldRate, float avgSegmentDldRate);
int abrClientGetBitrateIndex(float bandwidth, int rateMin, int rateMax, int bitratesSize, int* uiBitrates);
int abrClientGetAboveMinBitrate(int rateMin, int rateMax, int bitratesSize, int* uiBitrates);
int abrClientGetIndexFromBitrate(int bitrate, int bitratesSize, int* uiBitrates);
int abrClientGetNewBitrate(float lastFragmentThroughput, float avgFragmentThroughput, float bufferLength,
                           int bitratesSize, int* bitrates, int currentBitrate, int rateMin, int rateMax,
                           struct timespec* previousIncreaseTimeStamp, struct timespec* startTimeStamp);

#ifdef __cplusplus
}
#endif

#endif
