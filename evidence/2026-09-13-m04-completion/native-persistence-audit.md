# Controlled native checkpoint binding audit

Classification: **STATIC AUDIT and IMPLEMENTED_NOT_RUN** at creation. This note does not record a native load/save test or change M04 acceptance. The adapters still require the fresh original-game qualification recorded by the parent session.

The executable used as data is the pinned GOG GA 3.1.0.29 Win32 original-campaign executable, SHA-256 `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`. SDK commit is `cbf9206b9a823f0911cd9be0217104a49d72380b`. Ghidra 12.1.3/JDK 21.0.12.1 reuse the existing pinned, partially analyzed database. Decompiled types/names remain inference; disassembled argument setup, ECX use, returns and original callers establish the ABI described below. Native runtime behavior remains a separate gate.

## Findings and selected implementation

**Save:** original `0xB29600` is `bool __thiscall(manager, const wchar_t* filename, bool full_save)`. It retains ECX in EBP, reads the two stack slots, returns with `RET 8`, and returns the result in AL. Original Options continuation `0xE03417..0xE03429` pushes `true`, then `nullptr`, gets the persistence manager from `0xB3D440`, moves it to ECX, calls `0xB29600` and branches on `TEST AL,AL`. Its null filename uses the home planet's native name plus `.spo`. A separate original caller at `0xB2A240` uses `snapshot.spo,false`; the adapter does not use that incomplete snapshot branch.

The true branch runs backup preparation `0xB28420`, immediate message `0x0680C633`, original stage bookkeeping and callbacks, serialization `0xB29000`, and final marker `0xB28890`. Unlike the outer UI dispatcher `0xB29960`, `0xB29600` does not itself display the save-success dialog. Original Options code resumes its save pause before invoking the helper. The adapter runs at a serialized app-update boundary and calls it once with the fixed controlled filename `Satiria.spo,true` after verifying native Creature state and native home name `Satiria`.

**The returned bool is not a durable acknowledgement.** Serialization `0xB29000` is void; the final helper `0xB28890` opens `Game0.old/complete` through `0x931EF0`. The latter calls CreateFileW, SetFilePointer, SetEndOfFile and CloseHandle, without a FlushFileBuffers call in that helper. It does not propagate every individual file-operation result. Closed-file hashing, preservation of the preceding checkpoint, and successful native reload are separate acceptance evidence. The adapter records `native_save_return` with `request`, `native_success` and `durable:false`; the `saved` state means only the original helper returned true.

**Load:** an ordinary saved-game selection follows a different path from the SDK's uncertain `LoadGame(GameLoadParameters&)`. Original `0xDEBE70`, reached from `0xDEC130`'s saved-game branch, constructs the selected filename plus `.spo`. Its `0xDEBF4B..0xDEBF72` sequence sends immediate message `0x0680C633` with the filename, obtains reset-manager singleton `0xB3D460`, calls reset `0xB7E010`, obtains persistence manager `0xB3D440`, then calls `0xB28020(filename)`.

`0xB28020` is `void __thiscall(manager,const wchar_t*)`: ECX is retained, the one filename stack slot is consumed with `RET 4`, and no defined bool result exists. It copies the filename into native storage, composes the current original save-directory path, tests file existence, and requests original `kLoadGameMode` (`0x1654C08`) through `0xB1E350`. The resulting native load is asynchronous. It does not retain the passed filename pointer. `0xB7E010` is `void __thiscall(void*)`, no stack arguments and plain RET. Its manager constructor at `0xB7E0D0` installs primary vtable `0x14652B0`; the persistence constructor at `0xB26DF0` installs `0x145F9A0`. Both identities are checked before use.

The implemented load adapter permits only the original galaxy menu (`kGGEMode`, not any arbitrary non-Creature mode), only `Satiria.spo`, and runs the same immediate message/reset/filename-loader sequence. UI-specific selected-row/transition bookkeeping is not fabricated. The original gameplay loader restores its own checkpoint. No guessed star/species ownership is introduced. Runtime qualification must establish whether the original UI's additional progress display and bookkeeping are required on this pinned configuration; this is not assumed from static code alone.

Load completion is recorded only after a different scene epoch, original Creature mode, live original player/avatar, native home name `Satiria`, and increasing original AI counts across later app updates. `native_load_complete` includes the request, requested/observed epochs and AI before/after values. It explicitly does not certify restored bridge actor ownership; that is independently implemented and checked by the actor adapter. A 90-second completion deadline produces failure without retry.

**SDK layout finding:** the original parameter constructor `0xB26CE0` writes through offset `+0xB8`; original galaxy-list code `0xDF7F30` advances records by `0xBC`. The pinned SDK header's `GameLoadParameters` declares only `0xB4` and itself warns that the size is uncertain. Calling the constructor with SDK-sized storage would overflow it. The adapter does not instantiate or call this structure or `0xB271D0`.

## Lifecycle, fencing and observable state

`src/bridge/native_persistence.h/.cpp` define initialization, one-request enqueue, app-update execution, status annotation and disposal. `native_persistence_abi.h` exposes the exact aliases for independent Win32 executable ABI fixtures. Initialization assumes the existing executable/content/loader guard has succeeded, then checks five exact code/getter prefixes and the manager identity; embedded absolute getter operands are relocated before comparison. No work is added to DllMain or an I/O thread.

Requests contain no filename/pointer/container: the fixed filename remains inside the bridge. All ten command scalars must be zero, request sequence must be nonzero, and the epoch must match. Pending requests are rechecked on dispatch. The worker must refuse other mutations while persistence is pending/running. Status scalar 8 contains the engine request sequence, scalar 9 is state: `idle=0`, `save_pending=1`, `saving=2`, `saved=3`, `load_pending=4`, `loading=5`, `loaded=6`, `failed=7`, `unavailable=8`. `NativePersistenceSnapshot` separately exposes whether the native save bool exists and its value.

## Reproducible static evidence

Each command below exited 0; all requested functions decompiled. Exact expanded Ghidra arguments, tool versions and executable fingerprint are in the associated `local/m03-static/m04-completion-*.json` files. Output decompilations remain local and excluded from Git.

```powershell
pwsh -NoProfile -File tools/native/invoke-static-audit.ps1 -RunKey m04-completion-save-callers -ReuseDatabase -Addresses B2A240,E033A0,B28420,931EF0
pwsh -NoProfile -File tools/native/invoke-static-audit.ps1 -RunKey m04-completion-load-callers -ReuseDatabase -Addresses B3D440,BADAD0
pwsh -NoProfile -File tools/native/invoke-static-audit.ps1 -RunKey m04-completion-load-ui -ReuseDatabase -Addresses DE30E0,DF7F30,DE8710,DEB8E0,DEBE70,DEC130,B269F0,E82C70
pwsh -NoProfile -File tools/native/invoke-static-audit.ps1 -RunKey m04-completion-load-core -ReuseDatabase -Addresses B28020,B26CE0,B26F90,DEB7F0,DEB580,DEBE70
pwsh -NoProfile -File tools/native/invoke-static-audit.ps1 -RunKey m04-completion-load-prereq -ReuseDatabase -Addresses B3D460,B7E010,DEB580,B5D500,B26710,B26900
pwsh -NoProfile -File tools/native/invoke-static-audit.ps1 -RunKey m04-completion-load-view -ReuseDatabase -Addresses E3E350,B3D510,DDE990,B7E100
pwsh -NoProfile -File evidence/2026-09-13-m04-completion/audit-native-persistence.ps1
```

The final audit command validates the executable fingerprint, records exact objdump commands and exits, and hashes the SDK input headers, implementation snapshot, Ghidra metadata and all exported files into `native-persistence-static-provenance.json`. Disassembly is `local/m03-static/m04-completion-abi-disassembly.txt`. Parent-session source hashes supersede this initial implementation snapshot if integration changes these files.

Read-only exploration also tried `python -c "import capstone; print(capstone.__version__)"`; it exited 1 because that Python environment has no capstone module. No package was installed. Existing objdump supplied assembly evidence. Two exploratory `rg`/Get-Content calls used missing guessed file names (`Simulator/Simulator.h` and `.hpp` variants); repository enumeration resolved the real `.h` files. These failures did not execute native code or mutate saves.

Next smallest test: build the adapter and run Win32 ABI fixtures; from one prepared disposable worker profile, enqueue fixed-name load from the observed original galaxy menu, observe a new native Creature epoch and AI progress, then enqueue save and correlate the original return with a closed hashed checkpoint and fresh-generation reload. Require unchanged personal hashes, original scene inspection, restored/fenced A/B identities, and bounded actions after restore before accepting the persistence increment.
