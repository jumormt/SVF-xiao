add_llvm_executable(ae ae.cpp)
target_link_libraries(ae PUBLIC ${llvm_libs} ${LGRAPH_CPP_CLIENT_LIBRARIES} SvfLLVM)