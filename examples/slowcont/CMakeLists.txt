set(DRIVERS
    ${PROJECT_SOURCE_DIR}/drivers/class/hv.c
    ${PROJECT_SOURCE_DIR}/drivers/class/multi.c
    ${PROJECT_SOURCE_DIR}/drivers/device/nulldev.c
    ${PROJECT_SOURCE_DIR}/drivers/bus/null.c
)

file(GLOB_RECURSE CFILES "${CMAKE_SOURCE_DIR}/*.c")
SET_SOURCE_FILES_PROPERTIES(${CFILES} PROPERTIES LANGUAGE CXX)
SET_SOURCE_FILES_PROPERTIES(${DRIVERS} PROPERTIES LANGUAGE CXX)

add_executable(scfe scfe.c ${DRIVERS})
target_include_directories(scfe PRIVATE ${DRV_DIR} ${MXML_DIR})
target_link_libraries(scfe midas mfe ${LIBS})

if (HAVE_MSCB)
add_executable(mscb_fe
        mscb_fe.c
        ${DRIVERS}
        ${PROJECT_SOURCE_DIR}/drivers/device/mscbdev.c
        ${MSCB_DIR}/src/mscb.c)

target_include_directories(mscb_fe PRIVATE ${DRV_DIR} ${MXML_DIR} ${MSCB_DIR}/include )
target_link_libraries(mscb_fe midas mfe ${LIBS})
endif(HAVE_MSCB)

