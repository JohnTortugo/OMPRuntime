#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <pthread.h>
#include <unistd.h>
#include <sched.h>
#include <string.h>
#include <map>

#include "kmp.h"
#include "mtsp.h"
#include "scheduler.h"
#include "task_graph.h"
#include "ThreadedQueue.h"
#include "fe_interface.h"

bool 				volatile 	__mtsp_initialized 	= false;
pthread_t 						__mtsp_RuntimeThread;
bool 				volatile 	__mtsp_Single 		= false;

kmp_uint32 						__mtsp_globalTaskCounter = 0;


/// Initialization of locks
unsigned char volatile __mtsp_lock_initialized 	 = 0;
unsigned char volatile __mtsp_lock_newTasksQueue = 0;

/// Variables/constants related to the the taskMetadata buffer
bool volatile __mtsp_taskMetadataStatus[MAX_TASKMETADATA_SLOTS];
char __mtsp_taskMetadataBuffer[MAX_TASKMETADATA_SLOTS][TASK_METADATA_MAX_SIZE];
std::map<kmp_uint64, std::pair<kmp_uint64, kmp_uint64>> taskSize;
kmp_int16 __curCoalesceSize = 0;
kmp_int16 __curTargetCoalescingSize = 0;





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

extern void steal_from_single_run_queue(bool just_a_bit);
extern void steal_from_multiple_run_queue(bool just_a_bit);


unsigned long long beg_read_mtsp() {
	unsigned int high, low=0;
	unsigned long long cycles=0;

	asm volatile (	"CPUID\n\t"
					"RDTSC\n\t"
					"mov %%edx, %0\n\t"
					"mov %%eax, %1\n\t": "=r" (high), "=r" (low) :: "%rax", "%rbx", "%rcx", "%rdx");

	cycles = ((unsigned long long)high << 32) | low;

	return cycles;
}


unsigned long long end_read_mtsp() {
	unsigned int high, low=0;
	unsigned long long cycles=0;

	asm volatile (	"RDTSCP\n\t"
					"mov %%edx, %0\n\t"
					"mov %%eax, %1\n\t"
					"CPUID\n\t": "=r" (high), "=r" (low) :: "%rax", "%rbx", "%rcx", "%rdx");

	cycles = ((unsigned long long)high << 32) | low;

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
	std::cout << "__curTargetCoalescingSize       => " << __curTargetCoalescingSize << std::endl;
	std::cout << "freeSlots[0]                    => " << freeSlots[0] << std::endl;
	std::cout << "submissionQueue.cur_load        => " << submissionQueue.cur_load() << std::endl;
	std::cout << "RunQueue.cur_load               => " << RunQueue.cur_load() << std::endl;
	std::cout << "RetirementQueue.cur_load        => " << RetirementQueue.cur_load() << std::endl;
	std::cout << "------------------------------------------------------------------" << std::endl;
}


int stick_this_thread_to_core(const char* const pref, int core_id) {
//	int num_cores = sysconf(_SC_NPROCESSORS_ONLN);
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
//	printf("%s executing now on core %d\n", pref, sched_getcpu());

	return 0;
}

void updateAverageTaskSize(kmp_uint64 taskAddr, kmp_uint64 size) {
	if (taskAddr == (kmp_uint64) addToTaskGraph) {
		printf("Cost of addToTaskGraph = %llu\n", size);
	}


	if (taskSize.find(taskAddr) == taskSize.end()) {
		taskSize[taskAddr] = std::make_pair(1, size);
	}
	else {
		auto oldPair = taskSize[taskAddr];

		kmp_uint64 newMed = ((oldPair.second * oldPair.first) + size) / (oldPair.first + 1);

		taskSize[taskAddr] = std::make_pair(oldPair.first+1, newMed);
	}
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


	//===-------- This slot is free for use by any thread ----------===//
	for (int i=0; i<MAX_TASKMETADATA_SLOTS; i++) {
		__mtsp_taskMetadataStatus[i] = false;
	}

	/// This the original main thread to core-0
	stick_this_thread_to_core("ControlThread", __MTSP_MAIN_THREAD_CORE__);



	//===-------- Initialize the task graph manager ----------===//
	__mtsp_initializeTaskGraph();

}

void __mtsp_addNewTask(kmp_task* newTask, kmp_uint32 ndeps, kmp_depend_info* depList) {
	__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_Submission_Queue_Enqueue);

#ifdef MTSP_WORKSTEALING_CT
	/// The CT is trying to submit work but the queue is full. The CT will then
	/// spend some time executing tasks
	if (submissionQueue.cur_load() >= submissionQueue.cont_load()) {
		steal_from_single_run_queue(true);
	}
#endif

	// This is a global counter for we identify the task across the whole prog. execution
	newTask->metadata->globalTaskId = __mtsp_globalTaskCounter++;


	/// Increment the number of tasks in the system currently
	ATOMIC_ADD(&__mtsp_inFlightTasks, (kmp_int32)1);

	/// TODO: Can we improve this?
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

	/// Counter for the total cycles spent per task
	unsigned long long start=0, end=0;

	__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_Task_Stealing);

	while (RunQueue.cur_load() > RunQueue.cont_load()) {
		if (RunQueue.try_deq(&taskToExecute)) {
			 __itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_Task_In_Execution);

			start = beg_read_mtsp();

			/// Start execution of the task
			(*(taskToExecute->routine))(0, taskToExecute);

			end = end_read_mtsp();

			__itt_task_end(__itt_mtsp_domain);

			taskToExecute->metadata->taskSize = (end - start);

			tasksExecutedByRT++;

			/// Inform that this task has finished execution
			__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_Retirement_Queue_Enqueue);
			RetirementQueue.enq(taskToExecute);
			__itt_task_end(__itt_mtsp_domain);
		}
	}

	__itt_task_end(__itt_mtsp_domain);
}


void addCoalescedTask(kmp_task* coalescedTask) {
	/// Counter for the total cycles spent per task
	unsigned long long start=0, end=0;

	start = beg_read_mtsp();

	coalescedTask->routine = executeCoalesced;
	coalescedTask->metadata->coalesceSize = __curCoalesceSize;

	std::map<kmp_intptr, kmp_depend_info*> deps;

	for (int idx=0; idx<__curCoalesceSize; idx++) {
		kmp_task* currTask = coalescedTask->metadata->coalesced[idx];

		if (idx > 0) {
			/// Decrement the number of tasks in the system currently
			ATOMIC_SUB(&__mtsp_inFlightTasks, 1);

			freeSlots[0]++;
			freeSlots[ freeSlots[0] ] = currTask->metadata->taskgraph_slot_id;

			if (currTask->metadata->metadata_slot_id >= 0)
				__mtsp_taskMetadataStatus[currTask->metadata->metadata_slot_id] = false;
		}

		for (int depIdx=0; depIdx<currTask->metadata->ndeps; depIdx++) {
			kmp_depend_info* currDepInfo = &currTask->metadata->dep_list[depIdx];

			/// First time we see that addr or not?
			if (deps.find(currDepInfo->base_addr) == deps.end()) {
				deps[currDepInfo->base_addr] = currDepInfo;
			}
			else {
				deps[currDepInfo->base_addr]->flags.in |= currDepInfo->flags.in;
				deps[currDepInfo->base_addr]->flags.out |= currDepInfo->flags.out;
			}
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

	updateAverageTaskSize((kmp_uint64) addCoalescedTask, (end - start) / __curCoalesceSize);

	addToTaskGraph( coalescedTask );
}

kmp_int16 howManyShouldBeCoalesced(kmp_uint64 taskAddr) {
	kmp_int64 sti = taskSize[taskAddr].second;
	kmp_int64 m   = __mtsp_numThreads;												/// Includes the runtime and the control
	kmp_int64 ro  = 2 * taskSize[(kmp_uint64) __mtsp_RuntimeThreadCode].second; 	/// we double it because the stored value is average between add/del from TG
	kmp_int64 co  = 0;
	
	if (taskSize.find((kmp_uint64) addCoalescedTask) != taskSize.end()) {
		co = taskSize[(kmp_uint64) addCoalescedTask].second;
	}
	else {
		co = ro * 0.01;
	}


	kmp_int64 num = (m * ro);
	kmp_int64 den = (sti - m*co);

	if (num == 0 || den == 0) return 0;

	kmp_int64 n = (m * ro) / (sti - m*co);

	if (n < 0) {
#ifdef DEBUG_MODE
		printf("Impossible to amortize (%lld). [sti=%lld, m=%lld, ro=%lld, co=%lld, n=%lld]\n", MIN_SAMPLES_FOR_COALESCING, sti, m, ro, co, n);
#endif
		return MAX_COALESCING_SIZE;
	}
	else if (n < MIN_SAMPLES_FOR_COALESCING) {
#ifdef DEBUG_MODE
		printf("No need for coalescing. [sti=%lld, m=%lld, ro=%lld, co=%lld, n=%lld]\n", sti, m, ro, co, n);
#endif
		return 0;
	}
	else if (n > MAX_COALESCING_SIZE) {
#ifdef DEBUG_MODE
		printf("CRITICAL: Need to coalesce more than the allocated space (%lld). [sti=%lld, m=%lld, ro=%lld, co=%lld, n=%lld]\n", MAX_COALESCING_SIZE, sti, m, ro, co, n);
#endif
		return MAX_COALESCING_SIZE;
	}

	return ceil(n);
}

void* __mtsp_RuntimeThreadCode(void* params) {
	//===-------- Initialize VTune/libittnotify related stuff ----------===//
	__itt_thread_set_name("MTSPRuntime");

	stick_this_thread_to_core("RuntimeThread", __MTSP_RUNTIME_THREAD_CORE__);
	kmp_task* task = nullptr;
	kmp_uint64 iterations = 0;
	kmp_uint64 BatchSize = (64 - 1);	// should be 2^N - 1

	/// Used for task coalescing
	__curCoalesceSize = 0;
	__curTargetCoalescingSize = 0;
	kmp_routine_entry prevRoutine = nullptr;
	kmp_task* coalescedTask = nullptr;

	__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_RT_Main_Loop);
	while (true) {


		/// -------------------------------------------------------------------------------
		/// Check if the execution of a task has been completed
		__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_RT_Check_Del);
		if (RetirementQueue.try_deq(&task)) {
			updateAverageTaskSize((kmp_uint64) task->routine, task->metadata->taskSize);

			removeFromTaskGraph(task);
		}
		__itt_task_end(__itt_mtsp_domain);



		/// -------------------------------------------------------------------------------
		/// Check if there is any request for new thread creation
		__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_RT_Check_Add);
		if (freeSlots[0] > 0 && submissionQueue.try_deq(&task)) {
			/// Obtain an ID for the new task
			task->metadata->taskgraph_slot_id = freeSlots[ freeSlots[0] ];
			freeSlots[0]--;

			kmp_uint64 taskAddr = (kmp_uint64) task->routine;

			/// Did we increased the streak?
			if (task->routine == prevRoutine) {
				/// Before anything save the task that we just got
				coalescedTask->metadata->coalesced[__curCoalesceSize] = task;
				__curCoalesceSize ++;

				/// Did we reach the maximum size of the coalescing? If yes, we create the
				/// coalesced task and reset the coalescing size
				if (__curCoalesceSize >= __curTargetCoalescingSize) {
					/// create the coalesced task and add it to the task graph
					addCoalescedTask(coalescedTask);

					/// restart coalescing
					__curCoalesceSize = 0;
					coalescedTask = new kmp_task();
					coalescedTask->metadata = new _mtsp_task_metadata();
				}
			}
			else {
				/// The code enter heres the first time that we start to coalesce, because
				/// prevRoutine == nullptr and task->routine is != nullptr.
				/// 
				/// The program also enters here when we need to start a new streak.


				/// Add the pending tasks to the Task Graph
				if (__curCoalesceSize > 0) {
					/// create the coalesced task and add it to the task graph
					addCoalescedTask(coalescedTask);

					/// restart coalescing
					__curCoalesceSize = 0;
					coalescedTask = new kmp_task();
					coalescedTask->metadata = new _mtsp_task_metadata();
				}

				/// If we did not execute this kind of task a sufficient number of times to
				/// determine an average size -> do not try any coalescing.
				if (taskSize.find(taskAddr) == taskSize.end() || taskSize[taskAddr].first < MIN_SAMPLES_FOR_COALESCING) {
					addToTaskGraph( task );
					prevRoutine = nullptr;
				}
				else {
					/// Ok, so we have sufficient information. If we are just starting a coalesce
					/// we need to determine the targetCoalesceSize.
					__curTargetCoalescingSize = howManyShouldBeCoalesced(taskAddr);

					if (__curTargetCoalescingSize == 0) {
						addToTaskGraph( task );
						coalescedTask = nullptr;
						prevRoutine = nullptr;
						__curCoalesceSize = 0;

						//__mtsp_dumpTaskGraphToDot();
					}
					else {
						/// start the new coalescing streak
						coalescedTask = new kmp_task();
						coalescedTask->metadata = new _mtsp_task_metadata();
						coalescedTask->metadata->coalesced[0] = task;
						__curCoalesceSize = 1;
						prevRoutine = task->routine;
					}
				}
			}
		}
		else if (freeSlots[0] == 0) {
			if (__curCoalesceSize > 0) {
				/// create the coalesced task and add it to the task graph
				addCoalescedTask(coalescedTask);

				/// restart coalescing
				__curCoalesceSize = 0;
				coalescedTask = new kmp_task();
				coalescedTask->metadata = new _mtsp_task_metadata();
			}
		}

		__itt_task_end(__itt_mtsp_domain);




		/// -------------------------------------------------------------------------------
		/// Execute other "infrequent" bookkeeping tasks
		__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_RT_Check_Oth);
		iterations++;
		if ((iterations & BatchSize) == 0) {
			submissionQueue.fsh();

			if (__mtsp_threadWait && __curCoalesceSize > 0) {
				/// create the coalesced task and add it to the task graph
				addCoalescedTask(coalescedTask);

				/// start the new coalescing streak
				__curCoalesceSize =0;
				prevRoutine = nullptr;
			}

#ifdef MTSP_WORKSTEALING_RT
			if (RunQueue.cur_load() > __mtsp_numWorkerThreads) {		// may be we should consider the CT also
				__mtsp_RuntimeWorkSteal();
			}
#endif
		}
		__itt_task_end(__itt_mtsp_domain);
	}
	__itt_task_end(__itt_mtsp_domain);

	return 0;
}





