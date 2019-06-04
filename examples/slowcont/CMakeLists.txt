set(DRIVERS
    ${PROJECT_SOURCE_DIR}/drivers/class/hv.c
    ${PROJECT_SOURCE_DIR}/drivers/class/multi.c
    ${PROJECT_SOURCE_DIR}/drivers/device/nulldev.c
    ${PROJECT_SOURCE_DIR}/drivers/bus/null.c
)

add_executable(scfe ${PROJECT_SOURCE_DIR}/src/mfe.c scfe.c ${DRIVERS})
target_include_directories(scfe PRIVATE ${DRV_DIR} ${MXML_DIR})
target_link_libraries(scfe midas ${LIBS})

if (HAVE_MSCB)
add_executable(mscb_fe
        ${PROJECT_SOURCE_DIR}/src/mfe.c
        mscb_fe.c
        ${DRIVERS}
        ${PROJECT_SOURCE_DIR}/drivers/device/mscbdev.c
        ${MSCB_DIR}/src/mscb.c)

target_include_directories(mscb_fe PRIVATE ${DRV_DIR} ${MXML_DIR} ${MSCB_DIR}/include )
target_link_libraries(mscb_fe midas ${LIBS})
endif(HAVE_MSCB)

