#####################################################################
#
#  Name:         Makefile
#  Created by:   Stefan Ritt
#
#  Contents:     UNIX Makefile for MIDAS EPICS slow control frontend
#
#  $Id$
#
#####################################################################

ifndef MIDASSYS
MIDASSYS=$(HOME)/git/midas
endif

ifndef EPICS
EPICS=$(HOME)/git/epics-base
endif

#--------------------------------------------------------------------
# The following lines contain specific switches for different UNIX
# systems. Find the one which matches your OS and outcomment the 
# lines below.

# This is for Linux ----------------
#LIBS = -lbsd -lm -lutil -lpthread -lrt
#OSFLAGS = -DOS_LINUX

# This is for MacOS ----------------
LIBS = 
OSFLAGS = -DOS_LINUX

# This is for OSF1 -----------------
#LIBS = -lbsd
#OSFLAGS = -DOS_OSF1

# This is for Ultrix ---------------
#LIBS =
#OSFLAGS = -DOS_ULTRIX -Dextname

# This is for FreeBSD --------------
#LIBS = -lbsd -lcompat
#OSFLAGS = -DOS_FREEBSD

#-------------------------------------------------------------------
# The following lines define direcories. Adjust if necessary
#                 
INC_DIR 	= $(MIDASSYS)/include
LIB_DIR 	= $(MIDASSYS)/lib
DRV_DIR		= $(MIDASSYS)/drivers
EPICS_DIR       = $(EPICS)
#EPICS_LIBDIR    = $(EPICS)/lib/linux-x86_64
EPICS_LIBDIR    = $(EPICS)/lib/darwin-x86
EPICS_CFLAGS    = -I$(EPICS)/include -I$(EPICS)/include/os/Darwin -I$(EPICS)/include/compiler/clang
#-------------------------------------------------------------------
# Drivers needed by the frontend program
#                 
DRIVERS         = generic.o epics_ca.o

####################################################################
# Lines below here should not be edited
####################################################################

LIB = $(LIB_DIR)/libmidas.a $(EPICS_LIBDIR)/libca.a $(EPICS_LIBDIR)/libCom.a

# compiler
CC = gcc
CXX = g++
CFLAGS = -g  -D_POSIX_C_SOURCE=199506L -D_POSIX_THREADS -D_XOPEN_SOURCE=500 \
	-DOSITHREAD_USE_DEFAULT_STACK -D_X86_ -DUNIX -D_BSD_SOURCE -Dlinux \
	-I. -I$(INC_DIR) -I$(DRV_DIR) $(EPICS_CFLAGS)
LDFLAGS =

all: fe_epics

fe_epics:  $(LIB) $(LIB_DIR)/mfe.o frontend.o $(DRIVERS)
	$(CXX) $(CFLAGS) -o fe_epics frontend.o $(LIB_DIR)/mfe.o $(DRIVERS) $(LIB) $(LDFLAGS) $(LIBS)

frontend.o: frontend.c
	$(CXX) $(CFLAGS) $(OSFLAGS) -c $< -o $@

generic.o: $(DRV_DIR)/class/generic.cxx
	$(CXX) $(CFLAGS) $(OSFLAGS) -c $< -o $@

epics_ca.o: $(DRV_DIR)/device/epics_ca.cxx
	$(CXX) $(CFLAGS) $(OSFLAGS) -c $< -o $@

.c.o:
	$(CXX) $(CFLAGS) $(OSFLAGS) -c $<

clean:
	rm -f *.o *~ \#*
