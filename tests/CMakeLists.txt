include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
)
# Honor the parent project's runtime library selection on Windows.
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

add_executable(obme_tests
    test_price_level.cpp
    test_order_book.cpp
)
target_link_libraries(obme_tests PRIVATE obme obme_warnings GTest::gtest_main)

gtest_discover_tests(obme_tests)
