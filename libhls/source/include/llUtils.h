#ifndef LLUTILS_H
#define LLUTILS_H
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
