add_executable(mtfe mtfe)
target_include_directories(mtfe PRIVATE ${PROJECT_SOURCE_DIR}/drivers)
target_link_libraries(mtfe midas mfe)
