set(DRIVERS
   ${MIDASSYS}/drivers/class/hv.c
   ${MIDASSYS}/drivers/class/multi.c
   ${MIDASSYS}/drivers/device/nulldev.c
   ${MIDASSYS}/drivers/device/mscbdev.c
   ${MIDASSYS}/drivers/bus/null.c
)

add_executable(scfe ${CMAKE_SOURCE_DIR}/src/mfe.c scfe.c ${DRIVERS} ${MSCB_DIR}/src/mscb.c)
target_link_libraries(scfe midas)


