cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if ("${CMAKE_SYSTEM_NAME}" STREQUAL "iOS" OR IOS)
    set(CMAKE_OSX_ARCHITECTURES "arm64")
else()
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
endif()
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

project(ClickBetweenFrames VERSION 1.0.0)

add_library(${PROJECT_NAME} SHARED
    "src/main.cpp"
)

if (WIN32)
    target_sources(${PROJECT_NAME} PRIVATE src/windows.cpp)
else()
    if(${CMAKE_SYSTEM_NAME} STREQUAL "Android")
        target_sources(${PROJECT_NAME} PRIVATE src/android.cpp)
    elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Darwin" OR ${CMAKE_SYSTEM_NAME} STREQUAL "iOS")
        target_sources(${PROJECT_NAME} PRIVATE src/apple.mm)
        set_source_files_properties(src/apple.mm PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
    endif()
    target_sources(${PROJECT_NAME} PRIVATE src/notwindows.cpp)
endif()

if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Unable to find Geode SDK! Please define GEODE_SDK environment variable to point to Geode")
else()
    message(STATUS "Found Geode: $ENV{GEODE_SDK}")
endif()

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)

setup_geode_mod(${PROJECT_NAME})
