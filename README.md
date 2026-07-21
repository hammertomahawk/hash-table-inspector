# Hash Table Inspector

Hash Table Inspector is a Codex Week project that observes a real C11
byte-key-to-`int64_t` hash table and renders copied native events and
snapshots in an Android app built with Kotlin, JNI, and Jetpack Compose. The C
table is the only live source of truth.

Status: the host-native sanitizer suite passes, and the Android/JNI/Compose
slice builds for `arm64-v8a` and is physically validated on a Samsung Galaxy
S25 Ultra running API 36. See the consolidated
[physical-device validation](docs/device-validation.md) evidence.

## Install on Android

The hackathon APK requires Android 8.0 / API 26 or newer and an `arm64-v8a`
device. Download `debug-app.apk` from the repository's
[Releases page](https://github.com/hammertomahawk/hash-table-inspector/releases).
This release asset is byte-for-byte the same APK supplied directly to the
judges (SHA-256
`2bf9444a05856af2481be5531b03dd3c00e94aa05bc7c54637e149aa60b8dbd0`).

Sideload it by opening the downloaded APK, or install it with Platform-Tools:

```bash
adb install -r debug-app.apk
```

Android may ask you to allow installation from the browser, file manager, or
other unknown app source used to open the APK. After launching, tap
**Run deterministic demo** for the complete evaluation path.

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
- Android submission target: one physical Samsung Galaxy S25 Ultra, validated
  over USB ADB, `arm64-v8a` only.

The canonical design, ownership rules, C API, planned JNI boundary, milestones,
risks, and acceptance criteria are in
[docs/architecture-plan.md](docs/architecture-plan.md).
Automated device evidence, manual UI findings, and current limitations are in
[docs/device-validation.md](docs/device-validation.md).

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
docs/device-validation.md    consolidated physical-device evidence
```

Inspection and corruption are behind `HT_ENABLE_INSPECTION` and
`HT_ENABLE_CORRUPTION`, both off by default in the core. The Android demo opts
in explicitly; a production-oriented build can compile and link the core
without the timeline, adapter, JSON, JNI debug surface, or corruption code.

## Research and toolchain status

Initial research on 2026-07-18 inspected
[hammertomahawk/hash-table-inspector#1](https://github.com/hammertomahawk/hash-table-inspector/issues/1),
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
was installed or updated by this work. One early local
`~/Library/Android/sdk/platform-tools/adb devices -l` attempt tried to start the
ADB server but failed with `ADB server didn't ACK` and `failed to start daemon`.
At that checkpoint, the user declined a retry because the Mac and phone were
not on the same network, and no device contact occurred. Later, the authorized
Galaxy S25 Ultra connected over USB; APK installation, cold launch, runtime
health checks, and human UI validation succeeded. The historical failure and
later successful evidence are consolidated in
[docs/device-validation.md](docs/device-validation.md).

## Explicitly deferred hardening

The submission does not include byte-budget or operation-group timeline
eviction, degraded-history recovery, additional corruption modes, exhaustive
allocation-failure injection, randomized oracle testing, fuzzing,
cross-language golden fixtures, production symbol audits, Android `x86_64`,
emulator tests, or CI. These remain valid post-submission hardening work.

## License

Copyright 2026 Fenris Embedded, LLC. Released under the
[MIT License](LICENSE).

## Built with Codex

This is a selective submission record, not a prompt transcript.

| Date | Major task | Important human decisions | Where Codex accelerated the work | Validation |
| --- | --- | --- | --- | --- |
| 2026-07-18 to 2026-07-20 | Issue, repository, toolchain, and architecture research | Start with a real C11 table and Android Kotlin/JNI/Compose viewer; use reusable inspection primitives but no DSL; plan before implementation | Inspected the source issue and empty checkout, inventoried local tooling, researched the Android baseline, and turned the broad proposal into a concrete architecture, boundary, memory model, risks, milestones, and vertical slice | Issue inspected with `gh`; repository and tools queried directly; strict C11 ASan/UBSan probe passed; findings recorded in README and the architecture plan |
| 2026-07-20 | Host-native submission slice | Author the table here; target the physical Galaxy S25 Ultra and `arm64-v8a`; keep host builds in Bash/Clang; allow only minimal Android CMake; use copied JSON v1, 64 full frames, and size-only corruption; defer the named hardening work | Implemented the real table, stable hooks, generic timeline, immutable snapshot adapter, invariant validator, isolated corruption/restore flow, JSON encoder, deterministic tests, and synchronized documentation | `./scripts/test-native.sh` compiled under strict C11 warnings with ASan/UBSan and all 6 required native tests passed; no Android validation is claimed yet |
| 2026-07-20 | Pre-Android native-slice checkpoint | Require a concise, implementation-preserving handoff with exact evidence before JNI work; do not install or commit | Audited the implemented files, Git state, limitations, and remaining Android/JNI sequence and recorded them in `docs/native-slice-status.md` | Native suite rerun: 6/6 passed; Bash syntax and strict inspection-disabled core compile exited 0; report is 74 lines; no `HEAD`, staged changes, or tracked diff |
| 2026-07-20 | First Android/JNI/Compose vertical slice | Keep the installed SDK/NDK/CMake/JBR immutable; permit only pinned project dependency downloads; package `arm64-v8a` only; after one failed local ADB query, decline the retry while the Mac and phone are on different networks and defer device work | Added the reproducible Gradle app, strict C11 Android target, copied JNI calls and full-timeline contract, serialized Kotlin owner, and minimal interactive/deterministic Compose inspector | Native suite passed 6/6; `./gradlew :app:assembleDebug` passed; APK metadata reports target API 36 and only `arm64-v8a`; JNI signatures match; the ADB server failed to start before device contact, so no physical validation is claimed |
| 2026-07-20 | Physical-device validation and human-directed UI/lifecycle iteration | Keep the vertical timeline; defer graph and horizontal history; constrain the scrolling viewport to safe drawing bounds; make the historical return action explicit; retain the native session across configuration recreation but defer process-death restoration | Ran targeted USB installation, launch, process/focus, and logcat checks; implemented the inset, Material action, and ViewModel ownership changes in response to physical findings; consolidated the evidence without treating manual observations as automation | Automated native and Android builds passed; USB launch loaded the JNI library with no crash or fatal signal; human light/dark testing verified system-bar clearance and contrast, the **Return to live** action, operations, and preservation of the 17-frame timeline, frame 3 selection, and scroll position |
| 2026-07-21 | Public submission packaging | Publish the exact judge-supplied APK as a release asset rather than commit a binary; add the MIT license and public source-issue link | Located and fingerprinted the validated artifact, added concise installation guidance, and prepared the public release metadata | Local judge copy and validated build output matched byte-for-byte; release download checksum and public README links were checked after publishing |
