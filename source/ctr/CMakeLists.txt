find_program(HOST_PYTHON python3 REQUIRED NO_CMAKE_FIND_ROOT_PATH)
add_library(sm64ds_game_math game_math.cpp)
target_link_libraries(sm64ds_game_math PUBLIC sm64ds_math)
add_library(sm64ds_game_input_bridge game_input.cpp)
target_link_libraries(sm64ds_game_input_bridge PUBLIC sm64ds_game_input)
add_library(sm64ds_game_symbols game_symbols.cpp)
target_link_libraries(sm64ds_game_symbols PUBLIC sm64ds_game_data)
add_library(sm64ds_game_primitives game_primitives.cpp game_memory_primitives.S)
add_library(sm64ds_game_geometry_bridge game_geometry.cpp)
target_link_libraries(sm64ds_game_geometry_bridge PUBLIC sm64ds_game_geometry)
add_library(sm64ds_game_draw_bridge game_draw.cpp)
target_link_libraries(sm64ds_game_draw_bridge PUBLIC sm64ds_game_draw)
target_include_directories(sm64ds_game_primitives PUBLIC "${PROJECT_SOURCE_DIR}/include")
set(SHADER_BIN "${CMAKE_CURRENT_BINARY_DIR}/scene.shbin")
set(SHADER_HEADER "${CMAKE_CURRENT_BINARY_DIR}/scene_shader.h")
add_custom_command(OUTPUT "${SHADER_HEADER}"
  COMMAND "${CTR_PICASSO_EXE}" -o "${SHADER_BIN}" "${CMAKE_CURRENT_SOURCE_DIR}/scene.v.pica"
  COMMAND "${HOST_PYTHON}" "${PROJECT_SOURCE_DIR}/tools/embed.py" "${SHADER_BIN}" "${SHADER_HEADER}"
  DEPENDS scene.v.pica "${PROJECT_SOURCE_DIR}/tools/embed.py" VERBATIM)
add_executable(sm64ds-platform main.cpp input.cpp renderer.cpp "${SHADER_HEADER}")
add_library(sm64ds_game_renderer game_renderer.cpp "${SHADER_HEADER}")
target_include_directories(sm64ds_game_renderer PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(sm64ds_game_renderer PUBLIC sm64ds_game_textures citro3d ctru m)
option(SM64DS_RENDER_CHECK "Build separate recorded-frame GPU check; never the game" OFF)
if(SM64DS_RENDER_CHECK)
  add_executable(sm64ds-render-check "${PROJECT_SOURCE_DIR}/tests/game_renderer_ctr.cpp")
  target_link_libraries(sm64ds-render-check PRIVATE sm64ds_game_renderer)
  target_link_options(sm64ds-render-check PRIVATE -Wl,-z,noexecstack)
  ctr_create_3dsx(sm64ds-render-check)
endif()
option(SM64DS_CAPTURE_CHECK "Build separate synthetic GPU capture check; never the game" OFF)
if(SM64DS_CAPTURE_CHECK)
  add_executable(sm64ds-capture-check "${PROJECT_SOURCE_DIR}/tests/native_capture_ctr.cpp")
  target_link_libraries(sm64ds-capture-check PRIVATE sm64ds_game_renderer)
  target_link_options(sm64ds-capture-check PRIVATE -Wl,-z,noexecstack)
  ctr_create_3dsx(sm64ds-capture-check)
endif()
target_include_directories(sm64ds-platform PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(sm64ds-platform PRIVATE sm64ds_models sm64ds_game_data sm64ds_game_files citro3d ctru m)
option(SM64DS_ORIGINAL_HEAP "Initialize the tested original ARM11 game allocator" OFF)
option(SM64DS_ORIGINAL_COLLISION "Initialize original KCL collision queries from the user ROM" OFF)
if(SM64DS_ORIGINAL_COLLISION AND NOT SM64DS_ORIGINAL_HEAP)
  message(FATAL_ERROR "SM64DS_ORIGINAL_COLLISION requires SM64DS_ORIGINAL_HEAP")
endif()
if(SM64DS_ORIGINAL_HEAP)
  set(HEAP_MANIFEST "${PROJECT_SOURCE_DIR}/build/native-heap/objects.json")
  if(NOT EXISTS "${HEAP_MANIFEST}")
    message(FATAL_ERROR "Run native_core.py, link_native.py and test_game_heap_arm.py before enabling SM64DS_ORIGINAL_HEAP")
  endif()
  file(READ "${HEAP_MANIFEST}" HEAP_JSON)
  string(JSON HEAP_ISOLATED ERROR_VARIABLE HEAP_MANIFEST_ERROR GET "${HEAP_JSON}" isolated_game_operators)
  if(HEAP_MANIFEST_ERROR OR NOT HEAP_ISOLATED)
    message(FATAL_ERROR "Heap manifest must isolate original game new/delete from libctru/C++ runtime allocations")
  endif()
  string(JSON HEAP_COUNT LENGTH "${HEAP_JSON}" objects)
  math(EXPR HEAP_LAST "${HEAP_COUNT}-1")
  set(HEAP_OBJECTS "")
  foreach(INDEX RANGE 0 ${HEAP_LAST})
    string(JSON HEAP_OBJECT GET "${HEAP_JSON}" objects ${INDEX} object)
    list(APPEND HEAP_OBJECTS "${PROJECT_SOURCE_DIR}/${HEAP_OBJECT}")
  endforeach()
  set_source_files_properties(${HEAP_OBJECTS} PROPERTIES EXTERNAL_OBJECT TRUE)
  add_library(sm64ds_original_heap STATIC game_heap.cpp game_fatal.cpp ${HEAP_OBJECTS})
  target_include_directories(sm64ds_original_heap PRIVATE
    "${PROJECT_SOURCE_DIR}/build/native-core/generated/include" PUBLIC "${PROJECT_SOURCE_DIR}/include")
  target_link_libraries(sm64ds-platform PRIVATE sm64ds_original_heap)
  target_compile_definitions(sm64ds-platform PRIVATE SM64DS_ORIGINAL_HEAP=1)
  if(SM64DS_ORIGINAL_COLLISION)
    set(COLLISION_MANIFEST "${PROJECT_SOURCE_DIR}/build/native-collision/objects.json")
    if(NOT EXISTS "${COLLISION_MANIFEST}")
      message(FATAL_ERROR "Run tools/build_game_collision.py and test_game_collision_arm.py first")
    endif()
    file(READ "${COLLISION_MANIFEST}" COLLISION_JSON)
    set(COLLISION_REPORT "${PROJECT_SOURCE_DIR}/build/native-collision/report.json")
    if(NOT EXISTS "${COLLISION_REPORT}")
      message(FATAL_ERROR "Run tools/test_game_collision_arm.py before enabling collision integration")
    endif()
    file(READ "${COLLISION_REPORT}" COLLISION_REPORT_JSON)
    string(JSON COLLISION_STATUS GET "${COLLISION_REPORT_JSON}" status)
    string(JSON TESTED_MANIFEST_SHA GET "${COLLISION_REPORT_JSON}" manifest_sha256)
    file(SHA256 "${COLLISION_MANIFEST}" CURRENT_MANIFEST_SHA)
    if(NOT COLLISION_STATUS STREQUAL "passed" OR NOT TESTED_MANIFEST_SHA STREQUAL CURRENT_MANIFEST_SHA)
      message(FATAL_ERROR "Collision manifest differs from the passing ARM execution test; run it again")
    endif()
    string(JSON COLLISION_ISOLATED GET "${COLLISION_JSON}" isolated_game_operators)
    if(NOT COLLISION_ISOLATED)
      message(FATAL_ERROR "Collision bundle must isolate original game allocation operators")
    endif()
    # The allocator and collider share original BSS and deallocation TUs.
    # Keep one owner per input object when combining the two verified subsets.
    set(ORIGINAL_OBJECTS "")
    foreach(INDEX RANGE 0 ${HEAP_LAST})
      string(JSON ORIGINAL_OBJECT GET "${HEAP_JSON}" objects ${INDEX} original_object)
      list(APPEND ORIGINAL_OBJECTS "${ORIGINAL_OBJECT}")
    endforeach()
    string(JSON COLLISION_COUNT LENGTH "${COLLISION_JSON}" objects)
    math(EXPR COLLISION_LAST "${COLLISION_COUNT}-1")
    set(COLLISION_OBJECTS "")
    foreach(INDEX RANGE 0 ${COLLISION_LAST})
      string(JSON ORIGINAL_OBJECT GET "${COLLISION_JSON}" objects ${INDEX} original_object)
      if(NOT ORIGINAL_OBJECT IN_LIST ORIGINAL_OBJECTS)
        string(JSON COLLISION_OBJECT GET "${COLLISION_JSON}" objects ${INDEX} object)
        string(JSON EXPECTED_SHA GET "${COLLISION_JSON}" objects ${INDEX} sha256)
        file(SHA256 "${PROJECT_SOURCE_DIR}/${COLLISION_OBJECT}" ACTUAL_SHA)
        if(NOT ACTUAL_SHA STREQUAL EXPECTED_SHA)
          message(FATAL_ERROR "Collision object changed after the verified build: ${COLLISION_OBJECT}")
        endif()
        list(APPEND COLLISION_OBJECTS "${PROJECT_SOURCE_DIR}/${COLLISION_OBJECT}")
        list(APPEND ORIGINAL_OBJECTS "${ORIGINAL_OBJECT}")
      endif()
    endforeach()
    set_source_files_properties(${COLLISION_OBJECTS} PROPERTIES EXTERNAL_OBJECT TRUE)
    target_sources(sm64ds_original_heap PRIVATE ${COLLISION_OBJECTS})
    target_link_libraries(sm64ds-platform PRIVATE sm64ds_collision_data)
    target_compile_definitions(sm64ds-platform PRIVATE SM64DS_ORIGINAL_COLLISION=1)
  endif()
endif()
# SDK crtn.o has no GNU-stack note, but only returns from .init/.fini.
# Set the final executable's stack policy explicitly as well as object metadata.
target_link_options(sm64ds-platform PRIVATE -Wl,-z,noexecstack)
target_compile_options(sm64ds-platform PRIVATE -Wall -Wextra -Wpedantic)
ctr_generate_smdh(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/sm64ds-platform.smdh"
  NAME "SM64DS - platform preview" DESCRIPTION "Asset import and native GPU preview; not playable" AUTHOR "EstebanPdN / upstream contributors")
ctr_create_3dsx(sm64ds-platform SMDH "${CMAKE_CURRENT_BINARY_DIR}/sm64ds-platform.smdh")

# Experimental general game integration. Its original entry/scene/actor loop
# replaces the viewer workflow; it is not yet a release or a playability claim.
option(SM64DS_NATIVE_RUNTIME "Build the general original-game integration" OFF)
if(SM64DS_NATIVE_RUNTIME)
  set(RUNTIME_DIR "${PROJECT_SOURCE_DIR}/build/native-runtime")
  set(RUNTIME_SOURCES
    native_boot native_platform native_platform_abi native_audio_deferred
    native_touch_init native_boot_services native_video native_runtime
    native_frame native_snapshot native_display native_2d native_display_commit native_file_structors
    native_card native_file_video native_video_services native_save native_boot_transition native_jobs native_alarm
    native_projection native_menu_math native_soft_compare native_capture native_personal native_matrix_submit)
  set(RUNTIME_FILES "")
  foreach(NAME IN LISTS RUNTIME_SOURCES)
    list(APPEND RUNTIME_FILES "${PROJECT_SOURCE_DIR}/source/game_port/${NAME}.cpp")
  endforeach()
  add_library(sm64ds_native_services OBJECT ${RUNTIME_FILES} game_heap.cpp input.cpp)
  target_include_directories(sm64ds_native_services PRIVATE
    "${PROJECT_SOURCE_DIR}/build/native-core/generated/include")
  target_link_libraries(sm64ds_native_services PUBLIC sm64ds_game_renderer
    sm64ds_game_input_bridge sm64ds_game_draw_bridge sm64ds_game_geometry_bridge
    sm64ds_game_data sm64ds_game_files sm64ds_native_data sm64ds_game_primitives
    sm64ds_game_math sm64ds_game_symbols)
  set(RUNTIME_PROVIDER_LIBS sm64ds_game_input_bridge sm64ds_game_draw_bridge
    sm64ds_game_geometry_bridge sm64ds_game_primitives sm64ds_game_math sm64ds_game_files
    sm64ds_game_symbols)
  set(RUNTIME_BUILD_ARGS "")
  foreach(LIBRARY IN LISTS RUNTIME_PROVIDER_LIBS)
    list(APPEND RUNTIME_BUILD_ARGS --extra-archive "$<TARGET_FILE:${LIBRARY}>")
  endforeach()
  set(SCENE_VIDEO_OBJECTS "")
  set(SCENE_VIDEO_SOURCES "")
  foreach(SOURCE_NAME _ZN9BootScene13InitResourcesEv.cpp _ZN9BootScene8BehaviorEv.cpp
      _ZN10dScTitle_c13InitResourcesEv.cpp _ZN8dScene_c22ResetHardwareRegistersEv.cpp
      _ZN8dScene_c20Initialise3dGraphicsEv.cpp func_0205583c.c func_020556d0.c
      func_02049ee8.c func_0204af3c.c
      func_0204b244.c func_0204b028.c func_0204b460.c
      func_0204b81c.c func_0204bbd8.c func_0204be40.c
      func_0204c194.c func_0204c24c.c
      engine/fader/_ZN10FaderColor11AdvanceFadeEv.cpp
      engine/fader/_ZN9FaderWipe11AdvanceFadeEv.cpp
      _ZN7dWipe_c14SetForwardTimeEj.cpp
      _ZN7dWipe_c15SetBackwardTimeEj.c
      func_0202f58c.c
      func_0202fb30.c
      _ZN10dScEntry_c13InitResourcesEv.cpp
      _ZN10dScEntry_c8BehaviorEv.cpp
      _ZN10dScEntry_c15graphCallback_c14GraphCallback2Ev.cpp
      _ZN9dScDSMT_c13InitResourcesEv.cpp
      func_ov007_020b413c.c
      func_ov007_020b6c54.c
      func_ov007_020b6f4c.c
      func_ov007_020b7138.c
      func_ov007_020bd1c0.c
      func_ov007_020bd500.cpp
      func_ov007_020bd5a8.cpp
      func_ov007_020bd648.c
      func_ov007_020bd960.c
      func_ov007_020bf690.c
      func_ov007_020bf790.c
      func_ov007_020c0078.c
      func_ov007_020c03b0.c
      func_ov007_020c076c.c
      func_ov007_020c1404.c
      func_ov007_020c1448.c
      func_ov007_020c19cc.c
      func_ov007_020c232c.c
      func_ov007_020c49bc.c
      func_ov007_020c4fcc.cpp
      func_ov007_020c86c4.c
      func_ov007_020c897c.c
      func_ov007_020c8bc8.cpp
      func_ov007_020c8c88.c
      func_ov007_020ca5f0.c
      func_ov007_020ca86c.c
      func_ov007_020caeac.c
      func_ov007_020c421c.c
      func_ov007_020c42f8.c
      func_ov007_020c4684.c
      func_ov007_020c8b34.c
      func_ov007_020b6eb4.c
      func_ov007_020bd4e8.c
      d_s_stage.cpp
      _ZN5Stage13InitResourcesEv.cpp
      _ZN5Stage14LoadGraphics2DEbi.cpp
      _ZN5Stage10PS_CleanupEv.cpp
      _ZN5Stage7PS_InitEv.cpp
      _ZN5Stage7VE_InitEv.cpp
      _ZN5Stage9LC_UpdateEv.cpp
      _ZN5Stage9PS_UpdateEv.cpp
      _ZN5Stage9RenderFogEv.cpp
      _ZN5Stage9VE_UpdateEv.cpp
      _ZN3HUD13InitResourcesEv.cpp
      _ZN7Minimap13InitResourcesEv.cpp
      _ZN7Minimap8BehaviorEv.cpp
      func_ov002_020f1fcc.c
      func_ov002_020f23f0.c
      func_ov002_020f2aec.c
      func_ov002_020f2bf4.c
      func_ov002_020f30f8.c
      func_ov002_020f335c.c
      func_ov002_020f340c.c
      func_ov002_020f34bc.c
      func_ov002_020f35e4.c
      func_ov002_020f3828.c
      func_ov002_020f5b24.c
      func_ov002_020f5fe4.c
      _ZN6Camera6RenderEv.cpp
      _ZN3G3X6SetFogEbiii.cpp
      _ZN3G3X11SetFogTableEPv.cpp
      func_02029408.c
      func_0205f650.c
      func_0205f68c.c
      FUN_02029a68.c
      _ZN7Message12UpdateWindowEv.cpp
      _ZN7Message13DisplaySavingEt.cpp
      func_0201f32c.c
      func_020321fc.c
      func_020345b0.c)
    get_filename_component(SYMBOL "${SOURCE_NAME}" NAME_WE)
    list(APPEND SCENE_VIDEO_OBJECTS "${RUNTIME_DIR}/scene-video/${SYMBOL}.o")
    list(APPEND SCENE_VIDEO_SOURCES "${PROJECT_SOURCE_DIR}/upstream/src/${SOURCE_NAME}")
    list(APPEND RUNTIME_BUILD_ARGS --extra-object "${RUNTIME_DIR}/scene-video/${SYMBOL}.o")
  endforeach()
  add_custom_command(OUTPUT ${SCENE_VIDEO_OBJECTS}
    COMMAND "${HOST_PYTHON}" "${PROJECT_SOURCE_DIR}/tools/native_scene_video.py"
    DEPENDS ${SCENE_VIDEO_SOURCES} "${PROJECT_SOURCE_DIR}/tools/native_scene_video.py"
      "${PROJECT_SOURCE_DIR}/tools/native_vtable.py" "${PROJECT_SOURCE_DIR}/tools/native_vtable_rules.json"
      "${PROJECT_SOURCE_DIR}/tools/native_operators.py"
      "${PROJECT_SOURCE_DIR}/include/sm64ds/native_register.hpp"
      "${PROJECT_SOURCE_DIR}/include/sm64ds/native_video.hpp"
      "${PROJECT_SOURCE_DIR}/build/native-core/report.json"
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" VERBATIM)
  set_source_files_properties(${SCENE_VIDEO_OBJECTS} PROPERTIES GENERATED TRUE EXTERNAL_OBJECT TRUE)
  # The manual orbit is gated by the original Camera lifecycle and keeps the
  # complete original collision body as its inactive/fallback path. Rename
  # lifecycle originals as a unit; public wrappers retain the native ABI.
  set(NATIVE_CAMERA_OBJECTS "")
  foreach(OBJECT camera-0 camera-1 camera-2 sm64ds_original_camera_init
      sm64ds_original_camera_cleanup sm64ds_original_camera_pending_destroy)
    list(APPEND NATIVE_CAMERA_OBJECTS "${RUNTIME_DIR}/camera/${OBJECT}.o")
    list(APPEND RUNTIME_BUILD_ARGS --extra-object "${RUNTIME_DIR}/camera/${OBJECT}.o")
  endforeach()
  set(NATIVE_CAMERA_ORIGINAL_SOURCES
    src/func_02009e70.cpp src/_ZN6Camera13InitResourcesEv.cpp
    src/_ZN6Camera16CleanupResourcesEv.cpp src/_ZN6Camera16OnPendingDestroyEv.cpp)
  set(NATIVE_CAMERA_SOURCE_FILES "")
  foreach(SOURCE_NAME IN LISTS NATIVE_CAMERA_ORIGINAL_SOURCES)
    list(APPEND RUNTIME_BUILD_ARGS --replace-source "${SOURCE_NAME}")
    list(APPEND NATIVE_CAMERA_SOURCE_FILES "${PROJECT_SOURCE_DIR}/upstream/${SOURCE_NAME}")
  endforeach()
  add_custom_command(OUTPUT ${NATIVE_CAMERA_OBJECTS}
    COMMAND "${HOST_PYTHON}" "${PROJECT_SOURCE_DIR}/tools/native_camera_body.py"
      --out "${RUNTIME_DIR}/camera" --summary
    DEPENDS ${NATIVE_CAMERA_SOURCE_FILES}
      "${PROJECT_SOURCE_DIR}/source/game_port/native_camera.cpp"
      "${PROJECT_SOURCE_DIR}/source/core/camera_orbit.cpp"
      "${PROJECT_SOURCE_DIR}/include/sm64ds/native_camera.hpp"
      "${PROJECT_SOURCE_DIR}/include/sm64ds/camera_orbit.hpp"
      "${PROJECT_SOURCE_DIR}/tools/native_camera_body.py"
      "${PROJECT_SOURCE_DIR}/tools/native_operators.py"
      "${PROJECT_SOURCE_DIR}/build/native-core/report.json"
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" VERBATIM)
  set_source_files_properties(${NATIVE_CAMERA_OBJECTS} PROPERTIES GENERATED TRUE EXTERNAL_OBJECT TRUE)
  # These collision objects contain the recovered sphere/line bodies and their
  # native integer-sqrt provider; they do not carry diagnostic entry points.
  foreach(OBJECT sphere line bridge)
    list(APPEND RUNTIME_BUILD_ARGS --extra-object "${PROJECT_SOURCE_DIR}/build/native-collision/${OBJECT}.o")
  endforeach()
  list(APPEND RUNTIME_BUILD_ARGS --extra-object "${PROJECT_SOURCE_DIR}/build/native-adapters/original_check_input.o")
  add_custom_command(OUTPUT "${RUNTIME_DIR}/original.a" "${RUNTIME_DIR}/native_tables.o"
      "${RUNTIME_DIR}/unavailable.o" "${RUNTIME_DIR}/data-symbols.rsp" "${RUNTIME_DIR}/data-symbols.ld"
    COMMAND "${HOST_PYTHON}" "${PROJECT_SOURCE_DIR}/tools/build_native_runtime.py"
      "$<$<BOOL:$<TARGET_OBJECTS:sm64ds_native_services>>:--extra-object>"
      "$<JOIN:$<TARGET_OBJECTS:sm64ds_native_services>,;--extra-object;>"
      ${RUNTIME_BUILD_ARGS}
    # A target dependency orders compilation; the object dependencies also
    # regenerate ownership/trap tables when a provider's symbols change.
    DEPENDS sm64ds_native_services "$<TARGET_OBJECTS:sm64ds_native_services>"
      ${RUNTIME_PROVIDER_LIBS} ${SCENE_VIDEO_OBJECTS} ${NATIVE_CAMERA_OBJECTS}
      "${PROJECT_SOURCE_DIR}/tools/build_native_runtime.py"
      "${PROJECT_SOURCE_DIR}/tools/native_data_layout.py"
      "${PROJECT_SOURCE_DIR}/build/native-core/report.json"
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    COMMAND_EXPAND_LISTS VERBATIM)
  add_custom_target(sm64ds_native_bundle DEPENDS "${RUNTIME_DIR}/original.a"
    "${RUNTIME_DIR}/native_tables.o" "${RUNTIME_DIR}/unavailable.o")
  set_source_files_properties("${RUNTIME_DIR}/native_tables.o" "${RUNTIME_DIR}/unavailable.o"
    PROPERTIES GENERATED TRUE EXTERNAL_OBJECT TRUE)
  add_executable(sm64ds-native native_main.cpp "$<TARGET_OBJECTS:sm64ds_native_services>"
    ${SCENE_VIDEO_OBJECTS}
    ${NATIVE_CAMERA_OBJECTS}
    "${RUNTIME_DIR}/native_tables.o" "${RUNTIME_DIR}/unavailable.o"
    "${PROJECT_SOURCE_DIR}/build/native-collision/sphere.o"
    "${PROJECT_SOURCE_DIR}/build/native-collision/line.o"
    "${PROJECT_SOURCE_DIR}/build/native-collision/bridge.o"
    "${PROJECT_SOURCE_DIR}/build/native-adapters/original_check_input.o")
  add_dependencies(sm64ds-native sm64ds_native_bundle)
  target_link_libraries(sm64ds-native PRIVATE sm64ds_native_services
    "-Wl,--start-group" "${RUNTIME_DIR}/original.a" ${RUNTIME_PROVIDER_LIBS}
    sm64ds_game_renderer sm64ds_game_data sm64ds_native_data "-Wl,--end-group")
  target_link_options(sm64ds-native PRIVATE "-Wl,-T,${RUNTIME_DIR}/data-symbols.ld"
    "-Wl,-Map=${RUNTIME_DIR}/runtime.map" -Wl,-z,noexecstack
    "-Wl,--defsym,sm64ds_diagnostic_rw_start=ADDR(.data)"
    "-Wl,--defsym,sm64ds_diagnostic_rw_end=__bss_end__")
  find_program(NATIVE_3DSXTOOL 3dsxtool HINTS /opt/devkitpro/tools/bin REQUIRED NO_CMAKE_FIND_ROOT_PATH)
  add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/sm64ds-native.3dsx"
    COMMAND "${HOST_PYTHON}" "${PROJECT_SOURCE_DIR}/tools/native_3dsx_elf.py"
      "$<TARGET_FILE:sm64ds-native>" "${RUNTIME_DIR}/portable.elf"
    COMMAND "${NATIVE_3DSXTOOL}" "${RUNTIME_DIR}/portable.elf"
      "${CMAKE_CURRENT_BINARY_DIR}/sm64ds-native.3dsx"
    DEPENDS sm64ds-native "${PROJECT_SOURCE_DIR}/tools/native_3dsx_elf.py" VERBATIM)
  add_custom_target(sm64ds-native_3dsx DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/sm64ds-native.3dsx")
endif()
