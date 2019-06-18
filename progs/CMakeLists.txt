#
# Standard targets requiring only midas library
#
set(PROGS
   mserver
   #odbedit
   odbinit
   #mhttpd
   mlogger
   msequencer
   mhist
   mstat
   mdump
   lazylogger
   mtransition
   mhdump
   mchart
   odbhist
   melog
   mh2sql
   fetest
   feudp
   #crc32c
   #mfe_link_test
   #mfe_link_test_cxx
   #mana_link_test
   #mjson_test
   #mcnaf
   #get_record_test
   #odb_lock_test
   #rmlogger
   #rmana_link_test
)

foreach(PROG ${PROGS})
   add_executable(${PROG} ${PROG})
   target_include_directories(${PROG} PRIVATE ${DRV_DIR} ${PROJECT_SOURCE_DIR}/mxml ${PROJECT_SOURCE_DIR}/mscb/include)
   target_include_directories(${PROG} PRIVATE ${PROJECT_SOURCE_DIR}/drivers)
   target_link_libraries(${PROG} midas ${LIBS})
endforeach()

#
# mhttpd
#
add_executable(mhttpd mhttpd mongoose6 mgd ${PROJECT_SOURCE_DIR}/mscb/src/mscb)
target_compile_options(mhttpd PRIVATE -DHAVE_MSCB)
target_include_directories(mhttpd PRIVATE ${PROJECT_SOURCE_DIR}/mscb/include)
target_include_directories(mhttpd PRIVATE ${DRV_DIR} ${PROJECT_SOURCE_DIR}/mxml)
target_include_directories(mhttpd PRIVATE ${OPENSSL_INCLUDE_DIR})

# mongoose flags
target_compile_options(mhttpd PRIVATE -DHAVE_MONGOOSE6 -DMG_ENABLE_THREADS -DMG_DISABLE_CGI)
if (OPENSSL_FOUND)
   target_compile_options(mhttpd PRIVATE -DMG_ENABLE_SSL)
endif(OPENSSL_FOUND)

target_link_libraries(mhttpd midas ${OPENSSL_LIBRARIES} ${LIBS})

#
# feudp, fetest
#

target_link_libraries(feudp midas mfe ${LIBS})
target_link_libraries(fetest midas mfe ${LIBS})

#
# odbedit
#
add_executable(odbedit odbedit cmdedit)
target_include_directories(odbedit PRIVATE ${PROJECT_SOURCE_DIR}/mxml)
target_link_libraries(odbedit midas ${LIBS})

#
# ensure mlogger is built without ROOT
#

remove_definitions(-DHAVE_ROOT)

#
# rmlogger
#
if (HAVE_ROOT)
   add_executable(rmlogger mlogger)
   target_include_directories(rmlogger PUBLIC ${ROOT_INC})
   target_link_libraries(rmlogger midas ${LIBS} ${ROOT_LIBS})
endif(HAVE_ROOT)

#
# Installation
#
install(TARGETS ${PROGS} mhttpd odbedit DESTINATION bin)

if (HAVE_ROOT)
   install(TARGETS rmlogger DESTINATION bin)
endif(HAVE_ROOT)