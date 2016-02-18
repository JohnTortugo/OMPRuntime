SOURCES		= $(shell find $(SOURCEDIR) -maxdepth 1 -name '*.cpp')
OBJS		= $(SOURCES:.cpp=.o)
LIB_NAME	= mtsp_bridge.so

SERRORS		= -Wno-unused-variable -Wno-format -Wno-error=format -Wno-unused-function -Wno-unused-value
CFLAGS		= -g -std=c++11 -Wall -Werror -fpic $(SERRORS)
INCLUDES	= -I"../../tga_driver/library"
OPT_LVL		= -O3

ifdef DBG
	LINEAR_DEBUG = -DLINEAR_DEBUG
endif

mtsp: $(OBJS)
	g++ $(OBJS) $(INCLUDES) -shared -L./ -ltga -ltioga -o $(LIB_NAME)

clean:
	rm -rf $(OBJS) $(LIB_NAME)
	
remake:
	@make clean
	@make
	
%.o: %.cpp
	g++ -c $(CFLAGS) $(INCLUDES) $(OPT_LVL) $< -o $@ $(LINEAR_DEBUG)
