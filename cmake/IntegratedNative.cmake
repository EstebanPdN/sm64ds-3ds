# Complete credited native backend; original runtime/diagnostics remain intact.
find_program(SM64DS_INTEGRATED_HOST_PYTHON NAMES python3 REQUIRED)
set(SM64DS_INTEGRATED_ROM "${PROJECT_SOURCE_DIR}/build/roms/europe.nds" CACHE FILEPATH
    "Private verified EU ROM used only by the local integrated native build")
add_custom_target(sm64ds-integrated_3dsx
    COMMAND "${SM64DS_INTEGRATED_HOST_PYTHON}"
        "${PROJECT_SOURCE_DIR}/tools/build_integrated_native.py"
        --rom "${SM64DS_INTEGRATED_ROM}" --jobs 4
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    USES_TERMINAL
    COMMENT "Build and audit the pinned complete native backend before emulator use")
add_custom_target(sm64ds-integrated-60_3dsx
    COMMAND "${SM64DS_INTEGRATED_HOST_PYTHON}"
        "${PROJECT_SOURCE_DIR}/tools/build_integrated_native.py"
        --rom "${SM64DS_INTEGRATED_ROM}" --jobs 4 --render60
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    USES_TERMINAL
    COMMENT "Build and gate the render-only 60 Hz experiment before emulator use")
add_custom_target(sm64ds-integrated-rom-only_3dsx
    COMMAND "${SM64DS_INTEGRATED_HOST_PYTHON}"
        "${PROJECT_SOURCE_DIR}/tools/build_integrated_native.py"
        --rom "${SM64DS_INTEGRATED_ROM}" --jobs 4 --render60 --rom-only
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    USES_TERMINAL
    COMMENT "Build the ROM-only installer with loading screen and bounded-memory gates")
