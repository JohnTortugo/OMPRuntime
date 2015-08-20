#ifndef __MTSP_INTERFACE_HEADER
	#define __MTSP_INTERFACE_HEADER 1

#include "kmp.h"
#include "hws.h"
#include "ThreadedQueue.h"
#include <pthread.h>

/// Tells the maximum number of (!!)Bytes(!!) in the submission, run and retirement queue
#define MAX_TASKS						   				128
#define SUBMISSION_QUEUE_SIZE			 	(MAX_TASKS * 8)
#define RUN_QUEUE_SIZE						(MAX_TASKS * 8)
#define RETIREMENT_QUEUE_SIZE				(MAX_TASKS * 8)



extern pthread_t hwsThread;

extern bool __mtsp_initialized;

extern struct QDescriptor* __mtsp_SubmissionQueueDesc;
extern struct QDescriptor* __mtsp_RunQueueDesc;
extern struct QDescriptor* __mtsp_RetirementQueueDesc;


extern kmp_task* volatile tasks[MAX_TASKS];
extern SPSCQueue<kmp_uint16, MAX_TASKS*2, 4> freeSlots;







/**
 * This is the function responsible for initializing the systemC (HWS) module.
 */
void* __hws_init(void *);

/**
 * Tells whether hws is alive or not.
 */
extern volatile char hws_alive;


/**
 * Used to initialize the MTSP bridge. This allocate space for the submission,
 * run queues and retirement queues and sends their address to the HWS module.
 */
void __mtsp_bridge_init();




void __mtsp_enqueue_into_submission_queue(unsigned long long packet);


/**
* @brief	In the current implementation this function just parse the "variable argument" list
* 			and forward the execution to "\param kmpc_micro".
*
* 			This function were responsible for forking the execution of a microtask in
* 			several threads. The definition in the IRTL was at kmp_csupport.c:276.
*
* @param 	loc  		Source location of the omp construct.
* @param 	argc  		Total number of arguments in the ellipsis.
* @param 	microtask  	Pointer to callback routine (usually .omp_microtask) consisting of outlined "omp parallel" construct
* @param 	...  		Pointers to shared variables that aren't global
*/
void __kmpc_fork_call(ident *loc, kmp_int32 argc, kmpc_micro microtask, ...);



/**
* @brief	This function is responsible for allocating memory for storing task parameters and metadata.
*
*			The task structure, the task metadata and the task private/shared members are allocated with
*			just one call to malloc. I.e., these structs are contiguously allocated in memory. Macros are
*			used to cast pointers to these structures.
*
* 			In the Intel RTL (IRTL) this function did the same thing, i.e., allocating task storage space.
* 			The definition in the IRTL was at kmp_csupport.c:276.
*
* @param 	loc					Source location of the omp construct.
* @param 	gtid				Global id of the thread.
* @param 	pflags				This always came as "1" from the compiler.
* @param 	sizeof_kmp_task_t	This include the sizeof(kmp_task)+8 (i.e., 40bytes) + the space required by private vars.
* @param 	sizeof_shareds		Size of shared memory region.
* @param 	task_entry			Pointer to ptask wrapper routine.
*
* @return	Returns a pointer to the newly allocated task+metadata.
*/
kmp_task* __kmpc_omp_task_alloc(ident *loc, kmp_int32 gtid, kmp_int32 pflags, kmp_uint32 sizeof_kmp_task_t, kmp_uint32 sizeof_shareds, kmp_routine_entry task_entry);



/**
 * @brief	This function is responsible for calling the "ptask" wrapper function, which will finally
 * 			call the function for executing the task.
 *
 * 			This function were responsible for scheduling a task with dependences for execution. It just receive
 * 			the kmp_task pointer created by the function kmp_alloc and dispatch the task for execution in a thread.
 * 			The definition in the IRTL was at kmp_taskdeps.cpp:393.
 *
 * @param 	loc 				Location of the original task directive
 * @param 	gtid 				Global thread ID of encountering thread
 * @param 	new_task 			task thunk allocated by __kmp_omp_task_alloc() for the ''new task''
 * @param 	ndeps 				Number of depend items with possible aliasing
 * @param 	dep_list 			List of depend items with possible aliasing
 * @param 	ndeps_noalias 		Number of depend items with no aliasing
 * @param 	noalias_dep_list 	List of depend items with no aliasing
 *
 * @return 	Returns either TASK_CURRENT_NOT_QUEUED if the current task was not suspendend and
 * 			queued, or TASK_CURRENT_QUEUED if it was suspended and queued.
 */
kmp_int32 __kmpc_omp_task_with_deps(ident* loc, kmp_int32 gtid, kmp_task* new_task, kmp_int32 ndeps, kmp_depend_info* dep_list, kmp_int32 ndeps_noalias, kmp_depend_info* noalias_dep_list);



/**
 * @brief	This should make the calling thread pause until all tasks have been executed.
 *
 *  		The commnent in IRTL was: "Wait until all tasks generated by the current task are complete."
 *  		The definition in the IRTL was at kmp_tasking.cpp:1098.
 *
 *  @param	loc 	Location of the original task directive
 *  @param	gtid 	Global thread ID of encountering thread
 *
 *  @return	This was not described in the documentation. Analyzing the LLVM IR code the return is
 *  		discarded.
 */
kmp_int32 __kmpc_omp_taskwait(ident* loc, kmp_int32 gtid);




/**
 * @brief	Lock/Barrier related function. Currently not used in MTSP.
 */
kmp_int32 __kmpc_cancel_barrier(ident* loc, kmp_int32 gtid);




/**
 * @brief	Test whether to execute a <tt>single</tt> construct.
 */
kmp_int32 __kmpc_single(ident* loc, kmp_int32 gtid);




/**
 * @brief	Lock/Barrier related function. Currently not used in MTSP.
 */
void __kmpc_end_single(ident* loc, kmp_int32 gtid);




/**
 * @brief	Should return 1 if the thread gtid should execute the <tt>master</tt> block, 0 otherwise.
 */
kmp_int32 __kmpc_master(ident* loc, kmp_int32 gtid);




/**
 * @brief	Lock/Barrier related function. Currently not used in MTSP.
 */
void __kmpc_end_master(ident* loc, kmp_int32 gtid);

extern "C" {
	int omp_get_num_threads();
}


#endif
