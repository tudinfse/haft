NAME= word_count
SRC=  word_count

CCFLAGS := $(CCFLAGS) -DNODPRINTF #-DFAULTINJECTION

LIBS := -pthread $(LIB)

TX_RUNTIME_FLAGS := $(TX_RUNTIME_FLAGS) -D THRESHOLD=3000

TX_PASS_FLAGS := $(TX_PASS_FLAGS) -called-from-outside=wordcount_map -called-from-outside=merge_sections -called-from-outside=sort_section -called-from-outside=wordcount_cmp

include ../../Makefile.$(ACTION)

