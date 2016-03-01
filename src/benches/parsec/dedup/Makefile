NAME= dedup
SRC = dedup.opt

CCFLAGS := -pthread $(CCFLAGS)

TX_RUNTIME_FLAGS := $(TX_RUNTIME_FLAGS) -D THRESHOLD=500

TX_PASS_FLAGS := $(TX_PASS_FLAGS) -called-from-outside=Fragment -called-from-outside=FragmentRefine -called-from-outside=Deduplicate -called-from-outside=Compress -called-from-outside=Reorder

include ../../Makefile.$(ACTION)

