include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${})

add_executable(frontend ${CMAKE_SOURCE_DIR}/src/mfe.c frontend.cxx)
target_link_libraries(frontend midas)

add_executable(analyzer ${CMAKE_SOURCE_DIR}/src/mana.cxx
   analyzer.cxx adccalib.cxx adcsum.cxx scaler.cxx)

target_link_libraries(analyzer midas ${ROOT_LIBS})
