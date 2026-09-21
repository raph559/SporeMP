# M04 worker supervision — 2026-09-12 / 2026-09-13

**Historical result: M04 IN_PROGRESS. Evidence: BUILD, HOST/FIXTURE and one failed NATIVE startup.** Bridge 0.0.13 and launcher 0.1.4 introduced bounded scalar IPC, process supervision, two prepared OS-profile fixtures and Settings controls. Checkpoint load, native concurrency/isolation and real player independence were not accepted.

The complete session, literal commands, raw diagnostics, media and source/artifact manifests remain in the [private historical archive](../../docs/public-evidence.md#historical-artifacts) at `evidence/2026-09-12-m04-workers/`. This condensation adds no native acceptance.

## Implementation and checks

The supervisor used authenticated local pipes, generation/sequence fences, bounded queues, OS process/job identity and explicit ready/stall/crash/forced-stop states. Native work was dispatched on the app-update thread. Separate standard accounts had real mutually protected profiles; directory/environment changes were not treated as isolation proof. The original multipleInstances flag was statically identified. Load/save operations still returned unavailable.

Release build passed. Final worker HOST tests passed 173 assertions; launcher passed 30 assertions; affected worker/launcher Python checks passed 9/23 tests. Real token access probes denied personal/game/peer mutations. An offscreen actual WPF preview verified binding/layout against synthetic service state, not native worker readiness.

An initial pipe cleanup timeout exposed waiting on OVERLAPPED operations that had never become pending. Waiting only on outstanding operations fixed bounded teardown; tests covered controller closure, peer survival, job containment and forced stop. Other retained failures included compiler conversion warning under /WX, a PowerShell interpolation parser error, runner argument binding and a UTF-8 comparison false alarm. No acceptance clause changed.

## Original failure and remaining gate

The one original game started on a private desktop that was never activated. It displayed renderer error **1001**, produced no bridge/actor trace, and remained at zero progress. The supervisor initialization timeout terminated only its job: game exit 35, wrapper exit 31, expected 0. All 29 personal hashes remained unchanged.

The failed run lacked a complete pre-run source snapshot and failed before module enumeration. A staged candidate payload and later final source archive are not represented as observed loaded code. The next experiment was a prepared worker on the current signed-in rendered desktop. Headless/service, lock/disconnect, private desktop, RDP/VM and minimized support remained unqualified.

Pins: SDK `cbf9206b9a823f0911cd9be0217104a49d72380b`, injector `26adca9a2578b5bb32ba2eac90d96bd9ac7d48a9`, GOG GA 3.1.0.29 executable SHA-256 `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`. Windows 11 build 26200, Ryzen 7 9700X/RTX 4080 SUPER, MSVC 14.44.35207, Windows SDK 10.0.26100.0.

Two HOST children were not two original workers. Unattended original actions, actual player closure, concurrent native writes, checkpoint recovery and native shutdown remained NOT RUN.
