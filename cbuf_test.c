#include "cbuf.h"
#include "mtsig.h"
#include <time.h>
#include <assert.h>

#define STEP_SIZE 32	/* How many blocks should we try to reserve at one time */
//#define NUMITER 4800000	
#define NUMITER 756432	/* Number of read-writes PER THREAD. */
#define THREAD_CNT 33	/* must be less than or equal to NUMITER */

#define OBJ_SZ (sizeof(struct sequence) *1UL)
#define OBJ_CNT (STEP_SIZE * 100UL)

sig_atomic_t kill_flag = 0; /* global kill flag assumed by mtsig */

struct sequence {
	int		i;
	pthread_t	my_id;
	char		padding[48];
};

int test_cbuf_single(int use_malloc);
int test_cbuf_steps(int use_malloc);
int test_cbuf_threaded(int use_malloc);
void *snd_thread(void *args);
void *rcv_thread(void *args);

// RPA added for testing only so all the function signatures matched.
char *map_dir = "/tmp";

uint64_t global_sum = 0;
uint64_t expected_sum = 0;

#ifdef Z_BLK_LVL
#undef Z_BLK_LVL
#endif
#define Z_BLK_LVL 1

int main()
{
	int err_cnt = 0;
	clock_t start;
	Z_inf(0, "obj_sz >= %lu, obj_cnt = %lu, %ld iter", 
		OBJ_SZ, OBJ_CNT, (long)NUMITER * THREAD_CNT);
       
	/* Test that a cbuf does in fact go all the way to UINT32_MAX size.
		Manually adjust for the 8B added by cbuf_create().
		*/
	uint32_t req_sz = (1UL << 31) - sizeof(size_t) -1;
	cbuf_t *random = cbuf_create1(req_sz, 1, map_dir);
	Z_die_if(!random, "buf failed to create req_sz=%u", req_sz);
	Z_die_if(cbuf_sz_buf(random) != req_sz+1+sizeof(size_t),
		"buf_sz %u != req_sz %lu",
		cbuf_sz_buf(random), req_sz+1+sizeof(size_t));
	cbuf_free(random);

	start = clock();
	Z_inf(0, "single thread, single step");
	err_cnt += test_cbuf_single(0);
	start = clock() - start;
	Z_inf(0, "ELAPSED: %ld", start);
       
  	start = clock();
	Z_inf(0, "single thread, single step (malloc'ed)");
	err_cnt += test_cbuf_single(1);
	start = clock() - start;
	Z_inf(0, "ELAPSED: %ld", start);

	start = clock();
	Z_inf(0, "single thread, stepped: %d", STEP_SIZE);
	err_cnt += test_cbuf_steps(0);
	start = clock() - start;
	Z_inf(0, "ELAPSED: %ld", start);

    	start = clock();
	Z_inf(0, "single thread, stepped (malloc'ed): %d", STEP_SIZE);
	err_cnt += test_cbuf_steps(1);
	start = clock() - start;
	Z_inf(0, "ELAPSED: %ld", start);

  	start = clock();
	Z_inf(0, "%d threads, stepped: %d", THREAD_CNT, STEP_SIZE);
	err_cnt += test_cbuf_threaded(0);
	start = clock() - start;
	Z_inf(0, "ELAPSED: %ld", start);

  	start = clock();
	Z_inf(0, "%d threads, stepped (malloc'ed): %d", THREAD_CNT, STEP_SIZE);
	err_cnt += test_cbuf_threaded(1);
	start = clock() - start;
	Z_inf(0, "ELAPSED: %ld", start);

out:
	return err_cnt;
}

/*	test_cbuf_single()
Tests a single-sender -> single-receiver configuration.

Re: turns 0 on success.
	*/
int test_cbuf_single(int use_malloc)
{
	int err_cnt = 0;
	cbuf_t *c = NULL;
	if (use_malloc)
		c = cbuf_create_malloc(OBJ_SZ, OBJ_CNT);
	else
		c = cbuf_create1(OBJ_SZ, OBJ_CNT, map_dir);
	Z_die_if(!c, "expecting buffer");

	int i;
	struct sequence *seq;
	/* mult. by thread count so we do the same total iters as the threaded test */
	for (i=0; i < NUMITER * THREAD_CNT; i++) {
		/* reserve a 'step' of write blocks */
		seq = cbuf_snd_res(c);
		Z_die_if(!seq, "sender single reservation");
		seq->i = i;
		/* release */
		cbuf_snd_rls(c);

		/* do the same for the receive side */
		seq = cbuf_rcv_res(c);
		Z_die_if(!seq, "receive reservation");
		Z_die_if(seq->i != i, "wrong data");
		cbuf_rcv_rls(c);
	}

out:
	if (c)
		cbuf_free(c);
	return err_cnt;
}

int test_cbuf_steps(int use_malloc)
{
	int err_cnt = 0;
	cbuf_t *c = NULL;
	if (use_malloc)
		c = cbuf_create_malloc(OBJ_SZ, OBJ_CNT);
	else
		c = cbuf_create1(OBJ_SZ, OBJ_CNT, map_dir);
	Z_die_if(!c, "expecting buffer");

	int i, j;
	uint32_t pos;
	struct sequence *seq;
	/* mult. by thread count so we do the same total iters as the threaded test */
	for (i=0; i < NUMITER * THREAD_CNT; i += STEP_SIZE) {
		/* reserve a 'step' of write blocks */
		pos = cbuf_snd_res_m(c, STEP_SIZE);
		Z_die_if(pos == -1, "send reservation");
		/* write all blocks */
		for (j=0; j < STEP_SIZE; j++) {
			seq = cbuf_offt(c, pos, j); 
			seq->i = i+j;
		}
		/* release */
		cbuf_snd_rls_m(c, STEP_SIZE);

		/* do the same for the receive side */
		pos = cbuf_rcv_res_m(c, STEP_SIZE);
		Z_die_if(pos == -1, "receive reservation");
		for (j=0; j < STEP_SIZE; j++) {
			seq = cbuf_offt(c, pos, j);
			Z_die_if(seq->i != i+j, "wrong data");
		}
		cbuf_rcv_rls_m(c, STEP_SIZE);
	}

out:
	if (c)
		cbuf_free(c);
	return err_cnt;
}

/*	test_cbuf_threaded()
Creates a cbuf, then runs a multithreaded IO test on it.
If 'use_malloc' is set, uses a cbuf_malloc().

returns 0 on success.
	*/
int test_cbuf_threaded(int use_malloc)
{
	int err_cnt = 0;
	
	/* this is the expected sum of all 'i' loop iterators for all threads 
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
	uint64_t final_verif = (NUMITER -1) * 0.5 * NUMITER * THREAD_CNT;
	/* reset global sums
	(otherwise test will fail when run more than once)
		*/
	global_sum = expected_sum = 0;

	/* circ buf */
	cbuf_t *buf = NULL;
	if (use_malloc)
		buf = cbuf_create_malloc(OBJ_SZ, OBJ_CNT);
	else 
		buf = cbuf_create1(OBJ_SZ, OBJ_CNT, map_dir);

	Z_die_if(!buf, "fail to alloc");

	/* launch senders & receivers */
	int i;
	pthread_t snd_thr[THREAD_CNT];
	pthread_t rcv_thr[THREAD_CNT];
	for (i=0; i < THREAD_CNT; i++) {
		rcv_thr[i] = mts_launch(rcv_thread, buf, mts_free_noop, NULL);
		snd_thr[i] = mts_launch(snd_thread, buf, mts_free_noop, NULL);
	}

	/* recover sender threads, and get amount of busywaits */
	void *ret;
	uint64_t busywait_snd = 0;
	for (i=0; i < THREAD_CNT; i++) {
		pthread_join(snd_thr[i], &ret);
		busywait_snd += (uint64_t)ret;
	}
	Z_inf(0, "senders: %ld waits", busywait_snd);

	/* receiver threads */
	uint64_t busywait_rcv = 0;
	for (i=0; i < THREAD_CNT; i++) {
		pthread_join(rcv_thr[i], &ret);
		busywait_rcv += (uint64_t)ret;
	}
	Z_inf(0, "receivers: %ld waits", busywait_rcv);

	/* verify non-overlap of cbuf reservations */
	uint32_t exp_pos = (cbuf_sz_obj(buf) * NUMITER * THREAD_CNT) & buf->overflow_;
	Z_err_if(exp_pos != (buf->snd_pos & buf->overflow_), 
		"exp_pos %d != snd_pos %d\n \
		obj_sz %d, NUMITER %d, THREAD_CNT %d, overflow 0x%x", 
		exp_pos, buf->snd_pos, cbuf_sz_obj(buf), 
		NUMITER, THREAD_CNT, buf->overflow_);
	/* verify that expected sum is correct */
	Z_err_if(expected_sum != final_verif, 
		"expected_sum %ld - final_verif %ld = %ld",
		expected_sum, final_verif, expected_sum - final_verif);
	/* verify integrity of data */
	Z_err_if(global_sum != expected_sum,
		"global_sum %ld - expected_sum %ld = %ld\n\
		... this is known to fail from time to time and Sirio has no clue WHY ;(((",
		global_sum, expected_sum, global_sum - expected_sum);

out:
	if (buf)
		cbuf_free(buf);
	return err_cnt;
}

void *rcv_thread(void *args)
{
	volatile uint64_t busy_waits = 0; /* volatile because mts_jump */
	mts_setup_thr_();

	cbuf_t *b = (cbuf_t *)args;
	mts_jump_set_
	mts_jump_reinit_exec_
	mts_jump_end_block_

	struct sequence *s;
	uint32_t pos;
	int i, j;
	size_t step_sz;
	for (i=0; i < NUMITER; i += step_sz) {
retry:
		step_sz = STEP_SIZE;
		if ( i + step_sz > NUMITER )
		       step_sz = NUMITER - i;	
		pos = cbuf_rcv_res_m_cap(b, &step_sz);
		if (pos == -1) {
			/* if no reservation available, busy-wait */
			busy_waits++;
			pthread_yield();
			goto retry;
		} else {
			for (j=0; j < step_sz; j++) {
				s = cbuf_offt(b, pos, j);
				/* add to global sum; will be used to test data integrity */
				__atomic_add_fetch(&global_sum, 
					__atomic_load_n(&s->i, __ATOMIC_SEQ_CST),
					__ATOMIC_SEQ_CST);
				//Z_inf(0, "s @%08lx i = %d", (uint64_t)s, s->i);
			}
			cbuf_rcv_rls_m(b, step_sz);
		}

	}

	mts_cleanup_thr_();
	/* return number of busy-waits we had to go through */
	pthread_exit((void*)busy_waits);
}

void *snd_thread(void *args)
{
	volatile uint64_t busy_waits = 0;
	mts_setup_thr_();

	cbuf_t *b = (cbuf_t *)args;
	mts_jump_set_
	mts_jump_reinit_exec_
	mts_jump_end_block_

	int i, j;
	struct sequence *s;
	uint32_t pos;
	size_t step_sz = 0;
	for (i=0; i < NUMITER; i += step_sz) {
retry:
		/* NO: step size is hard-coded in test */
		step_sz = STEP_SIZE;
		if ( i + step_sz > NUMITER )
		       step_sz = NUMITER - i;	
		pos = cbuf_snd_res_m_cap(b, &step_sz);
		if (pos == -1) {
			/* if no reservation available, busy-wait */
			busy_waits++;
			pthread_yield();
			goto retry;
		} else {
			/* add to the sequence */
			for (j=0; j < step_sz; j++) {
				s = cbuf_offt(b, pos, j);
				/* this will be used for 'data integrity' check */
				__atomic_store_n(&s->i, i+j, __ATOMIC_SEQ_CST);
				/* ... and it will be checked against this: */
				__atomic_add_fetch(&expected_sum, s->i, __ATOMIC_SEQ_CST);
			}
			cbuf_snd_rls_m(b, step_sz);
		}
	}
	//Z_inf(0, "tx: sent %d datums", i);
	i = cbuf_checkpoint_loop(b);
	Z_inf(1, "%d iter on cbuf_checkpoint_verif", i);

	mts_cleanup_thr_();
	/* return number of busy-waits we had to go through */
	pthread_exit((void*)busy_waits);
}

#undef Z_BLK_LVL
#define Z_BLK_LVL 0
