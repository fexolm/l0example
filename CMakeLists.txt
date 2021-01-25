project(l0-example)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(LLVM REQUIRED CONFIG)

include_directories(${LLVM_INCLUDE_DIRS})
add_executable(l0example main.cpp l0device.cpp)

target_link_libraries(l0example ze_loader LLVM LLVMSupport LLVMSPIRVLib)

