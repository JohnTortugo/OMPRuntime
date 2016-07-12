/**
 * @file 	fe_interface.h
 * @brief 	This file contains the functions and variables that is used by the front-end of MTSP.
 *
 * In this file you will find the prototype of the functions that are used as an
 * interface between the serial application (i.e., the main application or the control thread)
 * and the runtime thread (or the MTSP module itself).
 */
#ifndef __MTSP_FE_INTERFACE_HEADER
	#define __MTSP_FE_INTERFACE_HEADER 1

	#include "kmp.h"
	#include "ThreadedQueue.h"


	extern kmp_uint64 tasksExecutedByCT;
	extern kmp_uint64 metadataRequestsNotServiced;
	extern volatile kmp_uint64 tasksExecutedByRT;

	extern SimpleQueue<kmp_task*, SUBMISSION_QUEUE_SIZE, SUBMISSION_QUEUE_CF> submissionQueue;

	extern std::map<kmp_critical_name*, bool> criticalRegions;
	extern volatile bool crit_reg_lock;

	extern pthread_t hwsThread;

	extern struct QDescriptor* __mtsp_SubmissionQueueDesc;
	extern struct QDescriptor* __mtsp_RunQueueDesc;
	extern struct QDescriptor* __mtsp_RetirementQueueDesc;


	extern kmp_task* volatile tasks[MAX_TASKS];

	/**
	 * This store a list of the free entries (slots) of the taskGraph. The index zero (0)
	 * is used as a counter for the number of free slots. The remaining positions store 
	 * indices of free slots in the taskGraph.
	 */
	extern MPMCQueue<kmp_uint16, MAX_TASKS*2, 4> freeSlots;


	/**
	* This is the function responsible for initializing the systemC (HWS) module.
	*/
	extern void* __hws_init(void *);

	/**
	* This is used for ringing a doorbell of the Submission Queue whenever a new
	* packet is inserted there.
	*/
	extern void ringDoorbell(long long int val);
	extern void play_doorbell();
	extern void place_doorbell();

	/**
	* Tells whether hws is alive or not.
	*/
	//extern volatile char hws_alive;


	/**
	* Used to initialize the MTSP bridge. This allocate space for the submission,
	* run queues and retirement queues and sends their address to the HWS module.
	*/
	void __mtsp_init();

	void __mtsp_enqueue_into_submission_queue(unsigned long long packet);


	/**
	* @brief	This function represents the initialization of a new OMP parallel region. 
	*
	* 			This is the first function called when a new OMP parallel region start
	* 			execution. Therefore, there are two main responsabilities of this function:
	* 			1) initialize the MTSP runtime module if this has not been done before, 
	* 			2) parse the \c var_arg list and forward the execution to the function 
	* 			pointed by \c kmpc_micro.
	*
	* @param 	loc  		Source location of the omp construct.
	* @param 	argc  		Total number of arguments in the ellipsis.
	* @param 	microtask  	Pointer to callback routine (usually .omp_microtask) consisting of outlined "omp parallel" construct
	* @param 	va_arg 		Pointers to shared variables that aren't global
	*/
	void __kmpc_fork_call(ident *loc, kmp_int32 argc, kmpc_micro microtask, ...);



	/**
	* @brief	This function is responsible for allocating memory for storing task parameters and metadata.
	*
	*			The task structure, the task metadata and the task private/shared members are allocated with
	*			just one call to malloc or taken from a pre-allocated memory region (i.e., it tries to reduce
	*			the number of calls to malloc). These structs are contiguously allocated in memory. Macros are
	*			used to cast pointers to these structures.
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
	 * @brief	This function add a task creation request to the Submission Queue.
	 *
	 * 			This function forward the \c new_task structure returned by \c __kmpc_omp_task_alloc
	 *			again to the runtime module, but this time in the form of a request to add a new task
	 *			to the task graph. In other words, this function is used by the control thread
	 *			to ask the runtime to add a new task to the task graph.
	 *
	 * @param 	loc 				Location of the original task directive
	 * @param 	gtid 				Global thread ID of encountering thread
	 * @param 	new_task 			task thunk allocated by __kmp_omp_task_alloc() for the ''new task''
	 * @param 	ndeps 				Number of depend items with possible aliasing
	 * @param 	dep_list 			List of depend items with possible aliasing
	 * @param 	ndeps_noalias 		Number of depend items with no aliasing
	 * @param 	noalias_dep_list 	List of depend items with no aliasing
	 *
	 * @return 						Always returns zero.
	 */
	kmp_int32 __kmpc_omp_task_with_deps(ident* loc, kmp_int32 gtid, kmp_task* new_task, kmp_int32 ndeps, kmp_depend_info* dep_list, kmp_int32 ndeps_noalias, kmp_depend_info* noalias_dep_list);



	/**
	 * @brief	This make the current thread to pause execution until all its subtasks have finished execution. 
	 *
	 *			In the current implementation this function represents a barrier. The control thread will
	 *			pause execution until all worker threads have reached this barrier. The worker threads only
	 *			get inside this barrier after all in flight tasks in the system has been executed.
	 *
	 *  @param	loc 	Location of the original task directive
	 *  @param	gtid 	Global thread ID of encountering thread
	 *
	 *  @return			LLVM currently ignores the return of this function.
	 */
	kmp_int32 __kmpc_omp_taskwait(ident* loc, kmp_int32 gtid);



	/**
	 * @brief	Lock/Barrier related function. Currently not used in MTSP.
	 *
	 * @return 	LLVM currently ignores the return of this function.
	 */
	kmp_int32 __kmpc_cancel_barrier(ident* loc, kmp_int32 gtid);




	/**
	 * @brief	Test whether to execute a \c single construct.
	 *
	 * @return 	LLVM currently ignores the return of this function.
	 */
	kmp_int32 __kmpc_single(ident* loc, kmp_int32 gtid);




	/**
	 * @brief	Lock/Barrier related function. Currently not used in MTSP.
	 *
	 * @return 	LLVM currently ignores the return of this function.
	 */
	void __kmpc_end_single(ident* loc, kmp_int32 gtid);




	/**
	 * @brief	Should return 1 if the thread with ID \c gtid should execute the 
	 * 			\c master block, 0 otherwise.
	 *
	 * @return 	1 if the thread with ID \c gtid should execute the \c master block, 0 otherwise.
	 */
	kmp_int32 __kmpc_master(ident* loc, kmp_int32 gtid);




	/**
	 * @brief	Lock/Barrier related function. Currently not used in MTSP.
	 *
	 * @return 	LLVM currently ignores the return of this function.
	 */
	void __kmpc_end_master(ident* loc, kmp_int32 gtid);


	extern "C" {
		/**
		 * @brief 	This function is from the OMP C API and it returns the
		 * 			total number of threads currently under OMP (MTSP) control.
		 *
		 * @return 	The number of threads currently in the system.
		 */
		int omp_get_num_threads();
		int omp_get_thread_num();
		int __kmpc_global_thread_num(ident* loc);
		void __kmpc_taskgroup(ident* loc, kmp_int32 gtid);
		void __kmpc_end_taskgroup(ident* loc, kmp_int32 gtid);
		void __kmpc_critical(ident* loc, kmp_int32 gtid, kmp_critical_name* name);
		void __kmpc_end_critical(ident* loc, kmp_int32 gtid, kmp_critical_name* name);
	}
#endif
