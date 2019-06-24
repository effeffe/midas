cmake_minimum_required(VERSION 3.0)

if (NOT DEFINED ENV{WDBSYS})
   message(SEND_ERROR "WDBSYS environment variable not defined.")
endif()
if (NOT DEFINED ENV{MIDASSYS})
   message(SEND_ERROR "MIDASSYS environment variable not defined.")
endif()

add_executable(wdfe wdfe $ENV{WDBSYS}/software/src/WDBLib.cpp)
target_include_directories(wdfe PRIVATE $ENV{MIDASSYS}/include $ENV{MIDASSYS}/mxml $ENV{WDBSYS}/software/include)
target_link_libraries(wdfe $ENV{MIDASSYS}/lib/libmfe.a $ENV{MIDASSYS}/lib/libmidas.a $ENV{WDBSYS}/software/lib/libwdb.a)
target_compile_definitions(wdfe PRIVATE -DWD2_DONT_INCLUDE_REG_ACCESS_VARS)

set_property(TARGET wdfe PROPERTY CXX_STANDARD 11)
