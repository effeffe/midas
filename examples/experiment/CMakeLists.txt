include_directories(${CMAKE_SOURCE_DIR}/include)

add_executable(frontend ${CMAKE_SOURCE_DIR}/src/mfe.c frontend.cxx)
target_link_libraries(frontend midas ${LIBS})

file(GLOB_RECURSE CFILES "${CMAKE_SOURCE_DIR}/*.c")
SET_SOURCE_FILES_PROPERTIES(${CFILES} PROPERTIES LANGUAGE CXX)

if (HAVE_ROOT)
   add_executable(analyzer ${CMAKE_SOURCE_DIR}/src/mana.cxx
      analyzer.cxx
      adccalib.cxx
      adcsum.cxx
      scaler.cxx)

   target_include_directories(analyzer PUBLIC ${ROOT_INC})
   target_link_libraries(analyzer midas ${ROOT_LIBS} ${LIBS})
endif(HAVE_ROOT)
