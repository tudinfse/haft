NAME= libc-util
SRC = bzero memcpy memmove memset memcmp memchr strcmp strncmp strcat strlen strcpy strncpy strchr strrchr strstr strcasecmp strncasecmp strspn strchrnul strcspn strpbrk \
      isdigit islower isspace isupper toupper tolower \
      exp exp2 sqrt sqrtf log log10 scalbn scalbnf fabs fabsf pow powf modf modff modfl ceil ceilf finite floor floorf cbrt cbrtf ldexp ldexpf nan frexp hypot
SRC2= main_dummy

CCFLAGS := $(CCFLAGS) #-DPRINTDEBUG

# we need to disable usage of llvm.mem* intrinsics, otherwise have recursion
CCFLAGS := $(CCFLAGS) -fno-builtin

include ../../Makefile.$(ACTION)



