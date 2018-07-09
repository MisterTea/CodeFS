set(GTEST_PATH ${PROJECT_SOURCE_DIR}/test)

file(GLOB TEST_SRCS Test*.cpp )
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
