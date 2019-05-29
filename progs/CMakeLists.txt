#
# Standard targets requiring only midas library
#
set(PROGS
   mserver
   mlogger
   msequencer
   lazylogger
   mtransition
)

foreach(PROG ${PROGS})
   add_executable(${PROG} ${PROG})
   target_link_libraries(${PROG} midas ${LIBS})
endforeach()


#
# non-standard executables using additional source files
#
if (HAVE_MSCB)
add_executable(mhttpd mhttpd.cxx mongoose6.c mgd.c ${MSCB_DIR}/src/mscb.c)
else(HAVE_MSCB)
add_executable(mhttpd mhttpd.cxx mongoose6.c mgd.c)
endif(HAVE_MSCB)

target_link_libraries(mhttpd midas ${OPENSSL_LIBRARIES} ${LIBS})

add_executable(odbedit odbedit.cxx cmdedit.cxx)
target_link_libraries(odbedit midas ${LIBS})

target_link_libraries(mlogger ${ROOT_LIBS})

#
# Installation
#
install(TARGETS ${PROGS} mhttpd odbedit mlogger DESTINATION bin)
