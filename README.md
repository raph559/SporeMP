# SporeMP

SporeMP is an experimental multiplayer mod for original SPORE. Its goal is to connect all five campaign stages—Cell, Creature, Tribal, Civilization and Space—in one persistent shared universe, while preserving the original game's gameplay, AI, combat, economy, editors and progression.

**Partial/experimental: a shared Creature encounter, native creation/editor transactions, checkpoint recovery and location travel are verified within recorded original-game fixtures; normal Creature gameplay is in progress. The full multiplayer campaign is unfinished; this repository is not a public multiplayer release.**

This repository contains the C++ bridge, coordinator and worker tools, Windows desktop launcher, build tooling, tests and development documentation. Instrumented original SPORE processes execute native gameplay; the coordinator handles identity, transport, ownership, supervision and persistence metadata. The project does not replace the game with a separate simulation.

The mod source and reviewed development history are hosted in the public [raph559/SporeMP repository](https://github.com/raph559/SporeMP). See [CONTRIBUTING.md](CONTRIBUTING.md) for reports and contributions, [repository workflow](docs/repository.md) for development, and the [evidence index](evidence/README.md) for concise acceptance records. Full traces and operational records are retained privately under the [public evidence policy](docs/public-evidence.md); they are not required for a normal source checkout.

For the project presentation, development news and roadmap, visit the [SporeMP website](https://raph559.github.io/sporemp-site/). Its independent public source repository is [raph559/sporemp-site](https://github.com/raph559/sporemp-site).

## Development progress and source availability

M00–M10 are verified for their recorded original Creature fixtures. **M11, normal Creature gameplay, is IN_PROGRESS with 3 of its 12 gates closed**: controls (C01), combat (C03) and eating (C02). Each is closed by a scripted scenario that passes after a fresh restore, with no person in the loop: key and mouse input is delivered inside each player's own game process, state is read back from the original interface, and recorded frames are inspected by the assistant. The other nine gates, among them social actions, packs and nests, separate species, mating and death, remain open. See the [gate map](tests/engine/M11.md), the [eating gate report](evidence/2026-10-10-m11-c02-eating/SESSION.md) and [STATUS.md](STATUS.md).

**This public checkout holds the development source through bridge/NativeHost 0.0.256 (2026-10-10)**: the M09, M10 and M11 implementation, the native trial runner and its scenarios, exported on 2026-10-11. It is a source publication; no compiled package is distributed, and the website is not a download. See the [export review](evidence/2026-10-11-source-export/SESSION.md).

## Exported implementation

M00–M10 are **VERIFIED within their recorded acceptance boundaries**:

| Milestones | Recorded result |
|---|---|
| M00–M01 | Project scope, pinned build, guarded original-game loading and the normal-account player launcher. |
| M02–M03 | Native Creature observations, two-actor commands, NPC combat and reward ownership in the recorded test scene. |
| M04 | Original-game worker independence, bounded worker isolation, overlapping native saves and checkpoint recovery with existing actor identities. |
| M05 | Authoritative results applied to a replica without a duplicate reward, with challenged local mutations and stale/disconnected updates fenced. |
| M06 | One original-game worker and two original-game clients, authenticated scene sharing, distinct controlled actors, movement/jumps and fresh reconnect baselines. Actual launcher Join/Rejoin preserves player identity. |
| M07 | Shared native combat/feeding and results, reconnect and fresh-session reset under actual packet impairment, with owner/stale-result rejection. |
| M08 | Native creation transfer, matching original model/part/capability data, missing-part preflight, canonical terrain checks and immutable editor commit/cancel while another player continues. |
| M09 | Durable native checkpoints recover both original players, exact tested inventory/progression and creation ownership; actual save/publication crash tests preserve the selected revision and reject duplicate rewards. |
| M10 | Separate original Creature locations, meeting, current-state return, paired native saves and restart/reconnect; authenticated global admission and 36 recorded native worker-fault cases. |

Current qualification covers the **recorded Creature fixtures** with a pinned game/content profile and canonical saved-world bundle. M08 content intake uses locally approved native creations and developer transaction controls; it does not add a public creation-sharing flow or replace campaign avatars. [M08 acceptance and retained limitations](evidence/2026-09-22-m08-completion/SESSION.md).

M09 uses an explicit protected stop/save/seal/commit/restart workflow with a five-minute budget for recovered authorities. It does not yet provide unattended checkpoint rotation. [M09 acceptance and recovery limits](evidence/2026-09-22-m09-persistence/acceptance.md).

The recorded native runs, M07 through M11, were made with the development build, whose SDK DLL is the upstream build. This public checkout builds the SDK with MIT Detours 4 instead; original-game execution with that rebuilt SDK remains **NOT RUN**. Source export and HOST test results do not extend the native qualifications. See [SDK build boundaries](docs/detours4-migration.md).

**Next: M11 gate C06, separate species**, then the remaining Creature gates, Cell and the later stages. All-stage gameplay, arbitrary worlds/content, Internet hosting qualification and release packaging remain unfinished. Rendered workers require the recorded signed-in Windows desktop configuration; headless/service operation is not qualified.

See [STATUS.md](STATUS.md) for the current evidence and limitations, [GOAL.md](GOAL.md) for the product scope, and [MILESTONES.md](MILESTONES.md) for the complete M00–M20 acceptance plan. The complete user-adopted brief is preserved in [docs/implementation-brief.md](docs/implementation-brief.md). Native execution and HOST/FIXTURE tests are reported separately; a successful build or host test does not establish native gameplay acceptance.

## Launcher

The launcher's **What's new** section shows the latest changes and complete release history. Repository history is also summarized in [CHANGELOG.md](CHANGELOG.md).

After building, open `build/launcher/Release/SporeMP.exe` and select **Play SPORE**. It detects the installed game, checks compatibility automatically and launches under your normal Windows account with existing saves. No separate profile or backup setup is required. Details stay in Settings.

Launcher **0.1.14** carries the development notes through the first M11 social experiments. It keeps the prepared-location invitation checks of 0.1.12 and reports a restored shared world only after native recovery and an applied baseline. Private invitations, authenticated **Join/Rejoin** and connection status remain available; the current bridge/NativeHost is **0.0.256**. These experimental multiplayer controls require the matching prepared Creature fixture described in [docs/m06-network.md](docs/m06-network.md). Travel uses developer tools; arbitrary saves and installations are not multiplayer-ready. Separate worker accounts and native test preparation are developer infrastructure, not normal Play prerequisites.

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
