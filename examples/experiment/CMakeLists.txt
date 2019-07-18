#
# CMakeLists.txt for midas examples/experiment
#
# S.R. 28 May 2019
# K.O. 28 June 2019
#
# This cmakefile is dual use:
# if MIDAS_IN_TREE_BUILD is set, the example experiment is built as part of the midas compilation
# if MIDAS_IN_TREE_BUILD is unset, the example experiment is built against MIDAS installed at $MIDASSYS
#
# Optional settings for this cmakefile:
#
# $MIDASSYS - env.variable pointing to the location of MIDAS
# root-config in the $PATH - sets the location of ROOT for the analyzer
# -DNO_ROOT=1 - do not use ROOT
#
# Normal use:
#
# Copy all files from examples/experiment to the experiment directory:
# mkdir -p $HOME/online/src
# cd $HOME/online/src
# cp $MIDASSYS/examples/experiment/* .
# mkdir build
# cd build
# cmake ..
# or cmake3 ..
# make
#
# For testing the cmakefile inside the midas tree:
#
# cd $HOME/git/midas/examples/experiment
# mkdir build
# cd build
# MIDASSYS=$HOME/git/midas cmake ..
# make
#

cmake_minimum_required(VERSION 3.0)
project(frontend)

# check if we are called from parent directory
if (NOT ${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_CURRENT_SOURCE_DIR})
   message(STATUS "MIDAS example/experiment: MIDAS in-tree-build")
else()
   message(STATUS "MIDAS example/experiment: MIDASSYS set to " $ENV{MIDASSYS})
   if (NOT EXISTS $ENV{MIDASSYS})
      message(FATAL_ERROR "Environment variable $MIDASSYS not defined, aborting.")
   endif()
   set(INC_PATH $ENV{MIDASSYS}/include $ENV{MIDASSYS}/mxml)
   link_directories($ENV{MIDASSYS}/lib)
endif()

# default build type
if (NOT CMAKE_BUILD_TYPE OR CMAKE_BUILD_TYPE STREQUAL "")
    message(STATUS "Setting default build type to \"RelWithDebInfo\"")
    set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "" FORCE)
endif()

# enable certain compile warnings
add_compile_options(-Wall -Wformat=2 -Wno-format-nonliteral -Wno-strict-aliasing -Wuninitialized -Wno-unused-function)

# ass required linux libraries
if (${CMAKE_SYSTEM_NAME} MATCHES Linux) 
   set(LIBS -lpthread -lutil -lrt) 
endif() 

add_executable(frontend frontend.cxx)
target_include_directories(frontend PRIVATE ${INC_PATH})
target_link_libraries(frontend mfe midas ${LIBS})

if (NO_ROOT)
   message(STATUS "MIDAS example/experiment: ROOT support is disabled via NO_ROOT")
else (NO_ROOT)
   execute_process(COMMAND root-config --version OUTPUT_VARIABLE HAVE_ROOT)
   if (HAVE_ROOT)
      string(REGEX REPLACE "\n" "" HAVE_ROOT ${HAVE_ROOT})
      message(STATUS "MIDAS example/experiment: Found ROOT version " ${HAVE_ROOT})
      execute_process(COMMAND root-config --incdir OUTPUT_VARIABLE ROOT_INC)
      execute_process(COMMAND root-config --libs OUTPUT_VARIABLE ROOT_LIBS)
      string(STRIP ${ROOT_LIBS} ROOT_LIBS)
      string(REGEX REPLACE "\n$" "" ROOT_INC "${ROOT_INC}")
      set(CMAKE_CXX_STANDARD 11)
   else (HAVE_ROOT)
      message(STATUS "MIDAS example/experiment: ROOT not found")
   endif(HAVE_ROOT)
endif(NO_ROOT)

if (HAVE_ROOT)
   add_executable(analyzer
      analyzer.cxx
      adccalib.cxx
      adcsum.cxx
      scaler.cxx)

   target_include_directories(analyzer PUBLIC ${ROOT_INC} ${INC_PATH})
   target_link_libraries(analyzer rmana midas ${ROOT_LIBS} ${LIBS})
endif(HAVE_ROOT)

#end
