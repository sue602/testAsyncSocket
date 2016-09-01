/*
 * common.c
 *
 *  Created on: Aug 18, 2016
 *      Author: ltzd
 */
#include <stdlib.h>
#include <stdio.h>
#include "zmalloc.h"
#include "spinlock.h"
#include "adlist.h"
#include "common.h"

//easy_malloc,easy_realloc,size can't be grater than 2^24 - 1
typedef uint32_t common_uint32;
#define MAKE_PREFIX_SIZE(t,sz) ( (t<<24) | sz )
#define PRE_TYPE(makesize) ( (makesize>>24) & 0x000000ff )
#define PRE_SIZE(makesize) (makesize & 0x00ffffff )
#define POW2(n) (1<<(n+4)) //begin is BUF16 = 2^4

static short g_common_inited = 0;

const int MALLOC_NUMBERS = 1000;

const short CHAR_SIZE = sizeof(char);
const short COMMON_PRE_SIZE = sizeof(common_uint32);

enum buf_type{
	BUF16			= 0,
	BUF32			= 1,
	BUF64			= 2,
	BUF128			= 3,
	BUF256			= 4,
	BUF512			= 5,
	BUF1K 			= 6,
	BUF2K 			= 7,
	BUF4K 			= 8,
	BUF8K 			= 9,
	BUF16K 			= 10,
	BUF32K			= 11,
	BUF64K			= 12,
	BUF128K			= 13,
	BUF256K			= 14,
	BUF512K			= 15,
	BUF1024k		= 16,
	BUF2048K		= 17,
	BUF4096K		= 18,
	BUF8192K		= 19,
	BUF16384K		= 20,
	BUF_TYPE_MAX	= 21
};

struct common_buf{
	struct spinlock lock;
	list * data;
};

static struct common_buf g_bufs[BUF_TYPE_MAX];

void common_init()
{
	if(1 == g_common_inited)
	{
		printf("common init already \n");
		return;
	}

	g_common_inited = 1;

	short i=0;
	for(;i<BUF_TYPE_MAX;i++)
	{
		g_bufs[i].data = listCreate();
		SPIN_INIT(&g_bufs[i])
		int j = 0;
		int weight = MALLOC_NUMBERS / (i+1) ;
		for(;j<weight;j++)
		{
			int sz = POW2(i);
			void * allocbuf = zmalloc(sz);//2^N
			*( (common_uint32*)allocbuf) = MAKE_PREFIX_SIZE(i,sz);
			listAddNodeTail(g_bufs[i].data, allocbuf );
		}
		printf("buf[%d],length=%d weight=%d\n",i,listLength(g_bufs[i].data),weight);
	}
	printf("common init ok\n");
}

void common_fini()
{
	short i=0;
	for(;i<BUF_TYPE_MAX;i++)
	{
		SPIN_LOCK(&g_bufs[i])
		//设置释放函数
		g_bufs[i].data->free = zfree;
		//release
		listRelease(g_bufs[i].data);
		//unlock and destroy
		SPIN_UNLOCK(&g_bufs[i])
		SPIN_DESTROY(&g_bufs[i])
	}
	g_common_inited = 0;
	printf("common_fini finish\n");
}

static enum buf_type _check_sz(uint32_t sz)
{
	if(sz > POW2(BUF_TYPE_MAX) )
	{
		printf("check buf size error,size=%d\n",sz);
		exit(0);
	}
	enum buf_type buft = BUF16;
	unsigned int test_sz = POW2(buft);
	do
	{
		if(test_sz >= sz)
			break;
		buft = buft + 1;
		test_sz = POW2(buft);
	}while(1);
	return buft;
}

void * easy_malloc(uint32_t sz)
{
	enum buf_type buft = _check_sz(sz+COMMON_PRE_SIZE);//预先找最合适大小

	SPIN_LOCK(&g_bufs[buft])

	void * buf = NULL;
	if(0 == listLength(g_bufs[buft].data))
	{
		printf("not enought ======sz =%d  type=%d\n",sz,buft);
		unsigned int malloc_sz = POW2(buft);
		void * allocbuf = zmalloc(malloc_sz);//2^N
		listAddNodeTail(g_bufs[buft].data,allocbuf);
	}
	listNode * node = listFirst(g_bufs[buft].data);
	buf = node->value;
	size_t len = zmalloc_size(buf);
	*( (common_uint32*) (buf+len-COMMON_PRE_SIZE) ) = MAKE_PREFIX_SIZE(buft,sz);
	if(NULL == buf)
	{
		printf("easy malloc error ,size =%d\n",sz);
		exit(0);
	}
	listDelNode(g_bufs[buft].data,node);
	SPIN_UNLOCK(&g_bufs[buft])
	// printf("easy malloc %p ,type=%d ,sz=%d make int=%d len=%d\n",buf,buft,sz,MAKE_PREFIX_SIZE(buft,sz),len);
	return buf;
}

void easy_free(void * p)
{
	size_t len = zmalloc_size(p);
	common_uint32 pre_data = * ( (common_uint32*)(p+len-COMMON_PRE_SIZE) );
	short type = PRE_TYPE(pre_data);
	size_t oldsize = PRE_SIZE(pre_data);
	if(type < 0 || type >=BUF_TYPE_MAX )
	{
		printf("easy free error,size=%d,p=%p",type,p);
		exit(0);
	}
	enum buf_type buft = type ;
	printf("easy_free ==========p=%p,sz =%d  type=%d\n",p,oldsize,buft);
	SPIN_LOCK(&(g_bufs[buft]))

	// printf("easy free %p ,size=%d,type=%d pre_data=%d len=%d\n",p,oldsize,type,pre_data,len);
	//printf("before free=%d\n",listLength(g_bufs[buft].data));

//	listAddNodeHead(g_bufs[buft].data,p);
	listAddNodeTail(g_bufs[buft].data,p);

	//printf("free add=%d\n\n",listLength(g_bufs[buft].data));

	SPIN_UNLOCK(&(g_bufs[buft]))
}

void * easy_realloc(void * p,uint32_t sz)
{
	if (NULL == p && sz > 0)
	{
		return easy_malloc(sz);
	}

	if(0 == sz)
	{
		easy_free(p);
		return NULL;
	}

	size_t len = zmalloc_size(p);
	common_uint32 pre_data = * ( (common_uint32*)(p+len-COMMON_PRE_SIZE) );
	short type = PRE_TYPE(pre_data);
	size_t oldsize = PRE_SIZE(pre_data);
	if(type < 0 || type >=BUF_TYPE_MAX )
	{
		printf("easy free error,size=%d,p=%p",type,p);
		exit(0);
	}
	enum buf_type buft = type ;

	unsigned int malloc_sz = POW2(buft);

	enum buf_type newbuft = _check_sz(sz+COMMON_PRE_SIZE);//预先找最合适大小

	if(buft < newbuft )
	{
		printf("bigger=================p=%p,oldsize=%d,sz=%d,oldsize_pow2=%d\n type=%d ,pre_data=%d,len=%d\n",p,oldsize,sz,malloc_sz,type,pre_data,len );
		void * oldp = p;
		void * allocbuf = easy_malloc(sz);
		memcpy(allocbuf,p,oldsize);//copy data
		easy_free(oldp);//free p
		// printf("easy free ok=\n");
		p = allocbuf;
	}
	else
	{
		*( (common_uint32*) (p+len-COMMON_PRE_SIZE) ) = MAKE_PREFIX_SIZE(buft,sz);
	}
	return p;
}
