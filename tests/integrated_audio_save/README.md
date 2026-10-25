# Audio and storage regression boundary

Run from the project root, without Azahar:

```sh
DEVKITPRO=/opt/devkitpro \
DEVKITARM=/opt/devkitpro/devkitARM \
build/code-pass-2026-10-21/venv/bin/python tests/integrated_audio_save/run.py --arm
```

Without `--arm`, the seven host groups still run with ASan/UBSan and warnings as
errors. Each invocation creates its own `build/integrated-native/audio-save-host-*`
directory with logs, source hashes and `report.json`. Optional `--bench` adds an
unsanitized `-O2` host microbenchmark of legacy gain versus ramp with detailed
metrics off/on at 0/100/200%, including seven relaxed atomic stores per new
block. It is informational and makes no ARM/FPS claim. Cart/menu probes write only
temporary files. The runner verifies hashes of every audio file and the touched
save HAL/probes against the vendor lock before and after testing.

Integration contract for the parent:

```python
from prepare_integrated_audio import prepare as prepare_audio
from prepare_integrated_save import prepare as prepare_save
changed.extend(prepare_audio(output))
changed.extend(prepare_save(output))
```

Call once per fresh generated overlay, after local options copying and optional
ROM-only path replacement. The audio pass accepts either the pinned output or
the exact existing render60 master-gain patch; placing the call immediately
after that patch works. Alternatively replace that whole old gain block with
the call. Do not run the legacy gain patch after `prepare_audio`.

The patchers copy their required headers, return changed relative file paths,
refuse vendor paths and source/header symlink escapes, and fail before modifying
sources if their exact anchors change. No CMake source registration is needed.
The central builder can run this test runner as a separate host gate. These
changes do not wire the gate into the central build themselves.

`enhance::audio_diagnostics::snapshot()` in `audio_diagnostics.hpp` exposes seven
lock-free 32-bit counters: submitted/completed buffers, observed queue drains,
clipped samples after user gain, maximum absolute input/output, and a flag
indicating whether detailed metrics were enabled for the last processed block.
Peaks/clipped counts are collected only when `live().developer_overlay` is set;
they retain prior measurements when disabled. They cannot establish absence of
clipping in unmeasured blocks. The output pump is the sole writer; a concurrent snapshot is advisory,
not transactional. Counts wrap modulo 2^32. Opening output resets the counters;
closing retains them for inspection. No realtime logging or file I/O is added.
The existing submitted/completed getter ABI remains unchanged.

`queue_drains` excludes initial prefill and counts a pump observation where all
previously owned NDSP buffers are DONE before refill. It is a lower bound on
queue exhaustion, not a missing-sample count or proof of physical crackles.
The existing three 512-frame buffers, 128-frame locked render chunks, rate,
interpolation and scheduling remain intact. The waiting flag uses an atomic
load compatible with the producer's existing atomic builtins. PCM processing
has fixed storage and bounded work per frame, with no locks or allocations.

Gain uses the same exact steady PCM16 result at every integer 0–200% setting,
including bit-exact 100%, steady silence at 0% and saturation at boosted peaks.
Changes ramp over 128 stereo frames (~3.9 ms), with no channel skew. Existing
queued PCM can still delay an audible volume change by the queue's latency.
Intentional clipping above unity remains observable; it is not called repaired.
With detailed metrics off, steady 100% bypasses the PCM loop entirely and
steady 0% uses a bounded memset. Active transitions are always processed.

Storage retains the 8192-byte cart format, checksums, mirrors and return codes.
Settings no longer unlink the current file before replacement. Shared storage
checks write/flush/fsync/close. **3DS always backs up an existing primary first**:
installed libctru 2.7.0 can delete a rename destination and then fail its retry.
Host POSIX prefers replacing rename, falling back after EEXIST/ENOTEMPTY.
Fault injection reproduces delete-then-EIO and verifies target backup-first,
failed commit/rollback recovery and preservation of another backup. A missing primary can read a
complete `.bak`; the next write restores that backup before starting a new
transaction. Another I/O error never becomes a blank save. On a non-replacing
filesystem, an existing unrelated/stale backup is preserved and replacement
fails. The ARM gate records the installed archive hash, disassembly's two
RenameFile calls with DeleteFile between them, and a relocatable link proving
the output and snapshot reader share one inline counter object. This is not a
full game link. Directory rename durability and sudden SD power loss are not proved.

Settings reject oversized (>4095 bytes), embedded-NUL or failed reads before
changing settings or the parse report. Overlong physical INI lines and malformed
sections cannot apply a truncated valid prefix. Invalid serializer buffers are
rejected. Normal file contents, defaults and path names are preserved.
Oversized `SM64DS_SETTINGS` entries no longer use snprintf's required length as
an out-of-range terminator index; later valid entries still apply. The 3DS entry
resets its environment and ignores env.txt in a user build; these helpers do
not reset a host process environment or change that entry contract.

The central pause-settings caller currently only prints `saved=0` on failure.
User-visible error handling is outside this audio/storage-only scope and remains
for the parent. Physical audible tests, actual SD behavior/power interruption,
gameplay save/reopen/progression and linked candidate acceptance remain open.
