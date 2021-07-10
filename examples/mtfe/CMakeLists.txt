add_executable(mtfe mtfe)
target_include_directories(mtfe PRIVATE ${PROJECT_SOURCE_DIR}/drivers ${PROJECT_SOURCE_DIR}/mxml)
target_link_libraries(mtfe midas mfe ${LIBS})
