#ifndef TIME_UTIL_H_
#define TIME_UTIL_H_

#include <time.h>

/* Our clock, in milliseconds, off CLOCK_REALTIME as the assignment requires.
 * Note bf.h's HELLO_TIMEOUT/ROOT_TIMEOUT are in seconds, so multiply by 1000. */
static inline double now_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	return (double) ts.tv_sec * 1000.0 + (double) ts.tv_nsec / 1e6;
}

#endif  // TIME_UTIL_H_
