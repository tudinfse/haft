NAME= kmeans
SRC= kmeans

CCFLAGS := $(CCFLAGS) -DNODPRINTF #-DFAULTINJECTION

TX_RUNTIME_FLAGS := $(TX_RUNTIME_FLAGS) -D THRESHOLD=5000

TX_PASS_FLAGS := $(TX_PASS_FLAGS) -called-from-outside=find_clusters -called-from-outside=calc_means

LIBS := -pthread $(LIBS)

include ../../Makefile.$(ACTION)
