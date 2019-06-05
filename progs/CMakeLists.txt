#
# Standard targets requiring only midas library
#
set(PROGS
   mserver
   mlogger
   msequencer
   lazylogger
   mtransition
   odbinit
   mdump
   mhist
   mstat
   mhdump
   mchart
   odbhist
   melog
   mh2sql
)

foreach(PROG ${PROGS})
   add_executable(${PROG} ${PROG})
   target_include_directories(${PROG} PRIVATE ${DRV_DIR} ${MXML_DIR} ${MSCB_DIR}/include)
   target_include_directories(${PROG} PRIVATE ${PROJECT_SOURCE_DIR}/drivers)
   target_link_libraries(${PROG} midas ${LIBS})
endforeach()

#
# Build C files with C++
#

file(GLOB_RECURSE CFILES "${CMAKE_SOURCE_DIR}/*.c")
SET_SOURCE_FILES_PROPERTIES(${CFILES} PROPERTIES LANGUAGE CXX)

#
# non-standard executables using additional source files
#
if (HAVE_MSCB)
   add_executable(mhttpd mhttpd.cxx mongoose6.c mgd.c ${MSCB_DIR}/src/mscb.c)
   target_compile_options(mhttpd PRIVATE -DHAVE_MSCB)
   target_include_directories(mhttpd PRIVATE ${MSCB_DIR}/include)
else(HAVE_MSCB)
   add_executable(mhttpd mhttpd.cxx mongoose6.c mgd.c)
endif(HAVE_MSCB)
target_include_directories(mhttpd PRIVATE ${DRV_DIR} ${MXML_DIR})
target_include_directories(mhttpd PRIVATE ${OPENSSL_INCLUDE_DIR})

# mongoose flags
target_compile_options(mhttpd PRIVATE -DHAVE_MONGOOSE6 -DMG_ENABLE_THREADS -DMG_DISABLE_CGI)
if (OPENSSL_FOUND)
   target_compile_options(mhttpd PRIVATE -DMG_ENABLE_SSL)
endif(OPENSSL_FOUND)

target_link_libraries(mhttpd midas ${OPENSSL_LIBRARIES} ${LIBS})

add_executable(odbedit odbedit.cxx cmdedit.cxx)
target_include_directories(odbedit PRIVATE ${DRV_DIR} ${MXML_DIR})
target_link_libraries(odbedit midas ${LIBS})
target_include_directories(mlogger PUBLIC ${ROOT_INC})
target_link_libraries(mlogger ${ROOT_LIBS})

#
# Installation
#
install(TARGETS ${PROGS} mhttpd odbedit DESTINATION bin)
