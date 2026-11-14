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

# Disable fmt's consteval format-string checks for Clang cross-compilation.
# GEODE_DISABLE_FMT_CONSTEVAL alone only sets FMT_CONSTEVAL; newer fmt versions
# select that macro through FMT_USE_CONSTEVAL, so disable the feature explicitly.
set(GEODE_DISABLE_FMT_CONSTEVAL ON CACHE BOOL "Disable fmt consteval checks for cross-compilation" FORCE)
add_compile_definitions(FMT_USE_CONSTEVAL=0)

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)

setup_geode_mod(${PROJECT_NAME})
