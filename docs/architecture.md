# Architecture — initial decisions

Status: architectural contract, **not implemented multiplayer functionality**. Only the M01 build and diagnostic tooling exist. Required product scope is in `../GOAL.md`; all milestones remain in `../MILESTONES.md`.

## Original engine owns gameplay

The bridge runs inside an original SPORE process. A player process retains native UI, camera, animation, audio and editors; it submits intentions. An authoritative worker process invokes verified native actions and observes their outcomes. The C++ coordinator handles authentication, stable IDs, authority generations, routing, supervision and transactional metadata. It contains no replacement combat, AI, pathfinding, production, evolution or generation model.

```mermaid
flowchart LR
  P[Original SPORE player process + bridge] -->|authenticated intentions| C[C++ coordinator]
  C -->|bounded private IPC + fenced ownership| W[Original SPORE worker + bridge]
  W -->|native outcomes and checkpoint artifacts| C
  C -->|authoritative baseline and events| P
  C <--> D[Transactional metadata store]
  C <--> A[Immutable native artifact store]
  S[Worker supervisor] --> W
```

This is a proposed execution flow. No network session or worker implementation is claimed by the diagram.

## Boundaries

- Only `src/bridge` includes ModAPI headers or depends on the game ABI. The current bridge is Win32, C++17, MSVC v143, /MD in Release and /MDd in Debug. Future coordinator language/toolchain choices must not alter that ABI.
- One coordinator with a local transactional store is the initial deployment. A rendered unattended original-game worker is acceptable; headless flags and render suppression are unproven.
- Native context partition size is unresolved. Do not assume one planet, one player or one stage per process. Co-located players share authoritative simulation; workers in other locations remain part of the same canonical universe.
- Own player/species/faction identities outlive connections and native pointers. Explicit shared-faction policy is separate from engine mechanics.
- Network threads decode bounded messages into queues. Verified engine-thread boundaries resolve stable IDs to current objects; queued work cannot retain unsafe raw engine pointers.
- Replica application and original-function detours need reentrancy guards and explicit execution roles. No prediction of irreversible rewards or unverified replay.
- Native simulation time and snapshot transmission cadence are separate. Preserve timer meanings through transitions and restores; no new timestep semantics without behavioral comparison.

## M01 lifecycle decision

The bridge follows the pinned SDK's `AddPostInitFunction` / `AddDisposeFunction` registration pattern. DllMain only registers these callbacks; the bridge installs no detours or gameplay hooks. Hashing and log I/O occur in the initialization callback. Disposal flushes and closes diagnostics through the SDK callback, never during DLL detach.

The upstream core hooks during DLL load, so the implemented native host validates compiled game/content/payload hashes before process creation and injection. Normal Play uses the player's account and saves. Optional separate-account tooling retains the earlier developer checks. Three original-game bridge lifecycles and final user acceptance complete M01.

## Persistence and world ownership

The coordinator fences one authority per mutable domain and stores immutable artifact manifests. Native files remain engine data. A native side effect and a database transaction are not inherently atomic; M09 defines accepted/applied/durable acknowledgments and tests kills across every boundary.

Native caches on workers do not grant mutation authority. Galaxy AI, economy, disasters and diplomacy require auditing even when off-screen. Restore/transfer may not double-run these global systems. Never combine live save directories, merge arbitrary galaxy databases or accept client saves as authoritative.

H1–H6 investigation gates and their expected observations are in `research-log.md`. M03 multi-actor failure blocks dependent gameplay; it does not authorize a custom gameplay simulator. M20 adventures remain optional.
