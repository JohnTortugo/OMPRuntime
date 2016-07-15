/**
 * @file 	scheduler.h
 * @brief 	This file contains functions and variables related to the Scheduler and worker threads.
 *
 */

#include "kmp.h"
#include "mtsp.h"
#include "scheduler.h"
#include "task_graph.h"
#include "fe_interface.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <mutex>

pthread_t* 		volatile workerThreads 				= nullptr;
kmp_uint32* 	volatile workerThreadsIds			= nullptr;
kmp_int32* 		volatile idOfCurrentTask			= nullptr;

SimpleQueue<kmp_task*, RUN_QUEUE_SIZE, RUN_QUEUE_CF> RunQueue;
SimpleQueue<kmp_task*, RETIREMENT_QUEUE_SIZE> RetirementQueue;


kmp_uint32		volatile __mtsp_threadWaitCounter	= 0;
kmp_int32		volatile __mtsp_inFlightTasks		= 0;
kmp_int32		volatile __ControlThreadDirectChild = 0;
bool			volatile __mtsp_threadWait			= false;
bool 			volatile __mtsp_activate_workers	= false;

kmp_uint32		volatile __mtsp_numThreads			= 0;
kmp_uint32		volatile __mtsp_numWorkerThreads	= 0;

bool 			volatile __run_queue_lock			= UNLOCKED;
bool 			volatile __ret_queue_lock			= UNLOCKED;

std::mutex runq_deq_mtx;

bool __mtsp_dequeue_from_run_queue(unsigned long long int* payload)
{
	/// By default, this function tries to dequeue packets
	/// from the first run queue.

    runq_deq_mtx.lock();

	if (tga_runq_can_deq(0))
	{
        *payload = tga_runq_raw_deq(0);
        runq_deq_mtx.unlock();

		return true;
	}
	else
    {
        runq_deq_mtx.unlock();
		return false;
    }
}

void* WorkerThreadCode(void* params) {
	kmp_task* taskToExecute = nullptr;

	// Currently the ID of the thread is also the ID of its target core
	kmp_uint32* tasksIdent  = (kmp_uint32*) params;
	kmp_uint16 myId 		= *tasksIdent;
	kmp_int16 prevTaskId	= -1;
	char taskName[100];

	threadId = myId;

	// The thread that initialize the runtime is the Control Thread
	sprintf(taskName, "WorkerThread-%02d", myId);
	__itt_thread_set_name(taskName);

	while (true) {
		if (RunQueue.try_deq(&taskToExecute)) {
			 __itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_Task_In_Execution);

			prevTaskId = idOfCurrentTask[myId];
			idOfCurrentTask[myId] = taskToExecute->metadata->taskgraph_slot_id;

			// Start execution of the task
			(*(taskToExecute->routine))(0, taskToExecute);

			idOfCurrentTask[myId] = prevTaskId;

			__itt_task_end(__itt_mtsp_domain);

			// Inform that this task has finished execution
			__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_Retirement_Queue_Enqueue);
			RetirementQueue.enq(taskToExecute);
			__itt_task_end(__itt_mtsp_domain);
		}
	}

	return nullptr;
}


void __mtsp_initScheduler() {
	__mtsp_numThreads = sysconf(_SC_NPROCESSORS_ONLN);

	// The environment variable overrides other confs.
	if (getenv("OMP_NUM_THREADS") != NULL)
		__mtsp_numThreads = atoi(getenv("OMP_NUM_THREADS"));

	if (__mtsp_numThreads < 2) {
		fprintf(stderr, "Total number of threads in the system should be at least two.");
		exit(1);
	}

	// The (-1) represents the runtime thread that never become a worker
	__mtsp_numWorkerThreads = __mtsp_numThreads - 1;

	// Allocate the requested number of threads
	workerThreads 	 = (pthread_t  *) malloc(sizeof(pthread_t)   * __mtsp_numWorkerThreads);
	workerThreadsIds = (kmp_uint32 *) malloc(sizeof(kmp_uint32)  * __mtsp_numWorkerThreads);
	idOfCurrentTask  = (kmp_int32 *) malloc(sizeof(kmp_int32)  * __mtsp_numThreads);

	// When not in bridge mode we also consider the runtime thread
	pthread_create(&__mtsp_RuntimeThread, NULL, __mtsp_RuntimeThreadCode, NULL);

	// create the requested number of worker threads
	for (unsigned int i=1; i<__mtsp_numWorkerThreads; i++) {
		// What is the ID/Core of the worker thread
		workerThreadsIds[i] = i;

		// Create the worker thread
		pthread_create(&workerThreads[i], NULL, WorkerThreadCode, (void*)&workerThreadsIds[i]);
	}
}
