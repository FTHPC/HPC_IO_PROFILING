cmake_minimum_required (VERSION 3.20)
project (IO_benchmarks)

include(GNUInstallDirs)
add_definitions(-O3 -g -Wall)
set(CXX_STANDARD_REQUIRED 11)
set(CMAKE_CXX_COMPILER "g++")

find_package(Boost REQUIRED COMPONENTS program_options)
find_package(OpenMP REQUIRED)
include_directories(${Boost_INCLUDE_DIR})
include_directories(${OpenMP_CXX_INCLUDE_DIRS})

add_executable(fileIO_bench benchmark_fileIO_simulator.cpp)
target_link_libraries(fileIO_bench m OpenMP::OpenMP_CXX Boost::program_options)
