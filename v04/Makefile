SOURCES		= $(shell find $(SOURCEDIR) -maxdepth 1 -name '*.cpp')
OBJS		= $(SOURCES:.cpp=.o) 
LIB_NAME	= mtsp.so 

SERRORS		= -Wno-unused-variable -Wno-format -Wno-error=format -Wno-unused-function -Wno-unused-value
CFLAGS		= -g -std=c++11 -Werror -fpic $(SERRORS) -I/opt/intel/vtune_amplifier_xe_2015.4.0.410668/include/ 
OPT_LVL		= -O3

mtsp: $(OBJS) CLANG_COMPLETE
	g++ $(CFLAGS) $(OBJS) /opt/intel/vtune_amplifier_xe/lib64/libittnotify.a -shared -ldl -o $(LIB_NAME)

install: $(LIB_NAME)
	sudo cp $(LIB_NAME) /usr/lib/libmtsp.so

clean:
	rm -rf $(OBJS) $(LIB_NAME)
	
run: mtsp

rebuild-run:
	@make clean
	@make mtsp
	
%.o: %.cpp
	g++ -c $(CFLAGS) $(OPT_LVL) $< -o $@


###############################################################################
######### This will update the .clang_complete file with all sources and CFLAGS
CLANG_COMPLETE:
	$(shell echo -std=c++11 > .clang_complete)
	$(foreach src, $(SOURCES), $(shell echo -include $(src) >> .clang_complete))
	$(foreach flg, $(CFLAGS), $(shell echo $(flg) >> .clang_complete))

