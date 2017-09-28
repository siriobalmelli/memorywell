#include <zed_dbg.h>
#include <nbuf.h>
#include <nmath.h>

#include <sched.h> /* sched_yield() */

/* TODO:
	- compiler barf if size_t is not atomic
	- generic nmath functions so 32-bit size_t case is cared for
	- implement differing compile-time concurrency strategies:
		i.) CAS loop
		ii.) Exchange
		iii.) Mutex
		iv.) Spinlock?
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

	/* mark available slots-count */
	out->tx.avail = out->ct.overflow / out->ct.blk_sz;
	/* derive a bitmask from size */
	out->ct.overflow--;

out:
	return err_cnt;
}


/*	nbuf_init()
Initialize an nbuf struct 'nb' with (caller-allocated) 'mem'.
This function expects 'nb' to have had nbuf_params() successfully called on it,
	and for 'mem' to be at least nbuf_size(nb) large.

The reason for this more complicated initialization pattern is to allow
	caller full control over 'mem' without bringing complexities
	into this library.

returns 0 on success
*/
int nbuf_init(struct nbuf *nb, void *mem)
{
	int err_cnt = 0;
	Z_die_if(!mem, "");

	nb->ct.buf = mem;

#if (NBUF_TECHNIQUE == NBUF_DO_MTX)
	Z_die_if(pthread_mutex_init(&nb->tx.lock, NULL), "");
	Z_die_if(pthread_mutex_init(&nb->rx.lock, NULL), "");
#endif

out:
	return err_cnt;
}


/*	nbuf_deinit()
*/
void nbuf_deinit(struct nbuf *nb)
{
#if (NBUF_TECHNIQUE == NBUF_DO_MTX)
	Z_die_if(pthread_mutex_destroy(&nb->tx.lock), "");
	Z_die_if(pthread_mutex_destroy(&nb->rx.lock), "");
out:
	return;
#endif
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
	/* derive number of bytes wanted */
	size_t size;
	if (__builtin_mul_overflow(blk_cnt, ct->blk_sz, &size))
		return -1;


	/*	critical section

	Data synchronization with other threads relies on these calls,
		so minimum memory model here is ACQUIRE.
	Also, no operation on 'pos' should be hoisted into/above this block.
	*/
#if (NBUF_TECHNIQUE == NBUF_DO_CAS)
	size_t avail = __atomic_load_n(&from->avail, __ATOMIC_RELAXED);
	if (avail < size)
		goto fail;

	/* Loop on spurious failures or other writes as long as we still have
		a chance to reserve.
	*/
	while (!(__atomic_compare_exchange_n(&from->avail, &avail, avail-size, 1,
					__ATOMIC_ACQUIRE, __ATOMIC_RELAXED)
	)) {
		if (avail < size)
			goto fail;
	}

#elif (NBUF_TECHNIQUE == NBUF_DO_XCH)
	size_t avail = __atomic_exchange_n(&from->avail, 0, __ATOMIC_ACQUIRE);
	if (!avail) {
		goto fail;
	} else if (avail < size) {
		__atomic_add_fetch(&from->avail, avail, __ATOMIC_RELAXED);
		goto fail;
	} else {
		/* Only ever ADD back in: otherwise multiple concurrent
			failures would either lose data or have to CAS-loop.
		*/
		__atomic_add_fetch(&from->avail, avail-size, __ATOMIC_RELAXED);
	};

#elif (NBUF_TECHNIQUE == NBUF_DO_MTX)
	size_t ret = -1;
	pthread_mutex_lock(&from->lock);
		if (from->avail >= size) {
			from->avail -= size;
			ret = from->pos;
			from->pos += size;
		}
	pthread_mutex_unlock(&from->lock);
	return ret;

#else
#error "nbuf technique not implemented"
#endif

#if (NBUF_TECHNIQUE == NBUF_DO_CAS || NBUF_TECHNIQUE == NBUF_DO_XCH)
	/* post-critical: we've synchronized availability
		and don't contend for position since we're _single()
		**on this side of the buf** - other side may be _multi()!
	*/
	return __atomic_fetch_add(&from->pos, size, __ATOMIC_RELAXED);
fail:
	sched_yield();
	return -1;
#endif
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

#if (NBUF_TECHNIQUE == NBUF_DO_CAS || NBUF_TECHNIQUE == NBUF_DO_XCH)
	__atomic_add_fetch(&to->avail, size, __ATOMIC_RELEASE);

#elif (NBUF_TECHNIQUE == NBUF_DO_MTX)
	pthread_mutex_lock(&to->lock);
		to->avail += size;
	pthread_mutex_unlock(&to->lock);

#else
#error "nbuf technique not implemented"
#endif

	return 0;
}
