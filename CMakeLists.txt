cmake_minimum_required(VERSION 3.16)
project(husk CXX)

# HUSK_VERSION: `git describe` (dirty-tree suffix included, so a build from
# uncommitted changes is visibly not a clean checkout) if this is a git
# checkout with git available, else "unknown" -- resolved once here at
# configure time, not re-run live (~/docs/CLI.md's "no inline computation"
# rule, applied to the build system instead of a shell command), so `husk
# --version` can say which build produced a given .glb. Matters for a
# format that moves under this tool constantly (see README.md's "Why
# test-first") -- a bug report is only as useful as knowing which husk
# build it came from.
find_package(Git QUIET)
set(HUSK_VERSION "unknown")
if(Git_FOUND)
  execute_process(
    COMMAND ${GIT_EXECUTABLE} describe --always --dirty
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE HUSK_VERSION_OUTPUT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE HUSK_VERSION_RESULT
  )
  if(HUSK_VERSION_RESULT EQUAL 0 AND HUSK_VERSION_OUTPUT)
    set(HUSK_VERSION "${HUSK_VERSION_OUTPUT}")
  endif()
endif()

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
find_package(CLI11 CONFIG REQUIRED)

# Everything except main.cpp, as a library: lets the test binary link against
# the exact same command/parsing code the real CLI runs, instead of
# re-including sources or testing a copy.
add_library(husk-lib STATIC
  src/chunk.cpp
  src/m2_primitives.cpp
  src/m2_header.cpp
  src/m2_skeleton.cpp
  src/m2_animation.cpp
  src/m2_scene.cpp
  src/skin.cpp
  src/skel.cpp
  src/bone.cpp
  src/phys.cpp
  src/gltf_math.cpp
  src/gltf_mesh.cpp
  src/gltf_skeleton.cpp
  src/gltf.cpp
  src/cmd_info.cpp
  src/export_transform.cpp
  src/export_skeleton.cpp
  src/export_animation.cpp
  src/export_materials.cpp
  src/export_skin_resolution.cpp
  src/cmd_export.cpp
  src/dump_writer_utils.cpp
  src/dump_emitters.cpp
  src/dump_phys.cpp
  src/dump_chunks_misc.cpp
  src/cmd_dump.cpp
)
target_include_directories(husk-lib PUBLIC src)
target_link_libraries(husk-lib PUBLIC tinygltf::tinygltf CLI11::CLI11)

add_executable(husk src/main.cpp)
target_link_libraries(husk PRIVATE husk-lib)
target_compile_definitions(husk PRIVATE HUSK_VERSION="${HUSK_VERSION}")

option(HUSK_BUILD_TESTS "Build the doctest suite" ON)
if(HUSK_BUILD_TESTS)
  find_package(doctest CONFIG REQUIRED)
  include(doctest)
  enable_testing()

  add_executable(husk-tests
    tests/test_main.cpp
    tests/test_chunk.cpp
    tests/test_m2_primitives.cpp
    tests/test_m2_header.cpp
    tests/test_m2_skeleton.cpp
    tests/test_m2_animation.cpp
    tests/test_m2_scene.cpp
    tests/test_skin.cpp
    tests/test_skel.cpp
    tests/test_bone.cpp
    tests/test_phys.cpp
    tests/test_gltf.cpp
    tests/test_gltf_math.cpp
    tests/test_gltf_mesh.cpp
    tests/test_gltf_skeleton.cpp
    tests/test_integration.cpp
    tests/test_cli.cpp
    tests/test_dump.cpp
    tests/test_conformance.cpp
  )
  target_link_libraries(husk-tests PRIVATE husk-lib doctest::doctest)
  target_compile_definitions(husk-tests PRIVATE
    HUSK_BINARY="$<TARGET_FILE:husk>"
    # Absolute by construction (see CLAUDE.md's ctest-runs-from-build/ hazard
    # note) -- tests/test_data_paths.hpp's resolve() checks this at runtime,
    # so a missing (gitignored, user-populated) test_data/ just means every
    # real-data test skips, not a configure-time error.
    HUSK_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test_data"
  )
  add_dependencies(husk-tests husk)

  # Both optional: exercise real downstream consumers of exported .glb files
  # (tests/test_conformance.cpp) when present in the dev shell, skip
  # gracefully at runtime when not -- same convention as the real-data env
  # vars in tests/test_integration.cpp.
  find_program(HUSK_GLTF_VALIDATOR_PROGRAM gltf_validator)
  if(HUSK_GLTF_VALIDATOR_PROGRAM)
    target_compile_definitions(husk-tests PRIVATE
      HUSK_GLTF_VALIDATOR="${HUSK_GLTF_VALIDATOR_PROGRAM}"
    )
  endif()
  find_program(HUSK_BLENDER_PROGRAM blender)
  if(HUSK_BLENDER_PROGRAM)
    target_compile_definitions(husk-tests PRIVATE
      HUSK_BLENDER="${HUSK_BLENDER_PROGRAM}"
      HUSK_BLENDER_IMPORT_SCRIPT="${CMAKE_CURRENT_SOURCE_DIR}/tests/blender_import_check.py"
    )
  endif()

  doctest_discover_tests(husk-tests)
endif()
