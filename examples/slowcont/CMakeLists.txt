set(DRIVERS
   ${MIDASSYS}/drivers/class/hv.c
   ${MIDASSYS}/drivers/class/multi.c
   ${MIDASSYS}/drivers/device/nulldev.c
   ${MIDASSYS}/drivers/device/mscbdev.c
   ${MIDASSYS}/drivers/bus/null.c
)

file(GLOB_RECURSE CFILES "${CMAKE_SOURCE_DIR}/*.c")
SET_SOURCE_FILES_PROPERTIES(${CFILES} PROPERTIES LANGUAGE CXX)

add_executable(scfe ${CMAKE_SOURCE_DIR}/src/mfe.c scfe.c ${DRIVERS} ${MSCB_DIR}/src/mscb.c)
target_link_libraries(scfe midas ${LIBS})

add_executable(mscb_fe ${CMAKE_SOURCE_DIR}/src/mfe.c mscb_fe.c ${DRIVERS} ${MSCB_DIR}/src/mscb.c)
target_link_libraries(mscb_fe midas ${LIBS})


