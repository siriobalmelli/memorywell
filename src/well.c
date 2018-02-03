#include <zed_dbg.h>
#include <well.h>
#include <nmath.h>

/*
	compile-time sanity
*/
NLC_ASSERT(size_t_is_pointer, sizeof(size_t) == sizeof(void *));
NLC_ASSERT(size_t_is_atomic, __atomic_always_lock_free(sizeof(size_t), 0) == 1);


/*
	readability for locking implementations
*/
#if (WELL_TECHNIQUE == WELL_DO_MTX)
	/* returns 0 if lock is acquired */
	#define TRYLOCK_(lock_ptr) \
		pthread_mutex_trylock(lock_ptr)
	#define LOCK_(lock_ptr) \
		pthread_mutex_lock(lock_ptr)
	#define UNLOCK_(lock_ptr) \
		pthread_mutex_unlock(lock_ptr)

#elif (WELL_TECHNIQUE == WELL_DO_SPL)
	#define TRYLOCK_(lock_ptr) \
		__atomic_test_and_set(lock_ptr, __ATOMIC_ACQUIRE)
		//__atomic_exchange_n(lock_ptr, 1, __ATOMIC_ACQUIRE)
	#define LOCK_(lock_ptr) \
		while (TRYLOCK_(lock_ptr)) \
			;
	#define UNLOCK_(lock_ptr) \
		__atomic_clear(lock_ptr, __ATOMIC_RELEASE)
		//__atomic_exchange_n(lock_ptr, 0, __ATOMIC_RELEASE)
#endif


/*	well_params()
Calculate required sizes for a well.
Memory allocation is left as an excercise to the caller so as to
	permit integration e.g. with nonlibc's nman for zero-copy I/O etc etc.
Write required sizes in '*out'.

Call well_size() on '*out' to get required buffer size.
Call well_blk_size() on '*out' to get individual block size
	(which has been promoted to next power of 2).

returns 0 on success
*/
int well_params(size_t blk_size, size_t blk_cnt, struct well *out)
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


/*	well_init()
Initialize an well struct 'buf' with (caller-allocated) 'mem'.
This function expects 'buf' to have had well_params() successfully called on it,
	and for 'mem' to be at least well_size(buf) large.

The reason for this more complicated initialization pattern is to allow
	caller full control over 'mem' without bringing complexities
	into this library.

returns 0 on success
*/
int well_init(struct well *buf, void *mem)
{
	int err_cnt = 0;
	Z_die_if(!buf, "");
	buf->tx.release_pos = buf->rx.release_pos = 0;

	Z_die_if(!mem, "");
	buf->ct.buf = mem;


#if (WELL_TECHNIQUE == WELL_DO_MTX)
	Z_die_if(pthread_mutex_init(&buf->tx.lock, NULL), "");
	Z_die_if(pthread_mutex_init(&buf->rx.lock, NULL), "");
#elif (WELL_TECHNIQUE == WELL_DO_SPL)
	buf->tx.lock = buf->rx.lock = 0;
#endif

out:
	return err_cnt;
}


/*	well_deinit()
*/
void well_deinit(struct well *buf)
{
#if (WELL_TECHNIQUE == WELL_DO_MTX)
	Z_die_if(pthread_mutex_destroy(&buf->tx.lock), "");
	Z_die_if(pthread_mutex_destroy(&buf->rx.lock), "");
out:
	return;
#endif
}


/*	well_reserve()
Reserve up to 'max_count' buffer blocks;
	single OR multiple producers/consumers.

Returns a 'struct well_res' containing:
	- number of slots reserved as 'cnt'
	- an opaque "position" variable as 'pos'.

Use _access() functions with 'pos' to obtain valid pointers.

On failure, 'cnt == 0' and 'pos' is garbage.

NOTE ON TIMING: will not wait; will not spin.
	Caller decides whether to sleep(), yield() or whatever.
*/
struct well_res	well_reserve(	struct well_sym	*from,
				size_t		max_count)
{
	/* Don't waste cycles setting this; but must ASSUME it's garbage
		until explicitly set.
	*/
	struct well_res ret;

#if (WELL_TECHNIQUE == WELL_DO_CAS)
	ret.cnt = __atomic_load_n(&from->avail, __ATOMIC_RELAXED);
	do {
		/* fail early and cheaply */
		if (!ret.cnt)
			return ret;
		/* prevent CAS'ing a negative integer into 'avail' */
		if (ret.cnt < max_count)
			max_count = ret.cnt;
	} while (!(__atomic_compare_exchange_n(&from->avail, &ret.cnt, ret.cnt - max_count,
						1, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)));
	ret.pos = __atomic_fetch_add(&from->pos, max_count, __ATOMIC_RELAXED);
	/* penalty for failing cheaply: succeed expensively */
	ret.cnt = max_count;
	return ret;


#elif (WELL_TECHNIQUE == WELL_DO_XCH)
	ret.cnt = __atomic_exchange_n(&from->avail, 0, __ATOMIC_ACQUIRE);
	if (!ret.cnt)
		return ret;

	if (ret.cnt > max_count) {
		__atomic_fetch_add(&from->avail, ret.cnt-max_count, __ATOMIC_RELAXED);
		ret.cnt = max_count;
	}
	ret.pos = __atomic_fetch_add(&from->pos, ret.cnt, __ATOMIC_RELAXED);
	return ret;


#elif (WELL_TECHNIQUE == WELL_DO_MTX || WELL_TECHNIQUE == WELL_DO_SPL)
	ret.cnt = 0;
	if (!TRYLOCK_(&from->lock)) {
		if (from->avail) {
			if (from->avail < max_count) {
				max_count = from->avail;
				from->avail = 0;
			} else {
				from->avail -= max_count;
			}
			ret.pos = from->pos;
			from->pos += max_count;
			ret.cnt = max_count;
		}
		UNLOCK_(&from->lock);
	}
	return ret;


#else
#error "well technique not implemented"
#endif
}



/*	well_release_single()
Release 'count' buffer blocks.

WARNING: ONLY call from SINGLE producer/consumer.
	Otherwise, use _multi() version below.

NOTE RE LOCKING: this function will always succeed;
	this means we are obliged to mutex_wait or spinlock until we acquire a lock.
*/
void well_release_single(struct well_sym	*to,
				size_t		count)
{
#if (WELL_TECHNIQUE == WELL_DO_CAS || WELL_TECHNIQUE == WELL_DO_XCH)
	__atomic_add_fetch(&to->avail, count, __ATOMIC_RELEASE);


#elif (WELL_TECHNIQUE == WELL_DO_MTX || WELL_TECHNIQUE == WELL_DO_SPL)
	LOCK_(&to->lock);
		to->avail += count;
	UNLOCK_(&to->lock);


#else
#error "well technique not implemented"
#endif
}



/*	well_release_multi()
Release a reservation made under contention (multiple threads on RX or TX side).
Requires 'res_pos' which is the 'pos' value written by an earlier successful
	call to reserve().
WARNINGS:
	- nonsense values of 'count' or 'res_pos' can lock up the entire buffer.
	- NEVER use both _release_single() and _release_multi()
		on the same side of the buffer.

returns 0 on failure, original value of 'count' on success.
*/
size_t	well_release_multi(struct well_sym	*to,
				size_t		count,
				size_t		res_pos)
{
#if (WELL_TECHNIQUE == WELL_DO_CAS || WELL_TECHNIQUE == WELL_DO_XCH)
	if (!__atomic_compare_exchange_n(&to->release_pos, &res_pos, res_pos + count,
					0, __ATOMIC_RELAXED, __ATOMIC_RELAXED))
		return 0;

	__atomic_add_fetch(&to->avail, count, __ATOMIC_RELEASE);
	return count;


#elif (WELL_TECHNIQUE == WELL_DO_MTX || WELL_TECHNIQUE == WELL_DO_SPL)
	size_t ret = 0;
	if (!TRYLOCK_(&to->lock)) {
		if (to->release_pos == res_pos) {
			to->avail += count;
			to->release_pos += count;
			ret = count;
		}
		UNLOCK_(&to->lock);
	}
	return ret;


#else
#error "well technique not implemented"
#endif
}
