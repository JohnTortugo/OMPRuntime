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
#include <pthread.h>

#include "kmp.h"
#include "mtsp.h"
#include "hws.h"
#include "task_graph.h"
#include "scheduler.h"
#include "fe_interface.h"

kmp_uint64 tasksExecutedByCT = 0;
kmp_uint64 metadataRequestsNotServiced = 0;
volatile kmp_uint64 tasksExecutedByRT = 0;

std::map<kmp_critical_name*, bool> criticalRegions;
volatile bool crit_reg_lock = UNLOCKED;

SimpleQueue<kmp_task*, SUBMISSION_QUEUE_SIZE, SUBMISSION_QUEUE_CF> submissionQueue;




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
    	__itt_thread_set_name("ControlThread");

		__mtsp_initialize();
    }
	RELEASE(&__mtsp_lock_initialized);


    // Capture the parameters and add them to a void* array
    va_start(ap, microtask);
    for(i=0; i < argc; i++) { *argv++ = va_arg(ap, void *); }
	va_end(ap);

	__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_CT_Fork_Call);

	// This is "global_tid", "local_tid" and "pointer to array of captured parameters"
    (microtask)(&tid, &tid, argvcp[0]);

	// At the end of each "parallel" construct there is a barrier
    while (__mtsp_inFlightTasks > 0);

    __itt_task_end(__itt_mtsp_domain);
}

kmp_task* __kmpc_omp_task_alloc(ident *loc, kmp_int32 gtid, kmp_int32 pflags, kmp_uint32 sizeof_kmp_task_t, kmp_uint32 sizeof_shareds, kmp_routine_entry task_entry) {
	__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_CT_Task_Alloc);

	kmp_uint32 shareds_offset  = sizeof(kmp_taskdata) + sizeof_kmp_task_t;
	kmp_int32 sizeOfMetadata = sizeof(mtsp_task_metadata);

    kmp_taskdata* taskdata = (kmp_taskdata*) malloc(shareds_offset + sizeof_shareds + sizeOfMetadata);

    kmp_task* task = KMP_TASKDATA_TO_TASK(taskdata);

#if DEBUG_MODE
	printf("Task [%d] (Thread %d) is submitting a new child task.\n", idOfCurrentTask[threadId], threadId);
#endif

    task->shareds  = (sizeof_shareds > 0) ? &((char *) taskdata)[shareds_offset] : NULL;
    task->routine  = task_entry;
    task->metadata = (_mtsp_task_metadata*) &((char *) taskdata)[shareds_offset + sizeof_shareds];
    task->metadata->parentTaskId = idOfCurrentTask[threadId];
    task->metadata->parentReseted = false;
    task->metadata->numDirectChild = 0;
	task->metadata->globalTaskId = -1;

    __itt_task_end(__itt_mtsp_domain);
    return task;
}

kmp_int32 __kmpc_omp_task_with_deps(ident* loc, kmp_int32 gtid, kmp_task* new_task, kmp_int32 ndeps, kmp_depend_info* dep_list, kmp_int32 ndeps_noalias, kmp_depend_info* noalias_dep_list) {
	__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_CT_Task_With_Deps);

	// stores a pointer to the child task in the parent task.
	// If the current task is < 0 then the current executing code is the control thread
	if (idOfCurrentTask[gtid] >= 0) {
		ATOMIC_ADD(&tasks[idOfCurrentTask[gtid] ]->metadata->numDirectChild, 1);
		ACQUIRE(&pcGraphLock);
		pcGraph[idOfCurrentTask[gtid]]->insert(new_task);
		RELEASE(&pcGraphLock);
	}
	else {
		ATOMIC_ADD(&__ControlThreadDirectChild, 1);
	}

	__mtsp_addNewTask(new_task, ndeps, dep_list);

	__itt_task_end(__itt_mtsp_domain);

	return 0;
}

void work() {
	kmp_task* taskToExecute = nullptr;

	if (RunQueue.try_deq(&taskToExecute)) {
		kmp_int16 taskgraph_slot_id = idOfCurrentTask[threadId];

		idOfCurrentTask[threadId] = taskToExecute->metadata->taskgraph_slot_id;

		// Start execution of the task
		(*(taskToExecute->routine))(0, taskToExecute);

		idOfCurrentTask[threadId] = taskgraph_slot_id;

		// Inform that this task has finished execution
		RetirementQueue.enq(taskToExecute);
	}
}

kmp_int32 __kmpc_omp_taskwait(ident* loc, kmp_int32 gtid) {
#if DEBUG_MODE
	printf("Task %d (Thread %d) on taskwait.\n", idOfCurrentTask[threadId], gtid);
#endif

	if (idOfCurrentTask[threadId] < 0) {
		while (__ControlThreadDirectChild) {
			work();
		}
	}
	else {
		auto task = tasks[ idOfCurrentTask[threadId] ];
		while ( task->metadata->numDirectChild ) {
			work();
		}
	}
	
#if DEBUG_MODE
	printf("Task %d (Thread %d) exiting taskwait.\n", idOfCurrentTask[threadId], gtid);
#endif

	return 0;
}


void __kmpc_taskgroup(ident* loc, kmp_int32 gtid) {
}

void __kmpc_end_taskgroup(ident* loc, kmp_int32 gtid) {
	if (idOfCurrentTask[threadId] < 0) {
		while (__ControlThreadDirectChild) work();
	}
	else {
		while ( ! pcGraph[idOfCurrentTask[threadId]]->empty() ) work();
	}
}


void __kmpc_barrier(ident* loc, kmp_int32 gtid) {
#if DEBUG_MODE
	printf("***************** Executando uma barreira.\n");
#endif
}

kmp_int32 __kmpc_cancel_barrier(ident* loc, kmp_int32 gtid) {
#if DEBUG_MODE
	printf("__kmpc_cancel_barrier %s:%d\n", __FILE__, __LINE__);
#endif
    return 0;
}

kmp_int32 __kmpc_single(ident* loc, kmp_int32 gtid) {
#if DEBUG_MODE
	printf("__kmpc_single %s:%d\n", __FILE__, __LINE__);
#endif
	return TRY_ACQUIRE(&__mtsp_Single);
}

void __kmpc_end_single(ident* loc, kmp_int32 gtid) {
#if DEBUG_MODE
	printf("__kmpc_end_single %s:%d\n", __FILE__, __LINE__);
#endif

	__kmpc_omp_taskwait(loc, gtid);

	RELEASE(&__mtsp_Single);
	return ;
}

kmp_int32 __kmpc_master(ident* loc, kmp_int32 gtid) {
#if DEBUG_MODE
	printf("__kmpc_master %s:%d\n", __FILE__, __LINE__);
#endif
	return 1;
}

void __kmpc_end_master(ident* loc, kmp_int32 gtid) {
#if DEBUG_MODE
	printf("__kmpc_end_master %s:%d\n", __FILE__, __LINE__);
#endif
}

int __kmpc_global_thread_num(ident* loc) {
	return threadId;
}

int omp_get_num_threads() {
	return __mtsp_numWorkerThreads + 2;
}

int omp_get_thread_num() {
	return threadId;
}

void __kmpc_critical(ident* loc, kmp_int32 gtid, kmp_critical_name* name) {
	ACQUIRE(&crit_reg_lock);

	if (criticalRegions.find(name) == criticalRegions.end())
		criticalRegions[name] = false;

	RELEASE(&crit_reg_lock);

#if DEBUG_MODE
	printf("Begin Critical Sec [%p].\n", name);
#endif
	ACQUIRE( &criticalRegions[name] );
}


void __kmpc_end_critical(ident* loc, kmp_int32 gtid, kmp_critical_name* name) {
#if DEBUG_MODE
	printf("End Critical Sec [%p].\n", name);
#endif
	RELEASE( &criticalRegions[name] );
}
