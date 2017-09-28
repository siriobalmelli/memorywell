#include <zed_dbg.h>
#include <nbuf.h>
#include <stdlib.h>

/*	benchmark/test cases
For each of a set of element sizes:
-	queue mostly empty
-	queue mostly full
-	max throughput (what Tony is doing now)
*/

const size_t numiter = 100000000;

int main()
{
	int err_cnt = 0;

	struct nbuf nb = { 0 };
	Z_die_if(nbuf_params(sizeof(uint64_t), numiter, &nb), "");
	Z_die_if(!(
		malloc(nbuf_size(&nb))
		), "size %zu", nbuf_size(&nb));

	/* TX thread */
	for (size_t i=0; i < numiter; i++) {
		size_t pos;	
		while ((pos = nbuf_reserve_single(&nb.ct, &nb.tx, 1)) == -1)
			; /* spinlock like a bitch */
		*((uint64_t*)nbuf_access(pos, 0, &nb)) = i;
		Z_die_if(nbuf_release_single(&nb.ct, &nb.rx, 1), "");
	}

out:
	return err_cnt;
}
