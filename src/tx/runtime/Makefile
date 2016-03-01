include ../../Makefile.local

DESTS = tx.ll
CLANG = $(LLVM_PATH)clang
override FLAGS += -O3 $(HTM_FLAGS) 

all: $(DESTS)

%.ll: %.c
	$(CLANG) $(FLAGS) -emit-llvm -S $< -o $@

tx.c: $(TX_VERSION)
	cp $< $@

clean:
	rm -f tx.c $(DESTS) *~

