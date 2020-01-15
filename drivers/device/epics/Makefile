###########################################################
#
#  General purpose Makefile for Linux and Darwin, SR 2015
#
###########################################################

#EPICS = /usr/local/epics/base
EPICS = $(HOME)/git/epics-base

CFLAGS  = -g -O0

# search main file
GREP = grep -l "main(" *cxx
MAIN = $(shell $(GREP))
OUTNAME = $(basename $(MAIN))

# OS specific flags and libs
ifeq ($(shell uname),Linux)
CFLAGS += -I$(EPICS)/include/os/Linux -I$(EPICS)/include
LIBS    = -pthread -L$(EPICS)/lib/SL5-x86_64 -lca
endif

ifeq ($(shell uname),Darwin)
CFLAGS  += -std=gnu++11 -x c++ -I$(EPICS)/include/os/Darwin -I$(EPICS)/include -I$(EPICS)/include/compiler/clang
#LIBS    = -framework IOKit -framework CoreFoundation -lobjc -L$(EPICS)/lib/darwin-x86 -lca -lCom
LIBS    = -L$(EPICS)/lib/darwin-x86 -lca -lCom
endif

# search *.c and *.cpp files
C_FILES = $(wildcard *.cxx)
C_OBJ = $(C_FILES:%.cxx=%.o)

all: $(OUTNAME)

$(OUTNAME): $(C_OBJ)
	g++ $(C_OBJ) -o $(OUTNAME) $(LIBS)

$(C_OBJ): %.o: %.cxx
	g++ $(CFLAGS) -c $<

clean:
	rm $(C_OBJ) $(OUTNAME)

#end
