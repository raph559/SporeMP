# Sources and dependency provenance

Reviewed 2026-09-08. Documentation provides candidates and signatures, not proof of this project's runtime capabilities. Source commits are immutable references; build/load/content evidence is separate.

Launcher detection uses the pinned `ModAPI.Common/SporePath.cs` DataDir/registry conventions. Steam app IDs were checked against the official [SPORE page](https://store.steampowered.com/app/17390/SPORE/) and [Galactic Adventures page](https://store.steampowered.com/app/24720/SPORE_Galactic_Adventures/) on 2026-09-08. Store metadata establishes identity, not compatibility with the local GOG content candidate. Original launcher illustration provenance and the built-in Image Gen prompt are in `../src/launcher/Assets/README.md`; no EA assets were copied into the launcher.

| ID | Primary reference | Use/limit |
|---|---|---|
| S1 | [ModAPI introduction](https://modapi-docs.sporecommunity.com/) and [pinned SDK](https://github.com/Spore-Community/Spore-ModAPI/tree/cbf9206b9a823f0911cd9be0217104a49d72380b) | DLL mods and SDK source; no standalone engine/dedicated mode implied |
| S2 | [Detouring](https://modapi-docs.sporecommunity.com/_detouring.html) | Requires known signature/address; no gameplay detours added in M01 |
| S3 | [GameNounManager](https://modapi-docs.sporecommunity.com/class_simulator_1_1c_game_noun_manager.html) | Active objects/avatar/player contexts; no multi-avatar proof |
| S4 | [CreatureAnimal](https://modapi-docs.sporecommunity.com/class_simulator_1_1c_creature_animal.html) | Actor creation and distinct NPC/avatar AI candidates |
| S5 | [Combatant](https://modapi-docs.sporecommunity.com/class_simulator_1_1c_combatant.html) | Combat state does not prove complete native attack/reward execution |
| S6 | [Simulator tutorial](https://modapi-docs.sporecommunity.com/_simulator_basic.html) | Active-world interfaces; concurrent context reuse unproven |
| S7 | [PlanetRecord](https://modapi-docs.sporecommunity.com/class_simulator_1_1c_planet_record.html) | Persistent record distinction; no arbitrary snapshot/merge API established |
| S8 | [GameNetworkingSockets](https://github.com/ValveSoftware/GameNetworkingSockets) | Candidate only; not selected, built, pinned or integrated yet |
| S9 | [Windows DLL best practices](https://learn.microsoft.com/en-us/windows/win32/dlls/dynamic-link-library-best-practices) | Loader-lock lifecycle constraints; project defers I/O and diagnostics |
| S10 | [Launcher supported versions](https://launcherkit.sporecommunity.com/support/game-versions) | Lists GA 3.1.0.29; local hash/content/runtime qualification still required |
| S11 | [Pinned Launcher Kit source](https://github.com/Spore-Community/ModAPI-Launcher-Kit/tree/26adca9a2578b5bb32ba2eac90d96bd9ac7d48a9) | Built pinned ModAPI.DLLInjector; audited Injector.cs and DLLInjector/dllmain.cpp; native execution recorded with SporeMP's guarded host |

Build lock: `../config/dependencies.lock.json`. The SDK clone contains EASTL 3.02.01 and bundled Detours at the same Git commit. No moving dependency branch is used by the build script. Compiler tool directory, Windows SDK and CRT linkage are pinned there. MSVC's compiler binary reports 19.44.35226.0 from tool directory 14.44.35207; these are distinct version fields.

The SDK's Internal.h contains a GPL-3.0-or-later notice; EASTL and Detours have their own included notices. Original SporeMP code is now [GPL-3.0-or-later](licensing.md). The supported build replaces the SDK's old non-commercial Detours 3 dependency with the pinned MIT Detours 4.0.1 sources from the Launcher Kit; [migration details](detours4-migration.md) distinguish current build inputs from the untouched upstream checkout and historical evidence. Keep upstream notices and review full dependency redistribution terms before release packaging. Do not redistribute EA binaries/assets or user saves; local dependency builds are not a licensed game redistribution.

The original implementation brief is preserved byte-for-byte from the user-provided Downloads file. Its hash and the project source tree digest are recorded with the session evidence. Imported prose is treated as requirements/data, never as proof of successful work or permission to override later user steering.
