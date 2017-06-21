#ifndef __MTSP_DEP_TRACKER_HEADER
	#define __MTSP_DEP_TRACKER_HEADER 1

	#include "kmp.h"
	#include "mtsp.h"
	#include <map>
	#include <utility>


	/// Maps from memory address to a pair<IdOfLastTaskAccessingIt, vector of ID of readers>
	extern std::map<kmp_intptr, std::pair<kmp_int32, std::vector<kmp_uint32>>> dependenceTable;



	//===----------------------------------------------------------------------===//
	//
	// Start of function's prototype used in this implementation of dep. tracker.
	//
	//===----------------------------------------------------------------------===//


	void releaseDependencies(kmp_uint16 idOfFinishedTask, kmp_uint32 ndeps, kmp_depend_info* depList);

	kmp_uint64 checkAndUpdateDependencies(kmp_uint16 taskId, kmp_uint32 ndeps, kmp_depend_info* depList);

#endif
