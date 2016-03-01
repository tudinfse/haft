NAME= pca
SRC=  pca

CCFLAGS := $(CCFLAGS) -DNODPRINTF #-DFAULTINJECTION

LIBS := -pthread $(LIB)

TX_RUNTIME_FLAGS := $(TX_RUNTIME_FLAGS) -D THRESHOLD=1000

TX_PASS_FLAGS := $(TX_PASS_FLAGS) -called-from-outside=calc_mean -called-from-outside=calc_cov

include ../../Makefile.$(ACTION)

