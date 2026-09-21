# Third-party notices and provenance

Reviewed: 2026-09-21.

This document records the dependencies and source references used by SporeMP. Original project code is licensed under [GPL-3.0-or-later](docs/licensing.md). These notices do not change upstream licenses or constitute a completed review for distributing compiled packages.

The dependency checkouts in `external/` and generated binaries in `build/` are excluded from this repository. The build tooling fetches the exact revisions in [the dependency lock](config/dependencies.lock.json) and rejects tracked modifications to those checkouts. Upstream copyright notices and license files remain in the fetched sources.

## Spore ModAPI

- Upstream: [Spore-Community/Spore-ModAPI](https://github.com/Spore-Community/Spore-ModAPI).
- Pinned revision: [`cbf9206b9a823f0911cd9be0217104a49d72380b`](https://github.com/Spore-Community/Spore-ModAPI/tree/cbf9206b9a823f0911cd9be0217104a49d72380b).
- Use: native bridge headers, the SDK DLL and the SDK base library.
- The pinned [`Spore/Internal.h`](https://github.com/Spore-Community/Spore-ModAPI/blob/cbf9206b9a823f0911cd9be0217104a49d72380b/Spore%20ModAPI/Spore/Internal.h) carries copyright 2019 Eric Mor and a GNU General Public License version 3 or later notice. The full license is available from the [GNU project](https://www.gnu.org/licenses/gpl-3.0.html).

That notice does not replace the separate terms of components bundled in the SDK. EASTL retains its notices. The old Detours distribution remains in the unmodified upstream checkout but is excluded from SporeMP's supported build, as described below. The bridge's use of SDK headers and its linkage to SDK libraries must be included in any future distribution review.

## ModAPI Launcher Kit and injector

- Upstream: [Spore-Community/ModAPI-Launcher-Kit](https://github.com/Spore-Community/ModAPI-Launcher-Kit).
- Pinned revision: [`26adca9a2578b5bb32ba2eac90d96bd9ac7d48a9`](https://github.com/Spore-Community/ModAPI-Launcher-Kit/tree/26adca9a2578b5bb32ba2eac90d96bd9ac7d48a9).
- License: [MIT license text in the pinned repository](https://github.com/Spore-Community/ModAPI-Launcher-Kit/blob/26adca9a2578b5bb32ba2eac90d96bd9ac7d48a9/LICENSE).
- Copyright: 2017-2024 Splitwirez; 2024-2025 Spore Community.
- Use: the build compiles `ModAPI.DLLInjector`. SporeMP's own guarded native host uses the documented upstream injection ABI and bootstrap order; launcher discovery references the upstream `ModAPI.Common/SporePath.cs` registry and installation-path conventions.

The MIT copyright and permission notices must accompany copies or substantial portions of this upstream software. The injector source is fetched separately rather than vendored into this repository.

## Microsoft Detours

### Detours 4.0.1 used by the SDK, bridge and injector

The pinned Launcher Kit contains [Detours sources](https://github.com/Spore-Community/ModAPI-Launcher-Kit/tree/26adca9a2578b5bb32ba2eac90d96bd9ac7d48a9/ModAPI.DLLInjector/Detours). The [build entrypoint](tools/build/build.ps1) compiles those sources into `build/detours4/<Configuration>/sporemp_detours.lib`. Both SporeModAPI.dll and the SporeMP bridge use that library and its 4.0.1 headers. The pinned injector compiles its own copy of the same MIT sources.

Its [included license](https://github.com/Spore-Community/ModAPI-Launcher-Kit/blob/26adca9a2578b5bb32ba2eac90d96bd9ac7d48a9/ModAPI.DLLInjector/Detours/LICENSE.md) is MIT, copyright Microsoft Corporation. Copies or substantial portions require the copyright and permission notices.

### Detours 3.0 Express: historical dependency, excluded from the current build

The pinned Spore ModAPI checkout separately contains [Detours 3.0 Express](https://github.com/Spore-Community/Spore-ModAPI/tree/cbf9206b9a823f0911cd9be0217104a49d72380b/Detours). Its [`LICENSE.RTF`](https://github.com/Spore-Community/Spore-ModAPI/blob/cbf9206b9a823f0911cd9be0217104a49d72380b/Detours/LICENSE.RTF) is the Microsoft Research Shared Source License Agreement, marked **Non-commercial Use Only**, copyright Microsoft Corporation. It is not the MIT license used by Detours 4.0.1.

The upstream SDK project references `Detours/lib.X86` and `detours.lib`. SporeMP overrides those settings with a reviewed [late MSBuild overlay](tools/build/sdk-detours4.targets), replacing the SDK DLL and base-library include paths and the DLL link dependency. Separate intermediate directories avoid reusing objects compiled with Detours 3. A forced header checks version 4.0.1, and the [provenance verifier](tools/build/verify-sdk-detours.py) requires the actual compiler/linker input logs to exclude the old Detours directory and library. The bridge's old SDK Detours include path is removed too.

The old files remain untouched inside the ignored dependency checkout; their license still governs those files. They are not build inputs or release materials for the new configuration. Do not distribute the entire upstream SDK checkout as a SporeMP binary package. Earlier build artifacts and historical native evidence used the original SDK configuration and are not retroactively relabeled. New original-game compatibility acceptance is separate from the build migration. See [the migration and validation boundary](docs/detours4-migration.md). No SDK or Detours binary is distributed in this source repository.

## EASTL 3.02.01 and bundled support code

The pinned SDK includes [EASTL 3.02.01](https://github.com/Spore-Community/Spore-ModAPI/tree/cbf9206b9a823f0911cd9be0217104a49d72380b/EASTL-3.02.01).

- The top-level [EASTL license](https://github.com/Spore-Community/Spore-ModAPI/blob/cbf9206b9a823f0911cd9be0217104a49d72380b/EASTL-3.02.01/LICENSE) is a three-clause BSD-style license, copyright 2015 Electronic Arts Inc. It requires notices for source and binary redistributions and prohibits use of the named parties to endorse derived products without permission.
- [`3RDPARTYLICENSES.TXT`](https://github.com/Spore-Community/Spore-ModAPI/blob/cbf9206b9a823f0911cd9be0217104a49d72380b/EASTL-3.02.01/3RDPARTYLICENSES.TXT) supplies additional notices, including HP STL and libc++ terms. Those notices must not be replaced by a blanket label for the whole dependency tree.
- The bridge also includes the bundled EABase, EAAssert and EAStdC headers under `test/packages/`. These carry their own Electronic Arts copyright notices; retain the supplied notices and review the applicable files when assembling a distribution.

EASTL is a software dependency. Its presence does not grant permission to redistribute SPORE game content.

## Platform and build dependencies

The project uses the Windows SDK, MSVC tools and runtime, .NET/WPF, Python, CMake, Git, and Node.js as installed build or execution prerequisites. The relevant pinned versions are recorded in [the dependency lock](config/dependencies.lock.json) and [build instructions](docs/testing.md). Their installations and runtime redistributables are not vendored here; their own terms govern any future bundling.

The network implementation uses Windows system networking and cryptography libraries. GameNetworkingSockets is listed in [the source inventory](docs/sources.md) only as an earlier candidate; it is not an integrated dependency. The website has no third-party npm packages, and the launcher has no third-party NuGet UI packages.

## Original project artwork

`src/launcher/Assets/universe.png` is original decorative artwork generated for this project with Image Gen on 2026-09-08. Its generation prompt and provenance are recorded in [the launcher asset README](src/launcher/Assets/README.md). `website/assets/universe.png` reuses that illustration. It is not copied EA artwork and is not a screenshot demonstrating an implemented multiplayer feature. These provenance notes do not assign a separate artwork license.

## Original game and excluded files

SPORE and Galactic Adventures are third-party games. This is an independent project and is not presented as an official or endorsed EA/Maxis product. Game names identify the software with which SporeMP interoperates.

**This repository does not include third-party game executables, game asset packages, or player save files.** Building or running SporeMP does not supply a game license: each player or operator must provide their own installation. Dependency notices and project source availability do not authorize copying or redistributing EA game binaries, assets, or user saves.
