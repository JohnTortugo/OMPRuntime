SOURCES		= $(shell find $(SOURCEDIR) -maxdepth 1 -name '*.cpp')
OBJS		= $(SOURCES:.cpp=.o) 
LIB_NAME	= mtsp.so 

ifeq ($(HOSTNAME), Pequizeiro)
	ITT_LIB=/opt/intel/vtune_amplifier_xe_2015.3.0.403110/lib64/libittnotify.a
	ITT_INC=/opt/intel/vtune_amplifier_xe_2015.3.0.403110/target/linux64/include/
else
	ITT_LIB=
	ITT_INC=
endif

SERRORS		= #-Wno-unused-variable -Wno-format -Wno-error=format -Wno-unused-function -Wno-unused-value
CFLAGS		= -g3 -std=c++11 -fpic $(SERRORS) -I$(ITT_INC)
OPT_LVL		= 

mtsp: $(OBJS) CLANG_COMPLETE
	g++ $(CFLAGS) $(OBJS) $(ITT_LIB) -shared -ldl -o $(LIB_NAME)

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

