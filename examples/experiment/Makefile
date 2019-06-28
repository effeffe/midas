#
# Makefile for the MIDAS examples/experiment frontend and analyzer
#

# The MIDASSYS should be defined prior the use of this Makefile
ifndef MIDASSYS
missmidas::
	@echo "...";
	@echo "Missing definition of environment variable 'MIDASSYS' !";
	@echo "...";
endif

# get OS type from shell
OSTYPE = $(shell uname)

# Linux settings
ifeq ($(OSTYPE),Linux)
OSTYPE = linux
endif

ifeq ($(OSTYPE),linux)
LIBS = -lm -lz -lutil -lnsl -lpthread -lrt
endif

# MacOSX/Darwin settings
ifeq ($(OSTYPE),Darwin)
OSTYPE = darwin
endif

ifeq ($(OSTYPE),darwin)
LIBS = -lpthread -lz
endif

# add this to compile against 32-bit midas (make linux32)
# OSFLAGS += -m32
# OS_DIR := linux-m32

#-----------------------------------------
# ROOT flags and libs
#
ifdef ROOTSYS
ROOTCFLAGS := $(shell  $(ROOTSYS)/bin/root-config --cflags)
ROOTCFLAGS += -DHAVE_ROOT
ROOTLIBS   := $(shell  $(ROOTSYS)/bin/root-config --libs) -Wl,-rpath,$(ROOTSYS)/lib
ROOTLIBS   += -lThread
endif

# location of MIDAS things
INC_DIR = $(MIDASSYS)/include
ifdef OS_DIR
LIB_DIR = $(MIDASSYS)/$(OS_DIR)/lib
else
LIB_DIR = $(MIDASSYS)/lib
endif

# midas library
LIB = $(LIB_DIR)/libmidas.a

# compiler settings
CFLAGS += -O2 -g -Wall -Wformat=2 -Wno-format-nonliteral -Wno-strict-aliasing -Wuninitialized -Wno-unused-function
CFLAGS += -I$(INC_DIR)
LDFLAGS +=

# list of programs to build
PROGS:=
PROGS+= frontend
ifdef ROOTSYS
PROGS+= analyzer
endif

all: $(PROGS)

frontend: $(LIB_DIR)/mfe.o frontend.o $(LIB)
	$(CXX) -o $@ $(CFLAGS) $(OSFLAGS) $^ $(LDFEFLAGS) $(LIBS)

analyzer: $(LIB_DIR)/rmana.o analyzer.o adccalib.o adcsum.o scaler.o $(LIB)
	$(CXX) -o $@ $(CFLAGS) $(OSFLAGS) $^ $(LDFLAGS) $(ROOTLIBS) $(LIBS)

%.o: %.cxx
	$(CXX) -o $@ -c $(USERFLAGS) $(ROOTCFLAGS) $(CFLAGS) $(OSFLAGS) $<

clean::
	-rm -f $(PROGS)
	-rm -f *.o *~ \#*

#end file
