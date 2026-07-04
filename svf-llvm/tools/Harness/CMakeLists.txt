add_llvm_executable(svf-harness svf-harness.cpp QueryEngine.cpp Queries.cpp SaberQueries.cpp MTAQueries.cpp VFPath.cpp GraphQueries.cpp Evidence.cpp HarnessServer.cpp Schema.cpp)
target_include_directories(svf-harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/external)
target_compile_definitions(svf-harness PRIVATE
  SVF_HARNESS_CFL_GRAMMAR_DIR="${CMAKE_SOURCE_DIR}/svf/include/CFL/grammar")
# add_llvm_executable inherits LLVM's -fno-exceptions; the harness uses
# exceptions for error reporting (QueryEngine throws, nlohmann::json throws).
target_compile_options(svf-harness PRIVATE -fexceptions)

# Integration test suite (python; needs clang on PATH — ctest inherits the
# caller's environment, so run ctest after `source ./setup.sh`).
if(BUILD_TESTING)
  add_test(NAME harness_integration
           COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_tests.py -v)
  set_tests_properties(harness_integration PROPERTIES
    ENVIRONMENT "SVF_HARNESS_BIN=$<TARGET_FILE:svf-harness>")
  # Tutorial example scripts (examples/01..05 via run_all.sh). RUN_BIG is
  # deliberately not set: the bash.bc stretch in 05 stays off in CI.
  add_test(NAME harness_examples
           COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/examples/run_all.sh)
  set_tests_properties(harness_examples PROPERTIES
    ENVIRONMENT "SVF_HARNESS_BIN=$<TARGET_FILE:svf-harness>")
endif()
