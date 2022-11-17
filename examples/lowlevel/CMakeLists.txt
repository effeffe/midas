set(PROGS produce consume rpc_test rpc_clnt rpc_srvr)

foreach(PROG ${PROGS})
   add_executable(${PROG} ${PROG}.cxx)
   target_link_libraries(${PROG} midas)
endforeach()
