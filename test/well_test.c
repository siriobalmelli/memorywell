/*	well_test.c

Test single- and multi-threaded use of memorywell.

Used to test that data in and out of buffer is correct.

Not good for benchmarking speed: uses a fixed number of runs
	(which may run terribly slowly depending on settings)
	to verify data correctness.
*/

#include <well.h>
#include <well_fail.h>

#include <nmath.h>
#include <ndebug.h>
#include <stdlib.h>
#include <pthread.h>
#include <getopt.h>
#include <nonlibc.h> /* timing */


static size_t numiter = 1000000;
static size_t blk_cnt = 256; /* how many blocks in the cbuf */
const static size_t blk_size = sizeof(size_t); /* in Bytes */

static size_t tx_thread_cnt = 1;
static pthread_t *tx = NULL;
static size_t rx_thread_cnt = 1;
static pthread_t *rx = NULL;

static size_t reservation = 1; /* how many blocks to reserve at once */

static size_t waits = 0; /* how many times did threads wait? */


/*	tx_thread()
*/
void *tx_single(void* arg)
{
	struct well *buf = arg;
	size_t tally = 0;
	size_t num = numiter;
	struct well_res res = { 0 };

	/* loop on TX */
	for (size_t i=0; i < num; i += res.cnt) {
		size_t ask = i + reservation < num ? reservation : num - i;

		while (!(res = well_reserve(&buf->tx, ask)).cnt)
			FAIL_DO();

		for (size_t j=0; j < res.cnt; j++)
			tally += WELL_DEREF(size_t, res.pos, j, buf) = i + j;
		well_release_single(&buf->rx, res.cnt);
	}

	__atomic_fetch_add(&waits, wait_count, __ATOMIC_RELAXED);
	return (void *)tally;
}
void *tx_multi(void* arg)
{
	struct well *buf = arg;
	size_t tally = 0;
	size_t num = numiter / tx_thread_cnt;
	struct well_res res = { 0 };

	/* loop on TX */
	for (size_t i=0; i < num; i += res.cnt) {
		size_t ask = i + reservation < num ? reservation : num - i;

		while (!(res = well_reserve(&buf->tx, ask)).cnt)
			FAIL_DO();

		for (size_t j=0; j < res.cnt; j++)
			tally += WELL_DEREF(size_t, res.pos, j, buf) = i + j;

		while (!well_release_multi(&buf->rx, res))
			FAIL_DO();
	}

	__atomic_fetch_add(&waits, wait_count, __ATOMIC_RELAXED);
	return (void *)tally;
}


/*	rx_thread()
*/
void *rx_single(void* arg)
{
	struct well *buf = arg;
	size_t tally = 0;
	size_t num = numiter;
	struct well_res res = { 0 };

	/* loop on RX */
	for (size_t i=0; i < num; i += res.cnt) {
		size_t ask = i + reservation < num ? reservation : num - i;

		while (!(res = well_reserve(&buf->rx, ask)).cnt)
			FAIL_DO();

		for (size_t j=0; j < res.cnt; j++) {
			size_t temp = WELL_DEREF(size_t, res.pos, j, buf);
			tally += temp;
		}
		well_release_single(&buf->tx, res.cnt);
	}

	__atomic_fetch_add(&waits, wait_count, __ATOMIC_RELAXED);
	return (void *)tally;
}
void *rx_multi(void* arg)
{
	struct well *buf = arg;
	size_t tally = 0;
	size_t num = numiter / rx_thread_cnt;
	struct well_res res = { 0 };

	/* loop on RX */
	for (size_t i=0; i < num; i += res.cnt) {
		size_t ask = i + reservation < num ? reservation : num - i;

		while (!(res = well_reserve(&buf->rx, ask)).cnt)
			FAIL_DO();

		for (size_t j=0; j < res.cnt; j++) {
			size_t temp = WELL_DEREF(size_t, res.pos, j, buf);
			tally += temp;
		}

		while (!well_release_multi(&buf->tx, res))
			FAIL_DO();
	}

	__atomic_fetch_add(&waits, wait_count, __ATOMIC_RELAXED);
	return (void *)tally;
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
-n, --numiter <iter>	:	Push <iter> blocks through the buffer.\n\
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
		{ "numiter",	required_argument,	0,	'n'},
		{ "count",	required_argument,	0,	'c'},
		{ "reservation",required_argument,	0,	'r'},
		{ "tx-threads",	required_argument,	0,	't'},
		{ "rx-threads",	required_argument,	0,	'x'},
		{ "help",	no_argument,		0,	'h'}
	};

	while ((opt = getopt_long(argc, argv, "n:c:r:t:x:h", long_options, NULL)) != -1) {
		switch(opt)
		{
			case 'n':
				opt = sscanf(optarg, "%zu", &numiter);
				NB_die_if(opt != 1, "invalid numiter '%s'", optarg);
				break;

			case 'c':
				opt = sscanf(optarg, "%zu", &blk_cnt);
				NB_die_if(opt != 1, "invalid blk_cnt '%s'", optarg);
				NB_die_if(blk_cnt < 2,
					"blk_cnt %zu impossible", blk_cnt);
				break;

			case 'r':
				opt = sscanf(optarg, "%zu", &reservation);
				NB_die_if(opt != 1, "invalid reservation '%s'", optarg);
				NB_die_if(!reservation || reservation > blk_cnt,
					"reservation %zu; blk_cnt %zu", reservation, blk_cnt);
				break;

			case 't':
				opt = sscanf(optarg, "%zu", &tx_thread_cnt);
				NB_die_if(opt != 1, "invalid tx_thread_cnt '%s'", optarg);
				break;

			case 'x':
				opt = sscanf(optarg, "%zu", &rx_thread_cnt);
				NB_die_if(opt != 1, "invalid rx_thread_cnt '%s'", optarg);
				break;

			case 'h':
				usage(argv[0]);
				goto die;

			default:
				usage(argv[0]);
				NB_die("option '%c' invalid", opt);
		}
	}
	/* sanity check thread counts */
	NB_die_if(numiter != nm_next_mult64(numiter, tx_thread_cnt),
		"numiter %zu doesn't evenly divide into %zu tx threads",
		numiter, tx_thread_cnt);
	NB_die_if(numiter != nm_next_mult64(numiter, rx_thread_cnt),
		"numiter %zu doesn't evenly divide into %zu rx threads",
		numiter, rx_thread_cnt);
	/* sanity check reservation sizes */
	size_t num = numiter / tx_thread_cnt;
	NB_die_if(num != nm_next_mult64(num, reservation),
		"TX: num %zu doesn't evenly divide into %zu reservation blocks",
		num, reservation);
	num = numiter / rx_thread_cnt;
	NB_die_if(num != nm_next_mult64(num, reservation),
		"RX: num %zu doesn't evenly divide into %zu reservation blocks",
		num, reservation);
	/* ... for finite values tho */
	NB_die_if(numiter > 1000000000, "one billion is plenty thanks");

	/* do MANY less iterations if running under Valgrind! */
	const static size_t valgrind_max = 100000;
	if (getenv("VALGRIND") && numiter >valgrind_max)
		numiter = valgrind_max;


	/* create buffer */
	struct well buf = { {0} };
	NB_die_if(
		well_params(blk_size, blk_cnt, &buf)
		, "");
	NB_die_if(
		well_init(&buf, malloc(well_size(&buf)))
		, "size %zu", well_size(&buf));

	void *(*tx_t)(void *) = tx_single;
	if (tx_thread_cnt > 1)
		tx_t = tx_multi;
	NB_die_if(!(
		tx = malloc(sizeof(pthread_t) * tx_thread_cnt)
		), "");

	void *(*rx_t)(void *) = rx_single;
	if (rx_thread_cnt > 1)
		rx_t = rx_multi;
	NB_die_if(!(
		rx = malloc(sizeof(pthread_t) * rx_thread_cnt)
		), "");

	nlc_timing_start(t);
		/* fire reader-writer threads */
		for (size_t i=0; i < tx_thread_cnt; i++)
			pthread_create(&tx[i], NULL, tx_t, &buf);
		for (size_t i=0; i < rx_thread_cnt; i++)
			pthread_create(&rx[i], NULL, rx_t, &buf);

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

	/* verify sums */
	NB_die_if(tx_i_sum != rx_i_sum, "%zu != %zu", tx_i_sum, rx_i_sum);
	/* This is the expected sum of all 'i' loop iterators for all threads
		The logic to arrive at this was:
			@1 : i = 0		0/1 = 0
			@2 : i = 0+1		2/1 = 0.5
			@3 : i = 0+1+2		3/3 = 1
			@4 : i = 0+1+2+3	6/4 = 1.5
			@5 : i = 0+1+2+3+4	10/5 = 2
			...
			@8 : i = 0+1+2+3+4+5+6+7	28/8	= 3.5
			@8 : (8-1)*0.5				= 3.5
			@8 : 0+1+2+3+4+5+6+7 = (8-1)*0.5*8 = 28
	*/
	numiter /= tx_thread_cnt;
	size_t verif_i_sum = (numiter -1) * 0.5 * numiter * tx_thread_cnt;
	NB_die_if(verif_i_sum != tx_i_sum, "%zu != %zu", verif_i_sum, tx_i_sum);

	/* print stats */
	printf("numiter %zu; blk_size %zu; blk_count %zu; reservation %zu\n",
		numiter, blk_size, blk_cnt, reservation);
	printf("TX threads %zu; RX threads %zu\n",
		tx_thread_cnt, rx_thread_cnt);
	printf("waits: %zu\n", waits);
	printf("cpu time %.4lfs; wall time %.4lfs\n",
		nlc_timing_cpu(t), nlc_timing_wall(t));

die:
	well_deinit(&buf);
	free(well_mem(&buf));
	free(tx);
	free(rx);
	return err_cnt;
}
