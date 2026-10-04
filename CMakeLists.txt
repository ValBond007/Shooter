cmake_minimum_required(VERSION 3.16)
project(ArenaShooter LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(BUILD_GAME   "Build the game (shooter)"               ON)
option(BUILD_AIMBOT "Build the external reader / aimbot"     ON)

# Put all executables into <build>/bin (and bin/<Config> for Visual Studio).
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

if(MSVC)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

if(BUILD_GAME)
    add_subdirectory(game)
endif()
if(BUILD_AIMBOT)
    add_subdirectory(aimbot)
endif()
