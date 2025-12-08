cmake_minimum_required(VERSION 3.10)
project(mirrors VERSION 1.0)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Compiler optimizations
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -march=native -Wall -Wextra")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -msse2")  # Enable SSE2 SIMD

# Find required packages
find_package(X11 REQUIRED)
find_package(Threads REQUIRED)

# Check for required X11 extensions
if(NOT X11_Xext_LIB)
    message(FATAL_ERROR "Xext library not found (required for XShm)")
endif()

if(NOT X11_Xtst_LIB)
    message(FATAL_ERROR "Xtst library not found (required for XTest)")
endif()

# Source files
set(SOURCES
    src/main.cpp
    src/capture.cpp
    src/renderer.cpp
    src/input.cpp
)

# Create executable
add_executable(mirrors ${SOURCES})

# Include directories
target_include_directories(mirrors PRIVATE ${X11_INCLUDE_DIR})

# Link libraries
target_link_libraries(mirrors 
    ${X11_LIBRARIES}
    ${X11_Xext_LIB}
    ${X11_Xtst_LIB}
    ${X11_Xdamage_LIB}
    Xfixes
    Threads::Threads
)

# Window Manager
add_executable(mirrors-wm src/wm.cpp)
target_link_libraries(mirrors-wm ${X11_LIBRARIES})

# Installation
install(TARGETS mirrors DESTINATION bin)

# Print build info
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Compiler flags: ${CMAKE_CXX_FLAGS}")
message(STATUS "X11 libraries: ${X11_LIBRARIES}")
