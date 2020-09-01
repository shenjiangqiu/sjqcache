cmake_minimum_required(VERSION 3.18)
project(sjqcache)
add_library(sjqcache "cache.cc")
target_include_directories(sjqcache PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})