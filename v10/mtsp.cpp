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


MPMCQueue<kmp_uint16, MAX_TASKS*2, 4> freeSlots;

/// Variables/constants related to the the taskMetadata buffer
bool volatile __mtsp_taskMetadataStatus[MAX_TASKMETADATA_SLOTS];
char __mtsp_taskMetadataBuffer[MAX_TASKMETADATA_SLOTS][TASK_METADATA_MAX_SIZE];

int mtsp_number_of_outstanding_task_descriptors = 0;

/// Variables related to the coalescing framework
std::map<kmp_uint64, std::pair<kmp_uint64, double>> taskSize;
std::map<kmp_uint64, bool> realTasks;

std::set<kmp_uint64> coalBlacklist;

std::set<kmp_uint64> uniqueAddrs;
double __coalHowManyAddrs=0;

double __curCoalL = 0;
double __tgtCoalL = 0;
kmp_int16 __curCoalesceSize = 0;
kmp_task* coalescedTask = nullptr;

kmp_int64 __coalNecessary = 0;
kmp_int64 __coalUnnecessary = 0;
kmp_int64 __coalImpossible = 0;
kmp_int64 __coalOverflowed = 0;

kmp_int64 __coalSuccess = 0;
kmp_int64 __coalFailed = 0;




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
	__itt_string_handle* volatile __itt_Coalescing = nullptr;
	__itt_string_handle* volatile __itt_Coal_In_Execution = nullptr;
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
	std::cout << "__mtsp_numThreads;              => " << __mtsp_numThreads << std::endl;
	std::cout << "__mtsp_numWorkerThreads;        => " << __mtsp_numWorkerThreads << std::endl;
	std::cout << "__mtsp_inFlightTasks            => " << __mtsp_inFlightTasks << std::endl;
	std::cout << "__mtsp_threadWait               => " << __mtsp_threadWait << std::endl;
	std::cout << "__mtsp_threadWaitCounter        => " << __mtsp_threadWaitCounter<< std::endl;
	std::cout << "__curCoalesceSize               => " << __curCoalesceSize << std::endl;
	std::cout << "freeSlots[0]                    => " << freeSlots.cur_load() << std::endl;
	std::cout << "submissionQueue.cur_load        => " << submissionQueue.cur_load() << std::endl;
	std::cout << "RunQueue.cur_load               => " << RunQueue.cur_load() << std::endl;
	std::cout << "RetirementQueue.cur_load        => " << RetirementQueue.cur_load() << std::endl;
	std::cout << "------------------------------------------------------------------" << std::endl;
}


void updateAverageTaskSize(kmp_uint64 taskAddr, double size) {
	double newMed=0, sum=0;

	if (taskSize.find(taskAddr) == taskSize.end()) {
		taskSize[taskAddr] = std::make_pair(1, size);
	}
	else if (taskSize[taskAddr].first < MIN_SAMPLES_AVERAGE) {
		taskSize[taskAddr].first++;

		if (taskSize[taskAddr].first == MIN_SAMPLES_AVERAGE) {
			taskSize[taskAddr].second = size;
		}
	}
	else {
		auto oldPair = taskSize[taskAddr];

		sum = oldPair.first / oldPair.second;
				sum += 1.0 / size;

		newMed = (oldPair.first + 1) / sum;

		taskSize[taskAddr] = std::make_pair(oldPair.first+1, newMed);
	}

	//printf("UATS: %llx -> %lf [%lf]\n", taskAddr, size, newMed);
}

void __mtsp_reInitialize() {
	__coalNecessary = 0;
	__coalUnnecessary = 0;
	__coalImpossible = 0;
	__coalOverflowed = 0;
	__coalSuccess = 0;
	__coalFailed = 0;

	/// The size of the tasks is reset every time we enter a new parallel region
   	taskSize.clear();

#ifdef MTSP_DUMP_STATS
	realTasks.clear();
#endif
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

	__itt_Coalescing = __itt_string_handle_create("Coalescing");
	__itt_Coal_In_Execution = __itt_string_handle_create("Coal_In_Execution");

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



	//===-------- This slot is free for use by any thread ----------===//
	for (int i=0; i<MAX_TASKMETADATA_SLOTS; i++) {
		__mtsp_taskMetadataStatus[i] = false;
	}

	//===-------- Initialize the task graph manager ----------===//
	__mtsp_initializeTaskGraph();

	//===---- The control thread is the parent of all tasks but starts executing no task ----------===//
	idOfCurrentTask[0] = -1;

	//===---- Thread local storage. The control thread have the ID 0 ----------===//
	threadId = 0;
}

void __mtsp_addNewTask(kmp_task* newTask, kmp_uint32 ndeps, kmp_depend_info* depList) {
	__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_Submission_Queue_Enqueue);

#ifdef MTSP_WORKSTEALING_CT
	// The CT is trying to submit work but the queue is full. The CT will then
	// spend some time executing tasks
	if (submissionQueue.cur_load() >= submissionQueue.cont_load()) {
		steal_from_single_run_queue(true);
	}
#endif

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

void __mtsp_RuntimeWorkSteal() {
	kmp_task* taskToExecute = nullptr;

	// Counter for the total cycles spent per task
	unsigned long long start=0, end=0;

	__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_Task_Stealing);

	while (RunQueue.cur_load() > RunQueue.cont_load()) {
		if (RunQueue.try_deq(&taskToExecute)) {
			 __itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_Task_In_Execution);

			start = beg_read_mtsp();

			// Start execution of the task
			(*(taskToExecute->routine))(0, taskToExecute);

			end = end_read_mtsp();

			__itt_task_end(__itt_mtsp_domain);

			taskToExecute->metadata->taskSize = (end - start);

			tasksExecutedByRT++;

			// Inform that this task has finished execution
			__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_Retirement_Queue_Enqueue);
			RetirementQueue.enq(taskToExecute);
			__itt_task_end(__itt_mtsp_domain);
		}
	}

	__itt_task_end(__itt_mtsp_domain);
}


void saveCoalesce() {
	// Counter for the total cycles spent per task
	unsigned long long start=0, end=0;

	__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_Coalescing);

	start = beg_read_mtsp();

	// Update the number of successes and failures to create a full coalesce
	__coalSuccess += (__curCoalL <= __tgtCoalL);
	__coalFailed += (__curCoalL > __tgtCoalL);

	coalescedTask->routine = executeCoalesced;
	coalescedTask->metadata->coalesceSize = __curCoalesceSize;

	std::map<kmp_intptr, kmp_depend_info*> deps;

	for (int idx=0; idx<__curCoalesceSize; idx++) {
		kmp_task* currTask = coalescedTask->metadata->coalesced[idx];

		for (int depIdx=0; depIdx<currTask->metadata->ndeps; depIdx++) {
			kmp_depend_info* currDepInfo = &currTask->metadata->dep_list[depIdx];

			// First time we see that addr or not?
			if (deps.find(currDepInfo->base_addr) == deps.end()) {
				deps[currDepInfo->base_addr] = currDepInfo;
			}
			else {
				deps[currDepInfo->base_addr]->flags.in |= currDepInfo->flags.in;
				deps[currDepInfo->base_addr]->flags.out |= currDepInfo->flags.out;
			}
		}

		if (idx > 0) {
			// Decrement the number of tasks in the system currently
			ATOMIC_SUB(&__mtsp_inFlightTasks, 1);

			freeSlots.enq( currTask->metadata->taskgraph_slot_id );
			//freeSlots[0]++;
			//freeSlots[ freeSlots[0] ] = currTask->metadata->taskgraph_slot_id;

			if (currTask->metadata->metadata_slot_id >= 0)
				__mtsp_taskMetadataStatus[currTask->metadata->metadata_slot_id] = false;
		}
	}

	coalescedTask->metadata->taskgraph_slot_id = coalescedTask->metadata->coalesced[0]->metadata->taskgraph_slot_id;
	coalescedTask->metadata->metadata_slot_id = coalescedTask->metadata->metadata_slot_id;
	coalescedTask->metadata->ndeps = deps.size();
	coalescedTask->metadata->dep_list = (kmp_depend_info*) malloc(sizeof(kmp_depend_info) * deps.size()); 

	int realIdx = 0;
	for (auto& realDep : deps) {
		coalescedTask->metadata->dep_list[realIdx] = *realDep.second;
		realIdx++;
	}

	end = end_read_mtsp();

	// Does an XOR with the address of the coalesce routine and the address of the task
	kmp_uint64 colKey = ((kmp_uint64) saveCoalesce) ^ ((kmp_uint64) coalescedTask->metadata->coalesced[0]->routine);

	updateAverageTaskSize(colKey, (end - start) / __curCoalesceSize);

	addToTaskGraph( coalescedTask );

	__itt_task_end(__itt_mtsp_domain);
}

double howManyShouldBeCoalesced(kmp_uint64 taskAddr) {
//	if (taskSize.find(taskAddr) == taskSize.end() || taskSize[taskAddr].first < MIN_SAMPLES_FOR_COALESCING) 
		return 99;
//	else
//		return 50;

//	auto rtlKey 	= ((kmp_uint64) __mtsp_RuntimeThreadCode ^ taskAddr);
//	auto colKey 	= ((kmp_uint64) saveCoalesce ^ taskAddr);
//
//	double sti 		= taskSize[taskAddr].second;
//	double m   		= __mtsp_numWorkerThreads;				/// Includes the runtime and the control
//	double ro  		= 2 * taskSize[rtlKey].second; 			/// we double it because the stored value is average of add/del (not the sum) from TG
//	double co  		= (taskSize.find(colKey) != taskSize.end()) ? taskSize[colKey].second : ro * 0.01;
//
//	if (sti >= m*(co + ro)) {
//		#ifdef DEBUG_COAL_MODE
//				printf("[Coalesce] No need for coalescing. [task=%llx, execs=%lld, sti=%lf, m=%lf, ro=%lf, co=%lf]\n", taskAddr, taskSize[taskAddr].first, sti, m, ro, co);
//		#endif
//		__coalUnnecessary++;
//		return 99;
//	}
//	else {
//		double l = (sti - m*co) / (m*ro);
//
//		if (l < 0.4) {
//			#ifdef DEBUG_COAL_MODE
//				printf("[Coalesce] Impossible to amortize. [task=%llx, execs=%lld, sti=%lf, m=%lf, ro=%lf, co=%lf, l=%lf]\n", taskAddr, taskSize[taskAddr].first, sti, m, ro, co, l);
//			#endif
//			__coalImpossible++;
//			return 100;	// I am trying to not decrease parallelism here
//		}
//		else {
//			#ifdef DEBUG_COAL_MODE
//				printf("[Coalesce] Need for coalescing. [task=%llx, execs=%lld, sti=%lf, m=%lf, ro=%lf, co=%lf, l=%lf]\n", taskAddr, taskSize[taskAddr].first, sti, m, ro, co, l);
//			#endif
//			__coalNecessary++;
//			return l;
//		}
//	}
}

inline bool hasPendingCoalesce() {
	return (__curCoalesceSize > 0);
}

inline void restartCoalesce() {
	__curCoalesceSize = 0;
//	__tgtCoalL = 100;
	__coalHowManyAddrs=0;

	uniqueAddrs.clear();

	coalescedTask = new kmp_task();
	coalescedTask->metadata = new _mtsp_task_metadata();
	coalescedTask->metadata->coalesceSize = 0;
}


inline void increaseCoalesce(kmp_task* task) {
	coalescedTask->metadata->coalesced[__curCoalesceSize] = task;
	__curCoalesceSize++;

	// Need to update the __curCoalL
	for (int depIdx=0; depIdx<task->metadata->ndeps; depIdx++) {
		uniqueAddrs.insert(task->metadata->dep_list[depIdx].base_addr);
	}
	__coalHowManyAddrs += task->metadata->ndeps;
	__curCoalL = uniqueAddrs.size() / __coalHowManyAddrs;

	// Did we reach the target of the linearity factor?
	if (__curCoalL <= __tgtCoalL || __curCoalesceSize == MAX_COALESCING_SIZE) {
		saveCoalesce();
		restartCoalesce();
	}
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

