set(CONDEMNEDVR_OPENXR_SDK_ROOT
    "${CMAKE_SOURCE_DIR}/vendor-local/openxr-sdk"
    CACHE PATH "Local pinned checkout of the Khronos OpenXR-SDK")

if(NOT EXISTS "${CONDEMNEDVR_OPENXR_SDK_ROOT}/CMakeLists.txt")
  message(FATAL_ERROR
    "OpenXR-SDK missing at ${CONDEMNEDVR_OPENXR_SDK_ROOT}. "
    "Run tools/prepare-dependencies.ps1 first.")
endif()

set(BUILD_LOADER ON CACHE BOOL "" FORCE)
set(DYNAMIC_LOADER OFF CACHE BOOL "" FORCE)
set(BUILD_API_LAYERS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_CONFORMANCE_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_SDK_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_ALL_EXTENSIONS OFF CACHE BOOL "" FORCE)
set(INSTALL_SYMBOLS OFF CACHE BOOL "" FORCE)
add_subdirectory(
  "${CONDEMNEDVR_OPENXR_SDK_ROOT}"
  "${CMAKE_BINARY_DIR}/third_party/openxr-sdk"
  EXCLUDE_FROM_ALL)

add_executable(condemnedvr-host
  ../host64/ipc_bridge.cpp
  ../host64/ipc_bridge.h
  ../host64/main.cpp
  ../host64/openxr_host.cpp
  ../host64/openxr_host.h
  ../host64/xr_input.cpp
  ../host64/xr_input.h
  ../host64/texture_renderer.cpp
  ../host64/texture_renderer.h
)
target_include_directories(condemnedvr-host PRIVATE
  ${CMAKE_SOURCE_DIR}/src/host64
)
target_compile_definitions(condemnedvr-host PRIVATE
  CONDEMNEDVR_PRODUCT
  XR_USE_PLATFORM_WIN32
  XR_USE_GRAPHICS_API_D3D11
  WIN32_LEAN_AND_MEAN
  NOMINMAX
)
target_link_libraries(condemnedvr-host PRIVATE
  fearvr-common
  fearvr-version
  fearvr-warnings
  OpenXR::openxr_loader
  d3d11
  d3dcompiler
  dxgi
)
set_target_properties(condemnedvr-host PROPERTIES
  OUTPUT_NAME "condemnedvr-host"
)
