add_executable(mtfe ${CMAKE_SOURCE_DIR}/src/mfe.c mtfe.c)
target_link_libraries(mtfe midas ${LIBS})
