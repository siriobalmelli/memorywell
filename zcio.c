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

/* free the given zcio_store struct */
void			zcio_free(struct zcio_store *zs)
{
	if (!zs)
		return;
	void *temp = NULL;

	/* TODO: implement this sexy thing everywhere */
	__atomic_exchange(&zs->buf,(struct cbuf**)&temp,(struct cbuf**)&temp, __ATOMIC_SEQ_CST);
	if (temp) {
		Z_inf(0, "cbuf_free");
		cbuf_free(temp);
	}

	if (zs->iov.iov_base) {
		/* free backing store (there is an SBFU function for the mapped case) */
		sbfu_unmap(zs->fd, &zs->iov);
	}


	void *zstmp = NULL;
	__atomic_exchange(&zs, (struct zcio_store**)&zstmp, (struct zcio_store**)&zstmp, 
				__ATOMIC_SEQ_CST);
	if (zstmp) {
		Z_inf(0, "zstmp free");
		free(zstmp);
	}
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


	// we need to create zcio_block from cbuf_offt 
	struct zcio_block *zb = cbuf_offt(zs->buf, dest);

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


	/* if got error, reset to "nothing" */	
	if (zb->data_len == -1) {
		Z_err("splice size=%ld", size);	
		zb->data_len = 0;
	}
	
	/* We could have spliced an amount LESS than requested.
	That is not an error: caller should check the return valu
		and act accordingly.	
		*/
	Z_err_if(zb->data_len == 0, "zb->data_len %ld; size %ld", zb->data_len, size);

	return zb->data_len;
}


/*	zcio_out_splice_sub()
Reads 'data_len' from the block described by 'pos' and 'i'.
See cbuf_blk_data_len() for details on where this is located physically.

splice()s 'data_len' bytes from block into 'fd_pipe_write'.
`fd_pipe_write` MUST be a pipe, not a file or mmap'ed region.

returns nr. of bytes spliced, 0 on error.

Note: if CBUF_MMAP, vmsplice() is used.
	*/
size_t			zcio_out_splice_sub(struct zcio_store *zs, 
					struct cbuf_blk_ref cbr,
					int fd_pipe_to,
					loff_t sub_offt, size_t sub_len)
{
	struct zcio_block *zb = cbuf_offt(zs->buf, cbr);

	/* sanity */
	if (zb->data_len == 0)
		return 0;

 	if (zb->data_len > zs->block_sz) {
		Z_err("corrupt splice size of %ld, max is %ld", 
			zb->data_len, zs->block_sz);
		return 0;
	}

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
		/* if we don't have an fd, we vmsplice as its a malloc */
		if (!zs->fd)
		{
			struct iovec iov;
			iov.iov_base = zs->buf + zb->blk_offset;
			iov.iov_len = zb->data_len;
			temp = vmsplice(fd_pipe_to, &iov, 1, SPLICE_F_GIFT);
		}
		else {
			/* we always just splice: splice() */
			temp = splice(zs->fd, &zb->blk_offset, fd_pipe_to, 
				NULL, zb->data_len, SPLICE_F_NONBLOCK);
		}
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

/*	zcio_new()
Creates a temporary "backing store" mmap()ed to the file path
	requested by 'backing_store'.
Any existing file at that path will be overwritten.

Then creates a cbuf which blocks are 'sizeof(cbufp_t)' large,
	each 'cbuf' describes a 'block' of memory mapped IN THE BACKING STORE.

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
struct zcio_store	*zcio_new(size_t block_sz, uint32_t block_cnt,
		enum zcio_store_type zctype, const char *map_dir)
{
	/* make accounting structure */
	struct zcio_store *zs  = calloc(1, sizeof(struct zcio_store));
	Z_die_if(!zs, "zcio_store create failed");

	/* create cbuf, passing 0x0 as flag, cause malloc only */
	zs->buf = cbuf_create_(sizeof(struct zcio_block), block_cnt, 0x0);
	Z_die_if(!zs->buf, "cbuf create failed");
	/* cbuf_create_() will have padded the obj size and obj count to 
		fit into powers of 2.
	The backing store MUST have sufficient space for EACH cbufp_t in cbuf 
		to point to a unique area of `obj_sz` length.
		*/
	block_cnt = cbuf_blk_cnt(zs->buf);		

	/* Map backing store.
		Typecasts because of insidious overflow.
		*/
	zs->iov.iov_len = ((uint64_t)block_sz * (uint64_t)block_cnt);

	switch(zctype)
	{
		case MALLOC:
			Z_die_if(!(zs->iov.iov_base = malloc(zs->iov.iov_len)),
					"");

			break;
		case MMAP:
			Z_die_if((
				zs->fd = sbfu_tmp_map(&zs->iov, map_dir)
				) < 1, "");
	
			break;
	}
	zs->block_sz = block_sz;

	/* populate tracking structures 
	Go through the motions of reserving cbuf blocks rather than
		accessing ret->buf directly.
	This is because cbuf will likely fudge the block size on creation, 
		and we don't want to care about that.
	
	*/
	struct	cbuf_blk_ref cbr;
	cbr.pos = cbuf_snd_res(zs->buf,block_cnt);
	Z_die_if(cbr.pos == -1, "");
	struct zcio_block *blk;
	loff_t	offt = 0;
	
	for (cbr.i = 0; cbr.i < block_cnt; 
		cbr.i++, offt += zs->block_sz) 
	{ 	
		blk = cbuf_offt(zs->buf, cbr);
		blk->blk_offset = offt;
		blk->data_len = 0;
	};
	cbuf_snd_rls(zs->buf, block_cnt);
	/* 'receive' so all blocks are marked as unused */
	cbr.pos = cbuf_rcv_res(zs->buf, block_cnt);
	Z_die_if(cbr.pos == -1, "");
	cbuf_rcv_rls(zs->buf, block_cnt);

	return zs;
out:
	zcio_free(zs);
	return NULL;
}


