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

#if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
#   # in standalone mode, install under <midas>/bin
#   set(CMAKE_INSTALL_PREFIX "${PROJECT_SOURCE_DIR}" CACHE PATH "")
#else()
#  # there is an user choice or a higher level project specifying the install path
#endif()

set(CMAKE_INSTALL_PREFIX ${CMAKE_SOURCE_DIR})

message(STATUS "MIDAS: CMAKE_INSTALL_PREFIX: ${CMAKE_INSTALL_PREFIX}")

set(SYSLIB_DIR ${PROJECT_SOURCE_DIR}/lib)
set(SYSINC_DIR ${PROJECT_SOURCE_DIR}/include)
set(MXML_DIR ${PROJECT_SOURCE_DIR}/../mxml)
set(MSCB_DIR ${PROJECT_SOURCE_DIR}/../mscb)
set(DRV_DIR ${PROJECT_SOURCE_DIR}/drivers)

#####################################################################
# Nothing needs to be modified after this line
#####################################################################

#
# Optional ROOT support
#
execute_process(COMMAND root-config --version OUTPUT_VARIABLE HAVE_ROOT)
if (HAVE_ROOT)
   string(REGEX REPLACE "\n" "" HAVE_ROOT ${HAVE_ROOT})
   message("Found ROOT version " ${HAVE_ROOT})
   execute_process(COMMAND root-config --incdir OUTPUT_VARIABLE ROOT_INC)
   execute_process(COMMAND root-config --libs OUTPUT_VARIABLE ROOT_LIBS)
   string(STRIP ${ROOT_LIBS} ROOT_LIBS)
   string(REGEX REPLACE "\n$" "" ROOT_INC "${ROOT_INC}")
   add_definitions(-DHAVE_ROOT)
   set(CMAKE_CXX_STANDARD 11)
endif(HAVE_ROOT)

#
# Optional SSL support for mhttpd
#
find_package(OpenSSL)
if (OPENSSL_FOUND)
   message("Found OpenSSL version " ${OPENSSL_VERSION})
endif()

#
# Check for MYSQL library for mlogger and for the history
#
execute_process(COMMAND mysql_config --version OUTPUT_VARIABLE HAVE_MYSQL)

#
# Check for ODBC for history support
#
if (EXISTS /usr/include/sql.h)
   set(HAVE_ODBC 1)
endif()

#
# Check for SQLITE for history support
#
if (EXISTS /usr/include/sqlite3.h)
   set(HAVE_SQLITE 1)
endif()

#
# Indicator that MSCB is available
#
if (EXISTS ${MSCB_DIR})
   set(HAVE_MSCB 1)
   message(STATUS "MSCB found")
endif()

#-----------------------
# Common flags
#
add_compile_options(-Wall -Wno-strict-aliasing -Wuninitialized -Wno-unused-function)

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
set(libsrc
   src/midas
   src/midas_cxx
   src/odb
   src/device_driver
   src/system
   src/alarm
   src/elog
   src/mrpc
   src/mjson
   src/json_paste
   src/mjsonrpc
   src/mjsonrpc_user
   src/history
   src/history_common
   src/history_midas
   src/history_odbc
   src/history_schema
   src/lz4
   src/lz4frame
   src/lz4hc
   src/xxhash
   src/crc32c
   src/sha256
   src/sha512
   src/ftplib
   progs/mdsupport
   ${MXML_DIR}/mxml
   ${MXML_DIR}/strlcpy
   ${GIT_REVISION}
   )

# this is the "object library" target: compiles the sources only once
add_library(objlib OBJECT ${libsrc})

# shared libraries need PIC
set_property(TARGET objlib PROPERTY POSITION_INDEPENDENT_CODE 1)

# shared and static libraries build from the same object files
add_library(midas STATIC $<TARGET_OBJECTS:objlib>)
add_library(midas-shared SHARED $<TARGET_OBJECTS:objlib>)

target_include_directories(objlib PUBLIC ${SYSINC_DIR})
target_include_directories(objlib PRIVATE ${DRV_DIR} ${MXML_DIR} ${MSCB_DIR}/include)
target_include_directories(objlib PRIVATE ${PROJECT_SOURCE_DIR}/drivers)
target_include_directories(objlib PRIVATE ${OPENSSL_INCLUDE_DIR})

# midas include is public so all programs linking to libmidas will inherit this
target_include_directories(midas PUBLIC ${SYSINC_DIR})

# library for mfe.o
add_library(mfe src/mfe)
target_include_directories(mfe PUBLIC ${SYSINC_DIR})
target_include_directories(mfe PRIVATE ${MXML_DIR})


install(TARGETS midas midas-shared mfe
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib)

#####################################################################
# generate git revision file
#####################################################################

find_package(Git)
set(GIT_REVISION ${PROJECT_SOURCE_DIR}/include/git-revision.h)
set(GIT_REVISION_TMP ${GIT_REVISION}.tmp)

add_custom_target(git_revision_h
   COMMAND ${CMAKE_COMMAND} -E echo_append "\\#define GIT_REVISION \\\"" > ${GIT_REVISION_TMP}
   COMMAND ${GIT_EXECUTABLE} log -n 1 --pretty=format:"%ad" >> ${GIT_REVISION_TMP}
   COMMAND ${CMAKE_COMMAND} -E echo_append " - " >> ${GIT_REVISION_TMP}
   COMMAND ${GIT_EXECUTABLE} describe --abbrev=8 --tags --dirty | tr -d '\\n' >> ${GIT_REVISION_TMP}
   COMMAND ${CMAKE_COMMAND} -E echo_append " on branch " >> ${GIT_REVISION_TMP}
   COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD  | tr -d '\\n' >> ${GIT_REVISION_TMP}
   COMMAND ${CMAKE_COMMAND} -E echo \\\" >> ${GIT_REVISION_TMP}
   COMMAND ${CMAKE_COMMAND} -E copy_if_different ${GIT_REVISION_TMP} ${GIT_REVISION}
   COMMAND ${CMAKE_COMMAND} -E remove ${GIT_REVISION_TMP}
   WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}/include/
)

add_dependencies(objlib git_revision_h)

#####################################################################
# sub-projects
#####################################################################
add_subdirectory(progs)
add_subdirectory(examples)
