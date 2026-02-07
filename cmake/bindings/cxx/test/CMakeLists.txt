project(GetDataTests)

add_executable(cxx_test_big_test ${GD_DIR}/bindings/cxx/test/big_test.cpp)
if(WIN32)
  target_link_libraries(cxx_test_big_test ${GD_CXX} getdata_static)
else()
  target_link_libraries(cxx_test_big_test ${GD_CXX} getdata)
endif()
add_test(NAME cxx_test_big_test COMMAND cxx_test_big_test)
