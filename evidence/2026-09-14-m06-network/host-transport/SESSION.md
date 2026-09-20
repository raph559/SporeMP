# M06 transport HOST evidence — 2026-09-14

**Result: HOST implementation and transport tests pass. No SPORE process was started by these commands.** Native-game acceptance remains in the parent M06 session and is not inferred from this evidence.

The implemented transport uses real Windows Schannel TLS 1.2 over Winsock TCP. CNG creates separate per-session certificate/credentials; the client validates its pinned server certificate before sending its player or authority credential. The engine-independent coordinator validates protocol/identity/ownership and supplies reliable lifecycle, sequenced motion, baseline acknowledgement and stable per-session player reconnect.

## Commands and observed outcomes

All paths below are relative to `C:\Users\Developer\Documents\ChatGPT\SporeMP`, shell PowerShell. Exact environment and frozen file hashes are in `source-and-environment.json`.

| Command | Exit | Expected and observed |
|---|---|---|
| `cmake -S src/network -B build/network-host -G 'Visual Studio 17 2022' -A Win32 -T 'v143,version=14.44.35207' '-DCMAKE_SYSTEM_VERSION=10.0.26100.0'` | 0 | Standalone Windows HOST project configures with MSVC 19.44.35226.0 and Windows SDK 10.0.26100.0. |
| `cmake --build build/network-host --config Release --parallel 4` | 0 | Network static library, coordinator and test executable build with `/W4 /WX`, zero compiler warnings/errors. |
| `ctest --test-dir build/network-host -C Release --output-on-failure -V` | 0 | Final test reports **2,531 HOST assertions**, 1/1 target passed in **3.61 seconds**. Preserved complete CTest log: `network-host-2531.log`. |
| `pwsh -NoProfile -File tests/network/cli-session.ps1 -Coordinator build/network-host/Release/SporeMP.Coordinator.exe -Output local/m06-network-cli-01` | 0 | Actual separate coordinator and client-probe processes authenticate, reconnect, reject a wrong build and stop cleanly. Preserved report: `separate-processes-01.json`; nonsecret closed server trace: `separate-processes-coordinator-01.jsonl`. |
| The same standalone configure and build after setting the coordinator `RUNTIME_OUTPUT_DIRECTORY` to the CMake binary root | 0 / 0 | Confirms the executable remains at `build/network-host/Release/SporeMP.Coordinator.exe`; integrated builds now put it at `build/win32/Release`, matching launcher discovery. No transport source changed for this placement correction. |

The initial configure invocation passed the SDK argument without quotes. PowerShell split it, and CMake emitted `Ignoring extra path ... .0.26100.0` while still selecting the correct installed SDK. The quoted argument above removes that warning and is the reproducible command.

## Meaning of the tests

The final count includes 384 individual truncated-length decoder checks and 2,046 enqueue checks for the additional entities in the maximum-size baseline. It is not 2,531 separate gameplay tests. The exercised behaviors include:

- Explicit binary roundtrip, every truncation, oversize/magic/version/schema/enum/count/reserved checks, NaN and invalid owner/action rejection, native direction 3 acceptance, and bounded hex parsing.
- Pure session authentication, two fixed player identities, fresh per-client baselines, action admission only after ACK, claimed owner replacement, wrong actor/scene/baseline refusal, reconnect without an additional canonical player, tombstones, new generation creation, late motion refusal, immutable ownership/native fingerprint, empty scene exit, authority loss and bounded connection slots.
- Per-channel 8,192-frame outgoing queue exhaustion, duplicate action request rejection and the 128-request/second player action limit.
- Actual pinned TLS authority and two real TCP client connections, full **2,048-entity** baselines to both clients, an authenticated client action arriving at the authority, wrong-owner rejection, reconnect with the same player and a new baseline, wrong certificate pin, wrong secret and mismatched build rejection.
- An oversized binary frame sent through an actual successfully negotiated TLS connection; the server closes that connection without admitting a session.

The separate CLI run used a synthetic identity/fixture digest set intentionally. It executes the transport handshake rather than claiming a validated game installation. Coordinator PID **45500** reported ready on `127.0.0.1:54622`; probe PIDs **42136** and **30960** authenticated as players 1 and 2 in the same session and exited **0**. Reconnect PID **7920** reused player 1's config and retained player 1/session, exit **0**. Mismatched-build probe PID **17816** returned `incompatible_build_executable_or_content`, exit **20**. The server observed `stop.request`, closed all peers and exited **0**. Probe JSON explicitly records `native_baseline_applied:false`.

The exact command argument arrays and process results are in the CLI report. Reconnect repeats the recorded player-1 `--client-probe --config` command. The invitation files and credentials remain exclusively in ignored `local/m06-network-cli-01/private-session`; they are not copied into this evidence. Its protected directory ACL is recorded without exposing credentials. The original coordinator trace writer serializes nonsecret scalar metadata and never writes `Packet.credential`.

## Failed attempts and corrections

1. First standalone build: **exit 1**, `MAXULONG` was unavailable in the chosen headers. Replaced the size bound with the explicit `0xffffffffULL` limit before conversion to `ULONG`.
2. Initial parser test: **CTest exit 1**, assertion 395. The test changed the low byte of an otherwise valid count to 255, which is legal under the 2,048 limit. Corrected the fixture to mutate the count's high byte; no acceptance was weakened.
3. Initial real TLS server: **CTest exit 1**, assertion 446, `AcquireCredentialsHandle(server):0x8009030d` (`SEC_E_UNKNOWN_CREDENTIALS`). `CRYPT_KEY_PROV_INFO.dwKeySpec` incorrectly used the `CERT_NCRYPT_KEY_SPEC` key-type discriminator; Microsoft documents that this field is passed to `NCryptOpenKey` as its legacy key specification. Using **0** for the created CNG key fixes the actual server credential acquisition. Subsequent real TLS test passed 479 assertions in 0.62 seconds.
4. Action duplicate/rate/queue checks raised coverage to 485 assertions, passing in 0.65 seconds. Expanding the real two-client baseline to the full 2,048 limit produces the final 2,531 assertions in 3.61 seconds. That is a new boundary test, not repetition of native evidence.

## Qualification boundaries and next step

The tested machine is Windows 11 Pro **10.0.26200**, AMD Ryzen 7 9700X, 8 cores/16 logical processors. Network HOST tests do not link or load the pinned ModAPI dependency, but the repository SDK identity is recorded as `cbf9206b9a823f0911cd9be0217104a49d72380b`. The current content-manifest and each network source/test/binary hash are recorded in the source manifest. No game binary, save, third-party asset or credential is added here.

The tests establish the selected transport and protocol mechanics on this machine. They do not establish 20 Hz native simulation at the maximum 2,048-entity limit, Internet latency/loss quality, arbitrary content transfer, durable coordinator restart, campaign progression or M07 combat outcomes. Engine-side interpolation/application, actual player input and a full native reconnect still require the parent session's original-game evidence.

The next smallest integration step is rebuilding the bridge/NativeHost/coordinator together, provisioning the matched native fixture and current private configs, and obtaining two real client scene-baseline application acknowledgements from one original-game authority. Keep any failed native trials visible in the parent session.

## Targeted rebase-race and diagnostic-flush correction

The root requested a brief pre-native correctness audit. It found a concrete timing issue: the coordinator fences client readiness as soon as an authority begins a new baseline, but sends the replacement client baseline only after the source finishes. A legitimate client action or ACK already in flight during this interval received a generic `not_ready`/`stale` rejection. The bridge interprets generic rejection as permanent quarantine, so it could fail before receiving its valid fresh baseline.

`Session` now returns a correlated `action_result` refusal for authenticated actions whose scene/baseline is not ready or obsolete. It preserves the original request/scene/baseline/entity and supplies the authenticated player ID. An obsolete ACK is ignored without enabling actions; a fresh matching ACK is required. Malicious transport sequence replay, duplicated action request IDs, wrong ownership and superseded connections retain their existing rejection behavior. The new HOST test places an action and ACK inside an authority rebase, finishes the new baseline, verifies an old ACK cannot restore readiness, then verifies that a fresh ACK restores routing.

The root also observed an empty coordinator event file after an interrupted early native attempt. The coordinator previously flushed on every 128th event or clean shutdown, which left low-volume events in the C++ stream buffer. It now flushes at least every **100 ms** as well. This makes events readable during a live process; it does not promise native persistence or power-loss durability.

Exact additional commands:

```powershell
cmake --build build/network-host --config Release --parallel 4
ctest --test-dir build/network-host -C Release --output-on-failure -V
pwsh -NoProfile -File tests/network/cli-session.ps1 -Coordinator build/network-host/Release/SporeMP.Coordinator.exe -Output local/m06-network-cli-02
```

All three commands exited **0**. Build output has zero warnings/errors. CTest passes **2,540 HOST assertions in 3.65 seconds**, with the complete log in `network-host-2540-rebase.log`. The updated separate-process harness additionally reads **1,794 event-log bytes before issuing stop.request**, while the low-volume coordinator is still running. The report and closed nonsecret trace are `separate-processes-02.json` and `separate-processes-coordinator-02.jsonl`. Server PID 26852 and all accepted probes exit 0; the mismatched-build probe exits 20 as intended. New source/binary hashes are in `source-rebase-and-flush.json`; this coordinator SHA-256 is `bb570c42a8597e383e11e196f018b87ed2c3f103292981d69f97db9f4f3e2ec3`.

The root separately corrected the bridge's receive budget to 4,098 packets per app update, implemented paused waiting on `authority_lost`, and made an empty baseline ACK remain frozen/connecting. Those source-level bridge changes are described in the network document without claiming they have passed a native run. No bridge file was edited by this transport task.
