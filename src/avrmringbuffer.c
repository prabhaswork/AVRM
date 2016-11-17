/**
 * @file avrmringbuffer.c
 * @author Prasanth
 * @brief This file defines ring buffer functions required for recording
 *
 */

#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include "avrmringbuffer.h"

typedef struct
{
    unsigned int *item;
    unsigned int validItems;
    unsigned int numItems;
    unsigned int head;
    unsigned int tail;
    pthread_mutex_t mut;
    RingBufNotifyCb notifyCb;
    RingBufMemFreeCb memFreeCb;
    RingBufMemCopyCb memCopyCb;
}RingBuffer_t;

/**
 *@brief This function creates a ring buffer instance
 \param[out] bufHandle   Handle to the ring buffer instance
 \param[in]  numItems	 Number of items in the ring buffer
 \return        AVRM_SUCCESS     Success
 \return        AVRM_FAILURE      Failure
 */
eAVRM_RETURN RingBufCreate(RbufHandle_t *bufHandle, int numItems)
{
    printf("\n Creating ringbuffer");
    eAVRM_RETURN ret =  AVRM_SUCCESS;
    RingBuffer_t *ringBuf = (RingBuffer_t*)malloc(sizeof(RingBuffer_t));
    
    if(NULL == ringBuf)
    {
        ret =  AVRM_FAILURE;
        goto exit;
    }

    //usage of unsigned int may restrict it to work only in 32bit.
    //change to unsigned int* in all places in this file and in callers to make it more portable.
    ringBuf->item = (unsigned int*)malloc(numItems * sizeof(unsigned int*));
    if(NULL == ringBuf->item)
    {

        ret =  AVRM_FAILURE;
        goto exit;
    }

    //create mutex
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    if(pthread_mutex_init(&ringBuf->mut,&attr))
    {
        printf("Error in RingBuffer: pthread_mutex_lock\n");
    }

    ringBuf->validItems = 0;
    ringBuf->numItems = numItems;
    ringBuf->head = 0;
    ringBuf->tail = 0;
    ringBuf->notifyCb = NULL;
    ringBuf->memFreeCb = NULL;
    ringBuf->memCopyCb = NULL;
    *bufHandle = (unsigned int*)ringBuf;
exit:
    return ret;
}

/**
 *@brief This function destroys the ring buffer instance
 \param[in] bufHandle    Handle to the ring buffer instance
 \data[in] data    	 	 pointer to the data
 \return        AVRM_SUCCESS     Success
 \return        AVRM_FAILURE      Failure
 */
eAVRM_RETURN RingBufDestroy(RbufHandle_t bufHandle)
{
    eAVRM_RETURN ret =  AVRM_SUCCESS;
    RingBuffer_t *ringBuf = (RingBuffer_t*)bufHandle;
    unsigned int i = 0;	

    pthread_mutex_lock(&ringBuf->mut);
    for(i = 0; i < ringBuf->validItems; i++)
    {
		//printf("\n Freeing index %d", i);
	    //ring buf is full, free head, overwrite and increment
	    if(ringBuf->memFreeCb != NULL)
		    ringBuf->memFreeCb((void*)ringBuf->item[i]);
    }
    
    pthread_mutex_destroy (&ringBuf->mut);

    ringBuf->validItems = 0;
    ringBuf->head = 0;
    ringBuf->tail = 0;
    ringBuf->notifyCb = NULL;
    ringBuf->memFreeCb = NULL;
    ringBuf->memCopyCb = NULL;
    
    if(ringBuf)
    {
        if(ringBuf->item)
        {
            free(ringBuf->item);
        }
        free(ringBuf);
    }
    //free(ringBuf->item);
    //free(ringBuf);
}

/**
 *@brief This function writes the buffer to the ring buffer
 \param[in] bufHandle    Handle to the buffer
 \param[in] data    	 pointer to the data
 \return        AVRM_SUCCESS     Success
 \return        AVRM_FAILURE      Failure
 */
eAVRM_RETURN RingBufWrite(RbufHandle_t bufHandle, void *data)
{
    eAVRM_RETURN ret =  AVRM_SUCCESS;
    RingBuffer_t *ringBuf = (RingBuffer_t*)bufHandle;
	static int count =0;
	count++;
	//printf("Write Mutex lock count %d ring buf handle %d\n", count ,bufHandle);
    pthread_mutex_lock(&ringBuf->mut);
    
    if(ringBuf->validItems < ringBuf->numItems)
    {
        ringBuf->item[ringBuf->head] = (unsigned int)data;
        ringBuf->validItems++;

        if(ringBuf->validItems == ringBuf->numItems)
        {
            ringBuf->head = 0;
        }
        else
        {
            ringBuf->head++;
        }
    }
    else if(ringBuf->validItems == ringBuf->numItems)
    {
        //ring buf is full, free head, overwrite and increment
        if(ringBuf->memFreeCb != NULL)
            ringBuf->memFreeCb((void*)ringBuf->item[ringBuf->head]);

        ringBuf->item[ringBuf->head] = (unsigned int)data;

        if(ringBuf->head < (ringBuf->numItems-1))
        {
            ringBuf->head++;
        }
        else if (ringBuf->head == (ringBuf->numItems-1))
        {
            ringBuf->head = 0;
        }
        else
        {
            //Error
            ret =  AVRM_FAILURE;
            goto exit;
        }
    }
    else
    {
        //Invalid case
        ret =  AVRM_FAILURE;
        goto exit;
    }
/*
    if(ringBuf->notifyCb != NULL)
    {
        ringBuf->notifyCb(bufHandle);
        ringBuf->notifyCb = NULL;
    }*/
	//printf("Head %d\n",ringBuf->head);
exit:
    pthread_mutex_unlock(&ringBuf->mut);
	//printf("Write Mutex unlock\n");
    return ret;
}

/**
 *@brief This function reads the buffer
 \param[in] bufHandle    Handle to the buffer
 \param[out] data    	 pointer to the data
 \return        AVRM_SUCCESS     Success
 \return        AVRM_FAILURE      Failure
 */
eAVRM_RETURN RingBufRead(RbufHandle_t bufHandle,unsigned int *data)
{
    eAVRM_RETURN ret =  AVRM_SUCCESS;
    RingBuffer_t *ringBuf = (RingBuffer_t*)bufHandle;
	static int count =0;
	count++;
	//printf("Read Mutex lock count %d ring buf handle %d\n", count,bufHandle);
    if(pthread_mutex_lock(&ringBuf->mut))
    {
        printf("Error in RingBuffer: pthread_mutex_lock\n");
    }

    if(ringBuf->head == ringBuf->tail)
    {
        //nothing to read
        *data = (unsigned int)NULL;
    }
    else
    {
        if(ringBuf->tail >= ringBuf->numItems)
        {
            ret =  AVRM_FAILURE;
            goto exit;
        }

        *data = (unsigned int)ringBuf->memCopyCb((void*)ringBuf->item[ringBuf->tail]);

        if(*data != 0)
        {
            if(ringBuf->tail < (ringBuf->numItems-1) )
            {
                ringBuf->tail++;
            }
            else
            {
                ringBuf->tail = 0;
            }
        }
        else
        {
            ret =  AVRM_FAILURE;
            goto exit;
        }
    }
	//printf("Tail %d\n", ringBuf->tail);
exit:
    pthread_mutex_unlock(&ringBuf->mut);
	//printf("Read Mutex unlock\n");
    return ret;
}

/**
 *@brief This function reads the buffer in the head position of ring buffer
 \param[in] bufHandle    Handle to the buffer
 \param[out] data    	 pointer to the data
 \return        AVRM_SUCCESS     Success
 \return        AVRM_FAILURE      Failure
 */
eAVRM_RETURN RingBufReadHead(RbufHandle_t bufHandle,unsigned int *data)
{
    eAVRM_RETURN ret =  AVRM_SUCCESS;
    RingBuffer_t *ringBuf = (RingBuffer_t*)bufHandle;

    pthread_mutex_lock(&ringBuf->mut);

    if( (ringBuf->head-1) >= ringBuf->numItems)
    {
        //head is invalid
        *data = (unsigned int)NULL;
        ret = AVRM_FAILURE;
        goto exit;
    }

    *data = (unsigned int)ringBuf->memCopyCb((void*)ringBuf->item[ringBuf->head-1] );
exit:
    pthread_mutex_unlock(&ringBuf->mut);
    return ret;
}

/**
 *@brief This function sets the read position to the correct index.
 \param[in] bufHandle    Handle to the buffer
 \return        AVRM_SUCCESS     Success
 \return        AVRM_FAILURE      Failure
 */
eAVRM_RETURN RingBufSetReadPos(RbufHandle_t bufHandle)
{
    eAVRM_RETURN ret = AVRM_SUCCESS;
    RingBuffer_t *ringBuf = (RingBuffer_t*)bufHandle;
    
    pthread_mutex_lock(&ringBuf->mut);
    
    if(ringBuf->numItems == ringBuf->validItems)
    {
        if(ringBuf->head <  (ringBuf->numItems-1))
            ringBuf->tail = (ringBuf->head+2);
        else
            ringBuf->tail = 0;
    }
    else
    {
        ringBuf->tail = 0;
    }
    
    pthread_mutex_unlock(&ringBuf->mut);
    
    return ret;
}

/**
 *@brief This function registers the notification callback. This will be invoked
 *when a new buffer is written to the ring buffer.
 \param[in] bufHandle    Handle to the buffer
 \param[in] notifyCb    Function pointer for the Notification Callback
 \return        AVRM_SUCCESS     Success
 \return        AVRM_FAILURE      Failure
 */
eAVRM_RETURN RingBufRegNotifyAvailable(RbufHandle_t bufHandle, RingBufNotifyCb notifyCb)
{
    eAVRM_RETURN ret = AVRM_SUCCESS;
    RingBuffer_t *ringBuf = (RingBuffer_t*)bufHandle;

    pthread_mutex_lock(&ringBuf->mut);
    ringBuf->notifyCb = notifyCb;
    pthread_mutex_unlock(&ringBuf->mut);
    return ret;
}

/**
 *@brief This function unregisters notification callback which was set previously
 \param[in] bufHandle    Handle to the buffer
 \return        AVRM_SUCCESS     Success
 \return        AVRM_FAILURE      Failure
 */
eAVRM_RETURN RingBufUnegNotifyAvailable(RbufHandle_t bufHandle)
{
    eAVRM_RETURN ret = AVRM_SUCCESS;
    RingBuffer_t *ringBuf = (RingBuffer_t*)bufHandle;

    pthread_mutex_lock(&ringBuf->mut);
    ringBuf->notifyCb = NULL;
    pthread_mutex_unlock(&ringBuf->mut);
    return ret;
}

/**
 *@brief This function registers MemFree callback
 \param[in] bufHandle    Handle to the buffer
 \param[in] memFreeCb    Function pointer for the MemFree Callback
 \return        AVRM_SUCCESS     Success
 \return        AVRM_FAILURE      Failure
 */
eAVRM_RETURN RingBufRegMemFreeCb(RbufHandle_t bufHandle, RingBufMemFreeCb memFreeCb)
{
    eAVRM_RETURN ret = AVRM_SUCCESS;
    RingBuffer_t *ringBuf = (RingBuffer_t*)bufHandle;

    pthread_mutex_lock(&ringBuf->mut);
    ringBuf->memFreeCb = memFreeCb;
    pthread_mutex_unlock(&ringBuf->mut);

    return ret;
}

/**
 *@brief This function unregisters MemFree callback which was set previously
 \param[in] bufHandle    Handle to the buffer
 \return        AVRM_SUCCESS     Success
 \return        AVRM_FAILURE      Failure
 */
eAVRM_RETURN RingBufUnRegMemFreeCb(RbufHandle_t bufHandle)
{
    eAVRM_RETURN ret = AVRM_SUCCESS;
    RingBuffer_t *ringBuf = (RingBuffer_t*)bufHandle;

    pthread_mutex_lock(&ringBuf->mut);
    ringBuf->memFreeCb = NULL;
    pthread_mutex_unlock(&ringBuf->mut);

    return ret;
}

/**
 *@brief This function registers MemCopy callback
 \param[in] bufHandle    Handle to the buffer
 \param[in] memCopyCb    Function pointer for the Memcopy Callback
 \return        AVRM_SUCCESS     Success
 \return        AVRM_FAILURE      Failure
 */
eAVRM_RETURN RingBufRegMemCopyCb(RbufHandle_t bufHandle, RingBufMemCopyCb memCopyCb)
{
    eAVRM_RETURN ret = AVRM_SUCCESS;
    RingBuffer_t *ringBuf = (RingBuffer_t*)bufHandle;

    pthread_mutex_lock(&ringBuf->mut);
    ringBuf->memCopyCb = memCopyCb;
    pthread_mutex_unlock(&ringBuf->mut);

    return ret;    
}

/**
 *@brief This function unregisters MemCopy callback which was set previously
 \param[in] bufHandle    Handle to the buffer
 \return        AVRM_SUCCESS     Success
 \return        AVRM_FAILURE      Failure
 */
eAVRM_RETURN RingBufUnRegMemCopyCb(RbufHandle_t bufHandle)
{
    eAVRM_RETURN ret = AVRM_SUCCESS;
    RingBuffer_t *ringBuf = (RingBuffer_t*)bufHandle;

    pthread_mutex_lock(&ringBuf->mut);
    ringBuf->memCopyCb = NULL;
    pthread_mutex_unlock(&ringBuf->mut);

    return ret;    
}

/**
 *@brief This function returns number of valid items in ring buffer
 \param[out] numItems    number of valid items in ring buffer
 \return        AVRM_SUCCESS     Success
 \return        AVRM_FAILURE      Failure
 */
eAVRM_RETURN RingBufGetValidItems(RbufHandle_t bufHandle,unsigned int *numItems)
{
    eAVRM_RETURN ret = AVRM_SUCCESS;
    RingBuffer_t *ringBuf = (RingBuffer_t*)bufHandle;

    *numItems = ringBuf->validItems;
    return ret;
}
