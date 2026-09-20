# M06 Windows transport and session implementation

This is the engine-independent M06 transport/coordinator implementation. Its HOST tests execute real Windows TLS and TCP and separate coordinator/client probe processes. They do not run SPORE and do not establish native scene acceptance. The native integration and milestone decision are recorded separately in `evidence/2026-09-14-m06-network/SESSION.md`.

## Transport and authentication

`src/network` uses Winsock TCP and Windows Schannel TLS 1.2, linked against the Windows SDK libraries `ws2_32`, `secur32`, `crypt32`, `ncrypt` and `bcrypt`. There is no downloaded transport package, bespoke encryption or reliable UDP implementation. Encryption and sequencing are supplied by Schannel and TCP. TCP head-of-line blocking is a known limitation; adverse Internet latency/loss performance is not qualified by local tests.

Each coordinator run creates a fresh CNG RSA-2048 key and SHA-256 self-signed certificate. A player invitation pins the exact SHA-256 of that certificate, and the client checks its validity dates and an actual negotiated cipher strength of at least 128 bits before sending credentials. The certificate is private-session trust, not a new system trust root. Separate CNG-generated 256-bit credentials authenticate authority, player 1 and player 2. The coordinator identifies players from the credential; it never trusts a player ID or ownership value supplied by a client. Secrets are sent only after TLS certificate verification. Shutdown removes the generated CNG key. A forced process kill can leave an unused key container; no automatic broad key-store cleanup is performed.

All four handshake digests must match:

| Field | Meaning |
|---|---|
| `build_sha256` | Exact guarded bridge DLL payload bytes. |
| `executable_sha256` | Exact original-game executable bytes. |
| `content_sha256` | Raw `config/compatibility.candidate.json` bytes, with actual installed content checked by NativeHost. |
| `fixture_sha256` | The agreed closed native saved-world fixture bytes, separately checked by NativeHost. This is a bounded shared-fixture requirement, not general content transfer. |

The compatibility rejection uses the fixed error `incompatible_build_executable_or_content`; that same rejection also covers the fixture digest. Protocol/schema version errors are rejected by the binary decoder. A successful launcher probe proves authentication and compatible claimed/locally guarded identities; it explicitly reports `native_baseline_applied:false`.

Microsoft API references consulted during implementation: [Schannel credentials](https://learn.microsoft.com/en-us/windows/win32/secauthn/obtaining-schannel-credentials), [self-signed certificate creation](https://learn.microsoft.com/en-us/windows/win32/api/wincrypt/nf-wincrypt-certcreateselfsigncertificate), and [CNG provider information](https://learn.microsoft.com/en-us/windows/win32/api/wincrypt/ns-wincrypt-crypt_key_prov_info). The last reference matters: `CRYPT_KEY_PROV_INFO.dwKeySpec` is the legacy spec passed to `NCryptOpenKey`, so the generated CNG key uses **0**, not the `CERT_NCRYPT_KEY_SPEC` discriminator from `CERT_KEY_CONTEXT`. The first HOST TLS run exposed that distinction with `SEC_E_UNKNOWN_CREDENTIALS`; the corrected run establishes actual server/client handshakes.

## Explicit binary schema

`Packet` and `Entity` are scalar C++ value objects, never native memory layouts. Only `encode` and `decode` produce/consume the fixed **384-byte** wire frame. All integers are little-endian with explicit widths, and floats are IEEE-754 32-bit values encoded through integer bits. No pointers, native strings, SDK containers, game objects or compressed data cross the wire.

| Byte offset | Field |
|---|---|
| 0, 4, 8, 12 | Magic `SMP6`, exact frame length 384, protocol 1, schema 1; each uint32. |
| 16, 20, 24, 28 | Message kind, role, error, bounded entity count; each uint32. |
| 32–71 | Sequence, session epoch, scene epoch, baseline ID, player ID; five uint64 values. |
| 72–231 | Build, executable, content, fixture and credential; five 32-byte values. |
| 232–263 | Global entity ID, entity generation, owner and source tick; four uint64 values. |
| 264–287 | Native noun/herd/species-instance/species-type/species-group/archetype fingerprints; six uint32 values. |
| 288–331 | XYZ, quaternion XYZW, health, energy, hunger and DNA; eleven float32 values. |
| 332–339 | Target entity ID, uint64. |
| 340–347 | Action verb uint32 and direction int32. |
| 348–355 | Action/result request correlation ID, uint64. |
| 356–367 | Velocity XYZ, three float32 values. |
| 368–383 | Reserved zero bytes; nonzero data is rejected. |

Message kinds are `hello`, `welcome`, `reject`, `scene_begin`, `entity`, `scene_end`, `baseline_ack`, `motion`, `despawn`, `action`, `action_result`, and `ping`. Legal message families depend on the authenticated role. Positions/vitals must be finite and bounded; generations/IDs cannot be zero; owners are 0 (native world), 1 or 2. Network action verbs are declared separately from native execution support; the M06 bridge accepts only its implemented movement/jump/stop subset.

## Authority, lifecycle and reconnect fences

One authenticated authority owns the published scene. A baseline is staged until the declared number of unique entities arrives and `scene_end` agrees. A nonempty baseline requires exactly one controlled actor for each player; an empty baseline can represent scene exit. The coordinator caches canonical scalar entities and assigns a distinct fresh baseline ID to each client. Clients must acknowledge the exact scene/baseline after native application before actions are admitted.

Reliable entity creation/removal is separate from replaceable motion. Motion must refer to a currently live ID/generation, retain its owner and native fingerprint, and have a newer source tick. A despawn retains a tombstone; neither a late motion frame nor a creation with the old generation can resurrect that entity. A new generation may create the identity again. Old scene/baseline updates are rejected. A new baseline fences old inputs and clears the previous scene's tombstones. Actions already in flight during baseline staging receive a correlated `action_result` refusal instead of a permanent session rejection; obsolete baseline ACKs are ignored and cannot restore readiness. A fresh matching ACK is still required.

A valid client action is forwarded with its authenticated player owner and the authority's current baseline. A repeated/nonincreasing request ID within the connection is refused even when its transport sequence is fresh. The authority's `action_result` is routed only to the owning player. An accepted result describes validation/queuing; native execution and durable persistence require separate evidence. No M09 durability/retry guarantee is implied.

A disconnected player's credential retains its fixed player slot. Reconnection supplies a fresh authoritative cached baseline without creating another player. A new connection for an already connected credential supersedes the old connection, which is fenced and closed. Authority loss clears readiness and emits `authority_lost`. The current bridge pauses replica mapping/input while keeping the authenticated client connection open to await a fresh authority baseline. It acknowledges an empty baseline while remaining frozen/connecting. These are implemented source behaviors; their native execution must be established in the parent session. Session epochs, credentials and certificate pins are not persisted across coordinator restarts; restart recovery remains M09.

## Threads and resource limits

Each peer has a background socket/TLS thread. `Peer::send` and `Peer::poll` only access bounded scalar queues. `Peer::stop` joins the thread and belongs in controlled disposal, never DllMain or an engine update. `Peer::try_restart` first checks `WaitForSingleObject(thread_handle, 0)` for actual thread termination, so an engine retry cannot wait on an active network connection. It reports `network_thread_still_exiting` while teardown remains pending. Native reads, native action calls, object adoption and scene projection stay in the bridge's SDK app-update callbacks.

| Resource | Implemented bound |
|---|---|
| Active/pending TCP connections | 8, including unauthenticated connections. |
| Authenticated roles | 1 authority and 2 player identities. |
| Scene entities / scene tombstones | 2,048 each; overflow fails explicitly. |
| Per-channel incoming and outgoing packet queues | 8,192 packets each. |
| Coordinator observation queue | 8,192 events; overflow closes/fences the affected connection. |
| TLS encrypted buffer / plaintext assembly | 65,536 bytes each. |
| Send batch | Up to 32 fixed frames before Schannel record splitting. |
| Accepted input rate per connection | At most `2048*25+256` frames/second. |
| Player action rate | 128 requests/second, then refusal and closure. |
| TCP connection / TLS handshake | 5 seconds per phase. Hostname DNS uses the Windows resolver; bounded DNS cancellation is not implemented. |
| Socket send timeout / receive timeout | 1,000 / 100 ms. |
| Heartbeat / no-packet timeout | Client ping every 2 seconds; disconnect after 10 seconds without a decoded packet. |
| Config file | 4,096 bytes, exact fields, no duplicates/unknown names, strict hash/port values. |
| Coordinator diagnostic log | 10,000,000 events; exhaustion stops with exit 21 and a terminal record. |

These are admission and memory limits, not advertised capacity/performance claims. The full 2,048-entry baseline is exercised over actual TLS to both HOST clients. Steady 20 Hz transmission at that scene limit, native rendering cost and Internet latency/loss remain separate measurements. The current bridge drains at most 4,098 incoming packets per SDK app update. This replaces its initial 256-packet budget, which could not keep up with 2,048 entities at 20 Hz even at 60 fps. The larger cap is an implementation correction, not a native throughput claim.

## Executables and local configuration

The integrated build places `SporeMP.Coordinator.exe` at `build/win32/Release`; the standalone network build places it at `build/network-host/Release`.

Server config contains exactly seven newline-separated `key=value` fields:

```text
schema=1
host=127.0.0.1
port=27060
build_sha256=<64 hex characters>
executable_sha256=<64 hex characters>
content_sha256=<64 hex characters>
fixture_sha256=<64 hex characters>
```

Run `SporeMP.Coordinator.exe --serve --config SERVER_CONFIG --output NEW_DIRECTORY`. The output directory must not exist; the coordinator creates it with a protected ACL granting the current user, SYSTEM and Administrators access. It writes `authority.conf`, `player-1.conf`, `player-2.conf`, the two corresponding `.invite` URLs and nonsecret `coordinator.jsonl`. Files with secrets remain in ignored local storage and must not be copied into evidence or Git. An operator running disposable accounts must provision their precise config access separately; public/private credential ACL broadening is not automatic.

Peer configs contain the same four hashes and `schema`, `host`, `port`, plus `certificate_sha256`, `credential`, and `role=player|authority`. A player's `.invite` contains `sporemp://join?host=...&port=...&cert=...&token=...`; claimed local payload/fixture hashes are computed by the launcher/NativeHost rather than trusted from the invite.

Run `SporeMP.Coordinator.exe --client-probe --config PEER_CONFIG` for the real TLS handshake. Exit 0 prints `{ "authenticated": true, "player": 1, ... }`; failed probes print `{ "error": "..." }` and exit 20. No credential is printed or placed in command arguments. To stop a running server cleanly without foreground console input, create an empty regular `NEW_DIRECTORY/stop.request`. It is checked every 200 ms; exit 0 follows successful shutdown/log closure. The event stream is flushed at least every 100 ms even below the 128-event batch threshold, so low-volume diagnostics are observable while the process remains alive. This is stream flushing, not a power-loss durability guarantee.

## Reproduction and evidence

```powershell
cmake -S src/network -B build/network-host -G 'Visual Studio 17 2022' -A Win32 -T 'v143,version=14.44.35207' '-DCMAKE_SYSTEM_VERSION=10.0.26100.0'
cmake --build build/network-host --config Release --parallel 4
ctest --test-dir build/network-host -C Release --output-on-failure -V
pwsh -NoProfile -File tests/network/cli-session.ps1 -Coordinator build/network-host/Release/SporeMP.Coordinator.exe -Output local/m06-network-cli-FRESH
```

The test suite covers every truncated frame length, oversized/invalid headers, enum/reserved/NaN checks, identity mismatches, duplicate/replayed action handling, role/owner/scene/baseline/generation/tick fences, tombstones, queue/action-rate exhaustion, two actual pinned TLS clients and an authority, a full 2,048-entry baseline, reconnect and an oversized message sent inside real TLS. The CLI harness starts separate coordinator and two probe processes, then reconnects a third process, challenges a changed build hash, verifies private invite storage and observes clean coordinator exit.

Exact results, source hashes, environment and known failed attempts are in `evidence/2026-09-14-m06-network/host-transport/`. There is no native-game acceptance claim in these HOST artifacts.
