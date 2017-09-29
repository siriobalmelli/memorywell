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
const size_t blk_cnt = 256; /* how many blocks in the cbuf */
const size_t reservation = 1; /* how many blocks to reserve at once */

/*	tx_thread()
*/
void* tx_thread(void* arg)
{
	struct nbuf *nb = arg;
	size_t tally = 0;

	size_t res_sz = nbuf_reservation_size(nb, reservation);
	Z_die_if(!res_sz, "reservation count %zu is broken", reservation);

	/* loop on TX */
	for (size_t i=0; i < numiter; i += reservation) {
		size_t pos;	
		while ((pos = nbuf_reserve_single(&nb->ct, &nb->tx, res_sz)) == -1)
			; /* spinlock like a bitch */
		for (size_t j=0; j < reservation; j++)
			tally += NBUF_DEREF(size_t, pos, j, nb) = i + j;
		Z_die_if(nbuf_release_single(&nb->ct, &nb->rx, res_sz), "");
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

	size_t res_sz = nbuf_reservation_size(nb, reservation);
	Z_die_if(!res_sz, "reservation count %zu is broken", reservation);

	/* loop on RX */
	for (size_t i=0; i < numiter; i += reservation) {
		size_t pos;
		while ((pos = nbuf_reserve_single(&nb->ct, &nb->rx, res_sz)) == -1)
			; /* spinlock like a bitch */
		for (size_t j=0; j < reservation; j++)
			tally += NBUF_DEREF(size_t, pos, j, nb);
		Z_die_if(nbuf_release_single(&nb->ct, &nb->tx, res_sz), "");
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
	Z_die_if(
		nbuf_params(sizeof(size_t), blk_cnt, &nb)
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

out:
	free(nb.ct.buf);
	return err_cnt;
}
