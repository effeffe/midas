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
   odbxx_test
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
#   set(CMAKE_CXX_FLAGS "-fsanitize=address")
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

target_compile_options(mhttpd PRIVATE -DHAVE_MONGOOSE616 -DMG_ENABLE_THREADS -DMG_DISABLE_CGI -DMG_ENABLE_EXTRA_ERRORS_DESC -DMG_ENABLE_IPV6 -DMG_ENABLE_HTTP_URL_REWRITES -DMG_ENABLE_HTTP_SSI=0)

if (MBEDTLS_FOUND)
   target_compile_options(mhttpd PRIVATE -DMG_ENABLE_SSL -DMG_SSL_IF=2 -DMG_SSL_MBED_DUMMY_RANDOM)
   target_link_libraries(mhttpd midas ${MBEDTLS_LIBS} ${LIBS} ${DB_LIBS})
elseif (OPENSSL_FOUND)
   target_compile_options(mhttpd PRIVATE -DMG_ENABLE_SSL -DMG_SSL_IF=1)
   target_include_directories(mhttpd PRIVATE ${OPENSSL_INCLUDE_DIR})
   target_link_libraries(mhttpd midas ${OPENSSL_LIBRARIES} ${LIBS} ${DB_LIBS})
else ()
   target_link_libraries(mhttpd midas ${LIBS} ${DB_LIBS})
endif(MBEDTLS_FOUND)


#
# mhttpd6 (old mongoose)
#
add_executable(mhttpd6 mhttpd mongoose6 mgd ${PROJECT_SOURCE_DIR}/mscb/src/mscb)
target_compile_options(mhttpd6 PRIVATE -DHAVE_MSCB)
target_include_directories(mhttpd6 PRIVATE ${PROJECT_SOURCE_DIR}/mscb/include)
target_include_directories(mhttpd6 PRIVATE ${DRV_DIR} ${PROJECT_SOURCE_DIR}/mxml)
target_include_directories(mhttpd6 PRIVATE ${OPENSSL_INCLUDE_DIR})
target_link_libraries(mhttpd6 ${DB_LIBS})

# mongoose flags
target_compile_options(mhttpd6 PRIVATE -DHAVE_MONGOOSE6 -DMG_ENABLE_THREADS -DMG_DISABLE_CGI -DMG_ENABLE_EXTRA_ERRORS_DESC)
if (OPENSSL_FOUND)
   target_compile_options(mhttpd6 PRIVATE -DMG_ENABLE_SSL)
   target_link_libraries(mhttpd6 ${OPENSSL_LIBRARIES})
endif(OPENSSL_FOUND)

target_link_libraries(mhttpd6 midas ${LIBS} ${DB_LIBS})

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
   target_include_directories(rmlogger PUBLIC ${ROOT_INCLUDE_DIRS})
   target_compile_options(rmlogger PRIVATE -DHAVE_ROOT ${ROOT_CXX_FLAGS})
   target_link_libraries(rmlogger midas ${LIBS} ${ROOT_LIBRARIES} ${DB_LIBS})
   set_target_properties(rmlogger PROPERTIES LINK_FLAGS "-Wl,-rpath,${ROOT_LIBRARY_DIR}")
endif(HAVE_ROOT)

#
# Installation
#
install(TARGETS ${PROGS} ${MFEPROGS} mhttpd mhttpd6 odbedit DESTINATION bin)
if (HAVE_NVIDIA)
   install(TARGETS msysmon-nvidia DESTINATION bin)
endif(HAVE_NVIDIA)

if (HAVE_ROOT)
   install(TARGETS rmlogger DESTINATION bin)
endif(HAVE_ROOT)

#end
