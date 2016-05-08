#include "cbuf_int.h"


#ifdef CBUF_MEM_INTEGRITY
/* default sanity value: LSb should not be set */
uint64_t cbuf_malign_mask = 0xfffffffffffffffe;

void __attribute__((constructor)) cbuf_malign_constructor_()
{
	/* verify sanity: get system clock value */
	uint64_t sanity = 0x0;
	char *cmd  = "date +\%s";
	FILE *f = popen(cmd, "r");
	int res;
	res = fscanf(f, "%ld", &sanity);
	if(!res || res == EOF) {
		Z_err("unable to determine sane value for platform");
		/* default sanity value: LSb should never be set */
		cbuf_malign_mask = 0xfffffffffffffffe;
	}
	/* Twiddle sane value from OS with known good data.
	Incidentally, this can be used as a mask for malloc values
		to ensure they are proper.
		*/
	//cbuf_malign_mask -= (!(sanity & ~0xffffff) ^ 0x57000000);
	cbuf_malign_mask = ~((sanity >> 24) - 0x57);
}

#endif
