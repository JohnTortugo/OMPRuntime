#ifndef __MTSP_TASK_GRAPH_HEADER
	#define __MTSP_TASK_GRAPH_HEADER 1

	#include "kmp.h"

	//===----------------------------------------------------------------------===//
	//
	// Start of global vars/defines used in this implementation of task graph.
	//
	//===----------------------------------------------------------------------===//

	/// Represents the maximum number of tasks that can be stored in the task graph
	#define MAX_TASKS 				64

	/// Represents the maximum number of tasks in the "new tasks queue" in the front-end
	#define NEW_TASKS_QUEUE_SIZE	16384

	/// Tells whether we have already initialized the task graph data structures
	extern bool taskGraphInitialized;

	/// Each position stores a pointer to a task descriptor for a task present in the task graph.
	extern kmp_task* volatile tasks[MAX_TASKS];

	/// This is a MAX_TASKS x 64 matrix of bits. Each row / column represent a different task
	/// A bit at position [i,j] is set if the task at row [i] depends on the task at column [j].
	/// When a row is zero (0) the task has no dependency and thus it is ready for execution/executing/finished.
	extern kmp_uint64 volatile taskGraph[MAX_TASKS];

	/// This will store a list of free rows (slots) from the taskGraph matrix. The index zero (0)
	/// is used as a counter for the number of free slots. The remaining positions store indices of
	/// free slots in the taskGraph.
	extern kmp_uint16 volatile freeSlots[MAX_TASKS + 1];

	/// This has the same structure as freeSlots but store tasks that are ready for execution
	//extern kmp_uint16 volatile readySlots[MAX_TASKS + 1];

	/// This has the same structure as freeSlots but store tasks that have finished execution
	extern kmp_uint16 volatile finishedSlots[MAX_TASKS + 1];

	/// Every time a thread finish executing a task it will add its ID in this queue. This
	/// is used for load balancing.
	extern kmp_uint16 volatile finishedIDS[MAX_TASKS + 1];

	/// The ID of the next worker-thread that will receive work (for a ciclic distribution)
	extern kmp_uint8 idNextWorkerThread;



	//===-------- Locks used to control access to the variables above ----------===//
	extern unsigned char volatile lock_readySlots;
	extern unsigned char volatile lock_finishedSlots;














	//===----------------------------------------------------------------------===//
	//
	// Start of function's prototype used in this implementation of task graph.
	//
	//===----------------------------------------------------------------------===//


	/// Need to be called only once to initialize the task graph data structures
	void __mtsp_initializeTaskGraph();


	/// Used to add a new task to the task graph
	///
	/// \param	newTask		A pointer to the new task allocated by kmpc_task_alloc
	/// \param	ndeps		The number of dependence items this task has
	/// \param	depList		The list of dependence items this task has
	void addToTaskGraph(kmp_task* newTask, kmp_uint32 ndeps, kmp_depend_info* depList);

	void removeFromTaskGraph(kmp_uint16 idOfFinishedTask);

	void dumpDependenceMatrix();
#endif
