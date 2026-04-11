cmake_minimum_required(VERSION 3.15)
project(CorridorKeyAE VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Adobe After Effects SDK ---
# Set AE_SDK_PATH to your local SDK location, or place it in ae_sdk/ at repo root
if(NOT DEFINED AE_SDK_PATH)
    set(AE_SDK_PATH "${CMAKE_SOURCE_DIR}/../ae_sdk" CACHE PATH "Path to Adobe After Effects SDK")
endif()

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
)

set(PLUGIN_HEADERS
    include/CorridorKeyAE.h
    include/CorridorKeyAE_Params.h
    include/CorridorKeyAE_Render.h
    include/CorridorKeyAE_Bridge.h
    include/CorridorKeyAE_Version.h
)

# --- Build target ---
if(APPLE)
    # macOS: Build as a .plugin bundle (Universal Binary)
    add_library(CorridorKeyAE MODULE ${PLUGIN_SOURCES} ${PLUGIN_HEADERS})

    set_target_properties(CorridorKeyAE PROPERTIES
        BUNDLE TRUE
        BUNDLE_EXTENSION "plugin"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/resources/Info.plist.in"
        OSX_ARCHITECTURES "x86_64;arm64"
        OUTPUT_NAME "CorridorKey"
    )
elseif(WIN32)
    # Windows: Build as a .aex (renamed DLL)
    add_library(CorridorKeyAE SHARED ${PLUGIN_SOURCES} ${PLUGIN_HEADERS})

    set_target_properties(CorridorKeyAE PROPERTIES
        SUFFIX ".aex"
        OUTPUT_NAME "CorridorKey"
    )

    # Windows-specific resource file for PiPL
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
    )
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

# --- Install ---
install(TARGETS CorridorKeyAE
    BUNDLE DESTINATION .
    LIBRARY DESTINATION .
    RUNTIME DESTINATION .
)
