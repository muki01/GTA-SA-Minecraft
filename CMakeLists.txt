cmake_minimum_required(VERSION 3.21)
project(MinecraftSA C CXX)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "GTA SA is a 32-bit game: configure with -A Win32")
endif()

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# ---------------------------------------------------------------- plugin-sdk (static lib)
set(SDK ${CMAKE_SOURCE_DIR}/third_party/plugin-sdk)
file(GLOB_RECURSE SDK_SOURCES
    ${SDK}/plugin_sa/*.cpp
    ${SDK}/shared/*.cpp
    ${SDK}/hooking/*.cpp
    ${SDK}/safetyhook/*.cpp
    ${SDK}/safetyhook/*.c
)
add_library(plugin_sa STATIC ${SDK_SOURCES})
target_include_directories(plugin_sa PUBLIC
    ${SDK}/plugin_sa
    ${SDK}/plugin_sa/game_sa
    ${SDK}/plugin_sa/game_sa/enums
    ${SDK}/plugin_sa/game_sa/rw
    ${SDK}/safetyhook
    ${SDK}/shared
    ${SDK}/shared/game
    ${SDK}
)
target_include_directories(plugin_sa SYSTEM PUBLIC ${SDK}/shared/dxsdk)
target_compile_definitions(plugin_sa PUBLIC
    GTASA PLUGIN_SGV_10US RW _DX9_SDK_INSTALLED _MBCS
    _CRT_SECURE_NO_WARNINGS _CRT_NON_CONFORMING_SWPRINTFS
    _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING
)
target_compile_options(plugin_sa PUBLIC
    $<$<COMPILE_LANGUAGE:CXX>:/std:c++latest>
    /sdl- /MP /wd4073 /wd4244 /wd4267 /wd4305 /wd4018 /wd4996
)

# ---------------------------------------------------------------- the mod (.asi)
file(GLOB MOD_SOURCES ${CMAKE_SOURCE_DIR}/src/*.cpp ${CMAKE_SOURCE_DIR}/src/generated/*.cpp)
add_library(MinecraftSA SHARED ${MOD_SOURCES})
set_target_properties(MinecraftSA PROPERTIES SUFFIX ".asi")
target_include_directories(MinecraftSA PRIVATE ${CMAKE_SOURCE_DIR}/src ${SDK}/stb)
target_link_libraries(MinecraftSA PRIVATE plugin_sa)
target_link_directories(MinecraftSA PRIVATE ${SDK}/shared/dxsdk)
target_link_libraries(MinecraftSA PRIVATE d3d9)
target_compile_options(MinecraftSA PRIVATE /W3 /utf-8 /Zi)
target_link_options(MinecraftSA PRIVATE /DEBUG /OPT:REF /OPT:ICF)

# ---------------------------------------------------------------- offline logic tests (not shipped)
add_executable(mc_tests tests/test_main.cpp src/Inventory.cpp src/Items.cpp src/World.cpp src/ModCommon.cpp
               src/generated/GameData.cpp)
target_include_directories(mc_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(mc_tests PRIVATE plugin_sa)
target_compile_options(mc_tests PRIVATE /utf-8)
