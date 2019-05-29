set(PROGS
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
   target_link_libraries(${PROG} midas ${LIBS})
endforeach()

install(TARGETS ${PROGS} DESTINATION bin)
