add_executable(mtfe ${CMAKE_SOURCE_DIR}/src/mfe.c mtfe)
target_link_libraries(mtfe midas ${LIBS})

file(GLOB_RECURSE CFILES "${CMAKE_SOURCE_DIR}/*.c")
SET_SOURCE_FILES_PROPERTIES(${CFILES} PROPERTIES LANGUAGE CXX)
