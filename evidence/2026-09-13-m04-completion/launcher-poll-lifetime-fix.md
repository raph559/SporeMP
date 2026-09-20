# Launcher worker polling and operation lifetime correction

Recorded 2026-09-13T01:28:24Z. Source change plus BUILD / HOST-FIXTURE validation. This subtask opened no UI, launched no original game, rebuilt no native bridge, and did not replace the launcher Release output being used by the parent session.

## Trigger and resulting behavior

The parent session's actual launcher test found that the three-second worker poll disabled the worker selector and cleared/recreated its items. Disabling the selector collapsed an open dropdown; replacing items and preserving the selection captured before awaiting also overwrote a selection made while the poll was running.

`WorkerSettingsModel` now separates background refresh from explicit mutation state. Polling leaves selection enabled. Existing worker IDs retain the same observable item objects; only actual roster additions/removals change the collection. Updated rows notify changed properties. When a read completes, it preserves the current selection rather than the selection that existed before the await.

A single semaphore serializes backend reads and mutations. An explicit Start/Stop captures its target once and can wait for an existing poll. It immediately suppresses another mutation. Window-lifetime cancellation cannot discard that queued explicit operation. The backend still checks fresh native worker state before starting or stopping anything.

`LauncherClient` accepts cancellation before launching a worker operation, then lets an already-submitted Start/Stop backend finish independently. It does not recursively kill that backend or its authority descendants on window cancellation, malformed output, or timeout. Other preparation/read-only diagnostic operations retain their existing cancellation cleanup. The worker backend saves its result before its final stdout report; losing that pipe therefore cannot interrupt the actual operation before its result is saved.

The frontend wait for a dispatched worker operation is bounded to three minutes. A timeout reports a pending/unknown result and leaves the backend running, with no retry. Existing backend start/control deadlines remain in force. On a normal close while Start/Stop is pending, `MainWindow` hides immediately and keeps its dispatcher alive until the pending operation finishes or its frontend deadline expires. This also covers a Start queued behind a poll when the window closes. It then closes the hidden window. The three-minute timeout path was source-reviewed, not elapsed-time tested.

## Changed sources

- `src/launcher/WorkerSettingsModel.cs`: stable worker choice objects, latest-selection reconciliation, separate refresh/mutation state, serialized backend operations and observable pending-mutation task.
- `src/launcher/LauncherClient.cs`: worker-specific cancellation/lifetime boundary and bounded frontend wait without process-tree termination.
- `src/launcher/MainWindow.xaml.cs`: hidden completion of pending explicit worker operation before final window closure.
- `tests/launcher/Program.cs`: actual child-process fixtures and observable-collection regressions.

Version, release notes and changelog are owned by parent integration and were not edited by this subtask. Bridge 0.0.14 binaries were not changed.

## Exact validation commands and observed results

All commands ran from the repository root in PowerShell.

| Command | Expected / observed exit | Observed result |
|---|---|---|
| `dotnet build tests/launcher/SporeMP.Launcher.Tests.csproj -c Release -o build/launcher-tests/m04-poll-fix --nologo -m:1` | 0 / 0 | Build succeeded, zero warnings/errors; MSBuild elapsed 1.40s. |
| `& build/launcher-tests/m04-poll-fix/SporeMP.Launcher.Tests.exe build/launcher/Release/launcher.runtime.json` | 0 / 0 | 39 launcher HOST assertions passed; native tests NOT RUN. Command wall time 3.613s. |
| `dotnet build src/launcher/SporeMP.Launcher.csproj -c Release -o build/launcher/m04-poll-fix --nologo -m:1` | 0 / 0 | WPF build succeeded, zero warnings/errors; MSBuild elapsed 0.71s. |

The added regressions verify that a held poll keeps selection/actions enabled; a selection changed mid-poll and all existing item references survive without collection changes; a queued Start waits for the read and executes once despite window cancellation; an already-started Stop completes after cancellation; a pre-cancelled request starts no child; and malformed worker output cannot kill the backend.

One fixture starts a separate launcher-test process, begins an actual Python worker-operation fixture, and exits that launcher process while the backend is in flight. The original parent test then observes the backend's completed marker after the launcher process has exited. This establishes OS child lifetime for the tested fixture, not original-game or live WPF acceptance. Every fixture uses a temporary service script that has no game launch code. Existing diagnostic cancellation/malformed-response cleanup checks still pass.

No actual dropdown was opened or inspected by this subtask. Native launcher-control reacceptance belongs to the parent session after integrating its version/release-note update.

After these checks, parent review found that the final stderr drain also needed the total operation deadline. The final source now uses `await stderr.WaitAsync(readToken)`. This one-line correction is included in parent .1.8 integration; the custom build/test artifacts below precede it. Final LauncherClient.cs SHA-256 is `a1f883576e8003e3ba6986d7a40b5ed4b56c0c61a2a09376e154cc21b3e98902`. This subtask did not repeat a build after that correction because parent .1.8 compilation/checks were beginning.

## Handoff hashes

SHA-256 at the recorded handoff; later parent version/resource integration may change built artifacts.

| Source/artifact | SHA-256 |
|---|---|
| WorkerSettingsModel.cs | `69b90b650cc5210f74192455b6f0eb4dabfbdc2198e52a0cb642344d5620e15d` |
| LauncherClient.cs | `7ac75b23cc3a86196fa5a987339f42e99c42d2ebc8e5d03ca31a5f4b6e4ed201` |
| MainWindow.xaml.cs | `bfecc69d14c50a359c1dcb43e696b7c0e9df088b6383b8160bec364d2fb38715` |
| tests/launcher/Program.cs | `ff6350cb1f8f5b8c4610ed3a1283286bc3923c2d752401a9ce5cd188a3de2bfa` |
| build/launcher/m04-poll-fix/SporeMP.exe | `af1db15ab87cdb09db698edd7a549788056056af80c52041e5b47f46f3fb640` |
| build/launcher/m04-poll-fix/SporeMP.dll | `bfc9309e0f1bf4c22a934edd32a02a09dce413f3d6aab57f2f7df587b4829680` |
| build/launcher-tests/m04-poll-fix/SporeMP.Launcher.Tests.exe | `a671ab44b6f4eff9231c212295dd5deea476517d77fe65228f91bd0cbef95883` |
| build/launcher-tests/m04-poll-fix/SporeMP.Launcher.Tests.dll | `c56abb0b11e16512104174bcf229c46608ff436d9676a0d97826ef18de0dbf4b` |
