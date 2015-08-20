#ifndef __MTSP_HEADER
	#define __MTSP_HEADER 1

	#include "kmp.h"
	#include "ittnotify.h"
	#include "task_graph.h"
	#include "ThreadedQueue.h"
	#include "fe_interface.h"

	#include <pthread.h>


	//===----------------------------------------------------------------------===//
	//
	// Start of MTSP configuration directives
	//
	//===----------------------------------------------------------------------===//

	/// Activate (when undefined) or deactivate (when defined) ITTNotify Events
	/// #define	INTEL_NO_ITTNOFIFY_API	1

	/// Work distribution in a round robin way
	#define MTSP_WORK_DISTRIBUTION_RR		1

	/// Work distribution based on a finish token
	///#define MTSP_WORK_DISTRIBUTION_FT	1

	/// Work distribution based on run queue size
	///#define MTSP_WORK_DISTRIBUTION_QS	1

	/// Work distribution based on the load on the queue
	///#define MTSP_WORK_DISTRIBUTION_QL	1

	/// Enable this define to use one retirement queue per worker thread
	///#define MTSP_MULTIPLE_RETIRE_QUEUES		1

	/// Enable this define to use one submission queue per worker thread
	#define MTSP_MULTIPLE_RUN_QUEUES 		1

	/// Uncomment if you want the worker threads to steal work
	///#define MTSP_WORKSTEALING_WT			1

	/// Uncomment if you want the CT to steal work
	#define MTSP_WORKSTEALING_CT			1

	///#define MTSP_DUMP_STATS					1

	/// Enable the use of critical path prediction and the creation of
	/// four run queues. Run[0] has the highest priority.
	#define MTSP_CRITICAL_PATH_PREDICTION 	1



	/// This is the size of the color vector used by the critical path
	/// prediction algorithm
	#define COLOR_VECTOR_SIZE 		((2 * MAX_TASKS) + 2)

	/// Maximum size of one taskMetadata slot. Tasks that require a metadata region
	/// larger than this will use a memory region returned by a call to std malloc.
	#define TASK_METADATA_MAX_SIZE 1024
	#define MAX_TASKMETADATA_SLOTS 4096




	//===----------------------------------------------------------------------===//
	//
	// Global variables and their locks, etc.
	//
	//===----------------------------------------------------------------------===//

	/// Tells whether the MTSP runtime has already been initialized
	extern bool volatile __mtsp_initialized;

	/// This is the thread referencing the MTSP runtime thread
	extern pthread_t __mtsp_RuntimeThread;

	extern bool volatile __mtsp_Single;


	/// Used in computation of the critical path. This is used to chose which
	/// color vector we are creating for the next window
	extern bool __mtsp_ColorVectorIdx;
	extern kmp_uint16 __mtsp_ColorVector[COLOR_VECTOR_SIZE];

	/// This tells the condition of the node in the task graph. For an index "i", if
	/// status[i] == 0 means that there is no current node at position "i", if status[i] == n
	/// it means that the node has "n-1" dependants.
	extern kmp_uint16	__mtsp_NodeStatus[MAX_TASKS];

	/// This stores the color of the node, which is a value ranging from 0 to MAX_TASKS
	extern kmp_uint16	__mtsp_NodeColor[MAX_TASKS];

	/// Memory region from where new tasks metadata will be allocated.
	extern bool __mtsp_taskMetadataStatus[MAX_TASKMETADATA_SLOTS];
	extern char __mtsp_taskMetadataBuffer[MAX_TASKMETADATA_SLOTS][TASK_METADATA_MAX_SIZE];





	//===-------- These vars are used to interact with VTune ----------===//
	/// ITTNotify domain of events/tasks/frames
	extern __itt_domain* 		volatile __itt_mtsp_domain;

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

	/// Labels for itt-events representing periods where the control thread was waiting in a taskwait barrier
	extern __itt_string_handle* volatile __itt_CT_Barrier_Wait;

	/// Labels for itt-events representing periods where an worker thread was waiting in a taskwait barrier
	extern __itt_string_handle* volatile __itt_WT_Barrier;

	/// Label for itt-events representing periods where an worker thread was waiting for tasks to execute
	extern __itt_string_handle* volatile __itt_WT_Wait_For_Work;

	/// Label for itt-events representing periods where an worker thread was executing a task
	extern __itt_string_handle* volatile __itt_Task_In_Execution;

	/// Label for itt-events representing periods where an thread stealing tasks
	extern __itt_string_handle* volatile __itt_Task_Stealing;

	/// Labels for itt-events representing periods where a new task was being added/deleted to/from the task graph
	extern __itt_string_handle* volatile __itt_TaskGraph_Add;
	extern __itt_string_handle* volatile __itt_TaskGraph_Del;

	/// Labels for itt-events representing periods where the dependence checker was checking/releasing dependences
	extern __itt_string_handle* volatile __itt_Checking_Dependences;
	extern __itt_string_handle* volatile __itt_Releasing_Dependences;

	/// Labels for itt-events representing periods where the control thread was executing task_alloc
	extern __itt_string_handle* volatile __itt_CT_Task_Alloc;

	/// Labels for itt-events representing periods where the control thread was executing task_with_deps
	extern __itt_string_handle* volatile __itt_CT_Task_With_Deps;




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
