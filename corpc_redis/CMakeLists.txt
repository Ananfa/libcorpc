cmake_minimum_required(VERSION 2.8)
project(libcorpc_redis)

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

find_library(HIREDIS_LIB hiredis)
if(NOT HIREDIS_LIB)
    message(FATAL_ERROR "hiredis library not found")
endif()

if (CMAKE_BUILD_TYPE)
else()
    set(CMAKE_BUILD_TYPE RELEASE)
endif()

message("------------ Options -------------")
message("  CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}")

aux_source_directory(${PROJECT_SOURCE_DIR}/src SOURCE_FILES)

set(CMAKE_VERBOSE_MAKEFILE ON)

# This for mac osx only
set(CMAKE_MACOSX_RPATH 0)

# Set lib version
set(LIB_VERSION 1)

# Set cflags
set(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} "-std=gnu++11 -fPIC -Wall -pthread")
set(CMAKE_CXX_FLAGS_DEBUG "-g -pg -O0 -DDEBUG=1 ${CMAKE_CXX_FLAGS}")
set(CMAKE_CXX_FLAGS_RELEASE "-g -O3 -DLOG_LEVEL=3 ${CMAKE_CXX_FLAGS}")

# Add include directories
include_directories(/usr/local/include)
include_directories(/usr/local/include/co)
include_directories(/usr/local/include/corpc)
include_directories(/usr/local/include/corpc/proto)
include_directories(/usr/local/include/hiredis)
include_directories(${PROJECT_SOURCE_DIR}/src)

# Add static and shared library target
#add_library(libcorpc_redis_static STATIC ${SOURCE_FILES})
add_library(libcorpc_redis_shared SHARED ${SOURCE_FILES})

# Set library output name
#set_target_properties(libcorpc_redis_static PROPERTIES OUTPUT_NAME corpc_redis)
set_target_properties(libcorpc_redis_shared PROPERTIES OUTPUT_NAME corpc_redis)

#set_target_properties(libcorpc_redis_static PROPERTIES CLEAN_DIRECT_OUTPUT 1)
set_target_properties(libcorpc_redis_shared PROPERTIES CLEAN_DIRECT_OUTPUT 1)

if(APPLE)
    set(MY_LIB_LINK_LIBRARIES -L/usr/local/lib -lhiredis -lprotobuf -lcorpc -lco)
    target_link_libraries(libcorpc_redis_shared ${MY_LIB_LINK_LIBRARIES})
endif()

# Set shared library version, will generate libcorpc.${LIB_VERSION}.so and a symbol link named libcorpc_redis.so
# For mac osx, the extension name will be .dylib
set_target_properties(libcorpc_redis_shared PROPERTIES VERSION ${LIB_VERSION} SOVERSION ${LIB_VERSION})

install(TARGETS libcorpc_redis_shared LIBRARY DESTINATION "lib" ARCHIVE DESTINATION "lib")
install(DIRECTORY ${PROJECT_SOURCE_DIR}/src/ DESTINATION "include/corpc" FILES_MATCHING PATTERN "*.h")
