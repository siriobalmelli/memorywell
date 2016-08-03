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
//this has to go, because we only want the malloc here
/* 
cbuf_t *cbuf_create1(uint32_t obj_sz, uint32_t obj_cnt, char *map_dir)
{
	return cbuf_create_(obj_sz, obj_cnt, 0x0, map_dir);
}
*/
cbuf_t *cbuf_create_malloc(uint32_t obj_sz, uint32_t obj_cnt)
{
	//implicitly malloc only, no MALLOC flag
	return cbuf_create_(obj_sz, obj_cnt, 0x0, NULL);
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

/*	cbuf_snd_res_cap()
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
	return cbuf_snd_res(buf, *res_cnt);
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
	return cbuf_rcv_res(buf, *res_cnt);
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
