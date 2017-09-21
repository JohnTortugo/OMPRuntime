#ifndef __MTSP_SCHEDULER_HEADER
	#define __MTSP_SCHEDULER_HEADER 1

	#include "mtsp.h"
	#include "task_graph.h"
	#include "ThreadedQueue.h"

	/// The bool var is used to set a flag indicating that worker threads must "barrier" synchronize
	/// The counter is used to count how many threads have already reached to the barrier
	extern kmp_uint32	volatile	__mtsp_threadWaitCounter;
	extern bool			volatile	__mtsp_threadWait;
	extern bool 		volatile 	__mtsp_activate_workers;

	/// The ***total*** number of tasks anywhere in the system
	extern kmp_int32	volatile	__mtsp_inFlightTasks;

	/// Used to register the number of active worker threads in the backend
	extern kmp_uint32	volatile 	__mtsp_numThreads;
	extern kmp_uint32	volatile	__mtsp_numWorkerThreads;

	/// Pointer to the list of worker pthreads
	extern pthread_t* 	volatile	workerThreads;

	/// Pointer to the list of worker threads IDs (may not start at 0)
	extern kmp_uint32* 	volatile	workerThreadsIds;

	/// This is a matrix. Each line represents a submission/steal queue of one worker thread
	extern SimpleQueue<kmp_task*, RUN_QUEUE_SIZE, RUN_QUEUE_CF> RunQueue;

	extern SimpleQueue<kmp_task*, RETIREMENT_QUEUE_SIZE> RetirementQueue;

	void __mtsp_initScheduler();

#endif
