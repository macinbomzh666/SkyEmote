cmake_minimum_required(VERSION 3.25)
project(SkyEmote VERSION 1.2.4 LANGUAGES CXX)

if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "SkyEmote targets Windows x64 using the MSVC toolchain.")
endif()

option(SKYEMOTE_DEPENDENCIES_ONLY "Build and install the pinned build dependencies" OFF)
if(SKYEMOTE_DEPENDENCIES_ONLY)
    add_subdirectory(cmake/dependencies)
    return()
endif()

find_package(spdlog 1.16.0 EXACT CONFIG REQUIRED)
find_package(directxmath CONFIG REQUIRED)

include(FetchContent)
set(ENABLE_SKYRIM_SE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_AE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_VR OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
# The renderer hook validates its call opcode before using the trampoline.
set(SKSE_SUPPORT_PATCH_SAFETY OFF CACHE BOOL "" FORCE)
set(COMMONLIB_PREBUILT OFF CACHE BOOL "" FORCE)
set(COMMONLIB_PREBUILT_DIR "" CACHE PATH "" FORCE)

FetchContent_Declare(commonlibsse
    GIT_REPOSITORY https://github.com/alandtse/CommonLibSSE-NG.git
    GIT_TAG 8ffc2b74af015f837de53cc02b0cb470a15678e4
    GIT_SUBMODULES extern/openvr
)
FetchContent_MakeAvailable(commonlibsse)
FetchContent_Declare(json
    URL https://raw.githubusercontent.com/nlohmann/json/v3.12.0/single_include/nlohmann/json.hpp
    URL_HASH SHA256=aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63
    DOWNLOAD_NO_EXTRACT TRUE
)
FetchContent_MakeAvailable(json)
FetchContent_Declare(imgui
    URL https://github.com/ocornut/imgui/archive/refs/tags/v1.92.9.tar.gz
    URL_HASH SHA256=af97ed649182c39314320514a672b82008ab462b9293fe23d37b30bfa5d05519
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(imgui)
add_library(SkyEmoteImGui STATIC
    "${imgui_SOURCE_DIR}/imgui.cpp"
    "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp"
    "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
    "${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp"
)
target_include_directories(SkyEmoteImGui SYSTEM PUBLIC "${imgui_SOURCE_DIR}")
target_compile_features(SkyEmoteImGui PRIVATE cxx_std_11)
target_compile_options(SkyEmoteImGui PRIVATE /utf-8)
target_link_libraries(SkyEmoteImGui PUBLIC d3d11 dxgi d3dcompiler imm32)

include("${commonlibsse_SOURCE_DIR}/cmake/CommonLibSSE.cmake")
set_property(DIRECTORY "${commonlibsse_SOURCE_DIR}" PROPERTY EXCLUDE_FROM_ALL YES)
target_link_libraries(CommonLibSSE PUBLIC Microsoft::DirectXMath)

add_library(SkyEmoteCore STATIC src/core/Pack.cpp src/core/StateSequence.cpp src/core/Favorites.cpp
    src/core/MenuSelection.cpp src/core/InputPolicy.cpp)
target_include_directories(SkyEmoteCore PUBLIC src/core)
target_include_directories(SkyEmoteCore SYSTEM PRIVATE "${json_SOURCE_DIR}")
target_compile_definitions(SkyEmoteCore PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_features(SkyEmoteCore PUBLIC cxx_std_23)
target_compile_options(SkyEmoteCore PRIVATE /W4 /permissive- /utf-8)

add_commonlibsse_plugin(SkyEmote
    AUTHOR Macinbomzh
    USE_ADDRESS_LIBRARY
    SOURCES src/Plugin.cpp src/core/Playback.cpp src/core/Renderer.cpp src/core/Menu.cpp
        src/core/Input.cpp src/core/MenuCamera.cpp src/core/PackWarnings.cpp
)
target_link_libraries(SkyEmote PRIVATE SkyEmoteCore SkyEmoteImGui)
target_precompile_headers(SkyEmote PRIVATE src/PCH.h)
target_compile_options(SkyEmote PRIVATE /W4 /permissive- /utf-8)
target_compile_definitions(SkyEmote PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

option(SKYEMOTE_BUILD_TESTS "Build functional tests" OFF)
if(SKYEMOTE_BUILD_TESTS)
    enable_testing()
    foreach(name IN ITEMS Pack PackFiles StateSequence Favorites PackDiagnostics Startup MenuSelection InputPolicy)
        set(target SkyEmote${name}Tests)
        add_executable(${target} tests/${name}Tests.cpp)
        target_link_libraries(${target} PRIVATE SkyEmoteCore)
        target_include_directories(${target} SYSTEM PRIVATE "${json_SOURCE_DIR}")
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
        set_property(TARGET ${target} PROPERTY CROSSCOMPILING_EMULATOR
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/run-tests.sh")
        add_test(NAME ${name} COMMAND ${target}
            "Z:${CMAKE_CURRENT_SOURCE_DIR}/assets/SKSE/Plugins/SkyEmote/Packs/vanilla.json")
        set_tests_properties(${name} PROPERTIES TIMEOUT 60)
    endforeach()
endif()

install(TARGETS SkyEmote RUNTIME DESTINATION SKSE/Plugins)
install(DIRECTORY assets/ DESTINATION .)
install(FILES LICENSE THIRD_PARTY_NOTICES.md DESTINATION licenses/SkyEmote)
install(DIRECTORY licenses/ DESTINATION licenses/SkyEmote/licenses)

set(CPACK_GENERATOR ZIP)
set(CPACK_PACKAGE_FILE_NAME "SkyEmote")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
include(CPack)
