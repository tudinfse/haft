NAME= streamcluster
SRC = streamcluster.opt

CCFLAGS := -pthread $(CCFLAGS)

TX_RUNTIME_FLAGS := $(TX_RUNTIME_FLAGS) -D THRESHOLD=1000

TX_PASS_FLAGS := $(TX_PASS_FLAGS) -called-from-outside=_Z14localSearchSubPv

include ../../Makefile.$(ACTION)

