# Condemned VR builds x86 game-side modules and an x64 OpenXR host in separate
# build trees. The retained fearvr-* target and macro names are compatibility
# identifiers inherited from the MIT-licensed upstream implementation.
cmake_minimum_required(VERSION 3.21)

project(CondemnedVr
  VERSION 0.5.0
  DESCRIPTION "OpenXR VR mod for Condemned: Criminal Origins"
  LANGUAGES C CXX)

set(FEARVR_VERSION_LABEL "dev")

option(CONDEMNEDVR_BUILD_M1_LOADER
  "Build the x86 Condemned stock-client loader" OFF)
option(CONDEMNEDVR_BUILD_M2_BRIDGE
  "Build the x86 Condemned D3D9 bridge" OFF)
option(CONDEMNEDVR_BUILD_HOST
  "Build the x64 Condemned OpenXR host" OFF)
option(CONDEMNEDVR_BUILD_TESTS
  "Build automated tests" ON)

if(NOT WIN32 OR NOT MSVC)
  message(FATAL_ERROR
    "Condemned VR builds only with MSVC on Windows.")
endif()

# A build directory targets one architecture. Never combine the x86 game-side
# modules and x64 OpenXR host in the same CMake tree.
if(CONDEMNEDVR_BUILD_M1_LOADER AND NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR
    "CONDEMNEDVR_BUILD_M1_LOADER requires an x86 build (-A Win32).")
endif()
if(CONDEMNEDVR_BUILD_M2_BRIDGE AND NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR
    "CONDEMNEDVR_BUILD_M2_BRIDGE requires an x86 build (-A Win32).")
endif()
if(CONDEMNEDVR_BUILD_M2_BRIDGE AND NOT CONDEMNEDVR_BUILD_M1_LOADER)
  message(FATAL_ERROR
    "CONDEMNEDVR_BUILD_M2_BRIDGE requires CONDEMNEDVR_BUILD_M1_LOADER.")
endif()
if(CONDEMNEDVR_BUILD_HOST AND NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR
    "CONDEMNEDVR_BUILD_HOST requires an x64 build (-A x64).")
endif()

if(NOT CONDEMNEDVR_BUILD_M1_LOADER AND
   NOT CONDEMNEDVR_BUILD_M2_BRIDGE AND
   NOT CONDEMNEDVR_BUILD_HOST AND
   NOT CONDEMNEDVR_BUILD_TESTS)
  message(FATAL_ERROR "No Condemned VR target is enabled.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

# Keep debug symbols local even for release-like configurations.
add_compile_options($<$<CONFIG:Release>:/Zi>)
add_link_options($<$<CONFIG:Release>:/DEBUG>)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/fearvr-warnings.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/fearvr-version.cmake)

# Header-only x86/x64 protocol and shared math. The target name is retained to
# avoid a risky protocol-wide rename during the port.
add_library(fearvr-common INTERFACE)
target_include_directories(fearvr-common INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/src/common)

message(STATUS
  "CondemnedVr ${PROJECT_VERSION}-${FEARVR_VERSION_LABEL} | "
  "ptr=${CMAKE_SIZEOF_VOID_P}*8 bit | "
  "LOADER=${CONDEMNEDVR_BUILD_M1_LOADER} "
  "BRIDGE=${CONDEMNEDVR_BUILD_M2_BRIDGE} "
  "HOST=${CONDEMNEDVR_BUILD_HOST} "
  "TESTS=${CONDEMNEDVR_BUILD_TESTS}")

if(CONDEMNEDVR_BUILD_TESTS)
  enable_testing()
endif()
if(CONDEMNEDVR_BUILD_HOST)
  add_subdirectory(src/condemned_host64)
endif()
if(CONDEMNEDVR_BUILD_M1_LOADER)
  add_subdirectory(src/condemned_gameclient_loader)
endif()
if(CONDEMNEDVR_BUILD_M2_BRIDGE)
  add_subdirectory(src/condemned_proxy32)
endif()
if(CONDEMNEDVR_BUILD_TESTS)
  add_subdirectory(tests)
endif()
