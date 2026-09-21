# SporeMP

SporeMP is an experimental multiplayer mod for original SPORE. Its goal is to connect all five campaign stages—Cell, Creature, Tribal, Civilization and Space—in one persistent shared universe, while preserving the original game's gameplay, AI, combat, economy, editors and progression.

**Partial/experimental: authenticated shared Creature movement and reconnects have been demonstrated in a bounded original-game test scene. The full multiplayer campaign is unfinished; this repository is not a public multiplayer release.**

This repository contains the C++ bridge, coordinator and worker tools, Windows desktop launcher, build tooling, tests and development documentation. Instrumented original SPORE processes execute native gameplay; the coordinator handles identity, transport, ownership and supervision. The project does not replace the game with a separate simulation.

The mod source and reviewed development history are hosted in the public [raph559/SporeMP repository](https://github.com/raph559/SporeMP). See [CONTRIBUTING.md](CONTRIBUTING.md) for reports and contributions, [repository workflow](docs/repository.md) for development, and the [evidence index](evidence/README.md) for concise acceptance records. Full traces and operational records are retained privately under the [public evidence policy](docs/public-evidence.md); they are not required for a normal source checkout.

For the project presentation, development news and roadmap, visit the [SporeMP website](https://raph559.github.io/sporemp-site/). Its independent public source repository is [raph559/sporemp-site](https://github.com/raph559/sporemp-site).

## Current progress

M00–M06 are **VERIFIED within their recorded acceptance boundaries**:

| Milestones | Recorded result |
|---|---|
| M00–M01 | Project scope, pinned build, guarded original-game loading and the normal-account player launcher. |
| M02–M03 | Native Creature observations, two-actor commands, NPC combat and reward ownership in the recorded test scene. |
| M04 | Original-game worker independence, bounded worker isolation, overlapping native saves and checkpoint recovery with existing actor identities. |
| M05 | Authoritative results applied to a replica without a duplicate reward, with challenged local mutations and stale/disconnected updates fenced. |
| M06 | One original-game worker and two original-game clients, authenticated scene sharing, distinct controlled actors, movement/jumps and fresh reconnect baselines. Actual launcher Join/Rejoin preserves player identity. |

The current networking qualification is limited to a **living Creature test scene** with matching game/content and saved-world fixtures. Death/respawn and a complete shared encounter are the next M07 work. General content transfer, durable server restart recovery, all-stage gameplay, Internet hosting qualification and release packaging remain unfinished. Rendered workers currently require the recorded signed-in Windows desktop configuration; headless/service operation is not qualified.

See [STATUS.md](STATUS.md) for the current evidence and limitations, [GOAL.md](GOAL.md) for the product scope, and [MILESTONES.md](MILESTONES.md) for the complete M00–M20 acceptance plan. The complete user-adopted brief is preserved in [docs/implementation-brief.md](docs/implementation-brief.md). Native execution and HOST/FIXTURE tests are reported separately; a successful build or host test does not establish native gameplay acceptance.

## Launcher

The launcher's **What's new** section shows the latest changes and complete release history. Repository history is also summarized in [CHANGELOG.md](CHANGELOG.md).

After building, open `build/launcher/Release/SporeMP.exe` and select **Play SPORE**. It detects the installed game, checks compatibility automatically and launches under your normal Windows account with existing saves. No separate profile or backup setup is required. Details stay in Settings.

Launcher **0.1.9** adds private invitations, authenticated **Join/Rejoin** and connection status, using bridge/NativeHost **0.0.30**. These experimental multiplayer controls require the matching prepared Creature fixture described in [docs/m06-network.md](docs/m06-network.md). They do not make arbitrary saves or installations multiplayer-ready. Separate worker accounts and native test preparation are developer infrastructure, not normal Play prerequisites.

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

The old validate/preflight commands remain read-only diagnostics. Normal Play uses the compiled native host. Compatibility qualification covers the exact recorded local GOG GA 3.1.0.29 executable/content/SDK/injector configuration, not every SPORE installation. See [docs/compatibility.md](docs/compatibility.md), [docs/testing.md](docs/testing.md) and [docs/recovery.md](docs/recovery.md).

No EA executables, assets or saves are distributed in source. Operators and players need their own original-game installations. Dependency provenance and redistribution review are tracked in `docs/sources.md`.

## License

SporeMP's original code is licensed under the **GNU General Public License, version 3 or any later version** (`GPL-3.0-or-later`). See [LICENSE](LICENSE) for the license text and [licensing scope](docs/licensing.md) for the grant and exclusions.

Third-party components retain their respective licenses, documented in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). The code license does not assign a license to the project's artwork or grant rights to EA game content, third-party materials or personal data. This source publication includes no compiled game package or EA content.
