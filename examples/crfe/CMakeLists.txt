cmake_minimum_required(VERSION 3.14)
project(crfe)

set(CMAKE_CXX_STANDARD 11)
set(MIDASSYS $ENV{MIDASSYS})
set(MXMLSYS ${MIDASSYS}/../mxml)

include_directories(${MIDASSYS}/include ${MXMLSYS})

add_executable(crfe crfe.cpp
        ${MIDASSYS}/src/mfe.c
        ${MIDASSYS}/src/midas.c
        ${MIDASSYS}/src/odb.c
        ${MIDASSYS}/src/system.c
        ${MIDASSYS}/src/alarm.c
        ${MIDASSYS}/src/elog.c
        ${MIDASSYS}/src/mrpc.c
        ${MXMLSYS}/mxml.c)
