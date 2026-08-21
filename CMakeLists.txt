cmake_minimum_required(VERSION 3.16)
project(husk CXX)

# HUSK_VERSION: single source of truth is nix/flake.nix's own HUSK_VERSION
# let-binding, passed down as the HUSK_ENV_VERSION env var (packages.default
# sets it directly on the derivation). That env var wins whenever it's set --
# the nix build's own sandbox has no .git (appSource's fileset deliberately
# excludes it) and no guaranteed git binary either (nativeBuildInputs is
# cmake+ninja only, deliberately not git, since a client machine building
# this package can't be trusted to have git available/on PATH), so this
# path never shells out to anything. `git describe` (dirty-tree suffix
# included, so a build from uncommitted changes is visibly not a clean
# checkout) is only attempted as a dev-convenience fallback for a plain
# manual `cmake` invocation outside the flake, where HUSK_ENV_VERSION is
# unset; "unknown" is the last resort if neither is available. Resolved
# once here at configure time, not re-run live (~/docs/CLI.md's "no inline
# computation" rule, applied to the build system instead of a shell
# command), so `husk --version` can say which build produced a given .glb.
# Matters for a format that moves under this tool constantly (see
# README.md's "Why test-first") -- a bug report is only as useful as
# knowing which husk build it came from.
if(DEFINED ENV{HUSK_ENV_VERSION} AND NOT "$ENV{HUSK_ENV_VERSION}" STREQUAL "")
  set(HUSK_VERSION "$ENV{HUSK_ENV_VERSION}")
else()
  set(HUSK_VERSION "unknown")
  find_package(Git QUIET)
  if(Git_FOUND)
    execute_process(
      COMMAND ${GIT_EXECUTABLE} describe --always --dirty
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
      OUTPUT_VARIABLE HUSK_VERSION_OUTPUT
      OUTPUT_STRIP_TRAILING_WHITESPACE
      RESULT_VARIABLE HUSK_VERSION_RESULT
      ERROR_QUIET
    )
    if(HUSK_VERSION_RESULT EQUAL 0 AND HUSK_VERSION_OUTPUT)
      set(HUSK_VERSION "${HUSK_VERSION_OUTPUT}")
    endif()
  endif()
endif()
message(STATUS "HUSK_VERSION: ${HUSK_VERSION}")

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
find_package(SQLite3 REQUIRED)

# Everything except main.cpp, as a library: lets the test binary link against
# the exact same command/parsing code the real CLI runs, instead of
# re-including sources or testing a copy.
add_library(husk-lib STATIC
  src/chunk.cpp
  src/m2_primitives.cpp
  src/m2_header.cpp
  src/m2_skeleton.cpp
  src/m2_animation.cpp
  src/m2_shader_names.cpp
  src/m2_scene.cpp
  src/skin.cpp
  src/skel.cpp
  src/bone.cpp
  src/phys.cpp
  src/blp.cpp
  src/db2.cpp
  src/dbd.cpp
  src/listfile.cpp
  src/husk_config.cpp
  src/db2table.cpp
  src/chrmodel_db2.cpp
  src/chrcustomization_db2.cpp
  src/texturefiledata_db2.cpp
  src/creature_geoset_db2.cpp
  src/chrrace_db2.cpp
  src/animationdata_db2.cpp
  src/cmd_db2.cpp
  src/cmd_db2_build.cpp
  src/cmd_blp.cpp
  src/appearance_string.cpp
  src/cmd_appearance.cpp
  src/gltf_math.cpp
  src/gltf_mesh.cpp
  src/gltf_skeleton.cpp
  src/gltf.cpp
  src/cmd_info.cpp
  src/export_transform.cpp
  src/export_skeleton.cpp
  src/export_animation.cpp
  src/export_texture_resolution.cpp
  src/export_materials.cpp
  src/export_skin_resolution.cpp
  src/export_extras.cpp
  src/cmd_export.cpp
  src/dump_writer_utils.cpp
  src/dump_emitters.cpp
  src/dump_phys.cpp
  src/dump_chunks_misc.cpp
  src/cmd_dump.cpp
)
target_include_directories(husk-lib PUBLIC src)
target_link_libraries(husk-lib PUBLIC tinygltf::tinygltf CLI11::CLI11 SQLite::SQLite3)

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
    tests/test_m2_animation_tracks.cpp
    tests/test_m2_shader_names.cpp
    tests/test_m2_scene.cpp
    tests/test_skin.cpp
    tests/test_skel.cpp
    tests/test_bone.cpp
    tests/test_phys.cpp
    tests/test_blp.cpp
    tests/test_db2.cpp
    tests/test_db2_offsetmap.cpp
    tests/test_dbd.cpp
    tests/test_listfile.cpp
    tests/test_db2table.cpp
    tests/test_cli_db2.cpp
    tests/test_cli_blp_export.cpp
    tests/test_cli_chrmodel.cpp
    tests/test_cli_chrcustomization.cpp
    tests/test_cli_animationdata.cpp
    tests/test_cli_chrmodel_id.cpp
    tests/test_gltf.cpp
    tests/test_gltf_math.cpp
    tests/test_gltf_mesh.cpp
    tests/test_gltf_skeleton.cpp
    tests/test_export_skeleton.cpp
    tests/test_integration.cpp
    tests/test_integration_weapons.cpp
    tests/test_integration_texture_transform.cpp
    tests/test_integration_lights.cpp
    tests/test_cli.cpp
    tests/test_cli_textures.cpp
    tests/test_cli_batch.cpp
    tests/test_cli_config.cpp
    tests/test_cli_appearance.cpp
    tests/test_cli_info.cpp
    tests/test_cli_errors.cpp
    tests/test_cli_argv.cpp
    tests/test_cli_skin.cpp
    tests/test_cli_anim.cpp
    tests/test_cli_anim_skel.cpp
    tests/test_cli_skel.cpp
    tests/test_cli_phys.cpp
    tests/test_dump.cpp
    tests/test_dump_emitters.cpp
    tests/test_dump_phys.cpp
    tests/test_dump_chunks_misc.cpp
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
    # Same "bake absolute at configure time" discipline as HUSK_TEST_DATA_DIR
    # above, but for reference/WoWDBDefs -- a dev-only investigation checkout
    # (gitignored, optional, never a hard dependency), not a test_data/-style
    # per-model WoW extraction fixture, hence its own separate macro rather
    # than routing through test_data_paths.hpp's test_data/-relative resolve().
    HUSK_TEST_DBD_DIR="${CMAKE_CURRENT_SOURCE_DIR}/reference/WoWDBDefs"
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
