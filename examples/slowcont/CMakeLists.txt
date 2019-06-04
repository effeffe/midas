set(DRIVERS
   ../../drivers/class/hv.c
   ../../drivers/class/multi.c
   ../../drivers/device/nulldev.c
   ../../drivers/device/mscbdev.c
   ../../drivers/bus/null.c
)

# $(CMAKE_SOURCE_DIR)/../../drivers/class/hv.c

file(GLOB_RECURSE CFILES "${CMAKE_SOURCE_DIR}/*.c")
SET_SOURCE_FILES_PROPERTIES(${CFILES} PROPERTIES LANGUAGE CXX)
SET_SOURCE_FILES_PROPERTIES(${DRIVERS} PROPERTIES LANGUAGE CXX)

add_executable(scfe ${CMAKE_SOURCE_DIR}/src/mfe.c scfe.c ${DRIVERS} ${MSCB_DIR}/src/mscb.c)
target_link_libraries(scfe midas ${LIBS})

add_executable(mscb_fe ${CMAKE_SOURCE_DIR}/src/mfe.c mscb_fe.c ${DRIVERS} ${MSCB_DIR}/src/mscb.c)
target_link_libraries(mscb_fe midas ${LIBS})


