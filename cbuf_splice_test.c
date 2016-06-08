
/*	This test program exists to demonstrate and test 
		cbuf's splicing (zero-copy) facilities.
	It copies an input file to an output file using two threads
		(a tx and an rx) linked by a cbuf.
*/

/* open */
#ifndef _GNU_SOURCE
	#define _GNU_SOURCE /* splice() */
#endif
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
/* unlink */
#include <unistd.h>
#include <stdlib.h>

#include "cbuf.h"
#include "mtsig.h"
#include "zed_dbg.h"

int test_splice_integrity();
/* 'p' == backing store */
int test_p();
int test_p_malloc();
/* 's' == splice test */
void *splice_tx(void *args);
void *splice_rx(void *args);
int test_splice();
/* test splice() call */
int straight_splice();

char *map_dir = NULL;

#define BLK_CNT	1024
#define BLK_SZ 8192

sig_atomic_t kill_flag = 0;
int src_fd = 0, dst_fd = 0;
void *src_buf = NULL, *dst_buf = NULL;
size_t sz_src = 0, sz_sent = 0;
cbuf_t *b = NULL;

/*	test_p()
Tests a cbuf_p (aka: cbuf with backing store).
	*/
int test_p()
{
	int err_cnt = 0;
	int plumbing[2] = { 0, 0 };
	Z_die_if(pipe(plumbing), "bad turd-herder");

	/* make cbufp large enough for entire file */
	uint32_t cnt = sz_src / BLK_SZ + 1;
	size_t pkt_sz = BLK_SZ;
	b = cbuf_create_p(BLK_SZ, cnt, map_dir); 
	Z_die_if(!b, "");

	uint32_t pos = cbuf_snd_res_m(b, cnt);
	Z_die_if(pos == -1, "can't reserve %d blocks snd", cnt);
	size_t i, temp;
	for (i=0; i < cnt; i++) {
		/* last packet is smaller is smaller than the stock packet size */
		if (i == cnt-1)
			pkt_sz = sz_src - (pkt_sz * (cnt-1));

		/* splice into pipe (increments seek position @ source */
		temp = splice(src_fd, NULL, plumbing[1], NULL, pkt_sz, 0);
		Z_die_if(temp != pkt_sz, 
			"file -> plumbing: temp %ld != pkt_sz %ld @i=%ld",
			temp, pkt_sz, i);

		/* splice into 'cbuf' (actually, backing store) */
		temp = cbuf_splice_from_pipe(plumbing[0], b, pos, i, pkt_sz);
		Z_die_if(temp != pkt_sz, 
			"plumbing -> cbufp: temp %ld != pkt_sz %ld @i=%ld",
			temp, pkt_sz, i);
	}
	cbuf_snd_rls_m(b, cnt);
	
	pos = cbuf_rcv_res_m(b, cnt);
	pkt_sz = BLK_SZ;
	Z_die_if(pos == -1, "can't reserve %d blocks rcv", cnt);
	for (i=0; i < cnt; i++) {
		/* last packet is smaller is smaller than the stock packet size */
		if (i == cnt-1)
			pkt_sz = sz_src - (pkt_sz * (cnt-1));

		/* splice into plumbing */
		temp = cbuf_splice_to_pipe(b, pos, i, plumbing[1]);
		Z_die_if(temp != pkt_sz, 
			"cbuf -> plumbing: temp %ld != pkt_sz %ld @i=%ld", 
			temp, pkt_sz, i);

		/* splice into destination file.
		Increments sz_sent as it writes.
			*/
		temp = splice(plumbing[0], NULL, dst_fd, (loff_t *)&sz_sent, pkt_sz, 0);
		Z_die_if(temp != pkt_sz, 
			"plumbing -> file: temp %ld != pkt_sz %ld @i=%ld", 
			temp, pkt_sz, i);
	}

out:
	if (plumbing[0])
		close(plumbing[0]);
	if (plumbing[1])
		close(plumbing[1]);
	return err_cnt;
}

/*	test_p_malloc()
Tests a cbuf_p in the malloc'ed case (aka: cbuf with backing store).
	*/
int test_p_malloc()
{
	int err_cnt = 0;
	int plumbing[2] = { 0, 0 };
	Z_die_if(pipe(plumbing), "bad turd-herder");

	/* make cbufp large enough for entire file */
	uint32_t cnt = sz_src / BLK_SZ + 1;
	size_t pkt_sz = BLK_SZ;

	Z_die_if(!b, "");

	uint32_t pos = cbuf_snd_res_m(b, cnt);
	Z_die_if(pos == -1, "can't reserve %d blocks snd", cnt);
	size_t i, temp;
	for (i=0; i < cnt; i++) {
		/* last packet is smaller is smaller than the stock packet size */
		if (i == cnt-1)
			pkt_sz = sz_src - (pkt_sz * (cnt-1));

		/* splice into pipe (increments seek position @ source */
		temp = splice(src_fd, NULL, plumbing[1], NULL, pkt_sz, 0);
		Z_die_if(temp != pkt_sz, 
			"file -> plumbing: temp %ld != pkt_sz %ld @i=%ld",
			temp, pkt_sz, i);

		/* splice into 'cbuf' (actually, backing store) */
		temp = cbuf_splice_from_pipe(plumbing[0], b, pos, i, pkt_sz);
		Z_die_if(temp != pkt_sz, 
			"plumbing -> cbufp: temp %ld != pkt_sz %ld @i=%ld",
			temp, pkt_sz, i);
	}
	cbuf_snd_rls_m(b, cnt);
	
	pos = cbuf_rcv_res_m(b, cnt);
	pkt_sz = BLK_SZ;
	Z_die_if(pos == -1, "can't reserve %d blocks rcv", cnt);
	for (i=0; i < cnt; i++) {
		/* last packet is smaller is smaller than the stock packet size */
		if (i == cnt-1)
			pkt_sz = sz_src - (pkt_sz * (cnt-1));

		/* splice into plumbing */
		temp = cbuf_splice_to_pipe(b, pos, i, plumbing[1]);
		Z_die_if(temp != pkt_sz, 
			"cbuf -> plumbing: temp %ld != pkt_sz %ld @i=%ld", 
			temp, pkt_sz, i);

		/* splice into destination file.
		Increments sz_sent as it writes.
			*/
		temp = splice(plumbing[0], NULL, dst_fd, (loff_t *)&sz_sent, pkt_sz, 0);
		Z_die_if(temp != pkt_sz, 
			"plumbing -> file: temp %ld != pkt_sz %ld @i=%ld", 
			temp, pkt_sz, i);
	}

out:
	if (plumbing[0])
		close(plumbing[0]);
	if (plumbing[1])
		close(plumbing[1]);
	return err_cnt;
}

void *splice_tx(void *args)
{
	mts_setup_thr_();

	mts_jump_set_
	mts_jump_reinit_exec_
	mts_jump_end_block_

	int err_cnt = 0;
	int fittings[2];
	Z_die_if(pipe(fittings), "");
	size_t sz_pushed = 0;

	uint32_t pos  = 0;
	size_t step_sz = 0;
	int i;

	size_t cbuf_payload = cbuf_splice_max(b);
	size_t sz_outstanding = 0;
	size_t temp;

	while (sz_pushed < sz_src && !kill_flag) {
		/* auto - advances the seek on src_fd */
		sz_outstanding = splice(src_fd, NULL, fittings[1], NULL, sz_src - sz_pushed, 0);
		if (sz_outstanding == -1)
			break;
		while (sz_outstanding) {
			step_sz = (sz_outstanding >> b->sz_bitshift_) +1;
			pos = cbuf_snd_res_m_cap(b, &step_sz);
			if (pos == -1) {
				pthread_yield();
				continue;
			}
			for (i = 0; i < step_sz; i++) {
				temp = cbuf_splice_from_pipe(fittings[0], b, pos, i, cbuf_payload);
				sz_outstanding -= temp;
				sz_pushed += temp;
			}
			cbuf_snd_rls_m(b, step_sz);
		}
	}
	i = cbuf_checkpoint_loop(b);
	Z_inf(1, "%d iter on cbuf_checkpoint_verif", i);

out:
	kill_flag += err_cnt;
	if (fittings[0])
		close(fittings[0]);
	if (fittings[1])
		close(fittings[1]);
	mts_cleanup_thr_();
	pthread_exit(NULL);
}

void *splice_rx(void *args)
{
	mts_setup_thr_();

	mts_jump_set_
	mts_jump_reinit_exec_
	mts_jump_end_block_

	int err_cnt = 0;
	int fittings[2];
	Z_die_if(pipe(fittings), "");

	uint32_t pos;
	size_t step_sz;
	int i;

	ssize_t temp;

	while (sz_sent < sz_src && !kill_flag) {
		/* get buffer chunks, dump them all into local pipe */
		step_sz = ((sz_src - sz_sent) >>b->sz_bitshift_) + 1;
		pos = cbuf_rcv_res_m_cap(b, &step_sz);
		if (pos == -1) {
			pthread_yield();
			continue;
		}
		for (i=0; i < step_sz; i++) {
			temp = cbuf_splice_to_pipe(b, pos, i, fittings[1]);
			/* Immediately splice - otherwise fittings may be full
				while we still have open cbufs.
			Increments sz_sent as it writes.
				*/
			temp = splice(fittings[0], NULL, 
				dst_fd, (loff_t *)&sz_sent, temp, 0);
			Z_die_if(temp == -1, "write to dest file");
		}
		cbuf_rcv_rls_m(b, step_sz);
	}

out:
	/* don't set kill_flag - 
	   rely on sender to checkpoint when the buffer is cleaned up */
	kill_flag += err_cnt;
	if (fittings[0])
		close(fittings[0]);
	if (fittings[1])
		close(fittings[1]);
	mts_cleanup_thr_();
	pthread_exit(NULL);
}

/*	test_splice()
src_file ->[tx_thread]-> cbuf ->[rx_thread]-> dst_file
*/
int test_splice()
{
	int err_cnt = 0;

	/* make cbuf */
	b = cbuf_create(BLK_SZ, BLK_CNT, map_dir);
	Z_die_if(!b, "");

	pthread_t tx_thr = mts_launch(splice_tx, NULL, NULL, NULL);
	pthread_t rx_thr = mts_launch(splice_rx, NULL, NULL, NULL);

	/*
	while (!kill_flag)
		sleep(1);
		*/

	pthread_join(tx_thr, NULL);
	pthread_join(rx_thr, NULL);
out:
	return err_cnt;
}

/*	test_splice_malloc()
src_file ->[tx_thread]-> cbuf(malloc) ->[rx_thread]-> dst_file
*/
int test_splice_malloc()
{
	int err_cnt = 0;

	/* make cbuf */
	b = cbuf_create_malloc(BLK_SZ, BLK_CNT);
	Z_die_if(!b, "");

	pthread_t tx_thr = mts_launch(splice_tx, NULL, NULL, NULL);
	pthread_t rx_thr = mts_launch(splice_rx, NULL, NULL, NULL);

	/*
	while (!kill_flag)
		sleep(1);
		*/

	pthread_join(tx_thr, NULL);
	pthread_join(rx_thr, NULL);
out:
	return err_cnt;
}

/*	straight_splice()
Demonstrates use of the Linux-specific splice() call.
	*/
int straight_splice()
{
	int err_cnt = 0;
	int piping[2];
	/*		This outperforms `cp` for a 500MB file 	
		sz_src = source file size
		sz_received = pulled from first splice
		sz_sent = pushed to file
	*/
	Z_die_if(pipe(piping), "");
	int temp;
	size_t sz_received = 0;
	while (sz_sent < sz_src) {
		/* advances the seek on src_fd */
		sz_received = splice(src_fd, NULL, piping[1], NULL, sz_src - sz_sent, 0);
		if (sz_received == -1)
			break;
		while (sz_received) {
			/* NOTE that splice() ADVANCES the offset (!). */
			temp = splice(piping[0], NULL, dst_fd, 
				(loff_t *)&sz_sent, sz_received, 0);
			Z_die_if(temp == -1, "dst splice size %ld", sz_sent);
			//sz_sent += temp; /* splice is advancing the offset */
			sz_received -= temp;
		}
	}
out:
	/* pipe */
	if (piping[0])
		close(piping[0]);
	if (piping[1])
		close(piping[1]);

	return err_cnt;
}

/*	test_splice_integrity()
Plays with splice() calls, shows how writes are handled to underlying files.
	*/
int test_splice_integrity()
{
	const int i_flags = O_RDWR | O_NOATIME | O_CREAT | O_TRUNC | O_NONBLOCK;
	const int i_size = UINT8_MAX; /* how large a memory region? */

	int err_cnt = 0;
	int i;
	int i_src_fd = 0, i_dst_fd = 0;
	uint8_t *i_src_data = NULL, *i_dst_data = NULL;
	cbuf_t *b = NULL;

	uint32_t pos;
	size_t check;

	/* source */
	const char *i_src_name = "./i_src.bin";
	i_src_fd = open(i_src_name, i_flags, S_IRUSR | S_IWUSR);
	Z_die_if(i_src_fd < 1, "open %s", i_src_name);
	Z_die_if(ftruncate(i_src_fd, i_size), "");
	Z_die_if(unlink(i_src_name), "");
	i_src_data = mmap(NULL, i_size, PROT_READ | PROT_WRITE, MAP_SHARED, i_src_fd, 0);
	Z_die_if(i_src_data == MAP_FAILED, "");

	/* destination */
	const char *i_dst_name = "./i_dst.bin";
	i_dst_fd = open(i_dst_name, i_flags, S_IRUSR | S_IWUSR);
	Z_die_if(i_dst_fd < 1, "open %s", i_dst_name);
	Z_die_if(ftruncate(i_dst_fd, i_size), "");
	Z_die_if(unlink(i_dst_name), "");
	i_dst_data = mmap(NULL, i_size, PROT_READ | PROT_WRITE, MAP_SHARED, i_dst_fd, 0);
	Z_die_if(i_dst_data == MAP_FAILED, "");
	
	/* plumbing */
	int plumbing[2] = { 0, 0 };
	Z_die_if(pipe(plumbing), "bad turd-herder");

	/* cbuf 
		... leave space for header at head of buf */
	Z_die_if(!(b = cbuf_create(i_size + sizeof(ssize_t), 1, map_dir)), "");

	/* set source */
	for (i=0; i < i_size; i++)
		i_src_data[i] = i;
	/* source -> pipe */
	check = splice(i_src_fd, NULL, plumbing[1], NULL, i_size, 0);
	Z_die_if(check != i_size, "splice: src -> pipe");

	/* pipe -> cbuf */
	pos = cbuf_snd_res_m(b, 1);
	Z_die_if(pos == -1, "snd reserve");
	check = cbuf_splice_from_pipe(plumbing[0], b, pos, 0, i_size);
	Z_die_if(check != i_size, "splice: pipe -> cbuf");
	cbuf_snd_rls(b);

	/* cbuf -> pipe */
	pos = cbuf_rcv_res_m(b, 1);
	Z_die_if(pos == -1, "rcv reserve");
	check = cbuf_splice_to_pipe(b, pos, 0, plumbing[1]);
	Z_die_if(check != i_size, "splice: cbuf -> pipe");
	/* get a handle on cbuf memory so we can check it's contents directly */
	// RPA for work on head being put to the end of the buffer 
	// size_t *head;
	// uint8_t *cbuf_mem_check = b->buf + cbuf_lofft(b, pos, 0, &head);
	size_t *data_len;
	uint8_t *cbuf_mem_check = b->buf + cbuf_lofft(b, pos, 0, &data_len);
	cbuf_rcv_rls(b); /* don't HAVE to release, we won't use cbuf again */

	/* write to destination file, verify data is clean */
	splice(plumbing[0], NULL, i_dst_fd, NULL, i_size, 0);
	for (i=0; i < i_size; i++)
		Z_err_if(i_dst_data[i] != i, "%d,%d", i_dst_data[i], i);

	/* play with data, see what happens */
	Z_die_if(i_src_data[0] != cbuf_mem_check[0]  /* all same */
		|| cbuf_mem_check[0] != i_dst_data[0], "src:%d buf:%d dst:%d",
		i_src_data[0], cbuf_mem_check[0], i_dst_data[0]);

	i_src_data[0] = 9;
	cbuf_mem_check[0] = 8;
	i_dst_data[0] = 7;

	Z_die_if(i_src_data[0] == cbuf_mem_check[0]  /* all different */
		|| cbuf_mem_check[0] == i_dst_data[0], "src:%d buf:%d dst:%d",
		i_src_data[0], cbuf_mem_check[0], i_dst_data[0]);

out:

	/* clean up cbuf */
	if (b)
		cbuf_free(b);

	/* unmap */
	if (i_src_data && i_src_data != MAP_FAILED)
		Z_err_if(munmap(i_src_data, i_size), "");
	if (i_dst_data && i_dst_data != MAP_FAILED)
		Z_err_if(munmap(i_dst_data, i_size), "");

	/* close FD's */
	if (i_src_fd)
		close(i_src_fd);
	if (i_dst_fd)
		close(i_dst_fd);
	/* clean up plumbing */
	if (plumbing[0])
		close(plumbing[0]);
	if (plumbing[1])
		close(plumbing[1]);

	return err_cnt;
}

/*	setup_files()
Map source and destination files, get lengths, etc.
	*/
int setup_files(int argc, char **argv)
{
	int err_cnt = 0;

	/* open source file */
	//src_fd = open(argv[2], O_RDONLY); /* NO: mmap will fail unless O_RDWR */
	src_fd = open(argv[2], O_RDWR | O_NOATIME);
	Z_die_if(src_fd < 1, "open %s", argv[2]);
	/* open dest. file */
	dst_fd = open(argv[3], O_RDWR | O_CREAT | O_TRUNC | O_NONBLOCK | O_NOATIME, 
		S_IRUSR | S_IWUSR);
	Z_die_if(!dst_fd || dst_fd == -1, "open %s", argv[3]);

	/* MMAP src */
	sz_src = lseek(src_fd, 0, SEEK_END);
	Z_die_if(sz_src == -1 || sz_src == 0, "sz_src %ld", sz_src);
	lseek(src_fd, 0, SEEK_SET);
	//src_buf = mmap(NULL, sz_src, PROT_READ | PROT_WRITE, MAP_SHARED, src_fd, 0);
	src_buf = mmap(NULL, sz_src, PROT_READ, MAP_SHARED, src_fd, 0);
	Z_die_if(src_buf == MAP_FAILED, "");
	/* dst */
	Z_die_if(ftruncate(dst_fd, sz_src), "");
	dst_buf = mmap(NULL, sz_src, PROT_READ | PROT_WRITE, MAP_SHARED, dst_fd, 0);
	Z_die_if(dst_buf == MAP_FAILED, "");

out:
	return err_cnt;
}

int main(int argc, char **argv)
{
	int err_cnt = 0;
	Z_die_if(argc < 2 || argc > 5, 
		"usage: %s [r|s|m|p|i] SOURCE_FILE OUTPUT_FILE [MAP_DIR]", argv[0])

	/* It's not illegal to have this NULL: cbuf library will use
		'/tmp' as map_dir in that case.
		*/
/*	if (argc == 5)
		map_dir = argv[4];
	else
		map_dir = NULL;
*/
	mtsig_util_sigsetup(mtsig_util_handler);

	switch (argv[1][0]) {
	case 'r':
		/* 'regular' mode: just do a splice file -> file 
		   as a proof of splice() usage and general comparison. 
			*/
		Z_die_if(setup_files(argc, argv), "");
		err_cnt += straight_splice();
		break;
	case 's':
		/* 'splice' mode: splice file -> tx_pipe -> cbuf -> rx_pipe -> file */
		Z_die_if(setup_files(argc, argv), "");
		err_cnt += test_splice();
		break;
	case 'm':
		/* 'splice' mode: splice file -> tx_pipe -> cbuf(malloc) -> rx_pipe -> file */
		Z_die_if(setup_files(argc, argv), "");
		err_cnt += test_splice_malloc();
		break;
	case 'p':
		/* test backing store */
		Z_die_if(setup_files(argc, argv), "");
		err_cnt += test_p();
		break;
	case 'i':
		err_cnt += test_splice_integrity();
		break;
	default:
		Z_err("don't know the opcode '%c'", argv[1][0]);
	}

	Z_inf(0, "source: %ld, dest: %ld", sz_src, sz_sent);

out:
	/* close cbuf */
	if (b) {
		cbuf_free(b);
		b = NULL;
	}

	/* unmap memory 
		Notice that both are sz_src large
		*/
	if (src_buf && src_buf != MAP_FAILED)
		Z_err_if(munmap(src_buf, sz_src) == -1, "");
	if (dst_buf && dst_buf != MAP_FAILED)
		Z_err_if(munmap(dst_buf, sz_src) == -1, "");

	/* close fd's */
	if (src_fd && src_fd != -1)
		close(src_fd);
	if (dst_fd && dst_fd != -1)
		close(dst_fd);

	return err_cnt;
}
