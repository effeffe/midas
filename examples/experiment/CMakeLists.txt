#
# CMakeLists.txt for midas examples/experiment
#
# S.R. 28 May 2019
# K.O. 28 June 2019
# K.O. 31 May 2021
#
# Optional settings:
#
# $MIDASSYS - env.variable pointing to the location of MIDAS
# $ROOTSYS  - env.variable pointing to the location of ROOT
#
# -DNO_ROOT=1 or unset $ROOTSYS - do not use ROOT
#
# Normal use:
#
# Copy files from examples/experiment to the experiment directory:
#
# mkdir -p $HOME/online/src
# cd $HOME/online/src
# cp $MIDASSYS/examples/experiment/* .
# mkdir build
# cd build
# cmake .. (or cmake3 ..)
# make
#

cmake_minimum_required(VERSION 3.0)

project(example_experiment)

#
# set default build type
#

if (NOT CMAKE_BUILD_TYPE OR CMAKE_BUILD_TYPE STREQUAL "")
    message(STATUS "Setting default build type to \"RelWithDebInfo\"")
    set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "" FORCE)
endif()

#
# Find MIDAS
#

if (NO_MIDAS)
   message(STATUS "${PROJECT_NAME}: MIDAS support is disabled via NO_MIDAS")
   unset(MIDAS_FOUND)
elseif(TARGET midas)
   message(STATUS "${PROJECT_NAME}: Building as a subproject of MIDAS")
   set(MIDAS_FOUND true)
elseif(DEFINED ENV{MIDASSYS})
   include($ENV{MIDASSYS}/lib/midas-targets.cmake)
   include($ENV{MIDASSYS}/lib/manalyzer-targets.cmake)
   message(STATUS "${PROJECT_NAME}: Building with MIDAS from $ENV{MIDASSYS}/lib/midas-targets.cmake")
   set(MIDAS_FOUND TRUE)
else()
   message(STATUS "${PROJECT_NAME}: Building without MIDAS")
   unset(MIDAS_FOUND)
endif()

#
# enable certain compile warnings
#

add_compile_options(-Wall -Wformat=2 -Wno-format-nonliteral -Wno-strict-aliasing -Wuninitialized -Wno-unused-function)

add_executable(frontend frontend.cxx)
target_link_libraries(frontend mfe midas)

if (NO_ROOT)
   message(STATUS "${PROJECT_NAME}: ROOT support is disabled via NO_ROOT")
   unset(ROOT_FOUND)
elseif (ROOT_FOUND)
   message(STATUS "${PROJECT_NAME}: Using ROOT: flags: ${ROOT_CXX_FLAGS} and includes: ${ROOT_INCLUDE_DIRS}")
elseif (DEFINED ENV{ROOTSYS})
   # You need to tell CMake where to find the ROOT installation. This can be done in a number of ways:
   #   - ROOT built with classic configure/make use the provided $ROOTSYS/etc/cmake/FindROOT.cmake
   #   - ROOT built with CMake. Add in CMAKE_PREFIX_PATH the installation prefix for ROOT
   list(APPEND CMAKE_PREFIX_PATH $ENV{ROOTSYS})

   #---Locate the ROOT package and defines a number of variables (e.g. ROOT_INCLUDE_DIRS)
   find_package(ROOT REQUIRED COMPONENTS Gui MathCore RIO Hist Tree Net RHTTP)

   #---Define useful ROOT functions and macros (e.g. ROOT_GENERATE_DICTIONARY)
   include(${ROOT_USE_FILE})
else()
   message(STATUS "${PROJECT_NAME}: ROOTSYS env.variable is not defined")
endif()

if (ROOT_FOUND)
   separate_arguments(ROOT_CXX_FLAGS UNIX_COMMAND "${ROOT_CXX_FLAGS}")
   message(STATUS "${PROJECT_NAME}: Found ROOT version " ${ROOT_VERSION})
else (ROOT_FOUND)
   message(STATUS "${PROJECT_NAME}: ROOT not found")
endif (ROOT_FOUND)

if (ROOT_FOUND)
   add_executable(analyzer
      analyzer.cxx
      adccalib.cxx
      adcsum.cxx
      scaler.cxx)

   target_include_directories(analyzer PUBLIC ${ROOT_INCLUDE_DIRS})
   target_compile_options(analyzer PUBLIC ${ROOT_CXX_FLAGS})
   target_link_libraries(analyzer rmana midas ${ROOT_LIBRARIES})

   add_executable(ana
      adccalib_module.cxx
      adcsum_module.cxx
      scaler_module.cxx
      )

   target_include_directories(ana PUBLIC ${ROOT_INCLUDE_DIRS})
   target_compile_options(ana PUBLIC -DHAVE_ROOT ${ROOT_CXX_FLAGS})
   target_link_libraries(ana manalyzer_main manalyzer midas ${ROOT_LIBRARIES})
endif(ROOT_FOUND)

#end
