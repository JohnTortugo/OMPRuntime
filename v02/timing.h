#ifndef __MTSP_TIMING_HEADER
	#define __MTSP_TIMING_HEADER 1
	#include <sys/time.h>
	#include <stdlib.h>

	#if TIMING_ENABLED
		/// This define is used to create a new timer. In fact, the define will create
		/// variables to store the begin/end timer and the wall time
		#define MTSP_CREATE_TIMER(NAME)					struct timeval __beg_time_##NAME;			\
														struct timeval __end_time_##NAME;			\
														double __wtime_##NAME = 0

		/// create a var to represent beg. of time
		/// create a var to represent end. of time
		/// initialize the begining of time with the current time
		/// create a var to represent the accumulated wall time
		#define MTSP_TIME_START(NAME)					extern struct timeval __beg_time_##NAME;	\
														gettimeofday(&__beg_time_##NAME, NULL)

		/// initialize it with the current time
		/// sum the initial time in the wall time
		/// decrement the final time from the wall time
		/// reset the begining of time
		#define MTSP_TIME_ACCUMULATE_AND_RESET(NAME)	do { 																									\
															extern struct timeval __beg_time_##NAME;															\
															extern struct timeval __end_time_##NAME;															\
															extern double __wtime_##NAME;																		\
															gettimeofday(&__end_time_##NAME, NULL);																\
															__wtime_##NAME -= (double)__beg_time_##NAME.tv_sec + (double)__beg_time_##NAME.tv_usec * .000001; 	\
															__wtime_##NAME += (double)__end_time_##NAME.tv_sec + (double)__end_time_##NAME.tv_usec * .000001; 	\
															gettimeofday(&__beg_time_##NAME, NULL);																\
														} while (0)

		/// reset the begining of time and wall time
		#define MTSP_TIME_RESET(NAME)					extern struct timeval __beg_time_##NAME;			\
														extern double wtime_##NAME;							\
														do { gettimeofday(&__beg_time_##NAME, NULL);		\
															__wtime_##NAME = 0;								\
														} while (0)

		/// Just print the value of the timer using the format "Time_Name = 0.000000\n"
		#define	MTSP_TIME_DUMP(NAME)					extern double __wtime_##NAME;									\
														printf("Time_%s = %.6lf (s.us)\n", #NAME, __wtime_##NAME)


	#else

		#define MTSP_CREATE_TIMER(NAME)					double __wtime_##NAME = 0;
		#define MTSP_TIME_START(NAME)					do { } while(0)
		#define MTSP_TIME_ACCUMULATE_AND_RESET(NAME)	do { } while(0)
		#define MTSP_TIME_RESET(NAME)					do { } while(0)
		#define	MTSP_TIME_DUMP(NAME)					do { } while(0)

	#endif

#endif
