#include "cbuf_int.h"

extern int kill_flag;

/*	cbuf_checkpoint_snapshot()

Take a "snapshot" of a circular buffer, which can be later be checked to verify
	that all outstanding blocks
		(released by sender, not yet consumed AND released by receiver(s))
	have been released.

The basic problem is, from the viewpoint of a cbuf sender:
	"how do I know when receiver has consumed all blocks I have sent?"
This is made trickier by the fact that OTHER senders may be interleaving packets
	amongst and AFTER the ones we have sent.
An interesting factor is that a circular buffer by definition rolls over:
	there is no guarantee that `snd_pos > rcv_pos`.

CAVEATS:
	Before calling `checkpoint`, sender must ALREADY have
		RELEASED any blocks previously reserved.
	A thread may only snapshot and verif ONE cbuf at a single time
		(this is a design tradeoff of using a static __thread struct
		internally).

With checkpoints, we talk about "actual sender" and "actual receiver"
	positions.
The so-called "actual" position of a (sender|receiver) is the most conservative
	estimate of what has been read/written by callers on that side of the
	buffer.
The "actual" position treats "reserved" and "uncommitted" blocks as
	unread or unwritten.
In other words, a block hasn't been consumed until it is RELEASED.

Some interesting equations:
	snd_pos = "actual sender" + (snd_reserved + snd_uncommitted);
		"actual sender" = rcv_pos + ready;
	rcv_pos = "actual receiver" + (rcv_reserved + rcv_uncommitted);
		"actual receiver" = snd_pos + unused;

The concept is that by recording both the "actual receiver" and the
	DIFFERENCE between that and "actual sender",
	we create a snapshot that can be compared against a later value of
	"actual receiver" to see if it has advanced AT LEAST as far as the
	`diff` value in the snapshot.
	*/
void		cbuf_checkpoint_snapshot(struct cbuf *b, cbuf_chk_t *check)
{
	/* zero values */
	check->diff = 0;
	check->actual_rcv = 0;

	/* Don't start checkpointing if we are closing,
	Atomically mark checkpoint so cleanup will wait
		for a verif() operation to decrement until it reaches 0.
		*/
	uint16_t cnt, cnt_new;
	do {
		cnt = __atomic_load_n(&b->chk_cnt, __ATOMIC_RELAXED);
		if (cnt & CBUF_CHK_CLOSING)
			return;
		cnt_new = cnt + 1;
	} while(!__atomic_compare_exchange(&b->chk_cnt, &cnt, &cnt_new,
			0, __ATOMIC_RELAXED, __ATOMIC_RELAXED));

	// use `diff` as a scratchpad to store "actual sender"
	cbuf_actuals__(b, (uint32_t *)&check->diff, (uint32_t*)&check->actual_rcv);
	check->diff = check->diff - check->actual_rcv;

	return;
}

/*	cbuf_checkpoint_verif()
Verifies state of the current cbuf at `b` against a checkpoint previously
	produced by `cbuf_checkpoint_snapshot()`.

RETURNS 1 if all data through `snd_pos` at the time snapshot was taken
	has been consumed by receiver.
	*/
int		cbuf_checkpoint_verif(struct cbuf *buf, cbuf_chk_t *checkpoint)
{
	uint32_t actual_rcv = 0;
	cbuf_actuals__(buf, NULL, &actual_rcv);

	return
		/* first case: we know more data has been consumed than was "diff"
			at time of original checkpoint.
		*/
		( ((int64_t)actual_rcv - checkpoint->actual_rcv) >= checkpoint->diff )
		/* second case: we slept too long and buffer lapped all the way around.
		But then data stopped being sent and we would otherwise wait eternally .. on an empty buffer.
			*/
		|| (buf->snd_pos == buf->rcv_pos);
}

/*	cbuf_checkpoint_loop()
Loops until checkpoint is reached or surpassed, yielding/sleeping in the interim.

Returns: number of loop iterations waited.
	-1 if loop was interrupted by closing buffer.
	*/
int		cbuf_checkpoint_loop(struct cbuf *buf)
{
	/* get snapshot */
	cbuf_chk_t check;
	cbuf_checkpoint_snapshot(buf, &check);

	int i = 0; /* how many times did we loop? */
	while (!cbuf_checkpoint_verif(buf, &check)) {
		i++;

		/* handle case where buffer is closing */
		if ((buf->chk_cnt & CBUF_CHK_CLOSING)
			|| kill_flag)
		{
			i = -1;
			break;
		}

		CBUF_YIELD();
	}

	/* log checkpoint done before exiting */
	__atomic_sub_fetch(&buf->chk_cnt, 1, __ATOMIC_RELAXED);

	Z_log(Z_in2, "%d iter on cbuf checkpoint", i);
	return i;
}
