#ifndef MCRINGBUFFER_H_
#define MCRINGBUFFER_H_

/// for padding purposes
#define L1D_LINE_SIZE 64

#define Q_LOCKED				1
#define Q_UNLOCKED				0
#define	GET_LOCK(ptr)			while (__sync_bool_compare_and_swap(ptr, Q_UNLOCKED, Q_LOCKED) == false)
#define RLS_LOCK(ptr)			__sync_bool_compare_and_swap(ptr, Q_LOCKED, Q_UNLOCKED)


/// Single Producer and Single Consumer (Lock-free)
template <typename T, int QUEUE_SIZE, int BATCH_SIZE>
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
		int afterNextWrite = nextWrite + 1;
			afterNextWrite &= (QUEUE_SIZE - 1);

		if (afterNextWrite == localRead) {
			while (afterNextWrite == read) ;
			localRead = read;
		}

		elems[nextWrite] = elem;

		nextWrite = afterNextWrite;

		if ((nextWrite & (BATCH_SIZE-1)) == 0)
			write = nextWrite;
	}

	/// Returns true if we can enqueue at least one more item on the queue.
	/// Actually it check to see if after the next enqueue the queue will be full.
	/// It updates the "localRead" if that would be true.
	bool can_enq() {
		return ((nextWrite+1) != localRead || ((localRead = read) != (nextWrite + 1)));
	}


	T deq() {
		if (nextRead == localWrite) {
			while (nextRead == write) ;
			localWrite = write;
		}

		T data	= elems[nextRead];

		nextRead += 1;
		nextRead &= (QUEUE_SIZE-1);

		if ((nextRead & (BATCH_SIZE-1)) == 0)
			read = nextRead;

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

	void fsh() {
		write = nextWrite;
		read = nextRead;
	}

};

/// Multiple Producers (Using atomic locks) and Single Consumer
template <typename T, int QUEUE_SIZE, int BATCH_SIZE>
class MPSCQueue {
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
	volatile bool lock;
	char pad3[L1D_LINE_SIZE - 2*sizeof(int)];

	// Circular buffer to insert elements
	// all position of the queue are already initialized
	T elems[QUEUE_SIZE];


public:
	MPSCQueue() {
		read			= 0;
		write			= 0;
		localRead		= 0;
		localWrite		= 0;
		nextRead		= 0;
		nextWrite		= 0;
		lock			= Q_UNLOCKED;
	}

	void enq(T elem) {
		GET_LOCK(&lock);

		int afterNextWrite = nextWrite + 1;
			afterNextWrite &= (QUEUE_SIZE - 1);

		if (afterNextWrite == localRead) {
			while (afterNextWrite == read) ;
			localRead = read;
		}

		elems[nextWrite] = elem;

		nextWrite = afterNextWrite;

		if ((nextWrite & (BATCH_SIZE-1)) == 0)
			write = nextWrite;

		RLS_LOCK(&lock);
	}

	/// Returns true if we can enqueue at least one more item on the queue.
	/// Actually it check to see if after the next enqueue the queue will be full.
	/// It updates the "localRead" if that would be true.
	bool can_enq() {
		return ((nextWrite+1) != localRead || ((localRead = read) != (nextWrite + 1)));
	}


	T deq() {
		if (nextRead == localWrite) {
			while (nextRead == write) ;
			localWrite = write;
		}

		T data	= elems[nextRead];

		nextRead += 1;
		nextRead &= (QUEUE_SIZE-1);

		if ((nextRead & (BATCH_SIZE-1)) == 0)
			read = nextRead;

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
		GET_LOCK(&lock);
		bool res (nextRead != localWrite || ((localWrite = write) != nextRead));
		RLS_LOCK(&lock);

		return res;
	}

	void fsh() {
		GET_LOCK(&lock);
		write = nextWrite;
		read = nextRead;
		RLS_LOCK(&lock);
	}

};

/// Single Producer and Single Consumer
template <typename T, int QUEUE_SIZE>
class SimpleQueue {
private:
	unsigned int length;
	T* data;
	bool lock;

public:
	SimpleQueue() {
		data = new T[QUEUE_SIZE];
		length = 0;
		lock = Q_UNLOCKED;
	}

	void enq(T elem) {
		GET_LOCK(&lock);
		data[length] = elem;
		length++;
		RLS_LOCK(&lock);
	}

	bool can_enq() {
		GET_LOCK(&lock);
		bool res (length != QUEUE_SIZE);
		RLS_LOCK(&lock);
		return res;
	}

	T deq() {
		GET_LOCK(&lock);
		length--;
		T elem = data[length];
		RLS_LOCK(&lock);
		return elem;
	}

	bool can_deq() {
		GET_LOCK(&lock);
		bool res = (length != 0);
		RLS_LOCK(&lock);
		return res;
	}

	bool try_deq(T* elem) {
		GET_LOCK(&lock);
		bool res = (length != 0);

		if (res) {
			length--;
			*elem = data[length];
		}

		RLS_LOCK(&lock);
		return res;
	}

	~SimpleQueue() {
		delete [] data;
	}
};

#endif /* MCRINGBUFFER_H_ */
