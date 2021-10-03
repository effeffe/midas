cmake_minimum_required(VERSION 3.0)

project(wdfe)

if (NOT DEFINED ENV{WDBSYS})
   message(SEND_ERROR "WDBSYS environment variable not defined.")
endif()
if (NOT DEFINED ENV{MIDASSYS})
   message(SEND_ERROR "MIDASSYS environment variable not defined.")
endif()

set(LIBS $ENV{MIDASSYS}/lib/libmfe.a $ENV{MIDASSYS}/lib/libmidas.a)
if (${CMAKE_SYSTEM_NAME} MATCHES Linux)
   set(LIBS ${LIBS} -lpthread -lutil -lrt)
endif()

add_executable(wdfe wdfe.cxx
   $ENV{WDBSYS}/sw/src/averager.cpp
   $ENV{WDBSYS}/sw/src/WDBLib.cpp
   $ENV{WDBSYS}/sw/src/DCBLib.cpp)
target_include_directories(wdfe PRIVATE
   $ENV{MIDASSYS}/include
   $ENV{MIDASSYS}/mxml
   $ENV{WDBSYS}/sw/include
   $ENV{WDBSYS}/sw/dcb/app/src/)
 
target_link_libraries(wdfe ${LIBS})
target_compile_definitions(wdfe
   PRIVATE -DWD2_DONT_INCLUDE_REG_ACCESS_VARS
   PRIVATE -DDCB_DONT_INCLUDE_REG_ACCESS_VARS)

set_property(TARGET wdfe PROPERTY CXX_STANDARD 11)
