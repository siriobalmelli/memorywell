#include <cbuf_int.h>

/** INTERNALS **/
// TODO: add HLE flags to cbuf operations to try and get a speed bump; measure this

struct cbuf *cbuf_create_(uint32_t	obj_sz,
			uint32_t	obj_cnt,
			uint8_t		flags)
{
	struct cbuf *b = NULL;
	Z_die_if(!obj_sz, "expecting object size");
	b = calloc(1, sizeof(struct cbuf));
	Z_die_if(!b, "no buf stat");
	b->cbuf_flags = flags;

	uint32_t sz_aligned;
	/* alignment: obj_sz must be a power of 2 */
	sz_aligned = nm_next_pow2_32(obj_sz );
	Z_die_if(sz_aligned < obj_sz,
		"aligned obj_sz overflow: obj_sz=%d > sz_aligned=%d",
		obj_sz, sz_aligned);
	obj_sz = sz_aligned;
	/* calc shift value necessary to turn `buf_sz / obj_sz`
		into a bitwise op.
	Use sz_aligned just as a temp variable.
		*/
	uint8_t bitshift = 0;
	while (sz_aligned > 1) {
		sz_aligned >>= 1;
		bitshift++;
	}
	/* force assignment to const value of 'b->sz_bitshift_'
	AKA: b->sz_bitshift_ = bitshift;
	*/
	((uint8_t*)&b->sz_bitshift_)[0] = bitshift;

	/* 'buf_sz' must be a multiple of obj_sz AND a power of 2 */
	uint32_t buf_sz = obj_sz * obj_cnt;
	sz_aligned = nm_next_pow2_32(nm_next_mult32(buf_sz, obj_sz));
	Z_die_if(sz_aligned < buf_sz,
		"aligned buf_sz overflow: buf_sz=%d > sz_aligned=%d",
		buf_sz, sz_aligned);
	buf_sz = sz_aligned;
	/* assign relevant cbuf_t values derived from 'buf_sz' */
	b->sz_unused = buf_sz;
	/* beware anti-typing monstrosity to evade 'const' restrictions
	AKA: b->overflow_ = buf_sz - 1; // used as a bitmask later
	*/
	((uint32_t*)&b->overflow_)[0] = buf_sz - 1;


	/* MALLOC ONLY */
	Z_die_if(!(
		b->buf = malloc(buf_sz)
		), "buf_sz = %d", buf_sz);

	Z_log(Z_in2, "cbuf @0x%lx size=%d obj_sz=%d overflow_=0x%x sz_bitshift_=%d flags='%s'",
		(uint64_t)b, cbuf_sz_buf(b), cbuf_sz_obj(b),
		b->overflow_, b->sz_bitshift_,
		cbuf_flags_prn_(b->cbuf_flags));
	return b;
out:
	cbuf_free_(b);
	return NULL;
}

void cbuf_free_(struct cbuf *buf)
{
	/* sanity */
	if (!buf)
		return;
	if (Z_LOG_LVL & Z_in3) {	/* only issue warning if debug level is 4 */
		Z_wrn_if(buf->sz_ready, "cbuf @0x%lx: %ld bytes unconsumed",
			(uint64_t)buf, buf->sz_ready);
	}

	/* mark buffer closing, wait for any pending checkpoints */
	uint16_t cnt = __atomic_or_fetch(&buf->chk_cnt, CBUF_CHK_CLOSING, __ATOMIC_RELAXED);
	while (cnt != CBUF_CHK_CLOSING) { /* this is 0 ongoing checkpoint loops, plus the flag we set */
		sched_yield();
		cnt = __atomic_load_n(&buf->chk_cnt, __ATOMIC_RELAXED);
	}

	/* if allocated, free buffer */
	void *temp;
	temp = (void *)__atomic_exchange_n(&buf->buf, NULL, __ATOMIC_RELAXED);
	if (temp)
		free(temp);

	Z_log(Z_in2, "cbuf @0x%lx", (uint64_t)buf);
	free(buf);
}

/*	cbuf_reserve__()
Reserve a chunk of bytes (identical mechanism for readers as for writers)
Returns new 'pos' (aka: offset)
	*/
uint32_t cbuf_reserve__(struct cbuf		*buf,
			size_t			blk_sz,
			/* TODO: these are NOT volatile: make them ATOMIC only - C11 FTW! */
			volatile int64_t	*sz_source,
			volatile uint32_t	*reserved,
			volatile uint32_t	*pos)
{
	/* TODO: do a fast-path check with a relaxed atomic by enclosing
		this whole thing in an 'if' block?
	*/

	/* Are there sufficient unused 'source' slots? */
	if (__atomic_sub_fetch(sz_source, blk_sz, __ATOMIC_RELAXED) < 0) {
		/* no? Put back the ones we took and bail */
		__atomic_add_fetch(sz_source, blk_sz, __ATOMIC_RELAXED);
		return -1;
	} else {
		/* mark this many bytes as 'reserved' (aka: 'being written) */
		__atomic_add_fetch(reserved, blk_sz, __ATOMIC_RELAXED);
	}

	/* get pos BEFORE increment,
	   mask it against overflow_ (allow overflow, just mask it off),
	   add to base address of buffer
	   ... we MUST mask here, because otherwise we could legitimately
		return -1.
	   */
	return __atomic_fetch_add(pos, blk_sz, __ATOMIC_RELAXED) & buf->overflow_;
}

void cbuf_release__(struct cbuf			*buf,
			size_t			blk_sz,
			volatile uint32_t	*reserved,
			volatile uint32_t	*uncommit,
			volatile int64_t	*sz_dest)
{
	if (*reserved < blk_sz) /* quit playing games with my heart, my heart... */
		return;
	/* Are there more bytes reserved than just the ones we are releasing? */
	if (__atomic_sub_fetch(reserved, blk_sz, __ATOMIC_RELAXED) > 0) {
		/* yes? Add ours to 'uncommitted' rather than releasing. */
		__atomic_add_fetch(uncommit, blk_sz, __ATOMIC_RELAXED);
	} else {
		/* add any uncommitted size to size of commit */
		blk_sz += __atomic_exchange_n(uncommit, 0, __ATOMIC_RELAXED);
		/* commit bytes as ready for destination queue
		   e.g.: unused -> ready | ready -> unused
		   */
		__atomic_add_fetch(sz_dest, blk_sz, __ATOMIC_RELAXED);
	}
}

/*	cbuf_release_scary__()
Utterly ignore committed bytes - Shia: JUST DOOOOIIIIT.

"scary" is usually safe in a single-threaded scenario (i.e.: caller knows
	it is the ONLY thread working with that side of the buffer).
In a multi-threaded setting, "scary" is PROBABLY safe if caller knows
	it holds the EARLIEST reservation, and only releases
	the precise quantity that was reserved THAT time (i.e.: caller is tracking
	'pos' and 'res_cnt' of EACH of ITS OWN reservations, and only releasing
	one reservation at a time).

Returns number of bytes released.
	*/
void cbuf_release_scary__(struct cbuf		*buf,
			size_t			blk_sz,
			volatile uint32_t	*reserved,
			volatile uint32_t	*uncommit,
			volatile int64_t	*sz_dest)
{
	if (*reserved < blk_sz) {
		Z_log_wrn("blk_sz %ld > %d reserved", blk_sz, *reserved);
		return;
	}
	/* remove from reserved */
	__atomic_sub_fetch(reserved, blk_sz, __ATOMIC_RELAXED);

	/* commit bytes as ready for destination queue
	   e.g.: unused -> ready, ready -> unused
	   */
	__atomic_add_fetch(sz_dest, blk_sz, __ATOMIC_RELAXED);
}

/*	cbuf_actuals__()

Take a faux-atomic snapshot of either/both "actual sender" and "actual receiver",
	depending on which pointers are given.

There is no structure-wide mutex which we can lock to guarantee that
	all variables are being read AT THE SAME TIME.
The solution is based on the observation that variables change in predictable WAYS
	(even if at unpredictable times, in unpredictable amounts).

We want to avoid that a "diff" (act_snd - act_rcv == "unprocessed by rcv")
	is TOO large: that would keep a checkpoint loop waiting incorrectly
	(and, if that was the last sender, ENDLESSLY).

Both `act_snd` and `act_rcv` can only increase.
	But "act_snd++ --> diff++" whereas "act_rcv++ --> diff--".
	This means: get `act_snd` FIRST.
Both `act_snd` and `act_rcv` are made up of a "pos" and a counter:
	`act_snd == rcv_pos + sz_ready`
	`act_rcv == sz_unused + snd_pos`
	Of these two, the `pos` can only increase, whereas the counter may
		increase or decrease.
	This means that for `act_snd` (which we want the LOWEST possible value for)
		we should get `rcv_pos` FIRST.
	For `act_rcv` however (which we want the HIGHEST possible value for)
		we should get the counter (sz_unused) first.

Both values are masked so as to give actual position values.
	*/
void	cbuf_actuals__(struct cbuf *buf, uint32_t *act_snd, uint32_t *act_rcv)
{
	/* TODO: write lock elision code for this */
	uint32_t act_s, act_r;

	/* top-level atomic stores forcing sequence:
		get sender before receiver.
	*/
	__atomic_store_n(&act_s,
		(	__atomic_load_n(&buf->rcv_pos, __ATOMIC_SEQ_CST)
			+ __atomic_load_n(&buf->sz_ready, __ATOMIC_SEQ_CST)
		)
		& buf->overflow_,
		__ATOMIC_SEQ_CST);

	__atomic_store_n(&act_r,
		(	__atomic_load_n(&buf->sz_unused, __ATOMIC_SEQ_CST)
			+ __atomic_load_n(&buf->snd_pos, __ATOMIC_SEQ_CST)
		)
		& buf->overflow_,
		__ATOMIC_SEQ_CST);

	if (act_snd)
		*act_snd = act_s;
	if (act_rcv)
		*act_rcv = act_r;
}

/*	cbuf_flags_prn_()
Useful utility for showing flag contents in print statements.
	*/
const char *cbuf_flags_prn_(uint8_t cbuf_flags)
{
	Z_die_if(cbuf_flags > 3, "%d out of range", cbuf_flags);
	const char *flags[] = {
		"NONE",
		"CBUF_P",
		"CBUF_MALLOC",
		"CBUF_P | CBUF_MALLOC"
	};
	return flags[cbuf_flags];
out:
	return NULL;
}
