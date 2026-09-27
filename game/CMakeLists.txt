# ---- raylib (downloaded automatically) -------------------------------------
# Offline? Pass -DFETCHCONTENT_SOURCE_DIR_RAYLIB=<path to a raylib checkout>.
include(FetchContent)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES    OFF CACHE BOOL "" FORCE)
set(CUSTOMIZE_BUILD OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG        5.5
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(raylib)

# ---- the game ------------------------------------------------------------------
add_executable(shooter
    src/main.cpp
    src/game.cpp
    src/render.cpp
    src/audio.cpp
    src/memory_export.cpp
)
target_include_directories(shooter PRIVATE src ${PROJECT_SOURCE_DIR}/shared)
target_link_libraries(shooter PRIVATE raylib)

if(MSVC)
    target_compile_options(shooter PRIVATE /W4 /wd4100)
else()
    target_compile_options(shooter PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
endif()

if(UNIX AND NOT APPLE)
    target_link_libraries(shooter PRIVATE ${CMAKE_DL_LIBS})
endif()

if(MINGW)
    # Self-contained exe (no libstdc++/libgcc DLLs needed next to it).
    target_link_options(shooter PRIVATE -static)
endif()
