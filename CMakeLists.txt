cmake_minimum_required (VERSION 3.0.2)
project (CodeFS VERSION 0.0.1)

SET(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/external/sanitizers-cmake/cmake" ${CMAKE_MODULE_PATH})
FIND_PACKAGE(Sanitizers)

MACRO(DECORATE_TARGET TARGET_NAME)
  add_sanitizers(${TARGET_NAME})
  set_target_properties(${TARGET_NAME} PROPERTIES COTIRE_CXX_PREFIX_HEADER_INIT "src/base/Headers.hpp")
  cotire(${TARGET_NAME})
ENDMACRO()

SET(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/external/cotire/CMake" ${CMAKE_MODULE_PATH})
INCLUDE(cotire)
if(POLICY CMP0058) 
    cmake_policy(SET CMP0058 NEW) # Needed for cotire
endif() 

option(CODE_COVERAGE "Enable code coverage" OFF)
option(BUILD_CLIENT "Build the client (depends on fuse)" ON)


SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DCODEFS_VERSION='\"${PROJECT_VERSION}\"'")

# Link whole archives
if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang" OR "${CMAKE_CXX_COMPILER_ID}" STREQUAL "AppleClang")
  # using Clang
  SET(WHOLE_ARCHIVE_ON "-Wl,-force_load,")
  SET(WHOLE_ARCHIVE_OFF "")
elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
  # using GCC
  SET(WHOLE_ARCHIVE_ON "-Wl,--whole-archive")
  SET(WHOLE_ARCHIVE_OFF "-Wl,--no-whole-archive")
else()
  message( FATAL_ERROR "Unsupported compiler ${CMAKE_CXX_COMPILER_ID}" )
endif()

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

if(UNIX)
  # Enable C++-11
  SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -std=c++11")

  # Enable debug info
  SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g")
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

IF(APPLE)
  set(CORE_LIBRARIES "-framework CoreServices" "-framework CoreFoundation" util resolv)
ELSEIF(FREEBSD)
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
  external/fswatch_config
  external/fswatch/libfswatch/src
  external/fswatch/libfswatch/src/libfswatch
  external/cxxopts/include
  ${CMAKE_CURRENT_BINARY_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${GFLAGS_INCLUDE_DIRS}
  ${FUSE_INCLUDE_DIR}
  ${Boost_INCLUDE_DIRS}
  ${ZMQ_INCLUDE_DIRS}
  ${ZLIB_INCLUDE_DIRS}
  )

if(APPLE)
SET(
    OS_FSWATCH_FILES

    external/fswatch/libfswatch/src/libfswatch/c++/fsevents_monitor.cpp
)
elseif(UNIX)
SET(
    OS_FSWATCH_FILES

    external/fswatch/libfswatch/src/libfswatch/c++/inotify_monitor.cpp
)
else()
SET(
    OS_FSWATCH_FILES

    external/fswatch/libfswatch/src/libfswatch/c++/windows_monitor.cpp
)
endif()
add_library(
  codefs-lib
  STATIC

  external/simpleini/ConvertUTF.c

  external/fswatch/libfswatch/src/libfswatch/c/cevent.cpp
  external/fswatch/libfswatch/src/libfswatch/c/libfswatch_log.cpp
  external/fswatch/libfswatch/src/libfswatch/c/libfswatch.cpp
  external/fswatch/libfswatch/src/libfswatch/c++/event.cpp
  external/fswatch/libfswatch/src/libfswatch/c++/fen_monitor.cpp
  external/fswatch/libfswatch/src/libfswatch/c++/event.cpp
  external/fswatch/libfswatch/src/libfswatch/c++/libfswatch_exception.cpp
  external/fswatch/libfswatch/src/libfswatch/c++/monitor.cpp
  external/fswatch/libfswatch/src/libfswatch/c++/monitor_factory.cpp
  external/fswatch/libfswatch/src/libfswatch/c++/path_utils.cpp
  external/fswatch/libfswatch/src/libfswatch/c++/poll_monitor.cpp
  external/fswatch/libfswatch/src/libfswatch/c++/string/string_utils.cpp
  ${OS_FSWATCH_FILES}

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
DECORATE_TARGET(codefs-lib)

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
  ${WHOLE_ARCHIVE_ON} codefs-lib
  ${WHOLE_ARCHIVE_OFF} ${ZMQ_LIBRARIES}
  ${CMAKE_THREAD_LIBS_INIT}
  ${PROTOBUF_LIBRARIES}
  ${GFLAGS_LIBRARIES}
  ${Boost_LIBRARIES}
  ${FUSE_LIBRARIES}
  ${ZLIB_LIBRARY_RELEASE}
  ${CORE_LIBRARIES}
)
DECORATE_TARGET(codefs)
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
  ${WHOLE_ARCHIVE_ON} codefs-lib
  ${WHOLE_ARCHIVE_OFF} ${ZMQ_LIBRARIES}
  ${CMAKE_THREAD_LIBS_INIT}
  ${PROTOBUF_LIBRARIES}
  ${GFLAGS_LIBRARIES}
  ${Boost_LIBRARIES}
  ${ZLIB_LIBRARY_RELEASE}
  ${CORE_LIBRARIES}
)
DECORATE_TARGET(codefsserver)

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
  ${WHOLE_ARCHIVE_ON} codefs-lib
  ${WHOLE_ARCHIVE_OFF} ${ZMQ_LIBRARIES}
  ${CMAKE_THREAD_LIBS_INIT}
  ${PROTOBUF_LIBRARIES}
  ${GFLAGS_LIBRARIES}
  ${sodium_LIBRARY_RELEASE}
  ${Boost_LIBRARIES}
  ${ZLIB_LIBRARIES}
  ${ZLIB_LIBRARY_RELEASE}
  ${CORE_LIBRARIES}
  )
add_test(
  codefs-test
  codefs-test
  )
DECORATE_TARGET(codefs-test)

install(TARGETS codefs codefsserver
  PERMISSIONS  OWNER_EXECUTE OWNER_WRITE OWNER_READ GROUP_EXECUTE GROUP_READ WORLD_EXECUTE WORLD_READ
  DESTINATION "bin"
  )
