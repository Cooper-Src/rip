cmake_minimum_required(VERSION 3.25)

project(rip
    VERSION 1.1.12
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

if(WIN32)
    enable_language(RC)
endif()

find_package(Threads REQUIRED)

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
        Threads::Threads
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
    src/rip-gui.qrc
    src/archive.cpp
    src/crc32.cpp
    src/compression.cpp
    src/token_codec.cpp
    src/token_huffman.cpp
)

if(WIN32)
    file(TO_CMAKE_PATH
        "${CMAKE_CURRENT_SOURCE_DIR}/branding/rip.ico"
        RIP_APP_ICON_PATH
    )
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/src/windows/rip-gui.rc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/rip-gui.rc"
        @ONLY
    )
    target_sources(rip-gui PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/rip-gui.rc")
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
        Threads::Threads
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

target_link_libraries(
    rip-compression-test
    PRIVATE
        Threads::Threads
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
        Threads::Threads
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

    set(RIP_RELEASE_VERSION "${PROJECT_VERSION}")

    if(DEFINED RIP_PACKAGE_VERSION AND
       NOT RIP_PACKAGE_VERSION STREQUAL "")
        string(
            REGEX REPLACE
            "^v"
            ""
            RIP_RELEASE_VERSION
            "${RIP_PACKAGE_VERSION}"
        )
    elseif(DEFINED ENV{GITHUB_REF_NAME})
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
        "RIP Archive Toolkit - custom archive utility and GUI for Windows"
    )
    set(
        CPACK_PACKAGE_VERSION
        "${RIP_RELEASE_VERSION}"
    )
    if(CMAKE_GENERATOR_PLATFORM STREQUAL "ARM64")
        set(RIP_WINDOWS_ARCH "arm64")
    else()
        set(RIP_WINDOWS_ARCH "x64")
    endif()

    set(
        CPACK_PACKAGE_FILE_NAME
        "rip-${RIP_RELEASE_VERSION}-windows-${RIP_WINDOWS_ARCH}"
    )

    include(CPack)

elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")

    # The GUI resolves its web assets relative to its executable. Keep the
    # executable and assets together under /usr/lib/rip, with a small launcher
    # in /usr/bin for terminals and desktop environments.
    install(
        TARGETS rip
        RUNTIME DESTINATION bin
    )

    install(
        TARGETS rip-gui
        RUNTIME DESTINATION lib/rip
    )

    install(
        DIRECTORY
            "${CMAKE_CURRENT_SOURCE_DIR}/gui/dist/"
        DESTINATION
            lib/rip/gui
    )

    install(
        PROGRAMS
            "${CMAKE_CURRENT_SOURCE_DIR}/packaging/linux/rip-gui"
        DESTINATION
            bin
    )

    install(
        FILES
            "${CMAKE_CURRENT_SOURCE_DIR}/packaging/linux/rip.desktop"
        DESTINATION
            share/applications
    )

    install(
        FILES
            "${CMAKE_CURRENT_SOURCE_DIR}/branding/rip-mark.svg"
        DESTINATION
            share/icons/hicolor/scalable/apps
        RENAME rip.svg
    )

    set(RIP_RELEASE_VERSION "${PROJECT_VERSION}")

    if(DEFINED RIP_PACKAGE_VERSION AND
       NOT RIP_PACKAGE_VERSION STREQUAL "")
        string(
            REGEX REPLACE
            "^v"
            ""
            RIP_RELEASE_VERSION
            "${RIP_PACKAGE_VERSION}"
        )
    elseif(DEFINED ENV{GITHUB_REF_NAME})
        string(
            REGEX REPLACE
            "^v"
            ""
            RIP_RELEASE_VERSION
            "$ENV{GITHUB_REF_NAME}"
        )
    endif()

    set(CPACK_GENERATOR "DEB;RPM;TGZ")
    set(CPACK_PACKAGE_NAME "rip")
    set(CPACK_PACKAGE_VENDOR "Cooper-Src")
    set(CPACK_PACKAGE_CONTACT "Cooper-Src")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
    set(
        CPACK_PACKAGE_DESCRIPTION_SUMMARY
        "RIP Archive Toolkit - archive utility and graphical archive manager"
    )
    set(
        CPACK_PACKAGE_DESCRIPTION
        "RIP Archive Toolkit creates, inspects, tests, and extracts .rip archives. Includes a command-line utility and a Qt-based graphical archive manager."
    )
    set(
        CPACK_PACKAGE_VERSION
        "${RIP_RELEASE_VERSION}"
    )
    set(
        CPACK_PACKAGE_FILE_NAME
        "rip-${RIP_RELEASE_VERSION}-linux-${CMAKE_SYSTEM_PROCESSOR}"
    )

    # Use standard package-manager filenames such as rip_1.2.3_amd64.deb
    # and rip-1.2.3-1.x86_64.rpm.
    set(CPACK_DEBIAN_FILE_NAME "DEB-DEFAULT")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Cooper-Src")
    set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
    set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
    set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/Cooper-Src/rip")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)

    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
        set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "arm64")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
        set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
    endif()

    set(CPACK_RPM_FILE_NAME "RPM-DEFAULT")
    set(CPACK_RPM_PACKAGE_RELEASE "1")
    set(CPACK_RPM_PACKAGE_LICENSE "MIT")
    set(CPACK_RPM_PACKAGE_GROUP "Applications/Archiving")
    set(CPACK_RPM_PACKAGE_URL "https://github.com/Cooper-Src/rip")
    set(CPACK_RPM_PACKAGE_AUTOREQPROV ON)

    include(CPack)

endif()
