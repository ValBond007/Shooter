add_executable(aimbot
    src/main.cpp
    src/config.cpp
    src/aimbot.cpp
    src/game_reader.cpp
    src/memory/memory.cpp
    src/memory/memory_dma.cpp
    src/memory/memory_winapi.cpp
    src/memory/memory_linux.cpp
    src/input/mouse.cpp
    src/input/mouse_kmbox_b.cpp
    src/input/mouse_kmbox_net.cpp
    src/input/mouse_sendinput.cpp
    src/input/mouse_x11.cpp
)
target_include_directories(aimbot PRIVATE src ${PROJECT_SOURCE_DIR}/shared)

if(MSVC)
    target_compile_options(aimbot PRIVATE /W4)
else()
    target_compile_options(aimbot PRIVATE -Wall -Wextra)
endif()

if(WIN32)
    target_link_libraries(aimbot PRIVATE winmm ws2_32)
endif()
if(MINGW)
    target_link_options(aimbot PRIVATE -static)
endif()

# ---- optional: KMBox Net vendor library -----------------------------------------
# Copy kmboxNet.cpp / kmboxNet.h (+ any other files it needs) from the KMBox
# vendor into aimbot/third_party/kmboxnet/ and re-run CMake.
set(KMBOXNET_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/kmboxnet)
if(EXISTS ${KMBOXNET_DIR}/kmboxNet.h)
    file(GLOB KMBOXNET_SOURCES ${KMBOXNET_DIR}/*.cpp ${KMBOXNET_DIR}/*.c)
    target_sources(aimbot PRIVATE ${KMBOXNET_SOURCES})
    target_include_directories(aimbot PRIVATE ${KMBOXNET_DIR})
    target_compile_definitions(aimbot PRIVATE AIMBOT_HAVE_KMBOXNET)
    message(STATUS "aimbot: KMBox Net support enabled (${KMBOXNET_DIR})")
else()
    message(STATUS "aimbot: KMBox Net library not found in ${KMBOXNET_DIR} (optional)")
endif()

# ---- optional: X11/XTest mouse output for testing on Linux ------------------------
if(UNIX AND NOT APPLE)
    find_package(X11)
    if(X11_FOUND AND X11_XTest_FOUND)
        target_compile_definitions(aimbot PRIVATE AIMBOT_HAVE_X11)
        target_link_libraries(aimbot PRIVATE ${X11_LIBRARIES} ${X11_XTest_LIB})
    endif()
endif()

# config.ini next to the executable
add_custom_command(TARGET aimbot POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/config.ini $<TARGET_FILE_DIR:aimbot>/config.ini)
