#include "cbuf.h"
#include "sbfu.h"
#include "zed_dbg.h"

#define BLK_CNT 66666 /* ~100MB */
#define BLK_SZ 1500

int main() {
	int err_cnt = 0;
	uint64_t *test;

	/* accounting */
	cbufp_t f;	
	memset(&f, 0x0, sizeof(f));
	f.file_path = malloc(strlen("./test_file.bin") + 1);
	sprintf(f.file_path, "./test_file.bin");
	f.iov.iov_len = (BLK_CNT * BLK_SZ);

	/* create backing store */
	Z_die_if(!(f.fd = sbfu_dst_map(&f.iov, f.file_path)), "");
	f.blk_iov.iov_len = BLK_SZ;
	f.blk_iov.iov_base = f.iov.iov_base;

	/* create cbuf */
	cbuf_t *b = cbuf_create(sizeof(f), BLK_CNT);
	uint32_t pos = cbuf_snd_res_m(b, BLK_CNT);
	Z_die_if(pos == -1, "");
	/* populate tracking structures */
	cbufp_t *p;
	for (f.blk_id=0; f.blk_id < BLK_CNT; 
		f.blk_id++, f.blk_iov.iov_base += BLK_SZ) 
	{
		p = cbuf_offt(b, pos, f.blk_id);
		memcpy(p, &f, sizeof(f));
		test = (uint64_t *)p->blk_iov.iov_base;
		*test = f.blk_id;
	};
	cbuf_snd_rls_m(b, BLK_CNT);
	
	/* go through cbuf and check things are in order */
	pos = cbuf_rcv_res_m(b, BLK_CNT);
	Z_die_if(pos == -1, "rcv res");
	uint64_t i;
	for (i=0; i < BLK_CNT; i++) {
		p = cbuf_offt(b, pos, i);
		Z_die_if(p->fd != f.fd, "p->fd %d != f.fd %d", p->fd, f.fd);
		test = p->blk_iov.iov_base;
		Z_die_if(i != p->blk_id || i != *test, 
			"i %ld == p->blk_id %ld == *test %ld",
			i, p->blk_id, *test);
	}
	cbuf_rcv_rls_m(b, BLK_CNT);

	/* clean up */
	p = b->buf;
	free(p->file_path);
	cbuf_free(b);
	sbfu_unmap(f.fd, &f.iov);
out:
	return err_cnt;
}
