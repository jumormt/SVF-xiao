add_llvm_executable(svf-harness svf-harness.cpp)
target_include_directories(svf-harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/external)
