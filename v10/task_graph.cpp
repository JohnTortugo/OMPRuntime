/**
 * @file 	task_graph.h
 * @brief 	This file contains functions and variables related to the Task Graph Manager.
 *
 */

#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <sys/time.h>
#include <queue>
#include <map>
#include <set>
#include <sstream>
#include <iomanip>
#include <cstring>


#include "kmp.h"
#include "task_graph.h"
#include "dep_tracker.h"
#include "scheduler.h"
#include "fe_interface.h"
#include "mtsp.h"

bool 		volatile	taskGraphInitialized = false;
kmp_task*	volatile	tasks[MAX_TASKS];
kmp_uint16 	volatile	depCounters[MAX_TASKS];
kmp_uint16 	volatile	dependents[MAX_TASKS][MAX_DEPENDENTS+1];
bool 		volatile	whoIDependOn[MAX_TASKS][MAX_DEPENDENTS+1];
std::set<kmp_task*>* volatile 	pcGraph[MAX_TASKS];

std::string colorNames[] = {"red", "blue", "cyan", "magenta", "yellow", "orange"};

void nodeLabel(std::stringstream& ss, int idx) {
	ss.str("");
	ss.clear();
	ss << "<table border=\"0\" cellpadding=\"0\" cellspacing=\"0\">";
	ss << "<tr><td align=\"center\">ID:: " << std::dec << idx << "</td></tr>";

	auto ndeps = tasks[idx]->metadata->ndeps;
	auto deps = tasks[idx]->metadata->dep_list;

	// Iterate over each dependence
	for (kmp_uint32 depIdx=0; depIdx<ndeps; depIdx++) {
		bool isInput	= deps[depIdx].flags.in;
		bool isOutput	= deps[depIdx].flags.out;

		ss << "<tr><td align=\"center\">";

		if (isInput) ss << "R"; else ss << " ";
		if (isOutput) ss << "W"; else ss << " ";

		ss << ": " << std::setfill('0') << std::setw(2) << std::hex << deps[depIdx].base_addr;

		ss << "</td></tr>";
	}

	ss << "</table>";
}



std::string nextColor() {
	static int index = 0;
	return colorNames[index++ % 4];
}



void __mtsp_dumpTaskGraphToDot() {
	// Just for the node labels;
	std::stringstream ss;

	// identify which nodes are present in the TG
	bool present[MAX_TASKS];

	// at the begining everybody is present
	for (int i=0; i<MAX_TASKS; i++) present[i] = true;

	// the slots that are free are not present
	std::queue<int> tmp;
	while (freeSlots.cur_load()) {
		int idx = freeSlots.deq();
		present[idx] = false;
		tmp.push(idx);
	}

	while (!tmp.empty()) {
		int idx = tmp.front(); tmp.pop();
		freeSlots.enq(idx);
	}

	// Store the name of the task graph and its identifier
	char fileName[100];
	static int counter = 0;

	// The "dot" file where we are going to write the graph
	sprintf(fileName, "taskgraph_%04d.dot", counter++);

	FILE* fp = fopen(fileName, "w+");

	if (!fp) { fprintf(stderr, "It was impossible to write the dependenceGraph to a dot file [%s].\n", fileName); exit(-1); }

	fprintf(fp, "digraph TaskGraph {\n");

	// for each present node
	for (int src=0; src<MAX_TASKS; src++) { if (present[src]) {
//		if (colors.find(tasks[src]->routine) == colors.end())
//			colors[tasks[src]->routine] = nextColor();

		nodeLabel(ss, src);
//		fprintf(fp, "Node_%04d [style=filled fillcolor=%s shape=\"Mrecord\" label=<%s>];\n", src, colors[tasks[src]->routine].c_str(), ss.str().c_str());
		fprintf(fp, "Node_%04d [style=filled shape=\"Mrecord\" label=<%s>];\n", src, ss.str().c_str());

		for (int j=1; j<=dependents[src][0]; j++) {
			fprintf(fp, "Node_%04d -> Node_%04d;\n", src, dependents[src][j]);
		}
	}}

	fprintf(fp, "}\n");
	fclose(fp);

	printf("Taskgraph written to file %s\n", fileName);
}




void __mtsp_initializeTaskGraph() {
	for (int i=0; i<MAX_TASKS; i++) {
		tasks[i]			= nullptr;

		freeSlots.enq(i);

		pcGraph[i] = new std::set<kmp_task*>();

		depCounters[i]		= 0;
		dependents[i][0]	= 0;

		for (int j=1; j<=MAX_TASKS; j++) 
			whoIDependOn[i][j] = false;
	}

	// also initialize the scheduler and dependence tracker
	__mtsp_initScheduler();
}



void removeFromTaskGraph(kmp_task* finishedTask) {
	__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_TaskGraph_Del);

	// Counter for the total cycles spent per task
	kmp_uint16 idOfFinishedTask = finishedTask->metadata->taskgraph_slot_id;
	kmp_int16 parentTaskId = finishedTask->metadata->parentTaskId;

#if DEBUG_MODE
	printf("Removing task %d from taskgraph.\n", idOfFinishedTask);
#endif

	// Release the dependent tasks
	int sz = dependents[idOfFinishedTask][0];
	for (int i=1; i<=sz; i++) {
		int depId = dependents[idOfFinishedTask][i];
		depCounters[depId]--;

		// We reset the dependent status of the dependent task
		// that is: depId no longer depends on idOfFinishedTask
		whoIDependOn[depId][idOfFinishedTask] = false;

		if (depCounters[depId] == 0) {
#if DEBUG_MODE
			printf("Releasing task [%d] to RunQueue.\n", depId);
#endif
			__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_Run_Queue_Enqueue);
			RunQueue.enq( tasks[depId] );
			__itt_task_end(__itt_mtsp_domain);
		}
	}

	// Remove from the dependence checker the positions that this task owns
	releaseDependencies(idOfFinishedTask, finishedTask->metadata->ndeps, finishedTask->metadata->dep_list);

	// removes the pointer to the child task in the parent
	if (parentTaskId >= 0 && parentTaskId != idOfFinishedTask) {
		ACQUIRE(&pcGraphLock);
		if ( pcGraph[parentTaskId]->find(finishedTask) != pcGraph[parentTaskId]->end() ) {
			//printf("Going to remove from task %d from parent %d.\n", idOfFinishedTask, parentTaskId);
			pcGraph[parentTaskId]->erase(finishedTask);
		}
		else {
			printf("Child not in parent ::: [child=%d, parent=%d, parentReseted=%d\n", idOfFinishedTask, parentTaskId, finishedTask->metadata->parentReseted);
			printf("\tThese are the children: {%02d} {DC=%02d} [", pcGraph[parentTaskId]->size(), tasks[parentTaskId]->metadata->numDirectChild);

			for (kmp_task* child : *pcGraph[parentTaskId]) {
				printf("(%04d : %04d) ", child->metadata->taskgraph_slot_id, child->metadata->parentTaskId);
			}

			printf("]\n");
		}
		RELEASE(&pcGraphLock);

		// If the parent is still the original it may be the case that
		// it is stuck in a taskwait/taskgroup waiting for its children
		if (!finishedTask->metadata->parentReseted && tasks[parentTaskId])
			ATOMIC_SUB(&tasks[parentTaskId]->metadata->numDirectChild, 1);

		if (pcGraph[idOfFinishedTask]->size() > 0) {
			for (kmp_task* child : *pcGraph[idOfFinishedTask]) {
				ACQUIRE(&pcGraphLock);
				pcGraph[parentTaskId]->insert(child);
				RELEASE(&pcGraphLock);
				child->metadata->parentTaskId = parentTaskId;
				child->metadata->parentReseted = true;
			}
		}
	}
	else if (parentTaskId == idOfFinishedTask) {
		if (pcGraph[idOfFinishedTask]->size() > 0) {
			for (kmp_task* child : *pcGraph[idOfFinishedTask]) {
				child->metadata->parentTaskId = child->metadata->taskgraph_slot_id;
				child->metadata->parentReseted = true;
			}
		}
	}
	else {
		ATOMIC_SUB(&__ControlThreadDirectChild, 1);

		if (pcGraph[idOfFinishedTask]->size() > 0) {
			for (kmp_task* child : *pcGraph[idOfFinishedTask]) {
				child->metadata->parentTaskId = child->metadata->taskgraph_slot_id;
				child->metadata->parentReseted = true;
			}
		}
	}

	// release malloc-ed memory
	free(finishedTask->metadata->dep_list);
	free( KMP_TASK_TO_TASKDATA(tasks[idOfFinishedTask]) );

	// reinitialize data structures
	dependents[idOfFinishedTask][0] = 0;
	depCounters[idOfFinishedTask] = 0;
	ACQUIRE(&pcGraphLock);
	pcGraph[idOfFinishedTask]->clear();
	RELEASE(&pcGraphLock);


	freeSlots.enq(idOfFinishedTask);

	// Decrement the number of tasks in the system currently
	ATOMIC_SUB(&__mtsp_inFlightTasks, (kmp_int32)1);

	__itt_task_end(__itt_mtsp_domain);
}



void addToTaskGraph(kmp_task* newTask) {
	__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_TaskGraph_Add);

	kmp_uint32 ndeps = newTask->metadata->ndeps;
	kmp_depend_info* depList = newTask->metadata->dep_list;

	// Obtain id for the new task
	kmp_uint16 newTaskId = newTask->metadata->taskgraph_slot_id; 
	kmp_int32 parentTaskId = newTask->metadata->parentTaskId; 

#if DEBUG_MODE
	printf("Checking Dependencies of task [%d] which parent is [%d].\n", newTaskId, parentTaskId);
#endif

	// depPattern stores a bit pattern representing the dependences of the new task
	kmp_uint64 depCounter = checkAndUpdateDependencies(newTaskId, newTask->metadata->parentTaskId, ndeps, depList);

	// stores the new task dependence pattern
	depCounters[newTaskId] = depCounter;
	dependents[newTaskId][0] = 0;

	// stores the pointer to the new task
	tasks[newTaskId] = newTask;

	
	// if the task has depPattern == 0 then it may already be dispatched.
	if (depCounter == 0) {
		__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_Run_Queue_Enqueue);
		RunQueue.enq( newTask );
		__itt_task_end(__itt_mtsp_domain);
	}

	__itt_task_end(__itt_mtsp_domain);
}




