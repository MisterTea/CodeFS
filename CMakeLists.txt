cmake_minimum_required (VERSION 3.0.2)
project (CodeFS VERSION 0.0.1)

option(CODE_COVERAGE "Enable code coverage" OFF)
option(BUILD_CLIENT "Build the client (depends on fuse)" ON)


SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DCODEFS_VERSION='\"${PROJECT_VERSION}\"'")

# Needed for FUSE
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_FILE_OFFSET_BITS=64")

# Debug info for getting line numbers
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g")

IF(APPLE)
# Turn off address randomizing to get debug prints
SET(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fno-pie")
ENDIF()

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

# For fsevent
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DHAVE_CONFIG_H")
SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DHAVE_CONFIG_H")

# Add cmake script directory.
LIST(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# Required packages
find_package(Threads REQUIRED)
find_package(GFlags REQUIRED)
find_package(ZeroMQ REQUIRED)
find_package(Protobuf REQUIRED)
find_package(Boost REQUIRED COMPONENTS filesystem)
find_package(ZLIB REQUIRED)

IF(BUILD_CLIENT)
IF(APPLE)
find_package(OSXFuse REQUIRED)
SET(FUSE_INCLUDE_DIR ${OSXFUSE_INCLUDE_DIR})
SET(FUSE_LIBRARIES ${OSXFUSE_LIBRARIES})
ELSE(APPLE)
find_package(LibFUSE REQUIRED)
SET(FUSE_INCLUDE_DIR ${LIBFUSE_INCLUDE_DIRS})
SET(FUSE_LIBRARIES ${LIBFUSE_LIBRARIES})
ENDIF(APPLE)
ENDIF(BUILD_CLIENT)

IF(FREEBSD)
  set(CORE_LIBRARIES util)
ELSE()
  set(CORE_LIBRARIES util resolv)
ENDIF()

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
  external
  external/asio/asio/include
  external/json/include
  external/msgpack-c/include
  external/fswatch_1.9.3
  external/fswatch_1.9.3/src/
  ${CMAKE_CURRENT_BINARY_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${GFLAGS_INCLUDE_DIRS}
  ${FUSE_INCLUDE_DIR}
  ${Boost_INCLUDE_DIRS}
  ${ZMQ_INCLUDE_DIRS}
  ${ZLIB_INCLUDE_DIRS}
  )

add_library(
  codefs-lib
  STATIC

  external/simpleini/ConvertUTF.c

  external/fswatch_1.9.3/src/c/cevent.cpp
  external/fswatch_1.9.3/src/c/libfswatch_log.cpp
  external/fswatch_1.9.3/src/c/libfswatch.cpp
  external/fswatch_1.9.3/src/c++/event.cpp
  external/fswatch_1.9.3/src/c++/fen_monitor.cpp
  external/fswatch_1.9.3/src/c++/fsevents_monitor.cpp
  external/fswatch_1.9.3/src/c++/event.cpp
  external/fswatch_1.9.3/src/c++/inotify_monitor.cpp
  external/fswatch_1.9.3/src/c++/kqueue_monitor.cpp
  external/fswatch_1.9.3/src/c++/libfswatch_exception.cpp
  external/fswatch_1.9.3/src/c++/monitor.cpp
  external/fswatch_1.9.3/src/c++/path_utils.cpp
  external/fswatch_1.9.3/src/c++/poll_monitor.cpp
  external/fswatch_1.9.3/src/c++/windows_monitor.cpp
  external/fswatch_1.9.3/src/c++/string/string_utils.cpp

  src/base/EasyLoggingWrapper.cpp

  src/base/LogHandler.hpp
  src/base/LogHandler.cpp

  src/base/DaemonCreator.hpp
  src/base/DaemonCreator.cpp

  src/base/PidController.hpp
  src/base/PidController.cpp

  src/base/FileSystem.hpp
  src/base/FileSystem.cpp

  src/base/FileUtils.hpp
  src/base/FileUtils.cpp

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

IF(BUILD_CLIENT)
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
  ${ZLIB_LIBRARY_RELEASE}
)
ENDIF(BUILD_CLIENT)

add_executable (
  codefsserver

  src/server/ServerFileSystem.cpp
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
  ${ZLIB_LIBRARY_RELEASE}
)

file(GLOB TEST_SRCS test/Test*.cpp )
add_executable(
  codefs-test

  ${TEST_SRCS}
  )

add_dependencies(
  codefs-test

  codefs-lib)

target_link_libraries(
  codefs-test
  codefs-lib
  ${ZMQ_LIBRARIES}
  ${CMAKE_THREAD_LIBS_INIT}
  ${GFLAGS_LIBRARIES}
  ${sodium_LIBRARY_RELEASE}
  ${Boost_LIBRARIES}
  ${ZLIB_LIBRARIES}
  ${ZLIB_LIBRARY_RELEASE}
  resolv
  util
  )
add_test(
  codefs-test
  codefs-test
  )

install(TARGETS codefs codefsserver
  PERMISSIONS  OWNER_EXECUTE OWNER_WRITE OWNER_READ GROUP_EXECUTE GROUP_READ WORLD_EXECUTE WORLD_READ
  DESTINATION "bin"
  )
