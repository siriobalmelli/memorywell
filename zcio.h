#ifndef ZCIO_H
#define ZCIO_H

#include <stdint.h>

#include "cbuf.h"
#include "zed_dbg.h"


#ifndef _GNU_SOURCE
	#define _GNU_SOURCE
	/* mkostemp, splice */
#endif
#include <fcntl.h> /* splice() */
#include "sbfu.h" /* For backing store operations: cbufp_ only
			TODO: put ONLY in ZCIO.
			*/


/*
	CBUF_P

TODO: evaluate the idea of cbuf's ALWAYS having a backing store 
	(whether it be malloc()ed or mmap()ed).
	fuck it - this will be rewritten
*/
struct zcio {
	/* backing store variables: identical values in all blocks of a zcio_p */
	int		fd;
	int		pad_int;
	struct iovec	iov;
	/* block-specific variables: different from block to block */
	uint64_t	blk_id;
	struct iovec	blk_iov;
	loff_t		blk_offset;
	size_t		data_len;
}__attribute__ ((packed));


/* splice
TODO: move to ZCIO
*/
Z_INL_FORCE size_t zcio_splice_max(cbuf_t *b)
{
	/* if buffer has a backing store, get length of one of the blocks */
	if (b->cbuf_flags)
		return ((struct zcio *)b->buf)->blk_iov.iov_len;

	/* if not, subtract the size of a header from `sz_obz` and return this */
	return cbuf_sz_obj(b) - sizeof(size_t);
}

size_t	zcio_blk_data_len(cbuf_t *b, uint32_t pos, int i);
int	zcio_blk_set_data_len(cbuf_t *b, uint32_t pos, int i, size_t len);
size_t	zcio_splice_from_pipe(int fd_pipe_read, cbuf_t *b, uint32_t pos, int i, size_t size);
size_t	zcio_splice_to_pipe_sub(cbuf_t *b, uint32_t pos, int i, int fd_pipe_write, 
				loff_t sub_offt, size_t sub_len);
Z_INL_FORCE size_t zcio_splice_to_pipe(cbuf_t *b, uint32_t pos, int i, int fd_pipe_write)
{
	return zcio_splice_to_pipe_sub(b, pos, i, fd_pipe_write, 0, 0);
}

/*	zcio_offt()
Deliver the memory address at the beginning of the nth in a 
	contiguous set of buffer blocks which starts at 'pos'.
The contiguous set of buffer blocks may exist partly at the end of the
	buffer memory block, and the rest of the way starting at the beginning.
This function exists to hide the masking necessary to roll over from the end to
	the beginning of the buffer.
	*/
Z_INL_FORCE void *zcio_offt(cbuf_t *buf, uint32_t start_pos, uint32_t n)
{
	start_pos += n << buf->sz_bitshift_; /* purrformance */
	return buf->buf + (start_pos & buf->overflow_);
}

//this creates a buffer with accounting structures point to data elsewhere
cbuf_t *cbuf_create_p1(uint32_t obj_sz, uint32_t obj_cnt, char *map_dir);


Z_INL_FORCE cbuf_t *cbuf_create_p(uint32_t obj_sz, uint32_t obj_cnt)
	{ return cbuf_create_p1(obj_sz, obj_cnt, NULL); }

#endif //ZCIO_H
