include(FetchContent)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(SPDLOG_INSTALL ON CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_USE_STD_FORMAT ON CACHE BOOL "" FORCE)
set(SPDLOG_WCHAR_SUPPORT ON CACHE BOOL "" FORCE)
set(BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(RAPIDCSV_BUILD_TESTS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(spdlog
    URL https://github.com/gabime/spdlog/archive/v1.16.0.zip
    URL_HASH SHA256=3d25808d2fc4db86621a46855800c99ab5734999b61c4cbf9470edf631555397
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_Declare(directxmath
    URL https://github.com/microsoft/DirectXMath/archive/feb2024.zip
    URL_HASH SHA256=214d71420107249dfb4bbc37a573f288b0951cc9ffe323dbf662101f3df4d766
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_Declare(rapidcsv
    URL https://github.com/d99kris/rapidcsv/archive/v8.92.zip
    URL_HASH SHA256=e7d8026b2cfdcecd6a8bbf3539a5df15300f64036bbbc7c0ad7a96b80d3ee50e
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_Declare(directxtk
    URL https://github.com/microsoft/DirectXTK/archive/feb2024.zip
    URL_HASH SHA256=edb643b2444ff24925339cfb1bc9f76c671d5404a5549d32ecaa0d61bbab28c9
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    PATCH_COMMAND "${CMAKE_COMMAND}"
        -DSOURCE_DIR=<SOURCE_DIR>
        -DLAUNCHER=${CMAKE_CURRENT_LIST_DIR}/directxtk-shaders.sh
        -P "${CMAKE_CURRENT_LIST_DIR}/patch-directxtk.cmake")

FetchContent_MakeAvailable(spdlog directxmath rapidcsv)
# rapidcsv 8.92 only installs its header when built as the top-level project.
install(FILES "${rapidcsv_SOURCE_DIR}/src/rapidcsv.h" DESTINATION include)
add_library(Microsoft::DirectXMath ALIAS DirectXMath)
FetchContent_MakeAvailable(directxtk)
target_link_libraries(DirectXTK PRIVATE DirectXMath)
