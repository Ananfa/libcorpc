cmake_minimum_required(VERSION 2.8)
project(tutorial3)

# Check dependency libraries
find_library(PROTOBUF_LIB protobuf)
if(NOT PROTOBUF_LIB)
    message(FATAL_ERROR "protobuf library not found")
endif()

find_library(CO_LIB co)
if(NOT CO_LIB)
    message(FATAL_ERROR "co library not found")
endif()

find_library(CORPC_LIB corpc)
if(NOT CORPC_LIB)
    message(FATAL_ERROR "corpc library not found")
endif()

if (CMAKE_BUILD_TYPE)
else()
    set(CMAKE_BUILD_TYPE RELEASE)
endif()

message("------------ Options -------------")
message("  CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}")

set(SERVER_SOURCE_FILES
    ../proto/echo.pb.cc
    ../proto/helloworld.pb.cc
    src/server.cpp)

set(CLIENT_SOURCE_FILES
    ../proto/echo.pb.cc
    ../proto/helloworld.pb.cc
    src/client.cpp)

set(CMAKE_VERBOSE_MAKEFILE ON)

# This for mac osx only
set(CMAKE_MACOSX_RPATH 0)

# Set cflags
set(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} "-std=gnu++11 -fPIC -Wall -pthread")
set(CMAKE_CXX_FLAGS_DEBUG "-g -pg -O0 -DDEBUG=1 ${CMAKE_CXX_FLAGS}")
set(CMAKE_CXX_FLAGS_RELEASE "-g -O3 ${CMAKE_CXX_FLAGS}")

# Add include directories
include_directories(/usr/local/include)
include_directories(/usr/local/include/co)
include_directories(/usr/local/include/corpc)
include_directories(/usr/local/include/corpc/proto)
include_directories(${PROJECT_SOURCE_DIR}/../proto)

# Add target
add_executable(tutorial3S ${SERVER_SOURCE_FILES})
add_executable(tutorial3C ${CLIENT_SOURCE_FILES})

set(MY_LINK_LIBRARIES -L/usr/local/lib -lprotobuf -lcorpc -lco -ldl)
target_link_libraries(tutorial3S ${MY_LINK_LIBRARIES})
target_link_libraries(tutorial3C ${MY_LINK_LIBRARIES})
