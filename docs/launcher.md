# Windows launcher

The normal player flow is **open SporeMP → Play SPORE**. Automatic discovery and compatibility checks lead directly to the installed original game under the player's normal Windows account, using existing saves. The user rejected mandatory separate profiles and backup gates, then confirmed the simplified launcher works on 2026-09-08.

The main view pairs the original space illustration with one prominent Play button, a separate readiness strip and the latest release. A permanent sidebar switches between Home, What's new and Settings. Installation selection, paths, errors and local report export stay in Settings. Multiplayer is explicitly still in development.

Version 0.1.2 gives **What's new** a searchable release list and a full reading pane. Search matches versions, dates, titles, summaries and individual changes, regardless of case; multiple words can match different parts of the same release. The installed version is marked in the list. A search with no matches offers Clear search, and Read update from Home always opens the latest release. The notes are embedded from src/launcher/Content/release-notes.json and available offline; they describe updates included with the installed launcher, not a live remote update service. CHANGELOG.md preserves the same release history in Git.

Settings groups installation selection, existing-save information and build information, with troubleshooting collapsed by default. Ctrl+1 opens Home, Ctrl+2 opens What's new, Ctrl+comma opens Settings, Ctrl+F opens and focuses release search, and Escape returns Home. Controls have visible keyboard focus. Pages scroll as needed on smaller displays; initial size is limited to the available work area, and short page fades respect Windows animation preferences.

## Implementation and state

The WPF window runs launcher_service.py with literal argument arrays and streamed progress. Discovery reads EA/GOG/installed-app records and Steam libraries, normalizes/deduplicates paths and remembers the selected installation. Missing or ambiguous installations request only the needed selection.

Preparation checks game files and required artifacts. It does not provision accounts, copy personal saves or prepare another profile. Play stages project DLLs under local/launcher/native-payload, then invokes SporeMP.NativeHost.exe --play with the game root, payload directory and fresh run directory. The host validates compiled fingerprints before process creation/injection and does not redirect Windows profile folders.

Closing the launcher during play hides its window while the existing operation collects game exit, then closes it. This behavior is implemented; the user's confirmation establishes the overall working Play flow, not a separately observed test of every window action.

The executable is build/launcher/Release/SporeMP.exe. The development runtime uses the source checkout, installed Python, .NET SDK 8.0.418 and .NET/WindowsDesktop 8.0.24. global.json pins the SDK; external NuGet sources are disabled. Moving the EXE alone is not the M19 distribution package.

For the 0.1.2 redesign, a ready-to-open build is staged at build/launcher/0.1.2/SporeMP.exe with its runtime configuration. After the user requested background work only, the running launcher was left alone while they played. The normal build command continues to write Release; the staged version is a separate output of the same project. Background compilation and a small check of the compiled release-note model passed; the redesigned window has not been opened or visually verified. Evidence: ../evidence/2026-09-08-launcher-redesign/SESSION.md.

Settings, staged project DLLs, run reports and export ZIPs stay under ignored local/launcher. Export includes only the selected report and a readme; no saves, game assets, credentials or automatic upload. Old backup/profile tools remain optional developer tools.

M01 is VERIFIED using the completed native/guard work and final user acceptance. See ../evidence/2026-09-08-m01-native/SESSION.md. Older launcher evidence describes earlier flows. Artwork provenance remains in ../src/launcher/Assets/README.md.
