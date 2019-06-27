cmake_minimum_required(VERSION 3.0)
project(frontend)

if (NOT DEFINED ENV{MIDASSYS})
   message(STATUS "MIDAS example/experiment: MIDASSYS environment variable not defined, assuming in-tree build")
   set(INC_PATH ../../include ../../mxml)
   set(LIB_PATH -L../..)
else()
   set(INC_PATH $ENV{MIDASSYS}/include $ENV{MIDASSYS}/mxml)
   set(LIB_PATH $ENV{MIDASSYS}/lib)
endif()

add_executable(frontend frontend.cxx)
target_include_directories(frontend PRIVATE ${INC_PATH})
target_link_libraries(frontend libmfe.a libmidas.a)
target_link_options(frontend PRIVATE ${LIB_PATH})

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
   target_link_libraries(analyzer librmana.a libmidas.a ${ROOT_LIBS} ${LIBS})
   target_link_options(analyzer PRIVATE ${LIB_PATH})
endif(HAVE_ROOT)
