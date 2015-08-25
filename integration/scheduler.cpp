#include "kmp.h"
#include "fe_interface.h"
#include "scheduler.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

pthread_t* 		volatile workerThreads 				= nullptr;
kmp_uint32* 	volatile workerThreadsIds			= nullptr;

kmp_uint32		volatile __mtsp_threadWaitCounter	= 0;
kmp_int32		volatile __mtsp_inFlightTasks		= 0;
bool			volatile __mtsp_threadWait			= false;

kmp_uint32		volatile __mtsp_numThreads			= 0;
kmp_uint32		volatile __mtsp_numWorkerThreads	= 0;

bool 			volatile __run_queue_lock			= UNLOCKED;
bool 			volatile __ret_queue_lock			= UNLOCKED;



bool __mtsp_dequeue_from_run_queue(unsigned long long int* payload) {
	if (TRY_ACQUIRE(&__run_queue_lock)) {
		if (__mtsp_RunQueueDesc->QHead != __mtsp_RunQueueDesc->QTail) {
			struct SQPacket* pos = (struct SQPacket*) (__mtsp_RunQueueDesc->base_address + __mtsp_RunQueueDesc->QHead);

			*payload = pos->payload;

			if (DEBUG_MODE) printf(ANSI_COLOR_RED "[MTSP - RUNQ] Packet with payload [%llx] coming from index %02ld of runq, address %p\n" ANSI_COLOR_RESET, *payload, __mtsp_RunQueueDesc->QHead, pos);

			__mtsp_RunQueueDesc->QHead = (__mtsp_RunQueueDesc->QHead + sizeof(struct SQPacket)) % __mtsp_RunQueueDesc->QSize;

			RELEASE(&__run_queue_lock);

			return true;
		}
		else {
			RELEASE(&__run_queue_lock);

			return false;
		}
	}

	return false;
}

void __mtsp_enqueue_into_retirement_queue(unsigned long long int taskSlot) {
	ACQUIRE(&__ret_queue_lock);

	while (((__mtsp_RetirementQueueDesc->QTail + (int)sizeof(struct SQPacket)) % __mtsp_RetirementQueueDesc->QSize) == __mtsp_RetirementQueueDesc->QHead);

	struct SQPacket* pos = (struct SQPacket*) (__mtsp_RetirementQueueDesc->base_address + __mtsp_RetirementQueueDesc->QTail);
	pos->payload 		 = taskSlot;

	if (DEBUG_MODE) printf(ANSI_COLOR_RED "[MTSP - RETQ] Packet with payload [%llx] going to index %02ld of retq, address %p\n" ANSI_COLOR_RESET, taskSlot, __mtsp_RetirementQueueDesc->QTail, pos);

	__mtsp_RetirementQueueDesc->QTail = (__mtsp_RetirementQueueDesc->QTail + sizeof(struct SQPacket)) % __mtsp_RetirementQueueDesc->QSize;

	freeSlots.enq(taskSlot);

	RELEASE(&__ret_queue_lock);
}

void* workerThreadCode(void* params) {
	kmp_task* taskToExecute = nullptr;
	unsigned long long int taskSlot = 0;
	kmp_uint64 tasksExecuted = 0;

	/// Currently the ID of the thread is also the ID of its target core
	kmp_uint32* tasksIdent  = (kmp_uint32*) params;
	kmp_uint16 myId 		= *tasksIdent;

	while (true) {
		if ( __mtsp_dequeue_from_run_queue(&taskSlot) ) {
			taskSlot 	  = taskSlot & 0x3FFFFFFFFFFFF;
			taskToExecute = tasks[taskSlot];

			/// Start execution of the task
			if (DEBUG_MODE) printf(ANSI_COLOR_RED "[MTSP       ] Going to execute task from slot %03x which points to %p\n" ANSI_COLOR_RESET, taskSlot, taskToExecute->routine);
			(*(taskToExecute->routine))(0, taskToExecute);

			tasksExecuted++;

			/// Decrement the number of tasks in the system currently
			/// This was not the original place of this
			ATOMIC_SUB(&__mtsp_inFlightTasks, (kmp_int32)1);

			/// Inform that this task has finished execution
			__mtsp_enqueue_into_retirement_queue(taskSlot);
		}
		else {
			/// has a barrier been activated?
			if (__mtsp_threadWait == true) {
				if (__mtsp_inFlightTasks == 0) {
					ATOMIC_ADD(&__mtsp_threadWaitCounter, 1);

					/// wait until the barrier is released
					while (__mtsp_threadWait);

					if (DEBUG_MODE) printf("%llu tasks were executed by thread %d.\n", tasksExecuted, myId);

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

	/// create the requested number of worker threads
	for (unsigned int i=0; i<__mtsp_numWorkerThreads; i++) {
		/// What is the ID/Core of the worker thread
		workerThreadsIds[i] = i;

		/// Create the worker thread
		pthread_create(&workerThreads[i], NULL, workerThreadCode, (void*)&workerThreadsIds[i]);
	}
}