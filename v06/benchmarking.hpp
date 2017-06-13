#include <cstdint>

#ifndef __BENCHMARKING__
  /// For reading the RDTSCP counter in the x86 architecture. You must call these
  /// functions in the order "beg" (target_region_of_interest) "end".

	inline uint64_t beg_read_mtsp() {
		uint32_t cycles_low, cycles_high;	

		asm volatile ("CPUID\n\t"
		 "RDTSC\n\t"
		 "mov %%edx, %0\n\t"
		 "mov %%eax, %1\n\t": "=r" (cycles_high), "=r" (cycles_low)::
		"%rax", "%rbx", "%rcx", "%rdx");

		return (((uint64_t) cycles_high) << 32) | cycles_low;
	}

	inline uint64_t end_read_mtsp() {
		uint32_t  cycles_low, cycles_high;	

		asm volatile("RDTSCP\n\t"
		 "mov %%edx, %0\n\t"
		 "mov %%eax, %1\n\t"
		 "CPUID\n\t": "=r" (cycles_high), "=r" (cycles_low)::
		"%rax", "%rbx", "%rcx", "%rdx");

		return (((uint64_t) cycles_high) << 32) | cycles_low;
	}
#endif
