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
/* Allocated a number of zcio_store structs given by block_sz and block_cnt
 
   Question is: doesn't it also need to include the zcio_block struct, as
   we also need to keep that one around?
   Based on the fact that a cbuf_p struct was one large struct that was in each
   buf block
 
 */
struct zcio_store	*zcio_new(size_t block_sz, uint32_t block_cnt)
{
	struct zcio_store* zs = (struct zcio_store*)calloc(sizeof(struct zcio_store), 1);

	if (!zs)
	{
		Z_err("zcio_store calloc failed");
	}

	return zs;
}

/* free the given zcio_store struct */
void			zcio_free(struct zcio_store *zs)
{
	if (!zs)
		return;
	void *temp = NULL;

	/* TODO: implement this sexy thing everywhere */
	__atomic_exchange(&zs->buf, NULL, (cbuf_t **)&temp, __ATOMIC_SEQ_CST);
	if (temp)
		cbuf_free(temp);

	if (zs->iov.iov_base) {
		/* free backing store (there is an SBFU function for the mapped case) */
	}

	free(zs);
}

size_t			zcio_in_splice(struct zcio_store *zs, 
					struct cbuf_blk_ref dest,
					int fd_pipe_from, size_t size)

{
	/* Splice from a pipe into memory backing store 
		so from fd_pipe_from -> zs->fd */

	if (!size)
	{
		Z_err("size is 0");
		return 0;
	}


	// we need to send reserve and then splice
	struct zcio_block *zb = cbuf_offt(zs->buf, dest);

	

	/* not sure what whether I need to loop here.. */
	do {
		if (!zs->fd) /* deal with the malloc case */
			zb->data_len = read(fd_pipe_from, 
					zs->iov.iov_base + zb->blk_offset, 
					size);
		else
		zb->data_len = splice(fd_pipe_from, NULL,  
					zs->fd, &zb->blk_offset, 
					size,  
					SPLICE_F_MOVE | SPLICE_F_NONBLOCK);
	/* notice we are not looping on a partial read/splice */
	} while (zb->data_len == -1 && errno == EWOULDBLOCK
		/* the below are tested/executed only if we would block: */ 
		&& !CBUF_YIELD() /* don't spinlock */
		&& !(errno = 0)); /* resets errno ONLY if we will retry */

	if (zb->data_len == -1)
	{
		/*error */
	}

	return zb->data_len;
}


size_t			zcio_out_splice_sub(struct zcio_store *zs, 
					uint32_t pos, int i, 
					int fd_pipe_to,
					loff_t sub_offt, size_t sub_len)
{
	/* Splice from backing store into pipe */


	struct zcio_block* zb = cbuf_offt(zs->buf, pos, i);

	if (!zs->fd)
	{
		Z_err("zs->fd is malloc");
		return 0;
	}

	if(zb->data_len	== 0)
		return 0;

	/* sub-block? */
	if (sub_len) {
		if (sub_len > (uint64_t)zb->data_len - sub_offt)
		{
			return 0;
		}
		zb->data_len = sub_len;
		zb->blk_offset = sub_offt;
		

	}
	
}

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
	if (size > zs->block_sz)
		size = zs->block_sz;

	do {
		/* read() if data is going a malloc()ed 'buf' */
		if (!zs->fd)
			zb->data_len = read(fd_pipe_read, zs->iov.iov_base + zb->blk_offset, size);
		/* otherwise splice() */
		else
			zb->data_len = splice(fd_pipe_read, NULL, zs->fd, &zb->blk_offset, 
				size, SPLICE_F_NONBLOCK);
	/* ... notice we don't loop on a PARTIAL read/splice */
	} while ((zb->data_len == -1) && errno == EWOULDBLOCK 
		/* the below are tested/executed only if we would block: */ 
		&& !cbuf_yield() /* don't spinlock */
		&& !(errno = 0)); /* resets errno only if we will retry */
	

	/* if got error, reset to "nothing" */
	if (zb->data_len == -1) {
		Z_err("splice size=%ld", size);
		zb->data_len = 0;
	}
	/* We could have spliced an amount LESS than requested.
	That is not an error: caller should check the return value
		and act accordingly.
		*/
	Z_err_if(zb->data_len == 0, "zb->data_len %ld; size %ld", zb->data_len, size);
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
	struct zcio_store *zs = (void *)b->user_bits; //this is wrong
	struct zcio_block *zb = cbuf_offt(b, pos, i);
	
	/* I removed the vmsplice read/write here when we had a malloc()d
	 * buf,
	 * not sure if that is correct,
	 * as the splice_from_ function above still reads from pipe
	 * if malloc() 
	 *
	 * I'm confus. :(
	 *
	 * */



	/* sanity */
	if (zb->data_len == 0)
		return 0;
	if (zs->fd == 0) /* we have a malloc()ed buf */
		return 0;

/* 	if (*data_len > zcio_splice_max(b)) {
		Z_err("corrupt splice size of %ld, max is %ld", 
			*data_len, zcio_splice_max(b));
		return 0;
	}
*/
	/* sub-block? */
	if (sub_len) {
		if (sub_len > (int64_t)zb->data_len - sub_offt) {
			Z_err("bad sub-block request: len %ld @offt %ld > *data_len %ld",
				sub_len, sub_offt, zb->data_len);
			return 0;
		}
		zb->data_len = sub_len;
		zb->blk_offset += sub_offt;
	}

	/* Pull chunk from buffer.
	Could return -1 if destination pipe is full.
	Have pipe empty before running this, then evacuate pipe.
		*/
	ssize_t temp;
	do {
		/* buf will never be malloc()ed, so this goes */
		/* we always just splice: splice() */
		temp = splice(zs->fd, &zb->blk_offset, fd_pipe_write, 
			NULL, zb->data_len, SPLICE_F_NONBLOCK);
	} while ((temp == -1) && errno == EWOULDBLOCK 
		/* the below are tested/executed only if we would block: */ 
		&& !CBUF_YIELD() /* don't spinlock */
		&& !(errno = 0)); /* resets errno ONLY if we will retry */

	/* haz error? */
	if (temp == -1) {
		temp = 0;
		Z_err("data_len=%lu", zb->data_len);
	}

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
cbuf_t *cbuf_create_p1(size_t obj_sz, uint32_t obj_cnt, char *map_dir)
{
	/* make accounting structure */
	struct zcio_store *zs  = calloc(1, sizeof(struct zcio_store));

	/* create cbuf, passing 0x0 as flag, cause malloc only */
	zs->buf = cbuf_create_(sizeof(struct zcio_block), obj_cnt, 0x0);
	Z_die_if(!ret, "cbuf create failed");
	/* cbuf_create_() will have padded the obj size and obj count to 
		fit into powers of 2.
	The backing store MUST have sufficient space for EACH cbufp_t in cbuf 
		to point to a unique area of `obj_sz` length.
		*/
	obj_cnt = cbuf_obj_cnt(ret);	

	
	loff_t	blk_offset;
	size_t	data_len;


	/* Map backing store.
		Typecasts because of insidious overflow.
		*/
	f.iov.iov_len = ((uint64_t)obj_sz * (uint64_t)obj_cnt);
	Z_die_if(!(
		f.fd = sbfu_tmp_map(&f.iov, map_dir)
		), "");
	
	f.block_sz = obj_sz;
	//no base..
	//offset - block_sz = base

	//f.blk_iov.iov_len = obj_sz;
	//f.blk_iov.iov_base = f.iov.iov_base;

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


