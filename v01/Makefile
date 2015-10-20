SOURCES		= $(shell find $(SOURCEDIR) -maxdepth 1 -name '*.cpp')
OBJS		= $(SOURCES:.cpp=.o) 
LIB_NAME	= mtsp.so 

CFLAGS		= -g -std=c++11  -Wno-attributes -Wall -Werror -fpic -Wno-unused-function -Wno-unused-value -I/opt/intel/vtune_amplifier_xe_2015.3.0.403110/target/linux64/include/ 
OPT_LVL		= -O3

mtsp: $(OBJS)
	g++ $(CFLAGS) $(OBJS) /opt/intel/vtune_amplifier_xe_2015.3.0.403110/lib64/libittnotify.a -shared -ldl -o $(LIB_NAME)

install: $(LIB_NAME)
	sudo cp $(LIB_NAME) /usr/lib/libmtsp.so

clean:
	rm -rf $(OBJS) $(LIB_NAME)
	
remake:
	@make clean
	@make
	
%.o: %.cpp
	g++ -c $(CFLAGS) $(OPT_LVL) $< -o $@
