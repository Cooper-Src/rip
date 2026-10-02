cmake_minimum_required(VERSION 3.25)

project(rip
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    WebChannel
    WebEngineCore
    WebEngineWidgets
)


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

# RIP Qt6 GUI

add_executable(
    rip-gui
    src/gui_main.cpp
)

if(WIN32)
    set_target_properties(
        rip-gui
        PROPERTIES
            WIN32_EXECUTABLE TRUE
    )
endif()

target_include_directories(
    rip-gui
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(
    rip-gui
    PRIVATE
        zlib
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::WebChannel
        Qt6::WebEngineCore
        Qt6::WebEngineWidgets
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
# Package / release
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
        "RIP custom archive utility and GUI for Windows"
    )
    set(
        CPACK_PACKAGE_VERSION
        "${RIP_RELEASE_VERSION}"
    )
    set(
        CPACK_PACKAGE_FILE_NAME
        "rip-${RIP_RELEASE_VERSION}-windows-${RIP_WEBVIEW2_ARCH}"
    )

    include(CPack)

elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")

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

    set(CPACK_GENERATOR "TGZ")
    set(CPACK_PACKAGE_NAME "rip")
    set(CPACK_PACKAGE_VENDOR "Cooper-Src")
    set(
        CPACK_PACKAGE_DESCRIPTION_SUMMARY
        "RIP custom archive utility and GUI for Linux"
    )
    set(
        CPACK_PACKAGE_VERSION
        "${RIP_RELEASE_VERSION}"
    )
    set(
        CPACK_PACKAGE_FILE_NAME
        "rip-${RIP_RELEASE_VERSION}-linux-${CMAKE_SYSTEM_PROCESSOR}"
    )

    include(CPack)

endif()
