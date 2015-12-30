#include "cbuf_int.h"

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

The concept is that by (atomically) recording both the "actual receiver"
	and the DIFFERENCE between that and "actual sender",
	we create a snapshot that can be compared against a later value of
	"actual receiver" to see if it has advanced AT LEAST as far as the
	`diff` value in the snapshot.

RETURNS: a pointer which can be fed to subsequent `verif()` calls.
	*/
cbuf_chk_t	*cbuf_checkpoint_snapshot(cbuf_t *b)
{
	/* obviates memory leak from function exiting for some other reason
		before it is done looping on `checkpoint_verif()`.
		*/
	static __thread cbuf_chk_t ret = {
		.diff = 0,
		.actual_rcv = 0	
	};

	// use `diff` as a scratchpad to store "actual sender"
	cbuf_actuals__(b, (uint32_t *)&ret.diff, (uint32_t*)&ret.actual_rcv);
	ret.diff = ret.diff - ret.actual_rcv;

	return &ret;
}

/*	cbuf_checkpoint_verif()
Verifies state of the current cbuf at `b` against a checkpoint previously
	produced by `cbuf_checkpoint_snapshot()`.

RETURNS 1 if all data through `snd_pos` at the time snapshot was taken 
	has been consumed by receiver.
	*/
int		cbuf_checkpoint_verif(cbuf_t *b, cbuf_chk_t *checkpoint)
{
	int64_t actual_rcv = 0;
	cbuf_actuals__(b, NULL, (uint32_t *)&actual_rcv);

	return (actual_rcv - checkpoint->actual_rcv) >= checkpoint->diff;
}

/*	cbuf_checkpoint_loop()
Loops until checkpoint is reached or surpassed, yielding/sleeping in the interim.

Returns: number of loop iterations waited.
	*/
int		cbuf_checkpoint_loop(cbuf_t *buf)
{
	int i = 0; /* how many times do we loop? */
	cbuf_chk_t *check = cbuf_checkpoint_snapshot(buf);
	
	while (!cbuf_checkpoint_verif(buf, check)) {
		i++;
		CBUF_YIELD();
	}

	return i;
}
