cmake_minimum_required(VERSION 3.16)

project(superlotis_camera
    VERSION 0.1.0
    DESCRIPTION "SuperLOTIS camera control server for the Teledyne PI Sophia 2048B-UV"
    LANGUAGES CXX)

option(SUPERLOTIS_WITH_PICAM "Build the PICam hardware backend (requires the PICam SDK)" OFF)
option(SUPERLOTIS_BUILD_TESTS "Build the unit and integration tests" ON)
option(SUPERLOTIS_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(SUPERLOTIS_WERROR "Treat compiler warnings as errors" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

include(GNUInstallDirs)

add_subdirectory(server)

if(SUPERLOTIS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(server/tests)
endif()
