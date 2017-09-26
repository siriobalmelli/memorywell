#ifndef nbuf_h_
#define nbuf_h_

#include <stddef.h>
#include <nonlibc.h>

/*	nbuf_const
Data which should not change after initializiation; goes on it's own
	cache line so it's never invalid.
*/
struct nbuf_const {
	void		*buf;
	size_t		overflow;	/* Used for quick masking of `pos` variables.
					It's also `buf_sz -1`, and is used
						in lieu of a dedicated `buf_sz`
						variable to keep struct size <=64B.
				       */
	size_t		blk_sz;		/* Block size is a power of 2 */

};


/*	nbuf_sym
a (symmetrical) half of a circular buffer
*/
struct nbuf_sym {
	size_t		pos;	/* head/tail of buffer */
	size_t		avail;	/* can be reserved */

	/* multi-read or multi-write contention only */
	size_t		reserved;
	size_t		uncommitted;
};


/* TODO: determine this at compile time for a given architecture */
#ifndef CACHE_LINE
#define CACHE_LINE 64
#endif

/*	nbuf
circular buffer with proper alignment
*/
struct nbuf {
	/* cache line 1: all the unchanging stuff that should NEVER be invalidated */
	struct nbuf_const	ct;
	unsigned char		pad_ln1[CACHE_LINE - sizeof(struct nbuf_const)];
	/* cache line 2: tx side */
	struct nbuf_sym		tx;
	unsigned char		pad_ln2[CACHE_LINE - sizeof(struct nbuf_sym)];
	/* cache line 3: rx side */
	struct nbuf_sym		rx;
	unsigned char		pad_ln3[CACHE_LINE - sizeof(struct nbuf_sym)];
};



NLC_INLINE size_t nbuf_size(const struct nbuf *nb)
{
	return nb->ct.overflow + 1;
}

NLC_INLINE size_t nbuf_blk_sz(const struct nbuf *nb)
{
	return nb->ct.blk_sz;
}

NLC_INLINE void *nbuf_access(size_t pos, size_t i, const struct nbuf *nb)
{
	pos += i * nb->ct.blk_sz;
	return nb->ct.buf + (pos & nb->ct.overflow);
}

int nbuf_params(size_t blk_sz, size_t blk_cnt, struct nbuf *out);

size_t nbuf_reserve_single(const struct nbuf_const	*ct,
				struct nbuf_sym		*from,
				size_t			blk_cnt);

int nbuf_release_single(const struct nbuf_const		*ct,
				struct nbuf_sym		*to,
				size_t			blk_cnt);

#endif /* nbuf_h_ */
