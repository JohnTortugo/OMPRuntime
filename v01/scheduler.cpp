#include "kmp.h"
#include "mtsp.h"
#include "scheduler.h"
#include "task_graph.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

pthread_t* 		volatile workerThreads 				= nullptr;
kmp_uint32* 	volatile workerThreadsIds			= nullptr;
kmp_uint32		volatile __mtsp_threadWaitCounter	= 0;
kmp_int32		volatile __mtsp_inFlightTasks		= 0;
bool			volatile __mtsp_threadWait			= false;
kmp_uint32		volatile __mtsp_numWorkerThreads	= 0;


void* workerThreadCode(void* params) {
	kmp_task* taskToExecute = nullptr;

	/// Currently the ID of the thread is also the ID of its target core
	kmp_uint32* targetCore  = (kmp_uint32*) params;

	stick_this_thread_to_core(*targetCore);

	while (true) {
		__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_WT_Wait_For_Work);
		bool gotLocked = TRY_ACQUIRE(&lock_readySlots);
		__itt_task_end(__itt_mtsp_domain);

		if (gotLocked) {
			__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_Run_Queue_Dequeue);
			if (readySlots[0] > 0) {
				kmp_uint16 taskId 	= readySlots[ readySlots[0] ];
				taskToExecute 		= (kmp_task*) tasks[ taskId ];
				readySlots[0]--;
				RELEASE(&lock_readySlots);
				 __itt_task_end(__itt_mtsp_domain);

				/// Start execution of the task
				 __itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_Task_In_Execution);
				(*(taskToExecute->routine))(0, taskToExecute);
				 __itt_task_end(__itt_mtsp_domain);

				/// Inform that this task has finished execution
				 __itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_Retirement_Queue_Enqueue);
				ACQUIRE(&lock_finishedSlots);
				finishedSlots[0]++;
				finishedSlots[finishedSlots[0]] = taskId;
				RELEASE(&lock_finishedSlots);
				__itt_task_end(__itt_mtsp_domain);
			}
			else {
				RELEASE(&lock_readySlots);
				 __itt_task_end(__itt_mtsp_domain);

				__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_WT_Wait_For_Work);

				/// has a barrier been activated?
				if (__mtsp_threadWait == true) {
//					kmp_uint32 inFlight = ATOMIC_ADD(&__mtsp_inFlightTasks, 0);

					if (__mtsp_inFlightTasks == 0) {
						__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_WT_Barrier);

						ATOMIC_ADD(&__mtsp_threadWaitCounter, 1);

						/// wait until the barrier is released
						while (__mtsp_threadWait);

						/// Says that the current thread have visualized the previous update to threadWait
						ATOMIC_SUB(&__mtsp_threadWaitCounter, 1);

						__itt_task_end(__itt_mtsp_domain);
					}
				}

				__itt_task_end(__itt_mtsp_domain);
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
	__mtsp_numWorkerThreads = __mtsp_numWorkerThreads - 2;

	/// Allocate the requested number of threads
	workerThreads 		= (pthread_t *) malloc(sizeof(pthread_t) * __mtsp_numWorkerThreads);
	workerThreadsIds 	= (kmp_uint32 *) malloc(sizeof(kmp_uint32) * __mtsp_numWorkerThreads);

	/// create the requested number of threads
	for (unsigned int i=0; i<__mtsp_numWorkerThreads; i++) {
		workerThreadsIds[i] = __MTSP_WORKER_THREAD_BASE_CORE__ + i;
		pthread_create(&workerThreads[i], NULL, workerThreadCode, (void*)&workerThreadsIds[i]);
	}
}
