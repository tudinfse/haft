NAME= x264
SRC = x264.opt

CCFLAGS := -pthread $(CCFLAGS)

LIBS := -lm $(LIBS)

TX_RUNTIME_FLAGS := $(TX_RUNTIME_FLAGS) -D THRESHOLD=3000

TX_PASS_FLAGS := $(TX_PASS_FLAGS) -called-from-outside=SigIntHandler -called-from-outside=read_frame_thread_int -called-from-outside=x264_slices_write

include ../../Makefile.$(ACTION)

