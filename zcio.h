#ifndef ZCIO_H
#define ZCIO_H

#include <stdint.h>

#include "cbuf.h"
#include "src/zed_dbg.h"


#include "sbfu.h" /* For backing store operations: cbufp_ only
			TODO: put ONLY in ZCIO.
			*/


/*
	CBUF_P

TODO: evaluate the idea of cbuf's ALWAYS having a backing store 
	(whether it be malloc()ed or mmap()ed).
	fuck it - this will be rewritten
*/
typedef struct {
	/* backing store variables: identical values in all blocks of a cbuf_p */
	int		fd;
	int		pad_int;
	struct iovec	iov;
	/* block-specific variables: different from block to block */
	uint64_t	blk_id;
	struct iovec	blk_iov;
	loff_t		blk_offset;
	size_t		data_len;
}__attribute__ ((packed))	cbufp_t;


/* splice
TODO: move to ZCIO
*/
Z_INL_FORCE size_t cbuf_splice_max(cbuf_t *b)
{
	/* if buffer has a backing store, get length of one of the blocks */
	if (b->cbuf_flags & CBUF_P)
		return ((cbufp_t *)b->buf)->blk_iov.iov_len;

	/* if not, subtract the size of a header from `sz_obz` and return this */
	return cbuf_sz_obj(b) - sizeof(size_t);
}
size_t	cbuf_blk_data_len(cbuf_t *b, uint32_t pos, int i);
int	cbuf_blk_set_data_len(cbuf_t *b, uint32_t pos, int i, size_t len);
size_t	cbuf_splice_from_pipe(int fd_pipe_read, cbuf_t *b, uint32_t pos, int i, size_t size);
size_t	cbuf_splice_to_pipe_sub(cbuf_t *b, uint32_t pos, int i, int fd_pipe_write, 
				loff_t sub_offt, size_t sub_len);
Z_INL_FORCE size_t cbuf_splice_to_pipe(cbuf_t *b, uint32_t pos, int i, int fd_pipe_write)
{
	return cbuf_splice_to_pipe_sub(b, pos, i, fd_pipe_write, 0, 0);
}

/*	cbuf_offt()
Deliver the memory address at the beginning of the nth in a 
	contiguous set of buffer blocks which starts at 'pos'.
The contiguous set of buffer blocks may exist partly at the end of the
	buffer memory block, and the rest of the way starting at the beginning.
This function exists to hide the masking necessary to roll over from the end to
	the beginning of the buffer.
	*/
Z_INL_FORCE void *cbuf_offt(cbuf_t *buf, uint32_t start_pos, uint32_t n)
{
	start_pos += n << buf->sz_bitshift_; /* purrformance */
	return buf->buf + (start_pos & buf->overflow_);
}

/*	cbuf_lofft()
Deliver the OFFSET between 'buf->buf' and the beginning of the nth in a
	contiguous set of blocks which starts at 'pos'.

This offset value is useful when calling splice().

Also, point '*data_len' to the last 8B of the block.
'data_len' will hold e.g.: the results of a splice() call.

not sure if I was supposed to have cbuf_lofft in here or if that was
one we I should delete... argh
*/

#endif //ZCIO_H
