#ifndef cbuf_int_h_
#define cbuf_int_h_

#include "cbuf.h"

/* internals */
uint32_t next_pow2(uint32_t x);
uint32_t next_multiple(uint32_t x, uint32_t mult);

struct cbuf *cbuf_create_(uint32_t	obj_sz,
			uint32_t	obj_cnt,
			uint8_t		flags);

void	cbuf_free_(struct cbuf *buf);

/*  TODO: Sirio: review masking return of this function ...
No legal value can ever POSSIBLY be other than a multiple of 2,
	and a bad return value of -1 will NEVER be a power of 2 ;)
	*/
uint32_t cbuf_reserve__(struct cbuf		*buf,
			size_t			blk_sz,
			volatile int64_t	*sz_source,
			volatile uint32_t	*reserved,
			volatile uint32_t	*pos);

void cbuf_release__(struct cbuf			*buf,
			size_t			blk_sz,
			volatile uint32_t	*reserved,
			volatile uint32_t	*uncommit,
			volatile int64_t	*sz_dest);

void cbuf_release_scary__(struct cbuf		*buf,
			size_t			blk_sz,
			volatile uint32_t	*reserved,
			volatile uint32_t	*uncommit,
			volatile int64_t	*sz_dest);

uint64_t cbuf_actual_receiver__(struct cbuf	*buf,
			volatile uint64_t	*snd_pos,
			volatile uint64_t	*sz_unused);
void	cbuf_actuals__(struct cbuf *buf, uint32_t *act_snd, uint32_t *act_rcv);

const char *cbuf_flags_prn_(uint8_t cbuf_flags);

#endif /* cbuf_int_h_ */
