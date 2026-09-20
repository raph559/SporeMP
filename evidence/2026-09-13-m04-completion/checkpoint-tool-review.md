# Checkpoint tool provenance review

Recorded 2026-09-13T00:54:33Z. SOURCE_REVIEW and HOST/FIXTURE evidence only. This subtask launched no game and performed no desktop input.

Reviewed and corrected `tools/native/worker-checkpoint.py`, then added `tests/unit/test_worker_checkpoint.py`.

The original draft checked fingerprint shape and native-file equality, but did not tie an edited fingerprint pair back to the source save trace. Its actor filtering also silently discarded malformed extra list entries. Snapshot request/epoch/owner matching could accept Python booleans as integers. Closed traces did not require the same recorded thread or a healthy final native stop. Restore success used worker status alone rather than requiring the explicit native adoption result.

The corrected tool:

- Pins source generation, fixed run directory and game PID; hashes the same source trace bytes it validates; requires the original final successful save, preceding complete snapshot and matching source epoch/request/actor fingerprints.
- Rejects missing, duplicate and malformed actors, invalid integer widths, boolean identities, crossed scene epochs and snapshots captured after their save.
- Requires contiguous native trace sequence, exact PID/thread, zero foreign callbacks and a healthy final detach for closed source evidence.
- Pins the backup-manifest SHA-256, verifies its single source is the expected Games directory and verifies both live native files and backup payload against the sealed manifest.
- Requires the destination `checkpoint_restore` event to report `adopted_existing_nouns`, `created_nouns=0`, and the same epoch/A/B IDs as current worker status.
- Accepts request 0 on legitimate status replies, while mutations require a nonzero request. An unknown load outcome is not retried or reported as restored.

The final focused command was:

```powershell
python -m unittest discover -s tests/unit -p test_worker_checkpoint.py -v
```

Expected exit 0, observed exit 0. All **16 HOST/FIXTURE tests passed** in unittest-reported 0.209 seconds. The preceding test increment passed 15 tests in 0.200 seconds before the explicit timeout/no-retry case and source-read hash check were added. No native execution is implied by these fixtures. The final command's wall time was 0.422 seconds. These timings come from actual tool output; the timestamp above is when this result was recorded, not an invented test-start timestamp.

At handoff, `worker-checkpoint.py` SHA-256 is `7d0839c4d011e8305f5447e658c195e1a64f04f20d272c2489268dfa17b8f953`; `test_worker_checkpoint.py` is `7b3c9f6e053b75dee2d8dd185eca840c931a5750818603210e8d83774ea61524`. Later parent integration may update the tool and must retain its final hashes separately.

New sidecars include `game_pid` and `backup_manifest_sha256`; seal must use this corrected script. The tool does not upgrade old sidecars by trusting their claims.

Remaining provenance boundary: sidecar metadata itself includes executable SHA-256, SDK commit and bridge version, but not separate native content/injector/SDK-core/bridge-DLL hashes. Those remain in the existing guarded-host run evidence and host compatibility gate. Bridge version alone is not a binary hash. The actor checkpoint remains a bounded M04 slice, not complete B progression/inventory persistence or a transactional durable-acknowledgement contract.
