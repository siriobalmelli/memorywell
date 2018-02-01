/*	well_bench.c

Used to test speed of memorywell buffers with different 
	threading and size configurations.

Runs for a fixed time and then reports number of blocks pushed/pulled
	from the buffer.
*/

#include <well.h>
#include <well_fail.h>

#include <zed_dbg.h>
#include <stdlib.h>
#include <pthread.h>
#include <getopt.h>
#include <nonlibc.h> /* timing */

#include <unistd.h> /* sleep */

static size_t blk_cnt = 256; /* how many blocks in the cbuf */
const static size_t blk_size = sizeof(size_t); /* in Bytes */
static unsigned int secs = 1; /* how long to run test */

static size_t tx_thread_cnt = 1;
static pthread_t *tx = NULL;
static size_t rx_thread_cnt = 1;
static pthread_t *rx = NULL;

static size_t reservation = 1; /* how many blocks to reserve at once */

static size_t waits = 0; /* how many times did threads wait? */

static uint_fast8_t kill_flag = 0;


/*	escape

Tell compiler and optimized to keep their hands off 'unused'.
*/
static void escape(size_t unused)
{
	asm volatile(	""			/* asm */
			:			/* outputs */
			: "r" (unused)		/* inputs */
			: "memory"		/* clobbers */
	);
}


/*	io_single()
Single-threaded I/O on one side of a buffer
	(will NOT contend for this side of buffer,
	whether multiple threads are contending on the other side).
*/
static inline size_t io_single(	struct well *buf,
				struct well_sym *get,
				struct well_sym *put)
{
	size_t i = 0, pos = 0, res = 0;

	/* loop get -> put
	Check kill flag after every failure to avoid spinning forever.
	*/
	while (! __atomic_load_n(&kill_flag, __ATOMIC_RELAXED)) {
		if ((res = well_reserve(get, &pos, reservation))) {
			for (size_t j=0; j < res; j++)
				escape(WELL_DEREF(size_t, pos, j, buf) = i + j);
			well_release_single(put, res);
			i += res;
		} else {
			FAIL_DO();
		}
	}

	__atomic_fetch_add(&waits, wait_count, __ATOMIC_RELAXED);
	return i;
}

/*	io_multi()
Multi-threaded I/O on one side of a buffer (will contend for this side of buffer).
*/
static inline size_t io_multi(	struct well *buf,
				struct well_sym *get,
				struct well_sym *put)
{
	size_t i = 0, pos = 0, res = 0;

	/* loop get -> put

	Funky logic owing to the fact we need to check 'kill_flag'
		after each failure else risk spinning forever.
	*/
	while (! __atomic_load_n(&kill_flag, __ATOMIC_RELAXED)) {
		if (res) {
			if (well_release_multi(put, res, pos)) {
				i += res;
				res = 0;
			}
		} else if ((res = well_reserve(get, &pos, reservation))) {
			for (size_t j=0; j < res; j++)
				escape(WELL_DEREF(size_t, pos, j, buf) = i + j);
			continue;
		}
		FAIL_DO();
	}

	__atomic_fetch_add(&waits, wait_count, __ATOMIC_RELAXED);
	return i;
}


/*
	tx side
*/
void *tx_single(void* arg)
{
	struct well *buf = arg;
	return (void *)io_single(buf, &buf->tx, &buf->rx);
}
void *tx_multi(void* arg)
{
	struct well *buf = arg;
	return (void *)io_multi(buf, &buf->tx, &buf->rx);
}
/*
	rx side
*/
void *rx_single(void* arg)
{
	struct well *buf = arg;
	return (void *)io_single(buf, &buf->rx, &buf->tx);
}
void *rx_multi(void* arg)
{
	struct well *buf = arg;
	return (void *)io_multi(buf, &buf->rx, &buf->tx);
}


/*	usage()
*/
void usage(const char *pgm_name)
{
	fprintf(stderr, "Usage: %s [OPTIONS]\n\
Test MemoryWell correctness/performance.\n\
\n\
Notes:\n\
- block size is fixed at sizeof(size_t)\n\
\n\
Options:\n\
-s, --secs <seconds>	:	How long to run test.\n\
-c, --count <blk_count>	:	How many blocks in the circular buffer.\n\
-r, --reservation <res>	:	(Attempt to) reserve <res> blocks at once.\n\
-t, --tx-threads	:	Number of TX threads.\n\
-x, --rx-threads	:	Number of RX threads.\n\
-h, --help		:	Print this message and exit.\n",
		pgm_name);
}


/*	main()
*/
int main(int argc, char **argv)
{
	/* Use global error count so that threads can error
		and directly affect the return code of main()
	int err_cnt = 0;
	*/


	/*
		options
	*/
	int opt = 0;
	static struct option long_options[] = {
		{ "secs",	required_argument,	0,	's'},
		{ "count",	required_argument,	0,	'c'},
		{ "reservation",required_argument,	0,	'r'},
		{ "tx-threads",	required_argument,	0,	't'},
		{ "rx-threads",	required_argument,	0,	'x'},
		{ "help",	no_argument,		0,	'h'}
	};

	while ((opt = getopt_long(argc, argv, "s:c:r:t:x:h", long_options, NULL)) != -1) {
		switch(opt)
		{
			case 's':
				opt = sscanf(optarg, "%u", &secs);
				Z_die_if(opt != 1, "invalid secs '%s'", optarg);
				break;

			case 'c':
				opt = sscanf(optarg, "%zu", &blk_cnt);
				Z_die_if(opt != 1, "invalid blk_cnt '%s'", optarg);
				Z_die_if(blk_cnt < 2,
					"blk_cnt %zu impossible", blk_cnt);
				break;

			case 'r':
				opt = sscanf(optarg, "%zu", &reservation);
				Z_die_if(opt != 1, "invalid reservation '%s'", optarg);
				Z_die_if(!reservation || reservation > blk_cnt,
					"reservation %zu; blk_cnt %zu", reservation, blk_cnt);
				break;

			case 't':
				opt = sscanf(optarg, "%zu", &tx_thread_cnt);
				Z_die_if(opt != 1, "invalid tx_thread_cnt '%s'", optarg);
				break;

			case 'x':
				opt = sscanf(optarg, "%zu", &rx_thread_cnt);
				Z_die_if(opt != 1, "invalid rx_thread_cnt '%s'", optarg);
				break;

			case 'h':
				usage(argv[0]);
				goto out;

			default:
				usage(argv[0]);
				Z_die("option '%c' invalid", opt);
		}
	}
	/* sanity check reservation sizes */
	Z_die_if(reservation > blk_cnt,
		"would attempt to reserve %zu from buffer with %zu blocks",
		reservation, blk_cnt);


	/* create buffer */
	struct well buf = { {0} };
	Z_die_if(
		well_params(blk_size, blk_cnt, &buf)
		, "");
	Z_die_if(
		well_init(&buf, malloc(well_size(&buf)))
		, "size %zu", well_size(&buf));

	void *(*tx_t)(void *) = tx_single;
	if (tx_thread_cnt > 1)
		tx_t = tx_multi;
	Z_die_if(!(
		tx = malloc(sizeof(pthread_t) * tx_thread_cnt)
		), "");

	void *(*rx_t)(void *) = rx_single;
	if (rx_thread_cnt > 1)
		rx_t = rx_multi;
	Z_die_if(!(
		rx = malloc(sizeof(pthread_t) * rx_thread_cnt)
		), "");

	nlc_timing_start(t);
		/* fire reader-writer threads */
		for (size_t i=0; i < tx_thread_cnt; i++)
			pthread_create(&tx[i], NULL, tx_t, &buf);
		for (size_t i=0; i < rx_thread_cnt; i++)
			pthread_create(&rx[i], NULL, rx_t, &buf);

		/* set kill flag after time elapsed */
		while ((secs = sleep(secs)))
			;
		__atomic_store_n(&kill_flag, 1, __ATOMIC_RELAXED);

		/* wait for threads to finish */
		size_t tx_i_sum = 0, rx_i_sum = 0;
		for (size_t i=0; i < tx_thread_cnt; i++) {
			void *tmp;
			pthread_join(tx[i], &tmp);
			tx_i_sum += (size_t)tmp;
		}
		for (size_t i=0; i < rx_thread_cnt; i++) {
			void *tmp;
			pthread_join(rx[i], &tmp);
			rx_i_sum += (size_t)tmp;
		}
	nlc_timing_stop(t);

	/* print stats */
	printf("secs %u; blk_size %zu; blk_count %zu; reservation %zu\n",
		secs, blk_size, blk_cnt, reservation);
	printf("TX threads %zu; RX threads %zu\n",
		tx_thread_cnt, rx_thread_cnt);
	printf("tx blocks %zu; rx blocks %zu; waits %zu\n",
		tx_i_sum, rx_i_sum, waits);
	printf("cpu time %.4lfs; wall time %.4lfs\n",
		nlc_timing_cpu(t), nlc_timing_wall(t));

out:
	well_deinit(&buf);
	free(well_mem(&buf));
	free(tx);
	free(rx);
	return err_cnt;
}
