NAME= vips
SRC = vips.opt

CCFLAGS := -pthread $(CCFLAGS)

LIBS := -lm -ldl -lstdc++ $(LIBS)

TX_RUNTIME_FLAGS := $(TX_RUNTIME_FLAGS) -D THRESHOLD=5000

TX_PASS_FLAGS := $(TX_PASS_FLAGS) -called-from-outside=_events_thread

TX_PASS_FLAGS := $(TX_PASS_FLAGS) -func-explicit-trans

include ../../Makefile.$(ACTION)

