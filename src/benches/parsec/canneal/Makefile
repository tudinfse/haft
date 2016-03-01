NAME= canneal
SRC = canneal.opt

CCFLAGS := -pthread $(CCFLAGS)

LIBS := -lm $(LIBS)

TX_RUNTIME_FLAGS := $(TX_RUNTIME_FLAGS) -D THRESHOLD=3000

TX_PASS_FLAGS := $(TX_PASS_FLAGS) -called-from-outside=_Z8entry_ptPv

include ../../Makefile.$(ACTION)

