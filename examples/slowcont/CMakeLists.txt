set(DRIVERS
    ${PROJECT_SOURCE_DIR}/drivers/class/hv.cxx
    ${PROJECT_SOURCE_DIR}/drivers/class/multi.cxx
    ${PROJECT_SOURCE_DIR}/drivers/device/nulldev.cxx
    ${PROJECT_SOURCE_DIR}/drivers/bus/null.cxx
    ${PROJECT_SOURCE_DIR}/drivers/device/mscbdev.cxx
    ${PROJECT_SOURCE_DIR}/mscb/src/mscb.cxx
)

add_executable(scfe scfe.cxx ${DRIVERS})
target_include_directories(scfe PRIVATE ${PROJECT_SOURCE_DIR}/drivers ${PROJECT_SOURCE_DIR}/mscb/include)
target_link_libraries(scfe mfe midas)

add_executable(mscb_fe mscb_fe.cxx ${DRIVERS})
target_include_directories(mscb_fe PRIVATE ${PROJECT_SOURCE_DIR}/drivers ${PROJECT_SOURCE_DIR}/mscb/include)
target_link_libraries(mscb_fe mfe midas)
