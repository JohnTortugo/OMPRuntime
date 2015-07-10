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

bool 				volatile __mtsp_initialized 	= false;
pthread_t 			__mtsp_RuntimeThread;

kmp_task* 			volatile __mtsp_newTasksQueue[NEW_TASKS_QUEUE_SIZE];
kmp_uint32 			volatile __mtsp_newTQDeps[NEW_TASKS_QUEUE_SIZE];
kmp_depend_info* 	volatile __mtsp_newTQDepsPointers[NEW_TASKS_QUEUE_SIZE];
bool				volatile __mtsp_newTQAvailables[NEW_TASKS_QUEUE_SIZE];
kmp_uint32			volatile __mtsp_newTQReadIndex;
kmp_uint32			volatile __mtsp_newTQWriteIndex;

/// Initialization of locks
unsigned char volatile __mtsp_lock_initialized 	 = 0;
unsigned char volatile __mtsp_lock_newTasksQueue = 0;

/// Variables/constants related to the the taskMetadata buffer
bool __mtsp_taskMetadataStatus[MAX_TASKMETADATA_SLOTS];
char __mtsp_taskMetadataBuffer[MAX_TASKMETADATA_SLOTS][TASK_METADATA_MAX_SIZE];



//===-------- VTune/libittnotify related stuff ----------===//
__itt_domain*			volatile __itt_mtsp_domain	= nullptr;
__itt_string_handle* 	volatile __itt_ReadyQueue_Dequeue	= nullptr;
__itt_string_handle* 	volatile __itt_ReadyQueue_Enqueue	= nullptr;
__itt_string_handle* 	volatile __itt_New_Tasks_Queue_Dequeue	= nullptr;
__itt_string_handle* 	volatile __itt_New_Tasks_Queue_Enqueue	= nullptr;
__itt_string_handle* 	volatile __itt_New_Tasks_Queue_Copy		= nullptr;
__itt_string_handle* 	volatile __itt_New_Tasks_Queue_Full		= nullptr;
__itt_string_handle* 	volatile __itt_Finished_Tasks_Queue_Dequeue	= nullptr;
__itt_string_handle* 	volatile __itt_Finished_Tasks_Queue_Enqueue	= nullptr;
__itt_string_handle* 	volatile __itt_Control_Thread_Barrier_Wait	= nullptr;
__itt_string_handle* 	volatile __itt_Worker_Thread_Barrier	= nullptr;
__itt_string_handle* 	volatile __itt_Task_In_Execution	= nullptr;
__itt_string_handle* 	volatile __itt_Add_Task_To_TaskGraph	= nullptr;
__itt_string_handle* 	volatile __itt_Del_Task_From_TaskGraph	= nullptr;
__itt_string_handle* 	volatile __itt_Checking_Dependences	= nullptr;
__itt_string_handle* 	volatile __itt_Releasing_Dependences	= nullptr;
__itt_string_handle* 	volatile __itt_Task_Alloc	= nullptr;
__itt_string_handle* 	volatile __itt_Task_With_Deps	= nullptr;
__itt_string_handle* 	volatile __itt_Worker_Thread_Wait_For_Work	= nullptr;





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
	__itt_ReadyQueue_Dequeue = __itt_string_handle_create("ReadyQueue_Dequeue");
	__itt_ReadyQueue_Enqueue = __itt_string_handle_create("ReadyQueue_Enqueue");
	__itt_New_Tasks_Queue_Dequeue = __itt_string_handle_create("New_Tasks_Queue_Dequeue");
	__itt_New_Tasks_Queue_Enqueue = __itt_string_handle_create("New_Tasks_Queue_Enqueue");
	__itt_New_Tasks_Queue_Full = __itt_string_handle_create("New_Tasks_Queue_Full");
	__itt_New_Tasks_Queue_Copy = __itt_string_handle_create("New_Tasks_Queue_Copy");
	__itt_Finished_Tasks_Queue_Dequeue = __itt_string_handle_create("Finished_Tasks_Queue_Dequeue");
	__itt_Finished_Tasks_Queue_Enqueue = __itt_string_handle_create("Finished_Tasks_Queue_Enqueue");
	__itt_Control_Thread_Barrier_Wait = __itt_string_handle_create("Control_Thread_Barrier_Wait");
	__itt_Worker_Thread_Barrier = __itt_string_handle_create("Worker_Thread_Barrier");
	__itt_Worker_Thread_Wait_For_Work = __itt_string_handle_create("Worker_Thread_Wait_For_Work");
	__itt_Task_In_Execution = __itt_string_handle_create("Task_In_Execution");
	__itt_Add_Task_To_TaskGraph = __itt_string_handle_create("Add_Task_To_TaskGraph");
	__itt_Del_Task_From_TaskGraph = __itt_string_handle_create("Del_Task_From_TaskGraph");
	__itt_Checking_Dependences = __itt_string_handle_create("Checking_Dependences");
	__itt_Releasing_Dependences = __itt_string_handle_create("Releasing_Dependences");
	__itt_Task_Alloc = __itt_string_handle_create("Task_Alloc");
	__itt_Task_With_Deps = __itt_string_handle_create("Task_With_Deps");


	//===-------- This slot is free for use by any thread ----------===//
	for (int i=0; i<MAX_TASKMETADATA_SLOTS; i++) {
		__mtsp_taskMetadataStatus[i] = false;
	}


	//===-------- Initialize runtime data structures ----------===//
	__mtsp_newTasksQueue[0] = (kmp_uint64) 0;
	__mtsp_newTQDepsPointers[0]	= nullptr;
	__mtsp_newTQAvailables[0] = false;
	__mtsp_newTQReadIndex = 0;
	__mtsp_newTQWriteIndex = 0;

	/// This the original main thread to core-0
	stick_this_thread_to_core(__MTSP_MAIN_THREAD_CORE__);

	__mtsp_initializeTaskGraph();

	pthread_create(&__mtsp_RuntimeThread, NULL, __mtsp_RuntimeThreadCode, NULL);
}

void __mtsp_addNewTask(kmp_task* newTask, kmp_uint32 ndeps, kmp_depend_info* depList) {
	__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_New_Tasks_Queue_Enqueue);
	ACQUIRE(&__mtsp_lock_newTasksQueue);

	/// If the position we would write is still in use we need to release the lock
	/// to let that position to be consumed.. and wait until it is in fact consumed.
	if (__mtsp_newTQAvailables[__mtsp_newTQWriteIndex]) {
		RELEASE(&__mtsp_lock_newTasksQueue);
		__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_New_Tasks_Queue_Full);
		while (__mtsp_newTQAvailables[__mtsp_newTQWriteIndex]);
		__itt_task_end(__itt_mtsp_domain);
		ACQUIRE(&__mtsp_lock_newTasksQueue);
	}

	/// Increment the number of tasks in the system currently
	ATOMIC_ADD(&__mtsp_inFlightTasks, (kmp_int32)1);

	__mtsp_newTasksQueue[__mtsp_newTQWriteIndex] 	 = newTask;
	__mtsp_newTQDeps[__mtsp_newTQWriteIndex] 		 = ndeps;
	__mtsp_newTQDepsPointers[__mtsp_newTQWriteIndex] = (kmp_depend_info*) malloc(sizeof(kmp_depend_info) * ndeps);

	/// TODO: Can we improve this?
	__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_New_Tasks_Queue_Copy);
	for (kmp_uint32 i=0; i<ndeps; i++)
		__mtsp_newTQDepsPointers[__mtsp_newTQWriteIndex][i] = depList[i];
	__itt_task_end(__itt_mtsp_domain);

	__mtsp_newTQAvailables[__mtsp_newTQWriteIndex]	= true;
	__mtsp_newTQWriteIndex						= (__mtsp_newTQWriteIndex + 1) % NEW_TASKS_QUEUE_SIZE;

	RELEASE(&__mtsp_lock_newTasksQueue);
	__itt_task_end(__itt_mtsp_domain);
}

void* __mtsp_RuntimeThreadCode(void* params) {
	//===-------- Initialize VTune/libittnotify related stuff ----------===//
	__itt_thread_set_name("MTSPRuntime");

	stick_this_thread_to_core(__MTSP_RUNTIME_THREAD_CORE__);

	while (true) {
		/// Remove any thread that may have finished execution
		ACQUIRE(&lock_finishedSlots);
		if ( finishedSlots[0] > 0 ) {
			kmp_uint16 idOfFinishedTask = finishedSlots[ finishedSlots[0] ];
			removeFromTaskGraph(idOfFinishedTask);
			finishedSlots[0]--;
		}
		RELEASE(&lock_finishedSlots);


		/// Check if there is any request for new thread creation
		ACQUIRE(&__mtsp_lock_newTasksQueue);
		if ( __mtsp_newTQAvailables[__mtsp_newTQReadIndex] && freeSlots[0] > 0 ) {
			addToTaskGraph(__mtsp_newTasksQueue[__mtsp_newTQReadIndex], __mtsp_newTQDeps[__mtsp_newTQReadIndex], __mtsp_newTQDepsPointers[__mtsp_newTQReadIndex]);
			__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_New_Tasks_Queue_Dequeue);
			__mtsp_newTQAvailables[__mtsp_newTQReadIndex]	= false;
			__mtsp_newTQReadIndex 					= (__mtsp_newTQReadIndex + 1) % NEW_TASKS_QUEUE_SIZE;
			__itt_task_end(__itt_mtsp_domain);
		}
		RELEASE(&__mtsp_lock_newTasksQueue);
	}

	return 0;
}
