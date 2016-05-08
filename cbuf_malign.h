#ifndef cbuf_malign_h_
#define cbuf_malign_h_

#include "zed_dbg.h"
#include "string.h"

extern uint64_t cbuf_malign_mask;

#ifdef CBUF_MEM_INTEGRITY
Z_INL_FORCE void *cbuf_malloc(size_t size)
{
	/* get malloc output */
	void *ret = malloc(size);
	/* sanitize OS malloc header */
	ret -= 1;
	ret = (void*)((uint64_t)ret & cbuf_malign_mask);
	/* return */
	return ret +1;
}

Z_INL_FORCE void *cbuf_calloc(size_t count, size_t size)
{
	size_t sz = count * size;
	void *ret = cbuf_malloc(sz);
	memset(ret, 0x0, sz);
	return ret;
}

#else

#define cbuf_malloc(x) malloc(x)
#define cbuf_calloc(x, y) calloc(x, y)

#endif

#endif /* cbuf_malign_h_ */
