# M06 transport HOST validation — 2026-09-14

**HOST implementation and real TLS transport checks passed. No SPORE process was started by these commands.** Original-game acceptance remains in the [parent report](../SESSION.md) and [acceptance decision](../acceptance.md).

The complete session, literal commands, test output and source/build manifests remain in the [private historical archive](../../../docs/public-evidence.md#historical-artifacts) at `evidence/2026-09-14-m06-network/host-transport/`. No check was repeated for this condensation.

## Tested behavior

Windows Schannel TLS 1.2 over Winsock TCP uses a per-session CNG certificate and credentials. Clients verify the certificate pin before sending credentials. The coordinator owns authentication, fixed player identities, scalar framing, reliable lifecycle, baseline acknowledgement, sequenced motion and per-session reconnect.

Final tests covered malformed/truncated/oversized frames and invalid values, wrong identity/owner/scene/baseline, stale lifecycle/motion, immutable ownership, duplicate/rate-limited actions, bounded queues/connections, and actual TLS rejection for wrong pin/secret/build. Two real TCP clients received full **2,048-entity** fixture baselines and routed an authenticated action to authority. Oversized data sent over negotiated TLS closed the connection without admitting a session.

The separate-process CLI harness authenticated two probe processes, reconnected the same player without duplication, rejected a wrong build with expected exit 20 and closed the server/accepted probes with exit 0. It explicitly recorded `native_baseline_applied:false`: synthetic identity fixtures did not validate a game installation. Invitations/credentials remained private.

## Commands, corrections and results

| Command | Final observed result |
|---|---|
| Standalone `cmake -S src/network -B build/network-host` with pinned Win32/v143/Windows SDK arguments | Exit 0. |
| `cmake --build build/network-host --config Release --parallel 4` | Exit 0, zero warnings/errors. |
| `ctest --test-dir build/network-host -C Release --output-on-failure -V` | Initial final boundary suite: 2,531 assertions, 1/1 in 3.61 seconds. After rebase/flush correction: **2,540 assertions, 1/1 in 3.65 seconds**. |
| `pwsh -NoProfile -File tests/network/cli-session.ps1 -Coordinator build/network-host/Release/SporeMP.Coordinator.exe -Output local/m06-network-cli-02` | Exit 0; process/authentication/refusal/closure results above, plus 1,794 event bytes readable before shutdown. |

The assertion count includes individual decoder truncations and baseline enqueues, not thousands of gameplay tests. Initial failures were an unavailable MAXULONG declaration, a malformed-count fixture that accidentally remained valid, and server CNG credential acquisition caused by the wrong dwKeySpec. Correct declarations/fixture/key metadata resolved them; no expected protection was removed.

A pre-native audit found valid actions/ACKs arriving during authority rebase could receive generic rejection and permanently quarantine a client. Correlated action refusals and ignored obsolete ACKs now preserve fencing until a fresh matching ACK. A dedicated HOST test verified routing resumes only after that fresh ACK. Low-volume coordinator logs now flush at least every 100 ms; this does not prove durable persistence.

## Qualification boundary

Windows 11 build 26200, Ryzen 7 9700X; MSVC 19.44.35226.0, Windows SDK 10.0.26100.0. Network tests do not load ModAPI. Final corrected coordinator SHA-256: `bb570c42a8597e383e11e196f018b87ed2c3f103292981d69f97db9f4f3e2ec3`.

These results establish transport/protocol mechanics on this machine. They do not establish 20-Hz native simulation at 2,048 entities, Internet latency/loss quality, arbitrary content transfer, durable restart or campaign combat. Bridge receive-budget, authority-loss pause and empty-baseline changes still required the subsequent native M06 runs.
