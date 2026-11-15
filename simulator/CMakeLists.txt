# Market-making simulator — built on the public obme API, kept out of
# include/obme/ so the core library's public surface is unchanged.

add_library(obme_sim
    Simulator.cpp
    FlowImbalanceMM.cpp
)
target_include_directories(obme_sim PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(obme_sim PUBLIC obme PRIVATE obme_warnings)

add_executable(mm_sim mm_main.cpp)
target_link_libraries(mm_sim PRIVATE obme_sim obme_warnings)
