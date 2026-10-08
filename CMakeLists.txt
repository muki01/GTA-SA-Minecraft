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

# ---------------------------------------------------------------- the Minecraft side (game rules and data)
# src/core knows nothing about GTA: it is built without plugin-sdk, so anything GTA in it does not compile.
file(GLOB CORE_SOURCES ${CMAKE_SOURCE_DIR}/src/core/*.cpp ${CMAKE_SOURCE_DIR}/src/core/generated/*.cpp)
add_library(mc_core STATIC ${CORE_SOURCES})
target_include_directories(mc_core PUBLIC ${CMAKE_SOURCE_DIR}/src/core)
target_compile_definitions(mc_core PUBLIC _CRT_SECURE_NO_WARNINGS)
target_compile_options(mc_core PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/std:c++latest> /W3 /utf-8 /Zi /MP /sdl-)

# ---------------------------------------------------------------- the GTA SA side: the mod (.asi)
file(GLOB MOD_SOURCES ${CMAKE_SOURCE_DIR}/src/gta/*.cpp)
add_library(MinecraftSA SHARED ${MOD_SOURCES})
set_target_properties(MinecraftSA PROPERTIES SUFFIX ".asi")
target_include_directories(MinecraftSA PRIVATE ${CMAKE_SOURCE_DIR}/src/gta ${SDK}/stb)
target_link_libraries(MinecraftSA PRIVATE mc_core plugin_sa)
target_link_directories(MinecraftSA PRIVATE ${SDK}/shared/dxsdk)
target_link_libraries(MinecraftSA PRIVATE d3d9)
target_compile_options(MinecraftSA PRIVATE /W3 /utf-8 /Zi)
target_link_options(MinecraftSA PRIVATE /DEBUG /OPT:REF /OPT:ICF)

# ---------------------------------------------------------------- offline tests of the core (not shipped)
# only the core: they run without the game, and prove the core does
add_executable(mc_tests tests/test_main.cpp)
target_link_libraries(mc_tests PRIVATE mc_core)
target_compile_options(mc_tests PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/std:c++latest> /utf-8)
