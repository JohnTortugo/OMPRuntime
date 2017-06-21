#ifndef __MTSP_TASK_GRAPH_HEADER
	#define __MTSP_TASK_GRAPH_HEADER 1

	#include "mtsp.h"

	//===----------------------------------------------------------------------===//
	//
	// Start of global vars/defines used in this implementation of task graph.
	//
	//===----------------------------------------------------------------------===//


	/// Tells whether we have already initialized the task graph data structures
	extern bool 							taskGraphInitialized;

	/// Each position stores a pointer to a task descriptor for a task present in the task graph.
	extern kmp_task* 			volatile 	tasks[MAX_TASKS];

	// depCounters[i] tells on how many other tasks task 'i' depends on
	extern kmp_uint16 			volatile	depCounters[MAX_TASKS];

	// dependents[i][0] tells how many depends task 'i' has, these depends are listed on the subsequent dependents[i][1...]
	extern kmp_uint16 			volatile	dependents[MAX_TASKS][MAX_DEPENDENTS+1];

	// If a position whoIDependOn[i][j] is true then task 'i' depends on task 'j'
	extern bool 				volatile	whoIDependOn[MAX_TASKS][MAX_DEPENDENTS+1];

	/// This will store a list of free rows (slots) from the taskGraph matrix. The index zero (0)
	/// is used as a counter for the number of free slots. The remaining positions store indices of
	/// free slots in the taskGraph.
	extern kmp_uint16 			volatile 	freeSlots[MAX_TASKS + 1];





	//===----------------------------------------------------------------------===//
	//
	// Start of function's prototype used in this implementation of task graph.
	//
	//===----------------------------------------------------------------------===//

	/// Dump the current task graph to a dot file
	void __mtsp_dumpTaskGraphToDot();

	/// Need to be called only once to initialize the task graph data structures
	void __mtsp_initializeTaskGraph();

	/// Used to add a new task to the task graph
	void addToTaskGraph(kmp_task*);

	void removeFromTaskGraph(kmp_task*);
#endif
