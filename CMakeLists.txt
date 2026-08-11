cmake_minimum_required(VERSION 3.14)

project(orderbook_microstructure_engine
        VERSION 0.1.0
        DESCRIPTION "Limit order book reconstruction & OFI signal backtester"
        LANGUAGES CXX)

# ---- Global settings -------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(OBME_BUILD_TESTS "Build the unit test suite" ON)

# Warnings-as-quality-bar: the portfolio README promises a clean -Wall -Wextra
# build. Scoped to our own targets (via this interface library) so third-party
# code fetched by CMake — e.g. GoogleTest — isn't held to it.
add_library(obme_warnings INTERFACE)
target_compile_options(obme_warnings INTERFACE -Wall -Wextra -Wpedantic)

# ---- Core library ----------------------------------------------------------
# Sources are added milestone by milestone. M1: core data structures only.
add_library(obme
    src/PriceLevel.cpp
    src/OrderBook.cpp
)
target_include_directories(obme PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(obme PRIVATE obme_warnings)

# ---- Tests -----------------------------------------------------------------
if(OBME_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
