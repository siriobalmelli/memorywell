#include "zcio.h"
#include "cbuf_int.h"

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
	/* mkostemp, splice */
#endif

#include <fcntl.h> /* splice() */

/*
	Functions for splicing memory in and out of cbuf blocks.

	For background and Linus' approach re: splice() and vmsplice(), please see
	thread @ http://yarchive.net/comp/linux/splice.html
*/

/*	cbuf_splice_from_pipe()
Splice()s at most 'size' bytes from 'a_pipe[0]' into the backing store
	of a zcio buffer.
Will not splice more than zcio_splice_max() bytes.

returns 'data_len' == nr. of bytes moved.
'data_len' may be less than 'size', and is 0 on error.

NOTE that if 'buf' is CBUF_MALLOC, the mechanics are identical save that 
	the data is read() instead of splice()ed.
	*/
size_t	zcio_splice_from_pipe(int fd_pipe_read, cbuf_t *b, uint32_t pos, int i, size_t size)
{
	struct zcio_store *zs = (void *)b->user_bits;
	struct zcio_block *zb = cbuf_offt(b, pos, i);

	/* validate size of requested splice operation */
	if (!size) {
		zb->data_len = 0;
		return 0;
	}
	if (size > zb->blk_iov.iov_len)
		size = zb->blk_iov.iov_len;

	do {
		/* read() if data is going a malloc()ed 'buf' */
		if (!zs->fd)
			zb->data_len = read(fd_pipe_read, zs->iov.iov_base + zb->blk_offset, size);
		/* otherwise splice() */
		else
			zb->data_len = splice(fd_pipe_read, NULL, fd, &temp_offset, 
				size, SPLICE_F_NONBLOCK);
	/* ... notice we don't loop on a PARTIAL read/splice */
	//} while ((zb->data_len  == 0 || zb->data_len == -1) && errno == EWOULDBLOCK 
	} while ((zb->data_len == -1) && errno == EWOULDBLOCK 
		/* the below are tested/executed only if we would block: */ 
		&& !CBUF_YIELD() /* don't spinlock */
		&& !(errno = 0)); /* resets errno ONLY if we will retry */
	

	/* if got error, reset to "nothing" */
	if (zb->data_len == -1) {
		Z_err("splice size=%ld", size);
		zb->data_len = 0;
	}
	/* We could have spliced an amount LESS than requested.
	That is not an error: caller should check the return value
		and act accordingly.
		*/
	Z_err_if(zb->data_len == 0, "zb->data_len %ld; size %ld", zb->data_len size);
	//Z_err_if(*cbuf_head != size, "*cbuf_head %ld; size %ld", *cbuf_head, size);

	/* done */
	return zb->data_len;
}

/*	cbuf_splice_to_pipe()
Reads 'data_len' from the block described by 'pos' and 'i'.
See cbuf_blk_data_len() for details on where this is located physically.

splice()s 'data_len' bytes from block into 'fd_pipe_write'.
`fd_pipe_write` MUST be a pipe, not a file or mmap'ed region.

returns nr. of bytes spliced, 0 on error.

Note: if CBUF_MMAP, vmsplice() is used.
	*/
size_t	zcio_splice_to_pipe_sub(cbuf_t *b, uint32_t pos, int i, int fd_pipe_write, 
				loff_t sub_offt, size_t sub_len)
{
	int fd;
	size_t *data_len;
	loff_t temp_offset;
	struct iovec iov;

	/* params: backing store */
	if (b->cbuf_flags) {
		struct zcio *f = cbuf_offt(b, pos, i);
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
	if (*data_len > zcio_splice_max(b)) {
		Z_err("corrupt splice size of %ld, max is %ld", 
			*data_len, zcio_splice_max(b));
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


/*	cbuf_create_p1()
Creates a temporary "backing store" mmap()ed to the file path
	requested by 'backing_store'.
Any existing file at that path will be overwritten.

Then creates a cbuf which blocks are 'sizeof(cbufp_t)' large,
	each 'cbuf_t' describes a 'block' of memory mapped IN THE BACKING STORE.

Essentially, each cbuf block is only used as a tracking structure describing 
	a block in the backing store.
See description of 'cbufp' in "cbuf.h".

Note that the reason 'backing_store' is user-provided is to allow the user to
	force backing store creation on a disk of their own choosing.
This reduces the cost of splice() operation between the backing store 
	and a destination file on that same file system (essentially,
	only some filesystem accounting needs to be done).

 ODO: Robert, the current 'backing_store' logic is probably wrong:
	we want to allow the user to specify which DIRECTORY PATH
	the backing store should be created in, not necessarily what
	its precise filename should be (I can't think of a case, can you?).
Look at 'char tfile[]' in "cbuf_int.c" and `man mkostemp` 
	for workable temp file creation mechanism.
	*/
cbuf_t *cbuf_create_p1(uint32_t obj_sz, uint32_t obj_cnt, char *map_dir)
{
	cbuf_t *ret = NULL;

	/* create cbuf, passing 0x0 as flag, cause malloc only */
	ret = cbuf_create_(sizeof(struct zcio), obj_cnt, 0x0, map_dir);
	Z_die_if(!ret, "cbuf create failed");
	/* cbuf_create_() will have padded the obj size and obj count to 
		fit into powers of 2.
	The backing store MUST have sufficient space for EACH cbufp_t in cbuf 
		to point to a unique area of `obj_sz` length.
		*/
	obj_cnt = cbuf_obj_cnt(ret);	

	/* make accounting structure */
	struct zcio f;	
	memset(&f, 0x0, sizeof(f));

	/* Map backing store.
		Typecasts because of insidious overflow.
		*/
	f.iov.iov_len = ((uint64_t)obj_sz * (uint64_t)obj_cnt);
	Z_die_if(!(
		f.fd = sbfu_tmp_map(&f.iov, map_dir)
		), "");
	f.blk_iov.iov_len = obj_sz;
	f.blk_iov.iov_base = f.iov.iov_base;

	/* populate tracking structures 
	Go through the motions of reserving cbuf blocks rather than
		accessing ret->buf directly.
	This is because cbuf will likely fudge the block size on creation, 
		and we don't want to care about that.
	 	*/
	uint32_t pos = cbuf_snd_res(ret, obj_cnt);
	Z_die_if(pos == -1, "");
	struct zcio *p;
	for (f.blk_id=0; f.blk_id < obj_cnt; 
		f.blk_id++, f.blk_iov.iov_base += obj_sz) 
	{ 
		p = cbuf_offt(ret, pos, f.blk_id);
		memcpy(p, &f, sizeof(f));
		p->blk_offset = f.blk_iov.iov_base - f.iov.iov_base;
	};
	cbuf_snd_rls(ret, obj_cnt);
	/* 'receive' so all blocks are marked as unused */
	pos = cbuf_rcv_res(ret, obj_cnt);
	Z_die_if(pos == -1, "");
	cbuf_rcv_rls(ret, obj_cnt);

	return ret;
out:
	cbuf_free_(ret);
	return NULL;
}


