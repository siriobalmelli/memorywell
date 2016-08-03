#ifndef cbuf_h_
#define cbuf_h_

/* cbuf - thread-safe circular buffer 


INTRODUCTION

	This circular buffer library is built to cope with:
		- arbitrary object sizes
		- multiple senders, multiple receivers

	The philosophy is that a cbuf has a send side and a receive side.
	One or more threads on each 'side' can reserve one or more 'blocks'. 
	Reserved blocks are exclusively held until they are released.
	Once released, they available for the 'other side' to reserve and utilize.

	The only caveat is the case where multiple threads have blocks
		reserved on one side (either senders or receivers).
	In this case, blocks released by one thread become 'uncommitted'
		until ALL threads have released their reserved blocks, 
		at which point the uncommitted blocks become 
		available for reservation by the opposite side.
	This is because there is no way to force that blocks will be released
		in the same order they were reserved.
	The only exception to this is the "release_scary(n)" call, which
		simply releases the first n reserved blocks.
	See comments at cbuf_release_scary__().
		

NOMENCLATURE

	The "buffer" is a block of memory which can be reserved
		and access in a circular fashion by callers.
	This is the variable 'buf'.

	A "block" refers to a contiguous chunk of bytes which can be
		"reserved" or "released" in a circular buffer.
	The buffer evenly divides into blocks.
	All blocks have the same size, which must be a power of 2, and
		is expressed as 'sz_bitshift_'.
		NOTE that 'sz_bitshift_' is a SHIFT COUNT and NOT a size.
	A block is identified using the variables 'pos' and 'n',
		which mean "start of a reserved sequence of blocks"
		and "nth block in the reserved sequence".

	'cbuf' is the 64B structure which contains the variables used to
		track reservation and releasing of blocks.
	This is done in a lock-free fashion, which is performant even
		under heavy contention (many threads reserving or releasing).
	This is usually the variable 'cb'.

	The approach is that 'cbuf' doesn't care what's in 'buffer',
		simply what it's size is and wheather it's in any one of
		the following states:

		- unused (ready for writing).
		- reserved by sender for writing.
		- ready (ready for reading).
		- reserved by receiver fir reading.
	
	NOTE: when dealing with splice() and similar:
		- 'data_len' is the size_t taking up the TRAILING sizeof(size_t)
			bytes of a block, OR a dedicated variable in `cbufp_t`.
		- 'data_len' gives how many USABLE DATA BYTES in that block.


CBUF

	The principles behind lock-free buffer reservation are:
	
	a.) A "state-count" (e.g.: how many bytes are "available")
		can be atomically decremented with the number of requested bytes.
	If the result of this operation is positive (>0), then we have 
		a de-facto "reservation".

	b.) There is no sequential constraint between obtaining a "reservation"
		and atomically increating a "position" variable, which
		tells us WHERE that reservation begins.
	
	c.) If sizes are properly aligned and position variables are
		unsigned integers, the "circular" aspect of the buffer
		(when reaching the end, going back to the beginning)
		can be implemented with masking and hardware overflow 
		of position variables.

	The "state-counts" and "position" variables are: 
		(in this illustration, 'pos' variables move DOWN)

				- rcv_reserved | [rcv_uncommitted]
			<< rcv_pos >>
				- ready (available for receiver)
				- snd_reserved | [snd_uncommitted]
			<< snd_pos >>
				- unused (available for sender)

	At creation time, all blocks are in the "unused" state.


CBUF_P flag == CBUF of Pointers

	A cbuf can be created which does not itself contain data, 
		but only small (128B) tracking structures (cbufp_t).
	These tracking structures in turn point to a mmmap()ed temporary file
		which contains ONLY DATA.

	This is useful when performing zero-copy I/O (the "splice" family of calls),
		as data which must be updated (e.g.: 'data_len') does not
		reside alongside data being splice()d.
	This is also useful as it removes the cbuf limitation of 2^32B size,
		allowing very large files buffers.

	WARNING:
	CBUF_P is ONLY intended for use with the "splice" family of calls. 
	Directly writing into any of the blocks erases accounting data
		which points to the backing store.


CBUF_MALLOC flag

	The "buffer" in a cbuf is usually mmap()ed, so as to allow
		zero-copy I/O ("splice" calls) to be performed into and out of it.

	However, it is possible to have "buffer" allocated using malloc()
		instead.

	This can be advantageous in some circumstances, e.g.: because the
		kernel will use transparent hugepages for malloc()ed regions,
		where mmap()ed areas must have a 4k page size.

	Look at the internals for the "splice" family of calls for more details.


ILLUSTRATION
	
	Here is an example of the various cbuf values in action.
	See NOMENCLATURE above for definition of the parameter names.

	'obj_sz' == 32 (any power of 2 will do), which means that
		'sz_bitshift_' == 5.
	'n' is a number of reserved blocks. Here, we have reserved 3 blocks.
	'buf' is a random base addess chosen

reserve(n) -> pos
reserve(3) -> 128

		pos=128 (beginning of reservation)
		|
		|	buf + pos + (n >> sz_bitshift_)
		|	|
		|	|	0xfa00 + 128 + (2 << 5) = 0xfac0
		|	|	|
*buf=0xfa00	|	|	|
|		|	|	|
|		128	160	192	<- [offsets into buf aka 'pos']
|		|	|	|
-------------------------------------------------------------------------
| (memory) ...	|	|	|	...	... (rest of buf)	|
-------------------------------------------------------------------------
		|	|	|
		n=0	n=1	n=2
		|	|	|
		|	|	128 + (2 << 5) = 192
		|	|
		|	128 + (1 << 5) = 160
		|
		128 + (0 << 5) = 128


regarding `pos` overflow:

let's say you have an 8-object cbuf, each block is 32B long.
(Sample masking for overflow condition, i.e. starting at the beginning of
 the circular buffer in an overflow condition.)

buf_sz() == 8 * 32 = 256B
sz_overflow_ == 255

pos & sz_overflow_
256 & 255 = 0

256 + 32 = 288
288 & 255 = 32
*/

#ifndef _GNU_SOURCE
	#define _GNU_SOURCE
	/* mkostemp, splice */
#endif
#include <fcntl.h> /* splice() */
#include <stdint.h> /* [u]int[blah] */
#include <stdlib.h> /* mkostemp */
#include <sys/mman.h> /* mmap */
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "zed_dbg.h"
//#include "zcio.h"

/*	TODO: changes to naming to implement:

	ZCIO: (Zero-Copy I/O library)
		in | out
		rsv | rls
*/


/*
	DEFINES	
*/
#define CBUF_YIELD() usleep(1000)	/* Effectively a pthread_yield(),
					without including threading libraries.

*/

#if 0 //we want to remove these macros as cbuf will no longer or
//have an option to do malloc or splice

define CBUF_P		0x01	/* This cbuf contains pointers to the data,
					not the data itself.
				The data is resident in a user-specified (at create-time)
					"backing store" file, which is mmap()ed.
					*/

#define CBUF_MALLOC	0x02	/* This buffer was allocated with malloc() rather than
					mmap().
				It may (likely) be on a "transparent hugepage" segment, 
					courtesy of the kernel.
				Hugepages are NOT supported for mmap()ed files.
					*/

#endif

#define CBUF_CHK_CLOSING	0x8000	/* cbuf closing. stop checkpointing.
					This is the high bit in `chk_cnt` below.
						*/


/*
	CBUF	
*/
typedef struct {
	void		*buf;		/* Contiguous block of memory over which
						the circular buffer iterates.
						*/
	uint8_t		sz_bitshift_;	/* Used to quickly multiply or divide by obj_sz.
					... or derive obj_sz by bitshifting `1`.
					NOTE that all sizes and positions in this 
						struct are in BYTES, not 'blocks' or 'cells'.
					This is OK as a uint8_t: it will never be
						more than 32.
						*/
	uint8_t		cbuf_flags;
	uint16_t	chk_cnt;	/* how many checkpoints in progress?
					Cannot clean up until all checkpoint
						loops are finished.
						*/
	uint32_t	overflow_;	/* Used for quick masking of `pos` variables. 
					It's also `buf_sz -1`, and is used 
						in lieu of a dedicated `buf_sz` 
						variable to keep struct size <=64B.
					       */

	/* byte offsets into buffer - MUST be masked: use only with cbuf_offt() */
	uint32_t	snd_pos; /* ... unsigned so they can roll over harmlessly. */
	uint32_t	rcv_pos;

	/* some sizes are SIGNED: atomic subtractions may push them negative */
	int64_t		sz_unused;	/* Can be written. */
	int64_t		sz_ready;	/* Ready for reading. */

	uint32_t	snd_reserved;	/* Reserved by writer(s). */
	uint32_t	snd_uncommit;	/* Not committed because other writers I/P. */
	uint32_t	rcv_reserved;	/* Reserved by readers(s). */
	uint32_t	rcv_uncommit;	/* Not committed because other readers I/P. */

	/* TODO: this goes away, and becomes an unusd uint64_t >:) */
	uint64_t	unused;
}__attribute__ ((packed))	cbuf_t;




/*
	CHECKPOINTS
*/
typedef struct {
	int64_t		diff;
	int64_t		actual_rcv;
} cbuf_chk_t;


/* A convenient holder for pertinent reservation data,
	defined here for convenience of library callers.
NOT actually used in the library code anywhere.
	*/
typedef struct {
	uint32_t	pos;
	uint32_t	i;	/* loop iterator (would have been padding anyways) */
	size_t		size;	/* how many blocks were reserved */
}__attribute__ ((packed))	cbuf_res_t;


/* compute some basic values out of a cbuf struct */
Z_INL_FORCE uint32_t cbuf_sz_buf(cbuf_t *b) { return b->overflow_ + 1; }
Z_INL_FORCE uint32_t cbuf_sz_obj(cbuf_t *b) { return 1 << b->sz_bitshift_; }
Z_INL_FORCE uint32_t cbuf_obj_cnt(cbuf_t *b) { return cbuf_sz_buf(b) >> b->sz_bitshift_; }

/* create/free (with backward-compatible functions) */
cbuf_t *cbuf_create1(uint32_t obj_sz, uint32_t obj_cnt, char *map_dir);
Z_INL_FORCE cbuf_t *cbuf_create(uint32_t obj_sz, uint32_t obj_cnt) 
	{ return cbuf_create1(obj_sz, obj_cnt, NULL); }

cbuf_t *cbuf_create_malloc(uint32_t obj_sz, uint32_t obj_cnt);
int	cbuf_zero(cbuf_t *buf);
void	cbuf_free(cbuf_t *buf);

/* reserve */
/*  TODO: keep cbuf_cnd_res_m_cap() .. just call it "snd_res" */
uint32_t cbuf_snd_res(cbuf_t *buf, size_t cnt); /* rename */
uint32_t cbuf_snd_res_cap(cbuf_t *buf, size_t *res_cnt);  /* rename */
uint32_t cbuf_rcv_res(cbuf_t *buf, size_t cnt); /* rename */
uint32_t cbuf_rcv_res_cap(cbuf_t *buf, size_t *res_cnt); /* rename */

/* release */
void cbuf_snd_rls(cbuf_t *buf, size_t cnt); /* rename */
void cbuf_rcv_rls(cbuf_t *buf, size_t cnt); /* rename */

/* sophisticated buffer tricks */
void		cbuf_snd_rls_mscary(cbuf_t *buf, size_t cnt);
void		cbuf_rcv_rls_mscary(cbuf_t *buf, size_t cnt);
uint32_t	cbuf_rcv_held(cbuf_t *buf, size_t *out_cnt);
uint32_t	cbuf_actual_snd(cbuf_t *buf);
uint32_t	cbuf_actual_rcv(cbuf_t *buf);

/* checkpoint */
cbuf_chk_t	*cbuf_checkpoint_snapshot(cbuf_t *b);
int		cbuf_checkpoint_verif(cbuf_t *buf, cbuf_chk_t *checkpoint);
int		cbuf_checkpoint_loop(cbuf_t *buf);


/*	cbuf_offt()
Deliver the memory address at the beginning of the nth in a 
	contiguous set of buffer blocks which starts at 'pos'.
The contiguous set of buffer blocks may exist partly at the end of the
	buffer memory block, and the rest of the way starting at the beginning.
This function exists to hide the masking necessary to roll over from the end to
	the beginning of the buffer.
	*/
Z_INL_FORCE void *cbuf_offt(cbuf_t *buf, uint32_t start_pos, uint32_t n)
{
	start_pos += n << buf->sz_bitshift_; /* purrformance */
	return buf->buf + (start_pos & buf->overflow_);
}

#endif /* cbuf_h_ */
