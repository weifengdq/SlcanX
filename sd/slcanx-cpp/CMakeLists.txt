cmake_minimum_required(VERSION 3.10)
project(slcanx-cpp)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(include)

# Library
add_library(slcanx src/slcanx.cpp)

# Examples
add_executable(01_simple_std examples/01_simple_std.cpp)
target_link_libraries(01_simple_std slcanx)

add_executable(02_periodic examples/02_periodic.cpp)
target_link_libraries(02_periodic slcanx)

add_executable(03_multi_std_threading examples/03_multi_std_threading.cpp)
target_link_libraries(03_multi_std_threading slcanx)

add_executable(05_simple_fd examples/05_simple_fd.cpp)
target_link_libraries(05_simple_fd slcanx)

add_executable(08_custom_timing examples/08_custom_timing.cpp)
target_link_libraries(08_custom_timing slcanx)
