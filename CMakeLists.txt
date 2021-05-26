#
# CMakeLists.txt for midas
#
# S.R. 28 May 2019
#

message(STATUS "MIDAS: cmake version: " ${CMAKE_VERSION})
cmake_minimum_required(VERSION 3.0)

if (NOT CMAKE_CXX_STANDARD)    # let top-level project overwrite c++ standard
   set(CMAKE_CXX_STANDARD 11)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)

#message(STATUS "MIDAS: CMAKE_CXX_FLAGS_RELWITHDEBINFO: ${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")
#message(STATUS "MIDAS: CMAKE_CXX_COMPILE_FEATURES: ${CMAKE_CXX_COMPILE_FEATURES}")

project(midas)

#
# Directories
#


if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
   # in standalone mode, install under <midas>/bin
   set(CMAKE_INSTALL_PREFIX ${CMAKE_SOURCE_DIR} CACHE PATH "" FORCE)
else()
   # there is an user choice or a higher level project
   # specifying the install path - keep that
   set(CMAKE_INSTALL_PREFIX ${CMAKE_INSTALL_PREFIX})
endif()
message(STATUS "MIDAS: CMAKE_INSTALL_PREFIX: ${CMAKE_INSTALL_PREFIX}")

set(SYSLIB_DIR ${PROJECT_SOURCE_DIR}/lib)
set(SYSINC_DIR ${PROJECT_SOURCE_DIR}/include)
set(DRV_DIR ${PROJECT_SOURCE_DIR}/drivers)

# NB: RPATH is not needed as everything is linked statically. K.O.
## Ensure the C-compat library can load the main library after install
#set(CMAKE_INSTALL_RPATH ${CMAKE_INSTALL_PREFIX}/lib)
#set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

#####################################################################
# Nothing needs to be modified after this line
#####################################################################

#
# Check for mxml
#
if (NOT EXISTS ${PROJECT_SOURCE_DIR}/mxml/mxml.cxx)
   message(FATAL_ERROR "\nError: 'mxml' submodule not found. Please install via\n"
      "'git submodule update --init'\n"
      "in midas root directory."
   )
endif()

#
# Check for mjson
#
if (NOT EXISTS ${PROJECT_SOURCE_DIR}/mjson/mjson.cxx)
   message(FATAL_ERROR "\nError: 'mjson' submodule not found. Please install via\n"
      "'git submodule update --init'\n"
      "in midas root directory."
   )
endif()

#
# Check for mvodb
#
if (NOT EXISTS ${PROJECT_SOURCE_DIR}/mvodb/mvodb.cxx)
   message(FATAL_ERROR "\nError: 'mvodb' submodule not found. Please install via\n"
      "'git submodule update --init'\n"
      "in midas root directory."
   )
endif()

#
# Check for midasio
#
if (NOT EXISTS ${PROJECT_SOURCE_DIR}/midasio/midasio.cxx)
   message(FATAL_ERROR "\nError: 'midasio' submodule not found. Please install via\n"
      "'git submodule update --init'\n"
      "in midas root directory."
   )
endif()

#
# Check for manalyzer
#
if (NOT EXISTS ${PROJECT_SOURCE_DIR}/manalyzer/manalyzer.cxx)
   message(FATAL_ERROR "\nError: 'manalyzer' submodule not found. Please install via\n"
      "'git submodule update --init'\n"
      "in midas root directory."
   )
endif()

#
# Check for mscb
#
if (NOT EXISTS ${PROJECT_SOURCE_DIR}/mscb/src/mscb.cxx)
   message(FATAL_ERROR "\nError: 'mscb' submodule not found. Please install via\n"
      "'git submodule update --init'\n"
      "in midas root directory."
   )
endif()

#
# NO_LOCAL_ROUTINES support for only remote connection
#
if (NO_LOCAL_ROUTINES)
   message(STATUS "MIDAS: building with NO_LOCAL_ROUTINES")
   add_compile_options(-DNO_LOCAL_ROUTINES=1)
endif()

#
# Optional ROOT support
#
if (NO_ROOT)
   message(STATUS "MIDAS: ROOT support is disabled via NO_ROOT")
   unset(HAVE_ROOT)
else (NO_ROOT)

   if (DEFINED ENV{ROOTSYS})
      # You need to tell CMake where to find the ROOT installation. This can be done in a number of ways:
      #   - ROOT built with classic configure/make use the provided $ROOTSYS/etc/cmake/FindROOT.cmake
      #   - ROOT built with CMake. Add in CMAKE_PREFIX_PATH the installation prefix for ROOT
      list(APPEND CMAKE_PREFIX_PATH $ENV{ROOTSYS})

      #---Locate the ROOT package and defines a number of variables (e.g. ROOT_INCLUDE_DIRS)
      find_package(ROOT REQUIRED COMPONENTS MathCore RIO Hist Tree Net)

      #---Define useful ROOT functions and macros (e.g. ROOT_GENERATE_DICTIONARY)
      #include(${ROOT_USE_FILE}) #  this pollutes midas cflags & co with ROOT include and library paths 

      if (ROOT_FOUND)
         separate_arguments(ROOT_CXX_FLAGS UNIX_COMMAND "${ROOT_CXX_FLAGS}")
         message(STATUS "MIDAS: Found ROOT version " ${ROOT_VERSION} " in " $ENV{ROOTSYS})
         set(HAVE_ROOT true)
      else (ROOT_FOUND)
         message(STATUS "MIDAS: ROOT not found")
      endif (ROOT_FOUND)
   else ()
      message(STATUS "MIDAS: ROOTSYS not defined")
      unset(HAVE_ROOT)
   endif()

endif(NO_ROOT)

#
# Optional ZLIB support for mlogger
#
if (NO_ZLIB)
   message(STATUS "MIDAS: ZLIB support is disabled via NO_ZLIB")
else (NO_ZLIB)
find_package(ZLIB)
if (ZLIB_FOUND)
   message(STATUS "MIDAS: Found ZLIB version " ${ZLIB_VERSION_STRING})
   add_compile_options(-DHAVE_ZLIB)
   set(LIBS ${LIBS} -lz)
else ()
   message(STATUS "MIDAS: ZLIB not found")
endif()
endif (NO_ZLIB)

#
# Optional SSL support for mhttpd
#
if (USE_OPENSSL)
if (NO_SSL)
   message(STATUS "MIDAS: OpenSSL https support is disabled via NO_SSL")
else (NO_SSL)
find_package(OpenSSL)
if (OPENSSL_FOUND)
   message(STATUS "MIDAS: Found OpenSSL version " ${OPENSSL_VERSION})
else ()
   message(STATUS "MIDAS: OpenSSL not found")
endif ()
endif (NO_SSL)
endif (USE_OPENSSL)

#
# Optional libcurl support for mlogger
#
if (NO_CURL)
   message(STATUS "MIDAS: libcurl mlogger support is disabled via NO_CURL")
else (NO_CURL)
   find_package(CURL)
   if (CURL_FOUND)
      if (EXISTS ${CURL_INCLUDE_DIRS}/curl/curl.h)
         message(STATUS "MIDAS: Found LibCURL version " ${CURL_VERSION_STRING})
         add_compile_options(-DHAVE_CURL)
         set(LIBS ${LIBS} -lcurl)
         set(HAVE_CURL true)
      else ()
         message(STATUS "MIDAS: curl.h not found")
      endif ()
   else ()
      message(STATUS "MIDAS: LibCURL not found")
   endif ()
endif (NO_CURL)

#
# Optional MBEDTLS SSL support for mhttpd
#
if (NO_MBEDTLS)
   message(STATUS "MIDAS: MBEDTLS https support is disabled via NO_MBEDTLS")
else (NO_MBEDTLS)
   if (EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/version.c)
      message(STATUS "MIDAS: Found MBEDTLS in ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls")
      set(MBEDTLS_FOUND 1)
      add_subdirectory(mbedtls)
   else ()
      message(STATUS "MIDAS: MBEDTLS not found")
   endif ()
endif (NO_MBEDTLS)

#
# Check for MYSQL library for mlogger and for the history
#
if (NO_MYSQL)
   message(STATUS "MIDAS: MySQL support is disabled via NO_MYSQL")
else (NO_MYSQL)
   execute_process(COMMAND mariadb_config --version OUTPUT_STRIP_TRAILING_WHITESPACE OUTPUT_VARIABLE HAVE_MYSQL)
   if (HAVE_MYSQL)
      message(STATUS "MIDAS: Found MariaDB (MySQL replacement) version " ${HAVE_MYSQL})
      set(HAVE_MYSQL 1)
      execute_process(COMMAND mariadb_config --cflags OUTPUT_STRIP_TRAILING_WHITESPACE OUTPUT_VARIABLE MYSQL_CFLAGS)
      execute_process(COMMAND mariadb_config --libs OUTPUT_STRIP_TRAILING_WHITESPACE OUTPUT_VARIABLE MYSQL_LIBS)
   else ()
      execute_process(COMMAND mysql_config --version OUTPUT_STRIP_TRAILING_WHITESPACE OUTPUT_VARIABLE HAVE_MYSQL)
      if (HAVE_MYSQL)
         message(STATUS "MIDAS: Found MySQL version " ${HAVE_MYSQL})
         set(HAVE_MYSQL 1)
         execute_process(COMMAND mysql_config --cflags OUTPUT_STRIP_TRAILING_WHITESPACE OUTPUT_VARIABLE MYSQL_CFLAGS)
         execute_process(COMMAND mysql_config --libs OUTPUT_STRIP_TRAILING_WHITESPACE OUTPUT_VARIABLE MYSQL_LIBS)
      else ()
         message(STATUS "MIDAS: MySQL not found")
      endif ()
   endif ()
   if (HAVE_MYSQL)
      separate_arguments(MYSQL_CFLAGS)
      add_compile_options(-DHAVE_MYSQL ${MYSQL_CFLAGS})
      set(DB_LIBS ${DB_LIBS} ${MYSQL_LIBS})
   endif ()
endif (NO_MYSQL)

#
# Check for ODBC for history support
#
if (NO_ODBC)
   message(STATUS "MIDAS: ODBC support is disabled via NO_ODBC")
else (NO_ODBC)
if (EXISTS /usr/include/sql.h)
   message(STATUS "MIDAS: Found ODBC /usr/include/sql.h")
   set(HAVE_ODBC 1)
   add_compile_options(-DHAVE_ODBC)
   set(DB_LIBS ${DB_LIBS} -lodbc)
else ()
   message(STATUS "MIDAS: ODBC not found")
endif ()
endif (NO_ODBC)

#
# Check for SQLITE for history support
#
if (NO_SQLITE)
   message(STATUS "MIDAS: SQLITE support is disabled via NO_SQLITE")
else (NO_SQLITE)
   find_package(SQLite3)
   if (SQLite3_FOUND)
      message(STATUS "MIDAS: Found SQLITE: " ${SQLite3_VERSION} " in " ${SQLite3_INCLUDE_DIRS})
      set(HAVE_SQLITE 1)
      add_compile_options(-DHAVE_SQLITE)
      set(DB_LIBS ${DB_LIBS} ${SQLite3_LIBRARIES})
   else ()
      message(STATUS "MIDAS: SQLITE not found")
   endif()
endif (NO_SQLITE)

#
# FTP support for mdsupport
#
add_compile_options(-DHAVE_FTPLIB)

#
# Optional nvidia gpu support, HAVE_NVIDIA is a count of CPUs
#
if (NOT NO_NVIDIA)
   set(NO_NVIDIA 0 CACHE PATH "")
endif()
if (NO_NVIDIA)
   message(STATUS "MIDAS (msysmon): NVIDIA GPU support is disabled via NO_NVIDIA")
else (NO_NVIDIA)
  if (EXISTS /usr/local/cuda/include)
    execute_process(COMMAND nvidia-smi -L COMMAND wc -l OUTPUT_VARIABLE HAVE_NVIDIA)
    if (HAVE_NVIDIA)
       message(STATUS "MIDAS (msysmon): Found nvidia-smi. Number of GPUs detected: " ${HAVE_NVIDIA} )
       set(NVIDIA_INC /usr/local/cuda/include)
       set(NVIDIA_LIBS -L/usr/local/cuda/lib64 -lnvidia-ml)
    else (HAVE_NVIDIA)
       message(STATUS "MIDAS (msysmon): nvidia-smi not found")
    endif(HAVE_NVIDIA)
  else()
    message(STATUS "MIDAS (msysmon): NVIDIA CUDA libs not found")
  endif()
endif(NO_NVIDIA)

#
# Optional lm-sensors support (used msysmon)
#

if (NO_SENSORS)
  message(STATUS "MIDAS (msysmon): LM_SENSORS disabled")
else()
  find_library(libsensors sensors) #Requires libsesnsors-dev
  if(libsensors STREQUAL "libsensors-NOTFOUND")
    message(STATUS "MIDAS (msysmon): LM_SENSORS libs not found")
  else()
    message(STATUS "MIDAS (msysmon): Found LM_SENSORS ")
    set(HAVE_LM_SENSORS TRUE)
  endif()
endif(NO_SENSORS)

#-----------------------
# Default build type
#

string(REPLACE "-DNDEBUG" "" CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO}") # midas requires working assert()

if (NOT CMAKE_BUILD_TYPE OR CMAKE_BUILD_TYPE STREQUAL "")
    message(STATUS "MIDAS: Setting default build type to \"RelWithDebInfo\", compiler flags: " ${CMAKE_CXX_FLAGS_RELWITHDEBINFO})
    set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "" FORCE)
endif()

#-----------------------
# Common flags
#
add_compile_options(-Wall -Wformat=2 -Wno-format-nonliteral -Wno-strict-aliasing -Wuninitialized -Wno-unused-function)

#####################################################################
# Conditional code for various OS'es
#####################################################################

#-----------------------
# MacOSC/Darwin
#
if (${CMAKE_SYSTEM_NAME} MATCHES Darwin)
endif()

#-----------------------
# This is for Linux
#
if (${CMAKE_SYSTEM_NAME} MATCHES Linux)
   # >2GB file support
   add_definitions(-D_LARGEFILE64_SOURCE)
   set(LIBS ${LIBS} -lpthread -lutil -lrt -ldl)
endif()

#####################################################################
# end of conditional code
#####################################################################

#
# Midas Library
#
set(libsrc
   src/midas
   src/odb
   src/odbxx
   src/device_driver
   src/system
   src/alarm
   src/elog
   src/mrpc
   mjson/mjson
   src/tmfe
   mvodb/mvodb
   mvodb/nullodb
   mvodb/midasodb
   mvodb/mxmlodb
   mvodb/mjsonodb
   midasio/midasio
   manalyzer/manalyzer
   src/json_paste
   src/mjsonrpc
   src/mjsonrpc_user
   src/history
   src/history_common
   src/history_odbc
   src/history_schema
   src/history_image
   src/lz4
   src/lz4frame
   src/lz4hc
   src/xxhash
   src/crc32c
   src/sha256
   src/sha512
   src/ftplib
   src/mdsupport
   mxml/mxml.cxx
   mxml/strlcpy.cxx
   ${GIT_REVISION}
   )

set(libsrc-c-compat
   src/midas_c_compat
   )

# this is the "object library" target: compiles the sources only once
add_library(objlib OBJECT ${libsrc})
add_library(objlib-c-compat OBJECT ${libsrc-c-compat})

# shared libraries need PIC
set_property(TARGET objlib PROPERTY POSITION_INDEPENDENT_CODE 1)
set_property(TARGET objlib-c-compat PROPERTY POSITION_INDEPENDENT_CODE 1)

# static midas library
add_library(midas STATIC $<TARGET_OBJECTS:objlib>)

# shared midas library
add_library(midas-shared SHARED $<TARGET_OBJECTS:objlib>)
target_link_libraries(midas-shared ${LIBS})
target_link_libraries(midas-shared ${DB_LIBS})

# shared midas library with C compatible signatures
add_library(midas-c-compat SHARED $<TARGET_OBJECTS:objlib-c-compat>)
target_link_libraries(midas-c-compat midas)
target_link_libraries(midas-c-compat ${LIBS})
if (HAVE_MYSQL)
   target_link_libraries(midas-c-compat ${DB_LIBS})
endif()

target_include_directories(objlib PUBLIC ${SYSINC_DIR})
target_include_directories(objlib PRIVATE ${DRV_DIR} ${PROJECT_SOURCE_DIR}/mxml ${PROJECT_SOURCE_DIR}/mjson ${PROJECT_SOURCE_DIR}/mvodb ${PROJECT_SOURCE_DIR}/midasio ${PROJECT_SOURCE_DIR}/manalyzer ${PROJECT_SOURCE_DIR}/mscb/include)
target_include_directories(objlib PRIVATE ${PROJECT_SOURCE_DIR}/drivers)
if (OPENSSL_FOUND)
   target_include_directories(objlib PRIVATE ${OPENSSL_INCLUDE_DIR})
endif ()
if (CURL_FOUND)
   target_include_directories(objlib PRIVATE ${CURL_INCLUDE_DIR})
endif ()

target_include_directories(objlib-c-compat PUBLIC ${SYSINC_DIR})
target_include_directories(objlib-c-compat PRIVATE ${DRV_DIR} ${PROJECT_SOURCE_DIR}/mxml ${PROJECT_SOURCE_DIR}/mscb/include)
target_include_directories(objlib-c-compat PRIVATE ${PROJECT_SOURCE_DIR}/drivers)
if (OPENSSL_FOUND)
   target_include_directories(objlib-c-compat PRIVATE ${OPENSSL_INCLUDE_DIR})
endif ()
if (CURL_FOUND)
   target_include_directories(objlib PRIVATE ${CURL_INCLUDE_DIR})
endif ()

# midas include is public so all programs linking to libmidas will inherit this
target_include_directories(midas PUBLIC ${SYSINC_DIR} ${PROJECT_SOURCE_DIR}/manalyzer ${PROJECT_SOURCE_DIR}/midasio ${PROJECT_SOURCE_DIR}/mvodb)

# library for mfe
add_library(mfe src/mfe)
target_include_directories(mfe PUBLIC ${SYSINC_DIR})
target_include_directories(mfe PRIVATE ${PROJECT_SOURCE_DIR}/mxml)

# library for mana
add_library(mana src/mana)
target_include_directories(mana PUBLIC ${SYSINC_DIR})
target_include_directories(mana PRIVATE ${PROJECT_SOURCE_DIR}/mxml)

# library for rmana
add_library(rmana src/mana)
target_include_directories(rmana PUBLIC ${SYSINC_DIR})
target_include_directories(rmana PRIVATE ${PROJECT_SOURCE_DIR}/mxml)
if (HAVE_ROOT)
   target_include_directories(rmana PUBLIC ${ROOT_INCLUDE_DIRS})
   target_compile_options(rmana PRIVATE -DHAVE_ROOT ${ROOT_CXX_FLAGS})
endif(HAVE_ROOT)

# library for manalyzer_main
add_library(manalyzer_main manalyzer/manalyzer_main)
target_include_directories(manalyzer_main PUBLIC ${SYSINC_DIR})
target_include_directories(manalyzer_main PRIVATE ${PROJECT_SOURCE_DIR}/mxml ${PROJECT_SOURCE_DIR}/manalyzer ${PROJECT_SOURCE_DIR}/midasio ${PROJECT_SOURCE_DIR}/mvodb)
if (HAVE_ROOT)
   target_include_directories(manalyzer_main PUBLIC ${ROOT_INCLUDE_DIRS})
   target_compile_options(manalyzer_main PRIVATE -DHAVE_ROOT ${ROOT_CXX_FLAGS})
endif(HAVE_ROOT)

install(TARGETS midas midas-shared midas-c-compat mfe mana rmana manalyzer_main
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib)

# library for mfe.o
add_library(mfeo OBJECT src/mfe)
target_include_directories(mfeo PUBLIC ${SYSINC_DIR})
target_include_directories(mfeo PRIVATE ${PROJECT_SOURCE_DIR}/mxml)

# this is somewhat dirty, as the path to the object dir is hardcoded and
# might change in future CMake versions. The only other option however
# would be to pack and then unpack again a library (NB, 12.6.2019)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/mfeo.dir/src/mfe.cxx.o DESTINATION lib RENAME mfe.o)

# library for mana.o
add_library(manao OBJECT src/mana)
target_include_directories(manao PUBLIC ${SYSINC_DIR})
target_include_directories(manao PRIVATE ${PROJECT_SOURCE_DIR}/mxml)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/manao.dir/src/mana.cxx.o DESTINATION lib RENAME mana.o)

# library for rmana.o
add_library(rmanao OBJECT src/mana)
target_include_directories(rmanao PUBLIC ${SYSINC_DIR})
target_include_directories(rmanao PRIVATE ${PROJECT_SOURCE_DIR}/mxml)
if (HAVE_ROOT)
   target_include_directories(rmanao PUBLIC ${ROOT_INCLUDE_DIRS})
   target_compile_options(rmanao PRIVATE -DHAVE_ROOT)
endif(HAVE_ROOT)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/rmanao.dir/src/mana.cxx.o DESTINATION lib RENAME rmana.o)

# keeps track of all the built libraries
set(MIDAS_LIBRARIES
#   objlib
#   objlib-c-compat
   midas
   midas-shared
   midas-c-compat
   mfe
#   mana
#   rmana
#   mfeo
#   manao
#   rmanao
   )
#MESSAGE (status ${MIDAS_LIBRARIES})

#####################################################################
# generate git revision file
#####################################################################

find_package(Git)
set(GIT_REVISION ${PROJECT_SOURCE_DIR}/include/git-revision.h)
set(GIT_REVISION_TMP ${GIT_REVISION}.tmp)

add_custom_target(git_revision_h
   COMMAND ${CMAKE_COMMAND} -E echo_append "\\#define GIT_REVISION \\\"" > ${GIT_REVISION_TMP}
   COMMAND ${GIT_EXECUTABLE} log -n 1 --pretty=format:"%ad" >> ${GIT_REVISION_TMP}
   COMMAND ${CMAKE_COMMAND} -E echo_append " - " >> ${GIT_REVISION_TMP}
   COMMAND ${GIT_EXECUTABLE} describe --abbrev=8 --tags --dirty | tr -d '\\n' >> ${GIT_REVISION_TMP}
   COMMAND ${CMAKE_COMMAND} -E echo_append " on branch " >> ${GIT_REVISION_TMP}
   COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD  | tr -d '\\n' >> ${GIT_REVISION_TMP}
   COMMAND ${CMAKE_COMMAND} -E echo \\\" >> ${GIT_REVISION_TMP}
   COMMAND ${CMAKE_COMMAND} -E copy_if_different ${GIT_REVISION_TMP} ${GIT_REVISION}
   COMMAND ${CMAKE_COMMAND} -E remove ${GIT_REVISION_TMP}
   WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}/include/
)

add_dependencies(objlib git_revision_h)

#####################################################################
# sub-projects
#####################################################################
add_subdirectory(progs)
add_subdirectory(examples)

configure_file(
    ${PROJECT_SOURCE_DIR}/MidasConfig.cmake.in
    ${PROJECT_SOURCE_DIR}/MidasConfig.cmake)
