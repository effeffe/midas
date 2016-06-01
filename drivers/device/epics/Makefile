###########################################################
#
#  General purpose Makefile for Linux and Darwin, SR 2015
#
###########################################################

CFLAGS  = -g -O0

# search main file
GREP = grep -l "main(" *c
MAIN = $(shell $(GREP))
OUTNAME = $(basename $(MAIN))

# OS specific flags and libs
ifeq ($(shell uname),Linux)
CFLAGS += -I/usr/local/epics/base/include/os/Linux -I/usr/local/epics/base/include
LIBS    = -pthread -L/usr/local/epics/base/lib/SL5-x86_64 -lca
endif

ifeq ($(shell uname),Darwin)
CFLAGS  += -std=gnu++11 -x c++ -I/usr/local/epics/base/include/os/Darwin -I/usr/local/epics/base/include
LIBS    = -framework IOKit -framework CoreFoundation -lobjc /usr/local/epics/base/lib/darwin-x86/libca.3.14.12.dylib
endif

# search *.c and *.cpp files
C_FILES = $(wildcard *.c)
C_OBJ = $(C_FILES:%.c=%.o)

all: $(OUTNAME)

$(OUTNAME): $(C_OBJ)
	gcc $(C_OBJ) -o $(OUTNAME) $(LIBS)

$(C_OBJ): %.o: %.c
	gcc $(CFLAGS) -c $<

clean:
	rm $(C_OBJ) $(OUTNAME)
