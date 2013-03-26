#ifndef LLUTILS_H
#define LLUTILS_H

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
 * @file llUtils.h @date February 9, 2012
 * 
 * @author Patryk Prus (pprus@cisco.com) 
 *
 * Defines new types used by HLS plugin
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <pthread.h>

typedef struct llistNode_t_
{
	void* pData;
	struct llistNode_t_* pPrev;
	struct llistNode_t_* pNext;
} llNode_t;

typedef struct 
{
    int numElements;
	llNode_t* pHead;
	llNode_t* pTail;
} llist_t;

typedef enum {
    LL_OK,
    LL_ERROR,
} llStatus_t;

llist_t* newLinkedList();
llStatus_t freeLinkedList(llist_t* pList);

llNode_t* newLinkedListNode(void* pData);
llStatus_t freeLinkedListNode(llNode_t* pNode, void** ppData);

llStatus_t insertHead(llist_t* pList, void* pData);
llStatus_t insertTail(llist_t* pList, void* pData);

llStatus_t removeHead(llist_t* pList, void** ppData);
llStatus_t removeTail(llist_t* pList, void** ppData);

llStatus_t findNode(llist_t* pList, void* pData, llNode_t** ppNode);

typedef struct
{
    pthread_mutex_t queueMutex;
    llist_t* pList;
} msgQueue_t;

msgQueue_t* newMsgQueue();
llStatus_t freeMsgQueue(msgQueue_t* pQueue);
llStatus_t pushMsg(msgQueue_t* pQueue, void* pMessage);
llStatus_t popMsg(msgQueue_t* pQueue, void** ppMessage);
llStatus_t getMsgCount(msgQueue_t* pQueue, int* pCount);

#ifdef __cplusplus
}
#endif

#endif
