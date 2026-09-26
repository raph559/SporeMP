# M11 first increment and supporting-material refresh

Date: 2026-09-27. M11: **IN_PROGRESS**. New native gameplay: **NOT RUN**.
This session implements a read-only trace reducer and performs a source/static
binding audit. It also refreshes the website, launcher notes and project docs
after M10. No game process was launched and no gameplay hook was changed.

## Supporting deliverables

- The English/French website now includes M09 checkpoint recovery, M10 meeting,
  return and recovery news, development releases through 0.1.13, and the
  Creature/Cell/campaign roadmap. Existing donations and article routes remain.
- Website commit `82d26fea8edc8b300495d7bde198daddffc42c8f` deployed successfully
  in [run 36277009768](https://github.com/raph559/sporemp-site/actions/runs/36277009768).
  Both live homepages, the English M10 article and its French language-switch
  destination were inspected. Original browser captures stay private.
- Launcher 0.1.13 refreshes offline notes only. Its version, embedded notes and
  changelog agree. Location invitation/recovery behavior remains that of the
  preceding implementation. No new travel controls or public package are added.
- Public documentation commit `a157518b29f7299f8744e3896cb1d70b192221c1` passed
  [CI run 36277011380](https://github.com/raph559/SporeMP/actions/runs/36277011380).
  It explicitly distinguishes M09/M10 development acceptance from the public
  M08 implementation. Exporting the later native code still requires separate
  source review and qualification; no private history was merged or pushed.
- The development README, launcher instructions, architecture, authority and
  multiplayer policy summaries are refreshed. Historical native reports keep
  their original date and scope. The 11 Creature coverage rows now cite actual
  partial foundations; all other stages and the complete M00–M20 plan remain.

## M11 findings and implementation

Normal network input currently maps W/A/S/D and Space. The command protocol has
no social/pack/mating command. B reward scopes cover tested combat and feeding;
the alternate-world update explicitly requires a shared native species profile.
These are concrete gaps for normal independent Creature gameplay.

The selected x86 audit found candidate social reward caller `D9BBD0`, including
true passed to `C042A0` and the original DNA call returning to RVA `0x99bd1b`.
This remains STATIC evidence. The new `analyze-creature-social.py` uses existing
closed actor traces; it does not add a hook, call a game function, infer an owner
or upgrade a call's immediate DNA delta into a completed award.

See the [contract](../../docs/m11-creature.md) and
[12-gate acceptance map](../../tests/engine/M11.md). First native step: record
one ordinary primary-player social interaction in a verified isolated profile,
correlate visible relationship/progression with the candidate trace pair, then
trace the initiating owner before attempting B's social path. That test needs
current desktop permission and verified protected-save preparation. It is NOT
RUN; a tool or static-analysis pass cannot satisfy it.

## Executed validation

| Exact command | Expected / observed |
|---|---|
| `node website/build.mjs` | 0 / 0; EN/FR homepages, 14 localized articles. |
| `node website/check.mjs` | 0 / 0; 31 files and 388 local references in the development site build. |
| `pwsh -NoProfile -File tools/build/build-launcher.ps1 -Configuration Release` | 0 / 0; launcher and HOST test builds, zero warnings/errors. |
| `ctest --test-dir build/win32 -C Release -R '^launcher_host$' --output-on-failure` | 0 / 0; 1/1 in 4.92 s. |
| `pwsh -NoProfile -File tools/native/invoke-static-audit.ps1 -RunKey m11-social-reward-20260927 -ReuseDatabase -Addresses C042A0` | 0 / 0; completed static export, pinned executable checked. |
| `pwsh -NoProfile -File tools/native/invoke-static-audit.ps1 -RunKey m11-social-caller-20260927 -ReuseDatabase -Addresses D9BBD0` | 0 / 0; completed static export. |
| `python -m unittest discover -s tests/unit -p test_creature_social.py -v` | 0 / 0; 5/5 HOST/FIXTURE tests, including ten invalid-evidence variants. |
| `python tools/native/analyze-creature-social.py evidence/2026-09-12-m03-awards/award-01/native/actors-26936.jsonl --output local/m11-2026-09-27/retained-combat-review.json` | 0 / 0; structurally valid retained combat trace, zero candidate social calls. |
| `python tools/native/analyze-creature-social.py evidence/2026-09-12-m03-awards/award-01/native/actors-26936.jsonl --output local/m11-2026-09-27/retained-combat-required-social.json --require-social` | 1 / 1; the combat-only trace cannot satisfy the requested social observation. |

Public-checkout commands were run from `local/public-preparation/public-export`:
stage the explicit report/allowlist and reviewed docs; `python tools/check-public-evidence.py`
passed 93 reviewed files / 894572 bytes; `node website/build.mjs` and
`node website/check.mjs` exited 0 (29 files / 358 references in that separately
maintained site mirror); `git diff --cached --check` exited 0. The focused commit
and `git push origin main` exited 0. Required CI then passed as linked above.
Website publication used `python local/website-publish.py push 'Update M09 and M10 news and the Creature gameplay roadmap'`,
exit 0, copying only five reviewed website files to the independent site repo.

Reports, source inventories and raw build output for this session are private
under `local/m11-2026-09-27/`. Static exports and wrapper command metadata are
under `local/m03-static/m11-social-*-20260927*`. The retained combat input remains
archived; it is not a new game session or a public trace download.

## Provenance

| Identity | Recorded value |
|---|---|
| SDK, checked from the local dependency | `cbf9206b9a823f0911cd9be0217104a49d72380b` |
| Executable SHA-256, checked by both static wrappers | `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37` |
| Retained M10 content identity; no content loaded this session | `06e58e2c169ff380e1fcea2ec519ef2b65653f87f82999ac644d004fdfdf6b3b` |
| Native bridge/host baseline | 0.0.86; unchanged by this increment |
| New launcher EXE SHA-256 | `daa7b88c267a9b834cf32c92fd743921048484d09a5991bffdbf3927b5a04464` |
| Social reducer source SHA-256 | `888f8f5dc796722ac6d4066831ffd78954e1c5655813858c64481a1847452853` |
| Reducer tests SHA-256 | `549f3cb7b1203682c95c864af35e19f83beeb20d2c8f6e5c7a4a4fdd7c01fa94` |
| Reward export index SHA-256 | `e676482409b391989dfc9c854050f8f046bff3c037dfefe8c49cf40c9cce8b08` |
| Caller export index SHA-256 | `cc844e2dc37de526670112088def9ec455082cc5f90fa21466bf6f025b5925c7` |
| Retained combat trace SHA-256 | `90c534414a890cfe555e0b6e9bfb40611293bf438b49f89ab19dbcb3dd38f39b` |
| OS/hardware checked this session | Windows 11 Pro 10.0.26200; Ryzen 7 9700X; GeForce RTX 4080 SUPER (also AMD integrated and Parsec virtual adapters). |

Static exports used pinned Ghidra 12.1.3 and Temurin 21.0.12.1+1. Their command
metadata retains the analysis script hash and exact arguments. No native input,
runtime social ABI qualification, new relationship result, species ownership
or M11 gameplay acceptance is established by these checks.
