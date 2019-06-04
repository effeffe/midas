add_executable(mtfe ${PROJECT_SOURCE_DIR}/src/mfe.c mtfe)
target_include_directories(mtfe PRIVATE ${SYSINC_DIR})
target_include_directories(mtfe PRIVATE ${DRV_DIR} ${MXML_DIR})
target_link_libraries(mtfe midas ${LIBS})
