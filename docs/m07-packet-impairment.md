# Bounded M07 packet impairment

`tools/native/m07-packet-impairment.ps1` runs real IPv4 localhost TCP packet
loss and delay through the signed WinDivert 2.2 driver. It is an optional
developer test tool. It does not launch SPORE or change player configuration,
firewall rules, routing, security settings or the normal launcher flow.
The older `m07-delay-proxy.py` remains a read-chunk delay relay and cannot
qualify packet loss. M07 native acceptance still requires a complete original
encounter through the packet filter with inspected native evidence.

## Dependency and lifecycle

The package is the upstream [WinDivert 2.2.2-A release](https://github.com/basil00/WinDivert/releases/tag/v2.2.2),
linked by the [official project page](https://reqrypt.org/windivert.html).
Its archive SHA-256 is
`63cb41763bb4b20f600b6de04e991a9c2be73279e317d4d82f237b150c5f3f15`.
The source tag resolves to `1789526ecfb9ff5397c94f9f54c1a3dc2fb60440`.
These are observed and pinned release identities, not an upstream published
checksum claim. The wrapper verifies the pinned archive before extraction,
the exact DLL/driver/control-tool hashes, and Windows Authenticode status
`Valid` with the reviewed driver signer thumbprint
`043589F75FCE2795E7F2CC3E526D46784D5DDAB3`.
The package and its license remain in ignored `local/m07-impairment/`.
No dependency binary is committed or bundled into a player release.

PowerShell 7, 64-bit Python and an elevated Windows process are required.
The [official API documentation](https://reqrypt.org/windivert-doc.html)
describes on-demand driver installation by `WinDivertOpen` and treats loopback
packets as outbound. The helper therefore filters outbound loopback packets
with **both** IPv4 addresses equal to `127.0.0.1` and exactly one TCP endpoint
equal to the requested dedicated port. There is no arbitrary filter option.
Network-layer filtering cannot identify an owning process; use a dedicated
port and start its intended server only after `ready.json` appears.

The wrapper refuses any preexisting WinDivert service and any existing TCP
socket using the requested port, including TIME_WAIT. A named mutex prevents
two instances of this wrapper from overlapping. A separate process deadline
is the configured 1–600 seconds plus 12 seconds for cleanup; expiry kills only
that owned process tree. On normal stop the helper disables new receives,
drains queued captures, reinjects pending packets and closes its handle.
The wrapper then checks the service's exact binary path and uses the pinned
control tool's read-only `list` mode to require zero remaining handles before
stopping/deleting its owned service. It verifies the driver service is absent.

The wrapper intentionally does **not** call `windivertctl uninstall`: the
[pinned upstream source](https://github.com/basil00/WinDivert/blob/1789526ecfb9ff5397c94f9f54c1a3dc2fb60440/examples/windivertctl/windivertctl.c)
shows that mode terminates other handle-owning processes. Unknown service
identity or remaining handles instead produce an explicit cleanup failure.
If a system security policy refuses the signed driver, retain that failure;
do not disable protections or enable test signing.

## Commands and evidence

Preparation without driver loading:

```powershell
pwsh -NoProfile -File tools/native/m07-packet-impairment.ps1 -Port 27160 -DelayMs 25 -JitterMs 5 -LossPercent 1 -Seed 731 -Seconds 60 -Output local/m07-impairment/prepare-new -PrepareOnly
```

Bounded HOST check, including a real coordinator and Schannel client probe:

```powershell
pwsh -NoProfile -File tools/native/m07-packet-impairment.ps1 -Port 27161 -DelayMs 25 -JitterMs 5 -LossPercent 1 -Seed 731 -Seconds 60 -Output local/m07-impairment/host-new -HostSelfTest -Python "$env:LOCALAPPDATA/Python/pythoncore-3.14-64/python.exe"
```

For an authorized native test, omit `-HostSelfTest`, use the dedicated
coordinator listen port and a fresh private output. Start the wrapper in a
hidden owned background process; wait for its `ready.json`, then start the
coordinator/clients through that port. An empty `stop.request` in the output
directory ends impairment early. Stop or disconnect the test clients and
coordinator first when practical. Await the wrapper's exit and inspect both
`report.json` and `setup.json`; a ready file alone proves neither a completed
test nor driver cleanup. Native startup/profile/capture requirements remain
unchanged and are owned by the native harness.

The helper randomly drops captured IP packets with the configured seeded
Bernoulli probability and schedules others with independent uniform jitter
around the base delay. TCP itself performs retransmission; TLS bytes are not
decrypted or replaced. Jitter can reorder packets. Actual scheduler latency
may exceed the requested interval, and no RTT is inferred. Outbound checksum
offload is handled by recalculating IP/TCP checksums before reinjection;
application payload bytes are unchanged. HOST self-test mode additionally
drops the first data packet in **each direction of each connection**, forcing
retransmission in both the byte-echo and TLS tests. This extra deterministic
loss must not be presented as a sampled 1% loss rate.

Limits are 1,000,000 captured packets, 1,024 pending user packets, 8 MiB pending
user bytes, and 65,535 bytes per IPv4 packet. Kernel queue bounds are 16,384
packets/32 MiB/16 seconds. User queue overflow bypasses delay, marks the run
failed and stops; it is never silently counted as configured loss. WinDivert
has no exposed kernel-overflow counter, so a balanced **captured** packet
ledger does not prove zero uncaptured kernel drops. Retransmission evidence
counts an identical TCP sequence/data-length range seen after an intentional
drop; segmentation changes can make this conservative. Packet metadata and
private coordinator credentials stay under ignored `local/`; packet contents
and payload hashes are never logged.

The capture budget was increased before the native impaired repeat. Native13
and native15 coordinator logs contain approximately 556 and 550 incoming
application messages per second respectively. Those messages are not IP
packet observations, and the coordinator does not log routine outgoing scene
fanout. The transport disables Nagle and batches at most 32 messages of 400
bytes per TLS send; actual TCP segmentation, acknowledgments and retransmissions
still determine the packet count. The former 100,000-packet budget was therefore
not a qualified capacity bound for the full encounter/reconnect window.

`ready.json` and `report.json` record the fixed capture and pending-queue limits.
Every packet decision and measured reinjection delay is streamed to JSONL,
bounded to 1,001,024 metadata records including the existing shutdown allowance.
The helper retains only delay extrema in memory; it also retains the bounded
set of intentionally dropped TCP ranges for conservative retransmission checks.
No packet sampling, payload logging or delay/loss-policy reduction accompanies
the larger capture budget. Exhausting the budget still fails the run and drains
captured pending packets. The 1–600-second wrapper limit is unchanged; the
native run must declare its own shorter action and overall deadlines.

After this capacity change, the focused Python 3.14 HOST/FIXTURE suite passed
9/9 tests. The added case exhausts a reduced test budget, requires an explicit
failure, and verifies complete pending-packet reinjection and driver closure;
the existing timing case also checks that the streamed delays and retained
minimum/maximum agree exactly. No driver is loaded by these tests.

## Executed HOST evidence, 2026-09-21

The final `host02` run used the exact command above with output
`local/m07-impairment/host02`. Python 3.14 and wrapper both exited **0**.
The retained report records:

- **44 captured packets; 4 actual drops; 40 reinjections**. Each direction had
  22 captures, 2 drops and 20 reinjections; all four drops were forced HOST
  first-data drops. Four dropped data ranges later appeared again.
- The TCP half-close echo returned **131,072 bytes exactly**, in 0.8691322
  seconds. The separate real coordinator/client probe authenticated with
  **Schannel TLS 1.2** despite the two forced TLS data drops. Both processes
  exited **0**, and `native_baseline_applied:false` remains explicit.
- Requested base delay **25 ms**, uniform jitter **±5 ms**, sampled loss
  probability **1%**, seed **731**. Recorded receive-to-send delays were
  **28.8334–31.5728 ms**. Two shutdown flushes are flagged separately.
- Zero engine errors or overflow bypasses, zero pending packets, receiver
  closed, captured packet accounting balanced. Driver stop returned **0**;
  subsequent service deletion returned **1060**, because driver shutdown had
  already removed its service. A fresh service query found no WinDivert
  service. No game, UI, firewall or routing action occurred.

Private original report SHA-256:
`1787df1ef572a35988a2717afc56e79ef9dc1d4a87464ae11387a4d77e06cfe3`.
Private original setup/cleanup SHA-256:
`cf0b6b12815bd6b96f497cd8988e3c2c5a17206a0ff457069bfd58c9c7b638b2`.
Packet metadata SHA-256:
`65f637ad0c7985e7050db6a7938435bdda1fb012887dc07da97e8d078aa103f2`.
These hashes identify the private originals, not sanitized exports.

`test_m07_packet_impairment.py` passes **8 HOST/FIXTURE tests** under the exact
Python 3.14 interpreter in 0.064 seconds. Tests never load the driver. They
exercise scope/parser refusal, bounded parameters, deterministic loss and
same-range retransmission detection, queue limits, preserved packet bytes,
minimum delay, shutdown drain, explicit overflow failure and driver closure.
The signed-driver HOST tests are intentionally separate from normal CTest.
Native encounter completion under this filter is **NOT RUN by this helper
qualification**; it remains part of [M07 acceptance](../tests/engine/M07.md).

The subsequent native27/.45 encounter completes combat, contested pickup,
result-preserving reconnect, late-resource/wrong-owner rejection and legitimate
continuation under the actual25 ±5ms,1% sampled-loss profile. Native28 completes
the selected original reset and movement/jump under the same still-live filter.
Both sequences retain actual packet decisions, closed process/filter accounting
and protected-file checks in the [M07 acceptance evidence](../evidence/2026-09-21-m07-completion/acceptance.md).
The earlier HOST helper qualification above remains separate. Captured-packet
accounting does not establish the unavailable kernel-overflow counter or an RTT
measurement.
