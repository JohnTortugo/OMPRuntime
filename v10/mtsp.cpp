/**
 * @file 	mtsp.h
 * @brief 	This file contains functions and variables related the main functions of MTSP.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <pthread.h>
#include <unistd.h>
#include <sched.h>
#include <string.h>
#include <map>
#include <cassert>

#include "kmp.h"
#include "mtsp.h"
#include "scheduler.h"
#include "task_graph.h"
#include "ThreadedQueue.h"
#include "fe_interface.h"

bool 				volatile 	__mtsp_initialized 	= false;
pthread_t 						__mtsp_RuntimeThread;
bool 				volatile 	__mtsp_Single 		= false;

__thread unsigned int threadId = 0;

kmp_uint32 						__mtsp_globalTaskCounter = 0;


/// Initialization of locks
unsigned char volatile __mtsp_lock_initialized 	 = 0;
unsigned char volatile __mtsp_lock_newTasksQueue = 0;

bool pcGraphLock = 0;

MPMCQueue<kmp_uint16, MAX_TASKS*2, 4> freeSlots;

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
	__itt_string_handle* volatile __itt_Releasing_Dep_Reader = nullptr;
	__itt_string_handle* volatile __itt_CT_Barrier_Wait = nullptr;
	__itt_string_handle* volatile __itt_CT_Task_Alloc = nullptr;
	__itt_string_handle* volatile __itt_CT_Task_With_Deps = nullptr;
	__itt_string_handle* volatile __itt_WT_Barrier = nullptr;
	__itt_string_handle* volatile __itt_WT_Serving_Steal = nullptr;
	__itt_string_handle* volatile __itt_WT_Wait_For_Work = nullptr;
	__itt_string_handle* volatile __itt_Task_In_Execution = nullptr;
	__itt_string_handle* volatile __itt_Task_Stealing = nullptr;
	__itt_string_handle* volatile __itt_SPSC_Enq = nullptr;
	__itt_string_handle* volatile __itt_SPSC_Deq = nullptr;
	__itt_string_handle* volatile __itt_SPSC_Enq_Blocking = nullptr;
	__itt_string_handle* volatile __itt_SPSC_Deq_Blocking = nullptr;


	__itt_string_handle* volatile __itt_RT_Main_Loop = nullptr;
	__itt_string_handle* volatile __itt_RT_Check_Del = nullptr;
	__itt_string_handle* volatile __itt_RT_Check_Add = nullptr;
	__itt_string_handle* volatile __itt_RT_Check_Oth = nullptr;

#if ! USE_ITTNOTIFY
	void __itt_task_end(const __itt_domain *domain) {}
	void __itt_task_begin(const __itt_domain *domain, __itt_id taskid, __itt_id parentid, __itt_string_handle *name) {}
	void __itt_thread_set_name(const char    *name) {}
	__itt_domain* __itt_domain_create(const char *name) {}
	__itt_string_handle* __itt_string_handle_create(const char *name) {}
#endif

extern void steal_from_single_run_queue(bool just_a_bit);
extern void steal_from_multiple_run_queue(bool just_a_bit);


unsigned long long beg_read_mtsp() {
	unsigned int high, low=0;
	unsigned long long cycles=0;

#ifndef __arm__
	asm volatile (	"CPUID\n\t"
					"RDTSC\n\t"
					"mov %%edx, %0\n\t"
					"mov %%eax, %1\n\t": "=r" (high), "=r" (low) :: "%rax", "%rbx", "%rcx", "%rdx");

	cycles = ((unsigned long long)high << 32) | low;
#endif

	return cycles;
}


unsigned long long end_read_mtsp() {
	unsigned int high, low=0;
	unsigned long long cycles=0;

#ifndef __arm__
	asm volatile (	"RDTSCP\n\t"
					"mov %%edx, %0\n\t"
					"mov %%eax, %1\n\t"
					"CPUID\n\t": "=r" (high), "=r" (low) :: "%rax", "%rbx", "%rcx", "%rdx");

	cycles = ((unsigned long long)high << 32) | low;
#endif

	return cycles;
}


void debug() {
	std::cout << std::endl;
	std::cout << "------------------ DEBUG ---------- DEBUG ------- DEBUG ----------" << std::endl;
	std::cout << "__mtsp_numThreads               => " << __mtsp_numThreads << std::endl;
	std::cout << "__mtsp_numWorkerThreads         => " << __mtsp_numWorkerThreads << std::endl;
	std::cout << "__mtsp_inFlightTasks            => " << __mtsp_inFlightTasks << std::endl;
	std::cout << "__ControlThreadDirectChild      => " << __ControlThreadDirectChild << std::endl;
	std::cout << "__mtsp_threadWait               => " << __mtsp_threadWait << std::endl;
	std::cout << "__mtsp_threadWaitCounter        => " << __mtsp_threadWaitCounter<< std::endl;
	std::cout << "idOfCurrentTask                 => [";

	for (int i=0; i<__mtsp_numThreads; i++) 
		printf("(%02d : %02d) ", i, idOfCurrentTask[i]);

	std::cout << "]" << std::endl;

	std::cout << "freeSlots[0]                    => " << freeSlots.cur_load() << std::endl;
	std::cout << "submissionQueue.cur_load        => " << submissionQueue.cur_load() << std::endl;
	std::cout << "RunQueue.cur_load               => " << RunQueue.cur_load() << std::endl;
	std::cout << "RetirementQueue.cur_load        => " << RetirementQueue.cur_load() << std::endl;
	std::cout << "------------------------------------------------------------------" << std::endl;
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
	__itt_Releasing_Dep_Reader = __itt_string_handle_create("Releasing_Dep_Reader");
	__itt_CT_Barrier_Wait = __itt_string_handle_create("CT_Barrier_Wait");
	__itt_CT_Task_Alloc = __itt_string_handle_create("CT_Task_Alloc");
	__itt_CT_Task_With_Deps = __itt_string_handle_create("CT_Task_With_Deps");
	__itt_WT_Barrier = __itt_string_handle_create("WT_Barrier");
	__itt_WT_Serving_Steal = __itt_string_handle_create("WT_Serving_Steal");
	__itt_WT_Wait_For_Work = __itt_string_handle_create("WT_Wait_For_Work");

	__itt_Task_In_Execution = __itt_string_handle_create("Task_In_Execution");
	__itt_Task_Stealing = __itt_string_handle_create("Task_Stealing");
	__itt_SPSC_Enq = __itt_string_handle_create("SPSC_Enq");
	__itt_SPSC_Deq = __itt_string_handle_create("SPSC_Deq");
	__itt_SPSC_Enq_Blocking = __itt_string_handle_create("SPSC_Enq_Blocking");
	__itt_SPSC_Deq_Blocking = __itt_string_handle_create("SPSC_Deq_Blocking");

	__itt_RT_Main_Loop = __itt_string_handle_create("RT_Main_Loop");
	__itt_RT_Check_Del = __itt_string_handle_create("RT_Check_Del");
	__itt_RT_Check_Add = __itt_string_handle_create("RT_Check_Add");
	__itt_RT_Check_Oth = __itt_string_handle_create("RT_Check_Oth");

	//===-------- Initialize the task graph manager ----------===//
	__mtsp_initializeTaskGraph();

	//===---- The control thread is the parent of all tasks but starts executing no task ----------===//
	idOfCurrentTask[0] = -1;

	//===---- Thread local storage. The control thread have the ID 0 ----------===//
	threadId = 0;
}

void __mtsp_addNewTask(kmp_task* newTask, kmp_uint32 ndeps, kmp_depend_info* depList) {
	__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_Submission_Queue_Enqueue);

	// Increment the number of tasks in the system currently
	ATOMIC_ADD(&__mtsp_inFlightTasks, (kmp_int32)1);

	// TODO: Can we improve this?
	{
		__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_Submission_Queue_Copy);

		newTask->metadata->dep_list = (kmp_depend_info*) malloc(sizeof(kmp_depend_info) * ndeps);
		for (kmp_uint32 i=0; i<ndeps; i++) {
			newTask->metadata->dep_list[i] = depList[i];
		}

		newTask->metadata->ndeps = ndeps;

		__itt_task_end(__itt_mtsp_domain);
	}


	__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_Submission_Queue_Add);
	submissionQueue.enq(newTask);
	__itt_task_end(__itt_mtsp_domain);

	__itt_task_end(__itt_mtsp_domain);
}

void* __mtsp_RuntimeThreadCode(void* params) {
	//===-------- Initialize VTune/libittnotify related stuff ----------===//
	__itt_thread_set_name("MTSPRuntime");

	kmp_task* task = nullptr;

	__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_RT_Main_Loop);
	while (true) {
		// -------------------------------------------------------------------------------
		// Check if the execution of a task has been completed
		__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_RT_Check_Del);
		if (RetirementQueue.try_deq(&task)) {
			removeFromTaskGraph(task);
		}
		__itt_task_end(__itt_mtsp_domain);


		// -------------------------------------------------------------------------------
		// Check if there is any request for new thread creation
		__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_RT_Check_Add);
		if (freeSlots.cur_load() > 0 && submissionQueue.try_deq(&task)) {
			task->metadata->taskgraph_slot_id = freeSlots.deq();
	
			addToTaskGraph(task);
		}
		__itt_task_end(__itt_mtsp_domain);

		//if (__mtsp_inFlightTasks >= 10) {
		//	__mtsp_dumpTaskGraphToDot();
		//}
	}
	__itt_task_end(__itt_mtsp_domain);

	return 0;
}

