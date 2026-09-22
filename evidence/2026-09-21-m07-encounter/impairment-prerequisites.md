# M07 impairment prerequisite inspection and HOST helper checks

Date: 2026-09-21 local project session. Evidence class: **HOST/read-only environment inspection and real HOST TCP/TLS tests**. No original SPORE game was launched, no desktop input was sent, no packet driver/software was installed, and no network/qdisc/firewall rule was changed. WSL was stopped before inspection; the read-only Linux commands started the existing distribution. They did not configure its network or stop unrelated services.

This is a reviewed public summary under [the public evidence policy](../../docs/public-evidence.md). Personal profile paths and account names are generalized below; the unredacted draft is retained in ignored `local/m07-encounter/private-acceptance-docs/`. No raw account inventory, full network inventory, credential or capture is included here.

## Installed capability findings

PowerShell inspected `Get-Command` for `clumsy`, `WinDivert`, `tc`, `wsl`, `docker`; only Windows `wsl.exe` was found on PATH. A bounded `Get-CimInstance Win32_SystemDriver` name/path filter for WinDivert/clumsy/Npcap found no relevant packet-diversion driver (`Npfs` was an unrelated loose-name match). Program Files directory names and the standard HKLM/HKCU uninstall registry entries contained no clumsy/WinDivert/NetLimiter/NetBalancer/Npcap/Wireshark/Docker match. These were installed-path checks, not an exhaustive full-disk inventory.

`rg --files` over Downloads, the repository's ignored local directory and `C:\ProgramData\SporeMP`, filtered to impairment-related filenames, found `$USERPROFILE/Downloads/netem-port.sh` and `netem-clear.sh`. They were read only. The first targets Linux `lo` and starts by deleting its root qdisc; the second flushes the shared mangle table. Neither was run. Windows loopback game traffic is not thereby routed through WSL loopback, and those broad shared-rule operations are unsuitable for this bounded test.

Exact WSL identity/capability command (PowerShell invocation, exit **0**):

```powershell
wsl -d Ubuntu-24.04 --exec sh -lc 'id; command -v tc; command -v ip; command -v python3; command -v unshare; uname -r; if test -r /proc/config.gz; then zcat /proc/config.gz | rg "CONFIG_(NET_SCH_NETEM|NET_NS|VETH)="; fi; if test -r /proc/net/psched; then cat /proc/net/psched; fi; tc qdisc show dev lo'
```

Observed: ordinary WSL developer account, UID 1000; `/usr/sbin/tc`, `/usr/sbin/ip`, `/usr/bin/python3`, `/usr/bin/unshare`; kernel `5.15.167.4-microsoft-standard-WSL2`; `CONFIG_NET_NS=y`, `CONFIG_VETH=y`; loopback `qdisc noqueue`. Presence of `tc` alone is not netem support.

Exact follow-up command (PowerShell invocation, overall shell exit **0** because the final read-only `ip route` succeeds):

```powershell
wsl -d Ubuntu-24.04 --exec sh -lc 'zcat /proc/config.gz | grep -E "NET_SCH_NETEM|NET_SCHED|NET_CLS_U32|NET_SCH_PRIO"; ls -ld /lib/modules/$(uname -r); command -v modprobe; modprobe --dry-run sch_netem 2>&1; ip -brief address; ip route'
```

Relevant observed output:

```text
CONFIG_NET_SCHED=y
# CONFIG_NET_SCH_PRIO is not set
# CONFIG_NET_SCH_NETEM is not set
CONFIG_NET_CLS_U32=y
modprobe: FATAL: Module sch_netem not found in directory /lib/modules/5.15.167.4-microsoft-standard-WSL2
```

The failing `modprobe --dry-run` subcommand's separate exit value was not captured; its explicit failure output is retained above. The module/kernel finding is decisive without a mutating qdisc trial. WSL uses an `eth0`/default-gateway route and has existing Docker bridges; the scripts were not applied to shared state.

**Outcome at this inspection:** actual packet-loss impairment was **NOT AVAILABLE through the inspected local tools**. A suitable remote packet-impairment environment or a reviewed installation/kernel change was required. The delay helper below does not close that gap. The later [signed WinDivert HOST qualification](../../docs/m07-packet-impairment.md) now provides a bounded, reversible real-packet helper; it does not retroactively change this read-only inspection or imply native M07 acceptance.

## Implemented delay helper and focused verification

`tools/native/m07-delay-proxy.py` is a loopback-only, opaque TCP byte relay with delay/jitter per read chunk in each direction. It has no loss option and reports `packet_loss: NOT_IMPLEMENTED`, `rtt_ms: NOT_INFERRED`, `tls_decryption:false`, `native_game_started:false` and `milestone_acceptance:NOT_VERIFIED`. It records source hash, monotonic timing metadata, byte counts, connection outcomes and the closed event-file SHA-256. It never records payloads, credentials or payload hashes. Fresh output is restricted to ignored repository `local/`, with reparse-point checks.

Bounds: at most eight active connections, 16 KiB per read iteration, drain-before-next-read backpressure, 3-second connect timeout, 5-second write-drain timeout, 2-second writer close timeout, 1–600 second CLI run, 100,000 events and 0–1,000 ms base delay with jitter no greater than the base. A bounded stop-request file avoids foreground control. These are helper bounds, not a native throughput or RTT claim.

Executed command:

```powershell
python -m unittest discover -s tests/unit -p test_m07_delay_proxy.py -v
```

Expected: exit **0**, focused HOST tests pass, no native game or network-rule mutation. Initial seven-test observed result: exit **0**, **7 tests pass in 1.235 seconds** under the default Python 3.11 interpreter. The Python 3.14 integration correction and final eight-test/full-suite results follow below. Tests cover:

- Invalid/nonfinite/out-of-range delay/jitter/seed/port/connection-limit values and a relay loop.
- Two simultaneous real loopback TCP clients each transferring over 70 KiB, with exact returned bytes/order and preserved client half-close.
- Actual requested/observed delay bounds and no plaintext payload/credential in event metadata.
- Connection-limit refusal and bounded shutdown closing owned connections.
- A blocked writer causes no read-ahead until drain completes; reads are capped at 16 KiB.
- Empty stop-request handling and a sealed nonsecret report with explicit native/loss nonacceptance.
- A separate real `SporeMP.Coordinator.exe` process and real `--client-probe` authenticate through the opaque delayed relay using **Schannel TLS 1.2**, the coordinator's certificate pin and a private temporary player config. The client reports `native_baseline_applied:false`; the coordinator exits **0** after `stop.request`. Private temporary credentials remain outside evidence and are removed with the test directory.

An initial four-test attempt failed its minimum-delay check because the Windows event-loop timer could wake before the high-resolution requested deadline. The helper was corrected to recheck `perf_counter_ns` before writing, rather than treating one `asyncio.sleep` return as proof of the configured delay. The next six-test run passed in **0.722 s**; the final seven-test run added the real Schannel path. This is a substantive timer-behavior correction, not relaxed acceptance rounding.

## Python 3.14 integration cleanup correction

The parent CTest run uses installed Python **3.14**, while the earlier standalone command resolved Python **3.11**. Its active-client shutdown test hung because newer `asyncio.Server.wait_closed()` waits for accepted client transports: the helper awaited that method before cancelling the connection tasks that owned those transports. An independent reproduction with the exact Python 3.14 interpreter and a 12-second `faulthandler` deadline reached the same active-client test and exited **1** on timeout. This was a runtime-dependent helper defect, not an original-game or preceding-fixture failure. The parent retained its failed CTest log in ignored storage.

The helper now closes the listening server, cancels and waits for its owned connection tasks with a six-second bound, and only then waits for server closure with a three-second bound. The HOST echo fixture uses the same ownership order. An intermediate full-suite rerun passed but exposed an upstream transport `ResourceWarning`: cancellation could interrupt cleanup after waiting for the first writer, before closing the other. Cleanup now closes **both** transports synchronously before awaiting either, with task/active-count bookkeeping in an unconditional `finally` block. A dedicated regression cancels a handler during its first writer's cleanup and asserts both transports are closed and the task is retired.

Final checks after those changes:

- Exact CTest Python **3.14**, full `test_*.py` discovery, with `ResourceWarning` always enabled, explicit post-run garbage collection and a 45-second `faulthandler` guard: **129 tests pass in 1.951 seconds**, exit **0**, no resource warnings observed.
- Default Python **3.11**, `python -m unittest discover -s tests/unit -p test_m07_delay_proxy.py -v`: **8 tests pass in 1.341 seconds**, exit **0**.
- `git diff --check`: exit **0**; existing CRLF normalization notices are not whitespace errors.

The full-discovery invocation, with the personal interpreter prefix generalized under the public-evidence policy, is:

```powershell
& "$env:LOCALAPPDATA/Python/pythoncore-3.14-64/python.exe" -W always::ResourceWarning -c 'import faulthandler,gc,unittest; faulthandler.dump_traceback_later(45,exit=True); result=unittest.TextTestRunner(verbosity=1).run(unittest.defaultTestLoader.discover("tests/unit",pattern="test_*.py")); gc.collect(); raise SystemExit(not result.wasSuccessful())'
```

Native game traffic through the helper: **NOT RUN**. Actual packet loss/retransmission: **NOT RUN**. Normalized encounter digest, contested native pickup, original terminal presentation and M07 milestone acceptance: separate required gates in [the M07 protocol](../../tests/engine/M07.md).
