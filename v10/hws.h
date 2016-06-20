#ifndef __MTSP_HWS_HEADER
#define __MTSP_HWS_HEADER 1
#include <stdint.h>

typedef enum { MULTI = 0, SINGLE = 1} HSA_QType;
typedef enum { KERNEL_DISPATCH = 1, AGENT_DISPATCH = 2, DATA_MODE = 0 } HSA_QFeature;



#define HWS_TASK_PACKET				0x1
#define HWS_DEP_PACKET				0x2
#define HWS_QOS					0x0


#define get_packet_type(var)    	(((var) & 0xC000000000000000) >> 62)
#define get_ASID(var)            	(((var) & 0x3FC0000000000000) >> 54)
#define get_QOS(var)            	(((var) & 0x0030000000000000) >> 52)
#define get_mode(var)            	(((var) & 0x0030000000000000) >> 52)
#define get_Last(var)            	(((var) & 0x0008000000000000) >> 51)
#define get_Filler(var)            	(((var) & 0x0004000000000000) >> 50)
#define get_WDPtr(var)            	(((var) & 0x0003FFFFFFFFFFFF))


#define set_packet_type(var, val)  	(((var) & 0xC000000000000000) >> 62)
#define set_ASID(var, val)        	(((var) & 0x3FC0000000000000) >> 54)
#define set_QOS(var, val)          	(((var) & 0x0030000000000000) >> 52)
#define set_mode(var, val)         	(((var) & 0x0030000000000000) >> 52)
#define set_Last(var, val)         	(((var) & 0x0008000000000000) >> 51)
#define set_Filler(var, val)       	(((var) & 0x0004000000000000) >> 50)
#define set_WDPtr(var, val)       	(((var) & 0x0003FFFFFFFFFFFF))


#define create_task_packet(var, prior, last, addr)						( (var) = 0);					\
												( (var) |= ((unsigned long long) 1     << 62));	\
												( (var) |= ((unsigned long long) 0     << 54));	\
												( (var) |= ((unsigned long long) prior << 52));	\
												( (var) |= ((unsigned long long) last  << 50));	\
												( (var) |= ((unsigned long long) addr))


#define create_dep_packet(var, mode, last, addr)						( (var) = 0);					\
												( (var) |= ((unsigned long long) 2     << 62));	\
												( (var) |= ((unsigned long long) 0     << 54));	\
												( (var) |= ((unsigned long long) mode  << 52));	\
												( (var) |= ((unsigned long long) last  << 50));	\
												( (var) |= ((unsigned long long) addr))




struct QDescriptor {
	HSA_QType QT; 					// 32 bits defining queue type for future expansion, used to determine dynamic queue protocol
	HSA_QFeature QF; 				// 32 bits to indicate specific features supported by the queue
	long long int base_address; 	// Mem* base_address; // a 64-bit pointer to the queue’s packet ring buffer
	//Mem* doorbell; 				// 64-bit pointer to doorbell object

	int32_t QSize; 					// 32-bit number indicating the max. number of **Bytes**
	int32_t reserved; 				// must be zero – padding for alignment
	int64_t QId; 					// unique identifier for this queue
	volatile int64_t QHead; 		// write index to head of queue
	volatile int64_t QTail; 		// read index to tail of queue
};


struct SQPacket {
	unsigned long long int payload;		// [63..62]	-> Package type (01 -> NT, 10 -> ND)
										// [61..54] -> 8 Bits (ASID)
										// [53..52] -> Qos or Dep Type (53->W, 52->R)
										// [51]		-> Last packet or not
										// [50]		-> Filler (always zero)
										// [49..0]	-> Task Address or Parameter address
};

#endif
