#include "cbuf_int.h"

#ifdef Z_BLK_LVL
#undef Z_BLK_LVL
#endif
#define Z_BLK_LVL 0
/*	debug levels:
	1:	
	2:	alloc/free
	3:	checkpoints
	4:
	5:	
*/

struct cbuf *cbuf_create(uint32_t obj_sz, uint32_t obj_cnt)
{ 
	return cbuf_create_ (obj_sz, obj_cnt, 0x0);
}

/*	cbuf_zero()
Zero the entire buffer.
Has the side effect of pre-faulting a buffer.

returns 0 on success.

Zeroing a buffer that has reserved blocks is considered bad form 
	and will produce an error.
*/
int cbuf_zero(struct cbuf *buf)
{
	int err_cnt = 0;
	Z_die_if(!buf, "no buffer");
	Z_bail_if((buf->snd_pos + buf->rcv_pos) & buf->overflow_, "buffer occupied");

	memset(buf->buf, 0x0, cbuf_sz_buf(buf));
out:
	return err_cnt;
}

void cbuf_free(struct cbuf *buf)
{
	Z_inf(2, "free cbuf 0x%lx", (uint64_t)buf);
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
uint32_t cbuf_snd_res(struct cbuf *buf, size_t cnt)
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
uint32_t cbuf_snd_res_cap(struct cbuf *buf, size_t *res_cnt)
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
uint32_t cbuf_rcv_res(struct cbuf *buf, size_t cnt)
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

uint32_t cbuf_rcv_res_cap(struct cbuf *buf, size_t *res_cnt)
{
	size_t possible = buf->sz_ready >> buf->sz_bitshift_;
	if (*res_cnt > possible)
		*res_cnt = possible;
	return cbuf_rcv_res(buf, *res_cnt);
}


void cbuf_snd_rls(struct cbuf *buf, size_t cnt)
{
	if (!cnt)
		return;
	//cbuf_release__(buf, buf->obj_sz * cnt, 
	cbuf_release__(buf, cnt << buf->sz_bitshift_, 
			&buf->snd_reserved, 
			&buf->snd_uncommit,
			&buf->sz_ready);
}


void cbuf_rcv_rls(struct cbuf *buf, size_t cnt)
{ if (!cnt)
		return;
	cbuf_release__(buf, cnt << buf->sz_bitshift_, 
			&buf->rcv_reserved, 
			&buf->rcv_uncommit,
			&buf->sz_unused);
}

void cbuf_rcv_rls_mscary(struct cbuf *buf, size_t cnt)
{
	if (!cnt)
		return;
	return cbuf_release_scary__(buf, cnt << buf->sz_bitshift_, 
			&buf->rcv_reserved, 
			&buf->rcv_uncommit,
			&buf->sz_unused);
}

void cbuf_snd_rls_mscary(struct cbuf *buf, size_t cnt)
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
uint32_t	cbuf_rcv_held(struct cbuf *buf, size_t *out_cnt)
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

uint32_t	cbuf_actual_snd(struct cbuf *buf)
{
	uint32_t ret;
	cbuf_actuals__(buf, &ret, NULL);
	return ret;
}

uint32_t	cbuf_actual_rcv(struct cbuf *buf)
{
	uint32_t ret;
	cbuf_actuals__(buf, NULL, &ret);
	return ret;
}

#undef Z_BLK_LVL
#define Z_BLK_LVL 0
