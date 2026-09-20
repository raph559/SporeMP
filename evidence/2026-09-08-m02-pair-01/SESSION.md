# M02 paired Creature baseline, pair 01

Status: IN_PROGRESS. Both native runs and recorders are closed. The observed trace passes its action, console, reload and provenance checks; the observed video failed, so the paired visual comparison is NOT VERIFIED and overhead is NOT MEASURED. This does not reopen M01 or establish multiplayer. The user selected **Fullscreen at 2560 x 1440** for both tests and supplied all gameplay input. No assistant desktop input was sent. Earlier pending entries below are historical snapshots; the closure and capture-audit sections at the end record their resolved state.

## Fixture and implementation

The prior user-operated run produced the saved planet Satiria. With no SporeApp process running, `python evidence/2026-09-08-m02-pair-01/prepare-fixture.py` executed the existing guarded quiescent-backup implementation. Expected/observed exit 0; 25 files, 34,642,419 bytes copied and verified before launch. Actual saves remain only in ignored `local/m02-fixtures/pair-01-start`; `fixture-start.json` contains hashes and paths. Satiria.spo starting SHA-256 is `6bee962d075f1a31997fc3ee5fae7ba9e2f75362ac5417cae074d7ddb8a8b6a0`. A file backup does not prove native restoration.

`tools/native/invoke-isolated.ps1` now accepts optional display mode/resolution, validates before side effects, and passes four allowlisted host arguments. Defaults retain existing behavior. PowerShell AST parsing and ten validation-prefix cases passed, expected/observed exit 0 (`wrapper-validation.json`). No compiled code changed for this wrapper increment; the existing display host was already built/tested in the launcher-display session. Source and artifact SHA-256 values, executable/content manifest identity and pinned SDK/loader commits are in `provenance.json`.

## Native launch

Exact command executed by `launch-reference.ps1`:

```powershell
pwsh -NoProfile -File tools/native/invoke-isolated.ps1 -Action Launch -RunName m02-pair-01-reference -Configuration Release -ObservationMode Off -DisplayMode Fullscreen -Resolution 2560x1440
```

The driver was started using PowerShell Start-Process with WindowStyle Hidden; driver PID 33840, host PID 4992, original game PID 32172. The OS-isolation, personal-backup, content and payload guards passed. The original command line includes `-f -r:2560x1440`. The bridge initialized on thread 25140; no M02 gameplay JSONL is expected in Off mode. Expected normal game/wrapper exit 0; actual exit PENDING. The driver preserves diagnostic logs and runs personal-file verification when the user quits normally. Native staging is `C:\ProgramData\SporeMP\M01\runs\m02-pair-01-reference`.

Computer Use (`@oai/sky`) returned the actual game window, then showed the fullscreen splash (2048x1152 logical screenshot). A later read reported `window is minimized; call activate_window, refresh with get_window, then retry get_window_state`. No activation or input was sent because the user controls the game. The requested physical render resolution is not independently verified by that logical screenshot. Launcher-to-native selection and windowed-mode acceptance are still pending.

OS/hardware and actual process command line are in `reference/startup.json`: Windows 11 Pro 10.0.26200 x64, Ryzen 7 9700X, RTX 4080 SUPER driver 32.0.15.9649. Other applications, including another game, are open; any subsequent performance comparison must report concurrent load rather than claim a controlled benchmark.

## Capture prerequisite and actual failures

Installed recorder: `C:\Program Files\NVIDIA Corporation\FrameViewSDK\bin\PresentMon_x64.exe`, product version 1.7.12119.0, SHA-256 in provenance. `--help` returned its supported flags (exit 1 for help; output retained). Reference recording and three bounded probes did not produce a CSV. Explicitly waited probe 02 and console-attached probe 03 exited 1 with no diagnostic text. Earlier shell invocation did not wait and its null exit is not a successful recorder result. The exact probe-02 arguments/exit are in `reference/presentmon-probe02-result.json`. Probe 03:

```powershell
& 'C:\Program Files\NVIDIA Corporation\FrameViewSDK\bin\PresentMon_x64.exe' --process_id 32172 --output_file 'C:\Users\Developer\Documents\ChatGPT\SporeMP\evidence\2026-09-08-m02-pair-01\reference\frames-probe-03.csv' --session_name SporeMP-M02-probe03 --timed 3 --terminate_after_timed --no_track_input | Out-Default
```

Expected exit 0 and frame rows; actual recorder exit 1 and no CSV. Observation overhead is NOT MEASURED. Next experiment: determine the recorder startup failure or use a verified ETW capture/parser path before treating this as timing evidence. None of these commands supplies game input.

Video attempt (expected a game-window recording):

```powershell
ffmpeg -hide_banner -loglevel warning -f gdigrab -framerate 30 -draw_mouse 0 -i 'title=SPORE™ Galactic Adventures' -t 900 -an -c:v libx264 -preset ultrafast -crf 23 -pix_fmt yuv420p -n 'C:\Users\Developer\Documents\ChatGPT\SporeMP\local\m02-captures\pair-01\reference.mkv'
```

Actual exit 1: `[libx264] width not divisible by 2 (199x34)`; no frames were written. This is consistent with the observed minimized window, not successful gameplay video. Preserve that failed artifact; use a fresh output path on retry after the user foregrounds and loads the game. Do not substitute encoded video cadence for native frame timings.

## Next smallest step

The user reported ready with Satiria loaded. Computer Use shows the Creature avatar near the nest and other creatures. Game-window recording now works; the decoded first frame was visually inspected, and ffprobe reports H.264, 2560x1440, 30/1 recording cadence. This confirms capture dimensions, not native presentation rate or observation overhead. `reference/capture-start.json` records the exact command, UTC start, PID 8452, exec session 11493, preview hash and proposed user sequence. The local output is `local/m02-captures/pair-01/reference-02.mkv`, with a ten-minute limit; it does not overwrite the failed zero-byte first attempt.

Executed command (still running; native/media exit PENDING):

```powershell
ffmpeg -hide_banner -loglevel warning -nostats -stats_period 1 -progress 'local/m02-captures/pair-01/reference-02-progress.txt' -f gdigrab -framerate 30 -draw_mouse 0 -i 'title=SPORE™ Galactic Adventures' -t 600 -an -c:v libx264 -preset ultrafast -crf 23 -pix_fmt yuv420p -n 'local/m02-captures/pair-01/reference-02.mkv'
```

The first frame extraction and ffprobe inspection both exited 0. Initial progress showed frames arriving without recorded duplication/drop; this is recorder health only. The user was instructed to do five jumps with full landings and roughly three seconds between presses, walk near the nest for 30 seconds, return to the galaxy/main menu without saving, reload Satiria, do one more jump with a full landing, and quit normally without saving. Requested inputs are not evidence that the sequence happened.

After the user's completion, stop/finalize the owned recorder, inspect the video and closed native logs, verify personal hashes, and preserve the closed reference-end profile separately before restoring only the owned disposable roots to the hash-verified pair start. Then repeat the sequence with Observe and exercise `sporemp_trace`. Restoration, observed run, paired outcomes, native frame timing, normal exits and final personal hashes are pending. M00–M20 requirements remain intact.

## Reference closure, accepted save and matching observed run

The user reported: “done, but i accepted the save instead of declining”. This is a recorded protocol deviation. It does not erase the preserved initial fixture. `python evidence/2026-09-08-m02-pair-01/close-reference.py` exited 0: seven closure checks passed, native/wrapper exit 0, one initialization/disposal on thread 25140 with matching identity, observation Off/zero hooks, and all 29 personal-file hashes unchanged. Reference native exit was 2026-09-08T17:04:00.650Z. The saved developer profile was backed up to ignored `local/m02-fixtures/pair-01-reference-end`, verified and indexed in `fixture-reference-end.json` (27 files, 38,169,971 bytes; 16 file differences from the starting fixture).

Recorder exec session 11493 returned exit 0. At game-window closure, gdigrab reported `Failed to capture image (error 8)` and `Error during demuxing: I/O error`; the Matroska recording was finalized. ffprobe exited 0 and reports duration 238.333 seconds, size 538,972,224 bytes, H.264 2560x1440, recording rate 30/1. Final progress: 7,112 frames, zero duplicated frames, 38 recorder-dropped frames. Native frame timing cannot be inferred from this recording cadence. The complete video hash and metadata are in `reference/closed-verification.json`.

Visual review used the actual recorded media: a full-session overview at one sample per ten seconds, avatar-focused sheets at four samples/second over 90–106 and 106–122 seconds, and a two-sample/second sheet over 219–235 seconds. Sampled images show repeated avatar rise/landing sequences near the nest, movement and nearby creature activity, a return to the galaxy, the Creature loading screen, resumed gameplay and a post-reload jump/landing. These are visual observations, not instrumented reference action counts or precise timing measurements. The media also shows save/exit prompts; the user confirms accepting a save. Exact equivalence of the two save-prompt choices is not assumed. Extraction commands used `ffmpeg -ss START -i reference-02.mkv -vf FILTER -frames:v 1 -update 1 -n OUTPUT`, with `fps=1/10,scale=480:-1,tile=4x6` for the overview, `fps=4,crop=1000:900:780:350,scale=250:225,tile=8x8` for the two avatar sheets, and `fps=2,scale=640:-1,tile=4x8` for the reload sheet; all exited 0. Derived images remain in ignored `local/m02-captures/pair-01`.

`pwsh -NoProfile -File evidence/2026-09-08-m02-pair-01/restore-start.ps1` exited 0. Before either directory move, it verified the owned SID, no running game, both backup manifests/content, unchanged reference-end roots, no reparse points, and every resolved absolute move target under `C:\Users\SporeMP-M01`. Original roots were renamed to sibling `.m02-pair-01-reference-end` directories, not deleted. Starting trees were copied back with the original root ACLs. Afterward both restored roots exactly matched the 25-file starting manifest and both original directories matched the saved end manifest. `restore-preflight.json` and `fixture-restored.json` record the executed checks. This is verified closed-file restoration; native load acceptance awaits the user loading Satiria.

`launch-observed.ps1` runs the same isolated command as reference with run name `m02-pair-01-observed` and `-ObservationMode Observe`. All reference artifact hashes were checked unchanged immediately before launch. Driver PID 36588, host PID 12596, game PID 28584. The guarded original command is still `-f -r:2560x1440`; `gameplay-28584.jsonl` starts with NATIVE_PROBE provenance, seven hooks ready on thread 29032, and no multiplayer mutations. Exit and final loss/coverage checks are pending. `observed-launch-request.json` records the request and script hash.

The first observed video attempt failed before frames (`Invalid properties, aborting`, exit 1) because the game was minimized. `record-observed.ps1`, capture-driver PID 35784, now waits up to 120 seconds for a usable game window, retaining failed attempts. It records only the selected SPORE title, rejects the tiny minimized image using a full-resolution 2560x1440 crop, and bounds successful recording to ten minutes. This adds no desktop input. Current state and exact per-attempt arguments are in `observed/capture-status.json`; `capture-request.json` pins the script. A `capturing` state indicates frames, not completed visual acceptance. The user is to foreground/load Satiria, repeat the jump/walk/menu/reload sequence, accept save prompts to follow the save-bearing reference variant, invoke the read-only `sporemp_trace` command, and exit normally. The local original readme lines 519–521 document Ctrl+Shift+C and Enter for the command console.

Remaining: inspect the observed capture and closed native trace, verify the console/scene/object events and personal hashes, preserve the observed end, and compare native invariants while recording the save-choice uncertainty and uncontrolled timing/RNG. PresentMon startup remains unresolved; observation overhead is NOT MEASURED. No M02 completion or later-milestone claim is made.

Capture update: driver 35784 reached its 120-second deadline with no frames while the game remained minimized. That attempt is retained. A fresh bounded recorder, recorded in observed/capture-request-03.json and capture-status-03.json, now waits up to ten minutes for the user to foreground the game, then records for at most ten minutes. It uses distinct observed-03-attempt filenames and the same capture validation; it does not send input. No recording success is claimed before frames arrive.

## Observed closure and native analysis

The user completed the requested sequence and quit. Observed wrapper completion is 2026-09-08T17:25:56.180Z, expected/observed exit 0. Native game PID 28584 also exited 0. The bridge initialized at 17:11:35.999Z and disposed at 17:25:54.697Z on engine thread 29032; the gameplay trace records seven installed hooks and a clean footer with zero lost records and zero foreign-thread callbacks. All 29 personal save/creation files still match their protected originals. The loaded injector/core/bridge hashes match reference and the pinned artifacts in provenance.json. No additional game launch or assistant desktop input occurred during closure analysis.

Executed commands, each expected/observed exit 0:

```powershell
python tools/native/analyze-gameplay-trace.py evidence/2026-09-08-m02-pair-01/observed/native/gameplay-28584.jsonl --require-native --require-m02-coverage --output evidence/2026-09-08-m02-pair-01/observed/gameplay-analysis.json
python evidence/2026-09-08-m02-pair-01/analyze-pair.py
```

The raw native trace has 20,275 records / 14,253,509 bytes, SHA-256 `b3187ea1876276431e17550bef348344619ce3a20a64ea45dbe8ed6687fac742`. The strict analyzer passes its required schema/coverage rules. The pair analysis passes all 14 action, identity, lifecycle, payload, fixture and personal-file checks. Both reports retain native_acceptance=NOT_VERIFIED. `analyze-pair.py` SHA-256 is `af4af9494c657b27511931fab9f6edc3ef825385b9ca2bf04ac29dbea6c4845a`.

There are five accepted jumps before reload on sampled avatar ID 1513 / scene epoch 3, then one after reload on sampled avatar ID 8424 / epoch 6. Each action has one entry and one return, argument 0 and native result true; each has exactly one later landing on the same avatar before the next jump or invalidation, 0.988–0.998 seconds later. The six landing sequences are 4828, 4891, 4958, 4970, 4977 and 19991. This is temporal association, not an invented causal action identifier. Avatar identity is taken from current avatar_state samples: this run's sole avatar_assigned event resets it to null during the scene change and cannot identify the six jumps by itself.

The two diagnostic invocations are sequences 6893 and 20024, each immediately followed by an avatar sample. Subsequent native gameplay continues. The trace also records Creature exit, galaxy entry/exit, load entry/exit and Creature re-entry; five total exit/enter pairs, 5,747 entity invalidations, 170,941 NPC AI entries and 6,166 avatar AI entries. The new avatar ID after reload is diagnostic identity only. Complete destruction/message paths, console-rendered text, absence of visible side effects and persistent multiplayer identity remain unqualified.

`analyze-pair.py` preserved and verified the closed observed end as `local/m02-fixtures/pair-01-observed-end`: 27 files, 38,438,184 bytes, indexed by `fixture-observed-end.json`. The live developer profile remains at this observed end; the initial fixture, reference end and renamed reference roots also remain preserved. Native Creature load after the exact closed-file restoration was observed, but no exact save-file-open trace was captured. This does not establish M09 checkpoint/crash recovery. Save-choice and timing/RNG equivalence between the two runs is not assumed.

## Observed media rejection and bounded recorder replacement

The successful-bytes GDI attempt was recorder PID 32200, from 17:22:53.098Z to 17:25:55.597Z, exit 0. `local/m02-captures/pair-01/observed-03-attempt97.mkv` is H.264 2560x1440, nominal 30/1, 182.366 seconds and 1,290,831 bytes; SHA-256 `db42e9a1fbe4061c2b8afa1a4ceb4cf4e80435391e9bfd2b829e6ef4621d238e`. Progress reports 5,403 frames, zero duplicates and 43 recorder drops. These values did not establish correct capture content.

Actual decoded-media review found initial non-game pixels followed by black frames. The media and derived images remain ignored/local and must not be published. The following audit command exited 0 and reported black_start=18.833, black_end=182.333, black_duration=163.5 seconds:

```powershell
ffmpeg -hide_banner -i local/m02-captures/pair-01/observed-03-attempt97.mkv -vf blackdetect=d=0.5:pix_th=0.01 -an -f null NUL
```

Expected: actual original-game frames throughout the action/reload sequence. Actual: unusable observed video despite a valid container and exit 0. The working reference video cannot replace that missing observed evidence. Paired visual behavior and console text are NOT VERIFIED. GDI's precise failure mechanism has not been established; focus/minimization is relevant context, not a proven root cause.

`tools/native/capture-game.ps1` is the prepared developer replacement. The installed FFmpeg 8.1.1 exposes [gfxcapture](https://ffmpeg.org/ffmpeg-filters.html#gfxcapture), which uses Windows Graphics Capture. The script requires one exact game PID/path, exact executable and title filters, fresh ignored local output and no reparse-point paths. It supplies no monitor fallback, records for a bounded duration, terminates only its own recorder on timeout, and always leaves visual acceptance pending actual frame review. No game or launcher code changed for this repair. Source SHA-256: `06d78e0830355219e4e6d5f040ff62018f9421efd6698f77f8fad26052c9d0cc`.

HOST checks: PowerShell AST parsing passed. The final missing-game guard command below exited 1 as expected before creating output or starting capture; `wgc-final-guard-check.json` pins the result and final source hash.

```powershell
pwsh -NoProfile -File tools/native/capture-game.ps1 -GamePid 28584 -OutputDirectory local/m02-captures/wgc-no-game-final -Seconds 5
```

A separate installed-FFmpeg missing-window probe used the exact source filter `gfxcapture=window_exe='(?i)^SporeApp[.]exe$':window_title='^SPORE™ Galactic Adventures$':capture_cursor=0:max_framerate=30,hwdownload,format=bgra,format=yuv420p`. Expected rejection; actual exit -2, `Failed to find capture source`, `Failed to setup graphics capture`, zero frames and a zero-byte output. `wgc-source-check.json` retains diagnostics. This checks refusal without a desktop fallback, not successful native capture. Actual game recording with this replacement remains IMPLEMENTED_NOT_RUN.

Next smallest experiment: at the next authorized gameplay opportunity, validate a short original-game WGC clip before requesting further actions, then collect only the missing comparison coverage. Establish a separately verified native frame-time source before an overhead comparison; installed PresentMon startup still fails without frame data. WGC/encoded-video cadence is not native frame timing. The full M00–M20 acceptance plan remains in force, and M02 remains IN_PROGRESS.

## Final evidence integrity review

`python evidence/2026-09-08-m02-pair-01/finalize-closure.py` performs the final read-only process, hash, Git whitespace and original milestone-text checks. The first invocation exited 1 because the review script assumed 84 separately labeled requirements; the preserved plan actually has 82 (M19 and M20 do not have a separate Deliverables label). Every compared field was identical. `closure-integrity.json` retains that failed harness assumption. The review script now compares the complete original milestone sections, excluding only the explicit current-increment note, as well as every labeled field. No requirement was edited to make the check pass.

The final invocation expects exit 0 and records its actual result in `closure-integrity-02.json`, including exact commands, the process snapshot and final source/report hashes. It checks all 21 milestone IDs, complete original milestone sections, the 14 native pair checks and unchanged trace/analysis/recorder source hashes. No compiled gameplay or launcher code changed during closure/capture repair, so the appropriate executed checks are native trace analysis, pair correlation, PowerShell parsing, recorder refusal tests and evidence integrity; prior C++/launcher build results are not represented as rerun here.
