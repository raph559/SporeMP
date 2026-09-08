# SporeMP

Global multiplayer for original SPORE, developed incrementally across all five campaign stages in one persistent shared universe. **Partial/experimental: no multiplayer gameplay is implemented yet.**

M00 and M01 are VERIFIED. Three original-game bridge lifecycles and compatibility checks passed, and the user confirmed the simplified player launcher works. See STATUS.md and evidence/2026-09-08-m01-native/SESSION.md.

Start with `GOAL.md`, `MILESTONES.md` and `STATUS.md`. The complete user-adopted brief is preserved in `docs/implementation-brief.md`.

The launcher's **What's new** section shows the latest changes and complete release history. Repository history is also summarized in [CHANGELOG.md](CHANGELOG.md).

Open build/launcher/Release/SporeMP.exe and select **Play SPORE**. It detects the installed game, checks compatibility automatically and launches under your normal Windows account with existing saves. No separate profile or backup setup is required. Details stay in Settings; multiplayer is still in development.

## Build and test

Windows, Git, CMake 3.24+, Python 3.10+, Visual Studio 2022 Build Tools with MSVC 14.44.35207/v143 and Windows SDK 10.0.26100.0 are required. The upstream project at the pinned commit uses v143, despite an older SDK tutorial naming v142.

The launcher additionally requires .NET SDK 8.0.418 and the .NET/WindowsDesktop 8.0.24 targeting/runtime packs. `global.json` pins the SDK and external NuGet sources are disabled. The build creates local runtime configuration automatically; it does not open the launcher or game.

```powershell
pwsh -NoProfile -File tools/build/build.ps1 -Configuration Release -FetchDependencies
ctest --test-dir build/win32 -C Release --output-on-failure
pwsh -NoProfile -File tools/build/build.ps1 -Configuration Debug
ctest --test-dir build/win32 -C Debug --output-on-failure
```

Artifacts: `build/win32/Release/SporeMP.Bridge.dll` and `build/sdk/Release/SporeModAPI.dll`. Build tooling does not install or launch them. The SDK core must never be loaded into an ordinary test executable: it binds native game addresses during DLL load.

```powershell
python tools/diagnostics/sporemp_diag.py validate --game-root 'C:\Games\SPORE'
python tools/diagnostics/sporemp_diag.py preflight --game-root 'C:\Games\SPORE'
```

The old validate/preflight commands remain read-only diagnostics. Normal Play uses the compiled native host. M01 qualification covers the exact recorded local executable/content/SDK/injector configuration. See docs/testing.md and docs/recovery.md.

No EA executables, assets or saves are distributed in source. Operators and players need their own original-game installations. Dependency provenance and redistribution review are tracked in `docs/sources.md`.
