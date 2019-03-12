cmake_minimum_required (VERSION 2.8.12)
project (CodeFS)

option(BUILD_TEST "Build all unit test" ON)
option(CODE_COVERAGE "Enable code coverage" OFF)


set (${PROJECT_NAME}_VERSION_MAJOR 0)
set (${PROJECT_NAME}_VERSION_MINOR 0)
set (${PROJECT_NAME}_VERSION_PATCH 1)
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DCODEFS_VERSION='\"${EternalTCP_VERSION_MAJOR}.${EternalTCP_VERSION_MINOR}.${EternalTCP_VERSION_PATCH}\"'")
# For easylogging, disable default log file, enable crash log, ensure thread safe, and catch c++ exceptions
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DELPP_NO_DEFAULT_LOG_FILE -DELPP_FEATURE_CRASH_LOG -DELPP_THREAD_SAFE -DELPP_HANDLE_SIGABRT")

# Needed for FUSE
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_FILE_OFFSET_BITS=64")

IF(BUILD_TEST)
  add_subdirectory(${CMAKE_SOURCE_DIR}/external/googletest-1.8.0)
ENDIF(BUILD_TEST)
IF(CODE_COVERAGE)
  if(UNIX)
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fprofile-arcs -ftest-coverage")
  endif()
ENDIF(CODE_COVERAGE)

# Enable C++-11
if(UNIX)
	SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -std=c++11")
endif()

#Using FreeBSD?
if (CMAKE_SYSTEM_NAME MATCHES "FreeBSD")
    set(FREEBSD TRUE)
endif (CMAKE_SYSTEM_NAME MATCHES "FreeBSD")

# Add cmake script directory.
LIST(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# Required packages
find_package(Threads REQUIRED)
find_package(GFlags REQUIRED)
find_package(ZeroMQ REQUIRED)
find_package(Protobuf REQUIRED)
find_package(Boost REQUIRED COMPONENTS filesystem)
find_package(fswatch REQUIRED)

IF(APPLE)
find_package(OSXFuse REQUIRED)
SET(FUSE_INCLUDE_DIR ${OSXFUSE_INCLUDE_DIR})
SET(FUSE_LIBRARIES ${OSXFUSE_LIBRARIES})
ELSE(APPLE)
find_package(LibFUSE REQUIRED)
SET(FUSE_INCLUDE_DIR ${LIBFUSE_INCLUDE_DIRS})
SET(FUSE_LIBRARIES ${LIBFUSE_LIBRARIES})
ENDIF(APPLE)

IF(FREEBSD)
  set(CORE_LIBRARIES util)
ELSE()
  set(CORE_LIBRARIES util resolv)
ENDIF()
IF(APPLE)
ELSE(APPLE)
  SET(CORE_LIBRARIES ulockmgr ${CORE_LIBRARIES})
ENDIF(APPLE)

PROTOBUF_GENERATE_CPP(
  CODEFS_SRCS
  CODEFS_HDRS

  proto/CodeFS.proto
)
add_custom_target(
  generated-code
  DEPENDS

  ${CODEFS_SRCS} ${CODEFS_HDRS}
)

include_directories(
  src/base
  external/msgpack-3.0.1/include
  external/Optional
  ${CMAKE_CURRENT_BINARY_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${GFLAGS_INCLUDE_DIRS}
  ${FUSE_INCLUDE_DIR}
  ${Boost_INCLUDE_DIRS}
  ${ZMQ_INCLUDE_DIRS}
  ${fswatch_INCLUDE_DIRS}
  )

add_library(
  codefs-lib
  STATIC

  src/base/easylogging++.h
  src/base/easylogging++.cc

  src/base/LogHandler.hpp
  src/base/LogHandler.cpp

  src/base/DaemonCreator.hpp
  src/base/DaemonCreator.cpp

  src/base/RawSocketUtils.hpp
  src/base/RawSocketUtils.cpp

  src/base/FileUtils.hpp
  src/base/FileUtils.cpp

  src/base/FuseAdapter.hpp
  src/base/FuseAdapter.cpp

  src/base/BiDirectionalRpc.hpp
  src/base/BiDirectionalRpc.cpp
  
  src/base/ZmqBiDirectionalRpc.hpp
  src/base/ZmqBiDirectionalRpc.cpp

  src/base/TimeHandler.hpp
  src/base/TimeHandler.cpp

  ${CODEFS_SRCS}
)
add_dependencies(
  codefs-lib
  generated-code
)

add_executable (
  codefs

  src/client/Client.hpp
  src/client/Client.cpp

  src/client/ClientFuseAdapter.hpp
  src/client/ClientFuseAdapter.cpp

  src/client/Main.cpp
  )

target_link_libraries (
  codefs
  LINK_PUBLIC
  codefs-lib
  ${ZMQ_LIBRARIES}
  ${CMAKE_THREAD_LIBS_INIT}
  ${PROTOBUF_LIBRARIES}
  ${GFLAGS_LIBRARIES}
  ${Boost_LIBRARIES}
  ${CORE_LIBRARIES}
  ${FUSE_LIBRARIES}
  ${fswatch_LIBRARIES}
)

add_executable (
  codefsserver

  src/server/ServerFileSystem.cpp
  src/server/Scanner.cpp
  src/server/Server.cpp
  src/server/fswatchexample.cpp

  src/server/Main.cpp
  )

target_link_libraries (
  codefsserver
  LINK_PUBLIC
  codefs-lib
  ${ZMQ_LIBRARIES}
  ${CMAKE_THREAD_LIBS_INIT}
  ${PROTOBUF_LIBRARIES}
  ${GFLAGS_LIBRARIES}
  ${Boost_LIBRARIES}
  ${CORE_LIBRARIES}
  ${FUSE_LIBRARIES}
  ${fswatch_LIBRARIES}
)

if(BUILD_TEST) # Build unit tests.
  message(STATUS "Enabling test for ${PROJECT_NAME}")
  enable_testing()

  set(GTEST_PATH ${PROJECT_SOURCE_DIR}/test)

  file(GLOB TEST_SRCS test/Test*.cpp )
  add_executable(
    codefs-test
  
    ${TEST_SRCS}
    )
  
  add_dependencies(
    codefs-test
    codefs-lib
    gtest
    gtest_main
    gmock
    gmock_main)
  
  target_link_libraries(
    codefs-test
    codefs-lib
    ${ZMQ_LIBRARIES}
    ${CMAKE_THREAD_LIBS_INIT}
    gtest
    gtest_main
    gmock
    gmock_main
    ${GFLAGS_LIBRARIES}
    ${sodium_LIBRARY_RELEASE}
    ${Boost_LIBRARIES}
    resolv
    util
    )
  add_test(
    codefs-test
    codefs-test
    )
  
endif()

install(TARGETS codefs codefsserver
  PERMISSIONS  OWNER_EXECUTE OWNER_WRITE OWNER_READ GROUP_EXECUTE GROUP_READ WORLD_EXECUTE WORLD_READ
  DESTINATION "bin"
  )
