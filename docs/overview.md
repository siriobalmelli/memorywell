---
title: Overview
order: 1
---

# Audience

This document is a general look at how MemoryWell is constructed,
	without diving into the nitty-gritty details.

If you plan on using this code in your project,
	this document will help make sense of the example code
	(aka: the [tests](../test)).
It's also a very handy guide on how to avoid shooting yourself in the foot.

If you plan on reviewing or contributing to the project,
	this document will demistify the code and help understand the
	terminology used.

## Premise

Moving data between computing threads usually involves:
	- allocating memory
	- writing allocated memory
	- synchronizing: passing a pointer to that memory between threads
	- releasing memory once finished

The performance and scaling bottlenecks lie in allocation and synchronization.

Allocating and freeing memory is slow because it incurs the cost of a syscall,
	and the OS allocator is likely to take a mutex.

Synchronization between threads requires either another mutex
	or a lock-free algorithm.
The former is subject to deadlock; the latter is nontrivial to implement correctly.

The basic premise of MemoryWell is that allocation and synchronization
	can become a single step, and that step can then be significantly optimized.

# Implementation

MemoryWell is a lock-free circular buffer which moves data in "blocks".
The block-size and block-count for a buffer are user-defined.

## Circular Buffer

A block of memory (buffer) which is written to and read from in a "circular"
	fashion: when reaching the end, starting back at the beginning.

## Approach

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
	struct well *buffer = args;

	/* go through every buffer block */
	size_t blk_count = well_blk_count(buffer);
	for (size_t i=0, res=0; i < blk_count; i += res) {
		/* Get a reservation of __at_most__ 42 blocks,
			may return a smaller number if a smaller number is available.
		Returns 0 if no block available.
		*/
		size_t pos;
		while (!(res = well_reserve(&buffer->tx, &pos, 42)))
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
			void *block = well_access(pos, j, buffer);
			memset(block, 0x1, well_blk_size(buffer));
		}

		/* Release reservation into the other side of the buffer.
		We reserved from 'tx' and so release into 'rx'.
		*/
		well_release_single(&buffer->rx, res);
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
	struct well *buffer = args;

	size_t blk_count = well_blk_count(buffer);
	for (size_t i=0, res=0; i < blk_count; i += res) {
		size_t pos;
		while (!(res = well_reserve(&buffer->tx, &pos, 32)))
			sched_yield();

		for (size_t j=0; j < res; j++) {
			void *block = well_access(pos, j, buffer);
			memset(block, 0x1, well_blk_size(buffer));
		}

		/* Release reservation.
		Multiple producer threads are contending on this
			side of the buffer: use _multi() release
			to ensure other side of the buffer does
			not see unfinished data.
		*/
		while (!well_release_multi(&buffer->rx, res, pos))
			sched_yield();
	}
}
```

## Pros and Cons

### Pro: memory agnostic

Here is code to set up and initialize a buffer:

```c
	int ret =0;
	struct well buffer = { {0} };

	/* Calculate buffer dimensions (blocks must be a power of 2).
	'blk_size' and 'blk_count' are previously set to arbitrary values
		that make sense for this particular program.
	*/
	ret = well_params(blk_size, blk_count, &nb);

	/* allocate memory for the buffer and initialize it */
	ret += well_init(&nb, malloc(well_size(&nb)));

	if (ret)
		; /* error handling here */
```

Some points to note:

1. Caller can declare the minimum size and minimum amount of blocks in the buffer:
	buffer blocks can be directly cast to structures when accessed

1. Library does not allocate memory; rather it defines minimum dimensions required:
	- does not enforce location: safe on both the stack and heap
	- safe to use in both user- and kernel-space with no semantic changes

### Pro: efficient

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

1. Avoids `malloc()` and `free()`:
	- no syscall overhead, no mutex taken by allocator
	- memory cannot leak

1. Better TLB efficiency:
	- address reuse: "allocations" (block reservations) are contiguous
	- no TLB invalidate on `free()`

### Pro: never accesses underlying memory

The memory pointed to by the buffer structure is **never** dereferenced or accessed.

This is an advantage when performing zero-copy I/O into and out of the buffer:
	- no page faults are taken
	- no data prefetch is triggered into cache

**TODO:**link to `nmem`

This makes it possible to point a buffer to a region already containing data,
	such as a memory-mapped file, and then using the buffer to synchronize
	access by multiple threads to successive blocks of the file.

### Pro: portable

1. Uses C11 Atomics
1. Does not rely on platform-selective tricks
	such as combining multiple variables into a single atomic CAS, etc

### Con: slower than an exchange-only queue of pointers

If for some reason you must already allocate memory and **only** want to
	synchronize a **pointer** between threads,
	nothing beats looping through an array of queues and
	atomically swapping pointers until you find `(!NULL)`.

See presentations by *Fedor G. Pikus* and others.

### Con: block-size and block-count constraints

`blk_size` and `blk_count` must both be a power of 2
	and are fixed after buffer initialization.

In the case that object sizes are wildly variable, `blk_size` may end up being
	very large so as to accomodate a small minority of large objects.

This is undesirable in some scenarios; also please see the preceding note on
	pointer queues.

### Con: slower than RCU for mostly-read scenarios

This library is for synchronizing writer and reader access to a
	continuous stream of data.

RCU solves an entirely different problem-set.
Please refer to the work of *Paul E. McKenney* as well as
	[memory-barriers.txt](https://github.com/torvalds/linux/blob/master/Documentation/memory-barriers.txt)
	in the Linux kernel documentation.

### Con: impasse possible through `_release_multi()` mismanagement

If a thread holding a reservation one side of the buffer never calls
	`_release_multi()` on that reservation (e.g.: is killed),
	any future reservation obtained on that side of the buffer
	*will never release*.

This is not necessarily a livelock situation:
	threads attempting to release will not spin forever;
	calls to `_release_multi()` will simply continue to fail.

The simplest workaround is to call `_release_multi()` for that reservation
	from another thread.
