#include <zed_dbg.h>
#include <nbuf.h>
#include <nmath.h>

/* TODO:
	- compiler barf if size_t is not atomic
	- generic nmath functions so the 32-bit case is cared for
*/


/*	nbuf_params()
Calculate required sizes for an nbuf.
Memory allocation is left as an excercise to the caller so as to
	permit integration e.g. with nonlibc's nman for zero-copy I/O etc etc.
Write required sizes in '*out'.

Call nbuf_size() on '*out' to get required buffer size.
Call nbuf_blk_sz() on '*out' to get individual block size
	(which has been promoted to next power of 2).

returns 0 on success
*/
int nbuf_params(size_t blk_sz, size_t blk_cnt, struct nbuf *out)
{
	int err_cnt = 0;

	/* block sizes are a power of 2 */
	out->ct.blk_sz = 0;
	/* should go away by the time compiler is through with it :P */
	out->ct.blk_sz = nm_next_pow2_64(blk_sz);
	Z_die_if(out->ct.blk_sz < blk_sz, "blk_sz %zu overflow", blk_sz);

	/* final size is not abortive */
	size_t size;
	Z_die_if(__builtin_mul_overflow(out->ct.blk_sz, blk_cnt, &size),
		"%zu many %zu-sized blocks overflows",
		out->ct.blk_sz, blk_cnt);
	out->ct.overflow = nm_next_pow2_64(size);
	Z_die_if(out->ct.overflow < size, "buffer size %zu overflow", size);

	/* derive a bitmask from size */
	out->ct.overflow--;

out:
	return err_cnt;
}

/*	nbuf_reserve_single()
A single producer/consumer reserves a buffer block.

Returns an opaque 'pos' value which much be properly dereferenced
	in order to obviate problems looping around the end of the buffer.

Return -1 on failure: -1 is an ODD number and our blocks are size pow(2),
	and aligned at offset 0; so always EVEN.
*/
size_t nbuf_reserve_single(const struct nbuf_const	*ct,
				struct nbuf_sym		*from,
				size_t			blk_cnt)
{
	/* Non-critical: derive number of bytes wanted and read SOME value of availability;
		compiler free to speculate on whether this op will succeed.
	*/
	size_t size;
	if (__builtin_mul_overflow(blk_cnt, ct->blk_sz, &size))
		return -1;
	size_t avail = __atomic_load_n(&from->avail, __ATOMIC_RELAXED);
	if (avail < size)
		return -1;

	/* Loop on spurious failures or other writes as long as we still have
		a chance to reserve.
	Data synchronization with other threads relies on these calls,
		so minimum memory model here is ACQUIRE.
	Also, no operation on 'pos' should be hoisted into/above this block.
	*/
	while (!(__atomic_compare_exchange_n(&from->avail, &avail, avail-size, 1,
					__ATOMIC_ACQUIRE, __ATOMIC_ACQUIRE)
	)) {
		if (avail < size)
			return -1;
	}

	/* post-critical: we've synchronized availability
		and don't contend for position since we're single
		__on this side of the buf__
	*/
	return from->pos += size;
}


/*	nbuf_release_single()
Return 0 on success
*/
int nbuf_release_single(const struct nbuf_const	*ct,
				struct nbuf_sym		*to,
				size_t			blk_cnt)
{
	size_t size;
	if (__builtin_mul_overflow(blk_cnt, ct->blk_sz, &size))
		return -1;
	__atomic_add_fetch(&to->avail, size, __ATOMIC_RELEASE);
	return 0;
}
