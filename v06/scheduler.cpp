#include "kmp.h"
#include "mtsp.h"
#include "scheduler.h"
#include "task_graph.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

pthread_t* 		volatile workerThreads 				= nullptr;
kmp_uint32* 	volatile workerThreadsIds			= nullptr;

SPSCQueue<kmp_task*, RUN_QUEUES_SIZE,RUN_QUEUES_BATCH_SIZE>* StealQueues;
std::pair<bool, kmp_int16> volatile * StealRequest;
bool volatile * WaitingStealAnswer;

SPSCQueue<kmp_task*, RUN_QUEUES_SIZE, RUN_QUEUES_BATCH_SIZE, RUN_QUEUES_CF>* RunQueues;
SimpleQueue<kmp_task*, RUN_QUEUE_SIZE, RUN_QUEUE_CF> RunQueue;

SPSCQueue<kmp_task*, RUN_QUEUES_SIZE, RUN_QUEUES_BATCH_SIZE>* RetirementQueues;
SimpleQueue<kmp_task*, RETIREMENT_QUEUE_SIZE> RetirementQueue;


kmp_uint32		volatile __mtsp_threadWaitCounter	= 0;
kmp_int32		volatile __mtsp_inFlightTasks		= 0;
bool			volatile __mtsp_threadWait			= false;

kmp_uint32		volatile __mtsp_numThreads			= 0;
kmp_uint32		volatile __mtsp_numWorkerThreads	= 0;

/// Remove X% of the tasks from the victim queue and put them on the stealer "StealQueue".
void serviceSteal(kmp_uint16 victim, kmp_uint16 stealer) {
	/// Check if the current queue has the minimum stealable
	if ( RunQueues[victim].cur_load() > RunQueues[victim].cont_load() ) {
		int curLoad = RunQueues[victim].cur_load();
		int stlNumb = curLoad * 0.10;

		for (int i=0; i<stlNumb; i++) {
			StealQueues[stealer].enq( RunQueues[victim].deq() );
		}
		StealQueues[stealer].fsh();

		//printf("I [%d] SERVICED [%d].\n", victim, stealer);
	}
//	else {
//		if (stealer == 2)
//			printf("I [%d] will not service [%d]. My load = %d, cont load = %d\n", victim, stealer, RunQueues[victim].cur_load(), RunQueues[victim].cont_load());
//	}
}

void* workerThreadCode(void* params) {
	kmp_task* taskToExecute = nullptr;

	/// Currently the ID of the thread is also the ID of its target core
	kmp_uint32* tasksIdent  = (kmp_uint32*) params;
	kmp_uint16 myId 		= *tasksIdent;
	char taskName[100];

	/// Init random number generator. We use this on the work stealing part.
	srandom( time(NULL) );

	/// Stick this thread to execute on the "Core X"
	stick_this_thread_to_core(*tasksIdent);

	/// The thread that initialize the runtime is the Control Thread
	sprintf(taskName, "WorkerThread-%02d", myId);
	__itt_thread_set_name(taskName);

	/// Counter for the number of threads
	kmp_uint64 tasksExecuted = 0;
	kmp_uint64 iterations = 0;

	while (true) {
#ifdef MTSP_MULTIPLE_RUN_QUEUES
		if (RunQueues[myId].try_deq(&taskToExecute) || StealQueues[myId].try_deq(&taskToExecute)) {
#else
		if (RunQueue.try_deq(&taskToExecute)) {
#endif

#ifdef MTSP_WORK_DISTRIBUTION_FT
			finishedIDS[0]++;
			finishedIDS[finishedIDS[0]] = myId;
#endif

			/// Start execution of the task
			 __itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_Task_In_Execution);
			(*(taskToExecute->routine))(0, taskToExecute);
			__itt_task_end(__itt_mtsp_domain);

			tasksExecuted++;

			/// Inform that this task has finished execution
			__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_Finished_Tasks_Queue_Enqueue);
#ifdef MTSP_MULTIPLE_RETIRE_QUEUES
			RetirementQueues[myId].enq(taskToExecute);
#else
			RetirementQueue.enq(taskToExecute);
#endif
			__itt_task_end(__itt_mtsp_domain);
		}
		else {
#if defined(MTSP_WORKSTEALING_WT) && defined(MTSP_MULTIPLE_RUN_QUEUES)
			/// I only ask if I am not already waiting for an answer
			if (WaitingStealAnswer[myId] == false) {
				/// In the MULTIPLE_RUN_QUEUE mode each worker thread has its own run queue.
				/// Since we could not dequeue a task our queue is empty. We are going to
				/// try to steal some tasks >D
				kmp_uint16 victim = random() % __mtsp_numWorkerThreads;
				CAS(&StealRequest[victim].second, -1, myId);

				WaitingStealAnswer[myId] = true;
			}
#endif
			__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_Worker_Thread_Wait_For_Work);
			/// has a barrier been activated?
			if (__mtsp_threadWait == true) {
				if (__mtsp_inFlightTasks == 0) {
					__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_Worker_Thread_Barrier);

					ATOMIC_ADD(&__mtsp_threadWaitCounter, 1);

					/// wait until the barrier is released
					while (__mtsp_threadWait);

#ifdef MTSP_DUMP_STATS
					printf("%llu tasks were executed by thread %d.\n", tasksExecuted, myId);
#endif

					/// Says that the current thread have visualized the previous update to threadWait
					ATOMIC_SUB(&__mtsp_threadWaitCounter, 1);

					__itt_task_end(__itt_mtsp_domain);
				}
			}
			__itt_task_end(__itt_mtsp_domain);
		}

#if defined(MTSP_MULTIPLE_RUN_QUEUES) && (defined(MTSP_WORKSTEALING_CT) || defined(MTSP_WORKSTEALING_WT))
		__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_WT_Serving_Steal);
		/// Check if there is an steal request pending...
		if (StealRequest[myId].second >= 0) {
			serviceSteal(myId, StealRequest[myId].second);

			/// Tell the "stealer" that the request was considered
			WaitingStealAnswer[StealRequest[myId].second] = false;

			/// Reset pending steal to false
			CAS(&StealRequest[myId].second, StealRequest[myId].second, -1);
		}
		__itt_task_end(__itt_mtsp_domain);
#endif

#ifdef MTSP_MULTIPLE_RETIRE_QUEUES
		iterations++;

		if ((iterations & 0xFF) == 0) {
			RetirementQueues[myId].fsh();
		}
#endif
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

	/// We create as many Run-Queues as threads we have. This is in preparation for
	///  task stealing from the control thread and runtime thread in the future
	RunQueues				= new SPSCQueue<kmp_task*, RUN_QUEUES_SIZE, RUN_QUEUES_BATCH_SIZE, RUN_QUEUES_CF>[__mtsp_numThreads];

	RetirementQueues		= new SPSCQueue<kmp_task*, RUN_QUEUES_SIZE, RUN_QUEUES_BATCH_SIZE>[__mtsp_numThreads];

	StealQueues				= new SPSCQueue<kmp_task*, RUN_QUEUES_SIZE, RUN_QUEUES_BATCH_SIZE>[__mtsp_numThreads];
	StealRequest			= new std::pair<bool, kmp_int16>[__mtsp_numThreads];
	WaitingStealAnswer		= new bool[__mtsp_numThreads];

	/// create the requested number of worker threads
	for (unsigned int i=0; i<__mtsp_numWorkerThreads; i++) {
		/// What is the ID/Core of the worker thread
		workerThreadsIds[i] = i;

		/// Initialize steal status to: <not_ready, not_requested>
		StealRequest[i].first = false;
		StealRequest[i].second = -1;
		WaitingStealAnswer[i] = false;

		/// Create the worker thread
		pthread_create(&workerThreads[i], NULL, workerThreadCode, (void*)&workerThreadsIds[i]);
	}

#ifdef MTSP_WORK_DISTRIBUTION_FT
	/// This is only necessary when we are using a "per finished token" work load distribution
	finishedIDS[0] = MAX_TASKS;
	for (int i=0; i<MAX_TASKS; i++) {
		finishedIDS[i+1] = i % __mtsp_numWorkerThreads;
	}
#endif
}
