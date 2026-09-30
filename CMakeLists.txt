cmake_minimum_required(VERSION 3.25)

project(rip
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --------------------------------------------------------------------------
# zlib-ng
#
# Build zlib-ng ourselves in zlib-compatible mode so RIP does not depend
# on a separately installed zlib library.
# --------------------------------------------------------------------------

include(FetchContent)

set(ZLIB_COMPAT ON CACHE BOOL "Build zlib-ng with zlib-compatible API" FORCE)
set(ZLIB_ENABLE_TESTS OFF CACHE BOOL "Disable zlib-ng tests" FORCE)
set(WITH_GZFILEOP OFF CACHE BOOL "Disable gzFile compatibility" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static libraries" FORCE)

FetchContent_Declare(
    zlib-ng
    GIT_REPOSITORY https://github.com/zlib-ng/zlib-ng.git
    GIT_TAG 2.3.3
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(zlib-ng)

# RIP

add_executable(rip
    src/main.cpp
    src/archive.cpp
    src/crc32.cpp
    src/compression.cpp
    src/token_codec.cpp
    src/token_huffman.cpp
)

target_include_directories(rip
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(rip
    PRIVATE
        zlib
)

if(MSVC)
    target_compile_options(rip PRIVATE
        /W4
        /permissive-
    )
else()
    target_compile_options(rip PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()

# RIP WebView2 GUI

if(WIN32)

    set(RIP_WEBVIEW2_VERSION "1.0.4191.47")

    set(
        RIP_WEBVIEW2_PACKAGE_DIR
        "${CMAKE_BINARY_DIR}/_deps/webview2-${RIP_WEBVIEW2_VERSION}"
    )

    set(
        RIP_WEBVIEW2_NUPKG
        "${CMAKE_BINARY_DIR}/_deps/webview2-${RIP_WEBVIEW2_VERSION}.nupkg"
    )

    if(NOT EXISTS
        "${RIP_WEBVIEW2_PACKAGE_DIR}/build/native/include/WebView2.h"
    )

        message(STATUS
            "Downloading WebView2 SDK ${RIP_WEBVIEW2_VERSION}..."
        )

        file(MAKE_DIRECTORY
            "${CMAKE_BINARY_DIR}/_deps"
        )

        if(NOT EXISTS "${RIP_WEBVIEW2_NUPKG}")

            file(DOWNLOAD
                "https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/${RIP_WEBVIEW2_VERSION}"
                "${RIP_WEBVIEW2_NUPKG}"
                SHOW_PROGRESS
            )

        endif()

        file(MAKE_DIRECTORY
            "${RIP_WEBVIEW2_PACKAGE_DIR}"
        )

        execute_process(
            COMMAND
                "${CMAKE_COMMAND}"
                -E
                tar
                x
                "${RIP_WEBVIEW2_NUPKG}"

            WORKING_DIRECTORY
                "${RIP_WEBVIEW2_PACKAGE_DIR}"

            RESULT_VARIABLE
                RIP_WEBVIEW2_EXTRACT_RESULT
        )

        if(NOT RIP_WEBVIEW2_EXTRACT_RESULT EQUAL 0)
            message(FATAL_ERROR
                "Failed to extract the WebView2 SDK."
            )
        endif()

    endif()

    if(CMAKE_VS_PLATFORM_NAME STREQUAL "ARM64")

        set(RIP_WEBVIEW2_ARCH arm64)

    elseif(CMAKE_VS_PLATFORM_NAME STREQUAL "x64")

        set(RIP_WEBVIEW2_ARCH x64)

    elseif(CMAKE_VS_PLATFORM_NAME STREQUAL "Win32")

        set(RIP_WEBVIEW2_ARCH x86)

    else()

        message(FATAL_ERROR
            "Unsupported WebView2 architecture: "
            "${CMAKE_VS_PLATFORM_NAME}"
        )

    endif()

    add_executable(
    rip-gui
    WIN32
    src/gui_main.cpp
    src/archive.cpp
    src/crc32.cpp
    src/compression.cpp
    src/token_codec.cpp
    src/token_huffman.cpp
)

    target_include_directories(
        rip-gui
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            "${RIP_WEBVIEW2_PACKAGE_DIR}/build/native/include"
    )

    target_link_libraries(
        rip-gui
        PRIVATE
            zlib

            "${RIP_WEBVIEW2_PACKAGE_DIR}/build/native/${RIP_WEBVIEW2_ARCH}/WebView2LoaderStatic.lib"

            user32
            shell32
            ole32
            shlwapi
            version
            advapi32
    )

    target_compile_definitions(
        rip-gui
        PRIVATE
            UNICODE
            _UNICODE
            NOMINMAX
            WIN32_LEAN_AND_MEAN
    )

    if(MSVC)

        target_compile_options(
            rip-gui
            PRIVATE
                /W4
                /permissive-
            )

    else()

        target_compile_options(
            rip-gui
            PRIVATE
                -Wall
                -Wextra
                -Wpedantic
            )

    endif()

    add_custom_command(
        TARGET rip-gui
        POST_BUILD

        COMMAND
            "${CMAKE_COMMAND}"
            -E
            make_directory
            "$<TARGET_FILE_DIR:rip-gui>/gui"

        COMMAND
            "${CMAKE_COMMAND}"
            -E
            copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/gui/dist"
            "$<TARGET_FILE_DIR:rip-gui>/gui"
    )

endif()

add_executable(
    rip-compression-test
    src/compression_test.cpp
    src/compression.cpp
    src/token_codec.cpp
    src/token_huffman.cpp
)

target_include_directories(
    rip-compression-test
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(MSVC)
    target_compile_options(
        rip-compression-test
        PRIVATE
            /W4
            /permissive-
    )
else()
    target_compile_options(
        rip-compression-test
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
endif()

add_executable(
    rip-compression-bench
    src/compression_bench.cpp
    src/compression.cpp
    src/token_codec.cpp
    src/token_huffman.cpp
)

target_include_directories(
    rip-compression-bench
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(
    rip-compression-bench
    PRIVATE
        zlib
)

if(MSVC)
    target_compile_options(
        rip-compression-bench
        PRIVATE
            /W4
            /permissive-
    )
else()
    target_compile_options(
        rip-compression-bench
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
endif()

add_executable(
    rip-token-codec-test
    src/token_codec_test.cpp
    src/token_codec.cpp
)

target_include_directories(
    rip-token-codec-test
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(MSVC)
    target_compile_options(
        rip-token-codec-test
        PRIVATE
            /W4
            /permissive-
    )
else()
    target_compile_options(
        rip-token-codec-test
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
endif()

add_executable(
    rip-token-huffman-test
    src/token_huffman_test.cpp
    src/token_huffman.cpp
    src/token_codec.cpp
)

target_include_directories(
    rip-token-huffman-test
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(MSVC)
    target_compile_options(
        rip-token-huffman-test
        PRIVATE
            /W4
            /permissive-
    )
else()
    target_compile_options(
        rip-token-huffman-test
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
endif()

add_executable(
    rip-token-huffman-bench
    src/token_huffman_bench.cpp
    src/token_huffman.cpp
    src/token_codec.cpp
)

target_include_directories(
    rip-token-huffman-bench
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(MSVC)
    target_compile_options(
        rip-token-huffman-bench
        PRIVATE
            /W4
            /permissive-
    )
else()
    target_compile_options(
        rip-token-huffman-bench
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
endif()

# --------------------------------------------------------------------------
# Windows package / release
# --------------------------------------------------------------------------

if(WIN32)

    install(
        TARGETS
            rip
            rip-gui
        RUNTIME DESTINATION .
    )

    install(
        DIRECTORY
            "${CMAKE_CURRENT_SOURCE_DIR}/gui/dist/"
        DESTINATION
            gui
    )

    set(
        RIP_RELEASE_VERSION
        "${PROJECT_VERSION}"
    )

    if(DEFINED ENV{GITHUB_REF_NAME})

        string(
            REGEX REPLACE
            "^v"
            ""
            RIP_RELEASE_VERSION
            "$ENV{GITHUB_REF_NAME}"
        )

    endif()

    set(CPACK_GENERATOR "ZIP")
    set(CPACK_PACKAGE_NAME "rip")
    set(CPACK_PACKAGE_VENDOR "Cooper-Src")
    set(
        CPACK_PACKAGE_DESCRIPTION_SUMMARY
        "RIP custom archive utility for Windows ARM64"
    )
    set(
        CPACK_PACKAGE_VERSION
        "${RIP_RELEASE_VERSION}"
    )
    set(
        CPACK_PACKAGE_FILE_NAME
        "rip-${RIP_RELEASE_VERSION}-windows-arm64"
    )

    include(CPack)

endif()
