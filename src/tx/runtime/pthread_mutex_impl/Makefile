.PHONY: clean

all: check_mutex_impl.exe

clean:
	rm check_mutex_impl.exe

check_mutex_impl.exe: check_mutex_impl.c
	gcc -O3 -pthread -o $@ $^
