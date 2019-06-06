set(DRIVERS
    ${PROJECT_SOURCE_DIR}/drivers/class/hv
    ${PROJECT_SOURCE_DIR}/drivers/class/multi
    ${PROJECT_SOURCE_DIR}/drivers/device/nulldev
    ${PROJECT_SOURCE_DIR}/drivers/bus/null
)

add_executable(scfe scfe ${DRIVERS})
target_include_directories(scfe PRIVATE ${DRV_DIR} ${MXML_DIR})
target_link_libraries(scfe midas mfe ${LIBS})

if (HAVE_MSCB)
add_executable(mscb_fe
        mscb_fe
        ${DRIVERS}
        ${PROJECT_SOURCE_DIR}/drivers/device/mscbdev
        ${MSCB_DIR}/src/mscb)

target_include_directories(mscb_fe PRIVATE ${DRV_DIR} ${MXML_DIR} ${MSCB_DIR}/include )
target_link_libraries(mscb_fe midas mfe ${LIBS})
endif(HAVE_MSCB)

