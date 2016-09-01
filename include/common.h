/*
 * common.h
 *
 *  Created on: Aug 18, 2016
 *      Author: ltzd
 */

#ifndef INCLUDE_COMMON_H_
#define INCLUDE_COMMON_H_

//init function
void common_init();

//uninit function
void common_fini();

//
void * easy_malloc(uint32_t sz);

//
void easy_free(void * p);

//
void * easy_realloc(void * p,uint32_t sz);

#endif /* INCLUDE_COMMON_H_ */
