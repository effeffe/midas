cmake_minimum_required(VERSION 3.0)
project(crfe)

# Check for MIDASSYS environment variable
if (NOT DEFINED ENV{MIDASSYS})
   message(SEND_ERROR "MIDASSYS environment variable not defined.")
endif()

set(CMAKE_CXX_STANDARD 11)
set(MIDASSYS $ENV{MIDASSYS})

if (${CMAKE_SYSTEM_NAME} MATCHES Linux)
   set(LIBS -lpthread -lutil -lrt)
endif()

add_executable(crfe crfe.cpp)

target_include_directories(crfe PRIVATE ${MIDASSYS}/include)
target_link_libraries(crfe ${MIDASSYS}/lib/libmfe.a ${MIDASSYS}/lib/libmidas.a ${LIBS})
