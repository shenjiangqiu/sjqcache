cmake_minimum_required(VERSION 3.10)
project(sjqcache)
add_library(sjqcache "sjqcache.cc")
set(CMAKE_CXX_FLAGS "-Wall -Wextra")

target_include_directories(sjqcache PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
