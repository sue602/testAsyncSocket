
#ifndef __easy_async_h__
#define __easy_async_h__

#include <stdio.h> /* for size_t */
#include <stdarg.h> /* for va_list */
#include <sys/time.h> /* for struct timeval */
#include <sys/types.h>
#include "ae.h"
#include "async.h"

typedef int (easyBufferReadCallback)(struct redisContext*c);
typedef int (easyBufferWriteCallback)(struct redisContext*c,int *done);

typedef struct {
	easyBufferReadCallback *read_callback;
	easyBufferWriteCallback *write_callback;
}easy_buffer_callback;

void easyAeReadEvent(aeEventLoop *el, int fd, void *privdata, int mask);

void easyAeWriteEvent(aeEventLoop *el, int fd, void *privdata, int mask);

void easyAeAddRead(void *privdata);

void easyAeDelRead(void *privdata);

void easyAeAddWrite(void *privdata);

void easyAeDelWrite(void *privdata);

void easyAeCleanup(void *privdata);

int easyAeAttach(aeEventLoop *loop, redisAsyncContext *ac);


#endif
