# Live transfer 06 — paired Save, published reload, return and player reconnect

Date: 2026-09-26. Frozen runtime: **0.0.83**, build **083b**. Classification: **NATIVE / NETWORK-REAL**, with independent **HOST closed-artifact analysis**.

Outward transfer **8306** completed at phase **15 / revision 13**. The Haagos authority then performed an original Save containing both real native players, continued native AI, and closed cleanly. Its reviewed whole-world checkpoint was published at revision **14**. A fresh original Haagos process loaded that checkpoint and adopted the existing visitor without creating duplicate nouns or replaying rewards. Return **8307** serialized the current visitor, restored the matching original Satiria primary, retired the visitor on Haagos and completed at phase **15 / revision 26 / avatar generation 6**. A fresh player replica reconnected on Satiria and drove original movement, stop, jump and landing.

All five original game processes and their hosts exited **0**. Their closed traces contain no native network quarantine or quiescence rejection. Original and published profile staging journals both show complete restoration and protected-personal checks passing. This closes the previous run's unexecuted return-client reconnect and adds a bounded paired Save/publication/reload path. It does **not** complete M10: native phase faults, native global producer exclusivity and the additional gates below remain separate work.

## Frozen fixture and build

The fresh coordinator used loopback port **27184**, original **30-file** input manifest `be1e22a513a63af45fbd35da15ab6308cb1a595fc5370caa622d47a94feb7e79`, initial checkpoint revision **2** and durable revision **3**. All four global leases initially belonged to Haagos. Haagos worker **12/generation 9** connected before Satiria worker **11/generation 5**. The published Haagos reload used **12/generation 10**; return home used **11/generation 6**.

The retained build command `pwsh -NoProfile -File tools/build/build.ps1 -Configuration Release` exited **0**, with no source drift against manifest `source-build-083b.json`. Full `ctest --test-dir build/win32 -C Release --output-on-failure` exited **0**, **18/18 passed in 58.65 seconds**. These are BUILD/HOST/FIXTURE results, distinct from the actual original processes below.

| Frozen member | SHA-256 |
|---|---|
| Bridge | `0146a940286cebd026007bf09befc74891e0a82bbc2a9d7fb18ffe274a4072a8` |
| NativeHost | `96a4aabe851249a834f25fd813248bf4472ed4b58c523ebae78d6fdf68a2351a` |
| WorkerControl | `751c03e5e07ca899bd5f97007261ee57ecb7938c4416525355c7ceec57d791f0` |
| Coordinator | `326202e8ebe2dd57c3a3ae3969a0092e8c6b9a7227c984eee3b376291f9a83ec` |
| SDK DLL | `d703c587fd5f629c435a7978fe8b826abc626e1f2e783f8cd3e488b4433bd55e` |
| Injector | `122b3a07df6746f29d89c2e47ead55ab5f8bb97dc5c378cdb376e9f34034056b` |
| Original source/build manifest | `4f06ed055338e29b46ee04706923140097e56d692d03da6249a531300df815d9` |

Original executable SHA-256 is `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`; pinned SDK commit is `cbf9206b9a823f0911cd9be0217104a49d72380b`. Loader/content/OS/hardware provenance follows [GRAPH.md (archived)](../../docs/public-evidence.md#historical-artifacts) and the frozen source/build record. Later source changes are not evidence for this `.83` execution.

| Original process | Game / host PID | Engine thread | Complete native rows | Actual closed observation UTC |
|---|---|---:|---:|---|
| Outward Satiria | 17536 / 21676 | 34516 | 2,495 | 14:53:40.7095357 |
| Haagos owner and original Save | 28176 / 33240 | 14308 | 7,300 | 14:53:48.8064620 |
| Fresh published Haagos | 2364 / 35836 | 36472 | 16,978 | 15:03:26.3545152 |
| Fresh return Satiria | 35268 / 8264 | 34648 | 22,969 | 15:03:25.4909180 |
| Returned player replica | 33908 / 34164 | 26904 | 3,227 | 15:03:24.5241768 |

Each trace has contiguous sequence numbers, one engine thread, zero foreign callbacks and healthy detach with status 0. Host records and retained process handles agree on all game/host exits 0. The coordinator also closed cleanly. Each of the two profile journals restored all six disposable roots; protected-personal checks passed for all three worker stages.

## Outward original transfer

Source **8306** selected native player **1**, avatar **232**, slot **1**, unique game ID **684349579**, canonical home **9519123**. The original measured pause was **3,000 ms / 184 zero deltas**, AI **2688 → 2688**. Its **367 before/after census rows** match exactly. Native serialization produced a closed **3,777-byte schema-3 artifact**, containing the exact three-object player/avatar/collection graph and original persistent stage.

Haagos original read created visitor player **3552**, herd **3553**, avatar **3554**, preserving resident player **1**, avatar **259**, herd **233**. The destination measured **3,000 ms / 159 zero deltas**, AI **61776 → 61776**, after native presentation settling. Activation released pause **1 → 0** and observed AI **61776 → 61795**. Source closure and coordinator cleanup then completed phase **15 / revision 13**, avatar generation **3**.

| Artifact | Outward 8306 SHA-256 | Current return 8307 SHA-256 |
|---|---|---|
| Original schema-3 DBPF | `1cf7482f1eac4c419a652a0fcd772c1939547d46922ac00e85562b4c41f65216` | `d23c66bdd166a5fc7036373ba792b24351f5e60fc757814fe2c249c50e894f4b` |
| 4,368-byte M10F approval | `1b02c6b54d0571b0492842a7231a81946b5ec291857f59850ba7a225e2b51703` | `78d4e30e441c87e7d255966e3bc06b00a2e431f3f3952b973671a075febe1a25` |
| Source native witness | `2073ae8da1239bf76f0e3584338885262dff0a986ee25bf5c6508789cd0aa5fc` | `dcff454417ff274fe1e57bdd897d03569f3e667aa1272f066f3786dc146bcd9b` |

Closed analysis independently reran the public artifact validator, recomputed both approvals byte-for-byte and compared each source, coordinator and destination artifact copy. Retained command hashes match their receipts. These checks supplement the actual authenticated native receipts; a command acknowledgement alone is not counted as native completion.

## Original paired Save and exact closed player state

The next Save command was accepted at epoch **3**, persistence request **1**. Original Save entered at sequence **5828**, returned success at **5932**, elapsed **516 ms**, and checkpoint completion reported success at **6666**, `diagnostic_only:false`. The original Save return is explicitly `durable:false`: later coordinator publication is a distinct step.

The imported, before-Save, after-Save, fresh-published-load and current-return-export censuses contain **the same 732 rows**. The closed original `Haagos.spo` is **186,097 bytes**, SHA-256 `02ae8f7bf7150ba90ede5b966c1386457b90e9e86ca708754a86bac8e74bc3a6`. Independent DBPF/ClassSerializer decoding finds **38 classes, 3,266 objects and exactly two cPlayers**. Both saved players' four progression values, full sorted item-status maps, ordered unlocked-item lists and unlock points match the same-request native census:

| Native player | Canonical role | Unique game ID | Statuses | Unlocked items |
|---|---|---:|---:|---:|
| 1 | Haagos resident 2 | 685579644 | 312 | 52 |
| 3552 | Satiria visitor 1 | 684349579 | 312 | 53 |

The native semantic publication validator additionally checks original actor/herd/species identity, resident stage, visitor extension stage, queues and pending grant arrays. Its approval was independently regenerated from the actual closed owner profile and trace. Source Satiria `.spo` remains byte-identical, SHA-256 `ca504370ceaa0cf7b21489ab1870b4e586f99e258523953606a3cd3e41d4a0fb`.

After Save, the authority remained `ready` and network `connected` at baseline 2, with both actors present. Native AI advanced **63201 → 69462** before closure. The post-Save capture's two-second frame shows the unobstructed original purple Haagos scene, creatures at a nest, water and normal HUD. The unsupported pooled-NPC capture rejection observed in [trial 04 (archived)](../../docs/public-evidence.md#historical-artifacts) did not recur in this run.

## Whole-world publication and retry boundary

The closed owner profile supplied the entire selected checkpoint; no independently edited global archive or per-player archive merge was substituted. Candidate size is **57,930,330 bytes**, SHA-256 `afaf1d14eb1bcbbf0d0ceebf63cbba703ea971ded313ceaf42b4f65b6e8880bf`. The retained **464-byte M10G approval** and proof were independently rebuilt byte-for-byte:

| Publication member | SHA-256 |
|---|---|
| Semantic proof | `bc324d14f61a5009482af0f2fa061fc67d3b36ba243c12e0e45c6acba1eafa66` |
| M10G approval | `e6a172167b1a54e3317b0ac8bb61b6012f10b5e36c49f0f371cd20a8414ac85d` |
| Exact publication request | `0d63d00d8a71a96c12f8943cda6fec5806a95d0f01ebec6179c380023cf6b7a4` |
| Successful receipt | `ff5c7c3474494edb7cd01722e9ae78b8b3d0b493993a90fed118061e26b380de` |
| Full 31-file published profile manifest | `db2d8fe02c4563d2c5fccb18aa84030aeb14a3b8b13a462c16944d71afcfcc0b` |

The coordinator accepted expected revision **13**, publishing durable/checkpoint revision **14**. Its receipt correctly has `native_reader_qualified:false` at publication time: the fresh original reader ran afterwards.

The exact request was retried in the same coordinator and after coordinator restart. Both retained receipts are byte-identical to the original revision-14 receipt. The runtime retry helper compared all **15 durable state files** before and after each retry and reported them unchanged. **Separate before/after file tables were not retained.** The closed audit independently verifies request and receipt identity, and retains the helper's 15-file equality assertion as runtime evidence; it does not claim a second independent historical filesystem comparison.

The independent native global audit also follows one actual resource consequence: owner callback sequence **58** initialized planet **21950529** from zero to spice **[480253286, 0, 0]**. That value matches the closed original record, complete published archive and fresh Haagos reader at observation marks **5/6/16**. The reader's **32 AssignSpice calls** left sampled values unchanged, with no observed selected-star regeneration. This is a verified original initialization consequence and fresh adoption, not proof of periodic economy/diplomacy execution or exclusive native global producers.

## Fresh published original reload

The exact **31-file** published world was staged through a new reversible journal before original Haagos PID **2364** launched. At sequence **279**, `checkpoint_restore` reports `adopted_existing_nouns`, actors **1/2**, **created_nouns:0**. Award restore reports visitor **3552**, **replayed_rewards:0**; pickup-grant replay count is **0**. The schema-3 extension restore succeeded and load completion reported `identities_restored:true`, elapsed **2,172 ms**, native AI **0 → 19**.

Loaded native players remain **1 and 3552**. All **732 census rows** match the original Save exactly. This independently demonstrates preserved progression and collection state across the original saved reload and no duplicated visitor. Zero reward replay is a restoration observation; this run did not earn a new reward or test a new progression grant.

## Current visitor return and source retirement

Return **8307** used checkpoint **14**, the published candidate hash, current avatar generation **4**, source worker **12/generation 10**, and fresh home worker **11/generation 6**. Maintenance preparation advanced revision **14 → 15**. It selected current native slot **2**, player **3552**, avatar **3554**, unique game ID **684349579**, canonical home **9519123**.

The current-source pause measured **3,016 ms / 159 zero deltas**, AI **104603 → 104603**. It produced a new **3,778-byte artifact**, different from the outward artifact. All **732 before/after source rows** match. The ten original persistent stage words match their native export events and approval; the outward artifact was not reused for return.

At home sequence **1850**, original primary restore reused player **1**, avatar **232**, herd **208**, with **zero created nouns**, three reused factory classes, exactly one native player, preserved noun IDs, primary binding and global color, and applied source stage. The native guards compare all four progression bit patterns, every item-status tuple, ordered unlocked items and unlock points before success. `.83` does not emit an additional full cPlayer census immediately after this primary overlay, so independent offline comparison of that extra census is unavailable; the actual native equality guards, stage application and subsequent observations remain the evidence.

Home then measured **3,000 ms / 183 zero deltas**, AI **96 → 96**. Activation released pause **1 → 0** and observed AI **96 → 128**. On Haagos, original visitor retirement removed exactly **three nouns**, leaving **366 resident census rows** identical to the original resident. Cleanup witness SHA-256 is `446110616addec8234316f78801fd94273b89ea292f2cc8546cabe4f35654ae9`; it reports cleared visitor roots, zero remaining imports and resumed native AI. Coordinator completion reached **phase 15 / revision 26 / avatar generation 6**. Both original authorities remained healthy until clean closure.

## Returned replica input and recording limits

The fresh returned player replica joined Satiria as canonical player **1**, baseline **1**, checkpoint **14**, at sequence **754**. Its controlled network entity **12884902120** maps to home native avatar **232**, owner/actor **1**. Exactly three replica intentions were issued: request **1 move**, **2 stop**, **3 jump**.

| Original home evidence | Native sequence |
|---|---:|
| Movement command begins | 18113 |
| Stop command begins | 18388 |
| Jump command begins | 18420 |
| Original jump accepted return | 18422 |
| Original landing, owner/actor 1, jump command 3 | 18589 |

Landing occurred **736.6207 ms** after the original jump return. The accepted network reply is not the only evidence: the authority's original command, jump return and landing establish actual execution.

The twelve-second WGC recording was requested at **15:02:51.8045205 UTC** and completed at **15:03:04.2480420 UTC**. Host commands were sent at **15:03:03.675** (move), **15:03:04.764** (stop) and **15:03:04.887** (jump). Stop and jump therefore occurred **after capture completion**. The inspected four-second frame shows the unobstructed original green Satiria scene, native creatures and normal HUD. **The recording is baseline visual evidence; it is not evidence of a filmed jump or landing.** The native action result above is separately supported by the trace.

## Reproduction and evidence integrity

Private artifacts are retained under `local/m10-native-2026-09-26/live-transfer-06/`; no saves, game assets, credentials or raw traces are exported in this report. The retained helpers enforce the frozen runtime and profile manifests. Native launch order was Haagos, then Satiria; the fresh published stage, home maintenance stage and returned player stage used their separate checked configurations.

Verified command bodies and outcome records include:

```text
pwsh -NoProfile -File local/m10-native-2026-09-26/build-083b.ps1
pwsh -NoProfile -File local/m10-native-2026-09-26/live-transfer-06/complete-save-close.ps1
python local/m10-native-2026-09-26/live-transfer-06/published-profiles.py observer --worker 02
python tools/native/native-global-observer.py --spec local/m10-native-2026-09-26/live-transfer-06/published-profiles/owner-reader-02-spec.json --output local/m10-native-2026-09-26/live-transfer-06/published-profiles/owner-reader-02-analysis.json --confirmed-closed
python local/m10-native-state-2026-09-26/global-producers/audit-trial06-publication.py
python local/m10-native-2026-09-26/save-format/analyze_live_transfer06.py
```

The Save orchestration runs `transfer.py --transfer-id 8306 --source-start <start-01.json> --destination-start <start-02.json>`, closes source 01, repeats the same controller with `--cleanup`, sends frozen WorkerControl `save 3`, waits for the matching native completion, captures game **28176** for five seconds, checks continuing AI, closes owner 02 and restores profiles. The returned replica controls used its recorded generation/host with `move 3 1 1 0`, `stop 3 1 1`, `jump 3 1 1`. Original process shutdowns used `shutdown 3`; command acceptance and actual retained-handle exits were both checked. All listed analysis/build commands exited 0.

The executing operator additionally confirms these trial-local top-level commands: `python seal-global-publication.py --confirmed-closed`, `python publish-global.py`, `python retry-global-publication.py --label same-process` and `--label after-restart`, `python published-profiles.py prepare` and `restore`, `launch-published-stage.ps1 -Stage published`, `home` and `player`, `return-after-publication.py prepare` and `run`, `close-published-stage.ps1 -Suffix 03-return`, `01-return` and `02-published`, and `stop-restarted-coordinator.ps1`. Where no complete top-level invocation ledger was retained, additional arguments are **not independently reconstructed**. The checked helper bodies, per-operation records and native results establish the execution described above; this report does not invent missing flags.

The player analyzer reuses only the pinned trial-05 transfer-validation function, not that script's top-level execution. Its sole path adjustment distinguishes the immutable pre-restart admin directory from current return admin files. It independently regenerates both approvals and checks the saved original archive, all five closed runs, lifecycle, input correlation, restoration and video timestamps. The global audit's sole path alias locates the unchanged original completed receipt in that immutable pre-restart directory; the pinned receipt hash remains unchanged.

| Reviewed private result / script | SHA-256 |
|---|---|
| Trial-06 player analyzer | `b2eabb3c0a8b35179c59779a59968d5c9f887c95ae3f347e582c64e84a65584b` |
| Trial-06 player analysis JSON | `dc681d5f3ca33fbfc4a678fdfc712c80295d27bf26ee82174b8212afafca499f` |
| Pinned reused trial-05 function source | `3284081d2b3dc031d8a6f54bf724fd6870ce52e498937e45d8c51b5b1b9b9f30` |
| Independent global publication analysis | `35d4bc7d4bf86a6f3f9da553b18f49d5eff7adb94b8c53441a0dd357ff1e0146` |
| Fresh global reader analysis | `db9775072b66ab81b1e08127a3887a9da152fe2a48d00b19c4bf75e816e46b4a` |
| Five-second owner Save recording | `08c35400ad7af786168b5cb728b8fc2519c3a1eed1124dec7706a252374d1a4b` |
| Inspected owner frame, two seconds | `10c5d5e93224b979f56b345a889618fb8f406bf84858ed175cdc54a5cccde50c` |
| Returned player recording | `0a375f41cc8d6637b7e9a9a85837947bbf7cb51f5dbed854d26f4b25bf0beeee` |
| Inspected returned frame, four seconds | `4a7e66a72144ad38f99f7936083d1867fc248d2df5c65e21a7f4022cde2a26fa` |

Closed original native trace hashes, ordered as the five-process table above:

```text
2e53592eb7c05123d0cc37b26904ac0e488557da31e4874b3da942893d5041aa
9df12f032c51c4cd8b533ad3d64061a056be51617ab58b513ce904184553a9b6
5c88ef97350a901a1fe90d5a8f3ef877e4069069a4599921cdeae49f60d0d44d
099cb2ce669e99c82be8a61065579102103266d01b650d191fd981e84a00cfe5
e0e13e29a6ea18b0fa38ccf8843d6b0aede11b4956eebb225baab7851ae58a6a
```

## Acceptance boundary and next checks

- **Verified in this bounded native run:** outward ownership transfer; original paired Save; exact player/progression/collection preservation; durable whole-world publication; same-request retry and restart receipt behavior; fresh native adoption without duplicate nouns or reward replay; fresh current visitor return; original source visitor retirement; returned replica input and original landing; clean lifecycle and profile restoration.
- **NOT RUN here:** native transfer-phase fault matrix, fresh reward earning, long-duration progression, all stages, and new lost-process recovery after this return.
- **Native global exclusivity remains unqualified.** The eight global observers pass through; independently loaded authorities can execute initialization before authentication. The selected native spice consequence and readback do not establish one producer for every global mutable resource. Preload authority permission and pre-mutation rejection/revocation evidence are required next.
- **Post-return saved-resident recovery needs a separate guard.** The earlier published Haagos checkpoint contains the visitor who has now returned. Ordinary recovery from that head must retire stale, unleased secondary state before claiming a healthy resident-only world. This run did not kill/reload that retired-source configuration.
- A full independent post-primary-overlay player census and retained before/after retry file tables would improve the noted evidence limits; existing native equality guards and runtime retry checks are not silently replaced by assumptions.

M10 remains **IN_PROGRESS**. These results extend [trial 05 (archived)](../../docs/public-evidence.md#historical-artifacts), [the native format and import evidence (archived)](../../docs/public-evidence.md#historical-artifacts) and [the earlier original global reader (archived)](../../docs/public-evidence.md#historical-artifacts); they do not substitute for the remaining milestone gates.
