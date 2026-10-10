# M11 relationship snapshot and cadence measurement — 2026-10-07

## Result and scope

Request: measure the cadence of the new test loop on the next M11 increment.
The documented next step was "replicate B's relationship entries live and
qualify a B kill changing its own card". Its second half needs a second kill by
B, which two recorded limits block: the authority's relationship path admits
one first kill per process, and the owner history snapshot holds one entry. This
increment is therefore the first half only.

**Bridge 0.0.189, schema 26: owner B's private species relationships reach B's
own client as a complete live snapshot.** The authority publishes B's first-map
values once per second with a revision; the coordinator routes the message only
to B's ready connection; B's client makes its private copy equal and the
existing goal display re-evaluates. Values only: no relationship action, reward
or history is replayed. Native result in the pinned Creature fixture (restore of
the checkpoint sealed by trial146): trial160 PASS, 58/58 checks.

M11 remains IN_PROGRESS with 0/12 complete gates.

## What trial160 establishes, and what it does not

| Established | Evidence |
|---|---|
| The authority publishes B's one relationship entry as revision 1 | `network_relationship_published`, entries 1, once in the run |
| Only B's client receives and applies it | `network_relationship_applied` once on player 2; none on player 1 or the authority |
| The applied values equal what B's client had adopted from the checkpoint | `native_relationship_snapshot_applied`: `changed` false, `exact` true, hash equal to the display copy's |
| Nothing is earned or replayed | no DNA, award, history or tutorial event on any peer; A 14 DNA, B 7 DNA, both alive |
| B's card is unaffected | Hunt 1/3 presented as in trial149 |

Not established: a relationship that **changes** during play reaching the
client (no such change can be produced yet), a new species entry, more than one
entry, more than eight entries (refused, reported once), reconnect during play,
Save after the message, coordinator restart. Visual review NOT INSPECTED.

## Commands and results

| Command | Exit | Observed |
|---|---|---|
| `pwsh -NoProfile -File tools/build/build.ps1 -Configuration Release` | 0 | 28.7 s; first build of the change, no error or warning |
| `ctest --test-dir build/win32 -C Release` | 0 | 20/20 in 70.9 s, including the new relationship codec and routing checks in `network_host` |
| `native-trial.py run --scenario tests/engine/scenarios/relationship-snapshot.py --name trial158 --port 27158` | 1 | FAIL before the scenario: the runner's fixed two-second liveness check aborted because the authority, launched last, had not started updating. Tooling flaw, fixed in `e8c3918`; all exits 0, fixture undone |
| same, `--name trial159 --port 27159` | 1 | 58/59: every scenario and standard check passed; the one failure was the runner's foreground check, since reclassified as an accepted observation (`6fb9e3c`) |
| same, `--name trial160 --port 27160` | 0 | PASS 58/58, 65.1 s |
| `native-trial.py replay-suite --manifest tests/engine/scenarios/replay-suite.json` | 0 | 16/16 pairs as expected, including trials 159 and 160 (PASS) and the pre-feature trial156 (FAIL) |

Each trial reused one session virtual display; every game and host exited 0;
each fixture journal ended `originals_restored`. The generated record of
trial160 is [RECORD-trial160.md](RECORD-trial160.md).

## Cadence measurement

Wall-clock timestamps taken at each step (UTC, start 02:46:42):

| Minute | Step |
|---|---|
| 0.0 | Start: reading the relationship and history code |
| 5.5 | Code, host tests and scenario written and committed |
| 6.0 | Build done |
| 7.3 | Full host suite done |
| 7.5-8.6 | trial158 (tooling failure) |
| 9.2 | Liveness check fixed, tested, committed |
| 9.4-10.5 | trial159 |
| 11.3 | Foreground rule changed, tested, committed; replay suite run |
| 11.8-12.9 | trial160 (PASS) |
| 13.2 | Verified |

| Measure | Old loop (131 trials, 2026-09-27 to 10-02) | This increment |
|---|---|---|
| Launch to closure | median 194 s | 65 s (three trials: 65.4, 64.5, 65.1) |
| Start of one trial to the next | median 8.1 min | 1.9 and 2.5 min |
| Whole increment | 37.6 min for the last comparable one (two trials, .182-.183) | 13.2 min to a verified native PASS (three trials), before this write-up |
| Desktop control calls, images read | 0 and 11 in that increment | 0 and 0 |
| Game windows and sound on the user's screens | every trial | none |

Limits of this measurement. It is one increment. It followed an existing
pattern (the owner history message) and worked natively on its first real run,
with no engine surprise to investigate; a harder increment will take longer in
the thinking, which no test loop shortens. One of the three trials was wasted
on a runner flaw and one on a runner rule, both found by this first real use.
The old figure of 37.6 minutes included its documentation; the 13.2 minutes here
does not.

## Pins

- Commits `ed01fa2` (feature), `e8c3918`, `6fb9e3c`; base `e22c2eb`.
- SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`; executable SHA-256
  `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`; checkpoint
  artifact SHA-256 prefix `4eb7bb32b89cf9db` (sealed by trial146).
- Windows 11 Pro 10.0.26200, AMD Ryzen 7 9700X, NVIDIA RTX 4080 SUPER, games on
  a Parsec virtual display (driver 0.45), started with the original `noSound` switch.

## Next smallest step

Admit B's second kill on the authority: audit the original relationship branch
taken when the kill count is already 1, and extend the owner history snapshot
beyond one entry. The snapshot channel added here then carries the change to
B's client and its card.
