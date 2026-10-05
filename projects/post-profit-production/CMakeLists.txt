# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.25)
project(post_profit_production VERSION 0.1.0 LANGUAGES CXX)
include(CTest)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/configs/research.cfg" PH_PRODUCTION_DEFAULTS)
string(FIND "${PH_PRODUCTION_DEFAULTS}" ")PHPCFG\"" PH_PRODUCTION_DELIMITER)
if(NOT PH_PRODUCTION_DELIMITER EQUAL -1)
  message(FATAL_ERROR "Configuration contains reserved C++ embedding delimiter")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS configs/research.cfg)
configure_file(simulation/default_config.hpp.in generated/default_config.hpp @ONLY)

add_library(ph_production_simulation simulation/simulation.cpp)
add_library(ph::production_simulation ALIAS ph_production_simulation)
target_compile_features(ph_production_simulation PUBLIC cxx_std_17)
set_target_properties(ph_production_simulation PROPERTIES CXX_EXTENSIONS OFF)
target_include_directories(ph_production_simulation PUBLIC simulation)
# This is the existing unmodified MIT JSON header, not the pricing engine.
target_include_directories(ph_production_simulation SYSTEM PUBLIC ../../tools/price-optimization/third_party)
target_include_directories(ph_production_simulation PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
if(MSVC)
  target_compile_options(ph_production_simulation PRIVATE /W4)
else()
  target_compile_options(ph_production_simulation PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(production_simulation simulation/main.cpp)
target_link_libraries(production_simulation PRIVATE ph::production_simulation)

add_library(ph_production_experiments experiments/experiment.cpp)
add_library(ph::production_experiments ALIAS ph_production_experiments)
target_link_libraries(ph_production_experiments PUBLIC ph::production_simulation)
target_include_directories(ph_production_experiments PUBLIC experiments)
set_target_properties(ph_production_experiments PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
  target_compile_options(ph_production_experiments PRIVATE /W4)
else()
  target_compile_options(ph_production_experiments PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_executable(production_experiment experiments/main.cpp)
target_link_libraries(production_experiment PRIVATE ph::production_experiments)
if(BUILD_TESTING)
  add_executable(production_simulation_test simulation/simulation_test.cpp)
  target_link_libraries(production_simulation_test PRIVATE ph::production_simulation)
  add_test(NAME production_simulation COMMAND production_simulation_test)
  set_tests_properties(production_simulation PROPERTIES TIMEOUT 30)
  add_test(NAME production_cli COMMAND ${CMAKE_COMMAND}
    -DPROGRAM=$<TARGET_FILE:production_simulation>
    -DCONFIG=${CMAKE_CURRENT_SOURCE_DIR}/configs/research.cfg
    -P ${CMAKE_CURRENT_SOURCE_DIR}/simulation/cli_test.cmake)
  add_executable(production_experiment_test experiments/experiment_test.cpp)
  target_link_libraries(production_experiment_test PRIVATE ph::production_experiments)
  target_compile_definitions(production_experiment_test PRIVATE
    PH_SCHEDULING_PLAN_PATH="${CMAKE_CURRENT_SOURCE_DIR}/configs/scheduling-comparison.cfg")
  add_test(NAME production_experiments COMMAND production_experiment_test)
  set_tests_properties(production_experiments PROPERTIES TIMEOUT 30)
  add_test(NAME production_experiment_cli COMMAND ${CMAKE_COMMAND}
    -DPROGRAM=$<TARGET_FILE:production_experiment>
    -DPLAN=${CMAKE_CURRENT_SOURCE_DIR}/configs/scheduling-comparison.cfg
    -P ${CMAKE_CURRENT_SOURCE_DIR}/experiments/cli_test.cmake)
endif()
