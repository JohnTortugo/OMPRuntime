#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <sched.h>
#include <string.h>

#include "kmp.h"
#include "mtsp.h"
#include "scheduler.h"
#include "task_graph.h"
#include "ThreadedQueue.h"
#include "fe_interface.h"

bool 				volatile 	__mtsp_initialized 	= false;
pthread_t 						__mtsp_RuntimeThread;
bool 				volatile 	__mtsp_Single 		= false;



/// Initialization of locks
unsigned char volatile __mtsp_lock_initialized 	 = 0;
unsigned char volatile __mtsp_lock_newTasksQueue = 0;

/// Variables/constants related to the the taskMetadata buffer
bool volatile __mtsp_taskMetadataStatus[MAX_TASKMETADATA_SLOTS];
char __mtsp_taskMetadataBuffer[MAX_TASKMETADATA_SLOTS][TASK_METADATA_MAX_SIZE];



//===-------- VTune/libittnotify related stuff ----------===//
__itt_domain* 		volatile __itt_mtsp_domain = nullptr;
__itt_string_handle* volatile __itt_CT_Fork_Call = nullptr;
__itt_string_handle* volatile __itt_Run_Queue_Dequeue = nullptr;
__itt_string_handle* volatile __itt_Run_Queue_Enqueue = nullptr;
__itt_string_handle* volatile __itt_Submission_Queue_Dequeue = nullptr;
__itt_string_handle* volatile __itt_Submission_Queue_Enqueue = nullptr;
__itt_string_handle* volatile __itt_Submission_Queue_Copy = nullptr;
__itt_string_handle* volatile __itt_Submission_Queue_Add = nullptr;
__itt_string_handle* volatile __itt_Retirement_Queue_Dequeue = nullptr;
__itt_string_handle* volatile __itt_Retirement_Queue_Enqueue = nullptr;
__itt_string_handle* volatile __itt_TaskGraph_Add = nullptr;
__itt_string_handle* volatile __itt_TaskGraph_Del = nullptr;
__itt_string_handle* volatile __itt_Checking_Dependences = nullptr;
__itt_string_handle* volatile __itt_Releasing_Dependences = nullptr;
__itt_string_handle* volatile __itt_CT_Barrier_Wait = nullptr;
__itt_string_handle* volatile __itt_CT_Task_Alloc = nullptr;
__itt_string_handle* volatile __itt_CT_Task_With_Deps = nullptr;
__itt_string_handle* volatile __itt_WT_Barrier = nullptr;
__itt_string_handle* volatile __itt_WT_Serving_Steal = nullptr;
__itt_string_handle* volatile __itt_WT_Wait_For_Work = nullptr;
__itt_string_handle* volatile __itt_Task_In_Execution = nullptr;
__itt_string_handle* volatile __itt_Task_Stealing = nullptr;



extern void steal_from_single_run_queue(bool just_a_bit);
extern void steal_from_multiple_run_queue(bool just_a_bit);

int stick_this_thread_to_core(int core_id) {
//	int num_cores = sysconf(_SC_NPROCESSORS_ONLN);
//
//	printf("Going to core %d of %d\n", core_id, num_cores);
//
//	if (core_id >= num_cores)
//		return -1;
//
//	cpu_set_t cpuset;
//	CPU_ZERO(&cpuset);
//	CPU_SET(core_id, &cpuset);
//
//	pthread_t current_thread = pthread_self();
//
//	pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset);
//
//	printf("Btw, I am executing on core %d\n", sched_getcpu());

	return 0;
}

void __mtsp_initialize() {
	__itt_mtsp_domain = __itt_domain_create("MTSP");
	__itt_CT_Fork_Call = __itt_string_handle_create("CT_Fork_Call");
	__itt_Run_Queue_Dequeue = __itt_string_handle_create("Run_Queue_Dequeue");
	__itt_Run_Queue_Enqueue = __itt_string_handle_create("Run_Queue_Enqueue");
	__itt_Submission_Queue_Dequeue = __itt_string_handle_create("Submission_Queue_Dequeue");
	__itt_Submission_Queue_Enqueue = __itt_string_handle_create("Submission_Queue_Enqueue");
	__itt_Submission_Queue_Copy = __itt_string_handle_create("Submission_Queue_Copy");
	__itt_Submission_Queue_Add = __itt_string_handle_create("Submission_Queue_Add");
	__itt_Retirement_Queue_Dequeue = __itt_string_handle_create("Retirement_Queue_Dequeue");
	__itt_Retirement_Queue_Enqueue = __itt_string_handle_create("Retirement_Queue_Enqueue");
	__itt_TaskGraph_Add = __itt_string_handle_create("TaskGraph_Add");
	__itt_TaskGraph_Del = __itt_string_handle_create("TaskGraph_Del");
	__itt_Checking_Dependences = __itt_string_handle_create("Checking_Dependences");
	__itt_Releasing_Dependences = __itt_string_handle_create("Releasing_Dependences");
	__itt_CT_Barrier_Wait = __itt_string_handle_create("CT_Barrier_Wait");
	__itt_CT_Task_Alloc = __itt_string_handle_create("CT_Task_Alloc");
	__itt_CT_Task_With_Deps = __itt_string_handle_create("CT_Task_With_Deps");
	__itt_WT_Barrier = __itt_string_handle_create("WT_Barrier");
	__itt_WT_Serving_Steal = __itt_string_handle_create("WT_Serving_Steal");
	__itt_WT_Wait_For_Work = __itt_string_handle_create("WT_Wait_For_Work");
	__itt_Task_In_Execution = __itt_string_handle_create("Task_In_Execution");
	__itt_Task_Stealing = __itt_string_handle_create("Task_Stealing");


	//===-------- This slot is free for use by any thread ----------===//
	for (int i=0; i<MAX_TASKMETADATA_SLOTS; i++) {
		__mtsp_taskMetadataStatus[i] = false;
	}

	/// This the original main thread to core-0
	stick_this_thread_to_core(__MTSP_MAIN_THREAD_CORE__);

	//===-------- Initialize the task graph manager ----------===//
	__mtsp_initializeTaskGraph();

	pthread_create(&__mtsp_RuntimeThread, NULL, __mtsp_RuntimeThreadCode, NULL);
}

void __mtsp_addNewTask(kmp_task* newTask, kmp_uint32 ndeps, kmp_depend_info* depList) {
	__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_Submission_Queue_Enqueue);

#ifdef MTSP_WORKSTEALING_CT
	/// The CT is trying to submit work but the queue is full. The CT will then
	/// spend some time executing tasks
	if (submissionQueue.cur_load() > submissionQueue.cont_load()) {
		steal_from_single_run_queue(true);
	}
#endif

	/// Increment the number of tasks in the system currently
	ATOMIC_ADD(&__mtsp_inFlightTasks, (kmp_int32)1);

	/// TODO: Can we improve this?
	{
		__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_Submission_Queue_Copy);

		newTask->metadata->dep_list = (kmp_depend_info*) malloc(sizeof(kmp_depend_info) * ndeps);
		for (kmp_uint32 i=0; i<ndeps; i++)
			newTask->metadata->dep_list[i] = depList[i];

		newTask->metadata->ndeps = ndeps;

		__itt_task_end(__itt_mtsp_domain);
	}

	__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_Submission_Queue_Add);
	submissionQueue.enq(newTask);
	__itt_task_end(__itt_mtsp_domain);

	__itt_task_end(__itt_mtsp_domain);
}

void __mtsp_RuntimeWorkSteal() {
	kmp_task* taskToExecute = nullptr;

	__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_Task_Stealing);

	while (RunQueue.cur_load() > RunQueue.cont_load()) {
		if (RunQueue.try_deq(&taskToExecute)) {
			/// Start execution of the task
			 __itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_Task_In_Execution);
			(*(taskToExecute->routine))(0, taskToExecute);
			__itt_task_end(__itt_mtsp_domain);

			tasksExecutedByRT++;

			/// Inform that this task has finished execution
			__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_Retirement_Queue_Enqueue);
			RetirementQueue.enq(taskToExecute);
			__itt_task_end(__itt_mtsp_domain);
		}
	}

	__itt_task_end(__itt_mtsp_domain);
}

void* __mtsp_RuntimeThreadCode(void* params) {
	//===-------- Initialize VTune/libittnotify related stuff ----------===//
	__itt_thread_set_name("MTSPRuntime");

	stick_this_thread_to_core(__MTSP_RUNTIME_THREAD_CORE__);
	kmp_task* task = nullptr;
	kmp_uint64 iterations = 0;
	kmp_uint64 BatchSize = (64 - 1);	// should be 2^N - 1

	while (true) {
		/// Check if the execution of a task has been completed
		if (RetirementQueue.try_deq(&task))
			removeFromTaskGraph(task);

		/// Check if there is any request for new thread creation
		if (freeSlots[0] > 0 && submissionQueue.try_deq(&task))
			addToTaskGraph(task);

		iterations++;
		if ((iterations & BatchSize) == 0) {
			submissionQueue.fsh();
#ifdef MTSP_WORKSTEALING_RT
			if (RunQueue.cur_load() > __mtsp_numWorkerThreads) {		// may be we should consider the CT also
				__mtsp_RuntimeWorkSteal();
			}
#endif
		}
	}

	return 0;
}





