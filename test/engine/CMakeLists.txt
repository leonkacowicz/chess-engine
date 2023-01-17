file(GLOB SRCS *.cpp)
add_executable(engine_test ${SRCS})
target_link_libraries(engine_test engine)
target_link_libraries(engine_test libgtest)
target_link_libraries(engine_test libgmock)

add_test(NAME engine_test COMMAND engine_test)
