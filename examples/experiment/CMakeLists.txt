add_executable(frontend frontend)
target_include_directories(frontend PRIVATE ${DRV_DIR} ${PROJECT_SOURCE_DIR}/mxml)
target_link_libraries(frontend mfe midas ${LIBS})

if (HAVE_ROOT)
   add_executable(analyzer
      analyzer
      adccalib
      adcsum
      scaler)

   target_include_directories(analyzer PUBLIC ${ROOT_INC} ${PROJECT_SOURCE_DIR}/mxml)
   target_link_libraries(analyzer mana midas ${ROOT_LIBS} ${LIBS})
endif(HAVE_ROOT)
