# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.25)
project(post_profit_exchange_windows VERSION 0.3.0 LANGUAGES CXX)
if(NOT WIN32 OR NOT MINGW OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "Use the x86_64 MinGW toolchain from the managed Debian container")
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
include(CTest)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
get_filename_component(PH_EXCHANGE_PROJECT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
get_filename_component(PH_EXCHANGE_REPOSITORY "${PH_EXCHANGE_PROJECT}/../.." ABSOLUTE)
set(PH_EXCHANGE_CODE "${PH_EXCHANGE_PROJECT}/code")
set(PH_EXCHANGE_IDYDB_PACKAGE "" CACHE PATH "Supplied Windows IdyDB package; empty permits compile-only checks")
set(PH_EXCHANGE_JSON "${PH_EXCHANGE_REPOSITORY}/tools/price-optimization/third_party")
set(PH_EXCHANGE_HTTP "${PH_EXCHANGE_CODE}/third_party/cpp-httplib")
file(SHA256 "${PH_EXCHANGE_HTTP}/httplib.h" PH_EXCHANGE_HTTP_SHA256)
if(NOT PH_EXCHANGE_HTTP_SHA256 STREQUAL "bc77114769db3b3c97e4a6d753dab797c3cadaaa48beead3806298b09702df18")
  message(FATAL_ERROR "cpp-httplib differs from the pinned v0.61.0 release")
endif()

# This target contains only exchange-owned files. No database source, mock or
# alternate persistence backend is ever compiled by this project.
add_library(exchange_windows_core OBJECT
  "${PH_EXCHANGE_CODE}/operations/opening.cpp"
  "${PH_EXCHANGE_CODE}/operations/transactions.cpp"
  "${PH_EXCHANGE_CODE}/interfaces/environment.cpp"
  "${PH_EXCHANGE_CODE}/platform/files.cpp"
  "${PH_EXCHANGE_CODE}/platform/windows_files.cpp"
  "${PH_EXCHANGE_CODE}/storage/environment_store_windows.cpp")
target_include_directories(exchange_windows_core PUBLIC "${PH_EXCHANGE_CODE}")
target_include_directories(exchange_windows_core SYSTEM PUBLIC "${PH_EXCHANGE_JSON}")
target_compile_definitions(exchange_windows_core PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
target_compile_options(exchange_windows_core PRIVATE -Wall -Wextra -Wpedantic)

add_library(exchange_windows_server_check OBJECT
  "${PH_EXCHANGE_CODE}/interfaces/server_main.cpp"
  "${PH_EXCHANGE_CODE}/interfaces/server_platform.cpp")
target_include_directories(exchange_windows_server_check SYSTEM PRIVATE "${PH_EXCHANGE_HTTP}")
target_link_libraries(exchange_windows_server_check PRIVATE exchange_windows_core)
target_compile_options(exchange_windows_server_check PRIVATE -Wall -Wextra -Wpedantic)
add_library(exchange_windows_cli_check OBJECT "${PH_EXCHANGE_CODE}/interfaces/environment_main.cpp")
target_link_libraries(exchange_windows_cli_check PRIVATE exchange_windows_core)
add_library(exchange_windows_database_test_check OBJECT
  "${PH_EXCHANGE_CODE}/tests/windows_environment_store_test.cpp")
target_link_libraries(exchange_windows_database_test_check PRIVATE exchange_windows_core)
add_custom_target(exchange_windows_compile_check ALL DEPENDS
  exchange_windows_core exchange_windows_server_check exchange_windows_cli_check exchange_windows_database_test_check)

# Objects above are useful before the library arrives. A runnable application
# target is created only after every supplied dependency file and contract pass.
if(PH_EXCHANGE_IDYDB_PACKAGE)
  execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/dependency.py"
    "${PH_EXCHANGE_IDYDB_PACKAGE}" "${CMAKE_CURRENT_BINARY_DIR}/idydb-verified.json"
    RESULT_VARIABLE PH_EXCHANGE_PACKAGE_RESULT
    OUTPUT_VARIABLE PH_EXCHANGE_PACKAGE_OUTPUT ERROR_VARIABLE PH_EXCHANGE_PACKAGE_ERROR)
  if(NOT PH_EXCHANGE_PACKAGE_RESULT EQUAL 0)
    message(FATAL_ERROR "Supplied IdyDB package rejected: ${PH_EXCHANGE_PACKAGE_ERROR}")
  endif()
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PH_EXCHANGE_IDYDB_PACKAGE}/exchange-idydb-windows.json")
  file(READ "${CMAKE_CURRENT_BINARY_DIR}/idydb-verified.json" PH_EXCHANGE_DEPENDENCY)
  string(JSON PH_IDYDB_HEADER GET "${PH_EXCHANGE_DEPENDENCY}" header absolute_path)
  get_filename_component(PH_IDYDB_INCLUDE "${PH_IDYDB_HEADER}" DIRECTORY)
  string(JSON PH_IDYDB_LIBRARY GET "${PH_EXCHANGE_DEPENDENCY}" library absolute_path)
  string(JSON PH_IDYDB_KIND GET "${PH_EXCHANGE_DEPENDENCY}" library kind)
  add_library(exchange_supplied_idydb UNKNOWN IMPORTED)
  set_target_properties(exchange_supplied_idydb PROPERTIES
    IMPORTED_LOCATION "${PH_IDYDB_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${PH_IDYDB_INCLUDE}"
    INTERFACE_COMPILE_DEFINITIONS "IDYDB_SIZING_MODE=3")
  if(PH_IDYDB_KIND STREQUAL "import")
    set_property(TARGET exchange_supplied_idydb APPEND PROPERTY INTERFACE_COMPILE_DEFINITIONS IDYDB_USE_DLL)
  endif()
  string(JSON PH_IDYDB_SUPPORT_COUNT LENGTH "${PH_EXCHANGE_DEPENDENCY}" link_libraries)
  if(PH_IDYDB_SUPPORT_COUNT GREATER 0)
    math(EXPR PH_IDYDB_SUPPORT_LAST "${PH_IDYDB_SUPPORT_COUNT} - 1")
    foreach(index RANGE 0 ${PH_IDYDB_SUPPORT_LAST})
      string(JSON dependency GET "${PH_EXCHANGE_DEPENDENCY}" link_libraries ${index} absolute_path)
      set_property(TARGET exchange_supplied_idydb APPEND PROPERTY INTERFACE_LINK_LIBRARIES "${dependency}")
    endforeach()
  endif()
  string(JSON PH_IDYDB_SYSTEM_COUNT LENGTH "${PH_EXCHANGE_DEPENDENCY}" system_libraries)
  if(PH_IDYDB_SYSTEM_COUNT GREATER 0)
    math(EXPR PH_IDYDB_SYSTEM_LAST "${PH_IDYDB_SYSTEM_COUNT} - 1")
    foreach(index RANGE 0 ${PH_IDYDB_SYSTEM_LAST})
      string(JSON dependency GET "${PH_EXCHANGE_DEPENDENCY}" system_libraries ${index})
      set_property(TARGET exchange_supplied_idydb APPEND PROPERTY INTERFACE_LINK_LIBRARIES "${dependency}")
    endforeach()
  endif()
  target_link_libraries(exchange_windows_core PUBLIC exchange_supplied_idydb)
  add_custom_target(exchange_verify_idydb
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/dependency.py"
      "${PH_EXCHANGE_IDYDB_PACKAGE}" "${CMAKE_CURRENT_BINARY_DIR}/idydb-verified.json"
    COMMENT "Recheck supplied IdyDB hashes before linking")
  add_dependencies(exchange_windows_core exchange_verify_idydb)

  add_executable(post_profit_exchange
    $<TARGET_OBJECTS:exchange_windows_server_check> $<TARGET_OBJECTS:exchange_windows_core>)
  set_target_properties(post_profit_exchange PROPERTIES OUTPUT_NAME post-profit-exchange)
  target_link_libraries(post_profit_exchange PRIVATE exchange_supplied_idydb bcrypt shell32 ws2_32 advapi32 ole32 uuid)
  target_link_options(post_profit_exchange PRIVATE -municode -static -static-libgcc -static-libstdc++ -Wl,--no-insert-timestamp)
  add_executable(exchange_environment
    $<TARGET_OBJECTS:exchange_windows_cli_check> $<TARGET_OBJECTS:exchange_windows_core>)
  target_link_libraries(exchange_environment PRIVATE exchange_supplied_idydb bcrypt shell32 ws2_32 advapi32 ole32 uuid)
  target_link_options(exchange_environment PRIVATE -municode -static -static-libgcc -static-libstdc++ -Wl,--no-insert-timestamp)
  if(BUILD_TESTING)
    add_executable(exchange_windows_database_storage_test "${PH_EXCHANGE_CODE}/tests/windows_environment_store_test.cpp")
    target_link_libraries(exchange_windows_database_storage_test PRIVATE exchange_windows_core
      exchange_supplied_idydb advapi32 shell32 ole32 uuid)
    target_link_options(exchange_windows_database_storage_test PRIVATE -static -static-libgcc -static-libstdc++ -Wl,--no-insert-timestamp)
  endif()
  add_custom_target(exchange_stage_idydb_runtime)
  add_dependencies(exchange_stage_idydb_runtime exchange_verify_idydb)
  add_dependencies(post_profit_exchange exchange_stage_idydb_runtime)
  add_dependencies(exchange_environment exchange_stage_idydb_runtime)
  if(BUILD_TESTING)
    add_dependencies(exchange_windows_database_storage_test exchange_stage_idydb_runtime)
  endif()
  string(JSON PH_IDYDB_DLL_COUNT LENGTH "${PH_EXCHANGE_DEPENDENCY}" runtime_dlls)
  if(PH_IDYDB_DLL_COUNT GREATER 0)
    math(EXPR PH_IDYDB_DLL_LAST "${PH_IDYDB_DLL_COUNT} - 1")
    foreach(index RANGE 0 ${PH_IDYDB_DLL_LAST})
      string(JSON runtime_dll GET "${PH_EXCHANGE_DEPENDENCY}" runtime_dlls ${index} absolute_path)
      add_custom_command(TARGET exchange_stage_idydb_runtime POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${runtime_dll}" "${CMAKE_CURRENT_BINARY_DIR}")
    endforeach()
  endif()
  message(STATUS "External IdyDB package hashes verified. Runtime integration remains a separate required test.")
else()
  target_include_directories(exchange_windows_core SYSTEM PRIVATE
    "${PH_EXCHANGE_REPOSITORY}/tools/idydb/upstream/include")
  message(STATUS "IdyDB Windows library not supplied: compile checks only; no runnable exchange application target")
endif()

# Real Windows accounting executable, entirely independent of persistence.
if(BUILD_TESTING)
  add_executable(exchange_windows_accounting_test
    "${PH_EXCHANGE_CODE}/tests/transactions_test.cpp"
    "${PH_EXCHANGE_CODE}/operations/opening.cpp"
    "${PH_EXCHANGE_CODE}/operations/transactions.cpp")
  target_include_directories(exchange_windows_accounting_test PRIVATE "${PH_EXCHANGE_CODE}")
  target_include_directories(exchange_windows_accounting_test SYSTEM PRIVATE "${PH_EXCHANGE_JSON}")
  target_link_options(exchange_windows_accounting_test PRIVATE -static -static-libgcc -static-libstdc++ -Wl,--no-insert-timestamp)
  add_executable(exchange_windows_server_platform_test
    "${PH_EXCHANGE_CODE}/tests/server_platform_test.cpp"
    "${PH_EXCHANGE_CODE}/interfaces/server_platform.cpp")
  target_include_directories(exchange_windows_server_platform_test PRIVATE "${PH_EXCHANGE_CODE}")
  target_compile_definitions(exchange_windows_server_platform_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
  target_link_libraries(exchange_windows_server_platform_test PRIVATE bcrypt shell32)
  target_link_options(exchange_windows_server_platform_test PRIVATE -static -static-libgcc -static-libstdc++ -Wl,--no-insert-timestamp)
  add_executable(exchange_windows_files_test
    "${PH_EXCHANGE_CODE}/tests/windows_platform_files_test.cpp"
    "${PH_EXCHANGE_CODE}/platform/files.cpp"
    "${PH_EXCHANGE_CODE}/platform/windows_files.cpp")
  target_include_directories(exchange_windows_files_test PRIVATE "${PH_EXCHANGE_CODE}")
  target_compile_definitions(exchange_windows_files_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
  target_link_libraries(exchange_windows_files_test PRIVATE advapi32 shell32 ole32 uuid)
  target_link_options(exchange_windows_files_test PRIVATE -static -static-libgcc -static-libstdc++ -Wl,--no-insert-timestamp)
  add_executable(exchange_windows_storage_files_test
    "${PH_EXCHANGE_CODE}/tests/windows_files_security_test.cpp"
    "${PH_EXCHANGE_CODE}/platform/windows_files.cpp")
  target_include_directories(exchange_windows_storage_files_test PRIVATE "${PH_EXCHANGE_CODE}")
  target_compile_definitions(exchange_windows_storage_files_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
  target_link_libraries(exchange_windows_storage_files_test PRIVATE advapi32)
  target_link_options(exchange_windows_storage_files_test PRIVATE -static -static-libgcc -static-libstdc++ -Wl,--no-insert-timestamp)
  find_program(PH_EXCHANGE_WINE wine64 PATHS /usr/lib/wine NO_DEFAULT_PATH)
  if(PH_EXCHANGE_WINE)
    foreach(item IN ITEMS accounting server_platform files storage_files)
      add_test(NAME exchange_windows_${item}
        COMMAND "${CMAKE_COMMAND}" -E env
          WINEPREFIX=/opt/post-profit-exchange/windows/wine WINEARCH=win64 WINEDEBUG=-all LC_ALL=C.UTF-8
          "${PH_EXCHANGE_WINE}" "$<TARGET_FILE:exchange_windows_${item}_test>")
      set_tests_properties(exchange_windows_${item} PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE)
    endforeach()
    set_tests_properties(exchange_windows_storage_files PROPERTIES SKIP_RETURN_CODE 77)
    foreach(mode IN ITEMS shutdown output)
      add_test(NAME exchange_windows_${mode}
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run_windows_test.py"
          "$<TARGET_FILE:exchange_windows_server_platform_test>" "--${mode}")
      set_tests_properties(exchange_windows_${mode} PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE)
    endforeach()
  else()
    message(WARNING "Wine absent: Windows executable tests are not registered")
  endif()
  add_test(NAME exchange_windows_build_tools
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_build_tools.py")
endif()
