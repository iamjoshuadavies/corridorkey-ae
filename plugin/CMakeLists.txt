cmake_minimum_required(VERSION 3.15)
project(CorridorKeyAE VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Adobe After Effects SDK ---
# Set AE_SDK_PATH to your local SDK location, or place it in ae_sdk/ at repo root.
# On Windows, the SDK usually extracts to ae_sdk_win/<zip-root>/<zstd-root>/Examples —
# check that nested path as a convenience fallback.
if(NOT DEFINED AE_SDK_PATH)
    set(_default_sdk "${CMAKE_SOURCE_DIR}/../ae_sdk")
    if(WIN32 AND NOT EXISTS "${_default_sdk}")
        file(GLOB _win_sdk_candidates
            "${CMAKE_SOURCE_DIR}/../ae_sdk_win/*/*/Examples"
            "${CMAKE_SOURCE_DIR}/../ae_sdk_win/*/Examples"
        )
        if(_win_sdk_candidates)
            list(GET _win_sdk_candidates 0 _default_sdk)
        endif()
    endif()
    set(AE_SDK_PATH "${_default_sdk}" CACHE PATH "Path to Adobe After Effects SDK")
endif()

# Normalize to forward slashes — on Windows the user may pass
# backslash-delimited paths (D:\a\...) which CMake otherwise treats
# as containing C-style escape sequences like \a when interpolated
# into strings. file(TO_CMAKE_PATH) converts \ -> / in place.
file(TO_CMAKE_PATH "${AE_SDK_PATH}" AE_SDK_PATH)

if(NOT EXISTS "${AE_SDK_PATH}")
    message(WARNING
        "After Effects SDK not found at: ${AE_SDK_PATH}\n"
        "Set -DAE_SDK_PATH=/path/to/sdk or place SDK in ae_sdk/ at repo root.\n"
        "Building in stub mode (no AE headers)."
    )
    set(AE_SDK_FOUND FALSE)
else()
    set(AE_SDK_FOUND TRUE)
    message(STATUS "After Effects SDK found at: ${AE_SDK_PATH}")
endif()

# --- Source files ---
set(PLUGIN_SOURCES
    src/CorridorKeyAE.cpp
    src/CorridorKeyAE_Params.cpp
    src/CorridorKeyAE_Render.cpp
    src/CorridorKeyAE_Bridge.cpp
    src/CorridorKeyAE_UI.cpp
    src/CorridorKeyAE_AutoHint.cpp
)

# SDK utility sources needed for smart render
set(SDK_UTIL_SOURCES)
if(AE_SDK_FOUND)
    list(APPEND SDK_UTIL_SOURCES
        "${AE_SDK_PATH}/Util/Smart_Utils.cpp"
        "${AE_SDK_PATH}/Util/AEFX_SuiteHelper.c"
    )
endif()

set(PLUGIN_HEADERS
    include/CorridorKeyAE.h
    include/CorridorKeyAE_Params.h
    include/CorridorKeyAE_Render.h
    include/CorridorKeyAE_Bridge.h
    include/CorridorKeyAE_Version.h
    include/CorridorKeyAE_UI.h
    include/CorridorKeyAE_Logo.h
    include/CorridorKeyAE_AutoHint.h
)

# --- Build target ---
if(APPLE)
    # macOS: Build as a .plugin bundle (Universal Binary)
    add_library(CorridorKeyAE MODULE ${PLUGIN_SOURCES} ${PLUGIN_HEADERS} ${SDK_UTIL_SOURCES})

    set_target_properties(CorridorKeyAE PROPERTIES
        BUNDLE TRUE
        BUNDLE_EXTENSION "plugin"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/resources/Info.plist.in"
        OSX_ARCHITECTURES "x86_64;arm64"
        OUTPUT_NAME "CorridorKey"
    )
elseif(WIN32)
    # Windows: Build as a .aex (renamed DLL)
    add_library(CorridorKeyAE SHARED ${PLUGIN_SOURCES} ${PLUGIN_HEADERS} ${SDK_UTIL_SOURCES})

    set_target_properties(CorridorKeyAE PROPERTIES
        SUFFIX ".aex"
        OUTPUT_NAME "CorridorKey"
    )

    # Windows-specific resource file for PiPL (wraps the generated _temp.rc)
    target_sources(CorridorKeyAE PRIVATE resources/CorridorKeyAE.rc)
endif()

# --- Include paths ---
target_include_directories(CorridorKeyAE PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(AE_SDK_FOUND)
    target_include_directories(CorridorKeyAE PRIVATE
        "${AE_SDK_PATH}/Headers"
        "${AE_SDK_PATH}/Headers/SP"
        "${AE_SDK_PATH}/Util"
        "${AE_SDK_PATH}/Resources"
    )
    target_compile_definitions(CorridorKeyAE PRIVATE AE_SDK_AVAILABLE=1)
else()
    target_compile_definitions(CorridorKeyAE PRIVATE AE_SDK_AVAILABLE=0)
endif()

# --- Platform definitions ---
if(APPLE)
    target_compile_definitions(CorridorKeyAE PRIVATE
        __MACH__
        TARGET_OS_MAC=1
    )
elseif(WIN32)
    target_compile_definitions(CorridorKeyAE PRIVATE
        _WINDOWS
        WIN32
        MSWindows
        # Silence MSVC nags about fopen/fscanf/getenv and inet_addr.
        # We use them deliberately and the POSIX variants are standard C.
        _CRT_SECURE_NO_WARNINGS
        _WINSOCK_DEPRECATED_NO_WARNINGS
    )
    # Build at /W4 with a narrow set of AE-SDK-specific silences:
    #   C4100 — unreferenced formal parameter. AE effect handlers take fixed
    #           (in_data, out_data, params, output, extra) signatures and we
    #           rarely use all five per command; the warnings are noise.
    #   C4201 — nonstandard extension: nameless struct/union. Fires inside
    #           the SDK's own headers (AE_Effect.h's PF_TimeDisplay union),
    #           not our code — and we don't want to patch SDK headers.
    target_compile_options(CorridorKeyAE PRIVATE /W4 /wd4100 /wd4201)
endif()

# --- Version info ---
target_compile_definitions(CorridorKeyAE PRIVATE
    CK_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    CK_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    CK_VERSION_PATCH=${PROJECT_VERSION_PATCH}
)

# --- PiPL Resource (macOS) ---
if(APPLE AND AE_SDK_FOUND)
    find_program(REZ_TOOL Rez
        PATHS
            "/usr/bin"
            "/Library/Developer/CommandLineTools/usr/bin"
            "/Applications/Xcode.app/Contents/Developer/usr/bin"
    )

    if(REZ_TOOL)
        message(STATUS "Rez found at: ${REZ_TOOL}")

        set(PIPL_R "${CMAKE_CURRENT_SOURCE_DIR}/resources/CorridorKeyAEPiPL.r")
        set(PIPL_RSRC "${CMAKE_CURRENT_BINARY_DIR}/CorridorKeyAEPiPL.rsrc")

        add_custom_command(
            OUTPUT "${PIPL_RSRC}"
            COMMAND ${REZ_TOOL}
                "${PIPL_R}"
                -o "${PIPL_RSRC}"
                -useDF
                -i "${AE_SDK_PATH}/Headers"
                -i "${AE_SDK_PATH}/Resources"
                -d AE_OS_MAC
            DEPENDS "${PIPL_R}"
            COMMENT "Compiling PiPL resource with Rez"
        )

        add_custom_target(CorridorKeyPiPL DEPENDS "${PIPL_RSRC}")
        add_dependencies(CorridorKeyAE CorridorKeyPiPL)

        # Place .rsrc in bundle Resources and add PkgInfo, then ad-hoc codesign
        add_custom_command(TARGET CorridorKeyAE POST_BUILD
            # Copy PiPL resource into bundle
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_BUNDLE_DIR:CorridorKeyAE>/Contents/Resources"
            COMMAND ${CMAKE_COMMAND} -E copy
                "${PIPL_RSRC}"
                "$<TARGET_BUNDLE_DIR:CorridorKeyAE>/Contents/Resources/CorridorKeyAEPiPL.rsrc"
            # Create PkgInfo via a cmake script
            COMMAND ${CMAKE_COMMAND}
                -DPKGINFO_PATH=$<TARGET_BUNDLE_DIR:CorridorKeyAE>/Contents/PkgInfo
                -P "${CMAKE_CURRENT_SOURCE_DIR}/resources/WritePkgInfo.cmake"
            # Ad-hoc codesign so macOS/AE accepts the plugin
            COMMAND codesign --force --sign - --timestamp=none
                "$<TARGET_BUNDLE_DIR:CorridorKeyAE>"
            COMMENT "Packaging and signing plugin bundle"
        )
    else()
        message(WARNING "Rez not found — PiPL resource will not be compiled. Plugin will not load in AE.")
    endif()
endif()

# --- PiPL Resource (Windows) ---
# Replicates the SDK sample pipeline:
#   1. cl /EP /I <Headers>  CorridorKeyAEPiPL.r              > CorridorKeyAEPiPL.rr
#   2. PiPLtool.exe         CorridorKeyAEPiPL.rr               CorridorKeyAEPiPL.rrc
#   3. cl /D MSWindows /EP  CorridorKeyAEPiPL.rrc            > CorridorKeyAEPiPL_temp.rc
#
# CorridorKeyAE.rc (tracked) #includes CorridorKeyAEPiPL_temp.rc, and we add the
# build directory to the rc.exe include path so the #include resolves.
if(WIN32 AND AE_SDK_FOUND)
    set(PIPL_R        "${CMAKE_CURRENT_SOURCE_DIR}/resources/CorridorKeyAEPiPL.r")
    set(PIPL_RR       "${CMAKE_CURRENT_BINARY_DIR}/CorridorKeyAEPiPL.rr")
    set(PIPL_RRC      "${CMAKE_CURRENT_BINARY_DIR}/CorridorKeyAEPiPL.rrc")
    set(PIPL_TEMP_RC  "${CMAKE_CURRENT_BINARY_DIR}/CorridorKeyAEPiPL_temp.rc")

    # PiPLtool.exe lives in Examples/Resources/
    set(PIPL_TOOL     "${AE_SDK_PATH}/Resources/PiPLtool.exe")

    if(NOT EXISTS "${PIPL_TOOL}")
        message(WARNING "PiPLtool.exe not found at: ${PIPL_TOOL}")
    endif()

    # Find cl.exe explicitly — needed because PiPL pipeline uses it as a preprocessor
    # outside the normal compile, and at custom-command time ${CMAKE_CXX_COMPILER}
    # may not be in PATH without the VS dev environment.
    set(CL_EXE "${CMAKE_CXX_COMPILER}")

    add_custom_command(
        OUTPUT "${PIPL_TEMP_RC}"
        # Step 1: preprocess .r with cl to produce .rr
        COMMAND "${CL_EXE}" /nologo /I "${AE_SDK_PATH}/Headers" /EP "${PIPL_R}" > "${PIPL_RR}"
        # Step 2: PiPLtool converts .rr to .rrc
        COMMAND "${PIPL_TOOL}" "${PIPL_RR}" "${PIPL_RRC}"
        # Step 3: preprocess .rrc with cl /D MSWindows to produce the final _temp.rc
        COMMAND "${CL_EXE}" /nologo /D "MSWindows" /EP "${PIPL_RRC}" > "${PIPL_TEMP_RC}"
        DEPENDS "${PIPL_R}"
        COMMENT "Generating PiPL resource (cl /EP -> PiPLtool -> cl /EP)"
        VERBATIM
    )

    add_custom_target(CorridorKeyPiPL_Win DEPENDS "${PIPL_TEMP_RC}")
    add_dependencies(CorridorKeyAE CorridorKeyPiPL_Win)

    # The tracked CorridorKeyAE.rc does `#include "CorridorKeyAEPiPL_temp.rc"`,
    # so rc.exe needs to find it in the build directory.
    target_include_directories(CorridorKeyAE PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
    # Newer CMake also honors this for resource compilation:
    set_property(SOURCE resources/CorridorKeyAE.rc APPEND PROPERTY
        OBJECT_DEPENDS "${PIPL_TEMP_RC}")
endif()

# --- Install ---
install(TARGETS CorridorKeyAE
    BUNDLE DESTINATION .
    LIBRARY DESTINATION .
    RUNTIME DESTINATION .
)
