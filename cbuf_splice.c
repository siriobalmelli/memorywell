#include "cbuf_int.h"

/*
	Functions for splicing memory in and out of cbuf blocks.

	For background and Linus' approach re: splice() and vmsplice(), please see
	thread @ http://yarchive.net/comp/linux/splice.html
*/


/*	cbuf_splice_sz()

Returns the SIZE OF SPLICED DATA represented by a cbuf block, 
	whether the data is in the cbuf itself 
	or whether it is in a backing store and only tracked by the cbuf block.
	*/
size_t	cbuf_splice_sz(cbuf_t *b, uint32_t pos, int i)
{
	size_t ret;
	/* get base of block being pointed to */
	void *base = cbuf_offt(b, pos, i);

	/* if this cbuf has a backing store, block is a tracking stuct.
		Typecast and dereference.
		*/
	if (b->cbuf_flags & CBUF_P)
		ret = ((cbufp_t *)base)->data_len;

	/* Otherwise, data length is in the first 8B of the block itself.
		Typecast and dereference.
		*/
	else
		ret = *((size_t *)base);

	if (ret > cbuf_splice_max(b)) {
		Z_err("cbuf 0x%lx pos %d i %d thinks it's size is %ld. likely corrupt.", 
			(uint64_t)b, pos, i, ret);
		ret=0;
	}
	return ret;
}

/*	cbuf_splice_from_pipe()
Splice()s at most `size` bytes from `a_pipe[0]` into the cbuf block at `pos`, offset `i`.

In the case that cbuf has a backing store (the block only contains a cbufp_t):
	The amount of bytes to push is limited to 'blk_iov.iov_len'.
	The amount of bytes actually pushed is stored in the 'data_len' variable.
	data_len is returned but is never less than 0.

In the case of a regular cbuf:
	The amount of bytes actually pushed is written in the first 8B of 
		the cbuf block itself, aka `cbuf_head`.
	`cbuf_head` may be 0 but will be AT MOST the size of cbuf block (minus 8B).
	Returns `cbuf_head` - which on error will be 0, NOT -1(!).

In the case of a malloc()ed cbuf, the mechanics are the same but data is read()
	instead of splice()ed. TODO Robert: implement this case.
	*/
size_t	cbuf_splice_from_pipe(int fd_pipe_read, cbuf_t *b, uint32_t pos, int i, size_t size)
{
	if (!size)
		return 0;
	if (size > cbuf_splice_max(b))
		size = cbuf_splice_max(b);

	size_t *cbuf_head;
	loff_t temp_offset;
	int fd;

	/* splice params: backing store */
	if (b->cbuf_flags & CBUF_P) {
		cbufp_t *f = cbuf_offt(b, pos, i);

		/* params */
		temp_offset = f->blk_offset;
		cbuf_head = &f->data_len;
		fd = f->fd; /* fd is the backing store's fd */

	/* params: cbuf block itself as splice destination */
	} else {
		/* get head of buffer block, put offset from buf fd info cbuf_off */
		temp_offset = cbuf_lofft(b, pos, i, &cbuf_head);
		/* fd is the cbuf itself */
		fd = b->mmap_fd;
	}

	/* do splice */
	do {
		*cbuf_head = splice(fd_pipe_read, NULL, fd, &temp_offset, 
				size, SPLICE_F_NONBLOCK);
	} while ((*cbuf_head == 0 || *cbuf_head == -1) && errno == EWOULDBLOCK 
		/* the below are tested/executed only if we would block: */ 
		&& !CBUF_YIELD() /* don't spinlock */
		&& !(errno = 0)); /* resets errno ONLY if we will retry */
	

	/* if got error, reset to "nothing" */
	if (*cbuf_head == -1)
		*cbuf_head = 0;
	/* We could have spliced an amount LESS than requested.
	That is not an error: caller should check the return value
		and act accordingly.
		*/
	Z_err_if(*cbuf_head == 0, "*cbuf_head %ld; size %ld", *cbuf_head, size);
	//Z_err_if(*cbuf_head != size, "*cbuf_head %ld; size %ld", *cbuf_head, size);

	/* done */
	return *cbuf_head;
}

/*	cbuf_splice_to_pipe()
Reads `cbuf_head` (see `cbuf_splice_from_pipe()` above) @(pos +i).
Splices `*cbuf_head` bytes from the cbuf into `fd_pipe_write`.
`fd_pipe_write` MUST be a pipe, not a file or mmap'ed region.
If there is an error will return 0, not -1.

In the case where the cbuf was malloc()ed instead of mmap()ed, does a vmsplice()
	rather than a splice(). TODO Robert: implement.
	*/
size_t	cbuf_splice_to_pipe(cbuf_t *b, uint32_t pos, int i, int fd_pipe_write)
{
	int fd;
	size_t *cbuf_head;
	loff_t temp_offset;

	/* params: backing store */
	if (b->cbuf_flags & CBUF_P) {
		cbufp_t *f = cbuf_offt(b, pos, i);

		cbuf_head = &f->data_len;
		temp_offset = f->blk_offset;
		fd = f->fd;

	/* params: cbuf block itself contains data */
	} else {
		/* get offset and head */
		temp_offset = cbuf_lofft(b, pos, i, &cbuf_head);
		/* fd is cbuf itself */
		fd = b->mmap_fd;
	}

	/* no data, no copy */
	if (*cbuf_head == 0)
		return 0;
	if (*cbuf_head > cbuf_splice_max(b)) {
		Z_err("corrupt splice size of %ld", *cbuf_head);
		return 0;
	}
	/* Pull chunk from buffer.
		Could return -1 if dest. pipe is full.
		Have pipe empty before running this, then evacuate pipe.
		*/
	ssize_t temp;
	do {
		temp = splice(fd, &temp_offset, fd_pipe_write, 
				NULL, *cbuf_head, SPLICE_F_NONBLOCK);
	} while ((temp == 0 || temp == -1) && errno == EWOULDBLOCK 
		/* the below are tested/executed only if we would block: */ 
		&& !CBUF_YIELD() /* don't spinlock */
		&& !(errno = 0)); /* resets errno ONLY if we will retry */

	/* haz error? */
	if (temp == -1)
		temp = 0;
	Z_err_if(temp != *cbuf_head, "temp %ld; *cbuf_head %ld", temp, *cbuf_head);

	/* return */
	return temp;
}
