# Free camera and Circle Pad checks

Compile on the host from the repository root:

```sh
clang++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I source/integrated_native/options tests/options_camera/test.cpp \
  source/integrated_native/options/cstick_camera.cpp \
  source/integrated_native/options/analog_pad.cpp \
  source/integrated_native/options/enhance_settings.cpp \
  -o /tmp/sm64ds-camera-test
/tmp/sm64ds-camera-test
```

The test links the actual settings implementation, not a duplicate settings stub.
It checks equal opposite yaw and pitch, unchanged command bits in free mode,
neutral deadzone, overlay/cutscene/recenter gates, zone reset, analog monotonicity,
all movement directions and sensitivity/run thresholds, and local controller mode.
The angular checks reconstruct atan2(vertical,horizontal) from real orbit
components for both signs, four deflections, four sensitivities and three
camera distances. Before bounds, yaw and pitch differ by less than four of
65536 angle units per turn (0.022 degrees), including pixel quantization.
The Free path skips the original additional pitch-only easing after keeping
its mode, floor and ceiling bounds. One camera sensitivity governs both axes;
the legacy separate pitch setting remains readable but does not skew Free.
It does not demonstrate physical stick calibration, visible movement or collision.
Those require the built game and actual gameplay validation.

`tools/prepare_integrated_camera.py` patches only a disposable generated source
copy. It refuses the real third_party path and a duplicate patch. The root prepare
pipeline must restore its generated dependency files from the pinned source before
calling it again. It preserves scripted camera dispatch and the normal follow
camera's distance, wall, ceiling and floor constraints after `L_A814`.

2026-10-21: `test_stylus.cpp` now checks an overshooting out-and-back gesture
between two game updates. The implementation accumulates net displacement and
clamps only when consumed, avoiding a false opposite rotation from individually
clamped HID segments. Native touch coordinates are bounded to 320x240, and
release/reset still discard pending movement. This is a host cadence check,
not physical Old3DS acceptance.

`tests/options_menu/test_prepare_contracts.py` executes the actual generated
native CheckCameraInput branch for every DS touch coordinate and all controller
modes. It checks that both old arrow and zoom hitboxes are inactive and stale
pen-repeat state is cleared; physical button mappings remain in CheckInput.
The approved replacement two-button HUD/touch integration is still pending.
