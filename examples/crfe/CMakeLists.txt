cmake_minimum_required(VERSION 3.14)
project(crfe)

set(CMAKE_CXX_STANDARD 11)
set(MIDASSYS $ENV{MIDASSYS})
set(MXMLSYS ${MIDASSYS}/../mxml)

add_executable(crfe crfe.cpp
        ${MIDASSYS}/src/mfe.c
        ${MXMLSYS}/mxml.c)

target_include_directories(crfe PRIVATE ${MIDASSYS}/include ${MXMLSYS})
target_link_libraries(crfe ${MIDASSYS}/lib/libmidas.a)