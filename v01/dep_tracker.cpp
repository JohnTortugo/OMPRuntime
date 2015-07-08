#include "dep_tracker.h"

std::map<kmp_intptr, kmp_uint32> dependenceTable;
unsigned char lock_dependenceTable;


void __mtsp_initializeDepChecker() {
	lock_dependenceTable = false;
}

void releaseDependencies(kmp_uint16 idOfFinishedTask, kmp_uint32 ndeps, kmp_depend_info* depList) {
	__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_Releasing_Dependences);

	ACQUIRE(&lock_dependenceTable);

	/// Iterate over each dependence and check if the task ID of the last task accessing that address
	/// is the ID of the finished task. If it is remove that entry, otherwise do nothing.
	for (kmp_uint32 depIdx=0; depIdx<ndeps; depIdx++) {
		auto baseAddr = depList[depIdx].base_addr;
		auto pos	  = dependenceTable.find(baseAddr);

		/// Is someone already accessing this address?
		if (pos != dependenceTable.end()) {
			if ((pos->second >> 16) == idOfFinishedTask) {
				dependenceTable.erase(pos);
			}
		}
	}

	RELEASE(&lock_dependenceTable);
	__itt_task_end(__itt_mtsp_domain);
}

kmp_uint64 checkAndUpdateDependencies(kmp_uint16 taskId, kmp_uint32 ndeps, kmp_depend_info* depList) {
	__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_Checking_Dependences);

	kmp_uint64 depPattern = 0;

	/// Iterate over each dependence
	for (kmp_uint32 depIdx=0; depIdx<ndeps; depIdx++) {
		auto baseAddr 		= depList[depIdx].base_addr;
		auto entry			= dependenceTable.find(baseAddr);
		kmp_uint32 newValue = taskId;
				   newValue = (newValue << 16) | (depList[depIdx].flags.in << 1) | depList[depIdx].flags.out;

		/// Is someone already accessing this address?
		if (entry != dependenceTable.end()) {
			/// obtain information about the last task acessing this baseAddr
			kmp_uint32 oldValue  = entry->second;

			/// extract the task id and the access mode of the last access
			kmp_uint16 oldTaskId = oldValue >> 16;
			/// TODO: improve the dependence tracking. e.g., when the are sucessive readings we dont have dependence.
			// kmp_uint16 oldAccMod = newValue << 16;

			/// set the depPattern bit corresponding to the oldTaskId index
			depPattern = depPattern | ((kmp_uint64)1 << oldTaskId);
		}

		/// Currently, no matter what, we are always updating the last access information
		dependenceTable[baseAddr] = newValue;
	}

	__itt_task_end(__itt_mtsp_domain);
	return depPattern;
}
