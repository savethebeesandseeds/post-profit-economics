# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.25)
project(ph_idydb VERSION 0.1.0 LANGUAGES C CXX)

# Build the preserved upstream snapshot; project additions stay outside upstream/.
add_subdirectory(upstream)
add_library(ph::idydb ALIAS idydb)
target_compile_features(idydb PUBLIC cxx_std_17)
set_target_properties(idydb PROPERTIES CXX_EXTENSIONS OFF)

include(CTest)
if(BUILD_TESTING)
  add_executable(idydb_roundtrip_test tests/roundtrip_test.cpp)
  target_link_libraries(idydb_roundtrip_test PRIVATE ph::idydb)
  target_compile_features(idydb_roundtrip_test PRIVATE cxx_std_17)
  set_target_properties(idydb_roundtrip_test PROPERTIES CXX_EXTENSIONS OFF)
  add_test(NAME idydb_roundtrip COMMAND idydb_roundtrip_test)
  set_tests_properties(idydb_roundtrip PROPERTIES LABELS smoke TIMEOUT 60)
endif()
