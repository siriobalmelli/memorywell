#include <zed_dbg.h>
#include <nbuf.h>
#include <stdlib.h>
#include <pthread.h>


/*	SPSC benchmark/test cases

- for varying block sizes
- for varying block counts in the buffer
- for varying reservation lengths

Change paradigm:
a.) generate random bytes INTO the block
b.) hash the bytes (fnv1a) at source and at destination; verify they agree
*/

const size_t numiter = 100000000;
const size_t blk_cnt_min = 512; /* how many blocks in the cbuf */
const size_t blk_cnt_max = 4096;
const size_t reservation_min = 1; /* how many blocks to reserve at once */
const size_t reservation_max = 8;
const size_t blk_sz_min = 32;
const size_t blk_sz_max = 256;
/*	tx_thread()
*/
void* tx_thread(void* arg)
{
	struct nbuf *nb = arg;
	size_t tally = 0;

	for (int res = reservation_min; res < reservation_max; res <<= 1)
	{
		Z_log(Z_inf, "reservation %zu", res);
		size_t res_sz = nbuf_reservation_size(nb, res);
		Z_log(Z_inf, "res_sz %zu", res_sz);
		Z_die_if(!res_sz, "reservation count %zu is broken", res);

		/* loop on TX */
		for (size_t i=0; i < numiter; i += res) {
			size_t pos;
			while ((pos = nbuf_reserve_single(&nb->ct, &nb->tx, res_sz)) == -1)
				; /* spinlock like a bitch */
			for (size_t j=0; j < res; j++)
				tally += NBUF_DEREF(size_t, pos, j, nb) = i + j;
			Z_die_if(nbuf_release_single(&nb->ct, &nb->rx, res_sz), "");
		}
	}
out:
	return (void *)tally;
}


/*	rx_thread()
*/
void* rx_thread(void* arg)
{
	struct nbuf *nb = arg;
	size_t tally = 0;

	for (int res = reservation_min; res < reservation_max; res <<= 1)
	{
		size_t res_sz = nbuf_reservation_size(nb, res);
		Z_die_if(!res_sz, "reservation count %zu is broken", res);

		/* loop on RX */
		for (size_t i=0; i < numiter; i += res) {
			size_t pos;
			while ((pos = nbuf_reserve_single(&nb->ct, &nb->rx, res_sz)) == -1)
				; /* spinlock like a bitch */
			for (size_t j=0; j < res; j++)
				tally += NBUF_DEREF(size_t, pos, j, nb);
			Z_die_if(nbuf_release_single(&nb->ct, &nb->tx, res_sz), "");
		}
	}
out:
	return (void *)tally;
}


/*	main()
*/
int main()
{
	int err_cnt = 0;

	/* create buffer */
	struct nbuf nb = { {0} };

	for (size_t j = blk_cnt_min; j < blk_cnt_max; j <<= 1)
	{
		for (size_t i = blk_sz_min; i < blk_sz_max; i <<= 1)
		{
			Z_log(Z_inf, "Iteration: blk_sz %zu, blk_cnt %zu", i, j );
			Z_die_if(
				nbuf_params(i, j, &nb)
				, "");
			Z_die_if(
				nbuf_init(&nb, malloc(nbuf_size(&nb)))
				, "size %zu", nbuf_size(&nb));

			/* fire reader-writer threads */
			pthread_t tx, rx;
			pthread_create(&tx, NULL, rx_thread, &nb);
			pthread_create(&rx, NULL, tx_thread, &nb);

			/* wait for threads to finish */
			size_t tx_i_sum = 0, rx_i_sum = 0;
			pthread_join(tx, (void*)&tx_i_sum);
			pthread_join(rx, (void*)&rx_i_sum);

			/* verify sums */
			Z_die_if(tx_i_sum != rx_i_sum, "%zu != %zu", tx_i_sum, rx_i_sum);

			free(nb.ct.buf);
			nb.ct.buf = NULL;

			Z_log(Z_inf, "test %d finished.", j);
		}
	}
out:
	free(nb.ct.buf);
	return err_cnt;
}
