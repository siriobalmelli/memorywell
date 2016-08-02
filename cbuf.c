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

/* TODO: Sirio: clean this up
The problem is that we have a few options when creating a cbuf:

a.) Should 'buf' be malloc()ed or mmap()ed?
Not sure there should be a default for this.
I do know that it actually doesn't make much sense to mmap() 'buf' when
	doing a cbufp (see below).

b.) Should it be a cbuf (data in 'buf') 
	or cbufp (accounting in 'buf, data in "backing store")?
Default is cbuf.

c.) If mmap()ed in a.) above, and/or if cbufp in b.), 
	which directory should contain the temp file
	(allow the user to choose which filesystem is backing the memory map,
	see comments above cbuf_create_p() for more info.
The default (if user doesn't input anything or doesn't care) would be to
	map in /tmp.

Thing is, those 3 otions are mostly orthogonal: the challenge is to have
	a concise and sensical set of library calls.

This is what I propose. 
My comments are below each of the function signatures.
Please take a look and if it makes sense, then implement:

cbuf_t *cbuf_create(uint32_t obj_sz, uint32_t obj_cnt, char *map_dir);
	Default is to create a mmap()ed cbuf.
	If 'map_dir' is NULL, put it in "/tmp" as might be expected with 
		a POSIX system.

cbuf_t *cbuf_create_malloc(uint32_t obj_sz, uint32_t obj_cnt);
	Obviously a malloc()ed buffer doesn't need a temp file at all.

cbuf_t *cbuf_create_p(uint32_t obj_sz, uint32_t obj_cnt, char *map_dir);
	Can't find a case where a cbufp might need 'buf' to mmap()ed, since
		no splicing is being done in/out of the cbufp_t accounting
		blocks in 'buf': therefore always malloc() 'buf'.
	As for the backing store, this must obviously be a file 
		(can't imagine cbufp is to allow >4GB buffers, noone should
		be malloc()ing that anyways!), so allow
		the user to choose which directory it's in.


USAGE CASES:
So far, the usage cases and comments on each are as follows:


a.) Moving data from a file:
It should be faster to splice() from [file] to [cbuf], but this can only be done 
	if [cbuf] is mmap()ed.

[file] -> splice() -> [cbuf] -> splice() -> [socket]

The alternative, if [cbuf] was malloc()ed, is to read(), but this is decidedly slower.


b.) Generating new data:
It should be faster to use a malloc()ed [cbuf] instead of mmap()ed, 
	when new data is created.

[file] -> read() -> [stack]; [stack] -> encrypt() -> [cbuf] -> vmsplice() -> [socket]

This should be faster because a malloc()ed [cbuf] does NOT have a file on disk 
	to which the O/S is synchronizing the memory in the background,
	so we should be avoiding any unnecessary write traffic.


c.) Passing/synchronizing data between threads:
In this case, [cbuf] is used to pass some sort of data between threads, 
	so there is no splicing involved.
Each sender reserves some amount of blocks, uses each block like it was 
	allocated memory, then releases it.
Each receiver reserves blocks ready to be read, uses each block like 
	allocated memory (with useful data from the sender), 
	and then releases the block when it no longer needs the data.

produce_data() -> [cbuf] -> consume_data()

Here, there is NO zero-copy I/O being done, and so the overhead of the O/S 
	synchronizing data to a mmap()ed file in the background 
	can be avoided entirely by malloc()ing 'buf'.
*/

cbuf_t *cbuf_create1(uint32_t obj_sz, uint32_t obj_cnt, char *map_dir)
{
	return cbuf_create_(obj_sz, obj_cnt, 0x0, map_dir);
}
cbuf_t *cbuf_create_malloc(uint32_t obj_sz, uint32_t obj_cnt)
{
	return cbuf_create_(obj_sz, obj_cnt, CBUF_MALLOC, NULL);
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

	/* create cbuf */
	ret = cbuf_create_(sizeof(cbufp_t), obj_cnt, CBUF_P | CBUF_MALLOC, map_dir);
	Z_die_if(!ret, "cbuf create failed");
	/* cbuf_create_() will have padded the obj size and obj count to 
		fit into powers of 2.
	The backing store MUST have sufficient space for EACH cbufp_t in cbuf 
		to point to a unique area of `obj_sz` length.
		*/
	obj_cnt = cbuf_obj_cnt(ret);	

	/* make accounting structure */
	cbufp_t f;	
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
	cbuf_free_(ret);
	return NULL;
}


/*	cbuf_zero()
Zero the entire buffer.
Has the side effect of pre-faulting a buffer.

returns 0 on success.

Zeroing a buffer that has reserved blocks is considered bad form 
	and will produce an error.
*/
int cbuf_zero(cbuf_t *buf)
{
	int err_cnt = 0;
	Z_die_if(!buf, "no buffer");
	Z_bail_if((buf->snd_pos + buf->rcv_pos) & buf->overflow_, "buffer occupied");

	memset(buf->buf, 0x0, cbuf_sz_buf(buf));
out:
	return err_cnt;
}

void cbuf_free(cbuf_t *buf)
{
	cbuf_free_(buf);
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
uint32_t cbuf_snd_res(cbuf_t *buf, size_t cnt)
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
uint32_t cbuf_snd_res_cap(cbuf_t *buf, size_t *res_cnt)
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
uint32_t cbuf_rcv_res(cbuf_t *buf, size_t cnt)
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

uint32_t cbuf_rcv_res_cap(cbuf_t *buf, size_t *res_cnt)
{
	size_t possible = buf->sz_ready >> buf->sz_bitshift_;
	if (*res_cnt > possible)
		*res_cnt = possible;
	return cbuf_rcv_res_m(buf, *res_cnt);
}


void cbuf_snd_rls(cbuf_t *buf, size_t cnt)
{
	if (!cnt)
		return;
	//cbuf_release__(buf, buf->obj_sz * cnt, 
	cbuf_release__(buf, cnt << buf->sz_bitshift_, 
			&buf->snd_reserved, 
			&buf->snd_uncommit,
			&buf->sz_ready);
}


void cbuf_rcv_rls(cbuf_t *buf, size_t cnt)
{ if (!cnt)
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

void cbuf_snd_rls_mscary(cbuf_t *buf, size_t cnt)
{
	if (!cnt)
		return;
	return cbuf_release_scary__(buf, cnt << buf->sz_bitshift_, 
			&buf->snd_reserved, 
			&buf->snd_uncommit,
			&buf->sz_ready);
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
	/*
	uint64_t snd_pos;
	uint64_t sz_unused;
	return cbuf_actual_receiver__(buf, &snd_pos, &sz_unused) & buf->overflow_;
	*/
	return (buf->snd_pos + buf->sz_unused) & buf->overflow_;
}

uint32_t	cbuf_actual_snd(cbuf_t *buf)
{
	uint32_t ret;
	cbuf_actuals__(buf, &ret, NULL);
	return ret;
}

uint32_t	cbuf_actual_rcv(cbuf_t *buf)
{
	uint32_t ret;
	cbuf_actuals__(buf, NULL, &ret);
	return ret;
}

#undef Z_BLK_LVL
#define Z_BLK_LVL 0
