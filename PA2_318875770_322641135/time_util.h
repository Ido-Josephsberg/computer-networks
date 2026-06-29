#ifndef TIME_UTIL_H_
#define TIME_UTIL_H_

#include <time.h>

/*
 * Single time source for the whole program.
 *
 * INVARIANT: every duration in this project is expressed in MILLISECONDS.
 * The bf.h constants HELLO_TIMEOUT (2) and ROOT_TIMEOUT (6) are in SECONDS and
 * must be multiplied by 1000 at every use site.
 *
 * The clock source is CLOCK_REALTIME (PDF Note 1). All deadline / expTime
 * arithmetic uses the absolute value returned here; the relative-to-start time
 * shown in the output is computed only at print time as (now_ms() - start_ms).
 */
static inline double now_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	return (double) ts.tv_sec * 1000.0 + (double) ts.tv_nsec / 1e6;
}

#endif  // TIME_UTIL_H_
