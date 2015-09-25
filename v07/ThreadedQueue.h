#ifndef MCRINGBUFFER_H_
#define MCRINGBUFFER_H_

#include "mtsp.h"
#include <stdio.h>
#include <stdlib.h>

/// for padding purposes
#define L1D_LINE_SIZE 	   64
#define USED			 	1
#define EMPTY			 	0

#define Q_LOCKED			1
#define Q_UNLOCKED			0
#define	GET_LOCK(ptr)		while (__sync_bool_compare_and_swap(ptr, Q_UNLOCKED, Q_LOCKED) == false)
#define RLS_LOCK(ptr)		__sync_bool_compare_and_swap(ptr, Q_LOCKED, Q_UNLOCKED)

/// Single Producer and Single Consumer (Lock-free)
template <typename T, int QUEUE_SIZE, int BATCH_SIZE, int CONT_FACTOR=100>
class SPSCQueue {
private:
	// shared control variables
	volatile int read;
	volatile int write;
	char pad1[L1D_LINE_SIZE - 2*sizeof(int)];

	// consumer local control variables
	int localWrite;
	int nextRead;
	char pad2[L1D_LINE_SIZE - 2*sizeof(int)];

	// producer local control variables
	int localRead;
	int nextWrite;
	char pad3[L1D_LINE_SIZE - 2*sizeof(int)];

	// Circular buffer to insert elements
	// all position of the queue are already initialized
	T elems[QUEUE_SIZE];


public:
	SPSCQueue() {
		read			= 0;
		write			= 0;
		localRead		= 0;
		localWrite		= 0;
		nextRead		= 0;
		nextWrite		= 0;

		/// The size of the queue must be a power of 2
		if (QUEUE_SIZE == 0 || (QUEUE_SIZE & (QUEUE_SIZE - 1)) != 0) {
			printf("CRITICAL: The size of the queue must be a power of 2.\n");
			exit(-1);
		}
	}

	void enq(T elem) {
		__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_SPSC_Enq);

		int afterNextWrite = nextWrite + 1;
			afterNextWrite &= (QUEUE_SIZE - 1);

		if (afterNextWrite == localRead) {
			__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_SPSC_Enq_Blocking);

			while (afterNextWrite == read) ;
			localRead = read;

			__itt_task_end(__itt_mtsp_domain);
		}

		elems[nextWrite] = elem;

		nextWrite = afterNextWrite;

		if ((nextWrite & (BATCH_SIZE-1)) == 0)
			write = nextWrite;

		__itt_task_end(__itt_mtsp_domain);
	}

	/// Returns true if we can enqueue at least one more item on the queue.
	/// Actually it check to see if after the next enqueue the queue will be full.
	/// It updates the "localRead" if that would be true.
	bool can_enq() {
		return ((nextWrite+1) != localRead || ((localRead = read) != (nextWrite + 1)));
	}


	T deq() {
		__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_SPSC_Deq);

		if (nextRead == localWrite) {
			__itt_task_begin(__itt_mtsp_domain, __itt_null, __itt_null, __itt_SPSC_Deq_Blocking);

			while (nextRead == write) ;
			localWrite = write;

			__itt_task_end(__itt_mtsp_domain);
		}

		T data	= elems[nextRead];

		nextRead += 1;
		nextRead &= (QUEUE_SIZE-1);

		if ((nextRead & (BATCH_SIZE-1)) == 0)
			read = nextRead;

		__itt_task_end(__itt_mtsp_domain);
		return data;
	}

	/// Returns true if there is any item in the queue ready to be dequeued
	/// We check to see if there is something left in the current batch visible
	/// to the consumer. If there is not then we check if the producer has
	/// already produced new items.
	///
	/// (nextRead != localWrite)	==> where I would read is where the producer
	///									was about to write a next item?
	///	(nextRead != write)			==> has the producer produced new items yet?
	bool can_deq() {
		return (nextRead != localWrite || ((localWrite = write) != nextRead));
	}

	bool try_deq(T* elem) {
		if (can_deq()) {
			*elem = deq();
			return true;
		}
		else {
			return false;
		}
	}

	int cur_load() {
		//fsh();

		int w = write, r = read;

		if (w-r >= 0)
			return w-r;
		else
			return (QUEUE_SIZE - r) + w;
	}

	/// Returns a value of XX% of the queue size. I chose this value arbitrarily
	/// and consider that when the queue has more than this value of items it is
	/// saturating
	int cont_load() {
		double cf = ((double)CONT_FACTOR / 100.0);
		//printf("CF = %lf, size = %d, load = %d\n", cf, QUEUE_SIZE, (int)(QUEUE_SIZE * cf));
		return (int)(QUEUE_SIZE * cf);
	}

	void fsh() {
		write = nextWrite;
		read = nextRead;
	}
};



/// Single Producer and Single Consumer
template <typename T, int QUEUE_SIZE, int CONT_FACTOR=100>
class SimpleQueue {
private:
	volatile unsigned int head;
	volatile unsigned int tail;

	volatile T* data;
	volatile bool* status;
	volatile bool rlock;
	volatile bool wlock;

public:
	SimpleQueue() {
		if (QUEUE_SIZE <= 0 || (QUEUE_SIZE & (QUEUE_SIZE-1)) != 0) {
			printf("Queue size is not a power of 2! [%s, %d]\n", __FILE__, __LINE__);
			exit(-1);
		}

		data   = new T[QUEUE_SIZE];
		status = new bool[QUEUE_SIZE];

		for (int i=0; i<QUEUE_SIZE; i++)
			status[i] = EMPTY;

		rlock = Q_UNLOCKED;
		wlock = Q_UNLOCKED;
		head = 0;
		tail = 0;
	}

	void enq(T elem) {
		GET_LOCK(&wlock);

		while (status[tail] != EMPTY);

		data[tail] = elem;
		status[tail] = USED;

		tail = ((tail+1) & (QUEUE_SIZE-1));

		RLS_LOCK(&wlock);
	}

	T deq() {
		GET_LOCK(&rlock);
		while (status[head] == EMPTY);

		T elem = data[head];
		status[head] = EMPTY;
		head = ((head+1) & (QUEUE_SIZE-1));
		RLS_LOCK(&rlock);

		return elem;
	}

	bool try_deq(T* elem) {
		GET_LOCK(&rlock);

		if (status[head] != EMPTY) {
			//while (status[head] == EMPTY);

			*elem = data[head];
			status[head] = EMPTY;
			head = ((head+1) & (QUEUE_SIZE-1));
			RLS_LOCK(&rlock);
			return true;
		}

		RLS_LOCK(&rlock);
		return false;
	}

	int cur_load() {
		if (tail-head >= 0)
			return tail-head;
		else
			return (QUEUE_SIZE - head) + tail;
	}

	/// Returns a value of XX% of the queue size. I chose this value arbitrarily
	/// and consider that when the queue has more than this value of items it is
	/// saturating
	int cont_load() {
		return QUEUE_SIZE * ((double)CONT_FACTOR / 100.0);
	}

	~SimpleQueue() {
		// For now this is the safest option
		//delete [] data;
		//delete [] status;
	}
};

#endif /* MCRINGBUFFER_H_ */
