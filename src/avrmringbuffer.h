#ifndef _AVRMRINGBUFFER_H_
#define _AVRMRINGBUFFER_H_

#include "avrm_prv.h"

/** Ringbuffer handle type definition */
typedef unsigned int* RbufHandle_t;


typedef void (*RingBufMemFreeCb)(void *ptr);
typedef void* (*RingBufMemCopyCb)(void *ptr);
typedef eAVRM_RETURN (*RingBufNotifyCb)(RbufHandle_t bufHandle);

eAVRM_RETURN RingBufCreate(RbufHandle_t *bufHandle, int numItems);
eAVRM_RETURN RingBufDestroy(RbufHandle_t bufHandle);
eAVRM_RETURN RingBufWrite(RbufHandle_t bufHandle, void *data);
eAVRM_RETURN RingBufRead(RbufHandle_t bufHandle,unsigned int *data);
eAVRM_RETURN RingBufReadHead(RbufHandle_t bufHandle,unsigned int *data);
eAVRM_RETURN RingBufSetReadPos(RbufHandle_t bufHandle);
//eAVRM_RETURN RingBufRegNotifyAvailable(RbufHandle_t bufHandle, RingBufNotifyCb notifyCb);
eAVRM_RETURN RingBufUnegNotifyAvailable(RbufHandle_t bufHandle);
eAVRM_RETURN RingBufRegMemFreeCb(RbufHandle_t bufHandle, RingBufMemFreeCb memFreeCb);
eAVRM_RETURN RingBufUnRegMemFreeCb(RbufHandle_t bufHandle);
eAVRM_RETURN RingBufRegMemCopyCb(RbufHandle_t bufHandle, RingBufMemCopyCb memCopyCb);
eAVRM_RETURN RingBufUnRegMemCopyCb(RbufHandle_t bufHandle);
eAVRM_RETURN RingBufGetValidItems(RbufHandle_t bufHandle, unsigned int *numItems);
#endif

