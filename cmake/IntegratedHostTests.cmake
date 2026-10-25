# Portable checks for the maintained integration. Resource-backed GPU/ARM
# differentials stay in the candidate builder and keep their own provenance.
set(SM64DS_INTEGRATED_DIR "${PROJECT_SOURCE_DIR}/source/integrated_native")

function(sm64ds_integrated_test name)
  add_executable("test_integrated_${name}" ${ARGN})
  target_include_directories("test_integrated_${name}" PRIVATE
    "${SM64DS_INTEGRATED_DIR}"
    "${SM64DS_INTEGRATED_DIR}/options"
    "${SM64DS_INTEGRATED_DIR}/movement"
    "${PROJECT_SOURCE_DIR}/third_party/libsm64-motion/src")
  target_compile_options("test_integrated_${name}" PRIVATE
    -UNDEBUG -Wall -Wextra -Werror)
  set(arguments)
  if(name STREQUAL "gameplay_camera")
    list(APPEND arguments "${CMAKE_CURRENT_BINARY_DIR}/gameplay-overlay.raw")
  endif()
  add_test(NAME "integrated_${name}" COMMAND "test_integrated_${name}" ${arguments})
  set_tests_properties("integrated_${name}" PROPERTIES LABELS "integrated;host")
endfunction()

set(settings "${SM64DS_INTEGRATED_DIR}/options/enhance_settings.cpp")
sm64ds_integrated_test(pause tests/options_menu/test_native_pause.cpp
  "${SM64DS_INTEGRATED_DIR}/options/native_pause_menu.cpp" "${settings}")
sm64ds_integrated_test(pause_art tests/options_menu/test_native_pause_art.cpp
  "${SM64DS_INTEGRATED_DIR}/options/native_pause_menu.cpp" "${settings}")
sm64ds_integrated_test(camera tests/options_camera/test.cpp
  "${SM64DS_INTEGRATED_DIR}/options/cstick_camera.cpp"
  "${SM64DS_INTEGRATED_DIR}/options/analog_pad.cpp" "${settings}")
sm64ds_integrated_test(stylus tests/options_camera/test_stylus.cpp)
sm64ds_integrated_test(gameplay_camera tests/options_camera/test_gameplay.cpp
  "${SM64DS_INTEGRATED_DIR}/options/cstick_camera.cpp" "${settings}")
sm64ds_integrated_test(display tests/options_display/test_plan.cpp)
sm64ds_integrated_test(title_background tests/options_display/test_title_background.cpp)
sm64ds_integrated_test(intro_skip tests/options_menu/test_intro_skip.cpp)
sm64ds_integrated_test(diagnostic_panel tests/options_menu/test_diagnostic_panel.cpp)
sm64ds_integrated_test(dump_transfer tests/options_menu/test_dump_transfer.cpp)
sm64ds_integrated_test(lower_art tests/options_menu/test_native_lower_art.cpp)
sm64ds_integrated_test(overlay_publication tests/test_overlay_publication.cpp)
sm64ds_integrated_test(setup_power tests/test_integrated_setup_power.cpp)
sm64ds_integrated_test(benchmark tests/native_benchmark/test_model.cpp)
sm64ds_integrated_test(motion_buttons tests/movement_lab/test_motion_buttons.cpp)
sm64ds_integrated_test(motion_retry tests/movement_lab/test_retry_gate.cpp)
sm64ds_integrated_test(stable_surfaces tests/movement_lab/test_stable_surfaces.cpp)
sm64ds_integrated_test(stable_platforms tests/movement_lab/test_stable_platforms.cpp)
sm64ds_integrated_test(raycast tests/movement_lab/test_raycast_arithmetic.cpp)
sm64ds_integrated_test(kcl_prism tests/movement_lab/test_kcl_prism.cpp)
sm64ds_integrated_test(audio_gain tests/integrated_audio_save/test_audio.cpp)
sm64ds_integrated_test(boot_settings tests/integrated_audio_save/test_boot_settings.cpp "${settings}")
sm64ds_integrated_test(profile_probe tests/test_profile_probe.cpp)
target_include_directories(test_integrated_profile_probe PRIVATE
  "${PROJECT_SOURCE_DIR}/third_party/sm64ds-3ds/port/platform/3ds")
