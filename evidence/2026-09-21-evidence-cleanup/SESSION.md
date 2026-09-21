# Curated public evidence

Date: 2026-09-21. Scope: repository maintenance and HOST/FIXTURE tooling. Native gameplay: **NOT RUN**; no milestone acceptance is changed.

The public checkout previously tracked 3,144 evidence files totaling 817,419,568 bytes, including 331 JSONL traces totaling 801,396,030 bytes. Ordinary builds and HOST tests do not require these historical raw records; the clean-build exporter already excludes `evidence/`.

## Preservation and curation

Before removal, the complete public history at `ba270b26fe08a5138985db6ca11ac7b6313c7eb0` was saved in a verified Git bundle and pushed to the private archive branch `archive/public-evidence-20260921`. The bundle SHA-256 is `f3aafca01af573066bdee4e7180e70fc7bc5f1682968e789234154fd603f2ecb`. The remote backup was checked against the original public head and the archive's private visibility was confirmed. Original unredacted development evidence remains in its existing private archive too.

Public Git retains concise Markdown reports, the small M03 acceptance decision and the reviewed Detours migration provenance. Long sessions are condensed without converting failures into passes. Archived-artifact links explicitly describe the private boundary. The [evidence index](../README.md) links the principal acceptance reports. Full traces, repeated snapshots, build logs, account/ACL inventories and one-off run outputs remain privately recoverable.

Reusable analyzers, measurement tooling and the native-award prefix generator move to `tools/native/` with explicit input/output paths. Their fixture checks are separate from native evidence. The compiled gameplay implementation is unchanged; the generated prefix include has only a provenance-comment path update.

`docs/public-evidence-files.txt` lists every retained report. `tools/check-public-evidence.py` and CI reject unlisted files, raw file extensions, individual reports above 64 KiB and a total above 2 MiB. These publication checks do not assess native acceptance.

## History and development boundary

Only the independently reviewed public history is filtered. Its `main` branch was the sole public branch, with no forks, open pull requests or tags at preparation. The full before-state is privately backed up. The private development checkout, M07 work and native payloads are not rewritten. Existing public clones must preserve local work and use a fresh clone; merging an old branch would restore archived objects.

Exact command logs and before/after inventories are retained under ignored `local/evidence-cleanup/` and the adjacent private cleanup workspace. No original game, save, binary or new gameplay claim is published by this cleanup.

## Validation

| Command or check | Expected | Observed |
|---|---|---|
| `git bundle create ../evidence-cleanup/public-before-cleanup.bundle --all` and `git bundle verify` | Preserve complete public history before any removal | Both exit 0; bundle complete and verified. |
| Private archive branch push and authenticated visibility/head check | Private remote backup matches the original public head | Exit 0; backup head matches and archive remains private. |
| `cmake --build build/win32 --config Release --parallel 4` | Build without historical evidence inputs | Exit 0 after raw evidence removal. SDK Detours provenance passes. |
| `ctest --test-dir build/win32 -C Release --output-on-failure -V` | Existing HOST suite and new tooling fixtures pass | Exit 0, **10/10 targets**, **136 Python tests**, 14.83 seconds. No game started. |
| `python tools/native/analyze-m06.py --self-test` | Relocated analyzer retains its fixture behavior | Exit 0, 19 HOST checks. |
| `python tools/native/m06-launcher-evidence.py --self-test` | Local capture-path change retains parsing/validation behavior | Exit 0, 7 HOST checks. |
| `python tools/native/generate-award-prefixes.py --executable C:/Games/SPORE/SporebinEP1/SporeApp.exe --check` | Read-only regeneration matches committed source | Exit 0, all 17 prefixes and 6 relocation words match; executable is never loaded. |
| Migrated tool `--help`, Python AST and PowerShell parser checks | Tools remain loadable after relocation | All pass. Four pinned static PE readers also exit 0 without loading the game. |
| `node website/build.mjs` and `node website/check.mjs` | Website remains valid | Both exit 0; 21 public files and 222 local references validated. |
| `python tools/check-public-evidence.py` after staging | Only reviewed compact reports are tracked | Exit 0; 48 files, less than 260 KiB in total. |
| Markdown local-file link scan | No links point to removed files | Exit 0; 91 Markdown files and 419 local links checked, no missing target. 144 historical-artifact links now identify the archive boundary. |
| `git diff --cached --check` | Staged changes contain no whitespace errors | Exit 0. |
| Fresh local clone, pinned `git-filter-repo` path filtering, final-tree comparison and `git fsck --full --strict` | Remove archived paths throughout public history while preserving the reviewed final tree | All exit 0; final tree matches exactly, 48 evidence paths across all 11 retained commits, only `main` remains. Filtered pack is 2,822,807 bytes (2.69 MiB excluding its index); complete Git object storage is 2.71 MiB, previously 27.34 MiB. |

Environment: Windows NT 10.0.26200.0, AMD Ryzen 7 9700X, pinned Win32 MSVC tool directory 14.44.35207 and Windows SDK 10.0.26100.0. SDK revision is `cbf9206b9a823f0911cd9be0217104a49d72380b`; Launcher Kit/Detours source is `26adca9a2578b5bb32ba2eac90d96bd9ac7d48a9`. The static generator accepts only executable SHA-256 `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`. No native compatibility or content qualification is added by these maintenance checks. The earlier [Detours report](../2026-09-21-detours4-license/SESSION.md) retains its build-artifact identities and open native gate.

History preparation used `git clone --no-local --single-branch --branch main PUBLIC_CHECKOUT FILTERED_CHECKOUT`, followed by `python git-filter-repo.py --path-regex "^(?!evidence/)" --paths-from-file filter-keep-paths.txt`. The path file contains the 48 reviewed report paths. The pinned filter script SHA-256 is `67447413e273fc76809289111748870b6f6072f08b17efe94863a92d810b7d94`. The filtered code/report tree matched the pre-filter cleanup tree `5e706f7a726d71b60f935c5359b811e0e01f0c8c` before this verification paragraph was added. Only the public branch is eligible for the guarded publication; its prior expected head is `ba270b26fe08a5138985db6ca11ac7b6313c7eb0`. Branch protections must be restored immediately after the guarded update.
