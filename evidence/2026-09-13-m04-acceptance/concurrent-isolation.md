# Bounded concurrent native save and profile isolation

The recorded two-account, controlled Creature configuration passes the exercised profile/save isolation checks. This report does not change a milestone status. Its scope is the pinned original executable and SDK, two prepared Windows accounts, original rendered windows on the current desktop, and the controlled native A/B fixture. Other content, stages, minimized windows, background desktops, and headless operation are outside this result.

`concurrent-isolation.json` joins the native save probe, exact-lifetime ETW, closed personal comparison, and independent checkpoint crosscheck. The closed ETL is `local/m04-acceptance/concurrent-save-01.etl`, 2,570,059,776 bytes, SHA-256 `2f13e08ba39c0f3c6358bab6a21c26de39f9b44a0d33df10d5398cc439b993cb`. All extraction, filtering, summarization, and analysis commands completed with exit 0. Extraction reported zero lost events.

| Worker | Generation | Game PID / host PID | Save request / epoch |
|---|---|---|---|
| 01 | `9f0d24d7007246a2975e6e2090b1ec8f` | 31528 / 37744 | 9 / 3 |
| 02 | `c3e4ad8624fe4fe6ba0ba92b0236441b` | 39716 / 39640 | 7 / 3 |

Each original native save returned true with its matching two-actor checkpoint snapshot. The conservative common-QPC intersection of the instrumented save brackets is 414.7126 ms. Entry/return brackets include boundary logging and scheduling overhead; the ETW adds actual original Games I/O evidence.

| Worker | First successful Games write request (UTC) | Last completion (UTC) | Engine-thread successful writes |
|---|---|---|---:|
| 01 | 2026-09-13 01:28:16.7266980 | 2026-09-13 01:28:17.2246816 | 488 |
| 02 | 2026-09-13 01:28:16.7186341 | 2026-09-13 01:28:17.2290633 | 488 |

These operations occur within the explicit concurrent save command window and use each trace's native engine thread: 16252 and 39204. Their I/O activity spans intersect for 497.9836 ms. More strongly, two successful `stars.db.tmp` Write operations have overlapping request-to-OperationEnd intervals for **20.1228 ms**:

- Worker 01: `01:28:17.2030066Z` to `01:28:17.2239517Z`, under its own `C:\Users\SporeMP-M04-01\AppData\Roaming\Spore\Games\Game0`.
- Worker 02: `01:28:17.2035566Z` to `01:28:17.2236794Z`, under its own `C:\Users\SporeMP-M04-02\AppData\Roaming\Spore\Games\Game0`.

Both completion statuses are zero. Exact FILETIME ticks, issuer thread IDs, native trace records, and paths remain in the JSON report. This is observed overlapping original save-file I/O, not merely overlapping client requests.

Kernel Process Start events from each expected host and matching `SporeApp.exe` Process End events define the selected lifetimes. This removes 764 and 8039 events outside those lifetimes, including earlier numeric PID reuse. The filtered traces retain 280137 and 279728 events. Their raw and filtered hashes and exact boundaries are in `concurrent-process-lifetimes.json`.

Both workers have successful mutations under their own Games/profile trees. There are no resolved peer-profile or personal-profile open paths, requested mutations, or completed mutations. `concurrent-path-alias-audit.json` independently checks the actual Windows short names as well: `SPOREM~2` is Worker 01 and `SPOREM~3` is Worker 02. The raw Worker 02 `SPOREM~3` path is its own profile. Shared driver/cache and run-log writes remain listed; this is not a claim that every global write stays beneath the user profile.

The closed personal comparison against `personal-before-concurrent.json` verifies all 29 files unchanged in `personal-after-concurrent-audit.json`. Its baseline is the fresh backup taken after the normal player closed, not the older baseline changed by that player's session. The pinned executable and all candidate content trees still match `config/compatibility.candidate.json`; no differences were observed in those compared roots. ETW likewise contains no resolved requested/completed mutations beneath the installation root for either selected game lifetime. Unresolved writes are not attributed to an SDK log or assumed harmless.

`concurrent-checkpoint-crosscheck.json` independently verifies both sealed backup manifests and file hashes, native closed-trace hashes, matching save request/epoch, the exact A/B fingerprint pairs, and the current closed Games trees. Both traces end healthy with detach status 0 and clean requested game/supervisor exits. The newly sealed concurrent artifacts have **not** been reloaded; the earlier native reload/adoption test is separate evidence. Neither a native true return nor this closed hash verification claims disk power-loss durability.

The unresolved evidence remains explicit:

| Counter | Worker 01 | Worker 02 |
|---|---:|---:|
| Unresolved file Write requests | 103 | 102 |
| Unresolved registry SetInformation | 3263 | 3132 |
| Undecoded events in exact lifetime | 1109 | 1093 |
| Property extraction errors | 0 | 0 |
| Pending mutation requests at trace end | 0 | 0 |
| Lost-event markers | 0 | 0 |

Raw nonzero I/O statuses are also retained in the JSON summaries. Empty resolved peer/personal results do not mean every ETW event was decoded. The closed file comparisons, worker identity/lifetime checks, original save traces, and previously recorded OS access-denial probes provide independent evidence alongside the trace.

Reproduction and provenance are in `analyze-concurrent-isolation.py`, `filter-concurrent-lifetimes.py`, `audit-concurrent-path-aliases.py`, their recorded `.command.json` / `.log` files, and the source/artifact hashes in the reports. The audit performed no source changes, restoration, native launch, control mutation, shutdown, or crash operation.
