NAME= linear_regression
SRC= linear_regression

CCFLAGS := $(CCFLAGS) -DNODPRINTF #-DFAULTINJECTION

LIBS := -pthread $(LIB)

TX_RUNTIME_FLAGS := $(TX_RUNTIME_FLAGS) -D THRESHOLD=5000

TX_PASS_FLAGS := $(TX_PASS_FLAGS) -called-from-outside=linear_regression_pthread

include ../../Makefile.$(ACTION)
