#####################################################################
#
#  Name:         Makefile
#  Created by:   Stefan Ritt
#
#  Contents:     Makefile for MIDAS binaries and examples under unix
#
#  $Id$
#
#####################################################################
#
# Usage:
#        gmake             To compile the MIDAS library and binaries
#        gmake install     To install the library and binaries
#        gmake examples    To compile low-level examples (not necessary)
#        gmake static      To make static executables for debugging
#
#
# This makefile contains conditional code to determine the operating
# system it is running under by evaluating the OSTYPE environment
# variable. In case this does not work or if the GNU make is not
# available, the conditional code has to be evaluated manually.
# Remove all ifeq - endif blocks except the one belonging to the
# current operating system. From this block remove only the first
# and the last line (the one with the ifeq and endif statement).
#
# "gmake install" will copy MIDAS binaries, the midas library and
# the midas include files to specific directories for each version.
# You may change these directories to match your preferences.
#####################################################################

# get OS type from shell
OSTYPE = $(shell uname)

#
# System direcotries for installation, modify if needed
#

ifndef PREFIX
PREFIX     = /usr/local
endif

SYSBIN_DIR = $(PREFIX)/bin
SYSLIB_DIR = $(PREFIX)/lib
SYSINC_DIR = $(PREFIX)/include

#
# Configurable and optional packages:
#
# ROOT  - mlogger output and analyzer
# MYSQL - mlogger begin/end of run info and history
# ODBC  - history
# SQLITE - history
# MSCB  - mhttpd
#
# In C/C++ code, optional features are controlled by "#ifdef HAVE_xxx", i.e. "#ifdef HAVE_ROOT"
# This is passed from the Makefile as -DHAVE_xxx, i.e. -DHAVE_ROOT
# In the Makefile, optional features are controlled by HAVE_xxx, i.e. "ifdef HAVE_ROOT"
# Autodetection of optional features is below, it sets HAVE_xxx if feature is present, leaves HAVE_xxx unset if feature is absent
# To explicitely disable an optional feature, use NO_xxx, i.e. "make NO_ROOT=1" to disable ROOT.
# (These NO_xxx Makefile variables are only used to control autodetection, they do not appear in the rest of the Makefile)
#

#
# Optional ROOT support for rmlogger
#
ifndef NO_ROOT
HAVE_ROOT := $(shell root-config --version 2> /dev/null)
endif

#
# Optional MYSQL library for mlogger and for the history
#
ifndef NO_MYSQL
HAVE_MARIADB := $(shell mariadb_config --include 2> /dev/null)
HAVE_MYSQL := $(shell mysql_config --include 2> /dev/null)
endif

#
# Optional ODBC history support
#
ifndef NO_ODBC
HAVE_ODBC := $(shell if [ -e /usr/include/sql.h ]; then echo 1; fi)
endif

#
# Optional SQLITE history support
#
ifndef NO_SQLITE
HAVE_SQLITE := $(shell if [ -e /usr/include/sqlite3.h ]; then echo 1; fi)
endif

#
# Optional SSL support
#
#
#NO_SSL=1
#

#
# Optional stack trace support
#
#
#NO_EXECINFO=1
#

#
# Optional fork() support
#
#
#NO_NOFORK=1
#

#
# Optional shared library libmidas-shared.so
#
#
#NO_SHLIB=1
#

#
# Option to use our own implementation of strlcat, strlcpy
#
NEED_STRLCPY=1

#
# Directory in which mxml.cxx/h resides. This library has to be checked
# out separately from the midas CVS since it's used in several projects
#
MXML_DIR=mxml

#
# Directory in which mscb.cxx/h resides. These files are necessary for
# the optional MSCB support in mhttpd
#
MSCB_DIR=mscb

#
# Indicator that MSCB is available
#
ifndef NO_MSCB
HAVE_MSCB := $(shell if [ -e $(MSCB_DIR) ]; then echo 1; fi)
endif

#
# Optional zlib support for data compression in the mlogger and in the analyzer
#
NEED_ZLIB=

#
# Optional nvidia gpu support, HAVE_NVIDIA is a count of CPUs
#
ifndef NO_NVIDIA
HAVE_NVIDIA := $(shell nvidia-smi -L  2> /dev/null | grep GPU )
endif

#####################################################################
# Nothing needs to be modified after this line 
#####################################################################

#-----------------------
# Common flags
#
CC = gcc $(USERFLAGS)
CXX = g++ $(USERFLAGS)
CFLAGS = -g -O2 -Wall -Wformat=2 -Wno-format-nonliteral -Wno-strict-aliasing -Wuninitialized -I$(INC_DIR) -I$(DRV_DIR) -I$(MXML_DIR) -I$(MSCB_DIR)/include -DHAVE_FTPLIB

#-----------------------
# Cross-compilation, change GCC_PREFIX
#
ifeq ($(OSTYPE),crosscompile)
GCC_PREFIX=$(HOME)/linuxdcc/Cross-Compiler/gcc-4.0.2/build/gcc-4.0.2-glibc-2.3.6/powerpc-405-linux-gnu
GCC_BIN=$(GCC_PREFIX)/bin/powerpc-405-linux-gnu-
LIBS=-L$(HOME)/linuxdcc/userland/lib -pthread -lutil -lrt -ldl
CC  = $(GCC_BIN)gcc
CXX = $(GCC_BIN)g++
OSTYPE = cross-ppc405
OS_DIR = $(OSTYPE)
CFLAGS += -DOS_LINUX
NO_MYSQL=1
NO_ODBC=1
NO_SQLITE=1
# For cross compilation targets lacking openssl, define -DNO_SSL
#CFLAGS += -DNO_SSL
endif

#-----------------------
# ARM Cross-compilation, change GCC_PREFIX
#
ifeq ($(OSTYPE),crosslinuxarm)

ifndef USE_TI_ARM
USE_YOCTO_ARM=1
endif

ifdef USE_TI_ARM
# settings for the TI Sitara ARM SDK
TI_DIR=/ladd/data0/olchansk/MityARM/TI/ti-sdk-am335x-evm-06.00.00.00/linux-devkit/sysroots/i686-arago-linux/usr
TI_EXT=arm-linux-gnueabihf
GCC_BIN=$(TI_DIR)/bin/$(TI_EXT)-
LIBS=-Wl,-rpath,$(TI_DIR)/$(TI_EXT)/lib -pthread -lutil -lrt -ldl
endif

ifdef USE_YOCTO_ARM
# settings for the Yocto poky cross compilers
POKY_DIR=/ladd/data0/olchansk/MityARM/Yocto/opt/poky/1.5/sysroots
POKY_EXT=arm-poky-linux-gnueabi
POKY_HOST=x86_64-pokysdk-linux
POKY_ARM=armv7a-vfp-neon-poky-linux-gnueabi
GCC_BIN=$(POKY_DIR)/$(POKY_HOST)/usr/bin/$(POKY_EXT)/$(POKY_EXT)-
LIBS=--sysroot $(POKY_DIR)/$(POKY_ARM) -Wl,-rpath,$(POKY_DIR)/$(POKY_ARM)/usr/lib -pthread -lutil -lrt -ldl
CFLAGS += --sysroot $(POKY_DIR)/$(POKY_ARM)
endif

CC  = $(GCC_BIN)gcc
CXX = $(GCC_BIN)g++
OSTYPE = linux-arm
OS_DIR = $(OSTYPE)
CFLAGS += -DOS_LINUX
CFLAGS += -fPIC
NO_MYSQL=1
NO_ODBC=1
NO_SQLITE=1
NO_ROOT=1
SSL_LIBS += -lssl -lcrypto
endif

#-----------------------
# ucLinux cross-compilation using emcraft tools
#
# To use, before running this Makefile,
# cd /home/agdaq/online/firmware/cdm/emcraft
# source ACTIVATE.sh
#
ifeq ($(OSTYPE),crosslinuxemcraft)
#GCC_PREFIX=$(HOME)/linuxdcc/Cross-Compiler/gcc-4.0.2/build/gcc-4.0.2-glibc-2.3.6/powerpc-405-linux-gnu
#GCC_BIN=$(GCC_PREFIX)/bin/powerpc-405-linux-gnu-
#LIBS=-L$(HOME)/linuxdcc/userland/lib -pthread -lutil -lrt -ldl
LIBS= -L$(INSTALL_ROOT)/A2F/root/usr/lib -pthread -lutil -lrt
GCC_BIN := arm-uclinuxeabi-
CC  = $(GCC_BIN)gcc
CXX = $(GCC_BIN)g++
#OSTYPE = cross-ppc405
OS_DIR = linux-emcraft
CFLAGS += -mcpu=cortex-m3 -mthumb
CFLAGS += -DOS_LINUX
NEED_STRLCPY=
CFLAGS += -DHAVE_STRLCPY
NO_SHLIB=1
NO_MYSQL=1
NO_ODBC=1
NO_SQLITE=1
NO_SSL=1
NO_EXECINFO=1
NO_FORK=1
# For cross compilation targets lacking openssl, define -DNO_SSL
CFLAGS += -DNO_SSL
ifdef NO_EXECINFO
CFLAGS += -DNO_EXECINFO
endif
ifdef NO_FORK
CFLAGS += -DNO_FORK
endif
endif

#-----------------------
# OSF/1 (DEC UNIX)
#
ifeq ($(HOSTTYPE),alpha)
OSTYPE = osf1
endif

ifeq ($(OSTYPE),osf1)
#OS_DIR = osf1
OSFLAGS = -DOS_OSF1
FFLAGS = -nofor_main -D 40000000 -T 20000000
LIBS = -lc -lbsd
SPECIFIC_OS_PRG = 
endif

#-----------------------
# Ultrix
#
ifeq ($(HOSTTYPE),mips)
OSTYPE = ultrix
endif

ifeq ($(OSTYPE),ultrix)
#OS_DIR = ultrix
OSFLAGS = -DOS_ULTRIX -DNO_PTY
LIBS =
SPECIFIC_OS_PRG = 
endif

#-----------------------
# FreeBSD
#
ifeq ($(OSTYPE), FreeBSD)
#OS_DIR = freeBSD
OSFLAGS = -DOS_FREEBSD
LIBS = -lbsd -lcompat
SPECIFIC_OS_PRG = 
endif

#-----------------------
# MacOSX/Darwin is just a funny Linux
#
ifeq ($(OSTYPE),Darwin)
OSTYPE = darwin
endif

ifeq ($(OSTYPE),darwin)
#OS_DIR = darwin
OSFLAGS = -DOS_LINUX -DOS_DARWIN -fPIC -Wno-unused-function
LIBS = -lpthread
SPECIFIC_OS_PRG = $(BIN_DIR)/mlxspeaker
NEED_ZLIB=1
NEED_STRLCPY=
NEED_RANLIB=1
SHLIB+=$(LIB_DIR)/libmidas-shared.dylib
SHLIB+=$(LIB_DIR)/libmidas-shared.so
# use the macports version of openssl
CFLAGS += -I/opt/local/include
SSL_LIBS = -L/opt/local/lib -lssl -lcrypto
endif

#-----------------------
# This is for Cygwin
#
ifeq ($(OSTYPE),CYGWIN_NT-5.1)
OSTYPE = cygwin
endif

ifeq ($(OSTYPE),cygwin)

#OS_DIR = cygwin
OSFLAGS = -DOS_LINUX -DOS_CYGWIN -Wno-unused-function
LIBS = -lutil -lpthread
endif

#-----------------------
# This is for Linux
#
ifeq ($(OSTYPE),Linux)
OSTYPE = linux
endif

ifeq ($(OSTYPE),linux)

# >2GB file support
CFLAGS += -D_LARGEFILE64_SOURCE

# include ZLIB support
NEED_ZLIB=1

#OS_DIR = linux
OSFLAGS += -DOS_LINUX -fPIC -Wno-unused-function
LIBS = -lutil -lpthread -lrt -ldl
SPECIFIC_OS_PRG = $(BIN_DIR)/mlxspeaker $(BIN_DIR)/dio

# add OpenSSL
ifndef NO_SSL
SSL_LIBS += -lssl -lcrypto
endif

endif

#-----------------------
# This is for Solaris
#
ifeq ($(OSTYPE),solaris)
CC = gcc
#OS_DIR = solaris
OSFLAGS = -DOS_SOLARIS
LIBS = -lsocket -lnsl
SPECIFIC_OS_PRG = 
endif

#####################################################################
# end of conditional code
#####################################################################

#
# Midas directories
#
INC_DIR  = include
SRC_DIR  = src
DRV_DIR  = drivers
EXAM_DIR = examples
PROGS_DIR = progs

#
# Midas operating system dependent directories
#
ifdef OS_DIR
LIB_DIR   = $(OS_DIR)/lib
BIN_DIR   = $(OS_DIR)/bin
else
LIB_DIR   = lib
BIN_DIR   = bin
endif

#
# targets
#

GIT_REVISION := $(INC_DIR)/git-revision.h

EXAMPLES = $(BIN_DIR)/consume $(BIN_DIR)/produce \
	$(BIN_DIR)/rpc_test $(BIN_DIR)/msgdump $(BIN_DIR)/minife \
	$(BIN_DIR)/minirc $(BIN_DIR)/odb_test

PROGS = $(BIN_DIR)/mserver \
	$(BIN_DIR)/odbedit \
	$(BIN_DIR)/odbinit \
	$(BIN_DIR)/mhttpd  \
	$(BIN_DIR)/mlogger \
	$(BIN_DIR)/msequencer \
	$(BIN_DIR)/mhist \
	$(BIN_DIR)/mstat \
	$(BIN_DIR)/mdump \
	$(BIN_DIR)/lazylogger \
	$(BIN_DIR)/mtransition \
	$(BIN_DIR)/mhdump \
	$(BIN_DIR)/mchart \
	$(BIN_DIR)/stripchart.tcl \
	$(BIN_DIR)/odbhist \
	$(BIN_DIR)/melog   \
	$(BIN_DIR)/mh2sql  \
	$(BIN_DIR)/mfe_link_test  \
	$(BIN_DIR)/mfe_link_test_cxx  \
	$(BIN_DIR)/fetest  \
	$(BIN_DIR)/sysmon \
	$(BIN_DIR)/feudp   \
	$(BIN_DIR)/fetest_tmfe    \
	$(BIN_DIR)/fetest_tmfe_thread \
	$(BIN_DIR)/mana_link_test \
	$(BIN_DIR)/mjson_test \
	$(BIN_DIR)/mcnaf    \
	$(BIN_DIR)/crc32c   \
	$(BIN_DIR)/get_record_test \
	$(BIN_DIR)/odb_lock_test \
	$(SPECIFIC_OS_PRG)

ifdef HAVE_ROOT
PROGS += $(BIN_DIR)/rmlogger
PROGS += $(BIN_DIR)/rmana_link_test
endif

ANALYZER = $(LIB_DIR)/mana.o

ifdef CERNLIB
ANALYZER += $(LIB_DIR)/hmana.o
endif

ifdef HAVE_ROOT
ANALYZER += $(LIB_DIR)/rmana.o
endif

EXAMPLES += examples/experiment/frontend
ifdef HAVE_ROOT
EXAMPLES += examples/experiment/analyzer
endif

ifdef HAVE_NVIDIA
PROGS += $(BIN_DIR)/sysmon-nvidia
endif
OBJS = \
	$(LIB_DIR)/midas.o \
	$(LIB_DIR)/midas_cxx.o \
	$(LIB_DIR)/system.o \
	$(LIB_DIR)/mrpc.o \
	$(LIB_DIR)/odb.o \
	$(LIB_DIR)/device_driver.o \
	$(LIB_DIR)/ftplib.o \
	$(LIB_DIR)/crc32c.o \
	$(LIB_DIR)/sha256.o \
	$(LIB_DIR)/sha512.o \
	$(LIB_DIR)/mxml.o \
	$(LIB_DIR)/mjson.o \
	$(LIB_DIR)/json_paste.o \
	$(LIB_DIR)/tmfe.o \
	$(LIB_DIR)/mvodb.o \
	$(LIB_DIR)/nullodb.o \
	$(LIB_DIR)/midasodb.o \
	$(LIB_DIR)/mxmlodb.o \
	$(LIB_DIR)/mjsonodb.o \
	$(LIB_DIR)/history_common.o \
	$(LIB_DIR)/history_odbc.o \
	$(LIB_DIR)/history_schema.o \
	$(LIB_DIR)/history.o \
	$(LIB_DIR)/lz4.o $(LIB_DIR)/lz4frame.o $(LIB_DIR)/lz4hc.o $(LIB_DIR)/xxhash.o \
	$(LIB_DIR)/alarm.o \
	$(LIB_DIR)/elog.o

ifdef NEED_STRLCPY
OBJS += $(LIB_DIR)/strlcpy.o
endif

LIBNAME = $(LIB_DIR)/libmidas.a
LIB     = $(LIBNAME)
ifndef NO_SHLIB
SHLIB   = $(LIB_DIR)/libmidas-shared.so
endif

VPATH = $(LIB_DIR):$(INC_DIR)

ALL:=
ALL+= $(LIB_DIR) $(BIN_DIR)
ALL+= $(LIBNAME) $(SHLIB)
ALL+= $(ANALYZER)
ALL+= $(LIB_DIR)/mfe.o
ALL+= $(PROGS)
ALL+= $(EXAMPLES)

all: check-mxml $(GIT_REVISION) $(ALL)

CMAKEFLAGS:=

ifdef NO_ROOT
CMAKEFLAGS+= -DNO_ROOT=1
endif

ifdef NO_ODBC
CMAKEFLAGS+= -DNO_ODBC=1
endif

ifdef NO_SQLITE
CMAKEFLAGS+= -DNO_SQLITE=1
endif

ifdef NO_MYSQL
CMAKEFLAGS+= -DNO_MYSQL=1
endif

ifdef NO_SSL
CMAKEFLAGS+= -DNO_SSL=1
endif

CMAKEFLAGS+= -DCMAKE_TARGET_MESSAGES=OFF
#CMAKEFLAGS+= -DCMAKE_RULE_MESSAGES=OFF
#CMAKEFLAGS+= -DCMAKE_VERBOSE_MAKEFILE=ON

CMAKEGREPFLAGS+= -e ^make
CMAKEGREPFLAGS+= -e ^Dependee
CMAKEGREPFLAGS+= -e cmake_depends
CMAKEGREPFLAGS+= -e ^Scanning
CMAKEGREPFLAGS+= -e cmake_link_script
CMAKEGREPFLAGS+= -e cmake_progress_start
CMAKEGREPFLAGS+= -e cmake_clean_target
CMAKEGREPFLAGS+= -e build.make
#CMAKEGREPFLAGS+= -e c++
#CMAKEGREPFLAGS+= -e Building

cmake:
	-mkdir build
	cd build; cmake .. $(CMAKEFLAGS); $(MAKE) --no-print-directory VERBOSE=1 all install | 2>&1 grep -v $(CMAKEGREPFLAGS)

cmake3:
	-mkdir build
	cd build; cmake3 .. $(CMAKEFLAGS); $(MAKE) --no-print-directory VERBOSE=1 all install | 2>&1 grep -v $(CMAKEGREPFLAGS)

cclean:
	-rm -f lib/*
	-rm -f bin/*
	-rm -rf build

dox:
	doxygen

linux32:
	$(MAKE) NO_ROOT=1 NO_MYSQL=1 NO_ODBC=1 NO_SQLITE=1 NO_SSL=1 OS_DIR=linux-m32 USERFLAGS=-m32

linux64:
	$(MAKE) NO_ROOT=1 OS_DIR=linux-m64 USERFLAGS=-m64

clean32:
	$(MAKE) NO_ROOT=1 OS_DIR=linux-m32 USERFLAGS=-m32 clean

clean64:
	$(MAKE) NO_ROOT=1 OS_DIR=linux-m64 USERFLAGS=-m64 clean

crosscompile:
	echo OSTYPE=$(OSTYPE)
	$(MAKE) NO_ROOT=1 NO_MYSQL=1 NO_ODBC=1 NO_SQLITE=1 OS_DIR=$(OSTYPE)-crosscompile OSTYPE=crosscompile

linuxarm:
	echo OSTYPE=$(OSTYPE)
	$(MAKE) NO_ROOT=1 NO_MYSQL=1 NO_ODBC=1 NO_SQLITE=1 OS_DIR=$(OSTYPE)-arm OSTYPE=crosslinuxarm

cleanarm:
	$(MAKE) NO_ROOT=1 OS_DIR=linux-arm clean

linuxemcraft:
	echo OSTYPE=$(OSTYPE)
	$(MAKE) NO_ROOT=1 NO_MYSQL=1 NO_ODBC=1 NO_SQLITE=1 NO_SSL=1 NO_EXECINFO=1 NO_SHLIB=1 NO_FORK=1 OS_DIR=$(OSTYPE)-emcraft OSTYPE=crosslinuxemcraft

cleanemcraft:
	$(MAKE) NO_ROOT=1 OS_DIR=linux-emcraft clean

cleandox:
	-rm -rf html

examples: $(EXAMPLES)

#####################################################################

#
# create library and binary directories
#

$(LIB_DIR):
	@if [ ! -d  $(LIB_DIR) ] ; then \
           echo "Making directory $(LIB_DIR)" ; \
           mkdir -p $(LIB_DIR); \
        fi;

$(BIN_DIR):
	@if [ ! -d  $(BIN_DIR) ] ; then \
           echo "Making directory $(BIN_DIR)" ; \
           mkdir -p $(BIN_DIR); \
        fi;

#
# put current GIT revision into header file to be included by programs
#
$(GIT_REVISION): $(SRC_DIR)/midas.cxx $(SRC_DIR)/midas_cxx.cxx $(SRC_DIR)/odb.cxx $(SRC_DIR)/system.cxx $(PROGS_DIR)/mhttpd.cxx $(INC_DIR)/midas.h
	echo \#define GIT_REVISION \"`git log -1 --format="%ad"` - `git describe --abbrev=8 --tags --dirty` on branch `git rev-parse --abbrev-ref HEAD`\" > $(GIT_REVISION)-new
	rsync --checksum $(GIT_REVISION)-new $(GIT_REVISION) # only update git-revision.h and update it's timestamp if it's contents have changed
	-/bin/rm -f $(GIT_REVISION)-new

#
# main binaries
#

ifdef HAVE_MARIADB
CFLAGS      += -DHAVE_MYSQL $(shell mariadb_config --include)
MYSQL_LIBS  += $(shell mariadb_config --libs)
NEED_ZLIB = 1
else ifdef HAVE_MYSQL
CFLAGS      += -DHAVE_MYSQL $(shell mysql_config --include)
MYSQL_LIBS  += $(shell mysql_config --libs)
NEED_ZLIB = 1
endif

ifdef HAVE_ODBC
CFLAGS      += -DHAVE_ODBC
ODBC_LIBS   += -lodbc
ifeq ($(OSTYPE),darwin)
ODBC_LIBS   += /System/Library/Frameworks/CoreFoundation.framework/CoreFoundation
endif
endif

ifdef HAVE_SQLITE
CFLAGS      += -DHAVE_SQLITE
SQLITE_LIBS += -lsqlite3
endif

ifdef HAVE_ROOT
ROOTLIBS    := $(shell root-config --libs)
ROOTGLIBS   := $(shell root-config --glibs)
ROOTCFLAGS  := $(shell root-config --cflags)
ROOTCFLAGS  += -DHAVE_ROOT
endif

ifdef HAVE_NVIDIA
NVIDIA_FLAGS      += -DHAVE_NVIDIA   -L/usr/local/cuda/lib64 -lnvidia-ml -I/usr/local/cuda/include
endif

ifdef NEED_ZLIB
CFLAGS     += -DHAVE_ZLIB
LIBS       += -lz
endif

ifdef HAVE_MSCB
CFLAGS     += -DHAVE_MSCB
endif

$(BIN_DIR)/mlogger: $(BIN_DIR)/%: $(PROGS_DIR)/%.cxx
	$(CXX) $(CFLAGS) $(OSFLAGS) -o $@ $< $(LIB) $(ODBC_LIBS) $(SQLITE_LIBS) $(MYSQL_LIBS) $(LIBS)

ifdef HAVE_ROOT
$(BIN_DIR)/rmlogger: $(BIN_DIR)/%: $(PROGS_DIR)/mlogger.cxx
	$(CXX) $(CFLAGS) $(OSFLAGS) $(ROOTCFLAGS) -o $@ $< $(LIB) $(ROOTLIBS) $(ODBC_LIBS) $(SQLITE_LIBS) $(MYSQL_LIBS) $(LIBS)
endif

$(BIN_DIR)/%:$(SRC_DIR)/%.cxx
	$(CXX) $(CFLAGS) $(OSFLAGS) -o $@ $< $(LIB) $(LIBS)

$(BIN_DIR)/odbedit: $(PROGS_DIR)/odbedit.cxx $(PROGS_DIR)/cmdedit.cxx
	$(CXX) $(CFLAGS) $(OSFLAGS) -o $@ $(PROGS_DIR)/odbedit.cxx $(PROGS_DIR)/cmdedit.cxx $(LIB) $(LIBS)

$(BIN_DIR)/odbinit: $(BIN_DIR)/%: $(PROGS_DIR)/%.cxx
	$(CXX) $(CFLAGS) $(OSFLAGS) -o $@ $< $(LIB) $(LIBS)

MHTTPD_OBJS=
MHTTPD_OBJS += $(LIB_DIR)/mhttpd.o
MHTTPD_OBJS += $(LIB_DIR)/mgd.o
MHTTPD_OBJS += $(LIB_DIR)/mjsonrpc.o $(LIB_DIR)/mjsonrpc_user.o
ifdef HAVE_MSCB
MHTTPD_OBJS += $(LIB_DIR)/mscb.o
endif

MHTTPD_OBJS += $(LIB_DIR)/mongoose6.o
CFLAGS      += -DHAVE_MONGOOSE6
CFLAGS      += -DMG_ENABLE_THREADS
CFLAGS      += -DMG_DISABLE_CGI
ifndef NO_SSL
CFLAGS      += -DMG_ENABLE_SSL
endif

#MHTTPD_OBJS += $(LIB_DIR)/mongoose614.o
#CFLAGS      += -DHAVE_MONGOOSE614
##CFLAGS      += -DMG_ENABLE_THREADS
#CFLAGS      += -DMG_DISABLE_CGI
#ifndef NO_SSL
#CFLAGS      += -DMG_ENABLE_SSL
#endif

$(LIB_DIR)/mongoose6.o: $(PROGS_DIR)/mongoose6.cxx $(INC_DIR)/mongoose6.h 
	$(CXX) -c $(CFLAGS) $(OSFLAGS) -o $@ $<
$(LIB_DIR)/mgd.o: $(PROGS_DIR)/mgd.cxx $(INC_DIR)/mgd.h 
	$(CXX) -c $(CFLAGS) $(OSFLAGS) -o $@ $<
$(BIN_DIR)/mhttpd: $(MHTTPD_OBJS)
	$(CXX) $(CFLAGS) $(OSFLAGS) -o $@ $(MHTTPD_OBJS) $(LIB) $(MYSQL_LIBS) $(ODBC_LIBS) $(SQLITE_LIBS) $(SSL_LIBS) $(LIBS) -lm

$(BIN_DIR)/msequencer: $(BIN_DIR)/%: $(PROGS_DIR)/msequencer.cxx
	$(CXX) $(CFLAGS) $(OSFLAGS) -o $@ $< $(LIB) $(LIBS)

$(BIN_DIR)/mh2sql: $(BIN_DIR)/%: $(PROGS_DIR)/mh2sql.cxx
	$(CXX) $(CFLAGS) $(OSFLAGS) -o $@ $< $(LIB) $(ODBC_LIBS) $(SQLITE_LIBS) $(MYSQL_LIBS) $(LIBS)

$(BIN_DIR)/mhist: $(BIN_DIR)/%: $(PROGS_DIR)/%.cxx
	$(CXX) $(CFLAGS) $(OSFLAGS) -o $@ $< $(LIB) $(ODBC_LIBS) $(SQLITE_LIBS) $(MYSQL_LIBS) $(LIBS)

$(PROGS): $(LIBNAME)

#
# examples
#

$(BIN_DIR)/%:$(EXAM_DIR)/lowlevel/%.cxx
	$(CXX) $(CFLAGS) $(OSFLAGS) -o $@ $< $(LIB) $(LIBS)

$(BIN_DIR)/%:$(EXAM_DIR)/basic/%.cxx
	$(CXX) $(CFLAGS) $(OSFLAGS) -o $@ $< $(LIB) $(LIBS)

$(EXAMPLES): $(LIBNAME)

examples/experiment/frontend:
	$(MAKE) -C examples/experiment MIDASSYS=$(PWD) frontend

examples/experiment/analyzer:
	$(MAKE) -C examples/experiment MIDASSYS=$(PWD) analyzer

#
# midas library
#

$(LIBNAME): $(OBJS)
	rm -f $@
	ar -crv $@ $^
ifdef NEED_RANLIB
	ranlib $@
endif

ifeq ($(OSTYPE),crosscompile)
%.so: $(OBJS)
	rm -f $@
	$(CXX) -shared -o $@ $^ $(LIBS) -lc
endif

ifeq ($(OSTYPE),crosslinuxarm)
%.so: $(OBJS)
	rm -f $@
	$(CXX) -shared -o $@ $^ $(LIBS) -lc
endif

ifeq ($(OSTYPE),linux)
%.so: $(OBJS)
	rm -f $@
	$(CXX) -shared -o $@ $^ $(LIBS) -lc
endif

ifeq ($(OSTYPE),darwin)
%.dylib: $(OBJS)
	rm -f $@
	g++ -dynamiclib -dylib -o $@ $^ $(LIBS) -lc

%.so: $(OBJS)
	rm -f $@
	g++ -bundle -flat_namespace -undefined suppress -o $@ $(OBJS) $(LIBS) -lc
endif

#
# frontend and backend framework
#

$(LIB_DIR)/history_sql.o $(LIB_DIR)/history_schema.o $(LIB_DIR)/history_midas.o $(LIB_DIR)/mhttpd.o $(LIB_DIR)/mlogger.o: history.h

$(LIB_DIR)/mgd.o: midas.h

$(LIB_DIR)/mfe.o: msystem.h midas.h midasinc.h mrpc.h

$(LIB_DIR)/mana.o: $(SRC_DIR)/mana.cxx msystem.h midas.h midasinc.h mrpc.h
	$(CXX) -c $(CFLAGS) $(OSFLAGS) -o $@ $<
$(LIB_DIR)/hmana.o: $(SRC_DIR)/mana.cxx msystem.h midas.h midasinc.h mrpc.h
	$(CXX) -Dextname -DHAVE_HBOOK -c $(CFLAGS) $(OSFLAGS) -o $@ $<
ifdef HAVE_ROOT
$(LIB_DIR)/rmana.o: $(SRC_DIR)/mana.cxx msystem.h midas.h midasinc.h mrpc.h
	$(CXX) -c $(CFLAGS) $(OSFLAGS) $(ROOTCFLAGS) -o $@ $<
endif

#
# library objects
#

$(LIB_DIR)/mhttpd.o: $(LIB_DIR)/%.o: $(PROGS_DIR)/%.cxx
	$(CXX) -c $(CFLAGS) $(OSFLAGS) -o $@ $<

$(LIB_DIR)/%.o:$(SRC_DIR)/%.cxx
	$(CXX) -c $(CFLAGS) $(OSFLAGS) -o $@ $<

$(LIB_DIR)/mxml.o:$(MXML_DIR)/mxml.cxx
	$(CXX) -c $(CFLAGS) $(OSFLAGS) -o $@ $(MXML_DIR)/mxml.cxx

$(LIB_DIR)/strlcpy.o:$(MXML_DIR)/strlcpy.cxx
	$(CXX) -c $(CFLAGS) $(OSFLAGS) -o $@ $(MXML_DIR)/strlcpy.cxx

ifdef HAVE_MSCB
$(LIB_DIR)/mscb.o:$(MSCB_DIR)/src/mscb.cxx $(MSCB_DIR)/include/mscb.h
	$(CXX) -c $(CFLAGS) $(OSFLAGS) -o $@ $(MSCB_DIR)/src/mscb.cxx
endif

$(LIB_DIR)/mhttpd.o: msystem.h midas.h midasinc.h mrpc.h mjsonrpc.h
$(LIB_DIR)/midas.o: msystem.h midas.h midasinc.h mrpc.h git-revision.h
$(LIB_DIR)/midas_cxx.o: msystem.h midas.h midasinc.h mrpc.h
$(LIB_DIR)/system.o: msystem.h midas.h midasinc.h mrpc.h
$(LIB_DIR)/mrpc.o: msystem.h midas.h mrpc.h
$(LIB_DIR)/odb.o: msystem.h midas.h midasinc.h mrpc.h git-revision.h
$(LIB_DIR)/mdsupport.o: msystem.h midas.h midasinc.h
$(LIB_DIR)/ftplib.o: msystem.h midas.h midasinc.h
$(LIB_DIR)/mxml.o: msystem.h midas.h midasinc.h $(MXML_DIR)/mxml.h
$(LIB_DIR)/alarm.o: msystem.h midas.h midasinc.h
$(LIB_DIR)/mjsonrpc.o: midas.h mjsonrpc.h
$(LIB_DIR)/mfe.o: midas.h msystem.h mfe.h

#
# utilities
#
$(BIN_DIR)/%: $(PROGS_DIR)/%.cxx
	$(CXX) $(CFLAGS) $(OSFLAGS) -o $@ $< $(LIB) $(LIBS)

$(BIN_DIR)/mcnaf: $(PROGS_DIR)/mcnaf.cxx $(DRV_DIR)/camac/camacrpc.cxx
	$(CXX) $(CFLAGS) $(OSFLAGS) -o $@ $(PROGS_DIR)/mcnaf.cxx $(DRV_DIR)/camac/camacrpc.cxx $(LIB) $(LIBS)

$(BIN_DIR)/mdump: $(PROGS_DIR)/mdump.cxx $(SRC_DIR)/mdsupport.cxx
	$(CXX) $(CFLAGS) $(OSFLAGS) -o $@ $(PROGS_DIR)/mdump.cxx $(SRC_DIR)/mdsupport.cxx $(LIB) $(LIBS)

$(BIN_DIR)/sysmon: $(PROGS_DIR)/sysmon.cxx $(LIB_DIR)/mfe.o
	$(CXX) $(CFLAGS) $(OSFLAGS) -o $@ $^ $(LIB) $(LIBS)

$(BIN_DIR)/sysmon-nvidia: $(PROGS_DIR)/sysmon.cxx $(LIB_DIR)/mfe.o
	$(CXX) $(CFLAGS) $(NVIDIA_FLAGS) $(OSFLAGS) -o $@ $^ $(LIB) $(LIBS)

$(BIN_DIR)/fetest: $(PROGS_DIR)/fetest.cxx $(LIB_DIR)/mfe.o
	$(CXX) $(CFLAGS) $(OSFLAGS) -o $@ $^ $(LIB) $(LIBS)

$(BIN_DIR)/feudp: $(PROGS_DIR)/feudp.cxx $(LIB_DIR)/mfe.o
	$(CXX) $(CFLAGS) $(OSFLAGS) -o $@ $^ $(LIB) $(LIBS)

$(BIN_DIR)/crc32c: $(SRC_DIR)/crc32c.cxx
	$(CXX) $(CFLAGS) $(OSFLAGS) -DTEST -o $@ $^ $(LIB) $(LIBS)

$(BIN_DIR)/mfe_link_test: $(PROGS_DIR)/mfe_link_test.cxx $(LIB_DIR)/mfe.o
	$(CXX) $(CFLAGS) $(OSFLAGS) -o $@ $^ $(LIB) $(LIBS)

$(BIN_DIR)/mfe_link_test_cxx: $(PROGS_DIR)/mfe_link_test_cxx.cxx $(LIB_DIR)/mfe.o
	$(CXX) $(CFLAGS) $(OSFLAGS) -o $@ $^ $(LIB) $(LIBS)

$(BIN_DIR)/mana_link_test: $(SRC_DIR)/mana.cxx
	$(CXX) $(CFLAGS) $(OSFLAGS) -DLINK_TEST -o $@ $(SRC_DIR)/mana.cxx $(LIB) $(LIBS)

ifdef HAVE_ROOT
$(BIN_DIR)/rmana_link_test: $(SRC_DIR)/mana.cxx
	$(CXX) $(CFLAGS) $(OSFLAGS) $(ROOTCFLAGS) -DLINK_TEST -o $@ $(SRC_DIR)/mana.cxx $(ROOTLIBS) $(LIB) $(LIBS)
endif

$(BIN_DIR)/mhdump: $(PROGS_DIR)/mhdump.cxx
	$(CXX) $(CFLAGS) $(OSFLAGS) -o $@ $<

$(BIN_DIR)/mtransition: $(PROGS_DIR)/mtransition.cxx
	$(CXX) $(CFLAGS) $(OSFLAGS) -o $@ $< $(LIB) $(LIBS)

$(BIN_DIR)/lazylogger: $(PROGS_DIR)/lazylogger.cxx $(SRC_DIR)/mdsupport.cxx
	$(CXX) $(CFLAGS) $(OSFLAGS) -o $@ $<  $(SRC_DIR)/mdsupport.cxx $(LIB) $(LIBS)

$(BIN_DIR)/dio: $(PROGS_DIR)/dio.cxx
	$(CXX) $(CFLAGS) $(OSFLAGS) -o $@ $(PROGS_DIR)/dio.cxx

$(BIN_DIR)/stripchart.tcl: $(PROGS_DIR)/stripchart.tcl
	cp -f $(PROGS_DIR)/stripchart.tcl $(BIN_DIR)/. 


#####################################################################

static:
	rm -f $(PROGS)
	make USERFLAGS=-static

#####################################################################

install:
# system programs and utilities
	@echo "... "
	@echo "... Installing programs and utilities to $(SYSBIN_DIR)"
	@echo "... "

	@for file in `find $(BIN_DIR) -type f | grep -v .svn` ; \
	  do \
	  install -v -D -m 755 $$file $(SYSBIN_DIR)/`basename $$file` ; \
	  done

	if [ -f $(SYSBIN_DIR)/dio ]; then chmod +s $(SYSBIN_DIR)/dio ; fi
	if [ -f $(SYSBIN_DIR)/mhttpd ]; then chmod +s $(SYSBIN_DIR)/mhttpd; fi
	ln -fs $(SYSBIN_DIR)/stripchart.tcl $(SYSBIN_DIR)/stripchart

# include
	@echo "... "
	@echo "... Installing include files to $(SYSINC_DIR)"
	@echo "... "

	@for file in `find $(INC_DIR) -type f | grep -v .svn` ; \
	  do \
	  install -v -D -m 644 $$file $(SYSINC_DIR)/`basename $$file` ; \
	  done

# library + objects
	@echo "... "
	@echo "... Installing library and objects to $(SYSLIB_DIR)"
	@echo "... "

	@for i in libmidas.a mana.o mfe.o ; \
	  do \
	  install -v -D -m 644 $(LIB_DIR)/$$i $(SYSLIB_DIR)/$$i ; \
	  done

ifdef CERNLIB
	install -v -m 644 $(LIB_DIR)/hmana.o $(SYSLIB_DIR)/hmana.o
else
	rm -fv $(SYSLIB_DIR)/hmana.o
	chmod +s $(SYSBIN_DIR)/mhttpd
endif
ifdef HAVE_ROOT
	install -v -m 644 $(LIB_DIR)/rmana.o $(SYSLIB_DIR)/rmana.o
else
	rm -fv $(SYSLIB_DIR)/rmana.o
endif
	-rm -fv $(SYSLIB_DIR)/libmidas.so
	-install -v -m 644 $(LIB_DIR)/libmidas-shared.so $(SYSLIB_DIR)

# drivers
	@echo "... "
	@echo "... Installing drivers to $(PREFIX)/drivers"
	@echo "... "

	@for file in `find $(DRV_DIR) -type f | grep -v .svn` ; \
	  do \
	  install -v -D -m 644 $$file $(PREFIX)/$$file ;\
	  done

#--------------------------------------------------------------
# minimal_install
minimal_install:
# system programs
	@echo "... "
	@echo "... Minimal Install for programs to $(SYSBIN_DIR)"
	@echo "... "

	@for i in mserver mhttpd; \
	  do \
	  install -v -m 755 $(BIN_DIR)/$$i $(SYSBIN_DIR) ; \
	  done

ifeq ($(OSTYPE),linux)
	install -v -m 755 $(BIN_DIR)/dio $(SYSBIN_DIR)
endif
	if [ -f $(SYSBIN_DIR)/dio ]; then chmod +s $(SYSBIN_DIR)/dio; fi
	if [ -f $(SYSBIN_DIR)/mhttpd ]; then chmod +s $(SYSBIN_DIR)/mhttpd; fi

# utilities
	@echo "... "
	@echo "... No utilities install to $(SYSBIN_DIR)"
	@echo "... "

# include
	@echo "... "
	@echo "... No include install to $(SYSINC_DIR)"
	@echo "... "

# library + objects
	@echo "... "
	@echo "... No library Install to $(SYSLIB_DIR)"
	@echo "... "

indent:
	find . -name "*.[hc]" -exec indent -kr -nut -i3 -l90 {} \;

clean:
	-rm -vf $(LIB_DIR)/*.o $(LIB_DIR)/*.a $(LIB_DIR)/*.so $(LIB_DIR)/*.dylib
	-rm -vf $(GIT_REVISION)
	-rm -rvf $(BIN_DIR)/*.dSYM
	-rm -vf $(BIN_DIR)/*
	-$(MAKE) -C examples/experiment clean

mrproper : clean
	rm -rf vxworks/68kobj vxworks/ppcobj

check-mxml :
ifeq ($(NEED_STRLCPY), 1)
	@if [ ! -e $(MXML_DIR)/strlcpy.h ]; then \
	  echo "please download mxml."; \
	  echo "http://midas.psi.ch/htmldoc/quickstart.html"; \
	  exit 1; \
	fi
endif
	@if [ ! -e $(MXML_DIR)/mxml.h ]; then \
	  echo "please download mxml."; \
	  echo "http://midas.psi.ch/htmldoc/quickstart.html"; \
	  exit 1; \
	fi

