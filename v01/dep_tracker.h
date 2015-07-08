#ifndef __MTSP_DEP_TRACKER_HEADER
	#define __MTSP_DEP_TRACKER_HEADER 1

	#include "kmp.h"
	#include "mtsp.h"
	#include <map>

	extern std::map<kmp_intptr, kmp_uint32> dependenceTable;


	//===-------- Locks used to control access to the variables above ----------===//
	extern unsigned char lock_dependenceTable;



	void __mtsp_initializeDepChecker();

	void releaseDependencies(kmp_uint16 idOfFinishedTask, kmp_uint32 ndeps, kmp_depend_info* depList);

	kmp_uint64 checkAndUpdateDependencies(kmp_uint16 taskId, kmp_uint32 ndeps, kmp_depend_info* depList);

#endif
