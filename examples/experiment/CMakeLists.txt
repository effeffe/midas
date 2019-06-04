add_executable(frontend ${PROJECT_SOURCE_DIR}/src/mfe.c frontend.cxx)
target_include_directories(frontend PRIVATE ${DRV_DIR} ${MXML_DIR})
target_link_libraries(frontend midas ${LIBS})

if (HAVE_ROOT)
   add_executable(analyzer ${PROJECT_SOURCE_DIR}/src/mana.cxx
      analyzer.cxx
      adccalib.cxx
      adcsum.cxx
      scaler.cxx)

   target_include_directories(analyzer PUBLIC ${ROOT_INC})
   target_link_libraries(analyzer midas ${ROOT_LIBS} ${LIBS})
endif(HAVE_ROOT)
