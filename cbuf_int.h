#ifndef cbuf_int_h_
#define cbuf_int_h_

#include "cbuf.h"

/* internals */
uint32_t next_pow2(uint32_t x);
uint32_t next_multiple(uint32_t x, uint32_t mult);

cbuf_t *cbuf_create_(	uint32_t obj_sz, 
			uint32_t obj_cnt, 
			uint8_t flags);

void	cbuf_free_(cbuf_t *buf);

uint32_t cbuf_reserve__(cbuf_t		*buf,
			size_t		blk_sz,
			int64_t		*sz_source, 
			uint32_t	*reserved,
			uint32_t	*pos);

void cbuf_release__(cbuf_t		*buf,
			size_t		blk_sz,
			uint32_t	*reserved,
			uint32_t	*uncommit,
			int64_t		*sz_dest);

void cbuf_release_scary__(cbuf_t	*buf,
			size_t		blk_sz,
			uint32_t	*reserved,
			uint32_t	*uncommit,
			int64_t		*sz_dest);

uint64_t cbuf_actual_receiver__(cbuf_t *buf,
			uint64_t	*snd_pos,
			uint64_t	*sz_unused);
//void	cbuf_actuals__(cbuf_t *buf, int64_t *act_snd, int64_t *act_rcv);
void	cbuf_actuals__(cbuf_t *buf, uint32_t *act_snd, uint32_t *act_rcv);

#endif /* cbuf_int_h_ */
