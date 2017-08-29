#ifndef zcio_h_
#define zcio_h_

/*	ZCIO == Zero-Copy I/O library.
Implements a "backing store" behind a cbuf.

TODO: Sirio: flesh out documentation, clean up comments in cbuf

NOTE: caller must interface with cbuf 'cb' DIRECTLY - no cbuf
	functionality is duplicated by this library.
*/

#include <nonlibc.h>

#ifndef _GNU_SOURCE
	#define _GNU_SOURCE
	/* splice */
#endif
#include <fcntl.h> /* splice() */
#include <sys/uio.h>

#include <cbuf.h>

#define ZCIO_SPLICE_FLAGS ( SPLICE_F_NONBLOCK | SPLICE_F_GIFT | SPLICE_F_MOVE )

/*
	ZCIO
*/
struct zcio_store {
	/* backing store variables */
	int		fd; /* if 0, then "backing store" is malloc() */
	uint32_t	pad_32;
	struct iovec	iov;
	struct cbuf	*cb;
	size_t		block_sz; /* size of each block */
}__attribute__ ((packed));

/* Inhabits a cbuf block, must be power of 2 so DON'T BLOAT over 16B */
struct zcio_block {
	loff_t		blk_offset;
	size_t		data_len;
}__attribute__ ((packed));

enum zcio_store_type
{
	MALLOC,
	MMAP
};

struct zcio_store		*zcio_new(size_t block_sz, uint32_t block_cnt,
						enum zcio_store_type zctype,
						const char *map_dir);

void				zcio_free(struct zcio_store *zs);

size_t				zcio_in_splice(struct zcio_store *zs,
						struct cbuf_blk_ref dest,
						int fd_pipe_from, size_t size);

size_t				zcio_out_splice_sub(struct zcio_store *zs,
						struct cbuf_blk_ref source,
						int fd_pipe_to,
						loff_t sub_offt, size_t sub_len);
NLC_INLINE size_t		zcio_out_splice(struct zcio_store*zs,
						struct cbuf_blk_ref source,
						int fd_pipe_to)
{
	return zcio_out_splice_sub(zs, source, fd_pipe_to, 0, 0);
}


NLC_INLINE struct zcio_block	*zcio_blk_get(struct zcio_store *zs, struct cbuf_blk_ref cbr)
{
	return cbuf_offt(zs->cb, cbr);
}
NLC_INLINE void		*zcio_offt(struct zcio_store *zs, struct zcio_block *zb)
{
	return zs->iov.iov_base + zb->blk_offset;
}

#endif /* zcio_h_ */
