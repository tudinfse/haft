NAME= ferret
SRC = ferret-pthreads.opt

CCFLAGS := -pthread $(CCFLAGS)

LIBS := -lm $(LIBS)

TX_RUNTIME_FLAGS := $(TX_RUNTIME_FLAGS) -D THRESHOLD=5000

TX_PASS_FLAGS := $(TX_PASS_FLAGS) -called-from-outside=t_load -called-from-outside=t_seg -called-from-outside=t_extract -called-from-outside=t_vec -called-from-outside=t_rank -called-from-outside=t_out

TX_PASS_FLAGS := $(TX_PASS_FLAGS) -func-explicit-trans

include ../../Makefile.$(ACTION)

