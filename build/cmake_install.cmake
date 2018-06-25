# Install script for directory: /Users/xianke.liu/refer_prjs/libcorpc

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "/usr/local")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "FALSE")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for each subdirectory.
  include("/Users/xianke.liu/refer_prjs/libcorpc/build/co/cmake_install.cmake")
  include("/Users/xianke.liu/refer_prjs/libcorpc/build/corpc/cmake_install.cmake")
  include("/Users/xianke.liu/refer_prjs/libcorpc/build/corpc_memcached/cmake_install.cmake")
  include("/Users/xianke.liu/refer_prjs/libcorpc/build/corpc_mongodb/cmake_install.cmake")
  include("/Users/xianke.liu/refer_prjs/libcorpc/build/corpc_mysql/cmake_install.cmake")
  include("/Users/xianke.liu/refer_prjs/libcorpc/build/corpc_redis/cmake_install.cmake")
  include("/Users/xianke.liu/refer_prjs/libcorpc/build/example/example_echoTcp/cmake_install.cmake")
  include("/Users/xianke.liu/refer_prjs/libcorpc/build/example/example_echoUdp/cmake_install.cmake")
  include("/Users/xianke.liu/refer_prjs/libcorpc/build/example/example_innerRpc/cmake_install.cmake")
  include("/Users/xianke.liu/refer_prjs/libcorpc/build/example/example_interRpc/cmake_install.cmake")
  include("/Users/xianke.liu/refer_prjs/libcorpc/build/example/example_memcached/cmake_install.cmake")
  include("/Users/xianke.liu/refer_prjs/libcorpc/build/example/example_mongodb/cmake_install.cmake")
  include("/Users/xianke.liu/refer_prjs/libcorpc/build/example/example_mysql/cmake_install.cmake")
  include("/Users/xianke.liu/refer_prjs/libcorpc/build/example/example_redis/cmake_install.cmake")

endif()

if(CMAKE_INSTALL_COMPONENT)
  set(CMAKE_INSTALL_MANIFEST "install_manifest_${CMAKE_INSTALL_COMPONENT}.txt")
else()
  set(CMAKE_INSTALL_MANIFEST "install_manifest.txt")
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
file(WRITE "/Users/xianke.liu/refer_prjs/libcorpc/build/${CMAKE_INSTALL_MANIFEST}"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
