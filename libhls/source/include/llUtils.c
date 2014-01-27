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
 * @file llUtils.c @date February 9, 2012
 *  
 * @author Patryk Prus (pprus@cisco.com) 
 *  
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <stdio.h>

#include "llUtils.h"
#include "debug.h"

/** 
 * Allocates memory for a new linked list structure and 
 * initializes members. 
 * 
 * @return llist_t* - pointer to newly allocated linked list
 */
llist_t* newLinkedList()
{
    llist_t* pList = NULL;

    pList = (llist_t*)malloc(sizeof(llist_t));	
    if(pList == NULL)
    {
        ERROR("error allocating linked list");
    }
    else
    {
        pList->numElements = 0;
        pList->pHead = NULL;
        pList->pTail = NULL;
    }

    return pList;
}

/** 
 * Frees memory allocated for given linked list.  List MUST be 
 * empty to be freed. 
 * 
 * @param pList - pointer to list to free
 * 
 * @return #llStatus_t
 */
llStatus_t freeLinkedList(llist_t* pList)
{
    llStatus_t rval = LL_OK;

    if(pList != NULL) 
    {
        /* Check to make sure list is empty */
        if((pList->numElements != 0) ||
           (pList->pHead != NULL) ||
           (pList->pTail != NULL))
        {
            ERROR("linked list is not empty -- cannot free");
            rval = LL_ERROR;
        }
        else
        {
            /* Free the list structure */
            free(pList);
        }
    }
    
    return rval;
}

/** 
 * Allocates memory for a new linked list node and initializes 
 * members.  pData member of the returned structure will be 
 * initializes to the pData function parameter. 
 * 
 * @param pData - initial value of returned node's pData field
 * 
 * @return llNode_t* - pointer to newly allocated linked list 
 *         node
 */
llNode_t* newLinkedListNode(void* pData)
{
    llNode_t* pNode = NULL;

    pNode = (llNode_t*)malloc(sizeof(llNode_t));	
    if(pNode == NULL)
    {
        ERROR("error allocating linked list node");
    }
    else
    {
        pNode->pNext = NULL;
        pNode->pPrev = NULL;
        pNode->pData = pData;
    }

    return pNode;
}

/** 
 * Frees memory allocated for given linked list node.  On return
 * *ppData will be set to the value of the freed node's pData 
 *  field.
 * 
 * @param pNode - linked list node to free
 * @param ppData - on return *ppData will contain the value of 
 *               the freed node's pData field
 * 
 * @return #llStatus_t
 */
llStatus_t freeLinkedListNode(llNode_t* pNode, void** ppData)
{
    llStatus_t rval = LL_OK;

    if((ppData == NULL) || (*ppData != NULL)) 
    {
        ERROR("invalid parameter");
        return LL_ERROR;
    }

    if(pNode != NULL) 
    {
        /* Retrieve the data */
        *ppData = pNode->pData;

        /* Reset members */
        pNode->pData = NULL;
        pNode->pNext = NULL;
        pNode->pPrev = NULL;

        /* Free the node structure */
        free(pNode);
    }
    else
    {
        /* If we were asked to free a NULL node, return a NULL pData */
        *ppData = NULL;
    }
    
    return rval;
}

/** 
 * Create new node to hold pData and add it to the head of 
 * pList 
 * 
 * @param pList - list to operate on
 * @param pData - data to add to list
 * 
 * @return #llStatus_t
 */
llStatus_t insertHead(llist_t* pList, void* pData)
{
    llStatus_t rval = LL_OK;

    llNode_t* pNode = NULL;

    if(pList == NULL)
    {
        ERROR("invalid parameter");
        return LL_ERROR;
    }

    do
    {
        /* Create new node to hold pData */
        pNode = newLinkedListNode(pData);
        if(pNode == NULL) 
        {
            ERROR("failed to create new linked list node");
            rval = LL_ERROR;
            break;
        }

        /* Check for empty list */
        if(pList->numElements == 0)
        {
            pList->pHead = pNode;
            pList->pTail = pNode;
        }
        else
        {
            pNode->pPrev = NULL;
            pNode->pNext = pList->pHead;

            pList->pHead->pPrev = pNode;

            pList->pHead = pNode;
        }

        pList->numElements += 1;

    } while(0);

    return rval;
}

/** 
 * Create new node to hold pData and add it to the tail of 
 * pList 
 * 
 * @param pList - list to operate on
 * @param pData - data to add to list
 * 
 * @return #llStatus_t
 */
llStatus_t insertTail(llist_t* pList, void* pData)
{
    llStatus_t rval = LL_OK;

    llNode_t* pNode = NULL;

    if(pList == NULL)
    {
        ERROR("invalid parameter");
        return LL_ERROR;
    }

    do
    {
        /* Create new node to hold pData */
        pNode = newLinkedListNode(pData);
        if(pNode == NULL) 
        {
            ERROR("failed to create new linked list node");
            rval = LL_ERROR;
            break;
        }

        /* Check for empty list */
        if(pList->numElements == 0)
        {
            pList->pHead = pNode;
            pList->pTail = pNode;
        }
        else
        {
            pNode->pPrev = pList->pTail;
            pNode->pNext = NULL;

            pList->pTail->pNext = pNode;

            pList->pTail = pNode;
        }

        pList->numElements += 1;

    } while(0);

    return rval;
}

/** 
 * Remove the head node of a list and retrieve the data it 
 * contains. Returns LL_ERROR if list is empty -- up to the 
 * caller to determine if there are any entries by checking 
 * pList->numElements.
 * 
 * @param pList - list to operate on
 * @param ppData - on return *ppData will contain the value of 
 *               the pData member of the removed head node
 * 
 * @return #llStatus_t
 */
llStatus_t removeHead(llist_t* pList, void** ppData)
{
    llStatus_t rval = LL_OK;

    llNode_t* pNode = NULL;

    if((pList == NULL) || (ppData == NULL) || (*ppData != NULL))
    {
        ERROR("invalid parameter");
        return LL_ERROR;
    }

    do
    {
        /* Is the list empty? */
        if(pList->numElements == 0) 
        {
            ERROR("empty linked list");
            rval = LL_ERROR;
            break;
        }

        /* Pop off the head node */
        pNode = pList->pHead;

        /* Set new head */
        if(pList->numElements == 1) 
        {
            pList->pHead = NULL;
            pList->pTail = NULL;
        }
        else
        {
            pList->pHead = pList->pHead->pNext;
            pList->pHead->pPrev = NULL;
        }

        /* Update element count */
        pList->numElements -= 1;

        pNode->pNext = NULL;
        pNode->pPrev = NULL;

        /* Free the node */
        rval = freeLinkedListNode(pNode, ppData);
        if(rval != LL_OK) 
        {
            ERROR("failed to free linked list node");
            break;
        }

        pNode = NULL;

    } while(0);

    return rval;
}

/** 
 * Remove the tail node of a list and retrieve the data it 
 * contains.  Returns LL_ERROR if list is empty -- up to the 
 * caller to determine if there are any entries by checking 
 * pList->numElements. 
 * 
 * @param pList - list to operate on
 * @param ppData - on return *ppData will contain the value of 
 *               the pData member of the removed tail node
 * 
 * @return #llStatus_t
 */
llStatus_t removeTail(llist_t* pList, void** ppData)
{
    llStatus_t rval = LL_OK;

    llNode_t* pNode = NULL;

    if((pList == NULL) ||(ppData == NULL) || (*ppData != NULL))
    {
        ERROR("invalid parameter");
        return LL_ERROR;
    }

    do
    {
        /* Is the list empty? */
        if(pList->numElements == 0) 
        {
            ERROR("empty linked list");
            rval = LL_ERROR;
            break;
        }

        /* Pop off the tail node */
        pNode = pList->pTail;

        /* Set new head */
        if(pList->numElements == 1) 
        {
            pList->pHead = NULL;
            pList->pTail = NULL;
        }
        else
        {
            pList->pTail = pList->pTail->pPrev;
            pList->pTail->pNext = NULL;
        }

        /* Update element count */
        pList->numElements -= 1;

        pNode->pNext = NULL;
        pNode->pPrev = NULL;

        /* Free the node */
        rval = freeLinkedListNode(pNode, ppData);
        if(rval != LL_OK) 
        {
            ERROR("failed to free linked list node");
            break;
        }

        pNode = NULL;

    } while(0);

    return rval;
}

/**
 * Looks through pList and sets *ppNode to point to the node for
 * which node->pData == the pData parameter.  If no node matches
 * pData, return LL_ERROR;
 * 
 * @param pList - The linked list to parse
 * @param pData - data to look for
 * @param ppNode - points to found node.
 * 
 * @return #llStatus_t
 */
llStatus_t findNode(llist_t* pList, void* pData, llNode_t** ppNode)
{
    if((pList == NULL) || (ppNode == NULL))
    {
        ERROR("invalid parameter");
        return LL_ERROR;
    }

    *ppNode = pList->pHead;

    while(*ppNode != NULL) 
    {
        if((*ppNode)->pData == pData) 
        {
            /* We've found the node */
            break;
        }

        *ppNode = (*ppNode)->pNext;
    }

    if(*ppNode == NULL) 
    {
        /* No matching node found */
        return LL_ERROR;
    }

	return LL_OK;
}

/** 
 * Allocates memory for a new message queue structure and 
 * initializes members. 
 * 
 * 
 * @return msgQueue_t* - pointer to newly allocated message 
 *         queue.  NULL on error.
 */
msgQueue_t* newMsgQueue()
{
    msgQueue_t* pQueue = NULL;

    do
    {
        pQueue = (msgQueue_t*)malloc(sizeof(msgQueue_t));	
        if(pQueue == NULL)
        {
            ERROR("error allocating message queue");
            break;
        }

        pQueue->pList = newLinkedList();
        if(pQueue->pList == NULL) 
        {
            ERROR("error creating linked list");
            freeMsgQueue(pQueue);
            pQueue = NULL;
            break;
        }

        if(pthread_mutex_init(&(pQueue->queueMutex), NULL) != 0)
        {
            ERROR("failed to initialize message queue mutex");
            freeLinkedList(pQueue->pList);
            freeMsgQueue(pQueue);
            pQueue = NULL;
            break;
        }

    } while(0);

    return pQueue;
}

/** 
 * Frees memory allocated for a given message queue.  Queue MUST 
 * be empty to be freed. 
 * 
 * @param pQueue - pointer to message queue to free
 * 
 * @return #llStatus_t
 */
llStatus_t freeMsgQueue(msgQueue_t* pQueue)
{
    llStatus_t rval = LL_OK;

    /* If pQueue == NULL do nothing */
    if(pQueue != NULL) 
    {
        do
        {
            /* Free the linked list */
            rval = freeLinkedList(pQueue->pList);

            if(rval != LL_OK) 
            {
                ERROR("could not free message queue linked list");
                break;
            }

            /* Delete the queue mutex */
            pthread_mutex_destroy(&(pQueue->queueMutex));            

            /* Free the message queue structure */
            free(pQueue);

        } while (0);
    }

    return rval;
}

/** 
 * Add message to tail of queue
 * 
 * @param pQueue - pointer to queue to operate on
 * @param pMessage - pointer to message to push onto queue
 * 
 * @return #llStatus_t
 */
llStatus_t pushMsg(msgQueue_t* pQueue, void* pMessage)
{
    llStatus_t rval = LL_OK;

    if((pQueue == NULL) || (pMessage == NULL)) 
    {
        ERROR("invalid parameter");
        return LL_ERROR;
    }

    /* Lock the queue mutex */
    pthread_mutex_lock(&(pQueue->queueMutex));

    do
    {
        /* Insert new node at tail of queue */
        rval = insertTail(pQueue->pList, pMessage);
        if(rval != LL_OK) 
        {
            ERROR("failed to insert node into queue");
            break;
        }

    } while(0);

    /* Unlock the queue mutex */
    pthread_mutex_unlock(&(pQueue->queueMutex));

    return rval;
}

/** 
 * Pop a message off of the head of the queue. Returns LL_ERROR 
 * if the queue is empty -- up to the caller to determine
 * if there are any messages using getMsgCount(). 
 * 
 * @param pQueue - pointer to queue to operate on
 * @param ppMessage - on return *ppMessage will point to the 
 *                  message pulled off of the queue
 * 
 * @return #llStatus_t
 */
llStatus_t popMsg(msgQueue_t* pQueue, void** ppMessage)
{
    llStatus_t rval = LL_OK;

    if((pQueue == NULL) || (ppMessage == NULL) || (*ppMessage != NULL)) 
    {
        ERROR("invalid parameter");
        return LL_ERROR;
    }

    /* Lock the queue mutex */
    pthread_mutex_lock(&(pQueue->queueMutex));

    do
    {
        /* Remove the head of the queue */
        rval = removeHead(pQueue->pList, ppMessage);

    } while(0);

    /* Unlock the queue mutex */
    pthread_mutex_unlock(&(pQueue->queueMutex));

    return rval;
}


/**
 * Get the number of messages currently in the queue
 * 
 * @param pQueue - pointer to queue to operate on
 * @param pCount - on return will point to the number of 
 *               messages in the queue
 * 
 * @return #llStatus_t
 */
llStatus_t getMsgCount(msgQueue_t* pQueue, int* pCount)
{
    llStatus_t rval = LL_OK;

    if((pQueue == NULL) || (pCount == NULL)) 
    {
        ERROR("invalid parameter");
        return LL_ERROR;
    }

    /* Lock the queue mutex */
    pthread_mutex_lock(&(pQueue->queueMutex));

    do
    {
        if(pQueue->pList == NULL) 
        {
            ERROR("null linked list");
            rval = LL_ERROR;
            break;
        }

        *pCount = pQueue->pList->numElements;

    } while(0);

    /* Unlock the queue mutex */
    pthread_mutex_unlock(&(pQueue->queueMutex));

    return rval;
}

#ifdef __cplusplus
}
#endif
