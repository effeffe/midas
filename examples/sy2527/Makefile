#####################################################################
#
#  Name:         Makefile
#  Created by:   Stefan Ritt
#
#  Contents:     UNIX Makefile for MIDAS slow control frontend
#
#  $Id: Makefile 2779 2005-10-19 13:14:36Z ritt $
#
#####################################################################

#--------------------------------------------------------------------
# The following lines contain specific switches for different UNIX
# systems. Find the one which matches your OS and outcomment the 
# lines below.

# This is for Linux ----------------
LIBS = -lbsd -lm -lutil -lpthread -lrt
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
# The following lines define directories. Adjust if necessary
#                 
INC_DIR 	= $(MIDASSYS)/include
LIB_DIR 	= $(MIDASSYS)/linux/lib
DRV_DIR		= $(MIDASSYS)/drivers
#CAEN_DIR        = $(HOME)/packages/CAENHVWrapper_2_11-LX
#CAEN_DIR        = $(HOME)/packages/CAENHVWrapper-3.10
CAEN_DIR        = $(HOME)/packages/CAENHVWrapper-5.82
CAEN_LIB        = $(CAEN_DIR)/lib/x64
CAEN_LIBS       = $(CAEN_LIB)/libcaenhvwrapper.so.5.82
#-------------------------------------------------------------------
# Drivers needed by the frontend program
#                 
DRIVERS         = hv.o dd_sy2527.o null.o 

####################################################################
# Lines below here should not be edited
####################################################################

LIB = $(LIB_DIR)/libmidas.a $(CAEN_LIBS) -lncurses -Wl,-rpath,$(CAEN_LIB)

# compiler
CC = cc
CFLAGS = -g -Wall -DUNIX -I$(INC_DIR) -I$(DRV_DIR) -I$(CAEN_DIR)/include
LDFLAGS = 

all: fesy2527.exe

fesy2527.exe:  $(LIB_DIR)/mfe.o sy2527.o $(DRIVERS)
	$(CC) -o $@ sy2527.o $(LIB_DIR)/mfe.o $(DRIVERS) $(LDFLAGS) $(LIB) $(LIBS)

hv.o: $(DRV_DIR)/class/hv.c
	$(CC) $(CFLAGS) $(OSFLAGS) -c $< -o $@

#hv.o: hv.c
#	$(CC) $(CFLAGS) $(OSFLAGS) -c $< -o $@

dd_sy2527.o: $(DRV_DIR)/device/dd_sy2527.c
	$(CC) $(CFLAGS) $(OSFLAGS) -c $< -o $@

#dd_sy2527.o: dd_sy2527.c
#	$(CC) $(CFLAGS) $(OSFLAGS) -c $< -o $@

nulldev.o: $(DRV_DIR)/device/nulldev.c
	$(CC) $(CFLAGS) $(OSFLAGS) -c $< -o $@

null.o: $(DRV_DIR)/bus/null.c
	$(CC) $(CFLAGS) $(OSFLAGS) -c $< -o $@

.c.o:
	$(CC) $(CFLAGS) $(OSFLAGS) -c $<

clean::
	-rm -f *.o *~ \#*

clean::
	-rm -f *.exe

#end
