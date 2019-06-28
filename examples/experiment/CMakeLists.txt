cmake_minimum_required(VERSION 3.0)
project(frontend)

if (${MIDAS_IN_TREE_BUILD})
   message(STATUS "MIDAS example/experiment: MIDAS_IN_TREE_BUILD: " ${MIDAS_IN_TREE_BUILD})
   set(INC_PATH $ENV{PWD}/../include $ENV{PWD}/../mxml)
   set(LIB_PATH $ENV{PWD})
else()
   message(STATUS "MIDAS example/experiment: MIDASSYS set to " $ENV{MIDASSYS})
   set(INC_PATH $ENV{MIDASSYS}/include $ENV{MIDASSYS}/mxml)
   set(LIB_PATH $ENV{MIDASSYS}/lib)
endif()

if (${CMAKE_SYSTEM_NAME} MATCHES Linux) 
   set(LIBS -lpthread -lutil -lrt) 
endif() 

link_directories(${LIB_PATH})

add_executable(frontend frontend.cxx)
target_include_directories(frontend PRIVATE ${INC_PATH})
target_link_libraries(frontend libmfe.a libmidas.a ${LIBS})

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
endif(HAVE_ROOT)
