#ifndef __MTSP_HEADER
	#define __MTSP_HEADER 1

	#include "kmp.h"
	#include "ittnotify.h"
	#include "task_graph.h"

	#include <pthread.h>


	//===----------------------------------------------------------------------===//
	//
	// Start of MTSP configuration directives
	//
	//===----------------------------------------------------------------------===//

	/// Activate (1) or deactivate (0) printf-based debugging
	#ifndef DEBUG_ENABLED
		#define	DEBUG_ENABLED	0
	#endif

	/// Activate (when undefined) or deactivate (when defined) ITTNotify Events
	/// #define	INTEL_NO_ITTNOFIFY_API	1

	/// Activate (1) or deactivate (0) timing MACROS
	#ifndef TIMING_ENABLED
		#define	TIMING_ENABLED	0
	#endif

	/// Makes eclipse happy so it does not complain about undefined NULL
	#ifndef NULL
		#define NULL	((void *)0)
	#endif

	/// Work distribution in a round robin way
	#define MTSP_WORK_DISTRIBUTION_RR	1

	/// Work distribution based on a finish token
	///#define MTSP_WORK_DISTRIBUTION_FT	1

	/// Work distribution based on run queue size
	///#define MTSP_WORK_DISTRIBUTION_QS	1

	/// Work distribution based on the load on the queue
	///#define MTSP_WORK_DISTRIBUTION_QL	1






	//===----------------------------------------------------------------------===//
	//
	// Global variables and their locks, etc.
	//
	//===----------------------------------------------------------------------===//

	/// Tells whether the MTSP runtime has already been initialized
	extern bool 			volatile __mtsp_initialized;

	/// This is the thread referencing the MTSP runtime thread
	extern pthread_t __mtsp_RuntimeThread;

	/// The five variables below represent:
	///		1. This represents the front end new tasks queue
	///		2. The number of dependences of each task
	///		3. A pointer to the list of dependences of each task
	///		4. An integer pointing to the next position to be written
	///		5. An integer pointing to the next position to be read
	extern kmp_task* 		volatile __mtsp_newTasksQueue[NEW_TASKS_QUEUE_SIZE];
	extern kmp_uint32 		volatile __mtsp_newTQDeps[NEW_TASKS_QUEUE_SIZE];
	extern kmp_depend_info* volatile __mtsp_newTQDepsPointers[NEW_TASKS_QUEUE_SIZE];
	extern kmp_uint32		volatile __mtsp_newTQReadIndex;
	extern kmp_uint32		volatile __mtsp_newTQWriteIndex;

	/// Maximum size of one taskMetadata slot. Tasks that require a metadata region
	/// larger than this will use a memory region returned by a call to std malloc.
	#define TASK_METADATA_MAX_SIZE 1024
	#define MAX_TASKMETADATA_SLOTS 4096

	/// Memory region from where new tasks metadata will be allocated.
	extern bool __mtsp_taskMetadataStatus[MAX_TASKMETADATA_SLOTS];
	extern char __mtsp_taskMetadataBuffer[MAX_TASKMETADATA_SLOTS][TASK_METADATA_MAX_SIZE];





	//===-------- These vars are used to interact with VTune ----------===//
	/// ITTNotify domain of events/tasks/frames
	extern __itt_domain* 		volatile __itt_mtsp_domain;

	/// Labels for itt-events representing enqueue and dequeue from the ready tasks queue
	extern __itt_string_handle* volatile __itt_ReadyQueue_Dequeue;
	extern __itt_string_handle* volatile __itt_ReadyQueue_Enqueue;

	/// Labels for itt-events representing enqueue and dequeue from the new tasks queue
	extern __itt_string_handle* volatile __itt_New_Tasks_Queue_Dequeue;
	extern __itt_string_handle* volatile __itt_New_Tasks_Queue_Enqueue;
	extern __itt_string_handle* volatile __itt_New_Tasks_Queue_Copy;
	extern __itt_string_handle* volatile __itt_New_Tasks_Queue_Full;

	/// Labels for itt-events representing enqueue and dequeue from the finished tasks queue
	extern __itt_string_handle* volatile __itt_Finished_Tasks_Queue_Dequeue;
	extern __itt_string_handle* volatile __itt_Finished_Tasks_Queue_Enqueue;

	/// Labels for itt-events representing periods where the control thread was waiting in a taskwait barrier
	extern __itt_string_handle* volatile __itt_Control_Thread_Barrier_Wait;

	/// Labels for itt-events representing periods where an worker thread was waiting in a taskwait barrier
	extern __itt_string_handle* volatile __itt_Worker_Thread_Barrier;

	/// Label for itt-events representing periods where an worker thread was waiting for tasks to execute
	extern __itt_string_handle* volatile __itt_Worker_Thread_Wait_For_Work;

	/// Labels for itt-events representing periods where an worker thread was executing a task
	extern __itt_string_handle* volatile __itt_Task_In_Execution;

	/// Labels for itt-events representing periods where a new task was being added/deleted to/from the task graph
	extern __itt_string_handle* volatile __itt_Add_Task_To_TaskGraph;
	extern __itt_string_handle* volatile __itt_Del_Task_From_TaskGraph;

	/// Labels for itt-events representing periods where the dependence checker was checking/releasing dependences
	extern __itt_string_handle* volatile __itt_Checking_Dependences;
	extern __itt_string_handle* volatile __itt_Releasing_Dependences;

	/// Labels for itt-events representing periods where the control thread was executing task_alloc
	extern __itt_string_handle* volatile __itt_Task_Alloc;

	/// Labels for itt-events representing periods where the control thread was executing task_with_deps
	extern __itt_string_handle* volatile __itt_Task_With_Deps;




	//===-------- Locks used to control access to the variables above ----------===//

	/// Used to control acess the __mtsp_initialized
	extern unsigned char volatile __mtsp_lock_initialized;


	/// The same lock is used to control all variables related to the new tasks queue
	extern unsigned char volatile __mtsp_lock_newTasksQueue;






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
	#define __MTSP_WORKER_THREAD_BASE_CORE__	2




	/*!
	 * Some of the defines below use intrinsics relative to GCC/G++:
	 * https://gcc.gnu.org/onlinedocs/gcc-5.1.0/gcc/_005f_005fsync-Builtins.html
	 *
	 */
	#define	TRY_ACQUIRE(ptr)		__sync_bool_compare_and_swap(ptr, UNLOCKED, LOCKED)
	#define	ACQUIRE(ptr)			while (__sync_bool_compare_and_swap(ptr, UNLOCKED, LOCKED) == false)
	#define RELEASE(ptr)			__sync_bool_compare_and_swap(ptr, LOCKED, UNLOCKED)

	#define ATOMIC_ADD(ptr, val)	__sync_add_and_fetch(ptr, val)
	#define ATOMIC_SUB(ptr, val)	__sync_sub_and_fetch(ptr, val)
	#define ATOMIC_AND(ptr, val)	__sync_and_and_fetch(ptr, val)
	#define ATOMIC_OR(ptr, val)		__sync_or_and_fetch(ptr, val)

#endif
