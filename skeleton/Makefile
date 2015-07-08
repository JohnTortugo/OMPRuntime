SOURCES		= $(shell find $(SOURCEDIR) -maxdepth 1 -name '*.cpp')
OBJS		= $(SOURCES:.cpp=.o)
LIB_NAME	= mtsp.so

CFLAGS		= -g -Wall -Werror -fpic
OPT_LVL		= -O3

mtsp: $(OBJS)
	g++ $(OBJS) -shared -o $(LIB_NAME)

clean:
	rm -rf $(OBJS) $(LIB_NAME)
	
remake:
	@make clean
	@make
	
%.o: %.cpp
	g++ -c $(CFLAGS) $(OPT_LVL) $< -o $@
