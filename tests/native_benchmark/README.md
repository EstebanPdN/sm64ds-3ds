Diagnostic-only original-game campaign
=====================================

This helper does not run unless both Developer memory dumps are enabled and
`sdmc:/3ds/sm64ds/benchmark-profile.txt` exists with this exact three-line format:

    SM64DS_BENCHMARK_V1
    scenario=bob-mario
    model=old

Accepted scenarios:castle-yoshi,bob-mario,whomp-luigi,bay-wario,clock-wario.
Model is old or new and must match the actual model supplied by the caller.
Malformed/unknown/extra/reordered fields are rejected. The file is read once;
it is neither deleted nor rewritten. A new isolated run uses a new marker.
The production default without this marker is a no-op.

Root integration API (game thread only):

    bool request = native_benchmark::frame(scene,updates,
        native_diagnostics::enabled(),actual_new3ds,svcGetSystemTick());
    // If true, trigger the existing full VM capture.
    // After both VM and GPU diagnostic captures finish:
    native_benchmark::capture_completed(success,svcGetSystemTick());

Only the benchmark's own requests receive the completion callback. A failed
capture aborts that profile; three requests never imply three successful dumps.
The model blocks further requests until the callback. All time spent blocked
inside VM/GPU capture is excluded from the10/30/60second sample clock.

At title scene1/update>=900, it calls the original StartFile(level,entrance)
once. StartFile sets the single-player globals through original routines,
requests LoadLevelNoReturn with star1, and uses original StartSceneFade(3,0,
0x7fff). No scene globals are overwritten by this helper. Once actual Stage3,
matching level and a controllable player exist, it invokes the original
Player::SetRealCharacter when needed. This is the character-door operation:
it releases/loads real model/animation resources, changes the real character,
heals the player and updates the selected character through the original code.
It is not the cap-disguise API. After the actual body-model pointer is ready,
120 stable original game updates pass before the sample clock starts.

The helper changes no HID input, positions, actor fields, resource tables,
script pointers, camera or renderer. A user/runner can move through real HID
between capture points; this module by itself samples the original spawn view.
Calling the original game APIs mutates normal game state and its selected-save
state in memory. Use a private disposable save copy; never the user's save.
This does not demonstrate earning unlocks or natural menu/course progression.

Five scenarios are paired across Old and New models (10 isolated runs total):

    Castle grounds: level1,entrance0,star1,Yoshi3
    Bob-omb Battlefield: level6,entrance0,star1,Mario0
    Whomp's Fortress: level7,entrance0,star1,Luigi1
    Jolly Roger Bay: level8,entrance0,star1,Wario2
    Tick Tock Clock: level27,entrance0,star1,Wario2

Level IDs are verified against the pinned original52-entry level-root table at
0x02092208, the8+level overlay mapping, each original level-header model handle,
and the canonical handles catalog, resolving to main_castle,bombhei_map,
battan_king_map,kaizoku_irie and clock_tower. Character IDs are original enum
values. Player.h's actual body models are checked through GetBodyModelID and a
non-null indexed model pointer. Logs include level,area,character,updates,
actual scene,model validation,successful dump count and API provenance.

Evidence boundary: tests simulate only parser/state scheduling. No gameplay,
level-loader success, character animation or FPS result follows from them.
A profile is complete only after actual valid Stage/player/model validation,
settling and three successful real capture callbacks. Root must verify each
capture's complete flag and mapped-region integrity independently.

Host check:

    clang++ -std=c++17 -O1 -g -fsanitize=address,undefined -Wall -Wextra -Werror tests/native_benchmark/test_model.cpp -o /tmp/sm64ds-benchmark-model
    /tmp/sm64ds-benchmark-model

The1,880 checks cover strict LF/CRLF parsing,all10 paired profile combinations,
diagnostics/model refusal,single StartFile/SetRealCharacter decisions,real
model-readiness gating,120update settling,10/30/60second capture decisions,
100second blocked capture exclusion,successful-versus-request counts and
failure abort. native_benchmark.cpp separately compiles as an ARM11 object.
NoAzahar run was performed by this subtask.
