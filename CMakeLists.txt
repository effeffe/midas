#
# CMakeLists.txt for midas
#
# S.R. 28 May 2019
#

message(STATUS "MIDAS: cmake version: " ${CMAKE_VERSION})
cmake_minimum_required(VERSION 3.12)

project(midas
        DESCRIPTION "MIDAS Data Acquisition System"
        HOMEPAGE_URL "https://midas.triumf.ca")

#
# specify default build type if none is defined with ccmake
#
if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
   set(CMAKE_BUILD_TYPE RelWithDebInfo)
   message(STATUS "MIDAS: Setting build type to '${CMAKE_BUILD_TYPE}' as non was specified")
endif()
message(STATUS "MIDAS: Curent build type is '${CMAKE_BUILD_TYPE}'")

#
# option for address sanitizer, turn on via "ccmake .." in build directory
#
option(USE_ADDRESS_SANITIZER "Enable address sanitizer" FALSE)
if (USE_ADDRESS_SANITIZER)
   if (${CMAKE_SYSTEM_NAME} MATCHES Darwin)
      add_compile_options(-fsanitize=address)
      add_link_options(-fsanitize=address)
   else()
      add_compile_options(-fsanitize=address -fsanitize=leak -fsanitize=undefined)
      add_link_options(-fsanitize=address -fsanitize=leak -fsanitize=undefined)
   endif()

   message(STATUS "MIDAS: Enabled address sanitizer")
else()
   message(STATUS "MIDAS: Disabled address sanitizer")
endif()

#
# Directories
#

set(BUILD_INC_DIR include mxml mjson mvodb midasio)

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
   # in standalone mode, install under <midas>/bin
   set(CMAKE_INSTALL_PREFIX ${CMAKE_SOURCE_DIR} CACHE PATH "" FORCE)
else()
   # there is an user choice or a higher level project
   # specifying the install path - keep that
   set(CMAKE_INSTALL_PREFIX ${CMAKE_INSTALL_PREFIX})
endif()
message(STATUS "MIDAS: CMAKE_INSTALL_PREFIX: ${CMAKE_INSTALL_PREFIX}")

if (${CMAKE_INSTALL_PREFIX} STREQUAL ${PROJECT_SOURCE_DIR})
   set(MIDAS_NO_INSTALL_INCLUDE_FILES true)
else()
endif()

# NB: RPATH is not needed as everything is linked statically. K.O.
## Ensure the C-compat library can load the main library after install
#set(CMAKE_INSTALL_RPATH ${CMAKE_INSTALL_PREFIX}/lib)
#set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

#add_compile_options("-O0")
add_compile_options("-O2")
add_compile_options("-g")
##add_compile_options("-std=c++11")

#add_compile_options("-Wnonnull")
#add_compile_options("-Walloca")

#add_compile_options(-fsanitize=address -fsanitize=leak)
#add_link_options(-fsanitize=address -fsanitize=leak)

add_compile_options(-fsanitize=thread)
add_link_options(-fsanitize=thread)

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
   list(APPEND XDEFINES -DNO_LOCAL_ROUTINES=1)
endif()

#
# Optional ROOT support
#
option(NO_ROOT "Disable ROOT support" FALSE)
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
      find_package(ROOT REQUIRED COMPONENTS
          Gui # TG* objects
          RHTTP # THttpServer
          XMLIO XMLParser
          MathCore RIO Hist Tree Net
          )

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
      message(STATUS "MIDAS: ROOT not found: ROOTSYS is not defined")
      unset(HAVE_ROOT)
   endif()

endif(NO_ROOT)

find_package(ZLIB REQUIRED)
if (ZLIB_FOUND)
   message(STATUS "MIDAS: Found ZLIB version " ${ZLIB_VERSION_STRING})
   list(APPEND XLIBS ${ZLIB_LIBRARIES})
endif(ZLIB_FOUND)

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
         list(APPEND XDEFINES -DHAVE_CURL)
         list(APPEND XLIBS -lcurl)
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
      list(APPEND XDEFINES -DHAVE_MYSQL)
      list(APPEND XFLAGS ${MYSQL_CFLAGS})
      list(APPEND XLIBS ${MYSQL_LIBS})
      message(STATUS "MIDAS: MySQL CFLAGS: ${MYSQL_CFLAGS} and libs: ${MYSQL_LIBS}")
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
   list(APPEND XDEFINES -DHAVE_ODBC)
   list(APPEND XLIBS -lodbc)
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
      list(APPEND XDEFINES -DHAVE_SQLITE)
      list(APPEND XLIBS ${SQLite3_LIBRARIES})
   else ()
      message(STATUS "MIDAS: SQLITE not found")
   endif()
endif (NO_SQLITE)

#
# FTP support for mdsupport
#
list(APPEND XDEFINES -DHAVE_FTPLIB)

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
  find_library(libsensors sensors) #Requires libsensors-dev
  if(libsensors STREQUAL "libsensors-NOTFOUND")
    message(STATUS "MIDAS (msysmon): LM_SENSORS libs not found")
  else()
    message(STATUS "MIDAS (msysmon): Found LM_SENSORS ")
    set(HAVE_LM_SENSORS TRUE)
  endif()
endif(NO_SENSORS)

#
# Option OpenCV support (used in mlogger for rstp cameras)
#

if (NOT NO_OPENCV)
   set(NO_OPENCV 0 CACHE PATH "")
endif()
if (NO_OPENCV)
  message(STATUS "MIDAS (mlogger): OpenCVG disabled (no rtsp camera support)")
else()
  find_package(OpenCV QUIET)
  if (OpenCV_FOUND)
    if (CURL_FOUND)
       message(STATUS "MIDAS (mlogger): Found OpenCV version " ${OpenCV_VERSION})
       list(APPEND XDEFINES -DHAVE_OPENCV)
       list(APPEND XLIBS ${OpenCV_LIBS})
    else()
       message(STATUS "MIDAS (mlogger): Found OpenCV but CURL missing")
    endif(CURL_FOUND)
    set(HAVE_OPENCV TRUE)
  else()
    message(STATUS "MIDAS (mlogger): OpenCV not found, (no rtsp camera support))")
  endif()
endif(NO_OPENCV)

#
# Find pthreads
#

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
#target_link_libraries(my_app PRIVATE Threads::Threads)
#list(APPEND XLIBS Threads::Threads)
list(APPEND XLIBS ${CMAKE_THREAD_LIBS_INIT})

#
# Tell subprojects that we have the "midas" target already defined
#

set(MIDAS_FOUND TRUE)

#-----------------------
# Default build type
#

#string(REPLACE "-DNDEBUG" "" CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO}") # midas requires working assert()
#
#if (NOT CMAKE_BUILD_TYPE OR CMAKE_BUILD_TYPE STREQUAL "")
#    message(STATUS "MIDAS: Setting default build type to \"RelWithDebInfo\", compiler flags: " ${CMAKE_CXX_FLAGS_RELWITHDEBINFO})
#    set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "" FORCE)
#endif()

#-----------------------
# Compiler warning flags
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
   list(APPEND XDEFINES -D_LARGEFILE64_SOURCE) # >2GB file support
   list(APPEND XLIBS -lutil) # forkpty()
   list(APPEND XLIBS -lrt)   # shm_unlink()
endif()

list(APPEND XLIBS ${CMAKE_DL_LIBS})

#####################################################################
# end of conditional code
#####################################################################

#
# Midas Library
#
set(libsrc
   src/midas.cxx
   src/odb.cxx
   src/odbxx.cxx
   src/device_driver.cxx
   src/system.cxx
   src/alarm.cxx
   src/elog.cxx
   src/mrpc.cxx
   mjson/mjson.cxx
   src/tmfe.cxx
   mvodb/mvodb.cxx
   mvodb/nullodb.cxx
   mvodb/midasodb.cxx
   mvodb/mxmlodb.cxx
   mvodb/mjsonodb.cxx
   midasio/midasio.cxx
   src/json_paste.cxx
   src/mjsonrpc.cxx
   src/mjsonrpc_user.cxx
   src/history.cxx
   src/history_common.cxx
   src/history_odbc.cxx
   src/history_schema.cxx
   src/history_image.cxx
   midasio/lz4.cxx
   midasio/lz4frame.cxx
   midasio/lz4hc.cxx
   midasio/xxhash.cxx
   src/crc32c.cxx
   src/sha256.cxx
   src/sha512.cxx
   src/ftplib.cxx
   src/mdsupport.cxx
   mxml/mxml.cxx
   mxml/strlcpy.cxx
   ${GIT_REVISION}
   )

set(libsrc-c-compat
   src/midas_c_compat.cxx
   )

# midas library shared sources for libmidas.a and libmidas-shared.so
add_library(objlib OBJECT ${libsrc})
target_compile_features(objlib PUBLIC cxx_std_11)
target_include_directories(objlib PUBLIC $<BUILD_INTERFACE:${BUILD_INC_DIR}>)
target_compile_definitions(objlib PUBLIC ${XDEFINES})
target_compile_options(objlib PUBLIC ${XFLAGS})
set_property(TARGET objlib PROPERTY POSITION_INDEPENDENT_CODE 1)

# c-compatibility library for python
add_library(objlib-c-compat OBJECT ${libsrc-c-compat})
target_compile_features(objlib-c-compat PUBLIC cxx_std_11)
target_include_directories(objlib-c-compat PUBLIC $<BUILD_INTERFACE:${BUILD_INC_DIR}>)
target_compile_definitions(objlib-c-compat PUBLIC ${XDEFINES})
target_compile_options(objlib-c-compat PUBLIC ${XFLAGS})
set_property(TARGET objlib-c-compat PROPERTY POSITION_INDEPENDENT_CODE 1)

# static midas library
add_library(midas STATIC $<TARGET_OBJECTS:objlib>)
target_compile_features(midas PUBLIC cxx_std_11)
target_compile_definitions(midas PUBLIC ${XDEFINES})
target_compile_options(midas PUBLIC ${XFLAGS})
target_link_libraries(midas ${XLIBS})

# shared midas library
add_library(midas-shared SHARED $<TARGET_OBJECTS:objlib>)
target_compile_features(midas-shared PUBLIC cxx_std_11)
target_compile_definitions(midas-shared PUBLIC ${XDEFINES})
target_compile_options(midas-shared PUBLIC ${XFLAGS})
target_link_libraries(midas-shared ${XLIBS})

# shared midas library with C compatible signatures for python
add_library(midas-c-compat SHARED $<TARGET_OBJECTS:objlib-c-compat>)
target_compile_features(midas-c-compat PUBLIC cxx_std_11)
target_link_libraries(midas-c-compat midas)

# library for mfe
add_library(mfe src/mfe)
target_compile_features(mfe PUBLIC cxx_std_11)
target_include_directories(mfe PUBLIC $<BUILD_INTERFACE:${BUILD_INC_DIR}>)

# library for mana
add_library(mana src/mana)
target_compile_features(mana PUBLIC cxx_std_11)
target_include_directories(mana PUBLIC $<BUILD_INTERFACE:${BUILD_INC_DIR}>)

# library for rmana
add_library(rmana src/mana)
target_compile_features(rmana PUBLIC cxx_std_11)
target_include_directories(rmana PUBLIC $<BUILD_INTERFACE:${BUILD_INC_DIR}>)
if (HAVE_ROOT)
   target_include_directories(rmana PUBLIC ${ROOT_INCLUDE_DIRS})
   target_compile_options(rmana PRIVATE -DHAVE_ROOT ${ROOT_CXX_FLAGS})
endif(HAVE_ROOT)

# library for mfe.o
add_library(mfeo OBJECT src/mfe.cxx)
target_compile_features(mfeo PUBLIC cxx_std_11)
target_include_directories(mfeo PUBLIC $<BUILD_INTERFACE:${BUILD_INC_DIR}>)

# this is somewhat dirty, as the path to the object dir is hardcoded and
# might change in future CMake versions. The only other option however
# would be to pack and then unpack again a library (NB, 12.6.2019)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/mfeo.dir/src/mfe.cxx.o DESTINATION lib RENAME mfe.o)

# library for mana.o
add_library(manao OBJECT src/mana.cxx)
target_compile_features(manao PUBLIC cxx_std_11)
target_include_directories(manao PUBLIC $<BUILD_INTERFACE:${BUILD_INC_DIR}>)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/manao.dir/src/mana.cxx.o DESTINATION lib RENAME mana.o)

# library for rmana.o
add_library(rmanao OBJECT src/mana.cxx)
target_compile_features(rmanao PUBLIC cxx_std_11)
target_include_directories(rmanao PUBLIC $<BUILD_INTERFACE:${BUILD_INC_DIR}>)
if (HAVE_ROOT)
   target_include_directories(rmanao PUBLIC ${ROOT_INCLUDE_DIRS})
   target_compile_options(rmanao PRIVATE -DHAVE_ROOT ${ROOT_CXX_FLAGS})
endif(HAVE_ROOT)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/rmanao.dir/src/mana.cxx.o DESTINATION lib RENAME rmana.o)

if (MIDAS_NO_INSTALL_INCLUDE_FILES)
   target_include_directories(midas          PUBLIC INTERFACE include mxml mjson mvodb midasio)
   target_include_directories(midas-shared   PUBLIC INTERFACE include mxml mjson mvodb midasio)
   target_include_directories(midas-c-compat PUBLIC INTERFACE include mxml mjson mvodb midasio)
   target_include_directories(mfe            PUBLIC INTERFACE include mxml mjson mvodb midasio)
   target_include_directories(mana           PUBLIC INTERFACE include mxml mjson mvodb midasio)
   target_include_directories(rmana          PUBLIC INTERFACE include mxml mjson mvodb midasio)
else()
   target_include_directories(midas          PUBLIC $<INSTALL_INTERFACE:include>)
   target_include_directories(midas-shared   PUBLIC $<INSTALL_INTERFACE:include>)
   target_include_directories(midas-c-compat PUBLIC $<INSTALL_INTERFACE:include>)
   target_include_directories(mfe            PUBLIC $<INSTALL_INTERFACE:include>)
   target_include_directories(mana           PUBLIC $<INSTALL_INTERFACE:include>)
   target_include_directories(rmana          PUBLIC $<INSTALL_INTERFACE:include>)

   install(FILES
      include/cfortran.h
      include/crc32c.h
      include/git-revision.h
      include/ftplib.h
      include/hardware.h
      include/hbook.h
      include/history.h
      include/mcstd.h
      include/mdsupport.h
      include/mexcept.h
      include/mfbstd.h
      include/mfe.h
      include/midas_c_compat.h
      include/midas.h
      include/midasinc.h
      include/midas_macro.h
      include/mleak.h
      include/mrpc.h
      include/msystem.h
      include/musbstd.h
      include/mvmestd.h
      include/odbxx.h
      include/tmfe.h
      include/tmfe_rev0.h
      mxml/mxml.h
      mxml/strlcpy.h
      mjson/mjson.h
      mvodb/mvodb.h
      midasio/midasio.h
      DESTINATION include)
endif()

install(TARGETS midas midas-shared midas-c-compat mfe mana rmana
        DESTINATION lib
        EXPORT ${PROJECT_NAME}-targets)
        
install(EXPORT ${PROJECT_NAME}-targets DESTINATION lib)

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
# sub-modules
#####################################################################
add_subdirectory(mscb)
add_subdirectory(manalyzer)

#####################################################################
# sub-projects
#####################################################################
add_subdirectory(progs)
add_subdirectory(examples)

#####################################################################
# generator for find_package(MIDAS)
#####################################################################

if (MIDAS_NO_INSTALL_INCLUDE_FILES)
list(APPEND CONFIG_MIDAS_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/include)
list(APPEND CONFIG_MIDAS_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/mxml)
list(APPEND CONFIG_MIDAS_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/mjson)
list(APPEND CONFIG_MIDAS_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/mvodb)
list(APPEND CONFIG_MIDAS_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/midasio)
list(APPEND CONFIG_MIDAS_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/manalyzer)
else()
list(APPEND CONFIG_MIDAS_INCLUDE_DIRS ${CMAKE_INSTALL_PREFIX}/include)
endif()
list(APPEND CONFIG_MIDAS_LIBRARY_DIRS ${CMAKE_INSTALL_PREFIX}/lib)
list(APPEND CONFIG_MIDAS_LIBRARIES1 -lmidas)
get_property(CONFIG_MIDAS_LIBRARIES2 TARGET midas PROPERTY INTERFACE_LINK_LIBRARIES)

configure_file(
    ${PROJECT_SOURCE_DIR}/MidasConfig.cmake.in
    ${PROJECT_SOURCE_DIR}/MidasConfig.cmake)

#####################################################################
# end
#####################################################################
