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

bool 				volatile 	__mtsp_initialized 		= false;
pthread_t 						__mtsp_RuntimeThread	= 0;
bool 				volatile 	__mtsp_Single 			= false;
bool							__mtsp_ColorVectorIdx	= true;

kmp_uint16 						__mtsp_ColorVector[COLOR_VECTOR_SIZE];

kmp_uint16						__mtsp_NodeStatus[MAX_TASKS];
kmp_uint16						__mtsp_NodeColor[MAX_TASKS];

/// Initialization of locks
unsigned char volatile __mtsp_lock_initialized 	 = 0;
unsigned char volatile __mtsp_lock_newTasksQueue = 0;

/// Variables/constants related to the the taskMetadata buffer
bool __mtsp_taskMetadataStatus[MAX_TASKMETADATA_SLOTS];
char __mtsp_taskMetadataBuffer[MAX_TASKMETADATA_SLOTS][TASK_METADATA_MAX_SIZE];



//===-------- VTune/libittnotify related stuff ----------===//
__itt_domain*			volatile __itt_mtsp_domain	= nullptr;
__itt_string_handle* 	volatile __itt_Run_Queue_Dequeue	= nullptr;
__itt_string_handle* 	volatile __itt_Run_Queue_Enqueue	= nullptr;
__itt_string_handle* 	volatile __itt_Submission_Queue_Dequeue	= nullptr;
__itt_string_handle* 	volatile __itt_Submission_Queue_Enqueue	= nullptr;
__itt_string_handle* 	volatile __itt_Submission_Queue_Copy		= nullptr;
__itt_string_handle* 	volatile __itt_Submission_Queue_Add		= nullptr;
__itt_string_handle* 	volatile __itt_Retirement_Queue_Dequeue	= nullptr;
__itt_string_handle* 	volatile __itt_Retirement_Queue_Enqueue	= nullptr;
__itt_string_handle* 	volatile __itt_CT_Barrier_Wait	= nullptr;
__itt_string_handle* 	volatile __itt_WT_Barrier	= nullptr;
__itt_string_handle* 	volatile __itt_Task_In_Execution	= nullptr;
__itt_string_handle* 	volatile __itt_Task_Stealing	= nullptr;
__itt_string_handle* 	volatile __itt_TaskGraph_Add	= nullptr;
__itt_string_handle* 	volatile __itt_TaskGraph_Del	= nullptr;
__itt_string_handle* 	volatile __itt_Checking_Dependences	= nullptr;
__itt_string_handle* 	volatile __itt_Releasing_Dependences	= nullptr;
__itt_string_handle* 	volatile __itt_CT_Task_Alloc	= nullptr;
__itt_string_handle* 	volatile __itt_CT_Task_With_Deps	= nullptr;
__itt_string_handle* 	volatile __itt_WT_Wait_For_Work	= nullptr;


extern void steal_from_single_run_queue(bool just_a_bit);
extern void steal_from_multiple_run_queue(bool just_a_bit);
extern void steal_from_prioritized_run_queues(bool just_a_bit);

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
    __itt_mtsp_domain = __itt_domain_create("MTSP.SchedulerDomain");
	__itt_Run_Queue_Dequeue = __itt_string_handle_create("ReadyQueue_Dequeue");
	__itt_Run_Queue_Enqueue = __itt_string_handle_create("ReadyQueue_Enqueue");
	__itt_Submission_Queue_Dequeue = __itt_string_handle_create("New_Tasks_Queue_Dequeue");
	__itt_Submission_Queue_Enqueue = __itt_string_handle_create("New_Tasks_Queue_Enqueue");
	__itt_Submission_Queue_Add = __itt_string_handle_create("New_Tasks_Queue_Full");
	__itt_Submission_Queue_Copy = __itt_string_handle_create("New_Tasks_Queue_Copy");
	__itt_Retirement_Queue_Dequeue = __itt_string_handle_create("Finished_Tasks_Queue_Dequeue");
	__itt_Retirement_Queue_Enqueue = __itt_string_handle_create("Finished_Tasks_Queue_Enqueue");
	__itt_CT_Barrier_Wait = __itt_string_handle_create("Control_Thread_Barrier_Wait");
	__itt_WT_Barrier = __itt_string_handle_create("Worker_Thread_Barrier");
	__itt_WT_Wait_For_Work = __itt_string_handle_create("Worker_Thread_Wait_For_Work");
	__itt_Task_In_Execution = __itt_string_handle_create("Task_In_Execution");
	__itt_Task_Stealing = __itt_string_handle_create("Task_Stealing");
	__itt_TaskGraph_Add = __itt_string_handle_create("Add_Task_To_TaskGraph");
	__itt_TaskGraph_Del = __itt_string_handle_create("Del_Task_From_TaskGraph");
	__itt_Checking_Dependences = __itt_string_handle_create("Checking_Dependences");
	__itt_Releasing_Dependences = __itt_string_handle_create("Releasing_Dependences");
	__itt_CT_Task_Alloc = __itt_string_handle_create("Task_Alloc");
	__itt_CT_Task_With_Deps = __itt_string_handle_create("Task_With_Deps");


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

/**
 * WARNING: We consider just one producer!!! This method not is thread safe.
 */
void __mtsp_addNewTask(kmp_task* newTask, kmp_uint32 ndeps, kmp_depend_info* depList) {
#ifdef MTSP_WORKSTEALING_CT
	/// The CT is trying to submit work but the queue is full. The CT will then
	/// spend some time executing tasks
	if (submissionQueue.cur_load() > submissionQueue.cont_load()) {
		/// - In distributed run queues mode we are going to steal until the
		/// number of inFlight tasks in the system become zero.
		/// - In the centralized run queue we are going to steal until the
		/// run queue become empty.
#ifdef MTSP_MULTIPLE_RUN_QUEUES
	#ifndef MTSP_CRITICAL_PATH_PREDICTION
		steal_from_multiple_run_queue(false);
	#else
		steal_from_prioritized_run_queues(false);
	#endif
#else
		steal_from_single_run_queue(true);
#endif
	}
#endif
	__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_Submission_Queue_Enqueue);

	/// Increment the number of tasks in the system currently
	ATOMIC_ADD(&__mtsp_inFlightTasks, (kmp_int32)1);

	/// TODO: Can we improve this?
	{
		__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_Submission_Queue_Copy);

		newTask->metadata->dep_list = (kmp_depend_info*) malloc(sizeof(kmp_depend_info) * ndeps);
		for (kmp_uint32 i=0; i<ndeps; i++)
			newTask->metadata->dep_list[i] = depList[i];

		__itt_task_end(__itt_mtsp_domain);
	}

	newTask->metadata->ndeps = ndeps;

	__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_Submission_Queue_Add);
	submissionQueue.enq(newTask);
	__itt_task_end(__itt_mtsp_domain);

	__itt_task_end(__itt_mtsp_domain);
}

void __mtsp_FlushRunQueues() {
	for (unsigned int i=0; i<__mtsp_numWorkerThreads; i++) {
		RunQueues[i].fsh();
	}
}

void __mtsp_compute_bottom_frontier() {
	/// Update the index for the new task window
	__mtsp_ColorVectorIdx = ! __mtsp_ColorVectorIdx;

	/// Store the size of the new fronter
	int frontierSize = 0;

	/// Reset the counters of the next task window
	int color = __mtsp_ColorVectorIdx * MAX_TASKS + 1;
	for (int idx=0; idx<MAX_TASKS; idx++)
		__mtsp_ColorVector[color++] = 0;

	/// Compute the roots of the new window
	color = __mtsp_ColorVectorIdx * MAX_TASKS + 1;
	for (int idx=0; idx<MAX_TASKS; idx++) {
		/// For each node that is present in the graph and has no children
		if (__mtsp_NodeStatus[idx] == 1) {
			__mtsp_NodeColor[idx] = color;
			__mtsp_ColorVector[color] = 1;

			/// Each node will have a different color
			color++;

			frontierSize++;
		}
	}

//	dumpDependenceGraphToDot(0, 0);
//	printf("Bottom frontier recomputed. The new frontier has %02d elements.\n", frontierSize);
}

void* __mtsp_RuntimeThreadCode(void* params) {
	//===-------- Initialize VTune/libittnotify related stuff ----------===//
	__itt_thread_set_name("MTSPRuntime");

	stick_this_thread_to_core(__MTSP_RUNTIME_THREAD_CORE__);
	kmp_uint64 iterations = 0;
	kmp_uint64 additions = 0;

	while (true) {
#ifdef MTSP_MULTIPLE_RETIRE_QUEUES
		for (unsigned int i=0; i<=__mtsp_numWorkerThreads; i++) {
			if (RetirementQueues[i].can_deq())
				removeFromTaskGraph(RetirementQueues[i].deq());
		}
#else
		if (RetirementQueue.can_deq())
			removeFromTaskGraph(RetirementQueue.deq());
#endif

		/// Check if there is any request for new thread creation
		if (freeSlots[0] > 0 && submissionQueue.can_deq()) {
			addToTaskGraph(submissionQueue.deq());

			/// If we have already added 64 tasks then we recompute the
			/// bottom frontier.
			if ((additions & 0x3F) == 0) {
				__mtsp_compute_bottom_frontier();
			}

			additions++;
		}

		/// This is a hack. It would be nice to have a better algorithm for
		/// intercore queue that do not suffer from false sharing and also
		/// do not need this.
		/// What this actually does is make sure the queues do not get stuck.
		iterations++;
		if ((iterations & 0xFF) == 0) __mtsp_FlushRunQueues();
	}

	return 0;
}
