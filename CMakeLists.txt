cmake_minimum_required(VERSION 3.21)
project(ClickBetweenFrames VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_ARCHITECTURES "x86_64")
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

# Set compiler and linker flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -flto=thin")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fuse-ld=lld-link")

# Set up the mod binary
add_library(${PROJECT_NAME} SHARED
    "src/main.cpp"
    # Add your cpp files here
)

# Check for Geode SDK environment variable
if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Unable to find Geode SDK! Please define GEODE_SDK environment variable to point to Geode")
else()
    message(STATUS "Found Geode: $ENV{GEODE_SDK}")
endif()

# Include Geode SDK and dependencies
add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)

# Link required libraries using keyword signature
target_link_libraries(${PROJECT_NAME}
    PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}/geode-deps/geode.custom-keybinds/geode.custom-keybinds.lib
    D:/a/Click-Between-Frames/Click-Between-Frames/geode-sdk-clone/loader/include/link/win64/libcocos2d.lib
    D:/a/Click-Between-Frames/Click-Between-Frames/geode-sdk-clone/loader/include/link/win64/libExtensions.lib
    D:/a/Click-Between-Frames/Click-Between-Frames/geode-sdk-clone/loader/include/link/win64/ssl.lib
)

# Link Geode libraries using keyword signature
target_link_libraries(${PROJECT_NAME} PRIVATE Geode::Core Geode::Extensions Geode::CustomKeybinds)

# Setup Geode mod (assuming it also uses keyword signature)
setup_geode_mod(${PROJECT_NAME})
