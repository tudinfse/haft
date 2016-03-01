NAME= blackscholes
SRC = blackscholes.opt

CCFLAGS := -pthread $(CCFLAGS)

LIBS := -lm $(LIBS)

TX_RUNTIME_FLAGS := $(TX_RUNTIME_FLAGS) -D THRESHOLD=5000

TX_PASS_FLAGS := $(TX_PASS_FLAGS) -called-from-outside=_Z9bs_threadPv

include ../../Makefile.$(ACTION)

