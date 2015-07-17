#ifndef __MTSP_SCHEDULER_HEADER
	#define __MTSP_SCHEDULER_HEADER 1

	#include "kmp.h"
	#include "ittnotify.h"
	#include "ThreadedQueue.h"
	#include <pthread.h>

	/// Represents the maximum number of tasks in the "new tasks queue" in the front-end
	#define RUN_QUEUES_SIZE				64
	#define RUN_QUEUE_SIZE				64
	#define RUN_QUEUES_BATCH_SIZE		 4
	#define RUN_QUEUES_CF			     5
	#define RUN_QUEUE_CF			    50


	#define RETIREMENT_QUEUE_SIZE		64

	/// The bool var is used to set a flag indicating that worker threads must "barrier" synchronize
	/// The counter is used to count how many threads have already reached to the barrier
	extern kmp_uint32	volatile	__mtsp_threadWaitCounter;
	extern bool			volatile	__mtsp_threadWait;

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
	extern SPSCQueue<kmp_task*, RUN_QUEUES_SIZE, RUN_QUEUES_BATCH_SIZE, RUN_QUEUES_CF>* RunQueues;
	extern SimpleQueue<kmp_task*, RUN_QUEUE_SIZE, RUN_QUEUE_CF> RunQueuea;

	/// Each core also has a queue for work stealing. The idea is that the victim will
	/// add work on the StealQueue of the steal thread. The StealStatus is used to keep
	/// track of <is_any_steal_request, from_who>.
	extern SPSCQueue<kmp_task*, RUN_QUEUES_SIZE, RUN_QUEUES_BATCH_SIZE>* StealQueues;
	extern std::pair<bool, kmp_int16> volatile * StealStatus;

	extern SPSCQueue<kmp_task*, RUN_QUEUES_SIZE,RUN_QUEUES_BATCH_SIZE>* RetirementQueues;
	extern SimpleQueue<kmp_task*, RETIREMENT_QUEUE_SIZE> RetirementQueue;

	void __mtsp_initScheduler();

#endif
