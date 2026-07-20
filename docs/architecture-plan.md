# Hash Table Inspector architecture plan

Last updated: 2026-07-20

Status: the long-term architecture is approved. The Build Week submission cut
is authoritative for implementation. The host-native slice is complete; the
Android/JNI viewer has not started.

## 1. Goal and boundaries

Hash Table Inspector operates on one real C11 byte-key-to-`int64_t` hash table
and renders copied observations of that same instance. It is not a Kotlin
reimplementation or an animation model.

The submission must demonstrate:

- insert/update, lookup hit/miss, and delete hit/miss;
- buckets, collision chains, entry identity, load, threshold, and resize;
- stable pre- and post-rehash frames;
- invariant validation;
- an explicit size-counter corruption and clean restoration; and
- a rewindable timeline containing at most 64 full immutable frames.

The initial adapter is hash-table-specific. A DSL, generic object reflection,
arbitrary memory editing, persistence, export, multiple structures, and other
frontends are outside the submission.

## 2. Approved submission decisions

| Area | Submission decision | Deferred direction |
| --- | --- | --- |
| Native structure | Author the real C11 table in this repository | Adapters for external structures |
| Android target | Physical Galaxy S25 Ultra over wireless ADB, `arm64-v8a` only | Emulator, `x86_64`, CI matrix |
| Android SDK | Android 16 SDK Platform 36.1 and Build-Tools 36 | API 37 is not required |
| Native build | Bash invokes host Clang; CMake is Android NDK glue only | Broader build abstraction |
| Boundary | Copied versioned JSON operation results and snapshots | Compact binary wire format/catalog |
| Demo keys | 1-32 ASCII bytes from `[A-Za-z0-9._-]` | General UI encoding for arbitrary bytes |
| History | FIFO of at most 64 full immutable frames | Byte budgets and operation-group eviction |
| Corruption | Size counter plus one, on an isolated clone | Wrong-bucket and cached-hash modes |
| Validation | Required deterministic native cases under strict warnings and sanitizers | Fuzzing, randomized oracle, allocation-failure matrix, cross-language goldens, symbol audit |

No unresolved design decision blocks the current work. Publication details
such as application ID, license, signing identity, and final remote can use
ordinary local defaults until release and only require human input when the
project is prepared for publication.

## 3. System architecture

```text
Jetpack Compose screen                         Android phase
        |
immutable Kotlin UI state + serialized commands
        |
NativeHashSession (Closeable, one owner)
        |
small C-only JNI bridge: opaque handle + copied UTF-8 JSON
        |
hti_session
  |-- real ht_t instance
  |     `-- stable compile-time trace hooks
  |-- hash-table snapshot/validation adapter
  `-- generic fixed-capacity inspection timeline
```

The native table is the source of truth. Kotlin will decode copies into
immutable display models. Selecting an older frame changes only what the UI
shows; it never rolls the live table backward.

## 4. Repository layout

Current host-native files:

```text
native/
  hash_table/
    include/hti/hash_table.h        public opaque table API
    src/hash_table.c                real table implementation
    src/hash_table_internal.h       trace and gated inspection internals
  inspection/
    include/hti/inspection.h        generic event/timeline API
    src/inspection_timeline.c       owned 64-frame ring
  adapters/hash_table/
    include/hti/hash_inspector.h    session, snapshot, result API
    src/hash_inspector.c            operations, trace capture, quarantine
    src/hash_snapshot.c             copy and independent validation
    src/hash_json.c                 JSON contract v1 encoder
    src/hash_inspector_internal.h
  tests/test_main.c                 deterministic host-native suite
scripts/test-native.sh              direct Bash + Clang entry point
docs/architecture-plan.md           canonical plan and decision record
README.md                           project status and submission record
```

Files to add only during the Android phase:

```text
app/                                Kotlin, ViewModel, Compose UI
native/jni/                         C-only JNI bridge
app/src/main/cpp/CMakeLists.txt      minimal Android NDK integration
Gradle wrapper and Android build files
```

CMake will declare only the C language, set C11, build one Android shared
library, and list the existing sources. It will not replace
`scripts/test-native.sh` for host builds.

## 5. Native table

The public surface in `native/hash_table/include/hti/hash_table.h` exposes an
opaque `ht_t` and explicit status codes:

- `ht_create` / `ht_destroy`;
- `ht_put`, returning inserted versus updated;
- `ht_get`, returning hit versus `HT_NOT_FOUND`;
- `ht_remove`, optionally returning the prior value;
- `ht_reserve`; and
- read-only size, capacity, and threshold accessors.

The implementation uses separate chaining, power-of-two capacities, owned
arbitrary byte keys, inline signed 64-bit values, stable monotonically assigned
entry IDs, and cached 64-bit hashes. Hashing is deterministic seeded FNV-1a
followed by an avalanche mix. Capacity has a minimum of 4, the threshold is
integer 3/4, and growth doubles capacity before an insertion would exceed that
threshold. It does not shrink automatically.

Checked size arithmetic and injectable allocation callbacks are part of the
core. Synchronization is external: the Android wrapper will serialize all
calls to one session.

## 6. Inspection and event API

`native/inspection/include/hti/inspection.h` is the small reusable spine. An
`inspect_event_t` contains:

- monotonic sequence and operation IDs;
- domain, event code, status, and flags;
- a stable subject ID; and
- four fixed 64-bit event payload values.

The generic layer does not know about buckets, keys, or hash tables. Each frame
owns an opaque snapshot and its release callback. Append transfers ownership to
the timeline. Retrieval returns borrowed views that remain valid until the
frame is evicted or the timeline is destroyed.

The timeline is a 64-slot FIFO ring. Appending frame 65 releases frame 1 and
its snapshot. The submission intentionally evicts individual oldest frames;
byte accounting, degraded-history behavior, and whole-operation eviction are
deferred.

The table's internal trace sink is compiled behind `HT_ENABLE_INSPECTION`. It
is invoked synchronously only at stable observation points and cannot mutate
the table or control the operation. It reports:

- inserted/updated;
- lookup hit/miss;
- delete hit/miss; and
- rehash begin/end/failure.

The hash adapter maps those traces into generic events and immediately captures
the real table. One public session operation receives one operation ID and may
produce multiple frames.

## 7. Snapshot model and invariants

Each `hti_snapshot_t` is one immutable arena allocation containing metadata,
bucket records, entry records, and copied key bytes. It contains no live table
pointers. A historical frame therefore remains unchanged after later updates,
deletes, or rehashes.

Snapshot metadata includes revision, reported size, independently counted
entries, capacity, resize threshold, validity, violation count, and first
violation. Bucket views include index, first entry index, and chain length.
Entry views include stable ID, hash, value, bucket and chain positions, and
borrowed key bytes owned by the snapshot.

Capture rejects invalid base structure such as a null bucket array, capacity
below 4, non-power-of-two capacity, or a detected chain cycle. The independent
validator then checks:

- reported size equals the traversed entry count;
- resize threshold matches 3/4 capacity;
- reported load has not exceeded the threshold;
- each cached hash recomputes from the copied key and seed;
- each entry is in its hash-selected bucket;
- entry IDs are nonzero, unique, and below `next_entry_id`; and
- keys are unique.

Load factor is derived at the display boundary as
`entryCount.toDouble() / capacity`; the integer operands remain in the copied
snapshot.

## 8. Ownership and lifetime

```text
hti_session
  |-- owns active ht_t
  |-- conditionally owns retained clean ht_t during quarantine
  `-- owns inspect_timeline
         `-- owns up to 64 hti_snapshot arenas
```

- `ht_t` owns its bucket array, entries, and copied key bytes.
- The session owns the active table and timeline.
- Snapshot capture allocates a complete independent arena.
- A successful timeline append consumes the snapshot; a failed append leaves
  it with the caller for release.
- Frame and snapshot accessor pointers are borrowed and must not outlive their
  owning timeline frame.
- `hti_operation_result_json` allocates a NUL-terminated buffer; its caller
  owns it and must call `hti_json_free`.
- Session destruction releases the active table, any retained clean backup,
  every timeline snapshot, and the timeline itself.

The future JNI method will create a Java `byte[]`, copy the encoded bytes with
`SetByteArrayRegion`, and free the native JSON buffer before returning. JNI
will never expose native structs, snapshot addresses, or pointers into Java
arrays.

## 9. Corruption isolation

Size corruption never modifies the retained clean table:

1. Deep-clone the active table, preserving capacity, chains, hashes, IDs,
   revision, seed, and allocator.
2. Retain the original as `clean_backup`.
3. Make the clone active and increment only its reported size.
4. Mark the session quarantined and capture the failing snapshot.
5. Allow validation, timeline reads, restore, and destruction; reject put,
   lookup, delete, and another corruption.
6. On restore, destroy the corrupted clone, make the untouched backup active,
   leave quarantine, and capture the restored valid state.

This demonstrates a real invariant failure without cycles, invalid pointers,
out-of-bounds writes, or contaminating the recoverable state.

## 10. Rehash-event handling

Growth from 4 to 8 is intentionally visible as three stable frames belonging
to the triggering insert operation:

1. `rehash_begin`: the old 4-bucket table, before relinking;
2. `rehash_end`: the complete 8-bucket table after every existing entry moved;
3. `put_inserted`: the 8-bucket table after the new entry is linked.

The new bucket array is allocated before `rehash_begin`. If allocation fails,
the table remains unchanged and emits `rehash_failed`; no partially relinked
state is observable. Cached hashes and stable entry IDs allow the viewer to
show the same entries moving between buckets.

## 11. Versioned C-to-Kotlin boundary

The implemented host contract is `HTI_JSON_CONTRACT_VERSION == 1`.
`hti_operation_result_json` copies the frames produced by one operation into a
UTF-8 JSON document shaped as:

```json
{
  "version": 1,
  "operationId": 4,
  "status": "ok",
  "tableStatus": "ok",
  "value": 3,
  "frames": [
    {
      "frameId": 5,
      "sequence": 5,
      "operationId": 4,
      "event": "rehash_begin",
      "status": 0,
      "subjectId": 0,
      "data": [4, 8, 3, 3],
      "snapshot": {
        "revision": 3,
        "reportedSize": 3,
        "entryCount": 3,
        "capacity": 4,
        "resizeAt": 3,
        "valid": true,
        "violationCount": 0,
        "violation": "none",
        "buckets": []
      }
    }
  ]
}
```

Actual snapshots always include every bucket and its ordered entry objects.
Hashes are fixed-width hexadecimal strings so JSON number precision cannot
truncate 64-bit values. Signed values remain JSON integers. Keys are inserted
without a general escape layer, so the adapter deliberately accepts only 1-32
safe ASCII bytes from `[A-Za-z0-9._-]`; the underlying table still accepts
arbitrary byte keys.

Recommended JNI surface for the Android phase:

```text
nativeCreate(initialCapacity: Long, seed: Long): Long
nativePut(handle: Long, key: ByteArray, value: Long): ByteArray
nativeLookup(handle: Long, key: ByteArray): ByteArray
nativeDelete(handle: Long, key: ByteArray): ByteArray
nativeValidate(handle: Long): ByteArray
nativeCorruptSize(handle: Long): ByteArray
nativeRestore(handle: Long): ByteArray
nativeTimeline(handle: Long): ByteArray
nativeClose(handle: Long): Unit
```

For this cut, the handle is an opaque native session token owned by one Kotlin
`Closeable`; Kotlin serializes calls, rejects use after close, clears the token
before close returns, and converts each returned byte array to immutable DTOs.
A generation-tagged native handle registry is deferred. `nativeTimeline` uses
the same version and frame schema to seed or resynchronize the Kotlin view.
Kotlin keeps at most the same 64 copied frames.

Expected outcomes such as lookup miss and quarantine are represented in the
result. JNI exceptions are reserved for programmer errors, invalid handles,
copy failures, or incompatible schema. Android's built-in JSON facilities are
the default parser to avoid another serialization dependency.

## 12. Production gating

The core defaults `HT_ENABLE_INSPECTION` and `HT_ENABLE_CORRUPTION` to zero.
The demo native target opts into both. A production-oriented target builds only
the public hash-table core, leaving the adapter, timeline, JSON encoder, JNI
debug calls, and corruption function out of the link. The required symbol
audit is deferred, so current validation proves the compile-time structure but
does not yet certify the final production binary.

## 13. First end-to-end vertical slice

One Compose screen will provide key/value inputs and Insert, Lookup, Delete,
Validate, Corrupt Size, Restore, and Reset actions. It will show:

- size, capacity, load factor, and resize threshold;
- a bucket list with collision chains, stable IDs, keys, hashes, and values;
- operation outcome and lookup probe information;
- rehash start/end movement; and
- a selectable 64-frame history with a clear historical-versus-live marker.

The deterministic demo starts at capacity 4, inserts a known colliding pair,
exercises lookup hit/miss and update/delete, adds enough keys to trigger the
4-to-8 rehash, rewinds across its three frames, applies size corruption, shows
the validation failure, and restores the clean table.

Acceptance requires those steps to run on the physical Galaxy S25 Ultra over
wireless ADB with only `arm64-v8a` packaged and without a duplicate live table
in Kotlin.

## 14. Milestones and acceptance criteria

### M0 - host-native foundation (complete)

- Real C11 table, stable trace hooks, generic timeline, immutable snapshots,
  validator, size corruption/restore, and JSON v1 are implemented.
- Direct Bash + Clang tests compile under strict warnings as errors with ASan
  and UBSan.
- Required deterministic behaviors pass.

### M1 - minimal Android bootstrap

- Pin the installed SDK 36.1, Build-Tools 36, NDK `28.2.13676358`, JDK 17,
  and the smallest compatible Gradle/AGP/Compose set.
- Configure only `arm64-v8a`; do not add emulator or `x86_64` tasks.
- CMake compiles the existing C sources as C11 and creates one JNI library.
- A smoke activity loads the library on the physical device.

### M2 - copied JNI contract

- Implement the C-only JNI surface and explicit close semantics.
- Verify operation JSON is copied before the native buffer is freed.
- Reject invalid demo keys and post-close calls cleanly.
- Add focused Kotlin parser tests for contract version and bounds.

### M3 - inspection UI

- Implement serialized ViewModel commands and immutable state.
- Render metrics, buckets, collisions, events, validation, and selected frame.
- Keep live and historical state visually distinct.

### M4 - device demo hardening

- Execute the deterministic scenario on the Galaxy S25 Ultra over wireless
  ADB.
- Verify orientation/recreation behavior chosen for the demo, error messages,
  restore behavior, and the 64-frame cap.
- Record repeatable build, install, launch, and demo commands in Bash/README.

## 15. Native test strategy and current result

Run:

```bash
./scripts/test-native.sh
```

The script directly invokes Clang with C11, strict warnings as errors, ASan,
and UBSan. On Apple Clang it disables only ASan's unsupported leak detector;
address and undefined-behavior checks remain active.

The six required tests cover:

1. core ownership of arbitrary binary keys;
2. insert/update, lookup hit/miss, and delete hit/miss;
3. a deterministic collision and chain snapshot;
4. stable 4-to-8 pre/post rehash frames, entry identity, and JSON v1;
5. invariant detection, size corruption, quarantine, and clean restoration;
6. FIFO retention of exactly the newest 64 full snapshots.

Still deferred by explicit scope: exhaustive allocation-failure injection,
randomized oracle testing, fuzzing, production symbol audits, C/Kotlin golden
fixtures, Android `x86_64`, emulators, and CI.

## 16. Major risks and defaults

| Risk | Submission mitigation and recommended default |
| --- | --- |
| Android tool versions do not form a compatible set | Pin versions only after the in-progress install is visible; prefer the versions bundled/recommended by Quail 2 that support SDK 36.1 |
| Wireless ADB interrupts a demo | Pair and verify shortly before demo; keep a USB fallback operationally available without changing the target architecture |
| JNI lifetime misuse | One serialized `Closeable`, copied `ByteArray` results, zero handle on close; do not cache native views |
| JSON ambiguity or precision loss | Contract version 1, safe ASCII keys, hashes as hex strings, strict field/range checks in Kotlin |
| Snapshot cost | Demo-sized data and a hard 64-frame FIFO; do not add byte-budget machinery now |
| Reentrant/partial observations | Synchronous non-controlling callbacks at stable states only |
| Corruption crashes the demo | Corrupt only a deep clone's size count and quarantine it |
| Feature leakage into production | Flags default off and production target links core only; add a symbol audit after the submission |
| Scope expansion | Finish the one-screen deterministic path before styling or generalized infrastructure |

Recommended defaults for remaining implementation details are: `minSdk 26`,
portrait-friendly responsive layout without hard orientation lock, one
ViewModel-owned session, `Dispatchers.Default` single-lane serialization,
built-in Android JSON parsing, decimal `int64_t` input with checked parsing,
and a deterministic fixed seed for the demo. None requires human input unless
it conflicts with an existing application or publishing requirement discovered
during Android bootstrap.

