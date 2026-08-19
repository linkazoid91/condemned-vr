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

add_library(condemnedvr-weapon-settings-store STATIC
  weapon_settings_store.cpp
  weapon_settings_store.h
)
target_compile_definitions(condemnedvr-weapon-settings-store PRIVATE
  WIN32_LEAN_AND_MEAN
  NOMINMAX
)
target_link_libraries(condemnedvr-weapon-settings-store
  PUBLIC fearvr-common
  PRIVATE
  fearvr-warnings
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
  arm_ik_discovery.cpp
  arm_ik_discovery.h
  arm_ik_integration.cpp
  arm_ik_integration.h
  binding_input.cpp
  binding_input.h
  dllmain.cpp
  renderer_probe.cpp
  renderer_probe.h
  retail_menu_integration.cpp
  retail_menu_integration.h
  weapon_identity_reader.cpp
  weapon_identity_reader.h
)
target_compile_definitions(condemnedvr-gameclient-loader PRIVATE
  WIN32_LEAN_AND_MEAN
  NOMINMAX
)
target_link_libraries(condemnedvr-gameclient-loader PRIVATE
  condemnedvr-module-identity
  condemnedvr-weapon-settings-store
  fearvr-common
  fearvr-warnings
  minhook
)
set_target_properties(condemnedvr-gameclient-loader PROPERTIES
  OUTPUT_NAME "GameClient"
  PREFIX ""
)

set(CONDEMNEDVR_DEFAULT_SETTINGS_FILE
  "${CMAKE_SOURCE_DIR}/config/condemnedvr-defaults.ini"
)
if(NOT EXISTS "${CONDEMNEDVR_DEFAULT_SETTINGS_FILE}")
  message(FATAL_ERROR
    "Packaged defaults missing at ${CONDEMNEDVR_DEFAULT_SETTINGS_FILE}.")
endif()
add_custom_command(TARGET condemnedvr-gameclient-loader POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${CONDEMNEDVR_DEFAULT_SETTINGS_FILE}"
    "$<TARGET_FILE_DIR:condemnedvr-gameclient-loader>/condemnedvr-defaults.ini"
  VERBATIM
)
set_property(TARGET condemnedvr-gameclient-loader APPEND PROPERTY
  LINK_DEPENDS "${CONDEMNEDVR_DEFAULT_SETTINGS_FILE}"
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

if(CONDEMNEDVR_BUILD_TESTS)
  add_executable(test-condemned-weapon-settings-store
    test_weapon_settings_store.cpp
  )
  target_compile_definitions(
    test-condemned-weapon-settings-store PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
  )
  target_link_libraries(
    test-condemned-weapon-settings-store PRIVATE
    condemnedvr-weapon-settings-store
    fearvr-warnings
  )
  add_dependencies(
    test-condemned-weapon-settings-store
    condemnedvr-gameclient-loader
  )
  add_test(
    NAME condemned_weapon_settings_store
    COMMAND test-condemned-weapon-settings-store
  )

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
