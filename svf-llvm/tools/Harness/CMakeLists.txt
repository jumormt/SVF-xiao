add_llvm_executable(svf-harness svf-harness.cpp QueryEngine.cpp Queries.cpp Evidence.cpp HarnessServer.cpp Schema.cpp)
target_include_directories(svf-harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/external)
# add_llvm_executable inherits LLVM's -fno-exceptions; the harness uses
# exceptions for error reporting (QueryEngine throws, nlohmann::json throws).
target_compile_options(svf-harness PRIVATE -fexceptions)
