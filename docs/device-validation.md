# Physical-device validation

Validated on 2026-07-20 against the submission APK and the physical Samsung
Galaxy S25 Ultra. Automated evidence below comes from build and targeted ADB
commands; follow-up health checks were read-only, and UI observations are
explicitly identified as human testing.

## Device and build evidence

| Item | Verified result |
| --- | --- |
| Device | Samsung Galaxy S25 Ultra, model `SM-S938U1` |
| ADB serial and transport | `R5CYA1FR2TD`, USB `usb:1-2`, authorized as `device` |
| Android / ABI | API 36, primary ABI `arm64-v8a` |
| Package | `com.hammertomahawk.hashtableinspector` |
| Launcher | `com.hammertomahawk.hashtableinspector/.MainActivity` |
| APK | `app/build/outputs/apk/debug/app-debug.apk` |

Representative identity queries returned the expected device:

```bash
~/Library/Android/sdk/platform-tools/adb devices -l
~/Library/Android/sdk/platform-tools/adb -s R5CYA1FR2TD shell getprop ro.product.model
~/Library/Android/sdk/platform-tools/adb -s R5CYA1FR2TD shell getprop ro.build.version.sdk
~/Library/Android/sdk/platform-tools/adb -s R5CYA1FR2TD shell getprop ro.product.cpu.abi
```

```text
R5CYA1FR2TD device usb:1-2 ... model:SM_S938U1 ...
SM-S938U1
36
arm64-v8a
```

The existing APK installed and its launcher was resolved through Package
Manager rather than assumed:

```bash
~/Library/Android/sdk/platform-tools/adb -s R5CYA1FR2TD install -r \
  app/build/outputs/apk/debug/app-debug.apk
~/Library/Android/sdk/platform-tools/adb -s R5CYA1FR2TD shell cmd package \
  resolve-activity --brief -a android.intent.action.MAIN \
  -c android.intent.category.LAUNCHER \
  com.hammertomahawk.hashtableinspector
```

Installation returned `Success`; Package Manager resolved `.MainActivity`.
The resolved component was then cold-launched:

```bash
~/Library/Android/sdk/platform-tools/adb -s R5CYA1FR2TD shell am force-stop \
  com.hammertomahawk.hashtableinspector
~/Library/Android/sdk/platform-tools/adb -s R5CYA1FR2TD shell am start -W -n \
  com.hammertomahawk.hashtableinspector/.MainActivity
```

`am start -W` returned `Status: ok`, `LaunchState: COLD`, and the resolved
activity. Subsequent `pidof`, Activity Manager, and Window Manager queries
showed the process alive and `.MainActivity` resumed and focused.

Launch logcat showed `lib/arm64-v8a/libhash_table_inspector.so` loading with
result `ok` and the first frame being committed. The crash buffer was empty,
and a targeted scan found no `FATAL EXCEPTION`, `AndroidRuntime`,
`UnsatisfiedLinkError`, JNI-detected error, native abort, or fatal signal.
Non-fatal Samsung/framework graphics diagnostics mentioned deprecated ashmem
pinning, an unavailable QSPM service, and optional `libpenguin.so`; the app
remained alive and these were not inspector linkage or crash failures.

The final checkpoint also passed:

```bash
./scripts/test-native.sh
./gradlew :app:assembleDebug
```

Results: all 6 native tests passed and the Android build reported
`BUILD SUCCESSFUL`.

An earlier local `adb devices -l` invocation had failed to start the ADB server
with `ADB server didn't ACK` and `failed to start daemon`; work was deferred at
that point. Later authorized USB ADB installation, launch, and validation on
the device succeeded.

## Manual UI observations

Initial physical testing found readable and interactive content passing under
the status-bar region and the gesture-navigation pill. The app retained its
edge-to-edge background and constrained the clipped scrolling viewport with
`WindowInsets.safeDrawing`, applying the system insets once. A human manually
verified top and bottom clearance and system-bar contrast in both light and
dark themes.

Human color-vision accessibility testing also found the small historical
`Live` chip too subdued to be reliably recognized as an action in either
theme. It was replaced with a full-width filled Material button labeled
exactly **Return to live**. Visible text, shape, fill/contrast, standard button
semantics, and an adequate touch target communicate the action without relying
on color alone. It was clearly identifiable in both themes.

## Deterministic demo evidence

- Frame 1 recorded session creation.
- Frames 2-3 formed the deterministic two-entry collision in bucket 3.
- The sequence exercised lookup hit and miss, update, delete, and additional
  inserts.
- `rehash_begin` captured capacity 4; `rehash_end` captured capacity 8 with
  three entries moved.
- Validation passed before the controlled size corruption was applied.
- Frame 15 failed validation with reported size 5 versus 4 counted entries,
  `size_mismatch`, and a quarantined live session.
- Historical frame 3 showed bucket 3 with chain length 2, entries `b=2` and
  `a=1`, and valid invariants. Historical selection was view-only: the
  quarantined live state did not change.
- Frame 16 recorded clean-state restoration.
- Frame 17 passed validation with an operational session, size/count `4/4`,
  capacity 8, and valid invariants.

Human testing confirmed **Return to live**, **Restore**, and **Validate** all
completed correctly.

## Lifecycle validation and decisions

Switching the system theme originally recreated the Activity and reset the
native inspector. Ownership therefore moved from Activity/Compose state to
`InspectorViewModel`. The ViewModel owns one `NativeHashSession` and copied,
immutable presentation state across configuration recreation; it retains no
Activity, Context, View, or Composition. `onCleared()` closes the current
native owner once, and explicit Reset closes the old session before creating
its replacement. The C table remains the only live source of truth.

A human dark/light configuration recreation preserved the same native
session, all 17 timeline frames, selected historical frame 3, and the timeline
scroll position.

The submission keeps the vertical newest-first timeline because it exposes the
entire operation sequence directly. A graph remains deferred until node/edge
meaning and accessible interaction are deliberately defined. Horizontal
timeline navigation is also deferred because it can hide history and reduce
discoverability. These physical-device findings directly drove the inset,
accessibility, and lifecycle changes.

## Limitations

- UI validation was manual; no taps, scrolling, theme changes, or rotation are
  claimed as automated tests.
- No screenshots or other validation media are stored in this repository.
- Validation covers this one physical API 36, `arm64-v8a` device; emulator,
  x86/x86_64, and broader device coverage remain deferred.
- Full process-death restoration is not implemented or claimed. A genuinely
  new process creates a new native session.
