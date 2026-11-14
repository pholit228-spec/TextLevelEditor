cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "" FORCE)
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

project(TextLevel VERSION 1.0.0)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp)
add_library(${PROJECT_NAME} SHARED ${SOURCES})

if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Unable to find Geode SDK! Please define GEODE_SDK environment variable to point to Geode")
else()
    message(STATUS "Found Geode: $ENV{GEODE_SDK}")
endif()

# Keep Geode's fmt consteval workaround enabled.
set(GEODE_DISABLE_FMT_CONSTEVAL ON CACHE BOOL "Disable fmt consteval checks for cross-compilation" FORCE)

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)

# fmt 10.2.1 fails to compile with Clang 21 in C++20 mode (FMT_STRING
# consteval diagnostics). Compile only the fmt dependency as C++17; the mod
# itself and Geode targets retain the project's C++20 setting.
if(TARGET fmt)
    set_target_properties(fmt PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
    )
endif()

setup_geode_mod(${PROJECT_NAME})
