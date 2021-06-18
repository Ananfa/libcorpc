cmake_minimum_required(VERSION 2.8)
project(libcorpc)

# Check dependency libraries
find_library(PROTOBUF_LIB protobuf)
if(NOT PROTOBUF_LIB)
    message(FATAL_ERROR "protobuf library not found")
endif()

find_library(CO_LIB co)
if(NOT CO_LIB)
    message(FATAL_ERROR "co library not found")
endif()

if (CMAKE_BUILD_TYPE)
else()
    set(CMAKE_BUILD_TYPE RELEASE)
endif()

message("------------ Options -------------")
message("  CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}")

# Add source files
aux_source_directory(${PROJECT_SOURCE_DIR}/src SOURCE_FILES)
aux_source_directory(${PROJECT_SOURCE_DIR}/src/proto SOURCE_FILES)

set(CMAKE_VERBOSE_MAKEFILE ON)

# This for mac osx only
set(CMAKE_MACOSX_RPATH 0)

# Set lib version
set(LIB_VERSION 2)

# Set cflags
set(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} "-std=gnu++11 -fPIC -Wall -Wno-reorder -pthread")
set(CMAKE_CXX_FLAGS_DEBUG "-g -pg -O0 -DDEBUG=1 -DLOG_LEVEL=0 ${CMAKE_CXX_FLAGS}")
set(CMAKE_CXX_FLAGS_RELEASE "-g -O3 -DLOG_LEVEL=0 ${CMAKE_CXX_FLAGS}")

# Add include directories
include_directories(/usr/local/include)
include_directories(/usr/local/include/co)
include_directories(${PROJECT_SOURCE_DIR}/src)
include_directories(${PROJECT_SOURCE_DIR}/src/proto)

# Add static and shared library target
#add_library(libcorpc_static STATIC ${SOURCE_FILES})
add_library(libcorpc_shared SHARED ${SOURCE_FILES})

# Set library output name
#set_target_properties(libcorpc_static PROPERTIES OUTPUT_NAME corpc)
set_target_properties(libcorpc_shared PROPERTIES OUTPUT_NAME corpc)

#set_target_properties(libcorpc_static PROPERTIES CLEAN_DIRECT_OUTPUT 1)
set_target_properties(libcorpc_shared PROPERTIES CLEAN_DIRECT_OUTPUT 1)

if(APPLE)
    set(MY_LIB_LINK_LIBRARIES -L/usr/local/lib -lprotobuf -lco)
    target_link_libraries(libcorpc_shared ${MY_LIB_LINK_LIBRARIES})
endif()

# Set shared library version, will generate libcorpc.${LIB_VERSION}.so and a symbol link named libcorpc.so
# For mac osx, the extension name will be .dylib
set_target_properties(libcorpc_shared PROPERTIES VERSION ${LIB_VERSION} SOVERSION ${LIB_VERSION})

install(TARGETS libcorpc_shared LIBRARY DESTINATION "lib" ARCHIVE DESTINATION "lib")
install(DIRECTORY ${PROJECT_SOURCE_DIR}/src/ DESTINATION "include/corpc" FILES_MATCHING PATTERN "*.h" PATTERN "*.proto")
