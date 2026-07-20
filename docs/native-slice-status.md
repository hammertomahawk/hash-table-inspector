# Host-native slice status

Checkpoint: 2026-07-20. The host-native submission slice is complete; Android and JNI integration have not started.

## Implemented files and features

- `native/hash_table/include/hti/hash_table.h`, `src/hash_table.c`, and `src/hash_table_internal.h`: real opaque C11 separate-chaining table, owned arbitrary byte keys, `int64_t` values, stable IDs, deterministic hashes, all required operations, 3/4 threshold, and doubling rehash.
- `native/inspection/include/hti/inspection.h` and
  `src/inspection_timeline.c`: reusable event envelope and owned 64-frame FIFO.
- `native/adapters/hash_table/include/hti/hash_inspector.h` plus its four `src/` files: real-table trace capture, immutable one-arena snapshots, independent invariant checks, JSON contract v1, and safe demo-key policy.
- Size corruption clones and quarantines the table; validation detects the mismatch and Restore reinstates the untouched clean table.
- Rehash capture records stable 4-bucket pre-rehash, 8-bucket post-rehash, and final insertion frames with preserved IDs.
- `native/tests/test_main.c` and `scripts/test-native.sh`: deterministic coverage and a direct Bash + Clang C11 entry point.
- `README.md` and `docs/architecture-plan.md`: research, approved cut, ownership/boundary design, milestones, deferrals, and Codex record.

## Exact verification commands and results

```bash
./scripts/test-native.sh
```

Result (exit 0):

```text
ok - core owns arbitrary byte keys
ok - insert/update, lookup hit/miss, and delete
    deterministic demo collision: a / b
ok - deterministic collision snapshot
ok - stable 4-to-8 rehash frames and JSON
ok - size corruption detection and clean restore
ok - timeline retains at most 64 full frames
6 native tests passed
```

This compiles with strict warnings as errors plus ASan and UBSan. Apple ASan's unsupported leak detector is disabled; address and undefined-behavior checks remain active.

```bash
bash -n scripts/test-native.sh
clang -std=c11 -O1 -g -Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wformat -Wformat-security -Wundef -Wpointer-arith -Wwrite-strings -Wvla -I native/hash_table/include -I native/hash_table/src -c native/hash_table/src/hash_table.c -o /tmp/hash-table-inspector-core-only.o
```

Both exited 0 with no diagnostics. The second leaves inspection and corruption disabled and verifies the standalone core.

## Git status and diff summary

`git status --short` at this checkpoint:

```text
?? README.md
?? docs/
?? native/
?? scripts/
```

There is no `HEAD` commit (`git rev-parse --verify HEAD` exits 128). All 15 project files, including this report, are untracked. `git diff --stat` and `git diff --cached --stat` both exit 0 with no output because nothing is tracked or staged. No commit was created.

## Known limitations or failures

- No required native test fails; Android/device behavior is not yet validated.
- No JNI, Kotlin, Compose, Gradle, or Android CMake files exist yet.
- The adapter accepts only 1-32 safe ASCII key bytes; JSON v1 relies on that restriction, while the table core accepts arbitrary bytes.
- History is 64 full frames with per-frame FIFO eviction; rewind is view-only. Byte budgets and whole-operation eviction are deferred.
- Only size corruption is implemented. Allocation failure injection, randomized/oracle tests, fuzzing, C/Kotlin goldens, and symbol audits are deferred.
- The future Kotlin owner must serialize calls and prevent use after close; the native session is not internally thread-safe.

## Remaining Android/JNI integration steps

1. Finish and verify Quail 2, SDK Platform 36.1, Build-Tools 36, Platform-Tools, JDK 17, NDK `28.2.13676358`, and minimal CMake.
2. Scaffold the smallest pinned Gradle/AGP/Kotlin/Compose app, with no API 37 requirement and only `arm64-v8a`.
3. Add a C-only CMake target for the existing C11 sources and enable inspection/corruption only for the demo library.
4. Add C-only JNI create, put, lookup, delete, validate, corrupt-size, restore, full-timeline, and close calls. Copy key input; return copied UTF-8 JSON `ByteArray`; free native JSON before return.
5. Add one serialized Kotlin `Closeable`: guard/clear its opaque handle, reject post-close calls, validate JSON v1, create immutable DTOs, and retain at most 64 copied frames.
6. Build the Compose commands, metrics, buckets/chains, outcomes, invariant, rehash, and historical/live views.
7. Pair the Galaxy S25 Ultra through wireless ADB, confirm arm64, install/launch, and run the deterministic demo. Emulator, `x86_64`, API 37, and CI stay out of scope.
