# Development progress and publication boundary

Date: 2026-09-27. Evidence classes: **review of existing NATIVE records**,
**BUILD**, **HOST** and **static website checks**, distinguished below.
No original-game session was started for this documentation update.

## Development acceptance through M10

The private development checkout records M09 and M10 as VERIFIED for their
prepared original Creature fixtures. This report summarizes the retained
acceptance records; it does not re-run or broaden them.

- M09 (2026-09-22): restarting the coordinator and original worker, then
  reconnecting two fresh original clients, preserves the existing players,
  tested inventory, positive DNA progression and immutable creation ownership.
  Actual original-save interruptions and five coordinator-publication crash
  boundaries recover a complete selected checkpoint. Resubmission does not
  grant the recorded reward twice. Recovered authorities have an explicit
  five-minute checkpoint budget; unattended production checkpoint rotation is
  not qualified.
- M10 (2026-09-26): separately simulated original Creature locations support
  meeting, current-state return, paired original Save/publication, restart and
  reconnect. The recovery report records 36 native worker-fault cases across
  applicable transfer, abort and cleanup boundaries. Authenticated native
  global admission gates the recorded producer paths. These results do not
  establish arbitrary-world support, normal Space travel or every global
  background system.

The archived originals are `2026-09-22-m09-persistence/acceptance.md` and
`2026-09-26-m10-native-state/NATIVE-RECOVERY-086.md`. Their private traces,
captures, credentials and save artifacts are omitted under the public evidence
policy. The M10 report also records failed experiments and the correction
needed for destination recovery with pending source cleanup.

The reviewed original report SHA-256 values are
`f1dd5a38bf0a43dce6d3677c24e904fab4f880d6fa373a31f7d5174997a421b9`
(M09) and
`331a149fcf11adc6d7c0c080c5f5da2fbbed99bd5dbfd26eebe0851ce843f3f1`
(M10). These identify the private originals, not this condensed public report.

| Recorded M10 identity | Value |
|---|---|
| Original executable SHA-256 | `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37` |
| SDK commit | `cbf9206b9a823f0911cd9be0217104a49d72380b` |
| SDK model-lifetime patch SHA-256 | `7ca2bce9880f1f6a8423f4bb28c73e5c4cf8ea10a4700cf9cf1e3edbb4479b44` |
| Bridge version / SHA-256 | `0.0.86` / `2ae7476b9f6327a151fa24e71b092fe386a2fa1752340cc0c0d7e82d1b885eb1` |
| Frozen source manifest SHA-256 | `0e973dcfd0c81eee380ebc5fc41a85b564255774dbd737e87ef65f7a1c58b383` |
| OS / hardware | Windows 11 Pro 10.0.26200; Ryzen 7 9700X; GeForce RTX 4080 SUPER |

Native reproduction requires the recorded original-game/content build,
separate verified disposable profiles, protected personal saves and the
matching archived development payload and fixtures. A normal public clone
does not contain those private fixtures or the unexported M09/M10 code.

## Public source boundary

The public implementation remains the reviewed **M08 source export**, with
launcher **0.1.10** and bridge/NativeHost **0.0.50**. Its independent Detours 4
SDK migration remains intact. M09/M10 source has not been transferred or
qualified in this public checkout. A separate reviewed source export and its
build/tests are required before this clone can reproduce those implementations.

The development launcher is now **0.1.13**. Its new offline notes describe
completed M10 tests and the next Creature gameplay milestone. Version 0.1.12
already added prepared-location invitation checks; 0.1.13 adds no travel
controls. This report does not publish a launcher binary or change the public
source's launcher version.

M11 normal Creature gameplay coverage and M12 Cell coverage remain unverified.
The full M00–M20 plan, independent progression and all five stages are retained.
There is no public multiplayer package or announced release date.

## Supporting-material validation

The development checkout executed:

```powershell
node website/build.mjs
node website/check.mjs
pwsh -NoProfile -File tools/build/build-launcher.ps1 -Configuration Release
ctest --test-dir build/win32 -C Release -R '^launcher_host$' --output-on-failure
```

Expected and observed: all four commands exited 0. The website generated both
homepages and 14 localized articles; its checker validated 31 files and 388
local references, language metadata and anchors. The launcher and test builds
reported zero warnings/errors. The existing launcher HOST test passed 1/1 in
4.92 seconds. These checks did not open the launcher or SPORE and do not
constitute new native or WPF visual acceptance.

The built development launcher EXE SHA-256 is
`daa7b88c267a9b834cf32c92fd743921048484d09a5991bffdbf3927b5a04464`.
The source version, latest embedded notes and changelog all identify 0.1.13.

The publication updates both website locales, the latest development releases,
the roadmap, project progress and the explicit public-source boundary. Public
evidence allowlisting and static checks are run from this checkout before its
focused documentation commit. Deployment success and live page checks are
reported separately after the website push.
