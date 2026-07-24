cmake_minimum_required(VERSION 3.16)
project(husk CXX)

# Nix provides ccache in the dev shell and sets CCACHE_DIR; use it if present
# rather than assuming it's on PATH outside that shell.
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
  set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
  set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(TinyGLTF CONFIG REQUIRED)

# Everything except main.cpp, as a library: lets the test binary link against
# the exact same command/parsing code the real CLI runs, instead of
# re-including sources or testing a copy.
add_library(husk-lib STATIC
  src/chunk.cpp
  src/m2.cpp
  src/skin.cpp
  src/gltf.cpp
  src/cmd_info.cpp
  src/cmd_export.cpp
)
target_include_directories(husk-lib PUBLIC src)
target_link_libraries(husk-lib PUBLIC tinygltf::tinygltf)

add_executable(husk src/main.cpp)
target_link_libraries(husk PRIVATE husk-lib)

option(HUSK_BUILD_TESTS "Build the doctest suite" ON)
if(HUSK_BUILD_TESTS)
  find_package(doctest CONFIG REQUIRED)
  include(doctest)
  enable_testing()

  add_executable(husk-tests
    tests/test_main.cpp
    tests/test_chunk.cpp
    tests/test_m2.cpp
    tests/test_skin.cpp
    tests/test_gltf.cpp
    tests/test_integration.cpp
  )
  target_link_libraries(husk-tests PRIVATE husk-lib doctest::doctest)
  target_compile_definitions(husk-tests PRIVATE
    HUSK_BINARY="$<TARGET_FILE:husk>"
  )
  add_dependencies(husk-tests husk)
  doctest_discover_tests(husk-tests)
endif()
