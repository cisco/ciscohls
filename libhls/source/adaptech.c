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

    Contributing Authors: Matt Snoby, Kris Kersey, Zack Wine, Chris Foster,
                          Tankut Akgul, Saravanakumar Periyaswamy

*/
/**
 * @file adaptech.c @date February 9, 2012
 *
 * @author Jake Nunn (janunn@cisco.com)
 *
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <time.h>
#include <stdio.h>

#include "adaptech.h"
#include "debug.h"

#define SEC(X) (X.tv_sec)
#define SECPTR(X) (X->tv_sec)

#define RAMPUPTHRESHOLD 20         // 20 seconds should give enough time (on avg) for a live session to ramp-up.
                                   // (i.e. - Go from its lowest bitrate and to its optimal bitrate.)
#define SLACKFACTOR 0.8            // c in Adaptech Algorithm
#define LOW_BUFF_LVL 5             // "theta1" in Adaptech Algorithm (The Adaptech paper uses an example value of 10 but their
                                   // player does not start playback until theta1 of A/V had been buffered; whereas for our
                                   // fast channel change we start playback as soon as we start downloading the first segment.
                                   // As a result, we need our theta1 to be smaller than a segment size so that when we run the
                                   // algorithm after we've downloaded a segment we won't always be starving. For example, if
                                   // theta1 is 5 seconds and we've downloaded 1 segment whose size minus the amount of the segment
                                   // played so far is still greater than 5 seconds then we won't be starving and we'll have the
                                   // opportunity to bitrate switch up and get to our our optimal bitrate as quickly as possible.)
                                   // (The Adaptech paper also uses segment size of 3 sec whereas HLS is usually 10.)
#define NORMAL_BUFF_LVL 20         // "theta2" in Adaptech Algorithm (The paper used a value of 20 with 3 sec segments)
#define CAN_SWITCH_UP_THRESHOLD 15 // "T" in Adaptech Algorithm (How long you have to wait to switch up to the next bitrate in the steady state.)
#define AVG_WEIGHT 0.5             // The "delta" in Adaptech Algorithm is the weight on the average segment D/L rate.
                                   // the Adaptec research paper used 0.8; however we adjusted it down to 0.5 due to increased
                                   // segment size (we use 10 second segments, vs. the 3 second segments in the example player)

// Returns the average segment download rate after adding the specified single download rate to the average.
// The formula used here is an exponentially weighted moving average as specified in the Adaptech algorithm.
float abrClientAddThroughputToAvg(float lastSegmentDldRate, float avgSegmentDldRate)
{
    return ((1 - AVG_WEIGHT) * lastSegmentDldRate) + (AVG_WEIGHT * avgSegmentDldRate);
}

//Returns the index of the maximum nominal bitrate that is smaller than the avail-bw
//and within the given bitrate window (min/max)
int abrClientGetBitrateIndex(float bandwidth, int rateMin, int rateMax, int bitratesSize, int* uiBitrates)
{
    int i = 0;
    int found = 0;
    int bitratesTooHigh = 1;
    int uiMaxBitrateIndex = bitratesSize - 1;

    DEBUG(DBG_NOISE, "bandwidth: %f rateMin: %d rateMax: %d", bandwidth, rateMin, rateMax);

    for (i = uiMaxBitrateIndex; i >= 0; i--)
    {
        DEBUG(DBG_NOISE, "bitrate %d: %d ", i, uiBitrates[i]);
        if ((uiBitrates[i]/bandwidth) <= 1)
        {
            bitratesTooHigh = 0;

            if((uiBitrates[i] > rateMin) && (uiBitrates[i] < rateMax))
            {
                found = 1;
                break;
            }
        }
    }
    if (!found)
    {
        if (!bitratesTooHigh)
        {
            ERROR("Couldn't find a suitable bitrate between %d & %d. Returning index '0'!", rateMin, rateMax);
        }
        else
        {
            ERROR("All available bitrates exceed current bandwidth. Returning index '0'!");
        }
    }

    if (i >= uiMaxBitrateIndex)
    {
        return uiMaxBitrateIndex;
    }
    else if (i < 0)
    {
        return 0;
    }
    else
    {
        return i;
    }
}

int abrClientGetAboveMinBitrate(int rateMin, int rateMax, int bitratesSize, int* uiBitrates)
{
    int i = 0;
    int found = 0;
    int uiMaxBitrateIndex = bitratesSize - 1;

    DEBUG(DBG_NOISE, "rateMin: %d rateMax: %d", rateMin, rateMax);
    for (i = uiMaxBitrateIndex; i > 0; i--)
    {
        DEBUG(DBG_NOISE, "bitrate %d: %d ", i, uiBitrates[i]);
        if (uiBitrates[i] < rateMax)
        {
            if(uiBitrates[i-1] < rateMin)
            {
                found = 1;
                break;
            }
            else if (i == 1)
            {
                i = 0;
                found = 1;
                break;
            }
        }
    }
    if (!found)
    {
        ERROR("Couldn't find a suitable bitrate between %d & %d. Returning index '0'!", rateMin, rateMax);
        i = 0;
    }

    return i;
}

int abrClientGetIndexFromBitrate(int bitrate, int bitratesSize, int* uiBitrates)
{
    int i = 0;
    int uiMaxBitrateIndex = bitratesSize - 1;

    DEBUG(DBG_NOISE, "bitrate: %d ", bitrate);
    for (i = uiMaxBitrateIndex; i >= 0; i--)
    {
        DEBUG(DBG_NOISE, "bitrate [%d]: %d ", i, uiBitrates[i]);
        if (uiBitrates[i] == bitrate)
        {
            DEBUG(DBG_NOISE, "found bitrate at index %d", i);
            break;
        }
    }

    if (i >= uiMaxBitrateIndex)
    {
        return uiMaxBitrateIndex;
    }
    else if (i < 0)
    {
        ERROR("couldn't find the specificed bitrate!");
        return 0;
    }
    else
    {
        return i;
    }
}

//Latest Version -- AdapTech Streaming
//See the paper "S.Akhshabi, S. Narayanaswamy, A.C. Begen, C.Dovrolis,
//An Experimental Evaluation of Rate-Adaptive Video Players over HTTP,
//Signal Processing: Image Communication, 2011"

//Ported from DownloadRatioRule.as in the ABR Experiment Project
//to this C implementation by Jake Nunn (janunn)

//Excerpts with pseudocode from Adaptech research paper are in comments
//following corresponding if() and else() statements in this function

int abrClientGetNewBitrate(float lastFragmentThroughput, float avgFragmentThroughput, float bufferLength,
                           int bitratesSize, int* bitrates, int currentBitrate, int rateMin, int rateMax,
                           struct timespec* previousIncreaseTimeStamp, struct timespec* startTimeStamp)
{

    //Current and maximum bitrate index
    int uiCurBitrateIndex;
    //int uiMaxBitrateIndex = bitratesSize - 1;

    int proposedIndex = 0, candidateBitrateIndex = 0;
    float prevIncTime = 0, playbackTime = 0;
    struct timespec nowTime;

    if ((bitrates == NULL) || (previousIncreaseTimeStamp == NULL) || (startTimeStamp == NULL))
    {
        ERROR("NULL pointer passed in.  return -1");
        return -1;
    }

    if ((rateMin > rateMax) || (rateMin < 0) || (rateMax < 0))
    {
        ERROR("rateMin (%d) and/or rateMax (%d) out of range!", rateMin, rateMax);
        return -1;
    }

    if(lastFragmentThroughput == 0)
    {
        DEBUG(DBG_NOISE, "First download ... start at lowest bitrate");
        return abrClientGetAboveMinBitrate(rateMin, rateMax, bitratesSize, bitrates);
    }

    uiCurBitrateIndex = abrClientGetIndexFromBitrate(currentBitrate, bitratesSize, bitrates);

    //Calculate time since playback started
    clock_gettime(CLOCK_MONOTONIC, &nowTime);
    //DEBUG(DBG_INFO, "!!!!!!!!!!!!!!!!!SEC %ld, SECPTR %ld", SEC(nowTime), SECPTR(startTimeStamp));
    playbackTime = (float)(SEC(nowTime) - SECPTR(startTimeStamp));
    //DEBUG(DBG_INFO, "!!!!!!!!!!!!!!!!!Float of playbackTime: %f", playbackTime);

    DEBUG(DBG_NOISE, "################################");
    DEBUG(DBG_NOISE, "ADAPTECH STATS");
    DEBUG(DBG_NOISE, "lastFragThroughput = %f ", lastFragmentThroughput);
    DEBUG(DBG_NOISE, "avgFragThroughput = %f ", avgFragmentThroughput);
    DEBUG(DBG_NOISE, "bufferLength = %f ", bufferLength);
    DEBUG(DBG_NOISE, "curBitrateIndex = %d ", uiCurBitrateIndex);
    DEBUG(DBG_NOISE, "Are we within Ramp-up period? = %s", (playbackTime < RAMPUPTHRESHOLD) ? "TRUE" : "FALSE");
    DEBUG(DBG_NOISE, "################################");

    /* Latest Version Logic */

    if(bufferLength >= NORMAL_BUFF_LVL) //if theta2 < Beta(t) < Betamax then:
    {
        // Write phi2 into candidateBitrateIndex.  phi1 based on avgFragmentThroughput.
        candidateBitrateIndex = abrClientGetBitrateIndex((SLACKFACTOR * avgFragmentThroughput), rateMin, rateMax, bitratesSize, bitrates);
        DEBUG(DBG_INFO, "current max possible bitrate based on average throughput: %d ", bitrates[candidateBitrateIndex]);

        DEBUG(DBG_INFO, "bufferLength %f >= %d", bufferLength, NORMAL_BUFF_LVL);
        if(candidateBitrateIndex <= uiCurBitrateIndex) // phi2 <= phicur
        {
            proposedIndex = uiCurBitrateIndex; //Do nothing
            DEBUG(DBG_INFO, "Average throughput (phi2) <= current bitrate -- stay @ %d", bitrates[proposedIndex]);
        }
        else // phi2 > phicur
        {
            // Write phi1 into candidateBitrateIndex (Overwriting phi2) phi1 based on lastFragmentThroughput.
            candidateBitrateIndex = abrClientGetBitrateIndex((SLACKFACTOR * lastFragmentThroughput), rateMin, rateMax, bitratesSize, bitrates);
            //Calculate time since last bitrate increase
            clock_gettime(CLOCK_MONOTONIC, &nowTime);
            //DEBUG(DBG_INFO, "!!!!!!!!!!!!!!!!!nowTimeSec: %ld prevIncSec: %ld", nowTime.tv_sec, previousIncreaseTimeStamp->tv_sec);
            //DEBUG(DBG_INFO, "!!!!!!!!!!!!!!!!!SEC %ld, SECPTR %ld", SEC(nowTime), SECPTR(previousIncreaseTimeStamp));
            prevIncTime = (float)(SEC(nowTime) - SECPTR(previousIncreaseTimeStamp));
            //DEBUG(DBG_INFO, "!!!!!!!!!!!!!!!!!Float of prevIncTime: %f", prevIncTime);

            if(candidateBitrateIndex > uiCurBitrateIndex)  // if phi1 > phicur
            {
                // The Adaptec algorithm's definition of "can-switch-up" being TRUE is when phi2 has been greater than phicur for >= CAN_SWITCH_UP_THRESHOLD amount of time.
                //
                // The "ramping up" logic (i.e. - playbackTime < RAMPUPTHRESHOLD) is a departure from the standard Adaptech algorithm
                // as laid out in the research paper. We discovered that if we have larger segment sizes (such as 10-second segments)
                // and a LOT of bandwidth, we can pull down a LOT video at a low bit rate in 15 seconds (or whatever CAN_SWITCH_UP_THRESHOLD happens to be).
                // So we decided to allow the bitrate to "ramp up" in (theta2 < Beta(t) < Betamax) even if it hasn't been CAN_SWITCH_UP_THRESHOLD amount of time
                // since the last profile switch up.
                if ((prevIncTime >= CAN_SWITCH_UP_THRESHOLD) || (playbackTime < RAMPUPTHRESHOLD))
                {
                    if (bitrates[uiCurBitrateIndex + 1] > rateMax)
                    {
                        proposedIndex = uiCurBitrateIndex;
                        DEBUG(DBG_INFO, "proposed bitrate exceeds max (%d) -- stay @ %d", rateMax, bitrates[proposedIndex]);
                    }
                    else if (bitrates[uiCurBitrateIndex + 1] < rateMin)
                    {
                        proposedIndex = candidateBitrateIndex;
                        DEBUG(DBG_INFO, "below min bitrate (%d) -- move up to %d", rateMin, bitrates[proposedIndex]);
                    }
                    else
                    {
                        proposedIndex = uiCurBitrateIndex + 1;
                        clock_gettime(CLOCK_MONOTONIC, previousIncreaseTimeStamp);
                        DEBUG(DBG_INFO, "move up to %d", bitrates[proposedIndex]);
                    }
                }
                else
                {
                    //Not enough time has passed or suggested rate lower than current rate
                    proposedIndex = uiCurBitrateIndex;
                    DEBUG(DBG_INFO, "15 sec HAVE NOT passed since last increase -- stay @ %d", bitrates[proposedIndex]);
                }
            }
            else
            {
                proposedIndex = uiCurBitrateIndex;
                DEBUG(DBG_INFO, "Instanteous throughput (phi1) <= current bitrate -- stay @ %d", bitrates[proposedIndex]);
            }
        }
    }
    else if(bufferLength < NORMAL_BUFF_LVL && bufferLength >= LOW_BUFF_LVL) // else if theta1 < Beta(t) < theta2 then:
    {
          // Write phi1 into candidateBitrateIndex.  phi1 based on lastFragmentThroughput.
          candidateBitrateIndex = abrClientGetBitrateIndex((SLACKFACTOR * lastFragmentThroughput), rateMin, rateMax, bitratesSize, bitrates);
          DEBUG(DBG_INFO, "current max possible bitrate based on instantaneous throughput: %d ", bitrates[candidateBitrateIndex]);

          DEBUG(DBG_INFO, "bufferLength %f >= %d and < %d", bufferLength, LOW_BUFF_LVL, NORMAL_BUFF_LVL);

          // On a channel change, our hope is that we'll fall into this if statment (theta1 < Beta(t) < theta2) after
          // downloading just one segment.  For example, if theta1 is 5 seconds and we've downloaded 1 segment
          // whose size minus the amount of the segment played so far is still greater than 5 seconds then we
          // should fall into this if statement (instead of staying in panic mode) which will allow us to "ramp up"
          // (i.e. - move to a higher bitrate) and get to our optimal bitrate as quickly as possible when on a high b/w network.

#if 0 // Remove this for now.  Leave Adaptech as-is until we see real reasons to start monkeying with it.
          if((candidateBitrateIndex == 0) && ((bitrates[0]/(SLACKFACTOR * lastFragmentThroughput)) >= 2))
          {
              proposedIndex = abrClientGetAboveMinBitrate(rateMin, rateMax, bitratesSize, bitrates);
              DEBUG(DBG_INFO, "Available bandwidth is 50 percent or less of lowest bitrate... PANIC MODE to lowest index within range which is %d!", proposedIndex);
              return proposedIndex;
          }
#endif

          if(candidateBitrateIndex < uiCurBitrateIndex) //if phi1 < phicur then:
          {
              if(uiCurBitrateIndex > 0)
              {
                  if (bitrates[uiCurBitrateIndex - 1] < rateMin)
                  {
                      proposedIndex = uiCurBitrateIndex;
                      DEBUG(DBG_INFO, "Not enough buffer and throughput too low, but would have dropped below min bitrate -- stay at %d", bitrates[proposedIndex]);
                  }
                  else
                  {
                      proposedIndex = uiCurBitrateIndex - 1;
                      DEBUG(DBG_INFO, "Not enough buffer and throughput too low -- move down to %d", bitrates[proposedIndex]);
                  }
              }
          }
          else if (candidateBitrateIndex > uiCurBitrateIndex) // if phi1 > phicur then:
          {
              if (bitrates[uiCurBitrateIndex + 1] > rateMax)
              {
                  proposedIndex = uiCurBitrateIndex;
                  DEBUG(DBG_INFO, "proposed bitrate exceeds max (%d) -- stay @ %d", rateMax, bitrates[proposedIndex]);
              }
              else if (bitrates[uiCurBitrateIndex + 1] < rateMin)
              {
                  proposedIndex = candidateBitrateIndex;
                  DEBUG(DBG_INFO, "below min bitrate (%d) -- move up to %d", rateMin, bitrates[proposedIndex]);
              }
              else
              {
                  proposedIndex = uiCurBitrateIndex + 1;
                  clock_gettime(CLOCK_MONOTONIC, previousIncreaseTimeStamp);
                  DEBUG(DBG_INFO, "move up to %d", bitrates[proposedIndex]);
              }
          }
          else
          {
              proposedIndex = uiCurBitrateIndex;
              DEBUG(DBG_INFO, "stay @ %d", bitrates[proposedIndex]);
          }
    }
    else // else if Beta(t) < theta1 then:
    {
        // panic mode (lowest bitrate NOW since we are below LOW_BUFF_LVL)
        DEBUG(DBG_INFO, "bufferLength %f < %d", bufferLength, LOW_BUFF_LVL);
        proposedIndex = abrClientGetAboveMinBitrate(rateMin, rateMax, bitratesSize, bitrates);
        DEBUG(DBG_INFO, "PANIC MODE! (Not enough buffer!) -- move to lowest bitrate %d", bitrates[proposedIndex]);
    }

    DEBUG(DBG_INFO, "Bitrate: %d --> %d", bitrates[uiCurBitrateIndex], bitrates[proposedIndex]);

    return proposedIndex;
}

#ifdef __cplusplus
}
#endif
