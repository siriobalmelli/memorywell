#include "cbuf_int.h"

/*
	Functions for splicing memory in and out of cbuf blocks.

	For background and Linus' approach re: splice() and vmsplice(), please see
	thread @ http://yarchive.net/comp/linux/splice.html
*/


/*	cbuf_blk_data_len()

Returns the SIZE OF USABLE DATA in a cbuf block.
Typically, this is set when splice()ing data into the block,
	or by calling _set_data_len() directly.
returns data_len or -1 on error;
	*/
size_t	cbuf_blk_data_len(cbuf_t *b, uint32_t pos, int i)
{
	size_t ret;

	/* If this cbuf has a backing store, block is a tracking stuct. */
	if (b->cbuf_flags & CBUF_P) {
		cbufp_t *f = cbuf_offt(b, pos, i);
		ret = f->data_len;

	/* Otherwise, data length is in the first 8B of the block itself.
		Typecast and dereference.
		*/
	} else {
		size_t *data_len = NULL;
		cbuf_lofft(b, pos, i, &data_len);
		ret = *data_len;
	}

	if (ret > cbuf_splice_max(b)) {
		Z_err("cbuf 0x%lx pos %d i %d thinks it's size is %ld. likely corrupt.", 
			(uint64_t)b, pos, i, ret);
		ret=0;
	}
	return ret;
}

/*	cbuf_blk_set_data_len()
Explicitly set the size of "usable" data in a cbuf.
Using this function, caller can directly control 
	e.g.: how many bytes will be splice()d out of this cbuf block.

returns 0 on success.
	*/
int	cbuf_blk_set_data_len(cbuf_t *b, uint32_t pos, int i, size_t data_len)
{
	int err_cnt = 0;
	Z_die_if(!b, "args");
	if (b->cbuf_flags & CBUF_P) {
		cbufp_t *f = cbuf_offt(b, pos, i);
		f->data_len = data_len;
	} else {
		size_t *data_len_buf = NULL;
		cbuf_lofft(b, pos, i, &data_len_buf);
		*data_len_buf = data_len;
	}

out:
	return err_cnt;
}

/*	cbuf_splice_from_pipe()
Splice()s at most 'size' bytes from 'a_pipe[0]' into the cbuf block at 'pos', offset 'i'.
Will not splice more than cbuf_splice_max() bytes.

returns 'data_len' == nr. of bytes moved.
'data_len' may be less than 'size', and is 0 on error.

'data_len' is saved WITH the block, and can be retrieved with cbuf_blk_data_len().

NOTE that if 'buf' is CBUF_MALLOC, the mechanics are identical save that 
	the data is read() instead of splice()ed.
	*/
size_t	cbuf_splice_from_pipe(int fd_pipe_read, cbuf_t *b, uint32_t pos, int i, size_t size)
{
	size_t *data_len = NULL;
	loff_t temp_offset = 0;
	int fd = 0;

	/* splice params: backing store */
	if (b->cbuf_flags & CBUF_P) {
		cbufp_t *f = cbuf_offt(b, pos, i);
		temp_offset = f->blk_offset;
		data_len = &f->data_len;
		fd = f->fd; /* fd is the backing store's fd */

	/* params: cbuf block itself as splice destination */
	} else {
		temp_offset = cbuf_lofft(b, pos, i, &data_len); /* sets data_len */
		fd = b->mmap_fd; /* fd is the cbuf itself */
	}

	/* validate size of requested splice operation */
	if (!size) {
		*data_len = 0;
		return 0;
	}
	 if (size > cbuf_splice_max(b))
		size = cbuf_splice_max(b);

	/* read() if data is going a malloc()ed 'buf', otherwise splice() */
	do {
		if (b->cbuf_flags & CBUF_MALLOC && !(b->cbuf_flags & CBUF_P))
			*data_len = read(fd_pipe_read, b->buf + temp_offset, size);
		else
			*data_len = splice(fd_pipe_read, NULL, fd, &temp_offset, 
				size, SPLICE_F_NONBLOCK);
	/* ... notice we don't loop on a PARTIAL read/splice */
	//} while ((*data_len  == 0 || *data_len == -1) && errno == EWOULDBLOCK 
	} while ((*data_len == -1) && errno == EWOULDBLOCK 
		/* the below are tested/executed only if we would block: */ 
		&& !CBUF_YIELD() /* don't spinlock */
		&& !(errno = 0)); /* resets errno ONLY if we will retry */
	

	/* if got error, reset to "nothing" */
	if (*data_len == -1) {
		Z_err("splice size=%ld", size);
		*data_len = 0;
	}
	/* We could have spliced an amount LESS than requested.
	That is not an error: caller should check the return value
		and act accordingly.
		*/
	Z_err_if(*data_len == 0, "*data_len %ld; size %ld", *data_len, size);
	//Z_err_if(*cbuf_head != size, "*cbuf_head %ld; size %ld", *cbuf_head, size);

	/* done */
	return *data_len;
}

/*	cbuf_splice_to_pipe()
Reads 'data_len' from the block described by 'pos' and 'i'.
See cbuf_blk_data_len() for details on where this is located physically.

splice()s 'data_len' bytes from block into 'fd_pipe_write'.
`fd_pipe_write` MUST be a pipe, not a file or mmap'ed region.

returns nr. of bytes spliced, 0 on error.

Note: if CBUF_MMAP, vmsplice() is used.
	*/
size_t	cbuf_splice_to_pipe_sub(cbuf_t *b, uint32_t pos, int i, int fd_pipe_write, 
				loff_t sub_offt, size_t sub_len)
{
	int fd;
	size_t *data_len;
	loff_t temp_offset;
	struct iovec iov;

	/* params: backing store */
	if (b->cbuf_flags & CBUF_P) {
		cbufp_t *f = cbuf_offt(b, pos, i);
		data_len = &f->data_len;
		temp_offset = f->blk_offset;
		fd = f->fd; /* fd is backing store */

	/* params: cbuf block itself contains data */
	} else {
		temp_offset = cbuf_lofft(b, pos, i, &data_len);
		fd = b->mmap_fd; /* fd is cbuf itself */
	}

	/* sanity */
	if (*data_len == 0)
		return 0;
	if (*data_len > cbuf_splice_max(b)) {
		Z_err("corrupt splice size of %ld, max is %ld", 
			*data_len, cbuf_splice_max(b));
		return 0;
	}

	/* sub-block? */
	if (sub_len) {
		if (sub_len > (int64_t)*data_len - sub_offt) {
			Z_err("bad sub-block request: len %ld @offt %ld > *data_len %ld",
				sub_len, sub_offt, *data_len);
			return 0;
		}
		data_len = &sub_len;
		temp_offset += sub_offt;
	}

	/* Pull chunk from buffer.
	Could return -1 if destination pipe is full.
	Have pipe empty before running this, then evacuate pipe.
		*/
	ssize_t temp;
	do {
		/* if 'buf' was malloc()ed, we must vmsplice() */
		if (b->cbuf_flags & CBUF_MALLOC && !(b->cbuf_flags & CBUF_P)) {
			/* set vmsplice-specific variables */
			iov.iov_base = b->buf + temp_offset;
			iov.iov_len = *data_len;
			temp = vmsplice(fd_pipe_write, &iov, 1, SPLICE_F_GIFT);

		/* all other cases: splice() */
		} else  {
			temp = splice(fd, &temp_offset, fd_pipe_write, 
				NULL, *data_len, SPLICE_F_NONBLOCK);
		}
	} while ((temp == -1) && errno == EWOULDBLOCK 
		/* the below are tested/executed only if we would block: */ 
		&& !CBUF_YIELD() /* don't spinlock */
		&& !(errno = 0)); /* resets errno ONLY if we will retry */

	/* haz error? */
	if (temp == -1) {
		temp = 0;
		Z_err("data_len=%lu", *data_len);
	}
	//Z_err_if(temp != *data_len, "temp %ld; *data_len %ld", temp, *data_len);

	/* return */
	return temp;
}
