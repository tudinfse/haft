NAME= matrix_multiply
SRC= matrix_multiply

CCFLAGS := $(CCFLAGS) -DNODPRINTF #-DFAULTINJECTION

LIBS := -pthread $(LIB)

TX_RUNTIME_FLAGS := $(TX_RUNTIME_FLAGS) -D THRESHOLD=3000

TX_PASS_FLAGS := $(TX_PASS_FLAGS) -called-from-outside=matrixmult_map

include ../../Makefile.$(ACTION)
