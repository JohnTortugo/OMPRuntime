#include "kmp.h"
#include "mtsp.h"
#include "scheduler.h"
#include "task_graph.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <iostream>

pthread_t* 		volatile workerThreads 				= nullptr;
kmp_uint32* 	volatile workerThreadsIds			= nullptr;

SimpleQueue<kmp_task*, RUN_QUEUE_SIZE, RUN_QUEUE_CF> RunQueue;
SimpleQueue<kmp_task*, RETIREMENT_QUEUE_SIZE> RetirementQueue;


kmp_uint32		volatile __mtsp_threadWaitCounter	= 0;
kmp_int32		volatile __mtsp_inFlightTasks		= 0;
bool			volatile __mtsp_threadWait			= false;
bool 			volatile __mtsp__BlockWorkers	    = false;

kmp_uint32		volatile __mtsp_numThreads			= 0;
kmp_uint32		volatile __mtsp_numWorkerThreads	= 0;

void* workerThreadCode(void* params) {
	kmp_task* taskToExecute = nullptr;

	/// Currently the ID of the thread is also the ID of its target core
	kmp_uint32* tasksIdent  = (kmp_uint32*) params;
	kmp_uint16 myId 		= *tasksIdent;

	while (true) {
	    /// This is just used in debug mode, for instance, when we want
	    /// to dump the taskgraph to a DOT file.
        while (__mtsp__BlockWorkers);

		if (RunQueue.try_deq(&taskToExecute)) {
			/// Start execution of the task
			(*(taskToExecute->routine))(0, taskToExecute);

            /// Retire task
			RetirementQueue.enq(taskToExecute);
		}
		else {
			/// has a barrier been activated?
			if (__mtsp_threadWait == true) {
				if (__mtsp_inFlightTasks == 0) {

					ATOMIC_ADD(&__mtsp_threadWaitCounter, 1);

					/// wait until the barrier is released
					while (__mtsp_threadWait);

					/// Says that the current thread have visualized the previous update to threadWait
					ATOMIC_SUB(&__mtsp_threadWaitCounter, 1);
				}
			}
		}
	}

	return nullptr;
}

void __mtsp_initScheduler() {
	__mtsp_numWorkerThreads = sysconf(_SC_NPROCESSORS_ONLN);

	/// The environment variable overrides other confs.
	if (getenv("OMP_NUM_THREADS") != NULL)
		__mtsp_numWorkerThreads = atoi(getenv("OMP_NUM_THREADS"));

	/// reduce the number of threads used by the runtime (i.e., subtract the runtime thread
	/// and the initial thread of the program).
	__mtsp_numThreads		= __mtsp_numWorkerThreads;
	__mtsp_numWorkerThreads = __mtsp_numWorkerThreads - 2;

	/// Allocate the requested number of threads
	workerThreads 			= (pthread_t  *) malloc(sizeof(pthread_t)   * __mtsp_numWorkerThreads);
	workerThreadsIds 		= (kmp_uint32 *) malloc(sizeof(kmp_uint32)  * __mtsp_numWorkerThreads);


	pthread_create(&__mtsp_RuntimeThread, NULL, __mtsp_RuntimeThreadCode, NULL);

	/// create the requested number of worker threads
	for (unsigned int i=0; i<__mtsp_numWorkerThreads; i++) {
		/// What is the ID/Core of the worker thread
		workerThreadsIds[i] = i;

		/// Create the worker thread
		pthread_create(&workerThreads[i], NULL, workerThreadCode, (void*)&workerThreadsIds[i]);
	}
}
