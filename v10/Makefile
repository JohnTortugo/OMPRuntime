SOURCES		= $(shell find $(SOURCEDIR) -maxdepth 1 -name '*.cpp')
OBJS		= $(SOURCES:.cpp=.o) 
LIB_NAME	= mtsp.so 
USE_ITTN	= 0

OPT_LVL	= 
SERRORS	= #-Wno-unused-variable -Wno-format -Wno-error=format -Wno-unused-function -Wno-unused-value
CFLAGS  = -I../library/
CFLAGS	+= -g -std=c++11 -fpic $(SERRORS)

ifeq ($(USE_ITTN), 1)
	CFLAGS += -L/opt/intel/vtune_amplifier_xe_2016.1.1.434111/lib64/libittnotify.a
	CFLAGS += -I/opt/intel/vtune_amplifier_xe_2016.1.1.434111/include/
	CFLAGS += -DUSE_ITTN=1
endif

ifeq ($(ARCH),arm)
ifndef CROSS
$(error "You need to set the CROSS parameter to the name of the cross compiler.")
endif
	CC=$(CROSS)
else
	CC=g++
endif

mtsp: $(OBJS) 
	$(CC) $(CFLAGS) $(OBJS) -shared -ldl -o $(LIB_NAME)

install: $(LIB_NAME)
	sudo cp $(LIB_NAME) /usr/lib/libmtsp.so

clean:
	rm -rf $(OBJS) $(LIB_NAME)

run: mtsp

rebuild-run:
	@make clean
	@make mtsp
	
%.o: %.cpp
	$(CC) -c $(CFLAGS) $(OPT_LVL) $< -o $@
