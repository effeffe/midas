#####################################################################
#
#  Name:         Makefile
#  Created by:   Stefan Ritt
#
#  Contents:     Makefile for MIDAS binaries and examples under unix
#
#####################################################################

all: help

help:
	@echo "Usage:"
	@echo ""
	@echo "   make cmake     --- full build of midas"
	@echo "   make cclean    --- remove everything build by make cmake"
	@echo ""
	@echo "   options that can be added to \"make cmake\":"
	@echo "      NO_LOCAL_ROUTINES=1 NO_CURL=1"
	@echo "      NO_ROOT=1 NO_ODBC=1 NO_SQLITE=1 NO_MYSQL=1 NO_SSL=1 NO_MBEDTLS=1"
	@echo "      NO_EXPORT_COMPILE_COMMANDS=1"
	@echo ""
	@echo "   make dox       --- run doxygen, results are in ./html/index.html"
	@echo "   make cleandox  --- remove doxygen output"
	@echo ""
	@echo "   make htmllint  --- run html check on resources/*.html"
	@echo ""
	@echo "   make gofmt     --- run gofmt on golang programs"
	@echo ""
	@echo "   make test      --- run midas self test"
	@echo ""
	@echo "   make mbedtls   --- enable mhttpd support for https via the mbedtls https library"
	@echo "   make update_mbedtls --- update mbedtls to latest version"
	@echo "   make clean_mbedtls  --- remove mbedtls from this midas build"
	@echo ""
	@echo "   make mtcpproxy --- build the https proxy to forward root-only port 443 to mhttpd https port 8443"
	@echo ""
	@echo "   make mini      --- minimal build, results are in linux/{bin,lib}"
	@echo "   make cleanmini --- remove everything build by make mini"
	@echo ""
	@echo "   make remoteonly      --- minimal build, remote connetion only, results are in linux-remoteonly/{bin,lib}"
	@echo "   make cleanremoteonly --- remove everything build by make remoteonly"
	@echo ""
	@echo "   make linux32   --- minimal x86 -m32 build, results are in linux-m32/{bin,lib}"
	@echo "   make clean32   --- remove everything built by make linux32"
	@echo ""
	@echo "   make linux64   --- minimal x86 -m64 build, results are in linux-m64/{bin,lib}"
	@echo "   make clean64   --- remove everything built by make linux64"
	@echo ""
	@echo "   make linuxarm  --- minimal ARM cross-build, results are in linux-arm/{bin,lib}"
	@echo "   make cleanarm  --- remove everything built by make linuxarm"
	@echo ""
	@echo "   make clean     --- run all 'clean' commands"
	@echo ""

#crosscompile:
#	echo OSTYPE=$(OSTYPE)
#	$(MAKE) NO_ROOT=1 NO_MYSQL=1 NO_ODBC=1 NO_SQLITE=1 OS_DIR=$(OSTYPE)-crosscompile OSTYPE=crosscompile
#
#linuxemcraft:
#	echo OSTYPE=$(OSTYPE)
#	$(MAKE) NO_ROOT=1 NO_MYSQL=1 NO_ODBC=1 NO_SQLITE=1 NO_SSL=1 NO_EXECINFO=1 NO_SHLIB=1 NO_FORK=1 OS_DIR=$(OSTYPE)-emcraft OSTYPE=crosslinuxemcraft
#
#cleanemcraft:
#	$(MAKE) NO_ROOT=1 OS_DIR=linux-emcraft clean

#####################################################################

# cancel useless built-in rules

.SUFFIXES:
%: RCS/%,v
%: %,v
%: RCS/%
%: s.%
%: SCCS/s.%

#####################################################################
#
#                           cmake build
#
#####################################################################

CMAKE:=cmake
#ifneq (,$(wildcard /opt/local/bin/cmake))
#CMAKE:=/opt/local/bin/cmake
#endif
ifneq (,$(wildcard /usr/bin/cmake3))
CMAKE:=/usr/bin/cmake3
endif

CMAKEFLAGS:=

ifdef NO_LOCAL_ROUTINES
CMAKEFLAGS+= -DNO_LOCAL_ROUTINES=1
endif

ifdef NO_CURL
CMAKEFLAGS+= -DNO_CURL=1
endif

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

ifdef NO_MBEDTLS
CMAKEFLAGS+= -DNO_MBEDTLS=1
endif

ifndef NO_EXPORT_COMPILE_COMMANDS
CMAKEFLAGS+= -DCMAKE_EXPORT_COMPILE_COMMANDS=1
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
	mkdir -p build
	cd build; $(CMAKE) .. $(CMAKEFLAGS); $(MAKE) --no-print-directory VERBOSE=1 all install | 2>&1 grep -v $(CMAKEGREPFLAGS)

cmake3:
	mkdir -p build
	cd build; cmake3 .. $(CMAKEFLAGS); $(MAKE) --no-print-directory VERBOSE=1 all install | 2>&1 grep -v $(CMAKEGREPFLAGS)

clean:: cclean

cclean:
	-rm -f lib/*
	-rm -f bin/*
	-rm -rf build
	-rm -rf examples/experiment/build

#####################################################################

# get OS type from shell
OSTYPE = $(shell uname)

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
# Option to use our own implementation of strlcat, strlcpy
#
NEED_STRLCPY=1

#
# Optional zlib support for data compression in the mlogger and in the analyzer
#
NEED_ZLIB=

#####################################################################
# Nothing needs to be modified after this line 
#####################################################################

#-----------------------
# Common flags
#
CC  = false ### C compiler is not used
CXX = g++ -std=c++11 $(USERFLAGS)
CFLAGS = -g -O2 -Wall -Wformat=2 -Wno-format-nonliteral -Wno-strict-aliasing -Wuninitialized -Iinclude -Imxml -Imjson -Imvodb

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
NO_EXECINFO=1
NO_FORK=1
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
OS_DIR = osf1
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
OS_DIR = ultrix
OSFLAGS = -DOS_ULTRIX -DNO_PTY
LIBS =
SPECIFIC_OS_PRG = 
endif

#-----------------------
# FreeBSD
#
ifeq ($(OSTYPE), FreeBSD)
OS_DIR = freebsd
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
OS_DIR = darwin
OSFLAGS = -DOS_LINUX -DOS_DARWIN -fPIC -Wno-unused-function
LIBS = -lpthread
SPECIFIC_OS_PRG = $(BIN_DIR)/mlxspeaker
NEED_ZLIB=1
NEED_STRLCPY=
NEED_RANLIB=1
endif

#-----------------------
# This is for Cygwin
#
ifeq ($(OSTYPE),CYGWIN_NT-5.1)
OSTYPE = cygwin
endif

ifeq ($(OSTYPE),cygwin)

OS_DIR = cygwin
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

OS_DIR = linux
OSFLAGS += -DOS_LINUX -fPIC -Wno-unused-function -std=c++11
LIBS = -lutil -lpthread -lrt -ldl
SPECIFIC_OS_PRG = $(BIN_DIR)/mlxspeaker $(BIN_DIR)/dio

endif

#-----------------------
# This is for Solaris
#
ifeq ($(OSTYPE),solaris)
CC = gcc
OS_DIR = solaris
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
PROGS_DIR = progs

#
# Midas operating system dependent directories
#
LIB_DIR   = $(OS_DIR)/lib
BIN_DIR   = $(OS_DIR)/bin

#
# targets
#

GIT_REVISION := include/git-revision.h

OBJS := 
OBJS +=	$(LIB_DIR)/midas.o
OBJS +=	$(LIB_DIR)/system.o \
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
	$(LIB_DIR)/alarm.o
OBJS +=	$(LIB_DIR)/elog.o
OBJS +=	$(LIB_DIR)/odbxx.o
ifdef NEED_STRLCPY
OBJS += $(LIB_DIR)/strlcpy.o
endif

LIB := $(LIB_DIR)/libmidas.a

$(LIB): $(OBJS)
	rm -f $@
	ar -crv $@ $(OBJS)
ifdef NEED_RANLIB
	ranlib $@
endif

MINI_PROGS :=
MINI_PROGS += $(BIN_DIR)/odbinit
MINI_PROGS += $(BIN_DIR)/odbedit
MINI_PROGS += $(BIN_DIR)/fetest
MINI_PROGS += $(BIN_DIR)/tmfe_example
MINI_PROGS += $(BIN_DIR)/tmfe_example_multithread

SUBMODULES :=
SUBMODULES += mxml/mxml.cxx
SUBMODULES += mjson/mjson.cxx
SUBMODULES += mvodb/mvodb.cxx

$(LIB_DIR)/mfe.o $(LIB) $(OBJS): $(OS_DIR) $(GIT_REVISION) $(SUBMODULES)

$(OS_DIR):
	mkdir -p $(BIN_DIR)
	mkdir -p $(LIB_DIR)

$(SUBMODULES):
	git submodule sync
	git submodule update --init

#
# create git revision file
#

$(GIT_REVISION): src/midas.cxx src/odb.cxx src/system.cxx progs/mhttpd.cxx include/midas.h
	echo \#define GIT_REVISION \"`git log -1 --format="%ad"` - `git describe --abbrev=8 --tags --dirty` on branch `git rev-parse --abbrev-ref HEAD`\" > $(GIT_REVISION)-new
	rsync --checksum $(GIT_REVISION)-new $(GIT_REVISION) # only update git-revision.h and update it's timestamp if it's contents have changed
	-/bin/rm -f $(GIT_REVISION)-new

#
# rules ot build library objects
#

$(LIB_DIR)/%.o: src/%.cxx
	$(CXX) -c $(CFLAGS) $(OSFLAGS) -o $@ $<

$(LIB_DIR)/%.o: mxml/%.cxx
	$(CXX) -c $(CFLAGS) $(OSFLAGS) -o $@ $<

$(LIB_DIR)/%.o: mjson/%.cxx
	$(CXX) -c $(CFLAGS) $(OSFLAGS) -o $@ $<

$(LIB_DIR)/%.o: mvodb/%.cxx
	$(CXX) -c $(CFLAGS) $(OSFLAGS) -o $@ $<

#
# rules to build executables
#

$(BIN_DIR)/%: progs/%.cxx $(LIB)
	$(CXX) $(CFLAGS) $(OSFLAGS) -o $@ $^ $(LIBS)

$(BIN_DIR)/odbedit: progs/odbedit.cxx progs/cmdedit.cxx $(LIB)
	$(CXX) $(CFLAGS) $(OSFLAGS) -o $@ $^ $(LIBS)

$(BIN_DIR)/fetest: progs/fetest.cxx $(LIB_DIR)/mfe.o $(LIB)
	$(CXX) $(CFLAGS) $(OSFLAGS) -o $@ $^ $(LIBS)

#
# Main target
#

mini: $(GIT_REVISION) $(LIB) $(MINI_PROGS)

clean:: cleanmini

cleanmini:
	-rm -vf $(LIB_DIR)/*.o $(LIB_DIR)/*.a $(LIB_DIR)/*.so $(LIB_DIR)/*.dylib
	-rm -vf $(GIT_REVISION)
	-rm -rvf $(BIN_DIR)/*.dSYM
	-rm -vf $(BIN_DIR)/*
	-rmdir $(BIN_DIR)
	-rmdir $(LIB_DIR)
	-rmdir $(OS_DIR)

#
# Doxygen
#

dox:
	doxygen

clean:: cleandox

cleandox:
	-rm -rf html

#
# HTML lint
#

htmllint:
	java -jar ~/git/validator/dist/vnu.jar --filterpattern ".*Use CSS instead.*" resources/*.html

#
# gofmt
#

gofmt:
	gofmt -w progs/*.go

#
# make targets
#

remoteonly:
	$(MAKE) mini OS_DIR=$(OSTYPE)-remoteonly USERFLAGS=-DNO_LOCAL_ROUTINES=1

clean:: cleanremoteonly

cleanremoteonly:
	$(MAKE) OS_DIR=$(OSTYPE)-remoteonly cleanmini

linux32:
	$(MAKE) mini OS_DIR=linux-m32 USERFLAGS=-m32

linux64:
	$(MAKE) mini OS_DIR=linux-m64 USERFLAGS=-m64

clean:: clean32 clean64 cleanarm cleanemcraft

clean32:
	$(MAKE) OS_DIR=linux-m32 cleanmini

clean64:
	$(MAKE) OS_DIR=linux-m64 cleanmini

crosscompile:
	echo OSTYPE=$(OSTYPE)
	$(MAKE) OS_DIR=$(OSTYPE)-crosscompile OSTYPE=crosscompile

linuxarm:
	echo OSTYPE=$(OSTYPE)
	$(MAKE) OS_DIR=$(OSTYPE)-arm OSTYPE=crosslinuxarm

cleanarm:
	$(MAKE) OS_DIR=linux-arm cleanmini

linuxemcraft:
	echo OSTYPE=$(OSTYPE)
	$(MAKE) NO_EXECINFO=1 NO_FORK=1 OS_DIR=$(OSTYPE)-emcraft OSTYPE=crosslinuxemcraft

cleanemcraft:
	$(MAKE) OS_DIR=linux-emcraft cleanmini


#####################################################################
# mbedtls support

mbedtls:
	git clone https://github.com/ARMmbed/mbedtls.git
	cd mbedtls; git checkout mbedtls-2.16

update_mbedtls:
	cd mbedtls; git checkout mbedtls-2.16; git pull

clean_mbedtls:
	-rm -rf mbedtls

mtcpproxy:
	cd progs; go build mtcpproxy.go
	cp -pv progs/mtcpproxy bin/

#####################################################################

runtest:
	@echo
	@echo "make runtest" will create an empty experiment and run some basic tests
	@echo
	rm -f exptab
	rm -rf testexpt
	mkdir testexpt
	echo testexpt $(PWD)/testexpt testuser > exptab
	@echo
	@echo export MIDASSYS=$(PWD)
	@echo export MIDAS_EXPTAB=$(PWD)/exptab
	@echo
	MIDASSYS=$(PWD) MIDAS_EXPTAB=$(PWD)/exptab ./bin/odbinit
	MIDASSYS=$(PWD) MIDAS_EXPTAB=$(PWD)/exptab ./bin/odbedit -c "ls -l"
	MIDASSYS=$(PWD) MIDAS_EXPTAB=$(PWD)/exptab ./bin/mhttpd -D
	sleep 1
	MIDASSYS=$(PWD) MIDAS_EXPTAB=$(PWD)/exptab ./build/examples/experiment/frontend -D
	sleep 1
	MIDASSYS=$(PWD) MIDAS_EXPTAB=$(PWD)/exptab ./bin/mlogger -D
	sleep 1
	MIDASSYS=$(PWD) MIDAS_EXPTAB=$(PWD)/exptab ./bin/odbedit -c "scl"
	MIDASSYS=$(PWD) MIDAS_EXPTAB=$(PWD)/exptab ./bin/mtransition START
	sleep 2
	MIDASSYS=$(PWD) MIDAS_EXPTAB=$(PWD)/exptab ./bin/mtransition STOP
	sleep 1
	MIDASSYS=$(PWD) MIDAS_EXPTAB=$(PWD)/exptab ./bin/mtransition START
	sleep 2
	MIDASSYS=$(PWD) MIDAS_EXPTAB=$(PWD)/exptab ./bin/mtransition STOP
	MIDASSYS=$(PWD) MIDAS_EXPTAB=$(PWD)/exptab ./bin/mhist -l
	MIDASSYS=$(PWD) MIDAS_EXPTAB=$(PWD)/exptab ./bin/mhdump -L testexpt/*.hst
	MIDASSYS=$(PWD) MIDAS_EXPTAB=$(PWD)/exptab ./bin/odbedit -c "sh \"sample frontend\""
	MIDASSYS=$(PWD) MIDAS_EXPTAB=$(PWD)/exptab ./bin/odbedit -c "sh logger"
	MIDASSYS=$(PWD) MIDAS_EXPTAB=$(PWD)/exptab ./bin/odbedit -c "sh mhttpd"
	cut -b25- < testexpt/midas.log | sed 's/checksum: 0x.*, .* bytes/checksum: (omitted)/'

testmhttpd:
	MIDASSYS=$(PWD) MIDAS_EXPTAB=$(PWD)/exptab ./bin/mhttpd

test:
	$(MAKE) --no-print-directory runtest 2>&1 | grep -v "on host localhost stopped" | sed "sZ$(PWD)ZPWDZg" | sed "sZshm_unlink(.*)Zshm_unlink(SHM)Zg" | tee testexpt.log
	@echo
	@echo compare output of "make test" with testexpt.example
	@echo
	diff testexpt.example testexpt.log
	@echo
	@echo MIDAS self test completed with no errors
	@echo

#####################################################################
