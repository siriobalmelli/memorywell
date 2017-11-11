#include <well.h>
#include <well_fail.h>

#include <zed_dbg.h>
#include <stdlib.h>
#include <pthread.h>
#include <getopt.h>
#include <nonlibc.h> /* timing */

#include<sys/time.h>
#include<signal.h>

static size_t waits = 0; /* how many times did threads wait? */
static int kill_flag = 0;

/* thread tracking */
typedef struct {
	void *(*func)(void *args);
	pthread_t thread;
} a_thread;
a_thread *threads = NULL;


/*	lone_thread()
Alternately read/write from one buffer
*/
void *lone_thread(void* arg)
{
	struct well *buf = arg;
	size_t tally = 0;
	volatile size_t consume;
	size_t pos;

	/* loop on TX */
	while (!__atomic_load_n(&kill_flag, __ATOMIC_CONSUME)) {
		Z_die_if(!well_reserve(&buf->tx, &pos, 1),
			"lone thread should never block");
		WELL_DEREF(size_t, pos, 0, buf) = tally++;
		well_release_single(&buf->rx, 1);

		Z_die_if(!well_reserve(&buf->rx, &pos, 1),
			"lone thread should never block");
		consume = WELL_DEREF(size_t, pos, 0, buf);
		tally++;
		well_release_single(&buf->tx, 1);
	}

out:
	/* iteration count */
	return (void *)tally;
}

/*	tx_thread()
*/
void *tx_single(void* arg)
{
	struct well *buf = arg;
	size_t tally = 0;
	size_t pos;

	/* loop on TX */
	while (!__atomic_load_n(&kill_flag, __ATOMIC_CONSUME)) {
		if (!well_reserve(&buf->tx, &pos, 1)) {
			FAIL_DO();
			continue;
		}
		WELL_DEREF(size_t, pos, 0, buf) = tally++;
		well_release_single(&buf->rx, 1);
	}

	/* iteration count */
	__atomic_fetch_add(&waits, wait_count, __ATOMIC_RELAXED);
	return (void *)tally;
}
void *tx_multi(void* arg)
{
	struct well *buf = arg;
	size_t tally = 0;
	size_t pos;

	nice(2);

	/* loop on TX */
	while (!__atomic_load_n(&kill_flag, __ATOMIC_CONSUME)) {
		if (!well_reserve(&buf->tx, &pos, 1)) {
			FAIL_DO();
			continue;
		}
		WELL_DEREF(size_t, pos, 0, buf) = tally++;
		while (!well_release_multi(&buf->rx, 1, pos))
			FAIL_DO();
	}

	/* iteration count */
	__atomic_fetch_add(&waits, wait_count, __ATOMIC_RELAXED);
	return (void *)tally;
}


/*	rx_thread()
*/
void *rx_single(void* arg)
{
	struct well *buf = arg;
	size_t tally = 0;
	volatile size_t consume;
	size_t pos;

	while (!__atomic_load_n(&kill_flag, __ATOMIC_CONSUME)) {
		if (!well_reserve(&buf->rx, &pos, 1)) {
			FAIL_DO();
			continue;
		}
		consume = WELL_DEREF(size_t, pos, 0, buf);
		tally++;
		well_release_single(&buf->tx, 1);
	}

	__atomic_fetch_add(&waits, wait_count, __ATOMIC_RELAXED);
	return (void *)tally;
}
void *rx_multi(void* arg)
{
	struct well *buf = arg;
	size_t tally = 0;
	volatile size_t consume;
	size_t pos;

	nice(2);

	while (!__atomic_load_n(&kill_flag, __ATOMIC_CONSUME)) {
		if (!well_reserve(&buf->rx, &pos, 1)) {
			FAIL_DO();
			continue;
		}
		consume = WELL_DEREF(size_t, pos, 0, buf);
		tally++;
		while (!well_release_multi(&buf->tx, 1, pos))
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
Benchmark MemoryWell correctness/performance.\n\
Measures number of I/O iterations through a MemoryWell buffer.\n\
\n\
Notes:\n\
- Block size is fixed at sizeof(size_t)\n\
- Buffer is fixed at block_count of 'threads * 4' blocks\n\
- Reservation size is 1: contention happens on every. single. block\n\
		(absolute worst-case scenario).\n\
	Real-world cases will exploit multi-block reservations.\n\
\n\
Options:\n\
-t, --threads	:	Number of thread PAIRS doing I/O on the buffer.\n\
			The special value '0' indicates a single thread\n\
				alternately write/reading on the same buffer.\n\
-s, --seconds	:	Number of seconds to run benchmark.\n\
-h, --help	:	Print this message and exit.\n",
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
		{ "threads",	required_argument,	0,	't'},
		{ "seconds",	required_argument,	0,	's'},
		{ "help",	no_argument,		0,	'h'}
	};

	size_t pairs = 0, seconds = 5;
	while ((opt = getopt_long(argc, argv, "t:s:h", long_options, NULL)) != -1) {
		switch(opt)
		{
			case 't':
				opt = sscanf(optarg, "%zu", &pairs);
				Z_die_if(opt != 1, "invalid pairs '%s'", optarg);
				break;

			case 's':
				opt = sscanf(optarg, "%zu", &seconds);
				Z_die_if(opt != 1, "invalid seconds '%s'", optarg);
				break;

			case 'h':
				usage(argv[0]);
				goto out;

			default:
				usage(argv[0]);
				Z_die("option '%c' invalid", opt);
		}
	}

	/* how many threads to execute */
	size_t exec_threads = 1;
	if (pairs)
		exec_threads = pairs * 2;
	Z_die_if(!(
		threads = malloc(sizeof(a_thread) * exec_threads)
		), "malloc %zu", sizeof(a_thread) * exec_threads)

	/* create buffer */
	struct well buf = { {0} };
	Z_die_if(
		well_params(sizeof(size_t), exec_threads *2, &buf)
		, "");
	Z_die_if(
		well_init(&buf, malloc(well_size(&buf)))
		, "size %zu", well_size(&buf));

	/* assign thread functions */
	if (exec_threads == 1) {
		threads[0].func = lone_thread;
	} else if (exec_threads == 2) {
		threads[0].func = tx_single;
		threads[1].func = rx_single;
	} else {
		for (size_t i=0; i < pairs; i++) {
			threads[i*2].func = tx_multi;
			threads[i*2+1].func = rx_multi;
		}
	}

	/* run, dos, run */
	nlc_timing_start(t);
		for (size_t t=0; t < exec_threads; t++)
			Z_die_if(
				pthread_create(&threads[t].thread, NULL,
						threads[t].func, &buf)
			, "");

		sleep(seconds);
		__atomic_store_n(&kill_flag, 1, __ATOMIC_RELEASE);
		Z_log(Z_inf, "set kill flag");

		size_t tally =0;
		for (size_t t=0; t < exec_threads; t++) {
			void *temp;
			Z_die_if(
				pthread_join(threads[t].thread, &temp)
				, "");
			tally += (size_t)temp;
		}
	nlc_timing_stop(t);

	/* print stats */
	printf("operations %zu\n", tally);
	printf("thread pairs %zu\n", pairs);
	printf("waits: %zu\n", waits);
	printf("cpu time %.4lfs; wall time %.4lfs\n",
		nlc_timing_cpu(t), nlc_timing_wall(t));

out:
	well_deinit(&buf);
	free(well_mem(&buf));
	free(threads);
	return err_cnt;
}
