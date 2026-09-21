# Native tooling and private evidence

These are developer tools. Build and HOST/FIXTURE tests never start SPORE.
Native control, capture and measurement commands require the compatibility,
process ownership and disposable-profile prerequisites in
[testing](../../docs/testing.md) and [compatibility](../../docs/compatibility.md).
Keep generated reports, logs, screenshots, personal inventories and source
artifacts under ignored `local/`. Publish only reviewed summaries following
[the public evidence policy](../../docs/public-evidence.md).

## Preserved analysis and reproduction tools

Reusable tools previously mixed into dated evidence directories now live here.
They accept explicit inputs and fresh output paths. Moving a historical analyzer
does not make it compatible with every newer trace schema or grant new native
acceptance. Source edits, personal paths and failed run data embedded in one-off
historical scripts remain in the private archive.

| Tool | Scope and inputs |
|---|---|
| `analyze-m03-probes.py` | Read the historical `probe-NN/native`, per-probe provenance and fixture layout. `--input-dir`, `--runs` and `--output` select the local archive and fresh report. |
| `analyze-m04.py` | Correlate pinned bridge 0.0.14 traces with payload provenance and optional action/checkpoint reports. Run `--help` for explicit paths. |
| `analyze-m05-presentation.py` | Summarize closed `native-*/archive.json` runs, optional `timing-*` captures and optional private media. Video metadata needs `ffprobe`; no media tool runs without `--media-dir`. |
| `analyze-m05-ability.py` | Extend the same closed archive analysis with recorded ability, denial and personal-after observations; uses the presentation analyzer without running its CLI. |
| `analyze-m06.py` | Verify hash-bound M06 harness archives, native action chains, poses and reconnects. `--self-test` uses synthetic HOST fixtures only. |
| `generate-award-prefixes.py` | Read the exact pinned, locally owned executable and regenerate the reviewed award prefix include. It neither loads nor edits the game. |
| `audit-m03-ai-registration.py`, `audit-m03-factory-tables.py`, `audit-m03-nest-binding.py`, `audit-m03-reward-bindings.py` | Preserve read-only pinned PE pointer/table/binding checks. Each requires `--executable` and a fresh `--output`; static observations remain distinct from native acceptance. |
| `adopt-loaded-actors.py` | Explicitly adopt actors in an already loaded worker using `--sidecar`; never launch, load, spawn or retry. This is a native mutation command, not an offline analyzer. |
| `save-workers-concurrently.py` | Explicitly request one native save per already running worker 01/02 and observe bounded overlap. This mutates disposable worker saves and is never part of automated tests. |
| `measure-frames.ps1` | Measure an explicitly selected original-game PID using the existing pinned PresentMon 2.5.1 executable. Output defaults to `local/native-timing/RunKey`. No download or game launch is performed. |

Run from the repository root; use fresh local names:

```powershell
python tools/native/analyze-m06.py --self-test
python tools/native/analyze-m05-ability.py --input-dir local/m05-archive --output local/m05-analysis.json
python tools/native/analyze-m05-presentation.py --input-dir local/m05-archive --output local/m05-presentation.json
python tools/native/generate-award-prefixes.py --executable C:/Games/SPORE/SporebinEP1/SporeApp.exe --check
```

The prefix generator requires executable SHA-256
`dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`.
`--check` compares all generated instructions and relocation masks against
`src/bridge/native_award_prefixes.inc` without writing files. Without `--check`,
the default outputs are `local/native-prefixes/native_award_prefixes.inc` and
`local/native-prefixes/binding-audit.json`. It refuses existing output files;
review any generated difference before changing source. Normal builds need
neither the generator nor a local game installation for this include.

The M05 readers report selected observations and retain `NOT_VERIFIED` or
`NOT_INFERRED_BY_ANALYZER` acceptance fields. Trace reports cannot replace
independent input provenance, visible gameplay review, audio listening or
milestone acceptance. Historical originals and their old path conventions are
retained privately; no raw archive is needed to run the synthetic unit tests.
