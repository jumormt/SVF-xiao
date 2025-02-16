add_llvm_executable(mta mta.cpp)
target_link_libraries(mta PUBLIC ${llvm_libs} ${LGRAPH_CPP_CLIENT_LIBRARIES} SvfLLVM)
