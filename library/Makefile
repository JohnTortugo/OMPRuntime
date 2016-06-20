CROSS =	arm-linux-gnueabihf-g++-5 -mcpu=cortex-a9 
CFLAGS = --std=gnu++11 -g -O0
DBG_LVL = 0
EXTRA_DBG_LVL = 4
DEFINES = 

ifeq ($(ARCH),arm)
	CC=$(CROSS) -mcpu=cortex-a9
else
	CC=g++
endif

shared: tioglib.cpp mem_interface.cpp 
	$(CC) $(CFLAGS) $(DEFINES) $? -DDBG_VAL=$(DBG_LVL)  -shared -fPIC -o libtioga.so

debug-shared: tioglib.cpp mem_interface.cpp 
	$(CC) $(CFLAGS) $(DEFINES) $? -DDBG_VAL=$(EXTRA_DBG_LVL) -shared -fPIC -o libtioga.so

all: shared debug-shared

clean:
	rm *.so 2> /dev/null || true
