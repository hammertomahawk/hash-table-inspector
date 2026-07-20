# Android/JNI slice status

Checkpoint: 2026-07-20. Both slices build; device work is intentionally deferred.

## Verified toolchain and dependencies

| Component | Exact verified version |
| --- | --- |
| SDK | `~/Library/Android/sdk`; Platform `android-36.1`, API 36.1, extension 20, package revision 1 |
| Build-Tools | `36.0.0` |
| Platform-Tools | `37.0.0`; one local ADB startup/device query failed as recorded below |
| NDK | `28.2.13676358`; bundled Clang `19.0.1` |
| CMake | package `3.22.1`; executable `3.22.1-g37088a8` |
| JBR | Android Studio bundled OpenJDK `21.0.10`, build `21.0.10+-117844308-b1163.108` |
| Gradle | wrapper `9.5.0`; distribution SHA-256 `553c78f50dafcd54d65b9a444649057857469edf836431389695608536d6b746` |
| Android Gradle Plugin | `9.3.0` |
| Kotlin | compiler, standard library, and Compose plugin `2.3.21` |
| Compose | BOM `2026.06.00`; resolved UI/runtime/foundation `1.11.3`, Material 3 `1.4.0`; Activity Compose `1.12.4` |

Android Studio Quail 2 (`2026.1.2`) supplied the JBR. No installed tool changed.

## Implemented files and features

- Root Gradle files, wrapper, `.gitignore`, and ignored `local.properties` pin
  repositories, tool versions, SDK location, and reproducible wrapper checksum.
- `app/build.gradle.kts` pins SDK 36.1, target 36, Build-Tools, NDK, CMake, and
  only `arm64-v8a`; `AndroidManifest.xml` defines the launcher activity.
- `app/src/main/cpp/CMakeLists.txt` builds the existing C11 core, inspection
  spine, adapter, JSON encoder, and C-only JNI bridge as one strict shared
  library. Demo inspection and size corruption are explicitly enabled there.
- `native/jni/hash_inspector_jni.c` implements create, put, lookup, delete,
  validate, corrupt-size, restore, full-timeline, and close. Inputs and UTF-8
  JSON outputs are copied; native JSON is freed before returning `ByteArray`.
- `hash_inspector.h` and `hash_json.c` add copied JSON v1 serialization of the
  complete retained timeline; `native/tests/test_main.c` covers that contract.
- `NativeHashSession.kt` is a synchronized, idempotent `Closeable` owner that
  prevents calls after close. `NativeModels.kt` validates JSON v1 and creates
  immutable Kotlin values.
- `MainActivity.kt` provides insert, lookup, delete, validate, corrupt, restore,
  reset, and deterministic-demo controls. It renders outcomes, load/threshold
  metrics, buckets/chains, collisions, validation, rehash frames, quarantine,
  and live/historical selection across at most 64 copied frames.
- `README.md` records the verified toolchain and concise Built with Codex entry.

## Exact validation commands and results

```bash
./scripts/test-native.sh
```

Exit 0: all 6 native tests passed under strict C11 warnings, ASan, and UBSan.

```bash
./gradlew :app:assembleDebug
```

Exit 0: `BUILD SUCCESSFUL`; 38 tasks, 2 executed and 36 up-to-date on the final
run. The arm64 CMake configure and build tasks executed successfully.

```bash
~/Library/Android/sdk/build-tools/36.0.0/aapt dump badging \
  app/build/outputs/apk/debug/app-debug.apk
```

Exit 0: package `com.hammertomahawk.hashtableinspector`, compile SDK 36,
minimum API 26, target API 36, and `native-code: 'arm64-v8a'`.

```bash
unzip -Z1 app/build/outputs/apk/debug/app-debug.apk | rg '^lib/'
```

Exit 0, with exactly these native payloads:

```text
lib/arm64-v8a/libandroidx.graphics.path.so
lib/arm64-v8a/libhash_table_inspector.so
```

Therefore the APK contains arm64-v8a native code and no x86 or x86_64 native
libraries. Native symbol and Kotlin bytecode audits also exited 0 and matched
all nine JNI methods. `git diff --check` exited 0.

Debug APK: `app/build/outputs/apk/debug/app-debug.apk` (11,450,420 bytes; SHA-256
`1ab653a0047cd81fb710787fc34b8f2b6ab838385ca36b70976f62308ae0869f`).

## Current Git state

`main` tracks `origin/main` at `84bb113`; nothing is staged, committed, or pushed.
Tracked modifications: `README.md`, `hash_inspector.h`, `hash_json.c`, and
`native/tests/test_main.c`—4 files, 95 insertions, and 25 deletions. There are
16 untracked files: the Android/Gradle/JNI scaffold, `.gitignore`, and this report.

## Known limitations

- One local `~/Library/Android/sdk/platform-tools/adb devices -l` invocation tried to start ADB; it failed with `ADB server didn't ACK` and `failed to start daemon`.
- The user declined a retry because the Mac and S25 Ultra were not on the same network. No pairing, connection, authorization, APK installation, launch, or device validation occurred; further ADB diagnosis and device work remain deferred.
- The submission is arm64-v8a-only: no emulator, x86/x86_64, API 37, or CI.
- No Android instrumentation/UI tests or C-to-Kotlin golden fixtures exist.
- Demo keys remain restricted to 1-32 safe ASCII bytes; the C table itself
  supports arbitrary byte keys.
- Only size-counter corruption and clean restoration are implemented. Rewind
  is view-only and history is limited to 64 full frames.
- Inspection/corruption are enabled only in the demo shared library; production
  packaging and symbol-exclusion audits remain deferred.

## Next physical-device validation steps

Only after the Mac and phone are explicitly ready on the same network:

1. Enable wireless debugging on the S25 Ultra and pair/connect using the
   existing Platform-Tools 37.0.0; do not update Platform-Tools.
2. Confirm authorization with `adb devices -l` and confirm `arm64-v8a` with
   `adb shell getprop ro.product.cpu.abi`.
3. Install with `adb install -r app/build/outputs/apk/debug/app-debug.apk` and
   launch the activity.
4. Run the deterministic demo and verify collision, 4-to-8 rehash frames,
   validation failure after corruption, clean restore, and historical/live UI.
5. Record device model/API/ABI, screenshots, runtime outcome, and any logcat
   failures without broadening the pinned submission scope.
