/*
 ============================================================================
 Name        : testAsyncSocket.c
 Author      : vincent
 Version     :
 Copyright   : Your copyright notice
 Description : Hello World in C, Ansi-style
 ============================================================================
 */

#include <stdio.h>
#include <stdlib.h>

#include "hiredis.h"
#include "async.h"
#include "adapters/ae.h"

/* Put event loop in the global scope, so it can be explicitly stopped */
static aeEventLoop *loop;

void getCallback(redisAsyncContext *c, void *r, void *privdata) {

    printf("argv[%s]\n", (char*)privdata);

    /* Disconnect after receiving the reply to GET */
    redisAsyncDisconnect(c);
}

void connectCallback(const redisAsyncContext *c, int status) {
	printf("connectCallback  status=== %d\n",status);
    if (status != REDIS_OK) {
        printf("Error: %s\n", c->errstr);
        aeStop(loop);
        return;
    }

    printf("Connected...\n");
}

void disconnectCallback(const redisAsyncContext *c, int status) {
	printf("disconnectCallback  status=== %d\n",status);
    if (status != REDIS_OK) {
        printf("Error: %s\n", c->errstr);
        aeStop(loop);
        return;
    }

    printf("Disconnected...\n");
    aeStop(loop);
}

int main (int argc, char **argv) {
    signal(SIGPIPE, SIG_IGN);

    redisAsyncContext *c = redisAsyncConnect("127.0.0.1", 6379);

    if (c->err) {
        /* Let *c leak for now... */
        printf("Error: %s\n", c->errstr);
        return 1;
    }

    loop = aeCreateEventLoop(64);
    redisAeAttach(loop, c);
    redisAsyncSetConnectCallback(c,connectCallback);
    redisAsyncSetDisconnectCallback(c,disconnectCallback);

    aeMain(loop);
    return 0;
}
