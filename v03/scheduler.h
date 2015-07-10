#ifndef __MTSP_SCHEDULER_HEADER
	#define __MTSP_SCHEDULER_HEADER 1

	#include "kmp.h"
	#include "ittnotify.h"
	#include <pthread.h>

	/// The bool var is used to set a flag indicating that worker threads must "barrier" synchronize
	/// The counter is used to count how many threads have already reached to the barrier
	extern kmp_uint32	volatile	__mtsp_threadWaitCounter;
	extern bool			volatile	__mtsp_threadWait;

	/// The ***total*** number of tasks anywhere in the system
	extern kmp_int32	volatile	__mtsp_inFlightTasks;

	/// Used to register the number of active worker threads in the backend
	extern kmp_uint32	volatile	__mtsp_numWorkerThreads;

	/// Pointer to the list of worker pthreads
	extern pthread_t* 	volatile	workerThreads;

	/// Pointer to the list of worker threads IDs (may not start at 0)
	extern kmp_uint32* 	volatile	workerThreadsIds;

	/// This is a matrix. Each line represents a submission queue of one worker thread
	extern kmp_uint32** volatile 	workerThreadsRunQueue;

	void __mtsp_initScheduler();

#endif
