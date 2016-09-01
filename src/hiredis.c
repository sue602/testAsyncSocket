/*
 * Copyright (c) 2009-2011, Salvatore Sanfilippo <antirez at gmail dot com>
 * Copyright (c) 2010-2011, Pieter Noordhuis <pcnoordhuis at gmail dot com>
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Redis nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "fmacros.h"
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <ctype.h>
#include "net.h"

#include "hiredis.h"

void __redisSetError(redisContext *c, int type, const char *str) {
    size_t len;

    c->err = type;
    if (str != NULL) {
        len = strlen(str);
        len = len < (sizeof(c->errstr)-1) ? len : (sizeof(c->errstr)-1);
        memcpy(c->errstr,str,len);
        c->errstr[len] = '\0';
    } else {
        /* Only REDIS_ERR_IO may lack a description! */
        assert(type == REDIS_ERR_IO);
        strerror_r(errno,c->errstr,sizeof(c->errstr));
    }
}

static redisContext *redisContextInit(void) {
    redisContext *c;

    c = calloc(1,sizeof(redisContext));
    if (c == NULL)
        return NULL;

    c->err = 0;
    c->errstr[0] = '\0';
    c->obuf = sdsempty();

    return c;
}

void redisFree(redisContext *c) {
    if (c->fd > 0)
        close(c->fd);
    if (c->obuf != NULL)
        sdsfree(c->obuf);
    free(c);
}

int redisFreeKeepFd(redisContext *c) {
	int fd = c->fd;
	c->fd = -1;
	redisFree(c);
	return fd;
}

/* Connect to a Redis instance. On error the field error in the returned
 * context will be set to the return value of the error function.
 * When no set of reply functions is given, the default set will be used. */
redisContext *redisConnect(const char *ip, int port) {
    redisContext *c;

    c = redisContextInit();
    if (c == NULL)
        return NULL;

    c->flags |= REDIS_BLOCK;
    redisContextConnectTcp(c,ip,port,NULL);
    return c;
}

redisContext *redisConnectWithTimeout(const char *ip, int port, const struct timeval tv) {
    redisContext *c;

    c = redisContextInit();
    if (c == NULL)
        return NULL;

    c->flags |= REDIS_BLOCK;
    redisContextConnectTcp(c,ip,port,&tv);
    return c;
}

redisContext *redisConnectNonBlock(const char *ip, int port) {
    redisContext *c;

    c = redisContextInit();
    if (c == NULL)
        return NULL;

    c->flags &= ~REDIS_BLOCK;
    redisContextConnectTcp(c,ip,port,NULL);
    return c;
}

redisContext *redisConnectBindNonBlock(const char *ip, int port,
                                       const char *source_addr) {
    redisContext *c = redisContextInit();
    c->flags &= ~REDIS_BLOCK;
    redisContextConnectBindTcp(c,ip,port,NULL,source_addr);
    return c;
}

redisContext *redisConnectUnix(const char *path) {
    redisContext *c;

    c = redisContextInit();
    if (c == NULL)
        return NULL;

    c->flags |= REDIS_BLOCK;
    redisContextConnectUnix(c,path,NULL);
    return c;
}

redisContext *redisConnectUnixWithTimeout(const char *path, const struct timeval tv) {
    redisContext *c;

    c = redisContextInit();
    if (c == NULL)
        return NULL;

    c->flags |= REDIS_BLOCK;
    redisContextConnectUnix(c,path,&tv);
    return c;
}

redisContext *redisConnectUnixNonBlock(const char *path) {
    redisContext *c;

    c = redisContextInit();
    if (c == NULL)
        return NULL;

    c->flags &= ~REDIS_BLOCK;
    redisContextConnectUnix(c,path,NULL);
    return c;
}

redisContext *redisConnectFd(int fd) {
    redisContext *c;

    c = redisContextInit();
    if (c == NULL)
        return NULL;

    c->fd = fd;
    c->flags |= REDIS_BLOCK | REDIS_CONNECTED;
    return c;
}

/* Set read/write timeout on a blocking socket. */
int redisSetTimeout(redisContext *c, const struct timeval tv) {
    if (c->flags & REDIS_BLOCK)
        return redisContextSetTimeout(c,tv);
    return REDIS_ERR;
}

/* Enable connection KeepAlive. */
int redisEnableKeepAlive(redisContext *c) {
    if (redisKeepAlive(c, REDIS_KEEPALIVE_INTERVAL) != REDIS_OK)
        return REDIS_ERR;
    return REDIS_OK;
}

/* Use this function to handle a read event on the descriptor. It will try
 * and read some bytes from the socket and feed them to the reply parser.
 *
 * After this function is called, you may use redisContextReadReply to
 * see if there is a reply available. */
int redisBufferRead(redisContext *c) {
    char buf[1024*16];
    int nread;

    /* Return early when the context has seen an error. */
    if (c->err)
        return REDIS_ERR;

    nread = read(c->fd,buf,sizeof(buf));
    if (nread == -1) {
        if ((errno == EAGAIN && !(c->flags & REDIS_BLOCK)) || (errno == EINTR)) {
            /* Try again later */
        } else {
            __redisSetError(c,REDIS_ERR_IO,NULL);
            return REDIS_ERR;
        }
    } else if (nread == 0) {
        __redisSetError(c,REDIS_ERR_EOF,"Server closed the connection");
        return REDIS_ERR;
    } else {

    }
    return REDIS_OK;
}

/* Write the output buffer to the socket.
 *
 * Returns REDIS_OK when the buffer is empty, or (a part of) the buffer was
 * succesfully written to the socket. When the buffer is empty after the
 * write operation, "done" is set to 1 (if given).
 *
 * Returns REDIS_ERR if an error occured trying to write and sets
 * c->errstr to hold the appropriate error string.
 */
int redisBufferWrite(redisContext *c, int *done) {
    int nwritten;

    /* Return early when the context has seen an error. */
    if (c->err)
        return REDIS_ERR;

    if (sdslen(c->obuf) > 0) {
        nwritten = write(c->fd,c->obuf,sdslen(c->obuf));
        if (nwritten == -1) {
            if ((errno == EAGAIN && !(c->flags & REDIS_BLOCK)) || (errno == EINTR)) {
                /* Try again later */
            } else {
                __redisSetError(c,REDIS_ERR_IO,NULL);
                return REDIS_ERR;
            }
        } else if (nwritten > 0) {
            if (nwritten == (signed)sdslen(c->obuf)) {
                sdsfree(c->obuf);
                c->obuf = sdsempty();
            } else {
                sdsrange(c->obuf,nwritten,-1);
            }
        }
    }
    if (done != NULL) *done = (sdslen(c->obuf) == 0);
    return REDIS_OK;
}
