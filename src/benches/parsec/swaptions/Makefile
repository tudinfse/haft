NAME= swaptions
SRC = swaptions.opt

CCFLAGS := -pthread $(CCFLAGS)

TX_RUNTIME_FLAGS := $(TX_RUNTIME_FLAGS) -D THRESHOLD=3000

TX_PASS_FLAGS := $(TX_PASS_FLAGS) -called-from-outside=_Z6workerPv

include ../../Makefile.$(ACTION)


