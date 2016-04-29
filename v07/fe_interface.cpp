/**
 * @file 	fe_interface.h
 * @brief 	This file contains functions and variables related the MTSP frontend.
 *
 */

#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <map>

#include "pthread.h"
#include "kmp.h"
#include "mtsp.h"
#include "task_graph.h"
#include "scheduler.h"
#include "fe_interface.h"
#include "hws.h"

kmp_uint64 tasksExecutedByCT = 0;
kmp_uint64 metadataRequestsNotServiced = 0;
kmp_uint64 tasksAdded = 0;

SPSCQueue<kmp_task*, SUBMISSION_QUEUE_SIZE, SUBMISSION_QUEUE_BATCH_SIZE, SUBMISSION_QUEUE_CF> submissionQueue;

void __kmpc_fork_call(ident *loc, kmp_int32 argc, kmpc_micro microtask, ...) {
	int i 			= 0;
	int tid 		= 0;
    void** argv 	= (void **) malloc(sizeof(void *) * argc);
    void** argvcp 	= argv;
    va_list ap;

    // Check whether the runtime library is initialized
    ACQUIRE(&__mtsp_lock_initialized);
    if (__mtsp_initialized == false) {
    	// Init random number generator. We use this on the work stealing part.
    	srandom( time(NULL) );

    	__mtsp_initialized = true;
    	__mtsp_Single = false;

    	//===-------- The thread that initialize the runtime is the Control Thread ----------===//
#ifdef __VTPROF
    	__itt_thread_set_name("ControlThread");
#endif

    	__mtsp_initialize();
    }

//	__mtsp_reInitialize();
	RELEASE(&__mtsp_lock_initialized);


    // Capture the parameters and add them to a void* array
    va_start(ap, microtask);
    for(i=0; i < argc; i++) { *argv++ = va_arg(ap, void *); }
	va_end(ap);

#ifdef __VTPROF
	__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_CT_Fork_Call);
#endif

	// This is "global_tid", "local_tid" and "pointer to array of captured parameters"
    (microtask)(&tid, &tid, argvcp[0]);

    // Comment below if you assume the compiler or the programmer added a #pragma taskwait at the end of parallel region.
    __kmpc_omp_taskwait(nullptr, 0);
    // printf("Expecting barrier....\n");

#ifdef __VTPROF
    __itt_task_end(__itt_mtsp_domain);
#endif
}

kmp_taskdata* allocateTaskData(kmp_uint32 numBytes, kmp_int16* memorySlotId) {
//	if (numBytes > TASK_METADATA_MAX_SIZE) {
//#ifdef DEBUG_MODE
//		printf("Request for metadata slot to big: %u\n", numBytes);
//#endif
//		metadataRequestsNotServiced++;
//		return (kmp_taskdata*) malloc(numBytes);
//	}
//	else {
//		for (int i=0; i<MAX_TASKMETADATA_SLOTS; i++) {
//			if (__mtsp_taskMetadataStatus[i] == false) {
//				__mtsp_taskMetadataStatus[i] = true;
//				*memorySlotId = i;
//				return  (kmp_taskdata*) __mtsp_taskMetadataBuffer[i];
//			}
//		}
//	}
//
//#ifdef DEBUG_MODE
//	static int counter = 0;
//	fprintf(stderr, "[%s:%d] There was not sufficient task metadata slots. %d\n", __FUNCTION__, __LINE__, counter++);
//#endif
//
//	metadataRequestsNotServiced++;

	// Lets take the "safe" side here..
	return (kmp_taskdata*) malloc(numBytes);
}

kmp_task* __kmpc_omp_task_alloc(ident *loc, kmp_int32 gtid, kmp_int32 pflags, kmp_uint32 sizeof_kmp_task_t, kmp_uint32 sizeof_shareds, kmp_routine_entry task_entry) {
#ifdef __VTPROF
	__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_CT_Task_Alloc);
#endif

	kmp_uint32 shareds_offset  = sizeof(kmp_taskdata) + sizeof_kmp_task_t;
	kmp_int32 sizeOfMetadata = sizeof(mtsp_task_metadata);
	kmp_int16 memorySlotId = -1;

    kmp_taskdata* taskdata = allocateTaskData(shareds_offset + sizeof_shareds + sizeOfMetadata, &memorySlotId);

    kmp_task* task = KMP_TASKDATA_TO_TASK(taskdata);

    task->shareds  = (sizeof_shareds > 0) ? &((char *) taskdata)[shareds_offset] : NULL;
    task->routine  = task_entry;
    task->metadata = (_mtsp_task_metadata*) &((char *) taskdata)[shareds_offset + sizeof_shareds];
	task->metadata->globalTaskId = -1;
	task->metadata->coalesceSize = 0;
    task->metadata->metadata_slot_id = memorySlotId;

#ifdef __VTPROF
    __itt_task_end(__itt_mtsp_domain);
#endif
    return task;
}

kmp_int32 __kmpc_omp_task_with_deps(ident* loc, kmp_int32 gtid, kmp_task* new_task, kmp_int32 ndeps, kmp_depend_info* dep_list, kmp_int32 ndeps_noalias, kmp_depend_info* noalias_dep_list) {
#ifdef __VTPROF
	__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_CT_Task_With_Deps);
#endif

#ifdef SUBQUEUE_PATTERN
	static std::map<kmp_uint64, kmp_uint64> taskTypes;
	static kmp_uint64 counter=0; 

	// This holds the task function address
	// Mon Apr  4 12:55:03 BRT 2016
	kmp_uint64 addr = (kmp_uint64) new_task->routine;

	if (taskTypes.find(addr) == taskTypes.end())
		taskTypes[addr] = taskTypes.size();

	printf("%llu %u\n", counter++, taskTypes[addr]);
#endif
	
#ifdef __TRACE
	static FILE * fp;
	static bool first = true;
	unsigned long long packet;

	if (first)
	{
		first = false;
		fp = fopen("task_dump.txt", "w");
	}

	create_task_packet(packet, 0, (ndeps == 0), new_task->routine);
	fprintf(fp, "%llx\n", packet);

	for (kmp_int32 i=0; i<ndeps; i++) {
		unsigned char mode = dep_list[i].flags.in | (dep_list[i].flags.out << 1);

		create_dep_packet(packet, mode, (i == (ndeps-1)), dep_list[i].base_addr);
		fprintf(fp, "%llx\n", packet);
	}
#endif

	// Ask to add this task to the task graph
	__mtsp_addNewTask(new_task, ndeps, dep_list);

#ifdef __VTPROF
	__itt_task_end(__itt_mtsp_domain);
#endif
	return 0;
}

void steal_from_single_run_queue(bool just_a_bit) {
	kmp_task* taskToExecute = nullptr;
	kmp_uint16 myId = __mtsp_numWorkerThreads;

	// Counter for the total cycles spent per task
	unsigned long long start=0, end=0;

#ifdef __VTPROF
	__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_Task_Stealing);
#endif

	while (true) {
		if (just_a_bit) {
			if (RunQueue.cur_load() < RunQueue.cont_load() && submissionQueue.cur_load() < submissionQueue.cont_load()) {
#ifdef __VTPROF
				__itt_task_end(__itt_mtsp_domain);
#endif
				return;
			}
		}
		else {
			if (RunQueue.cur_load() < RunQueue.cont_load() && submissionQueue.cur_load() <= 0) {
#ifdef __VTPROF
				__itt_task_end(__itt_mtsp_domain);
#endif
				return;
			}
		}

		if (RunQueue.try_deq(&taskToExecute)) {

#ifdef __VTPROF
			 __itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_Task_In_Execution);
#endif

			start = beg_read_mtsp();

			// Start execution of the task
			(*(taskToExecute->routine))(0, taskToExecute);

			end = end_read_mtsp();

#ifdef __VTPROF
			__itt_task_end(__itt_mtsp_domain);
#endif

			taskToExecute->metadata->taskSize = (end - start);

			tasksExecutedByCT++;

			/// Inform that this task has finished execution
#ifdef __VTPROF
			__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_Retirement_Queue_Enqueue);
#endif
			RetirementQueue.enq(taskToExecute);
#ifdef __VTPROF
			__itt_task_end(__itt_mtsp_domain);
#endif
		}
	}

#ifdef __VTPROF
	__itt_task_end(__itt_mtsp_domain);
#endif
}


void barrierFinishCode() {
#ifdef __VTPROF
	__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_CT_Barrier_Wait);
#endif

	// Flush the current state of the submission queues..
	submissionQueue.fsh();

#ifdef MTSP_WORKSTEALING_CT
	steal_from_single_run_queue(false);
#endif

#ifdef TG_DUMP_MODE
	while (submissionQueue.cur_load() > 0);
	__mtsp_dumpTaskGraphToDot();
	__mtsp_activate_workers = true;
#endif

	// Reset the number of threads that have currently reached the barrier
	ATOMIC_AND(&__mtsp_threadWaitCounter, 0);

	// Tell threads that they should synchronize at a barrier
	ATOMIC_OR(&__mtsp_threadWait, 1);

	// Wait until all threads have reached the barrier
	while (__mtsp_threadWaitCounter != __mtsp_numWorkerThreads);

	// OK. Now all threads have reached the barrier. We now free then to continue execution
	ATOMIC_AND(&__mtsp_threadWait, 0);

	// Before we continue we need to make sure that all threads have "seen" the previous
	// updated value of threadWait
	while (__mtsp_threadWaitCounter != 0);

#ifdef TG_DUMP_MODE
	// delete: this is just to debug (temporarily)
	__mtsp_activate_workers = false;
#endif

#ifdef MTSP_DUMP_STATS
	printf("/ ---------------------------------------------------------------------------------\\\n");

#ifdef MTSP_WORKSTEALING_CT	
	printf("| %llu tasks were executed by the control thread.\n", tasksExecutedByCT);
#endif

#ifdef MTSP_WORKSTEALING_RT	
	printf("| %llu tasks were executed by the runtime thread.\n\n", tasksExecutedByRT);
#endif

	printf("| Number of necessary coalesces %llu\n", __coalNecessary);
	printf("| Number of unnecessary coalesces %llu\n", __coalUnnecessary);
	printf("| Number of impossible coalesces %llu\n", __coalImpossible);
	printf("| Number of overflowed coalesces %llu\n", __coalOverflowed);
	printf("| Number of successfull coalesces %llu\n", __coalSuccess);
	printf("| Number of failed coalesces %llu\n", __coalFailed);
	printf("| \n");

	for (auto& ts : realTasks) {
		auto taskAddr = ts.first;
		auto taskRtlAddr = (ts.first ^ (kmp_uint64) __mtsp_RuntimeThreadCode);
		auto taskColAddr = (ts.first ^ (kmp_uint64) saveCoalesce);
		auto taskMacAddr = (ts.first ^ (kmp_uint64) executeCoalesced);
		auto taskRclAddr = (taskMacAddr ^ (kmp_uint64) __mtsp_RuntimeThreadCode);

		auto tskTimes = taskSize[taskAddr].first;
		auto tskAverage = taskSize[taskAddr].second;

		auto rtlTimes = taskSize[taskRtlAddr].first;
		auto rtlAverage = taskSize[taskRtlAddr].second;

		auto rclTimes = taskSize[taskRclAddr].first;
		auto rclAverage = taskSize[taskRclAddr].second;

		auto colTimes = taskSize[taskColAddr].first;
		auto colAverage = taskSize[taskColAddr].second;

		auto macTimes = taskSize[taskMacAddr].first;
		auto macAverage = taskSize[taskMacAddr].second;


		std::cout << "| Task " 					 						 << std::hex << taskAddr << " executed " << std::dec << tskTimes << " times, taking on average " << tskAverage << " cycles to execute." << std::endl;
		std::cout << "| \t" << std::setw(25) << "Runtime-Ind for Task "  << std::hex << taskAddr << " executed " << std::dec << rtlTimes << " times, taking on average " << rtlAverage << " cycles to execute." << std::endl;
		std::cout << "| \t" << std::setw(25) << "Runtime-Col for Task "  << std::hex << taskAddr << " executed " << std::dec << rclTimes << " times, taking on average " << rclAverage << " cycles to execute." << std::endl;
		std::cout << "| \t" << std::setw(25) << "Coalescing for Task "   << std::hex << taskAddr << " executed " << std::dec << colTimes << " times, taking on average " << colAverage << " cycles to execute." << std::endl;
		std::cout << "| \t" << std::setw(25) << "Macrotask for Task "    << std::hex << taskAddr << " executed " << std::dec << macTimes << " times, taking on average " << macAverage << " cycles to execute." << std::endl;
	}

	printf("\\---------------------------------------------------------------------------------/\n\n\n");

#endif

#ifdef __VTPROF
	__itt_task_end(__itt_mtsp_domain);
#endif

}


kmp_int32 __kmpc_omp_taskwait(ident* loc, kmp_int32 gtid) {
	barrierFinishCode();
	return 0;
}

void __kmpc_barrier(ident* loc, kmp_int32 gtid) {
#ifdef DEBUG_MODE
	printf("***************** Executando uma barreira.\n");
#endif
}

kmp_int32 __kmpc_cancel_barrier(ident* loc, kmp_int32 gtid) {
#ifdef DEBUG_MODE
	printf("__kmpc_cancel_barrier %s:%d\n", __FILE__, __LINE__);
#endif
    return 0;
}

kmp_int32 __kmpc_single(ident* loc, kmp_int32 gtid) {
#ifdef DEBUG_MODE
	printf("__kmpc_single %s:%d\n", __FILE__, __LINE__);
#endif
	return TRY_ACQUIRE(&__mtsp_Single);
}

void __kmpc_end_single(ident* loc, kmp_int32 gtid) {
#ifdef DEBUG_MODE
	printf("__kmpc_end_single %s:%d\n", __FILE__, __LINE__);
#endif
	barrierFinishCode();
	RELEASE(&__mtsp_Single);
	return ;
}

kmp_int32 __kmpc_master(ident* loc, kmp_int32 gtid) {
#ifdef DEBUG_MODE
	printf("__kmpc_master %s:%d\n", __FILE__, __LINE__);
#endif
	return 1;
}

void __kmpc_end_master(ident* loc, kmp_int32 gtid) {
#ifdef DEBUG_MODE
	printf("__kmpc_end_master %s:%d\n", __FILE__, __LINE__);
#endif
}

int omp_get_num_threads() {
	return __mtsp_numWorkerThreads + 2;
}
