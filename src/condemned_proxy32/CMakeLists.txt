set(CONDEMNEDVR_MINHOOK_ROOT
  "${CMAKE_SOURCE_DIR}/vendor-local/minhook"
  CACHE PATH "Local pinned checkout of MinHook")
if(NOT EXISTS "${CONDEMNEDVR_MINHOOK_ROOT}/CMakeLists.txt")
  message(FATAL_ERROR
    "MinHook missing at ${CONDEMNEDVR_MINHOOK_ROOT}. "
    "Run tools/prepare-dependencies.ps1 first.")
endif()
if(NOT TARGET minhook)
  add_subdirectory(
    "${CONDEMNEDVR_MINHOOK_ROOT}"
    "${CMAKE_BINARY_DIR}/third_party/condemned-minhook"
    EXCLUDE_FROM_ALL
  )
endif()

add_library(condemnedvr-d3d9-diagnostic SHARED
  diagnostic_bridge.cpp
  dllmain.cpp
)
target_compile_definitions(condemnedvr-d3d9-diagnostic PRIVATE
  WIN32_LEAN_AND_MEAN
  NOMINMAX
)
target_link_libraries(condemnedvr-d3d9-diagnostic PRIVATE
  condemnedvr-module-identity
  fearvr-warnings
  minhook
)
set_target_properties(condemnedvr-d3d9-diagnostic PROPERTIES
  OUTPUT_NAME "condemnedvr-d3d9-diagnostic"
  PREFIX ""
)

add_library(condemnedvr-d3d9-bridge SHARED
  transport_entry.cpp
  transport_exports.cpp
  dllmain.cpp
  ../proxy32/bridge.cpp
  ../proxy32/system_d3d9.cpp
)
target_include_directories(condemnedvr-d3d9-bridge PRIVATE
  ${CMAKE_SOURCE_DIR}/src/proxy32
)
target_compile_definitions(condemnedvr-d3d9-bridge PRIVATE
  CONDEMNEDVR_PRODUCT
  WIN32_LEAN_AND_MEAN
  NOMINMAX
)
target_link_libraries(condemnedvr-d3d9-bridge PRIVATE
  condemnedvr-module-identity
  fearvr-common
  fearvr-version
  fearvr-warnings
  minhook
  d3dcompiler
  dxgi
  shell32
)
set_target_properties(condemnedvr-d3d9-bridge PROPERTIES
  OUTPUT_NAME "condemnedvr-d3d9"
  PREFIX ""
)

if(CONDEMNEDVR_BUILD_TESTS)
  add_executable(test-condemned-bridge-guard
    test_bridge_guard.cpp
  )
  target_compile_definitions(test-condemned-bridge-guard PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
  )
  target_link_libraries(test-condemned-bridge-guard PRIVATE
    fearvr-warnings
  )
  add_dependencies(
    test-condemned-bridge-guard
    condemnedvr-d3d9-bridge
    condemnedvr-d3d9-diagnostic
  )
  add_test(
    NAME condemned_bridge_version_guard
    COMMAND test-condemned-bridge-guard
      $<TARGET_FILE:condemnedvr-d3d9-bridge>
  )
endif()
