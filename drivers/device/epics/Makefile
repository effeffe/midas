###########################################################
#
#  General purpose Makefile for Linux and Darwin, SR 2015
#
###########################################################

ifndef MIDASSYS
MIDASSYS=$(HOME)/git/midas
endif

ifndef EPICS
#EPICS = /usr/local/epics/base
EPICS = $(HOME)/git/epics-base
endif

CFLAGS  = -g -O2 -Wall

# search main file
GREP = grep -l "main(" *cxx
MAIN = $(shell $(GREP))
OUTNAME = $(basename $(MAIN))

CFLAGS += -I$(MIDASSYS)/include

# OS specific flags and libs
ifeq ($(shell uname),Linux)
CFLAGS += -I$(EPICS)/include/os/Linux -I$(EPICS)/include -I$(EPICS)/include/compiler/gcc
LIBS    = -pthread -L$(EPICS)/lib/linux-x86_64 -lca
endif

ifeq ($(shell uname),Darwin)
CFLAGS  += -std=gnu++11 -I$(EPICS)/include/os/Darwin -I$(EPICS)/include -I$(EPICS)/include/compiler/clang
#LIBS    = -framework IOKit -framework CoreFoundation -lobjc -L$(EPICS)/lib/darwin-x86 -lca -lCom
LIBS    = -L$(EPICS)/lib/darwin-x86 -lca -lCom
endif

# search *.c and *.cpp files
C_FILES = $(wildcard *.cxx)
C_OBJ = $(C_FILES:%.cxx=%.o)

all: $(OUTNAME) epics_ca.o

$(OUTNAME): $(C_OBJ)
	g++ $(C_OBJ) -o $(OUTNAME) $(LIBS)

$(C_OBJ): %.o: %.cxx
	g++ $(CFLAGS) -c $<

epics_ca.o: %.o: ../%.cxx
	g++ $(CFLAGS) -c $<

clean:
	rm $(C_OBJ) $(OUTNAME)

#end
