# Windows launcher

The normal player flow is **open SporeMP → Play SPORE**. Automatic discovery and compatibility checks lead directly to the installed original game under the player's normal Windows account, using existing saves. The user rejected mandatory separate profiles and backup gates, then confirmed the simplified launcher works on 2026-09-08.

The main view keeps the original space illustration, compact readiness status and one Play button. Installation selection, paths, errors and local report export stay in Settings. Multiplayer is explicitly still in development.

Version 0.1.1 adds a latest-update card on the home screen and a **What's new** panel with the full release history. Each entry shows its version, date, summary and changes. Players can return with Back to Play, the close button, an outside click or Escape. The notes are embedded from src/launcher/Content/release-notes.json and available offline; they describe updates included with the installed launcher, not a live remote update service. CHANGELOG.md preserves the same release history in Git.

## Implementation and state

The WPF window runs launcher_service.py with literal argument arrays and streamed progress. Discovery reads EA/GOG/installed-app records and Steam libraries, normalizes/deduplicates paths and remembers the selected installation. Missing or ambiguous installations request only the needed selection.

Preparation checks game files and required artifacts. It does not provision accounts, copy personal saves or prepare another profile. Play stages project DLLs under local/launcher/native-payload, then invokes SporeMP.NativeHost.exe --play with the game root, payload directory and fresh run directory. The host validates compiled fingerprints before process creation/injection and does not redirect Windows profile folders.

Closing the launcher during play hides its window while the existing operation collects game exit, then closes it. This behavior is implemented; the user's confirmation establishes the overall working Play flow, not a separately observed test of every window action.

The executable is build/launcher/Release/SporeMP.exe. The development runtime uses the source checkout, installed Python, .NET SDK 8.0.418 and .NET/WindowsDesktop 8.0.24. global.json pins the SDK; external NuGet sources are disabled. Moving the EXE alone is not the M19 distribution package.

Settings, staged project DLLs, run reports and export ZIPs stay under ignored local/launcher. Export includes only the selected report and a readme; no saves, game assets, credentials or automatic upload. Old backup/profile tools remain optional developer tools.

M01 is VERIFIED using the completed native/guard work and final user acceptance. See ../evidence/2026-09-08-m01-native/SESSION.md. Older launcher evidence describes earlier flows. Artwork provenance remains in ../src/launcher/Assets/README.md.
