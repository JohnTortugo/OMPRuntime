#include "kmp.h"
#include "fe_interface.h"
#include "scheduler.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

pthread_t* 		volatile workerThreads 				= nullptr;
kmp_uint32*	 	volatile workerThreadsIds			= nullptr;

kmp_uint32		volatile __mtsp_threadWaitCounter		= 0;
kmp_int32		volatile __mtsp_inFlightTasks			= 0;
bool			volatile __mtsp_threadWait			= false;

kmp_uint32		volatile __mtsp_numThreads			= 0;
kmp_uint32		volatile __mtsp_numWorkerThreads		= 0;

bool 			volatile __run_queue_lock			= UNLOCKED;
bool 			volatile __ret_queue_lock			= UNLOCKED;



bool __mtsp_dequeue_from_run_queue(unsigned long long int* payload)
{
	/// By default, this function tries to dequeue packets
	/// from the first run queue.

	asm volatile("isb");
	asm volatile("dmb");
	asm volatile("dsb");
	if (tga_runq_can_deq(0))
	{
		asm volatile("isb");
		asm volatile("dmb");
		asm volatile("dsb");
		*payload = tga_runq_raw_deq(0);
		asm volatile("isb");
		asm volatile("dmb");
		asm volatile("dsb");

		return true;
	}
	else
		return false;
}

void __mtsp_enqueue_into_retirement_queue(unsigned long long int taskSlot) {
	asm volatile("isb");
	asm volatile("dmb");
	asm volatile("dsb");
	tga_retq_enq(taskSlot);
	asm volatile("isb");
	asm volatile("dmb");
	asm volatile("dsb");
	freeSlots.enq(taskSlot);
	asm volatile("isb");
	asm volatile("dmb");
	asm volatile("dsb");
}

void* workerThreadCode(void* params) {
	kmp_task* taskToExecute = nullptr;
	unsigned long long int packet = 0;
	unsigned long long int taskSlot = 0;
	kmp_uint64 tasksExecuted = 0;

	/// Currently the ID of the thread is also the ID of its target core
	kmp_uint32* tasksIdent  = (kmp_uint32*) params;
	kmp_uint16 myId 		= *tasksIdent;

	while (true) {
		if ( __mtsp_dequeue_from_run_queue(&packet) ) {
			asm volatile("isb");
			asm volatile("dmb");
			asm volatile("dsb");
			taskSlot 	  = packet & 0x1FF;
			asm volatile("isb");
			asm volatile("dmb");
			asm volatile("dsb");
		
			//printf("[mtsp]: We are now going to get function information for the run-task with id = %d\n", taskSlot);
			asm volatile("isb");
			asm volatile("dmb");
			asm volatile("dsb");
			taskToExecute = tasks[taskSlot];
			asm volatile("isb");
			asm volatile("dmb");
			asm volatile("dsb");

			/// Start execution of the task
#ifdef DBG
			printf(ANSI_COLOR_RED "[MTSP       ] Going to execute task from slot %03x which points to %p\n" ANSI_COLOR_RESET, taskSlot, taskToExecute->routine);
#endif
#ifdef DBG
			printf("[mtsp]: Pointer to the kmp_task structure holding the function to be run: %p\n", taskToExecute);
#endif
#ifdef DBG
			printf("[mtsp]: Pointer of the encapsulated function to be run: %p\n", taskToExecute->routine);
#endif
			asm volatile("isb");
			asm volatile("dmb");
			asm volatile("dsb");
			(*(taskToExecute->routine))(0, taskToExecute);
			asm volatile("isb");
			asm volatile("dmb");
			asm volatile("dsb");

			asm volatile("isb");
			asm volatile("dmb");
			asm volatile("dsb");
			tasksExecuted++;
			asm volatile("isb");
			asm volatile("dmb");
			asm volatile("dsb");

			/// Decrement the number of tasks in the system currently
			/// This was not the original place of this
			asm volatile("isb");
			asm volatile("dmb");
			asm volatile("dsb");
			ATOMIC_SUB(&__mtsp_inFlightTasks, (kmp_int32)1);
			asm volatile("isb");
			asm volatile("dmb");
			asm volatile("dsb");

			/// Inform that this task has finished execution
			asm volatile("isb");
			asm volatile("dmb");
			asm volatile("dsb");
			__mtsp_enqueue_into_retirement_queue(packet);
			asm volatile("isb");
			asm volatile("dmb");
			asm volatile("dsb");
		}
		else {
			/// has a barrier been activated?
			asm volatile("isb");
			asm volatile("dmb");
			asm volatile("dsb");
			if (__mtsp_threadWait == true) {
				if (__mtsp_inFlightTasks == 0) {
				asm volatile("isb");
				asm volatile("dmb");
				asm volatile("dsb");
				ATOMIC_ADD(&__mtsp_threadWaitCounter, 1);
				asm volatile("isb");
				asm volatile("dmb");
				asm volatile("dsb");

					/// wait until the barrier is released
					while (__mtsp_threadWait);

#ifdef DBG
					printf("%llu tasks were executed by thread %d.\n", tasksExecuted, myId);
#endif

					/// Says that the current thread have visualized the previous update to threadWait
					asm volatile("isb");
					asm volatile("dmb");
					asm volatile("dsb");
					ATOMIC_SUB(&__mtsp_threadWaitCounter, 1);
					asm volatile("isb");
					asm volatile("dmb");
					asm volatile("dsb");
				}
			}
		}
	}

	return nullptr;
}

void __mtsp_initScheduler() {
	asm volatile("isb");
	asm volatile("dmb");
	asm volatile("dsb");
	__mtsp_numWorkerThreads = sysconf(_SC_NPROCESSORS_ONLN);
	asm volatile("isb");
	asm volatile("dmb");
	asm volatile("dsb");

	/// The environment variable overrides other confs.
	if (getenv("OMP_NUM_THREADS") != NULL)
		asm volatile("isb");
		asm volatile("dmb");
		asm volatile("dsb");
		__mtsp_numWorkerThreads = atoi(getenv("OMP_NUM_THREADS"));
		asm volatile("isb");
		asm volatile("dmb");
		asm volatile("dsb");

	/// reduce the number of threads used by the runtime (i.e., subtract the runtime thread
	/// and the initial thread of the program).
	asm volatile("isb");
	asm volatile("dmb");
	asm volatile("dsb");
	__mtsp_numThreads		= __mtsp_numWorkerThreads;
	asm volatile("isb");
	asm volatile("dmb");
	asm volatile("dsb");
	__mtsp_numWorkerThreads = __mtsp_numWorkerThreads - 2;
	asm volatile("isb");
	asm volatile("dmb");
	asm volatile("dsb");

	/// Allocate the requested number of threads
	asm volatile("isb");
	asm volatile("dmb");
	asm volatile("dsb");
	workerThreads 			= (pthread_t  *) malloc(sizeof(pthread_t)   * __mtsp_numWorkerThreads);
	asm volatile("isb");
	asm volatile("dmb");
	asm volatile("dsb");
	workerThreadsIds 		= (kmp_uint32 *) malloc(sizeof(kmp_uint32)  * __mtsp_numWorkerThreads);
	asm volatile("isb");
	asm volatile("dmb");
	asm volatile("dsb");

	/// create the requested number of worker threads
	for (unsigned int i=0; i<__mtsp_numWorkerThreads; i++) {
		/// What is the ID/Core of the worker thread
		asm volatile("isb");
		asm volatile("dmb");
		asm volatile("dsb");
		workerThreadsIds[i] = i;
		asm volatile("isb");
		asm volatile("dmb");
		asm volatile("dsb");

		/// Create the worker thread
		asm volatile("isb");
		asm volatile("dmb");
		asm volatile("dsb");
		pthread_create(&workerThreads[i], NULL, workerThreadCode, (void*)&workerThreadsIds[i]);
		asm volatile("isb");
		asm volatile("dmb");
		asm volatile("dsb");
	}
}
