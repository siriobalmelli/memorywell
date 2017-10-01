#include <zed_dbg.h>
#include <nbuf.h>
#include <nmath.h>

/* TODO:
	- compiler barf if size_t is not atomic
	- generic nmath functions so 32-bit size_t case is cared for
	- speed differential if combining cache lines
	- warnings on BSD
	- mutex runs forever on BSD
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
int nbuf_params(size_t blk_size, size_t blk_cnt, struct nbuf *out)
{
	int err_cnt = 0;

	/* should go away by the time compiler is through with it :P */
	out->ct.blk_size = nm_next_pow2_64(blk_size);
	Z_die_if(out->ct.blk_size < blk_size, "blk_size %zu overflow", blk_size);
	/* left-shift when multiplying by block size */
	out->ct.blk_shift = nm_bit_pos(out->ct.blk_size) -1;

	size_t size;
	/* final size is not abortive */
	Z_die_if(__builtin_mul_overflow(out->ct.blk_size, blk_cnt, &size),
		"%zu many %zu-sized blocks overflows",
		out->ct.blk_size, blk_cnt);
	/* final size must be a power of 2 */
	out->ct.overflow = nm_next_pow2_64(size);
	Z_die_if(out->ct.overflow < size, "buffer size %zu overflow", size);

	/* mark available block-count */
	out->tx.avail = out->ct.overflow >> out->ct.blk_shift;
	/* turn size into a bitmask */
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

Returns 0 on failure.
Otherwise, returns number of slots reserved, and outputs an opaque
	"position" variable into '*out_pos'.
Use _access() with '*out_pos' to obtain valid pointers.

NOTE: there is no sanity check on 'count'; it should be less than the
	blocks in the buffer.
*/
size_t nbuf_reserve_single(const struct nbuf_const	*ct,
				struct nbuf_sym		*from,
				size_t			*out_pos,
				size_t			count)
{
	/*	critical section

	Data synchronization with other threads relies on these calls,
		so minimum memory model here is ACQUIRE.
	Also, no operation on 'pos' should be hoisted into/above this block.
	*/
#if (NBUF_TECHNIQUE == NBUF_DO_CAS)
	size_t avail = __atomic_load_n(&from->avail, __ATOMIC_RELAXED);
	if (avail < count)
		return 0;

	/* Loop on spurious failures or other writes as long as we still have
		a chance to reserve.
	*/
	while (!(__atomic_compare_exchange_n(&from->avail, &avail, avail-count, 1,
					__ATOMIC_ACQUIRE, __ATOMIC_RELAXED)
	)) {
		if (avail < count)
			return 0;
	}
	/* post-critical: we've synchronized availability
		and don't contend for position since we're _single()
		**on this side of the buf** - other side may be _multi()!
	*/
	*out_pos = __atomic_fetch_add(&from->pos, count, __ATOMIC_RELAXED);
	return count;


#elif (NBUF_TECHNIQUE == NBUF_DO_XCH)
	size_t avail = __atomic_exchange_n(&from->avail, 0, __ATOMIC_ACQUIRE);
	/* no slots available */
	if (!avail) {
		return 0;

	/* too few slots available */
	} else if (avail < count) {
		/* Only ever ADD back in: otherwise multiple concurrent
			failures would either lose data or have to CAS-loop.
		*/
		__atomic_add_fetch(&from->avail, avail, __ATOMIC_RELAXED);
		return 0;

	/* too many slots available */
	} else if (avail > count) {
		__atomic_add_fetch(&from->avail, avail-count, __ATOMIC_RELAXED);
	}
	/* just right: goldilocks gets her slots */
	*out_pos = __atomic_fetch_add(&from->pos, count, __ATOMIC_RELAXED);
	return count;


#elif (NBUF_TECHNIQUE == NBUF_DO_MTX)
	size_t ret = 0;
	pthread_mutex_lock(&from->lock);
		if (from->avail >= count) {
			from->avail -= count;
			*out_pos = from->pos;
			from->pos += count;
			ret = count;
		}
	pthread_mutex_unlock(&from->lock);
	return ret;

#else
#error "nbuf technique not implemented"
#endif
}


/*	nbuf_reserve_single_var()
Variable reservation size - allows for opportunistic reservation as long
	as any amount of blocks are available.
Outputs number of blocks reserved to 'out_size'.

See _reserve_single() above for notes.
*/
size_t nbuf_reserve_single_var(const struct nbuf_const	*ct,
				struct nbuf_sym		*from,
				size_t			*out_pos)
{
#if (NBUF_TECHNIQUE == NBUF_DO_CAS || NBUF_TECHNIQUE == NBUF_DO_XCH)
	size_t count = __atomic_exchange_n(&from->avail, 0, __ATOMIC_ACQUIRE);
	if (!count)
		return 0;
	*out_pos = __atomic_fetch_add(&from->pos, count, __ATOMIC_RELAXED);
	return count;

#elif (NBUF_TECHNIQUE == NBUF_DO_MTX)
	size_t count = 0;
	pthread_mutex_lock(&from->lock);
		if (from->avail) {
			*out_pos = from->pos;
			from->pos += count = from->avail;
			from->avail = 0;
		}
	pthread_mutex_unlock(&from->lock);
	return count;

#else
#error "nbuf technique not implemented"
#endif
}


/*	nbuf_release_single()
Reciprocal of nbuf_reserve_single() above; see it's header blurb for notes.
*/
void nbuf_release_single(const struct nbuf_const	*ct,
				struct nbuf_sym		*to,
				size_t			count)
{
#if (NBUF_TECHNIQUE == NBUF_DO_CAS || NBUF_TECHNIQUE == NBUF_DO_XCH)
	__atomic_add_fetch(&to->avail, count, __ATOMIC_RELEASE);

#elif (NBUF_TECHNIQUE == NBUF_DO_MTX)
	pthread_mutex_lock(&to->lock);
		to->avail += count;
	pthread_mutex_unlock(&to->lock);

#else
#error "nbuf technique not implemented"
#endif
}
