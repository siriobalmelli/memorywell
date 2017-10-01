#include <zed_dbg.h>
#include <nbuf.h>
#include <nmath.h>

/* TODO:
	- compiler barf if size_t is not atomic
	- generic nmath functions so 32-bit size_t case is cared for
	- speed differential if combining cache lines
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
	/* left-shift when multiplying by block size */
	out->ct.blk_shift = nm_bit_pos(out->ct.blk_sz) -1;

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


/*	nbuf_reservation_size()
Compute a sane "reservation size" to pass to _reserve() or _release()
	functions.
Reservation size is always a multiple of 'blk_cnt'.

This function exists because caller usually knows reservation size before
	looping through *many* reservations; makes sense to do this compute
	only once, and allows us to be more pedantic in checking for errors.
*/
size_t __attribute__((const))
	nbuf_reservation_size(const struct nbuf	*nb, size_t blk_cnt)
{
	size_t size;
	if (__builtin_mul_overflow(blk_cnt, nb->ct.blk_sz, &size))
		return 0;
	if (size > nb->ct.overflow +1)
		return 0;
	return size;
}


/*	nbuf_reserve_single()
A single producer/consumer reserves a buffer block.

Returns an opaque 'pos' value which much be properly dereferenced
	in order to obviate problems looping around the end of the buffer.

Return -1 on failure: -1 is an ODD number and our blocks are size pow(2),
	and aligned at offset 0; so always EVEN.

NOTE: 'size' is in BYTES (not "blocks") and we do NOT sanity-check 'size':
	- insanely large values will fail
	- values not a multiple of blk_sz will irrevocably
		screw the entire buffer
THEREFORE: ALWAYS obtain size by calling nbuf_reservation_size();
*/
size_t nbuf_reserve_single(const struct nbuf_const	*ct,
				struct nbuf_sym		*from,
				size_t			size)
{
	/*	critical section

	Data synchronization with other threads relies on these calls,
		so minimum memory model here is ACQUIRE.
	Also, no operation on 'pos' should be hoisted into/above this block.
	*/
#if (NBUF_TECHNIQUE == NBUF_DO_CAS)
	size_t avail = __atomic_load_n(&from->avail, __ATOMIC_RELAXED);
	if (avail < size)
		return -1;

	/* Loop on spurious failures or other writes as long as we still have
		a chance to reserve.
	*/
	while (!(__atomic_compare_exchange_n(&from->avail, &avail, avail-size, 1,
					__ATOMIC_ACQUIRE, __ATOMIC_RELAXED)
	)) {
		if (avail < size)
			return -1;
	}

#elif (NBUF_TECHNIQUE == NBUF_DO_XCH)
	size_t avail = __atomic_exchange_n(&from->avail, 0, __ATOMIC_ACQUIRE);
	if (!avail) {
		return -1;
	} else if (avail < size) {
		__atomic_add_fetch(&from->avail, avail, __ATOMIC_RELAXED);
		return -1;
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
#endif
}


/*	nbuf_reserve_single_var()
Variable reservation size - allows for opportunistic reservation as long
	as any amount of blocks are available.
Outputs number of bytes reserved to 'out_size'.

See _reserve_single() above for notes.
*/
size_t nbuf_reserve_single_var(const struct nbuf_const	*ct,
				struct nbuf_sym		*from,
				size_t			*out_size)
{
#if (NBUF_TECHNIQUE == NBUF_DO_CAS || NBUF_TECHNIQUE == NBUF_DO_XCH)
	*out_size = __atomic_exchange_n(&from->avail, 0, __ATOMIC_ACQUIRE);
	if (!(*out_size))
		return -1;
	return __atomic_fetch_add(&from->pos, *out_size, __ATOMIC_RELAXED);

#elif (NBUF_TECHNIQUE == NBUF_DO_MTX)
	size_t ret = -1;
	pthread_mutex_lock(&from->lock);
		if (from->avail) {
			ret = from->pos;
			from->pos += *out_size = from->avail;
			from->avail = 0;
		}
	pthread_mutex_unlock(&from->lock);
	return ret;

#else
#error "nbuf technique not implemented"
#endif
}


/*	nbuf_release_single()
Reciprocal of nbuf_reserve_single() above; see it's header blurb for notes.
Return 0 on success
*/
int nbuf_release_single(const struct nbuf_const	*ct,
				struct nbuf_sym		*to,
				size_t			size)
{
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
