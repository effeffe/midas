set(DRIVERS
    ${PROJECT_SOURCE_DIR}/drivers/class/hv
    ${PROJECT_SOURCE_DIR}/drivers/class/multi
    ${PROJECT_SOURCE_DIR}/drivers/device/nulldev
    ${PROJECT_SOURCE_DIR}/drivers/bus/null
)

add_executable(scfe scfe ${DRIVERS})
target_include_directories(scfe PRIVATE ${DRV_DIR})
target_link_libraries(scfe mfe midas ${LIBS})

add_executable(mscb_fe
        mscb_fe
        ${DRIVERS}
        ${PROJECT_SOURCE_DIR}/drivers/device/mscbdev
        ${PROJECT_SOURCE_DIR}/mscb/src/mscb)

target_include_directories(mscb_fe PRIVATE ${DRV_DIR} ${PROJECT_SOURCE_DIR}/mscb/include)
target_link_libraries(mscb_fe mfe midas ${LIBS})


