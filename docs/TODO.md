---
title: TODO
order: 0
---

# TODO

- describe buid system and test options
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
