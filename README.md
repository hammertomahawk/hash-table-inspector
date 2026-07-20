# Hash Table Inspector

Hash Table Inspector is a Codex Week project that observes a real C11
byte-key-to-`int64_t` hash table and renders copied native events and
snapshots in an Android app built with Kotlin, JNI, and Jetpack Compose. The C
table is the only live source of truth.

Status: the host-native sanitizer suite passes, and the first Android/JNI/
Compose vertical slice assembles successfully for `arm64-v8a`. Physical Galaxy
S25 Ultra installation and validation are intentionally deferred.

## Run the native suite

```bash
./scripts/test-native.sh
```

The Bash script invokes Clang directly as C11 with strict warnings as errors,
AddressSanitizer, and UndefinedBehaviorSanitizer. It does not use CMake or
Node.js and writes its temporary test binary outside the repository.

Current result:

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

On macOS, the script disables only ASan's unsupported leak detector; ASan and
UBSan remain enabled.

## Submission cut

- A real opaque C11 hash table with owned arbitrary byte keys, signed 64-bit
  values, separate chaining, stable entry IDs, deterministic hashes, and 3/4
  growth threshold.
- Insert/update, lookup hit/miss, delete hit/miss, deterministic collision,
  and automatic 4-to-8 rehash behavior.
- Stable trace events that inspect the table at real operation boundaries.
- A small reusable native event/timeline spine plus a hash-specific adapter.
- Independently validated, immutable one-allocation snapshots containing every
  bucket, chain, entry, and copied key.
- At most 64 full frames, evicted oldest-first.
- Size-counter corruption on an isolated clone, quarantine, validation
  failure, and clean restoration.
- Versioned copied JSON v1 operation results. Demo adapter keys are limited to
  1-32 safe ASCII bytes `[A-Za-z0-9._-]`; the table core remains binary-key
  capable.
- Android submission target: one physical Samsung Galaxy S25 Ultra over
  wireless ADB, `arm64-v8a` only.

The canonical design, ownership rules, C API, planned JNI boundary, milestones,
risks, and acceptance criteria are in
[docs/architecture-plan.md](docs/architecture-plan.md).

## Architecture

```text
Compose UI
    |
immutable Kotlin state and serialized commands
    |
C-only JNI bridge with copied UTF-8 JSON
    |
hti_session
    |-- actual ht_t instance and stable trace hooks
    |-- hash-table snapshot/validation adapter
    `-- generic 64-frame owned timeline
```

Current source layout:

```text
native/hash_table/           real opaque table core
native/inspection/           generic events and fixed-capacity timeline
native/adapters/hash_table/  snapshots, invariants, corruption, JSON v1
native/jni/                  copied C-only JNI boundary
native/tests/                deterministic host-native coverage
app/                         arm64-only Kotlin and Compose Android app
scripts/test-native.sh       direct Bash + Clang test entry point
docs/architecture-plan.md    canonical approved plan
```

Inspection and corruption are behind `HT_ENABLE_INSPECTION` and
`HT_ENABLE_CORRUPTION`, both off by default in the core. The Android demo opts
in explicitly; a production-oriented build can compile and link the core
without the timeline, adapter, JSON, JNI debug surface, or corruption code.

## Research and toolchain status

Initial research on 2026-07-18 inspected
[hammertomahawk/projects#26](https://github.com/hammertomahawk/projects/issues/26),
the local checkout, and the available toolchain. The issue proposes a broad
data-structure introspection engine; this repository deliberately starts with
one reliable hash-table vertical slice and defers a DSL and generic reflection.

The checkout initially had no commits, configured remote, application code,
Android project, or repository `AGENTS.md`. The host was arm64 macOS with Apple
Clang 21, Xcode 26.6, Git, GitHub CLI, Make, and CMake available. A strict C11
ASan/UBSan probe succeeded. JDK, Android Studio, SDK, NDK, Gradle, Kotlin, ADB,
emulator, and Ninja were not then installed or on `PATH`. No system-wide tools
were installed by Codex.

Toolchain verified on 2026-07-20 without changing the installation:

- Android Studio Quail 2 (`2026.1.2`) and its bundled OpenJDK 21 JBR are usable.
- The SDK is at `~/Library/Android/sdk` with Platform 36.1, Build-Tools
  `36.0.0`, and Platform-Tools present.
- NDK `28.2.13676358` and CMake `3.22.1` are present and pinned by the app.
- The project pins Gradle `9.5.0` with its distribution checksum, AGP `9.3.0`,
  Compose compiler `2.3.21`, Compose BOM `2026.06.00`, and Activity Compose
  `1.12.4`. Repositories are limited to the Gradle Plugin Portal, Google, and
  Maven Central.
- The app compiles against SDK 36.1, targets API 36, and packages only
  `arm64-v8a`; API 37, emulators, `x86_64`, and CI remain out of scope.
- CMake is Android NDK integration only. Host builds and tests remain direct
  Bash + Clang entry points.

No Android Studio, SDK, Build-Tools, Platform-Tools, NDK, CMake, or JBR package
was installed or updated by this work. One local
`~/Library/Android/sdk/platform-tools/adb devices -l` attempt tried to start the
ADB server but failed with `ADB server didn't ACK` and `failed to start daemon`.
The user declined a retry because the Mac and Galaxy S25 Ultra were not yet on
the same network. No pairing, connection, authorization, APK installation,
launch, or physical-device validation occurred; further ADB diagnosis and
device work remain deferred.

## Explicitly deferred hardening

The submission does not include byte-budget or operation-group timeline
eviction, degraded-history recovery, additional corruption modes, exhaustive
allocation-failure injection, randomized oracle testing, fuzzing,
cross-language golden fixtures, production symbol audits, Android `x86_64`,
emulator tests, or CI. These remain valid long-term work after the physical
device vertical slice is reliable.

## Built with Codex

This is a selective submission record, not a prompt transcript.

| Date | Major task | Important human decisions | Where Codex accelerated the work | Validation |
| --- | --- | --- | --- | --- |
| 2026-07-18 to 2026-07-20 | Issue, repository, toolchain, and architecture research | Start with a real C11 table and Android Kotlin/JNI/Compose viewer; use reusable inspection primitives but no DSL; plan before implementation | Inspected the source issue and empty checkout, inventoried local tooling, researched the Android baseline, and turned the broad proposal into a concrete architecture, boundary, memory model, risks, milestones, and vertical slice | Issue inspected with `gh`; repository and tools queried directly; strict C11 ASan/UBSan probe passed; findings recorded in README and the architecture plan |
| 2026-07-20 | Host-native submission slice | Author the table here; target the physical Galaxy S25 Ultra and `arm64-v8a`; keep host builds in Bash/Clang; allow only minimal Android CMake; use copied JSON v1, 64 full frames, and size-only corruption; defer the named hardening work | Implemented the real table, stable hooks, generic timeline, immutable snapshot adapter, invariant validator, isolated corruption/restore flow, JSON encoder, deterministic tests, and synchronized documentation | `./scripts/test-native.sh` compiled under strict C11 warnings with ASan/UBSan and all 6 required native tests passed; no Android validation is claimed yet |
| 2026-07-20 | Pre-Android native-slice checkpoint | Require a concise, implementation-preserving handoff with exact evidence before JNI work; do not install or commit | Audited the implemented files, Git state, limitations, and remaining Android/JNI sequence and recorded them in `docs/native-slice-status.md` | Native suite rerun: 6/6 passed; Bash syntax and strict inspection-disabled core compile exited 0; report is 74 lines; no `HEAD`, staged changes, or tracked diff |
| 2026-07-20 | First Android/JNI/Compose vertical slice | Keep the installed SDK/NDK/CMake/JBR immutable; permit only pinned project dependency downloads; package `arm64-v8a` only; after one failed local ADB query, decline the retry while the Mac and phone are on different networks and defer device work | Added the reproducible Gradle app, strict C11 Android target, copied JNI calls and full-timeline contract, serialized Kotlin owner, and minimal interactive/deterministic Compose inspector | Native suite passed 6/6; `./gradlew :app:assembleDebug` passed; APK metadata reports target API 36 and only `arm64-v8a`; JNI signatures match; the ADB server failed to start before device contact, so no physical validation is claimed |
