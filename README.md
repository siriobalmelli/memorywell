# nbuf

breakthrough: look at availability, NOT position

Shortlist of advantages:
	- don't need a memory allocator
	- symmetric: can send data both ways (same synchronization overhead as one-way)
	- any block size
	- multiple blocks per reservation (allows efficient "max possible blocks" scenario)
	- any combination of single/multiple producer/consumer
	- fast (split cache lines)

Cons:
	- if **already** allocating memory, slower than an exchange-queue of pointers

## TODO

- give it a name
- figure out best static vs. dynamic link setup (nonlibc also)
- generic nmath functions so 32-bit size_t case is cared for
- speed differential if combining cache lines (avoid false sharing)?
