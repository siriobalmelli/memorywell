#include <zed_dbg.h>
#include <nbuf.h>
#include <stdlib.h>
#include <pthread.h>
#include <getopt.h>
#include <nonlibc.h> /* timing */


static size_t numiter = 100000000;
static size_t blk_cnt = 256; /* how many blocks in the cbuf */
static size_t blk_size = 8; /* in Bytes */
static size_t reservation = 1; /* how many blocks to reserve at once */


/* TODO:
	- yield to a specific thread (eventing) instead of general yield()?
	- validate reserve and release with '0' size.
*/


/*	tx_fixed()
TX with a fixed reservation size.
*/
void *tx_fixed(void* arg)
{
	struct nbuf *nb = arg;
	size_t tally = 0;

	/* loop on TX */
	for (size_t i=0; i < numiter; i += reservation) {
		size_t pos;
		while ((nbuf_reserve_single(&nb->ct, &nb->tx, &pos, reservation)) != reservation)
			sched_yield(); /* no scheduling decisions taken by nbuf */
		for (size_t j=0; j < reservation; j++)
			tally += NBUF_DEREF(size_t, pos, j, nb) = i + j;
		nbuf_release_single(&nb->ct, &nb->rx, reservation);
	}

	return (void *)tally;
}

/*	rx_fixed()
*/
void *rx_fixed(void* arg)
{
	struct nbuf *nb = arg;
	size_t tally = 0;
	size_t seq_errs = 0; /* enforce that buffer is sequential */

	/* loop on RX */
	for (size_t i=0; i < numiter; i += reservation) {
		size_t pos;
		while ((nbuf_reserve_single(&nb->ct, &nb->rx, &pos, reservation)) != reservation)
			sched_yield(); /* no scheduling decisions taken by nbuf */
		for (size_t j=0; j < reservation; j++) {
			size_t temp = NBUF_DEREF(size_t, pos, j, nb);
			tally += temp;
			/* Buffer should be sequential: it was written to
				in the same order it is being read from.
			*/
			seq_errs += (temp != i + j);
		}
		nbuf_release_single(&nb->ct, &nb->tx, reservation);
	}
	/* error if sequential consistency was violated */
	Z_err_if(seq_errs, "%zu violations of sequential consistency", seq_errs);

	return (void *)tally;
}

/*	rx_variable()
*/
void *rx_variable(void* arg)
{
	struct nbuf *nb = arg;
	size_t tally = 0;
	size_t seq_errs = 0; /* enforce that buffer is sequential */

	/* loop on RX */
	for (size_t i=0, reservation; i < numiter; i += reservation) {
		size_t pos;
		while (!(reservation = nbuf_reserve_single_var(&nb->ct, &nb->rx, &pos)))
			sched_yield(); /* no scheduling decisions taken by nbuf */

		for (size_t j=0; j < reservation; j++) {
			size_t temp = NBUF_DEREF(size_t, pos, j, nb);
			tally += temp;
			seq_errs += (temp != i + j);
		}

		nbuf_release_single(&nb->ct, &nb->tx, reservation);
	}
	/* error if sequential consistency was violated */
	Z_err_if(seq_errs, "%zu violations of sequential consistency", seq_errs);

	return (void *)tally;
}



/*	usage()
*/
void usage(const char *pgm_name)
{
	fprintf(stderr, "Usage: %s [OPTIONS]\n\
Test Single-Producer|Single-Consumer correctness/performance.\n\
\n\
Options:\n\
-n, --numiter <iter>	:	Push <iter> blocks through the buffer.\n\
-c, --count <blk_count>	:	How many blocks in the circular buffer.\n\
-s, --size <blk_size>	:	Size of each block (bytes).\n\
-r, --reservation <res>	:	(Attempt to) reserve <res> blocks at once.\n\
-e, --variable		:	RX (consumer) uses variable reservation size.\n\
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
		{ "size",	required_argument,	0,	's'},
		{ "count",	required_argument,	0,	'c'},
		{ "reservation",required_argument,	0,	'r'},
		{ "variable",	no_argument,		0,	'e'},
		{ "help",	no_argument,		0,	'h'}
	};

	/* fixed- or variable-reservation RX thread */
	void *(*rx_thread)(void *) = rx_fixed;

	while ((opt = getopt_long(argc, argv, "n:s:c:r:eh", long_options, NULL)) != -1) {
		switch(opt)
		{
			case 'n':
				opt = sscanf(optarg, "%zu", &numiter);
				Z_die_if(opt != 1, "invalid numiter '%s'", optarg);
				break;

			case 's':
				opt = sscanf(optarg, "%zu", &blk_size);
				Z_die_if(opt != 1, "invalid blk_size '%s'", optarg);
				Z_die_if(blk_size < 8 || blk_size > UINT32_MAX,
					"blk_size %zu impossible", blk_size);
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

			case 'e':
				rx_thread = rx_variable;
				break;

			case 'h':
				usage(argv[0]);
				goto out;

			default:
				usage(argv[0]);
				Z_die("option '%c' invalid", opt);
		}
	}
	/* sanity check reservation size */
	Z_die_if(numiter != nm_next_mult64(numiter, reservation),
		"numiter %zu doesn't evenly divide into %zu reservation blocks",
		numiter, reservation);


	/* create buffer */
	struct nbuf nb = { {0} };
	Z_die_if(
		nbuf_params(blk_size, blk_cnt, &nb)
		, "");
	Z_die_if(
		nbuf_init(&nb, malloc(nbuf_size(&nb)))
		, "size %zu", nbuf_size(&nb));

	nlc_timing_start(t);
		/* fire reader-writer threads */
		pthread_t tx, rx;
		pthread_create(&tx, NULL, tx_fixed, &nb);
		pthread_create(&rx, NULL, rx_thread, &nb);

		/* wait for threads to finish */
		size_t tx_i_sum = 0, rx_i_sum = 0;
		pthread_join(tx, (void*)&tx_i_sum);
		pthread_join(rx, (void*)&rx_i_sum);
	nlc_timing_stop(t);

	/* verify sums */
	Z_die_if(tx_i_sum != rx_i_sum, "%zu != %zu", tx_i_sum, rx_i_sum);
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
	size_t verif_i_sum = (numiter -1) * 0.5 * numiter;
	Z_die_if(verif_i_sum != tx_i_sum, "%zu != %zu", verif_i_sum, tx_i_sum);

	/* print stats */
	printf("numiter %zu; blk_size %zu; blk_count %zu; reservation %zu\n",
		numiter, blk_size, blk_cnt, reservation);
	if (rx_thread == rx_fixed)
		printf("TX/RX reservation %zu\n", reservation);
	else
		printf("TX reservation %zu, RX variable\n", reservation);
	printf("cpu time: %.4lfs\n", nlc_timing_secs(t));

out:
	nbuf_deinit(&nb);
	free(nb.ct.buf);
	return err_cnt;
}
