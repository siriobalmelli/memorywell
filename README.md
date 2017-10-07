# MemoryWell

Speed up synchronization and motion of data between computing threads;
	a lock-free circular buffer done right.

## How So? Pray tell

Moving data between computing threads usually involves:
	- allocating memory
	- writing allocated memory
	- synchronizing: passing a pointer to that memory between threads
	- releasing memory once finished

The performance and scaling bottlenecks lie in allocation and synchronization.

The basic premise of MemoryWell is that allocation and synchronization
	can become a single step, and that step then significantly optimized.

### Specifics

Allocating and freeing memory is slow because it incurs the cost of a syscall,
	and the OS allocator is likely to take a mutex.

Synchronization between threads requires either another mutex
	or a lock-free algorithm.
The former is subject to deadlock; the latter is nontrivial to implement correctly.

## Implementation

MemoryWell is a lock-free circular buffer which moves data in "blocks".
The block-size and blocks-count for a buffer are user-defined.

The fundamental breakthrough is that it is more efficient to contend over
	the **amount** of available blocks as opposed to the exact **position**
	of those blocks.

The model is that threads "reserve" and "release" an amount of blocks;
	and given a "pos" token which allows them to access that reservation:
	- without having to understand precisely **where** it is (simple)
	- without synchronizing with other threads (fast)

Here is a thread reserving blocks and writing data into them:

```c
void *producer_thread_single(void *args)
{
	struct nbuf *buffer = args;

	/* go through every buffer block */
	size_t blk_count = nbuf_blk_count(buffer);
	for (size_t i=0, res=0; i < blk_count; i += res) {
		/* Get a reservation of __at_most__ 42 blocks,
			may return a smaller number if a smaller number is available.
		Returns 0 if no block available.
		*/
		size_t pos;
		while (!(res = nbuf_reserve(&buffer->tx, &pos, 42)))
			sched_yield();	/* No scheduling decisions are taken by the library.
							We could spin or sleep() or wait for a semaphore here,
								or become a consumer and read from the 'rx'
								side of the buffer.
							*/

		/* Write into blocks.
		Notice we access blocks one at a time: some may be at the end
			of the buffer and some may have looped back to the beginning.
		*/
		for (size_t j=0; j < res; j++) {
			void *block = nbuf_access(pos, j, buffer);
			memset(block, 0x1, nbuf_blk_size(buffer));
		}

		/* Release reservation into the other side of the buffer.
		We reserved from 'tx' and so release into 'rx'.
		*/
		nbuf_release_single(&buffer->rx, res);
	}
}
```

The "consumer" thread on the other side would be nearly identical,
	with the difference that it would be reserving from the `rx` side
	and releasing into the `tx` side.

### Multiple producers/consumers

There are 2 distinct types of synchronization/contention:
	- between TX (producer) thread(s) and RX (consumer) thread(s).
	- between multiple threads on one **side** (either TX or RX) of the buffer.

For this reason, buffers/queues are usually referred to as:
	- SPSC: Single Producer, Single Consumer
	- SPMC: Single Producer, Multiple Consumer
	- MPSC: Mulitple Producer, Single Consumer
	- MPMC: Multiple Producer, Multiple Consumer

The design of this library is such that `reserve()` is already safe
	whether used by single or multiple producer/consumer thread(s).

With `release()` however, in the "multiple" case we must guarantee that any
	**earlier** reservations have already been released.
This is performed in `release_multi()`.

`_release_multi()` is more expensive **and may fail**;
	so the caller is given the option of the faster and guaranteed successful
	`_release_single()` if they know only a single thread will access
	one side (`tx` or `rx`) of the buffer.

The previous code example, to become multi-threaded, need only change
	the release call:

```c
void *producer_thread_multi(void *args)
{
	struct nbuf *buffer = args;

	size_t blk_count = nbuf_blk_count(buffer);
	for (size_t i=0, res=0; i < blk_count; i += res) {
		size_t pos;
		while (!(res = nbuf_reserve(&buffer->tx, &pos, 32)))
			sched_yield();

		for (size_t j=0; j < res; j++) {
			void *block = nbuf_access(pos, j, buffer);
			memset(block, 0x1, nbuf_blk_size(buffer));
		}

		/* Release reservation.
		Multiple producer threads are contending on this
			side of the buffer: use _multi() release
			to ensure other side of the buffer does
			not see unfinished data.
		*/
		while (!nbuf_release_multi(&buffer->rx, res, pos))
			sched_yield();
	}
}
```

## Pros and Cons

### Memory agnostic

Here is code to set up and initialize a buffer:

```c
	int ret =0;
	struct nbuf buffer = { {0} };

	/* Calculate buffer dimensions (blocks must be a power of 2).
	'blk_size' and 'blk_count' are previously set to arbitrary values
		that make sense for this particular program.
	*/
	ret = nbuf_params(blk_size, blk_count, &nb);

	/* allocate memory for the buffer and initialize it */
	ret += nbuf_init(&nb, malloc(nbuf_size(&nb)));

	if (ret)
		; /* error handling here */
```

Some points to note:

1. Caller can declare the minimum size and minimum amount of blocks in the buffer:
	buffer blocks can be directly cast to structures when accessed

1. Library does not allocate memory; rather it defines minimum dimensions required:
	- does not enforce location: safe on both the stack and heap
	- safe to use in both user- and kernel-space with no semantic changes

### Efficient

1. Reservation of multiple blocks simultaneously:
	synchronization costs spread over a large number of blocks.

1. Variable reservation size: returns any number of available blocks
	up to the requested amount (which may be `-1` to get all available blocks).

1. Symmetric: blocks are reserved and released identically on either side
	of the buffer (named `tx` and `rx` for clarity when used by caller):
	- reduces code footprint
	- allows for returning data from consumer(s) to producer(s) without
		additional synchronization or a separate buffer.

1. Implements a faster `_release_single()` case when caller knows it is
	the only thread accessing that side (`tx` or `rx`) of the buffer.

1. `reserve()` and `release()` each only touch **one** side of the buffer.
Memory layout puts the `tx` and `rx` sides on different cache lines,
	avoiding "false sharing".
The parameters used in `access()` are in yet a third cache line,
	which will never be written to (invalidated) during operation.

### Never accesses underlying memory

The memory pointed to by the buffer structure is **never** dereferenced or accessed.

This is an advantage when performing zero-copy I/O into and out of the buffer:
	- no page faults are taken
	- no data prefetch is triggered into cache

**TODO:**link to `nmem`

This makes it possible to point a buffer to a region already containing data,
	such as a memory-mapped file, and then using the buffer to synchronize
	access by multiple threads to successive blocks of the file.

### portable

1. Uses C11 Atomics
1. Does not rely on platform-selective tricks
	such as combining multiple variables into a single atomic CAS, etc

### slower than an exchange-only queue of pointers

If for some reason you must already allocate memory and **only** want to
	synchronize a **pointer** between threads,
	nothing beats looping through an array of queues and
	atomically swapping pointers until you find `(!NULL)`.

See presentations by *Fedor G. Pikus* and others.

### block-size and block-count constraints

`blk_size` and `blk_count` must both be a power of 2
	and are fixed after buffer initialization.

In the case that object sizes are wildly variable, `blk_size` may end up being
	very large so as to accomodate a small minority of large objects.

This is undesirable in some scenarios; also please see the preceding note on
	pointer queues.

### slower than RCU for mostly-read scenarios

This library is for synchronizing writer and reader access to a
	continuous stream of data.

RCU solves an entirely different problem-set.
Please refer to the work of *Paul E. McKenney* as well as
	[memory-barriers.txt](https://github.com/torvalds/linux/blob/master/Documentation/memory-barriers.txt)
	in the Linux kernel documentation.

## TODO

- speed differential if combining cache lines (actual impact of false sharing)?
- generic nmath functions so 32-bit size_t case is cared for
- no safety checking or locking on init/deinit; unsure of the best approach here
- Python bindings
- C++ extensions
- non-contention cost of operations (reserving and releasing buffer blocks
	one by one)
- example of stack allocation
- example of underlying file access
- example of returning data to producers
- example of using zero-copy I/O (split nmem from nonlibc?)
- man pages

## Benchmark against other implementations

- <https://github.com/Nyufu/LockFreeRingBuffer/blob/master/unittests/EnqueueDequeueOrder4Thread.cpp>
- <https://github.com/shramov/ring>
- <https://github.com/ixtli/ringbuffer>

## What's with the name tho

All the cliché titles like *lock-free*, *atomic*, *supercritical* and *cherenkov*
	were predictably taken, and adding to the roughly 400 search results for
	*circular buffer* would have been a bit of a buzz-kill.

Hence: **MemoryWell**; your friendly neighborhood circular buffer:
	- a well from whence to draw memory
	- a well-implemented circular buffer
