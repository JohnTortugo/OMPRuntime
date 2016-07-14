/**
 * @file 	mtsp.h
 * @brief 	This file contains functions and variables that are used all over the project.
 *
 * This file contains the prototypes of functions and variables used all over the 
 * system, but mostly by the runtime thread itself. In this file there is also a
 * set of preprocessor defines used to specify the size of the several queues and
 * the task graph.
 */

#ifndef __MTSP_HEADER
	#define __MTSP_HEADER 1

	#include "kmp.h"

#if USE_ITTNOTIFY
	#include "ittnotify.h"
#endif

	#include <pthread.h>
	#include <vector>
	#include <map>
	#include <set>
	#include <utility>
	#include <iostream>
	#include <iomanip>
	#include <cassert>
	#include "tioglib.h"


	//===----------------------------------------------------------------------===//
	//
	// Start of MTSP configuration directives
	//
	//===----------------------------------------------------------------------===//

	/// Enable or Disable security checks (i.e., overflow on queues, etc.)
	#define DEBUG_MODE						0

	/// Enable the exportation of the whole task graph to .dot
	/// remember to reserve a large space for task graph, submission queue, run queue, etc.
///	#define TG_DUMP_MODE					1

	/// Represents the maximum number of tasks that can be stored in the task graph
	#define MAX_TASKS 					     		       8192
	#define MAX_DEPENDENTS						  	  MAX_TASKS

	/// Represents the maximum number of tasks in the Submission Queue
	#define SUBMISSION_QUEUE_SIZE			        4*MAX_TASKS
	#define SUBMISSION_QUEUE_BATCH_SIZE						  4
	#define SUBMISSION_QUEUE_CF	  			   				 50

	/// Represents the maximum number of tasks in the Run Queue
	#define RUN_QUEUE_SIZE							  MAX_TASKS
	#define RUN_QUEUE_CF			    		 			 50

	/// Represents the maximum number of tasks in the Retirement Queue
	#define RETIREMENT_QUEUE_SIZE					4*MAX_TASKS

	/// Maximum size of one taskMetadata slot. Tasks that require a metadata region
	/// larger than this will use a memory region returned by a call to std malloc.
	#define TASK_METADATA_MAX_SIZE  								   1024

	/// Number of task metadata slots
	#define MAX_TASKMETADATA_SLOTS 		(MAX_TASKS + SUBMISSION_QUEUE_SIZE)

	#define BRIDGE_MODE				(getenv("MTSP_BRIDGE_MODE") != NULL)

	/// Just global constants recognized as \c LOCKED and \c UNLOCKED
	#define LOCKED															1
	#define UNLOCKED														0



	/**
	 * The defines below are used as wrappers to GCC intrinsic functions. These
	 * are all related to atomic operations supported in x86-64.
	 * Some of the defines below use intrinsics relative to GCC/G++:
	 * https://gcc.gnu.org/onlinedocs/gcc-5.1.0/gcc/_005f_005fsync-Builtins.html
	 */
	#define	TRY_ACQUIRE(ptr)		__sync_bool_compare_and_swap(ptr, UNLOCKED, LOCKED)
	#define	ACQUIRE(ptr)			while (__sync_bool_compare_and_swap(ptr, UNLOCKED, LOCKED) == false)
	#define RELEASE(ptr)			__sync_bool_compare_and_swap(ptr, LOCKED, UNLOCKED)
	#define CAS(ptr, val1, val2)	__sync_bool_compare_and_swap(ptr, val1, val2)


	#define ATOMIC_ADD(ptr, val)	__sync_add_and_fetch(ptr, val)
	#define ATOMIC_SUB(ptr, val)	__sync_sub_and_fetch(ptr, val)
	#define ATOMIC_AND(ptr, val)	__sync_and_and_fetch(ptr, val)
	#define ATOMIC_OR(ptr, val)		__sync_or_and_fetch(ptr, val)

	#define ANSI_COLOR_RED     "\x1b[31m"
	#define ANSI_COLOR_GREEN   "\x1b[32m"
	#define ANSI_COLOR_YELLOW  "\x1b[33m"
	#define ANSI_COLOR_BLUE    "\x1b[34m"
	#define ANSI_COLOR_MAGENTA "\x1b[35m"
	#define ANSI_COLOR_CYAN    "\x1b[36m"
	#define ANSI_COLOR_RESET   "\x1b[0m"




	//===----------------------------------------------------------------------===//
	//
	// Global variables and their locks, etc.
	//
	//===----------------------------------------------------------------------===//

	/// Tells whether the MTSP runtime has already been initialized or not.
	extern bool volatile __mtsp_initialized;

	/// This variable is used as a lock. The lock is used when initializing the MTSP runtime.
	extern unsigned char volatile __mtsp_lock_initialized;

	extern __thread unsigned int threadId;

	/// This variable is used as a lock. The lock is used in the \c kmpc_omp_single 
	extern bool volatile __mtsp_Single;

	/// This is the thread variable for the MTSP runtime thread.
	extern pthread_t __mtsp_RuntimeThread;

	/// This is a global counter used to give an unique identifier to all tasks
	/// executed by the system.
	extern kmp_uint32 __mtsp_globalTaskCounter;

	/// This is used together with \c __mtsp_globalTaskCounter for assigning/dumping
	/// the global unique ID of each task in the system.
	extern kmp_uint32 lastPrintedTaskId;

	/// Memory region from where new tasks metadata will be allocated.
	extern char __mtsp_taskMetadataBuffer[MAX_TASKMETADATA_SLOTS][TASK_METADATA_MAX_SIZE];

	/// Status of each slot in the \c __mtsp_taskMetadataBuffer
	extern volatile bool __mtsp_taskMetadataStatus[MAX_TASKMETADATA_SLOTS];


	/////////////////////////////////////////////////////////////////////////////////////
	/////////////////////////////////////////////////////////////////////////////////////
	/////////////////////////////////////////////////////////////////////////////////////
	/////////////////////////////////////////////////////////////////////////////////////
	// These vars are used to interact with VTune
#if ! USE_ITTNOTIFY
	typedef struct ___itt_domain
	{
		volatile int flags; /*!< Zero if disabled, non-zero if enabled. The meaning of different non-zero values is reserved to the runtime */
		const char* nameA;  /*!< Copy of original name in ASCII. */
		void* nameW;
		int   extra1; /*!< Reserved to the runtime */
		void* extra2; /*!< Reserved to the runtime */
		struct ___itt_domain* next;
	} __itt_domain;

	typedef struct ___itt_string_handle
	{
		const char* strA; /*!< Copy of original string in ASCII. */
		void* strW;
		int   extra1; /*!< Reserved. Must be zero   */
		void* extra2; /*!< Reserved. Must be zero   */
		struct ___itt_string_handle* next;
	} __itt_string_handle;

	typedef struct ___itt_id
	{
		unsigned long long d1, d2, d3;
	} __itt_id;

	static const __itt_id __itt_null = { 0, 0, 0 };

	extern void __itt_task_end(const __itt_domain *domain);
	extern void __itt_task_begin(const __itt_domain *domain, __itt_id taskid, __itt_id parentid, __itt_string_handle *name);
	extern void __itt_thread_set_name(const char    *name);
	extern __itt_domain* __itt_domain_create(const char *name);
	extern __itt_string_handle* __itt_string_handle_create(const char *name);
#endif

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
	extern __itt_string_handle* volatile __itt_Releasing_Dep_Reader;

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

	extern __itt_string_handle* volatile __itt_Task_In_Execution;

	/// Label for itt-events representing periods where an thread stealing tasks
	extern __itt_string_handle* volatile __itt_Task_Stealing;

	/// Label for itt-events representing the blocking of a access to the spsc queue
	extern __itt_string_handle* volatile __itt_SPSC_Enq_Blocking;
	extern __itt_string_handle* volatile __itt_SPSC_Deq_Blocking;

	extern __itt_string_handle* volatile __itt_SPSC_Enq;
	extern __itt_string_handle* volatile __itt_SPSC_Deq;

	/// Labels for the runtime events. Check for adding/removing tasks from the queues and the graph.
	/// and others stuffs
	extern __itt_string_handle* volatile __itt_RT_Main_Loop;
	extern __itt_string_handle* volatile __itt_RT_Check_Del;
	extern __itt_string_handle* volatile __itt_RT_Check_Add;
	extern __itt_string_handle* volatile __itt_RT_Check_Oth;

	/////////////////////////////////////////////////////////////////////////////////////
	/////////////////////////////////////////////////////////////////////////////////////
	/////////////////////////////////////////////////////////////////////////////////////
	/////////////////////////////////////////////////////////////////////////////////////
	


	// For reading the RDTSCP counter in the x86 architecture. You must call these
	// functions in the order "beg" (target_region_of_interest) "end".
	/// Used to read MTSP at the beggining of a region
	unsigned long long end_read_mtsp();

	/// Used to read MTSP at the beggining of a region
	unsigned long long beg_read_mtsp();

	/**
	 * This function is the one responsible for initializing the MTSP runtime itself.
	 * It is called from inside the \c __kmpc_fork_call by the control thread the first
	 * time it encounters a parallel region. This function will call another functions
	 * that will initialize the Dependence Manager, Task Graph Manager, Scheduler and
	 * create the Worker Threads according to the environment variable OMP_NUM_THREADS.
	 */
	void __mtsp_initialize();

	/**
	 * This function is the one that actually enqueue a task in the submission queue.
	 * It is executed by the control thread.
	 *
	 * @param newTask		A pointer to a task structure returned by \c kmpc_omp_task_alloc
	 * @param ndeps			The number of memory addresses this task access
	 * @param depList		The list of memory addresses this task access
	 */
	void __mtsp_addNewTask(kmp_task* newTask, kmp_uint32 ndeps, kmp_depend_info* depList);

	/**
	 * This function is the one executed by the runtime thread itself. The parameter is
	 * just boiler plate code used by the pthread API and is not used for anything. The
	 * body of the function is mostly composed of a \c while loop with two parts: 1) removing
	 * tasks from the retirement queue and from the task graph; 2) removing task creation
	 * requests from the submission queue and adding to the task graph.
	 *
	 * @param	params		Used for nothing.
	 * @return 				nullptr.
	 */
	void* __mtsp_RuntimeThreadCode(void* params);

#endif
