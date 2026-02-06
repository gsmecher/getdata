include_directories(${GD_DIR}/util)

add_executable(checkdirfile ${GD_DIR}/util/checkdirfile.c)
if(SKBUILD)
  # For wheel builds, statically link to avoid auditwheel complications with scripts
  target_link_libraries(checkdirfile getdata_static)
  install(TARGETS checkdirfile DESTINATION ${SKBUILD_SCRIPTS_DIR} COMPONENT Runtime)
else()
  target_link_libraries(checkdirfile getdata)
  install(TARGETS checkdirfile DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT Runtime)
endif()

if(HAVE_GETOPT_LONG)
  add_executable(dirfile2ascii ${GD_DIR}/util/dirfile2ascii.c)
  if(SKBUILD)
    # For wheel builds, statically link to avoid auditwheel complications with scripts
    target_link_libraries(dirfile2ascii getdata_static)
    install(TARGETS dirfile2ascii DESTINATION ${SKBUILD_SCRIPTS_DIR} COMPONENT Runtime)
  else()
    target_link_libraries(dirfile2ascii getdata)
    install(TARGETS dirfile2ascii DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT Runtime)
  endif()
endif()
