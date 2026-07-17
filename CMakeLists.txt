cmake_minimum_required(VERSION 3.20)
project(gpufab CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(GPUFAB_SANITIZE "Build with ASan + UBSan" OFF)

add_library(gpufab_warnings INTERFACE)
target_compile_options(gpufab_warnings INTERFACE -Wall -Wextra -Wpedantic -Werror)

add_library(gpufab_core
  src/gpufab/config.cpp
  src/gpufab/engine.cpp
  src/gpufab/router/ecmp.cpp
  src/gpufab/telemetry.cpp
  src/gpufab/topology.cpp
)
target_include_directories(gpufab_core PUBLIC src)
target_link_libraries(gpufab_core PUBLIC gpufab_warnings)

if(GPUFAB_SANITIZE)
  target_compile_options(gpufab_core PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(gpufab_core PUBLIC -fsanitize=address,undefined)
endif()

add_executable(gpufab_sim src/main.cpp)
target_link_libraries(gpufab_sim PRIVATE gpufab_core)

enable_testing()
foreach(t test_config test_rng test_topology test_engine_fct test_congestion)
  add_executable(${t} tests/${t}.cpp)
  target_link_libraries(${t} PRIVATE gpufab_core)
  add_test(NAME ${t} COMMAND ${t})
endforeach()
