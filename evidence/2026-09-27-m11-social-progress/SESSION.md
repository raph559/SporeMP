# M11 social rewards and saved progress

Date: 2026-09-27. **M11 IN_PROGRESS; all full C01–C12 gates remain open.**
This is a reviewed summary of private development trials 17–22. Raw traces,
captures, profile inventories and saves remain private. This public source is
still M08, bridge/NativeHost 0.0.50 and launcher 0.1.10, with its independent
Detours 4 SDK migration. The implementations described below are not exported.

## Observed native results and retained failures

- Trial 17 (.102), one original authority and two original clients: B earns the
  original social reward; First DNA guidance appears only on B's client while
  A can jump. Original save completes with A 35 / B 10 DNA and both completed
  tutorial markers. Trial 18 (.103) reloads that state without replay and both
  actors jump. Process-relative clocks are compared only within each process;
  cross-process concurrency also uses inspected captures and ordered UI actions.
- Trial 19 (.104), repeated network exchange: one greeting fails; the next gives
  B the expected 10 DNA. A later species objective incorrectly gives A another
  25 DNA. The immediate reward passing does not qualify the entire transaction.
  Trial 20 (.105) reproduces the leak locally and observes the exact native
  objective callback. Both attempts are retained as failures; neither is saved.
- Trial 21 (.106), local authority: the original relationship changes from two
  to three befriended creatures and relation 4 to 6. A one-use receipt connects
  this exact transition to the original delayed objective callback. The game
  computes and awards B 10 DNA for the encounter and 25 for the objective;
  A remains at 35, B reaches 45. The bridge routes ownership, not the calculation.
  Original Save reports successful completion before process exit.
- Trial 22 (.106), fresh original load: A 35 / B 45 DNA, both tutorial markers
  completed, existing identities adopted and zero reward replay. All 682 saved
  census entries agree, including the previously held unlocks and statuses.
  Both actors perform original jumps and land. This is not a new-part-unlock test.

Every listed process closes cleanly. Closed actor traces report healthy state
and no foreign-thread callbacks. Recordings were inspected for the relevant
views; protected disposable files and 29 personal files pass their recorded
checks. A stale .102 sealer configuration correctly refuses the .106 build;
sealing succeeds only after the configuration names the actual payload hash.
The resulting artifact is locally sealed, not a published coordinator checkpoint.

## Qualification boundary

Prepared Creature world, pinned installed content, shared species, empty pack,
exclusive social exchange and developer social controls. The .106 objective
correction and its restore were tested locally; its network repetition remains
NOT RUN. Normal target/ability controls, distinct species, independent objective
sets, simultaneous social encounters, new parts, packs/nests, mating/editor
return, complete respawn and the remaining Creature requirements are open.
See the [complete gate map](../../tests/engine/M11.md).

## Pins and verification

Windows 11 Pro 10.0.26200; Ryzen 7 9700X; RTX 4080 SUPER.
SDK commit `cbf9206b9a823f0911cd9be0217104a49d72380b`.
GOG GA 3.1.0.29 executable SHA-256
`dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`.
Content inventory SHA-256
`06e58e2c169ff380e1fcea2ec519ef2b65653f87f82999ac644d004fdfdf6b3b`.

| Artifact | SHA-256 |
|---|---|
| Development .106 bridge | `ae8110a8737ce3a9fd1cd445a15070d32d5ad25d791b04ed64dd4de5381a8ef5` |
| Development .106 NativeHost | `bd9b99fb3aa7701cc44021ad1a11a9b5b6c9415919fd59818394306f407ecc3c` |
| Trial 21 closed actor trace | `cbf3789e5832adcff1d7ed06cfcc10a30a54be9de845834afbfeba32b0b4c1a0` |
| Trial 22 closed actor trace | `b802fb7f2e18e78c0d3f481a084041c0756fe5ef912b6d2e6d44aae8ea23a280` |
| Trial 21 locally sealed checkpoint | `c2e5779f400f6e1a093ed7cd98b36113faed09ed555629dd7a865fe8e79b9aa1` |

Commands executed in the development checkout:

```powershell
pwsh -NoProfile -File tools/build/build.ps1 -Configuration Release
ctest --test-dir build/win32 -C Release --output-on-failure
python -m unittest discover -s tests/unit -p test_creature_social.py -v
dotnet build src/launcher/SporeMP.Launcher.csproj -c Release --nologo
dotnet run --project tests/launcher/SporeMP.Launcher.Tests.csproj -c Release
node website/build.mjs
node website/check.mjs
```

BUILD/HOST results: all commands exit 0; 19/19 CTest targets (59.18 seconds),
19/19 focused social reducer tests, and 26 launcher assertions. The stricter
objective reducer rejects trial 20 (expected exit 1) and accepts trial 21
(exit 0); the closed-restore review accepts trial 22 (exit 0). These tools do
not by themselves grant native acceptance. Native reproduction requires the
matching private disposable-profile fixture and frozen development payloads;
they are not supplied with the public repository.

Development launcher 0.1.14 describes the new bounded results. The English and
French website article was published in site commit
`bbf37a1e770b4a27819bef8b1465c79feadfdcac`; [deployment](https://github.com/raph559/sporemp-site/actions/runs/36325967670)
completed successfully. All 41 generated live files match locally, 632 static
links/assets pass, and 18 local plus 18 live browser checks cover the home and
article routes, language continuity and 320/390/1440-pixel layouts. The rendered
live English desktop and French mobile articles were inspected. Website and
launcher checks are separate from native gameplay evidence.

Next smallest step: native client target and ability-bar intentions through the
existing authenticated ownership/generation guards, followed by normal social
controls and a network repetition of the delayed objective result.
