NAME= string_match
SRC=  string_match

CCFLAGS := $(CCFLAGS) -DNODPRINTF #-DFAULTINJECTION

LIBS := -pthread $(LIB)

TX_RUNTIME_FLAGS := $(TX_RUNTIME_FLAGS) -D THRESHOLD=5000

TX_PASS_FLAGS := $(TX_PASS_FLAGS) -called-from-outside=string_match_map

include ../../Makefile.$(ACTION)

