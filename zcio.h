#ifndef zcio_h_
#define zcio_h_

#ifndef _GNU_SOURCE
	#define _GNU_SOURCE
	/* splice */
#endif
#include <fcntl.h> /* splice() */
#include <sys/uio.h>

#include "sbfu.h" /* For backing store operations: cbufp_ only
			TODO: put ONLY in ZCIO.
			*/

#include "cbuf.h"

/*
	ZCIO
*/
struct zcio_store {
	/* backing store variables */
	int		fd; /* if 0, then "backing store" is malloc() */
	uint32_t	pad_32;
	struct iovec	iov;
	cbuf_t		*buf;
	size_t		block_sz; /* size of each block */
}__attribute__ ((packed));

/* Inhabits a cbuf block, must be power of 2 so DON'T BLOAT over 16B */
struct zcio_block {
	loff_t		blk_offset;
	size_t		data_len;
}__attribute__ ((packed));

struct zcio_store	*zcio_new(size_t block_sz, uint32_t block_cnt);
void			zcio_free(struct zcio_store *zs); 

Z_INL_FORCE uint32_t	zcio_in_res(struct zcio_store *zs, uint32_t cnt)
{
	return cbuf_snd_res(zs->buf, cnt);
}

Z_INL_FORCE size_t	zcio_blk_data_len(struct zcio_store *zs, 
					uint32_t pos, int i)
{
	struct zcio_block *zb = cbuf_offt(zs->buf, pos, i);
	return zb->data_len;
}
Z_INL_FORCE void	zcio_blk_set_data_len(struct zcio_store *zs, 
					uint32_t pos, int i, size_t len)
{
	struct zcio_block *zb = cbuf_offt(zs->buf, pos, i);
	zb->data_len = len;
}

size_t			zcio_in_splice(struct zcio_store *zs, 
					struct cbuf_blk_ref dest,
					int fd_pipe_from, size_t size);

size_t			zcio_out_splice_sub(struct zcio_store *zs, 
					struct cbuf_blk_ref source,
					int fd_pipe_to,
					loff_t sub_offt, size_t sub_len);
Z_INL_FORCE size_t	zcio_out_splice(struct zcio_store*zs, 
					struct cbuf_blk_ref source,
					int fd_pipe_to)
{
	return zcio_out_splice_sub(zs, source, fd_pipe_to, 0, 0);
}

size_t	zcio_splice_from_pipe(int fd_pipe_read, cbuf_t *b, uint32_t pos, int i, size_t size);
size_t	zcio_splice_to_pipe_sub(cbuf_t *b, uint32_t pos, int i, int fd_pipe_write, 
				loff_t sub_offt, size_t sub_len);
Z_INL_FORCE size_t zcio_splice_to_pipe(cbuf_t *b, uint32_t pos, int i, int fd_pipe_write)
{
	return zcio_splice_to_pipe_sub(b, pos, i, fd_pipe_write, 0, 0);
}

#endif /* zcio_h_ */
