#include "cbuf_int.h"

#ifdef Z_BLK_LVL
#undef Z_BLK_LVL
#endif
#define Z_BLK_LVL 0
/*	debug levels:
	1:	
	2:	
	3:	checkpoints
	4:
	5:	
*/

cbuf_t *cbuf_create(uint32_t obj_sz, uint32_t obj_cnt)
{
	return cbuf_create_(obj_sz, obj_cnt, 0x0);
}
cbuf_t *cbuf_create_p(uint32_t obj_sz, uint32_t obj_cnt, char *backing_store)
{
	cbuf_t *ret = NULL;
	Z_die_if(!backing_store, "please provide a path for the backing store");

	/* create cbuf */
	ret = cbuf_create_(sizeof(cbufp_t), obj_cnt, CBUF_P);
	Z_die_if(!ret, "cbuf create failed");
	/* cbuf_create_() will have padded the obj size and obj count to 
		fit  into powers of 2.
	The backing store MUST have sufficient space for EACH cbufp_t in cbuf 
		to  point to a unique area of `obj_sz` length.
		*/
	obj_cnt = cbuf_obj_cnt(ret);	

	/* make accounting structure */
	cbufp_t f;	
	memset(&f, 0x0, sizeof(f));
	/* string masturbation */
	size_t len = strlen(backing_store) + 1;
	Z_die_if(!(f.file_path = malloc(len)), "");
	memcpy(f.file_path, backing_store, len);

	/* Map backing store.
		Typecasts because insidious overflow.
		*/
	f.iov.iov_len = ((uint64_t)obj_sz * (uint64_t)obj_cnt);
	Z_die_if(!(f.fd = sbfu_dst_map(&f.iov, f.file_path)), "");
	f.blk_iov.iov_len = obj_sz;
	f.blk_iov.iov_base = f.iov.iov_base;

	/* populate tracking structures 
	Go through the motions of reserving cbuf blocks rather than
		accessing ret->buf directly.
	This is because cbuf will likely fudge the block size on creation, 
		and we don't want to care about that.
		*/
	uint32_t pos = cbuf_snd_res_m(ret, obj_cnt);
	Z_die_if(pos == -1, "");
	cbufp_t *p;
	for (f.blk_id=0; f.blk_id < obj_cnt; 
		f.blk_id++, f.blk_iov.iov_base += obj_sz) 
	{
		p = cbuf_offt(ret, pos, f.blk_id);
		memcpy(p, &f, sizeof(f));
		p->blk_offset = f.blk_iov.iov_base - f.iov.iov_base;
	};
	cbuf_snd_rls_m(ret, obj_cnt);
	/* 'receive' so all blocks are marked as unused */
	pos = cbuf_rcv_res_m(ret, obj_cnt);
	Z_die_if(pos == -1, "");
	cbuf_rcv_rls_m(ret, obj_cnt);

	return ret;
out:
	if (f.file_path)
		free(f.file_path);
	cbuf_free_(ret);
	return NULL;
}

void cbuf_free(cbuf_t *buf)
{
	cbuf_free_(buf);
}

/*	cbuf_(snd|rcv)_reserve()
Obtain exclusive title to an obj_sz chunk of memory in the circular buffer.
This memory is no longer available for either sending or receiving
	... until the corresponsing "release" is called.
*/
void *cbuf_snd_res(cbuf_t *buf)
{
	/* pos is already masked to cycle back to 0 */
	uint32_t pos = cbuf_reserve__(buf, 1 << buf->sz_bitshift_, 
					&buf->sz_unused, 
					&buf->snd_reserved, 
					&buf->snd_pos);
	if (pos == -1)
		return NULL;
	else
		return buf->buf + pos; /* does masking */
}

/*	cbuf_(snd|srv)_serve_multi()
Obtain multiple contiguous (obj_sz * cnt) chunks of memory.
Returns the POSITION of the reservation, not a memory address.
`pos` can then be fed to cbuf_offt() at each access loop iteration to get a 
	proper memory address (and cleanly loop through the end of the buffer).
`pos` as returned MAY be an overflow - use it only as a token to be passed
	to cbuf_offt().
Returns -1 on fail.
	*/
uint32_t cbuf_snd_res_m(cbuf_t *buf, size_t cnt)
{
	/* sanity */
	if (!cnt)
		return -1;
	/* attempt a reservation, get position */
	//return cbuf_reserve__(buf, buf->obj_sz * cnt, 
	return cbuf_reserve__(buf, cnt << buf->sz_bitshift_, 
					&buf->sz_unused, 
					&buf->snd_reserved, 
					&buf->snd_pos);
}

/*	cbuf_snd_res_m_cap()
Tries to reserve a variable number of buffer slots up to *res_cnt.
If successful, returns `pos` and *res_cnt is set to the
	amount of slots reserved.
May fail, which returns -1 and leaves the value of *res_cnt
	as the reservation size attempted.
	*/
uint32_t cbuf_snd_res_m_cap(cbuf_t *buf, size_t *res_cnt)
{
	/* bitshift sz_ready so it gives an OBJECT COUNT
		as opposed to a count of BYTES
		*/
	size_t possible = buf->sz_unused >> buf->sz_bitshift_;
	/* what CAN we ask for? */
	if (*res_cnt > possible)
		*res_cnt = possible; /* if it's less than ideal, reflect that */
	/* say please */
	return cbuf_snd_res_m(buf, *res_cnt);
}

/*	The _rcv_ family of functions is symmetric to the _snd_ ones
		above. Look there for detailed comments.
	*/
void *cbuf_rcv_res(cbuf_t *buf)
{
	/* attempt a reservation, get position */
	uint32_t pos = cbuf_reserve__(buf, 1 << buf->sz_bitshift_, 
					&buf->sz_ready, 
					&buf->rcv_reserved, 
					&buf->rcv_pos);
	/* turn pos into an address */
	if (pos == -1)
		return NULL;
	else
		return buf->buf + pos;
}

uint32_t cbuf_rcv_res_m(cbuf_t *buf, size_t cnt)
{
	/* not asking for anything? bye. */
	if (!cnt)
		return -1;
	/* attempt a reservation, get position */
	//return cbuf_reserve__(buf, buf->obj_sz * cnt, 
	return cbuf_reserve__(buf, cnt << buf->sz_bitshift_, 
					&buf->sz_ready, 
					&buf->rcv_reserved, 
					&buf->rcv_pos);
}

uint32_t cbuf_rcv_res_m_cap(cbuf_t *buf, size_t *res_cnt)
{
	size_t possible = buf->sz_ready >> buf->sz_bitshift_;
	if (*res_cnt > possible)
		*res_cnt = possible;
	return cbuf_rcv_res_m(buf, *res_cnt);
}

void cbuf_snd_rls(cbuf_t *buf)
{
	cbuf_release__(buf, 1 << buf->sz_bitshift_, 
			&buf->snd_reserved, 
			&buf->snd_uncommit,
			&buf->sz_ready);
}

void cbuf_snd_rls_m(cbuf_t *buf, size_t cnt)
{
	if (!cnt)
		return;
	//cbuf_release__(buf, buf->obj_sz * cnt, 
	cbuf_release__(buf, cnt << buf->sz_bitshift_, 
			&buf->snd_reserved, 
			&buf->snd_uncommit,
			&buf->sz_ready);
}

void cbuf_rcv_rls(cbuf_t *buf)
{
	cbuf_release__(buf, 1 << buf->sz_bitshift_, 
			&buf->rcv_reserved, 
			&buf->rcv_uncommit,
			&buf->sz_unused);
}

void cbuf_rcv_rls_m(cbuf_t *buf, size_t cnt)
{
	if (!cnt)
		return;
	cbuf_release__(buf, cnt << buf->sz_bitshift_, 
			&buf->rcv_reserved, 
			&buf->rcv_uncommit,
			&buf->sz_unused);
}

void cbuf_rcv_rls_mscary(cbuf_t *buf, size_t cnt)
{
	if (!cnt)
		return;
	return cbuf_release_scary__(buf, cnt << buf->sz_bitshift_, 
			&buf->rcv_reserved, 
			&buf->rcv_uncommit,
			&buf->sz_unused);
}

/*	cbuf_rcv_held()

Get `pos` and `i` for ALL blocks currently reserved or uncommitted
	on the receiver side.

NOTE: this is NOT thread-safe - NO receive-side blocks must be alloc'ed or
	released while this is run.
This is ONLY safe to run when receiver is a single thread.

return: same semantics as reservation calls: -1 is bad.
	*/
uint32_t	cbuf_rcv_held(cbuf_t *buf, size_t *out_cnt)
{
	if (!out_cnt)
		return -1;

	/* get a count of how many blocks are reserved or uncommitted */
	*out_cnt = (buf->rcv_reserved + buf->rcv_uncommit) >> buf->sz_bitshift_;

	/* return actual receiver */
	uint64_t snd_pos;
	uint64_t sz_unused;
	return cbuf_actual_receiver__(buf, &snd_pos, &sz_unused) & buf->overflow_;
}

/*	cbuf_checkpoint_snapshot()

Take a "snapshot" of the current sender position IN RELATION TO the "actual receiver".
Note that this checkpoint concept relies on NOT overflowing, or "unrolling" the count.
uint64_t for the edge case where size of cbuf (overflow_ +1) == UINT32_MAX.

some interesting equations:
	snd_pos = "actual sender" + (snd_reserved + snd_uncommitted);
		"actual sender" = rcv_pos + ready;
	rcv_pos = "actual receiver" + (rcv_reserved + rcv_uncommitted);
		"actual receiver" = snd_pos + unused;

The concept is that once "actual receiver" moves PAST the "snapshot" of
	a previous sender position, all bytes up to that snapshot
	will have been "consumed" (aka: reserved AND released by receiver).

RETURNS: a uint64_t which can be fed to `cbuf_checkpoint_verif()` later.
	*/
uint64_t	cbuf_checkpoint_snapshot(cbuf_t *b)
{
	/*  get a clean snapshot of variables */
	uint64_t ret;
	uint64_t sz_unused;
	uint64_t actual_rcv = cbuf_actual_receiver__(b, &ret, &sz_unused);

	/* If sender is smaller than "actual receiver", it actually rolled over:
		add a full buffer size to it.
	NOTE: snd_pos + unused == "actual receiver".
		*/
	if (ret < actual_rcv)
		ret += b->overflow_ +1;
	Z_inf(3, "snapshot. snd_pos(unrolled) %ld, 'actual receiver' %ld",
		ret, actual_rcv);
	return ret;
}

/*	cbuf_checkpoint_verif()

Verifies state of the current cbuf at `b` against a checkpoint previously
	produced by `cbuf_checkpoint_snapshot()`.

RETURNS 1 if all data through `snd_pos` at the time snapshot was taken 
	has been consumed by receiver.
	*/
int cbuf_checkpoint_verif(cbuf_t *b, uint64_t checkpoint)
{
	/* "actual receiver" is the concept of 
		"what does the receiver NO LONGER CARE ABOUT."
	This is : `rcv_pos - (rcv_reserved + rcv_uncommit)`
		... but subtraction is messy to roll over "before 0",
		so we use the equivalent `snd_pos + unused`
	When "actual receiver" has surpassed the snd_pos at time of snapshot,
		all data which was queued for receiver at the time is
		guaranteed to have been released by receiver.
		*/
	uint64_t actual_rcv = ((uint64_t)b->snd_pos + b->sz_unused); /* notice we don't mask to overflow */
	Z_inf(3, "checkpoint: %ld vs 'actual receiver' %ld", checkpoint, actual_rcv);
	return actual_rcv >= checkpoint;
}

/*	cbuf_splice_sz()

Returns the SIZE OF SPLICED DATA represented by a cbuf block, 
	whether the data is in the cbuf itself 
	or whether it is in a backing store and only tracked by the cbuf block.
	*/
size_t	cbuf_splice_sz(cbuf_t *b, uint32_t pos, int i)
{
	/* get base of block being pointed to */
	void *base = cbuf_offt(b, pos, i);

	/* if this cbuf has a backing store, block is a tracking stuct.
		Typecast and dereference.
		*/
	if (b->cbuf_flags & CBUF_P)
		return ((cbufp_t *)base)->data_len;

	/* Otherwise, data length is in the first 8B of the block itself.
		Typecast and dereference.
		*/
	return *((size_t *)base);
}

/*	cbuf_splice_from_pipe()
Pulls at most `size` bytes from `a_pipe[0]` into the cbuf block at `pos`, offset `i`.

In the case that cbuf has a backing store (the block only contains a cbufp_t):
	The amount of bytes to push is limited to 'blk_iov.iov_len'.
	The amount of bytes actually pushed is stored in the 'data_len' variable.
	data_len is returned but is never less than 0.

In the case of a regular cbuf:
	The amount of bytes actually pushed is written in the first 8B of 
		the cbuf block itself, aka `cbuf_head`.
	`cbuf_head` may be 0 but will be AT MOST the size of cbuf block (minus 8B).
	Returns `cbuf_head` - which on error will be 0, NOT -1(!).
	*/
size_t	cbuf_splice_from_pipe(int fd_pipe_read, cbuf_t *b, uint32_t pos, int i, size_t size)
{
	if (!size)
		return 0;

	size_t *cbuf_head;
	loff_t temp_offset;
	int fd;

	/* splice params: backing store */
	if (b->cbuf_flags & CBUF_P) {
		cbufp_t *f = cbuf_offt(b, pos, i);

		/* sanity */
		if (size > f->blk_iov.iov_len){
			Z_err("size %d larger than iov_len %d", (int)size, (int)f->blk_iov.iov_len);
			size = f->blk_iov.iov_len;
		}
		
		/* params */
		temp_offset = f->blk_offset;
		cbuf_head = &f->data_len;
		fd = f->fd; /* fd is the backing store's fd */

	/* params: cbuf block itself as splice destination */
	} else {
		/* sanity */
		if (size > (cbuf_sz_obj(b) - sizeof(size_t)))
			size = cbuf_sz_obj(b) - sizeof(size_t);

		/* get head of buffer block, put offset from buf fd info cbuf_off */
		temp_offset = cbuf_lofft(b, pos, i, &cbuf_head);
		/* fd is the cbuf itself */
		fd = b->mmap_fd;
	}

	/* do splice */
	do {
		*cbuf_head = splice(fd_pipe_read, NULL, fd, &temp_offset, 
				size, SPLICE_F_NONBLOCK);
	} while (*cbuf_head == 0 && errno == EWOULDBLOCK);
	/* housekeeping */
	if (errno == EWOULDBLOCK)
		errno = 0;

	/* if got error, reset to "nothing" */
	if (*cbuf_head == -1)
		*cbuf_head = 0;

	/* done */
	Z_err_if(*cbuf_head == 0, "*cbuf_head %ld; size %ld", *cbuf_head, size);
	return *cbuf_head;
}

/*	cbuf_splice_to_pipe()
Reads `cbuf_head` (see `cbuf_splice_from_pipe()` above) @(pos +i).
Splices `*cbuf_head` byted from the cbuf into `fd_pipe_write`.
`fd_pipe_write` MUST be a pipe, not a file or mmap'ed region.
If there is an error will return 0, not -1.
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
	/* Pull chunk from buffer.
		Could return -1 if dest. pipe is full.
		Have pipe empty before running this, then evacuate pipe.
		*/
	ssize_t temp;
	do {
		temp = splice(fd, &temp_offset, fd_pipe_write, 
				NULL, *cbuf_head, SPLICE_F_NONBLOCK);
	} while (!temp && errno == EWOULDBLOCK);
	/* housekeeping */
	if (errno == EWOULDBLOCK)
		errno = 0;

	/* haz error? */
	if (temp == -1) {
		temp = 0;
	}

	/* return */
	Z_err_if(temp != *cbuf_head, "temp %ld; *cbuf_head %ld", temp, *cbuf_head);
	return temp;
}

#undef Z_BLK_LVL
#define Z_BLK_LVL 0
