#
# CMakeLists.txt for midas
#
# S.R. 28 May 2019
#

project(midas)
cmake_minimum_required(VERSION 3.1)

#
# Directories
#

# do not install under /usr/local, but under current directory
set(CMAKE_INSTALL_PREFIX ${CMAKE_SOURCE_DIR})
set(SYSLIB_DIR ${CMAKE_SOURCE_DIR}/lib)
set(SYSINC_DIR ${CMAKE_SOURCE_DIR}/include)
set(MXML_DIR ${CMAKE_SOURCE_DIR}/../mxml)
set(MSCB_DIR ${CMAKE_SOURCE_DIR}/../mscb)

#####################################################################
# Nothing needs to be modified after this line
#####################################################################

#
# Configurable options. Use "ccmake .." to change them.
#
option(USE_MYSQL   "mlogger begin/end of run info and history" OFF)
option(USE_ODBC    "history ODBC logging" OFF)
option(USE_SQLITE  "history sqlite logging" OFF)
option(USE_MSCB    "mhttpd mscb support" OFF)

#
# Check for MIDASSYS environment variable
#
if (EXISTS $ENV{MIDASSYS})
   set(MIDASSYS $ENV{MIDASSYS})
else()
   message(FATAL_ERROR "\nMissing definition of environment variable 'MIDASSYS'!")
endif()

#
# Optional ROOT support for mlogger
#
execute_process(COMMAND root-config --version OUTPUT_VARIABLE HAVE_ROOT)
if (HAVE_ROOT)
   string(REGEX REPLACE "\n" "" HAVE_ROOT ${HAVE_ROOT})
   message("Found ROOT version " ${HAVE_ROOT})
   execute_process(COMMAND root-config --incdir OUTPUT_VARIABLE ROOT_INC)
   execute_process(COMMAND root-config --libs OUTPUT_VARIABLE ROOT_LIBS)
   string(STRIP ${ROOT_LIBS} ROOT_LIBS)
   include_directories(${ROOT_INC})
   add_definitions(-DHAVE_ROOT)
endif(HAVE_ROOT)

#
# Optional SSL support for mhttpd
#
find_package(OpenSSL)
if (OPENSSL_FOUND)
   message("Found OpenSSL version" ${OPENSSL_VERSION})
endif()

#
# Optional MYSQL library for mlogger and for the history
#
if (USE_MYSQL)
   execute_process(COMMAND mysql_config --version OUTPUT_VARIABLE HAVE_MYSQL)
endif(USE_MYSQL)

#
# Optional ODBC history support
#
if (USE_ODBC)
   if (EXISTS /usr/include/sql.h)
      set(HAVE_ODBC 1)
   endif()
endif(USE_ODBC)

#
# Optional SQLITE history support
#
if (USE_SQLITE)
   if (EXISTS /usr/include/sqlite3.h)
      set(HAVE_SQLITE 1)
   endif()
endif(USE_SQLITE)

#
# Indicator that MSCB is available
#
if (USE_MSCB)
   if (EXISTS ${MSCB_DIR})
      set(HAVE_MSCB 1)
   endif()
endif(USE_MSCB)

#-----------------------
# Common flags
#
add_compile_options(-Wall -Wno-strict-aliasing -Wuninitialized -Wno-unused-function)
set(CMAKE_CXX_STANDARD 11)

#-----------------------
# Include directories
#
include_directories(${SYSINC_DIR} ${DRV_DIR} ${MXML_DIR} ${MSCB_DIR}/include)
include_directories(${OPENSSL_INCLUDE_DIR})

#-----------------------
# Mongoose server
#
add_definitions(-DHAVE_MONGOOSE6 -DMG_ENABLE_THREADS -DMG_DISABLE_CGI)
if (OPENSSL_FOUND)
   add_definitions(-DMG_ENABLE_SSL)
endif(OPENSSL_FOUND)

#####################################################################
# Conditional code for various OS'es
#####################################################################

#-----------------------
# MacOSC/Darwin
#
if (${CMAKE_SYSTEM_NAME} MATCHES Darwin)
endif()

#-----------------------
# This is for Linux
#
if (${CMAKE_SYSTEM_NAME} MATCHES Linux)
   # >2GB file support
   add_definitions(-D_LARGEFILE64_SOURCE)
   set(LIBS -lpthread -lutil -lrt)
endif()

#####################################################################
# end of conditional code
#####################################################################

#
# Midas Library
#
add_library(midas STATIC 
   src/midas.c
   src/midas_cxx.cxx
   src/odb.c
   src/system.c
   src/alarm.c
   src/elog.c
   src/mrpc.c
   src/mjson.cxx
   src/json_paste.cxx
   src/mjsonrpc.cxx
   src/mjsonrpc_user.cxx
   src/history.c
   src/history_common.cxx
   src/history_midas.cxx
   src/history_odbc.cxx
   src/history_schema.cxx
   src/lz4.c
   src/lz4frame.c
   src/lz4hc.c
   src/xxhash.c
   src/crc32c.c
   src/sha256.c
   src/sha512.c
   src/ftplib.c
   src/mdsupport.cxx
   ${MXML_DIR}/mxml.c
   ${MXML_DIR}/strlcpy.c
   )

#
# Standard targets requiring only midas library
#
set(PROGS
   mserver
   odbinit
   mlogger
   msequencer
   lazylogger
   mtransition
   )

foreach(PROG ${PROGS})
   add_executable(${PROG} src/${PROG})
   target_link_libraries(${PROG} midas ${LIBS})
endforeach()

#
# non-standard executables using additional source files
#
add_executable(mhttpd src/mhttpd.cxx src/mongoose6.c src/mgd.c)
target_link_libraries(mhttpd midas ${OPENSSL_LIBRARIES} ${LIBS})

add_executable(feudp utils/feudp.cxx src/mfe.c)
target_link_libraries(feudp midas ${LIBS})

add_executable(fetest utils/fetest.cxx src/mfe.c)
target_link_libraries(fetest midas ${LIBS})

add_executable(odbedit src/odbedit.cxx src/cmdedit.cxx)
target_link_libraries(odbedit midas ${LIBS})

target_link_libraries(mlogger ${ROOT_LIBS})

#####################################################################
# sub-projects
#####################################################################
add_subdirectory(utils)
add_subdirectory(examples)
