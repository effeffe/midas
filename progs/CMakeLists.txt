#
# Standard targets requiring only midas library
#
set(PROGS
   mserver
   odbinit
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
   fetest_tmfe
   fetest_tmfe_thread
   crc32c_sum
)

set(TESTPROGS
   mjson_test
   get_record_test
   odb_lock_test
)

set(MFEPROGS
   mfe_link_test
   mfe_link_test_cxx
   fetest
   feudp
   msysmon
)

foreach(PROG ${PROGS} ${TESTPROGS})
   add_executable(${PROG} ${PROG})
   target_include_directories(${PROG} PRIVATE ${DRV_DIR} ${PROJECT_SOURCE_DIR}/mxml ${PROJECT_SOURCE_DIR}/mscb/include)
   target_include_directories(${PROG} PRIVATE ${PROJECT_SOURCE_DIR}/drivers)
   target_link_libraries(${PROG} midas ${LIBS} ${DB_LIBS})
endforeach()


foreach(PROG ${MFEPROGS})
   add_executable(${PROG} ${PROG})
   target_include_directories(${PROG} PRIVATE ${DRV_DIR} ${PROJECT_SOURCE_DIR}/mxml ${PROJECT_SOURCE_DIR}/mscb/include)
   target_include_directories(${PROG} PRIVATE ${PROJECT_SOURCE_DIR}/drivers)
   target_link_libraries(${PROG} mfe midas ${LIBS} ${DB_LIBS})
endforeach()

if (HAVE_NVIDIA)
   add_executable(msysmon-nvidia msysmon)
   target_include_directories(msysmon-nvidia PRIVATE ${DRV_DIR} ${PROJECT_SOURCE_DIR}/mxml ${PROJECT_SOURCE_DIR}/mscb/include ${NVIDIA_INC})
   target_include_directories(msysmon-nvidia PRIVATE ${PROJECT_SOURCE_DIR}/drivers)
   target_link_libraries(msysmon-nvidia mfe midas ${LIBS} ${DB_LIBS} ${NVIDIA_LIBS})
   target_compile_options(msysmon-nvidia PRIVATE -DHAVE_NVIDIA)
endif(HAVE_NVIDIA)

#
# mhttpd
#
add_executable(mhttpd mhttpd mongoose616 mgd ${PROJECT_SOURCE_DIR}/mscb/src/mscb)
target_compile_options(mhttpd PRIVATE -DHAVE_MSCB)
target_include_directories(mhttpd PRIVATE ${PROJECT_SOURCE_DIR}/mscb/include)
target_include_directories(mhttpd PRIVATE ${DRV_DIR} ${PROJECT_SOURCE_DIR}/mxml)
target_include_directories(mhttpd PRIVATE ${OPENSSL_INCLUDE_DIR})
target_link_libraries(mhttpd ${DB_LIBS})

# mongoose flags
target_compile_options(mhttpd PRIVATE -DHAVE_MONGOOSE616 -DMG_ENABLE_THREADS -DMG_DISABLE_CGI)
if (OPENSSL_FOUND)
   target_compile_options(mhttpd PRIVATE -DMG_ENABLE_SSL)
endif(OPENSSL_FOUND)

target_link_libraries(mhttpd midas ${OPENSSL_LIBRARIES} ${LIBS} ${DB_LIBS})

#
# odbedit
#
add_executable(odbedit odbedit cmdedit)
target_include_directories(odbedit PRIVATE ${PROJECT_SOURCE_DIR}/mxml)
target_link_libraries(odbedit midas ${LIBS})

#
# rmlogger
#
if (HAVE_ROOT)
   add_executable(rmlogger mlogger)
   target_include_directories(rmlogger PUBLIC ${ROOT_INC})
   target_compile_options(rmlogger PRIVATE -DHAVE_ROOT)
   target_link_libraries(rmlogger midas ${LIBS} ${ROOT_LIBS} ${DB_LIBS})
endif(HAVE_ROOT)

#
# Installation
#
install(TARGETS ${PROGS} ${MFEPROGS} mhttpd odbedit DESTINATION bin)
if (HAVE_NVIDIA)
   install(TARGETS msysmon-nvidia)
endif(HAVE_NVIDIA)

if (HAVE_ROOT)
   install(TARGETS rmlogger DESTINATION bin)
endif(HAVE_ROOT)

#end
