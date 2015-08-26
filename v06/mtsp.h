#ifndef __MTSP_HEADER
	#define __MTSP_HEADER 1

	#include "kmp.h"
	#include "ittnotify.h"

	#include <pthread.h>


	//===----------------------------------------------------------------------===//
	//
	// Start of MTSP configuration directives
	//
	//===----------------------------------------------------------------------===//

	/// Enable or Disable security checks (i.e., overflow on queues, etc.)
//	#define DEBUG_MODE						1

	/// Enable the exportation of the whole task graph to .dot
	/// remember to reserva a large space for task graph, submission queue, run queue, etc.
//	#define TG_DUMP_MODE					1

	/// Activate (when undefined) or deactivate (when defined) ITTNotify Events
//	#define	INTEL_NO_ITTNOFIFY_API			1

	/// Uncomment if you want the CT to steal work
	#define MTSP_WORKSTEALING_CT			1

	/// Uncomment if you want the RT to steal work
	#define MTSP_WORKSTEALING_RT			1

	/// Uncomment if you want to see some statistics at the end of
	#define MTSP_DUMP_STATS					1




	//===----------------------------------------------------------------------===//
	//
	// Global variables and their locks, etc.
	//
	//===----------------------------------------------------------------------===//

	/// Tells whether the MTSP runtime has already been initialized
	extern bool volatile __mtsp_initialized;

	extern bool volatile __mtsp_Single;


	/// This is the thread referencing the MTSP runtime thread
	extern pthread_t __mtsp_RuntimeThread;



	/// Represents the maximum number of tasks that can be stored in the task graph
	#define MAX_TASKS 					     		       4096
	#define MAX_DEPENDENTS						  	  MAX_TASKS

	/// Represents the maximum number of tasks in the "new tasks queue" in the front-end
	#define SUBMISSION_QUEUE_SIZE			 			   512
	#define SUBMISSION_QUEUE_BATCH_SIZE						 4
	#define SUBMISSION_QUEUE_CF	  			   				 5


	/// Represents the maximum number of tasks in the "new tasks queue" in the front-end
	#define RUN_QUEUE_SIZE							  MAX_TASKS
	#define RUN_QUEUE_CF			    					  1

	#define RETIREMENT_QUEUE_SIZE		MAX_TASKS

	/// Maximum size of one taskMetadata slot. Tasks that require a metadata region
	/// larger than this will use a memory region returned by a call to std malloc.
	#define TASK_METADATA_MAX_SIZE  									750
	#define MAX_TASKMETADATA_SLOTS 		(MAX_TASKS + SUBMISSION_QUEUE_SIZE)

	/// Memory region from where new tasks metadata will be allocated.
	extern volatile bool __mtsp_taskMetadataStatus[MAX_TASKMETADATA_SLOTS];
	extern char __mtsp_taskMetadataBuffer[MAX_TASKMETADATA_SLOTS][TASK_METADATA_MAX_SIZE];





	//===-------- These vars are used to interact with VTune ----------===//
	/// ITTNotify domain of events/tasks/frames
	extern __itt_domain* 		volatile __itt_mtsp_domain;

	/// Labels for itt-events representing the duration of a fork-call
	extern __itt_string_handle* volatile __itt_CT_Fork_Call;

	/// Labels for itt-events representing enqueue and dequeue from the ready tasks queue
	extern __itt_string_handle* volatile __itt_Run_Queue_Dequeue;
	extern __itt_string_handle* volatile __itt_Run_Queue_Enqueue;

	/// Labels for itt-events representing enqueue and dequeue from the new tasks queue
	extern __itt_string_handle* volatile __itt_Submission_Queue_Dequeue;
	extern __itt_string_handle* volatile __itt_Submission_Queue_Enqueue;
	extern __itt_string_handle* volatile __itt_Submission_Queue_Copy;
	extern __itt_string_handle* volatile __itt_Submission_Queue_Add;

	/// Labels for itt-events representing enqueue and dequeue from the finished tasks queue
	extern __itt_string_handle* volatile __itt_Retirement_Queue_Dequeue;
	extern __itt_string_handle* volatile __itt_Retirement_Queue_Enqueue;

	/// Labels for itt-events representing periods where a new task was being added/deleted to/from the task graph
	extern __itt_string_handle* volatile __itt_TaskGraph_Add;
	extern __itt_string_handle* volatile __itt_TaskGraph_Del;

	/// Labels for itt-events representing periods where the dependence checker was checking/releasing dependences
	extern __itt_string_handle* volatile __itt_Checking_Dependences;
	extern __itt_string_handle* volatile __itt_Releasing_Dependences;

	/// Labels for itt-events representing periods where the control thread was waiting in a taskwait barrier
	extern __itt_string_handle* volatile __itt_CT_Barrier_Wait;

	/// Labels for itt-events representing periods where the control thread was executing task_alloc
	extern __itt_string_handle* volatile __itt_CT_Task_Alloc;

	/// Labels for itt-events representing periods where the control thread was executing task_with_deps
	extern __itt_string_handle* volatile __itt_CT_Task_With_Deps;

	/// Labels for itt-events representing periods where an worker thread was waiting in a taskwait barrier
	extern __itt_string_handle* volatile __itt_WT_Barrier;
	extern __itt_string_handle* volatile __itt_WT_Serving_Steal;

	/// Label for itt-events representing periods where an worker thread was waiting for tasks to execute
	extern __itt_string_handle* volatile __itt_WT_Wait_For_Work;

	/// Label for itt-events representing periods where an worker thread was executing a task
	extern __itt_string_handle* volatile __itt_Task_In_Execution;

	/// Label for itt-events representing periods where an thread stealing tasks
	extern __itt_string_handle* volatile __itt_Task_Stealing;




	//===-------- Locks used to control access to the variables above ----------===//

	/// Used to control acess the __mtsp_initialized
	extern unsigned char volatile __mtsp_lock_initialized;







	//===----------------------------------------------------------------------===//
	//
	// Global functions prototype
	//
	//===----------------------------------------------------------------------===//

	int stick_this_thread_to_core(int core_id);

	void __mtsp_initialize();

	void __mtsp_addNewTask(kmp_task* newTask, kmp_uint32 ndeps, kmp_depend_info* depList);

	void* __mtsp_RuntimeThreadCode(void* params);







	//===----------------------------------------------------------------------===//
	//
	// Global directives to help with locks/atomics
	//
	//===----------------------------------------------------------------------===//

	#define LOCKED					1
	#define UNLOCKED				0

	#define __MTSP_MAIN_THREAD_CORE__			0
	#define __MTSP_RUNTIME_THREAD_CORE__		1




	/*!
	 * Some of the defines below use intrinsics relative to GCC/G++:
	 * https://gcc.gnu.org/onlinedocs/gcc-5.1.0/gcc/_005f_005fsync-Builtins.html
	 *
	 */
	#define	TRY_ACQUIRE(ptr)		__sync_bool_compare_and_swap(ptr, UNLOCKED, LOCKED)
	#define	ACQUIRE(ptr)			while (__sync_bool_compare_and_swap(ptr, UNLOCKED, LOCKED) == false)
	#define RELEASE(ptr)			__sync_bool_compare_and_swap(ptr, LOCKED, UNLOCKED)
	#define CAS(ptr, val1, val2)	__sync_bool_compare_and_swap(ptr, val1, val2)


	#define ATOMIC_ADD(ptr, val)	__sync_add_and_fetch(ptr, val)
	#define ATOMIC_SUB(ptr, val)	__sync_sub_and_fetch(ptr, val)
	#define ATOMIC_AND(ptr, val)	__sync_and_and_fetch(ptr, val)
	#define ATOMIC_OR(ptr, val)		__sync_or_and_fetch(ptr, val)

#endif
