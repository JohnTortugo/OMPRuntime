SOURCES		= $(shell find $(SOURCEDIR) -maxdepth 1 -name '*.cpp')
OBJS		= $(SOURCES:.cpp=.o)
LIB_NAME	= mtsp.so

SERRORS		= -Wno-unused-variable -Wno-format -Wno-error=format -Wno-unused-function -Wno-unused-value
CFLAGS		= -g -std=gnu++11 -Wall -Werror -fpic $(SERRORS)
INCLUDES	= -I"../../tga_driver/library"
OPT_LVL		= -O0

ifdef DBG
	LINEAR_DEBUG = -DLINEAR_DEBUG
endif

ifeq ($(ARCH),arm)
CC	= arm-linux-gnueabihf-g++-5 -mcpu=cortex-a9
else
CC	= g++
endif

mtsp: $(OBJS)
	$(CC) $(OBJS) $(INCLUDES) -shared -L./ -ltioga -o $(LIB_NAME)

static: $(OBJS)
	ar rcs libmtsp.a $(OBJS) tioglib.o mem_interface.o

clean:
	rm -rf $(OBJS) $(LIB_NAME)
	
remake:
	@make clean
	@make
	
%.o: %.cpp
	$(CC) -c $(CFLAGS) $(INCLUDES) $(OPT_LVL) $< -o $@ $(LINEAR_DEBUG)
