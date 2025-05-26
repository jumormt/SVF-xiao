add_llvm_executable(svf-ex svf-ex.cpp)
target_link_libraries(svf-ex PUBLIC ${llvm_libs} ${LGRAPH_CPP_CLIENT_LIBRARIES} SvfLLVM)