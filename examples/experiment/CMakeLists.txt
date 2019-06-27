cmake_minimum_required(VERSION 3.0)
project(frontend)

if (NOT DEFINED ENV{MIDASSYS})
   message(SEND_ERROR "MIDASSYS environment variable not defined.")
endif()

add_executable(frontend frontend.cxx)
target_include_directories(frontend PRIVATE $ENV{MIDASSYS}/include $ENV{MIDASSYS}/mxml)
target_link_libraries(frontend $ENV{MIDASSYS}/lib/libmfe.a $ENV{MIDASSYS}/lib/libmidas.a)

if (NO_ROOT)
   message(STATUS "MIDAS: ROOT support is disabled via NO_ROOT")
else (NO_ROOT)
   execute_process(COMMAND root-config --version OUTPUT_VARIABLE HAVE_ROOT)
   if (HAVE_ROOT)
      string(REGEX REPLACE "\n" "" HAVE_ROOT ${HAVE_ROOT})
      message(STATUS "MIDAS: Found ROOT version " ${HAVE_ROOT})
      execute_process(COMMAND root-config --incdir OUTPUT_VARIABLE ROOT_INC)
      execute_process(COMMAND root-config --libs OUTPUT_VARIABLE ROOT_LIBS)
      string(STRIP ${ROOT_LIBS} ROOT_LIBS)
      string(REGEX REPLACE "\n$" "" ROOT_INC "${ROOT_INC}")
      set(CMAKE_CXX_STANDARD 11)
   else (HAVE_ROOT)
      message(STATUS "MIDAS: ROOT not found")
   endif(HAVE_ROOT)
endif(NO_ROOT)

if (HAVE_ROOT)
   add_executable(analyzer
      analyzer.cxx
      adccalib.cxx
      adcsum.cxx
      scaler.cxx)

   target_include_directories(analyzer PUBLIC ${ROOT_INC} $ENV{MIDASSYS}/include $ENV{MIDASSYS}/mxml)
   target_link_libraries(analyzer $ENV{MIDASSYS}/lib/librmana.a $ENV{MIDASSYS}/lib/libmidas.a ${ROOT_LIBS} ${LIBS})
endif(HAVE_ROOT)
