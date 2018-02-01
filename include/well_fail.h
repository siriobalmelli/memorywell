#ifndef well_fail_h_
#define well_fail_h_

/*	well_fail.h

Failure strategies for well libraries.
AKA: what to do when reserve/release doesn't succeed?

TODO:
	- yield to a specific thread (eventing) instead of general yield()?
*/

#include <stddef.h> /* size_t */
__thread size_t wait_count = 0;


/* Warning: unsafe for high thread counts! */
#if (FAIL_METHOD == WELL_FAIL_SPIN || FAIL_METHOD == COUNT)
	#define FAIL_DO() { wait_count++; }


#elif (FAIL_METHOD == WELL_FAIL_YIELD) /* OS X scheduler seems to dislike yield() */
	#define FAIL_DO() { wait_count++; sched_yield(); }


/* Warning: this is horrifyingly slow on OS X */
#elif (FAIL_METHOD == WELL_FAIL_SLEEP)
	#include <time.h>
	#define FAIL_DO() { wait_count++; usleep(1); }


#elif (FAIL_METHOD == WELL_FAIL_SIGNAL)
#error "signal not implemented"


#elif (FAIL_METHOD == WELL_FAIL_BOUNDED)
	/* spin only 8 iterations, then yield() */
	#define FAIL_DO() if (!(++wait_count & 0x7)) { sched_yield(); }


#else
#error "fail method unknown"
#endif


#endif /* well_fail_h_ */
