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

add_library(condemnedvr-module-identity STATIC
  module_identity.cpp
  module_identity.h
)
target_compile_definitions(condemnedvr-module-identity PRIVATE
  WIN32_LEAN_AND_MEAN
  NOMINMAX
)
target_include_directories(condemnedvr-module-identity PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(condemnedvr-module-identity PRIVATE
  bcrypt
  fearvr-warnings
)

add_library(condemnedvr-gameclient-loader SHARED
  GameClient.def
  binding_input.cpp
  binding_input.h
  dllmain.cpp
  renderer_probe.cpp
  renderer_probe.h
)
target_compile_definitions(condemnedvr-gameclient-loader PRIVATE
  WIN32_LEAN_AND_MEAN
  NOMINMAX
)
target_link_libraries(condemnedvr-gameclient-loader PRIVATE
  condemnedvr-module-identity
  fearvr-common
  fearvr-warnings
  minhook
)
set_target_properties(condemnedvr-gameclient-loader PROPERTIES
  OUTPUT_NAME "GameClient"
  PREFIX ""
)

add_executable(verify-condemned-gameclient
  verify_condemned_gameclient.cpp
)
target_compile_definitions(verify-condemned-gameclient PRIVATE
  WIN32_LEAN_AND_MEAN
  NOMINMAX
)
target_link_libraries(verify-condemned-gameclient PRIVATE
  condemnedvr-module-identity
  fearvr-warnings
)

add_executable(verify-condemned-executable
  verify_condemned_executable.cpp
)
target_compile_definitions(verify-condemned-executable PRIVATE
  WIN32_LEAN_AND_MEAN
  NOMINMAX
)
target_link_libraries(verify-condemned-executable PRIVATE
  condemnedvr-module-identity
  fearvr-warnings
)

if(FEARVR_BUILD_TESTS)
  add_executable(test-condemned-module-identity
    test_module_identity.cpp
  )
  target_compile_definitions(test-condemned-module-identity PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
  )
  target_link_libraries(test-condemned-module-identity PRIVATE
    condemnedvr-module-identity
    fearvr-warnings
  )
  add_test(
    NAME condemned_module_identity
    COMMAND test-condemned-module-identity
  )

  add_executable(test-condemned-loader-fail-closed
    test_loader_fail_closed.cpp
  )
  target_compile_definitions(test-condemned-loader-fail-closed PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
  )
  target_link_libraries(test-condemned-loader-fail-closed PRIVATE
    fearvr-warnings
  )
  add_dependencies(
    test-condemned-loader-fail-closed
    condemnedvr-gameclient-loader
  )
  add_test(
    NAME condemned_loader_fail_closed
    COMMAND test-condemned-loader-fail-closed
      $<TARGET_FILE:condemnedvr-gameclient-loader>
  )
endif()
