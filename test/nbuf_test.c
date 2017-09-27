#include <zed_dbg.h>
#include <nbuf.h>
#include <stdlib.h>
#include <pthread.h>

/*	benchmark/test cases
-	queue mostly empty
-	queue mostly full
-	max throughput (what Tony is doing now)
*/

const size_t numiter = 100000000;
const size_t thread_cnt = 1;

void* tx_thread(void *arg);
void* rx_thread(void *arg);

uint64_t tx_i_sum = 0;
uint64_t rx_i_sum = 0;

int main()
{
	int err_cnt = 0;

	struct nbuf nb = { 0 };
	Z_die_if(nbuf_params(sizeof(uint64_t), numiter, &nb), "");
	Z_die_if(!(
		malloc(nbuf_size(&nb))
		), "size %zu", nbuf_size(&nb));

	pthread_t tx;
	pthread_t rx;
	pthread_create(&tx, NULL, &rx_thread, &nb);
	pthread_create(&rx, NULL, &tx_thread, &nb);

	/* wait for threads to finish */
	void* ret;
	pthread_join(tx, &ret);
	pthread_join(rx, &ret);

	/* verify sums */
	uint64_t verif_i_sum = (numiter -1) * 0.5 * numiter * thread_cnt;
	Z_die_if(verif_i_sum != tx_i_sum,
		"tx_i_sum %ld - verif_i_sum %ld = %ld",
		tx_i_sum, verif_i_sum, tx_i_sum - verif_i_sum);
	Z_die_if(rx_i_sum != tx_i_sum,
		"rx_i_sum %ld - tx_i_sum %ld = %ld\n",
		rx_i_sum, tx_i_sum, rx_i_sum - tx_i_sum);

out:
	return err_cnt;
}

void* tx_thread(void* arg)
{
	struct nbuf nb = *((struct nbuf*)arg);

	/* TX thread */
	for (size_t i=0; i < numiter; i++) {
		size_t pos;	
		while ((pos = nbuf_reserve_single(&nb.ct, &nb.tx, 1)) == -1)
			; /* spinlock like a bitch */
		*((uint64_t*)nbuf_access(pos, 0, &nb)) = i;
		tx_i_sum += i;
		Z_die_if(nbuf_release_single(&nb.ct, &nb.rx, 1), "");
	}

out:
	return NULL;
}

void* rx_thread(void* arg)
{
	struct nbuf nb = *((struct nbuf*)arg);

	/* RX thread */
	for (size_t i=0; i < numiter; i++) {
		size_t pos;
		while ((pos = nbuf_reserve_single(&nb.ct, &nb.rx, 1)) == -1)
			; /* spinlock like a bitch */
		rx_i_sum += *((uint64_t*)nbuf_access(pos, 0, &nb));
		Z_die_if(nbuf_release_single(&nb.ct, &nb.tx, 1), "");
	}

out:
	return NULL;
}
