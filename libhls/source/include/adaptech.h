#ifndef ADAPTECH_H
#define ADAPTECH_H
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
