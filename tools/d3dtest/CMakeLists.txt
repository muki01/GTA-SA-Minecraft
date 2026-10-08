cmake_minimum_required(VERSION 3.21)
project(d3dtest CXX)
add_executable(d3dtest d3dtest.cpp)
target_link_libraries(d3dtest d3d9)
