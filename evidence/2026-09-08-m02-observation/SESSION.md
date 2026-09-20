# M02 observation increment — 2026-09-08

Objective: begin native-behavior observability after the accepted M01, preserving the full M00–M20 plan. Base commit: `d106404da0b7c4531f63f98d0df2377bb897a0f7`; initial worktree was clean. Milestone **IN_PROGRESS**; observation bridge **IMPLEMENTED_NOT_RUN** in the original game. No multiplayer gameplay is implemented.

## Implemented result

- Bridge 0.0.2: seven source-backed candidate hooks for native jump/landing, noun creation/destruction, avatar assignment and avatar/NPC AI entry counts. Trampolines preserve original inputs/results and are installed/removed in SDK lifecycle callbacks outside loader lock.
- App scene-message observation and Creature avatar scalar snapshots, bounded 8192-slot opaque entity identities, invalidation before native destruction, fresh IDs after address reuse/scene changes, and no post-action/destruction object reads.
- Fixed-size JSONL buffering and 64 MiB cap, engine/mod provenance, monotonic sequence and QPC timing, parent action correlations and explicit overflow/foreign-thread counts. No pointers/native containers are serialized.
- Read-only `sporemp_trace` console command plus developer-only `--observe` / `-ObservationMode Observe`. Normal Play and the reference mode clear inherited observation activation. No launcher presentation/files or personal game install were changed.
- Strict evidence analyzer; five-stage source inventory; binding/thread/ABI registry; paired original-game fixture protocol. Damage/death events, inventory/progression and save/load callbacks remain investigations, not claimed implementation.

## Build and executable results

Exact ordered commands, working directories, UTC intervals, expected and observed exits: [commands.json](commands.json). Reproduction script: [run-host-checks.ps1](run-host-checks.ps1). These commands never start a game; the new-mode guard command deliberately fails before creation.

| Command / evidence | Expected | Observed |
|---|---|---|
| CMake configure, pinned Win32 v143 toolset 14.44.35207 and Windows SDK 10.0.26100.0 | Exit 0 | Exit 0; `configure.txt` |
| `cmake --build build/win32 --config Release --parallel 4` | Exit 0 | Exit 0; `release-build.txt` |
| `ctest --test-dir build/win32 -C Release --output-on-failure -V` | Exit 0, four targets pass | Exit 0, 4/4; `release-tests.txt` |
| `cmake --build build/win32 --config Debug --parallel 4` | Exit 0 | Exit 0; `debug-build.txt` |
| `ctest --test-dir build/win32 -C Debug --output-on-failure -V` | Exit 0, four targets pass | Exit 0, 4/4; `debug-tests.txt` |
| Actual observation test writer with `--output` fresh evidence directory | Exit 0, explicitly HOST_FIXTURE records | Exit 0; `host-writer.txt`, `host-fixture/gameplay-33440.jsonl` |
| Analyzer on retained actual host writer output | Exit 0 for structure, no native acceptance | Exit 0; `host-trace-analysis.json`: 9 records, 3 completed fixture calls, coverage false, NOT_VERIFIED |
| Same host trace with `--require-native` | Exit 2 | Exit 2: `HOST_FIXTURE cannot satisfy a native evidence request`; `host-native-denial.txt` |
| New `--observe` host mode under operator account and deliberately wrong SID | Exit 20, zero game processes | Exit 20, `Dedicated standard account required`, `launched_processes=0`; `observe-guard/native-host.jsonl` |
| `git diff --check` | Exit 0 | Exit 0; `diff-check.txt` |

Each complete CTest pass contains 11 existing diagnostic host assertions, 22 new observation host assertions, 50 Python tests at that checkpoint, and 14 existing launcher host assertions. Host fixtures use temporary synthetic files, not personal saves. The actual pinned Detours library executes against project-owned compiled fixture methods; the SDK core and original game are never loaded by that test.

After the complete builds/tests, parser review added strict text/integer bounds and a regression for an object observed inside its factory before the factory returns. Exact final targeted command:

`python -m unittest discover -s tests/unit -p test_gameplay_trace.py -v`

Expected/observed: exit 0, **13/13 tests pass**, retained in `parser-final-tests.txt`. No compiled/native code changed after the full Release/Debug runs. The complete suite was not rerun for this parser-only change; affected parser tests were rerun. An additional read of the retained host trace verifies parser/writer compatibility after this refinement, and the final diff check covers the document updates.

## Earlier build/test observations retained

- Initial command `cmake -S . -B build/win32 -G 'Visual Studio 17 2022' -A Win32 -T 'v143,version=14.44.35207' -DCMAKE_SYSTEM_VERSION=10.0.26100.0` returned 0 but PowerShell split the unquoted dotted definition, producing `Ignoring extra path from command line: ".0.26100.0"`. Reissued with the entire `'-DCMAKE_SYSTEM_VERSION=10.0.26100.0'` argument quoted; exit 0 without the warning. The recorded final configure uses a literal argument array with the full pinned value.
- Initial `cmake --build build/win32 --config Release --target SporeMP.NativeHost --parallel 4`: exit 0.
- Initial `cmake --build build/win32 --config Release --target sporemp_observation_tests --parallel 4`: exit 0. The first `build/win32/Release/sporemp_observation_tests.exe` returned **1** at its combined nested-call-count/baseline assertion. Its fixture used a tail-recursive direct call, which does not require a separate function entry after optimization. Replaced that fixture call with a volatile member-function pointer to force a true nested entry; rebuilt and reran with exits 0/0. Final recorded Release/Debug runs pass all 22 assertions. This was a fixture correction, not SPORE runtime evidence.
- Earlier targeted parser runs passed 11 then 12 tests during implementation (exit 0); final source passes 13 as above. Missing guessed SDK filenames during read-only exploration were resolved by `rg --files`; provenance records reference actual existing paths.

## Identity and evidence boundary

Environment: [environment.json](environment.json). Windows 11 Pro 10.0.26200 x64; AMD Ryzen 7 9700X (8 cores/16 threads), 33,407,430,656 bytes RAM; NVIDIA RTX 4080 SUPER plus recorded AMD/Parsec adapters. SDK commit `cbf9206b9a823f0911cd9be0217104a49d72380b`; injector source `26adca9a2578b5bb32ba2eac90d96bd9ac7d48a9`. Tracked SDK source was clean during inspection; no dependency was fetched or edited.

The original executable was hashed read-only: GOG GA 3.1.0.29 Win32, SHA-256 `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`. The content candidate manifest SHA-256 is `06e58e2c169ff380e1fcea2ec519ef2b65653f87f82999ac644d004fdfdf6b3b`. Content files were not requalified and no candidate inventory was promoted.

Exact project source hashes: [source-manifest.json](source-manifest.json). Exact Release/Debug bridge, host, fixture executable, core and injector hashes: [build-artifacts.json](build-artifacts.json). Pinned binding headers/implementations/Detours provenance hashes: [sdk-binding-sources.json](sdk-binding-sources.json). The capture script checks that all 21 original milestone work/deliverables/acceptance sections remain intact; `docs/implementation-brief.md` was not changed.

**NATIVE: NOT RUN.** No game was launched, no desktop input was sent, and personal save files were not accessed. There is no new original-game action trace, baseline equivalence, runtime binding qualification, scene-lifecycle proof or measured native overhead. No engine failure or native impossibility has been reproduced. Documentation of the next fixture is not evidence that it passed.

## Next smallest step

Release the user's previously recorded no-desktop-control constraint, then run the actual paired Creature campaign fixture in [tests/engine/M02.md](../../tests/engine/M02.md), using the existing disposable developer environment and hash/backup gates. A concise permission question was presented after the probe and executable checks were ready. Until answered, do not launch/control a game. Normal player Play requires no separate profile or backup step.

Compare one accepted original jump through visible landing, native NPC/avatar activity, one actual entity invalidation, scene exit/reload and normal shutdown against the uninstrumented reference. Pair actual host/module/lifecycle/trace evidence and fixture hashes. Qualify the exact bindings only after those observations, then continue the remaining M02 damage/death, inventory/progression and save/load investigations without reducing the all-five-stage scope.
