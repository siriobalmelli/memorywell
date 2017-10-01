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


/* TODO: yield to a specific thread (eventing) instead of general yield() ? */

/*	tx_fixed()
TX with a fixed reservation size.
*/
void* tx_fixed(void* arg)
{
	struct nbuf *nb = arg;
	size_t tally = 0;

	size_t res_sz = nbuf_reservation_size(nb, reservation);
	Z_die_if(!res_sz, "reservation count %zu is broken", reservation);

	/* loop on TX */
	for (size_t i=0; i < numiter; i += reservation) {
		size_t pos;
		while ((pos = nbuf_reserve_single(&nb->ct, &nb->tx, res_sz)) == -1)
			sched_yield(); /* no scheduling decisions taken by nbuf */
		for (size_t j=0; j < reservation; j++)
			tally += NBUF_DEREF(size_t, pos, j, nb) = i + j;
		Z_die_if(nbuf_release_single(&nb->ct, &nb->rx, res_sz), "");
	}

out:
	return (void *)tally;
}
/*	rx_fixed()
*/
void* rx_fixed(void* arg)
{
	struct nbuf *nb = arg;
	size_t tally = 0;

	size_t res_sz = nbuf_reservation_size(nb, reservation);
	Z_die_if(!res_sz, "reservation count %zu is broken", reservation);

	/* loop on RX */
	for (size_t i=0; i < numiter; i += reservation) {
		size_t pos;
		while ((pos = nbuf_reserve_single(&nb->ct, &nb->rx, res_sz)) == -1)
			sched_yield(); /* no scheduling decisions taken by nbuf */
		for (size_t j=0; j < reservation; j++)
			tally += NBUF_DEREF(size_t, pos, j, nb);
		Z_die_if(nbuf_release_single(&nb->ct, &nb->tx, res_sz), "");
	}

out:
	return (void *)tally;
}


/*	tx_variable()
TX with a variable/opportunistic reservation size.
*/
void* tx_variable(void* arg)
{
	struct nbuf *nb = arg;
	size_t tally = 0;

	/* loop on TX */
	for (size_t i=0, res_sz; i < numiter; i += res_sz) {
		/* reserve a variable amount of bytes */
		size_t pos;
		while ((pos = nbuf_reserve_single_var(&nb->ct, &nb->tx, &res_sz)) == -1)
			sched_yield(); /* no scheduling decisions taken by nbuf */

		/* turn a bytecount into a block-count; loop through each block */
		size_t reservation = nbuf_blk_div(nb, res_sz);
		for (size_t j=0; j < reservation; j++)
			tally += NBUF_DEREF(size_t, pos, j, nb) = i + j;

		Z_die_if(nbuf_release_single(&nb->ct, &nb->rx, res_sz), "");
	}

out:
	return (void *)tally;
}
/*	rx_variable()
*/
void* rx_variable(void* arg)
{
	struct nbuf *nb = arg;
	size_t tally = 0;

	/* loop on RX */
	for (size_t i=0, res_sz; i < numiter; i += res_sz) {
		size_t pos;
		while ((pos = nbuf_reserve_single_var(&nb->ct, &nb->rx, &res_sz)) == -1)
			sched_yield(); /* no scheduling decisions taken by nbuf */

		size_t reservation = nbuf_blk_div(nb, res_sz);
		for (size_t j=0; j < reservation; j++)
			tally += NBUF_DEREF(size_t, pos, j, nb);

		Z_die_if(nbuf_release_single(&nb->ct, &nb->tx, res_sz), "");
	}

out:
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
				The special value '-1' means \"variable reservation\".\n",
		pgm_name);
}


/*	main()
*/
int main(int argc, char **argv)
{
	int err_cnt = 0;


	/*
		options
	*/
	int opt = 0;
	static struct option long_options[] = {
		{ "numiter",	required_argument,	0,	'n'},
		{ "size",	required_argument,	0,	's'},
		{ "count",	required_argument,	0,	'c'},
		{ "reservation",required_argument,	0,	'r'},
		{ "help",	no_argument,		0,	'h'}
	};

	while ((opt = getopt_long(argc, argv, "n:s:c:r:", long_options, NULL)) != -1) {
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
				/* -1 means "variable reservation" */
				Z_die_if(!reservation || (reservation > blk_cnt && reservation != -1),
					"reservation %zu; blk_cnt %zu", reservation, blk_cnt);
				break;

			case 'h':
				usage(argv[0]);
				goto out;

			default:
				usage(argv[0]);
				Z_die("option '%c' invalid", opt);
		}
	}


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
		if (reservation != -1) {
			pthread_create(&tx, NULL, rx_fixed, &nb);
			pthread_create(&rx, NULL, tx_fixed, &nb);
		} else {
			reservation = 0;
			pthread_create(&tx, NULL, rx_variable, &nb);
			pthread_create(&rx, NULL, tx_variable, &nb);
		}

		/* wait for threads to finish */
		size_t tx_i_sum = 0, rx_i_sum = 0;
		pthread_join(tx, (void*)&tx_i_sum);
		pthread_join(rx, (void*)&rx_i_sum);
	nlc_timing_stop(t);

	/* verify sums */
	Z_die_if(tx_i_sum != rx_i_sum, "%zu != %zu", tx_i_sum, rx_i_sum);
	printf("numiter %zu; blk_size %zu; blk_count %zu; reservation %zu; %.4lfs cputime\n",
		numiter, blk_size, blk_cnt, reservation, nlc_timing_secs(t));

out:
	free(nb.ct.buf);
	return err_cnt;
}
