#include "cbuf_int.h"

#ifdef Z_BLK_LVL
#undef Z_BLK_LVL
#endif
#define Z_BLK_LVL 1
/* debug levels:
	1: 
	2:
	3: cbuf create/del
	4: unused bytes at time of free_()
*/

static uint32_t cbuf_hugepage_sz; /* for future hugepages support */

int __attribute__ ((constructor))	cbuf_const()
{
	// TODO: use a more, ahem, empirical method to determine this.
	cbuf_hugepage_sz = 2048;	

	return 0;
}

int __attribute__ ((destructor))	cbuf_dest()
{
	return 0;
}

/** INTERNALS **/

/*	next_pow2()
Returns next higher power of 2, or itself if already power of 2.
Shamelessly ripped off of an S/O thread.
	*/
uint32_t next_pow2(uint32_t x)
{
	Z_die_if(!x, "no number to power");
	x--;
	x |= x >> 1;
	x |= x >> 2;
	x |= x >> 4;
	x |= x >> 8;
	x |= x >> 16;

	return x+1;
out:
	return 0;
}

/*	next_multiple()
Returns `x` if `x` divides evenly into `mult`
Else returns next multiple of mult above x
	*/
uint32_t next_multiple(uint32_t x, uint32_t mult)
{
	return ((x + (mult -1)) / mult) * mult;
}

cbuf_t *cbuf_create_(uint32_t obj_sz, 
			uint32_t obj_cnt, 
			uint8_t flags) 
{
	cbuf_t *b = NULL;
	Z_die_if(!obj_sz, "expecting object size");
	b = calloc(1, sizeof(cbuf_t));
	Z_die_if(!b, "no buf stat");
	memset(b, 0x0, sizeof(cbuf_t));
	b->cbuf_flags = flags;

	/* alignment:
		obj_sz must be a power of 2
		buf_sz must be a multiple of obj_sz AND a power of 2
		*/
	obj_sz = next_pow2(obj_sz);
	uint32_t buf_sz = obj_sz * obj_cnt;
	buf_sz = next_pow2(next_multiple(buf_sz, obj_sz));
	Z_err_if(buf_sz == -1, "buffer or object size too big");
	b->overflow_ = buf_sz - 1; /* used as a bitmask later */
	b->sz_unused = buf_sz;

	/* calc shift value necessary to turn `buf_sz / obj_sz` 
		into a bitwise op */
	uint32_t temp = obj_sz;
	while (temp > 1) {
		temp >>= 1;
		b->sz_bitshift_++;
	}

	/* MMAP 
	The fuss with UINT16_MAX is because we only had 16 bits
		left before the cache line boundary in cbuf_t.
		*/
	char tfile[] = "/tmp/cbufXXXXXX";
	int fd = mkostemp(tfile, O_NOATIME);
	Z_die_if(fd == -1 || fd > UINT16_MAX, "temp");
	/* make space, map */
	size_t len = next_multiple(buf_sz, cbuf_hugepage_sz);
	Z_die_if(ftruncate(fd, len), "");
	/* MUST be MAP_SHARED. If not, cbuf -> file splices WILL NOT WRITE to disk. */
	b->buf = mmap(NULL, len, (PROT_READ | PROT_WRITE), 
		(MAP_SHARED | MAP_LOCKED | MAP_NORESERVE), fd, 0);
	Z_die_if(b->buf == MAP_FAILED, "sz:%ld", len);
	b->mmap_fd = fd;
	Z_die_if(unlink(tfile), "");

	/* open pipes */
	Z_die_if(pipe2(b->plumbing, O_NONBLOCK), "");

	Z_inf(3, "cbuf @0x%lx size=%d obj_sz=%d overflow_=0x%x sz_bitshift_=%d", 
	     (uint64_t)b, cbuf_sz_buf(b), cbuf_sz_obj(b), b->overflow_, b->sz_bitshift_);
	return b;
out:
	cbuf_free_(b);
	return NULL;
}

void cbuf_free_(cbuf_t *buf)
{
	/* sanity */
	if (!buf)
		return;
	Z_DO(4, /*  only issue warning if debug level is 4 */
		Z_warn_if(buf->sz_ready, "cbuf @0x%lx: %ld bytes unconsumed", 
			(uint64_t)buf, buf->sz_ready);
	);

	/* free memory */
	if (buf->buf) {

		/* handle backing store (cbufp_) ? */
		if (buf->cbuf_flags & CBUF_P) {
			cbufp_t *f = buf->buf;
			sbfu_unmap(f->fd, &f->iov);
			unlink(f->file_path); 
			free(f->file_path);
			errno = 0;
		}

		/* avoid trying to free a '-1' (aka: MAP_FAILED) */
		if (buf->buf != MAP_FAILED)
			munmap(buf->buf, next_multiple(cbuf_sz_buf(buf), cbuf_hugepage_sz));
	}

	/* open file descriptors */
	if (buf->mmap_fd)
		close(buf->mmap_fd);
	if (buf->plumbing[0]) {
		Z_err_if(close(buf->plumbing[0]), "");
	}
	if (buf->plumbing[1]) {
		Z_err_if(close(buf->plumbing[1]), "");
	}

	Z_inf(3, "cbuf @0x%lx", (uint64_t)buf);
	free(buf);
}

/*	cbuf_reserve__()
Reserve a chunk of bytes (identical mechanism for readers as for writers)
Returns new 'pos' (aka: offset)
	*/
uint32_t cbuf_reserve__(cbuf_t		*buf,
			size_t		blk_sz,
			int64_t		*sz_source, 
			uint32_t	*reserved,
			uint32_t	*pos)
{

	/* Are there sufficient unused 'source' slots? */ 
	if (__atomic_sub_fetch(sz_source, blk_sz, __ATOMIC_SEQ_CST) < 0) { 
		/* no? Put back the ones we took and bail */
		__atomic_add_fetch(sz_source, blk_sz, __ATOMIC_SEQ_CST);
		return -1;
	} else {
		/* mark this many bytes as 'reserved' (aka: 'being written) */
		__atomic_add_fetch(reserved, blk_sz, __ATOMIC_SEQ_CST);
	}

	/* get pos BEFORE increment, 
	   mask it against overflow_ (allow overflow, just mask it off),
	   add to base address of buffer
	   ... we MUST mask here, because otherwise we could legitimately
		return -1.
	   */
	return __atomic_fetch_add(pos, blk_sz, __ATOMIC_SEQ_CST) & buf->overflow_;
}

void cbuf_release__(cbuf_t		*buf,
			size_t		blk_sz,
			uint32_t	*reserved,
			uint32_t	*uncommit,
			int64_t		*sz_dest)
{
	if (*reserved < blk_sz) /* quit playing games with my heart, my heart... */
		return;
	/* Are there more bytes reserved than just the ones we are releasing? */
	if (__atomic_sub_fetch(reserved, blk_sz, __ATOMIC_SEQ_CST) > 0) {
		/* yes? Add ours to 'uncommitted' rather than releasing. */
		__atomic_add_fetch(uncommit, blk_sz, __ATOMIC_SEQ_CST); 
	} else {
		/* add any uncommitted size to size of commit */
		blk_sz += __atomic_exchange_n(uncommit, 0, __ATOMIC_SEQ_CST);
		/* commit bytes as ready for destination queue 
		   e.g.: unused -> ready | ready -> unused
		   */
		__atomic_add_fetch(sz_dest, blk_sz, __ATOMIC_SEQ_CST);
	}
}

/* utterly ignore committed bytes - Shia: JUST DOOOOIIIIT 
	... returns number of bytes released.
	*/
void cbuf_release_scary__(cbuf_t		*buf,
			size_t		blk_sz,
			uint32_t	*reserved,
			uint32_t	*uncommit,
			int64_t		*sz_dest)
{
	if (*reserved < blk_sz) {
		Z_wrn("blk_sz %ld > %d reserved", blk_sz, *reserved);
		return;
	}
	/* remove from reserved */
	__atomic_sub_fetch(reserved, blk_sz, __ATOMIC_SEQ_CST);

	/* commit bytes as ready for destination queue 
	   e.g.: unused -> ready, ready -> unused
	   */
	__atomic_add_fetch(sz_dest, blk_sz, __ATOMIC_SEQ_CST);
}

#undef Z_BLK_LVL
#define Z_BLK_LVL 0
