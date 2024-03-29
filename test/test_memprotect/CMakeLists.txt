cmake_minimum_required(VERSION 2.8)
project(example_release_channel)

# Check dependency libraries

if (CMAKE_BUILD_TYPE)
else()
    set(CMAKE_BUILD_TYPE RELEASE)
endif()

message("------------ Options -------------")
message("  CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}")

set(SOURCE_FILES
    src/main.cpp)

set(CMAKE_VERBOSE_MAKEFILE ON)

# This for mac osx only
set(CMAKE_MACOSX_RPATH 0)

# Set cflags
set(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} "-std=gnu++11 -fPIC -Wall -pthread")
set(CMAKE_CXX_FLAGS_DEBUG "-g -pg -O0 -DDEBUG=1 -DLOG_LEVEL=0 ${CMAKE_CXX_FLAGS}")
set(CMAKE_CXX_FLAGS_RELEASE "-g -O3 -DLOG_LEVEL=1 ${CMAKE_CXX_FLAGS}")

# Add include directories
include_directories(/usr/local/include)

# Add target
add_executable(test ${SOURCE_FILES})

set(MY_LINK_LIBRARIES -L/usr/local/lib -ldl)
target_link_libraries(test ${MY_LINK_LIBRARIES})
