# M04 checkpoint implementation — 2026-09-13

**Historical result: M04 IN_PROGRESS. Evidence: BUILD, HOST/FIXTURE and STATIC analysis. New original-game execution: NOT RUN.** Bridge 0.0.14 and launcher 0.1.6 implemented a concrete next native experiment; prior native runs were not attributed to the new binaries.

The complete session, literal commands, raw diagnostics, media and source/artifact manifests remain in the [private historical archive](../../docs/public-evidence.md#historical-artifacts) at `evidence/2026-09-13-m04-completion/`. This condensation adds no native acceptance.

## Implemented behavior

Original save and saved-game UI load candidates were guarded by exact executable/manager identity, engine thread, scene/epoch and the controlled Satiria fixture. Load completion required a new Creature epoch, matching home name and subsequent original AI progress; acknowledgement alone was insufficient.

A pre-serialization snapshot required idle A/B, one original player and no pending bridge/reward context. Restore enumerated existing native nouns and checked IDs, species/archetype, herds, uniqueness and original-avatar identity. It created zero replacement nouns by design, but that behavior was not yet native-tested. Original loading removes non-primary players, so complete B progression persistence was not claimed.

Closed checkpoint sealing tied native save/trace identity to preserved file hashes. Restore pinned a new generation, loaded/adopted once, and required native restore evidence. Unknown mutation outcomes were not retried. Supervisor shutdown gained a bounded escalation when acknowledgement never arrived. Launcher Stop pinned the selected generation before status lookup and could not be redirected by replaced metadata.

## Executed checks and missing acceptance

Release native builds and launcher build exited 0. Four focused HOST targets passed in 2.54 seconds: 184 worker, 15 actor-command, 34 ABI and 30 launcher assertions. Python passed 96 tests, including 16 checkpoint and 12 manager tests. ABI fixtures exercised exact signatures, receivers and repeated stack balance in HOST executables; they did not call original game functions.

An initial runner invocation failed argument binding before building; direct PowerShell array invocation corrected it. Static partial exports and missing optional tooling remained recorded without runtime claims.

Pins: SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, injector `26adca9a2578b5bb32ba2eac90d96bd9ac7d48a9`, GOG GA 3.1.0.29 executable SHA-256 `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`. Windows 11 build 26200, Ryzen 7 9700X/RTX 4080 SUPER.

No game, visible launcher, desktop input, personal-save mutation or native checkpoint was performed. Native load/save, zero-created-noun adoption, launcher/player closure and concurrent save isolation were the next required checks. The [subsequent acceptance session](../2026-09-13-m04-acceptance/SESSION.md) records their results. Static ABI/lifetime detail remains in the adjacent audit Markdown files.
