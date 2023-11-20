
# CMake minimum version
cmake_minimum_required(VERSION 3.0)

# project name and languages
project(t1mockup CXX RC)

# t1mockup.exe
add_executable(t1mockup WIN32 t1mockup.cpp t1mockup_res.rc)
target_compile_definitions(t1mockup PRIVATE -DMOCKUP=1)
target_link_libraries(t1mockup comctl32 imm32)
