# Launcher changelog and Git history — 2026-09-08

Request: establish a Git repository if needed and show the latest changelogs in the player launcher.

The repository was already initialized on main with no commits and no remote. This increment creates the first local commit containing the existing M00/M01 source, documentation and evidence plus launcher 0.1.1. Generated builds, downloaded dependencies, local profiles, backups and credentials remain ignored. Only original launcher artwork and prior launcher UI evidence images are included as image files.

The home screen now includes a latest-update card. What's new opens a scrollable full history with version/date, summary and categorized changes, plus Back to Play. Close/outside click/Escape also dismiss it. Notes are embedded from src/launcher/Content/release-notes.json and available offline; the app does not fetch or invent remote release information. The launcher project version is 0.1.1. CHANGELOG.md and the project working contract describe how future player-visible updates maintain the notes.

Verification was deliberately limited to this change: dotnet build src/launcher/SporeMP.Launcher.csproj -c Release -o build/launcher/Release --nologo returned 0, with zero warnings/errors (build.log). No full regression suite or native game run was repeated. The revised UI was not re-exercised through Computer Use after the earlier user interruption; the compiled artifact is ready for the next normal launcher opening.

Git staging was inspected before commit for generated/private artifact paths and executable/save/credential formats. git add staged project files only; no game assets, binaries, saves or credentials were staged. Initial source/history commit is local; no remote was configured or push performed.

Source/build fingerprints are recorded in files.json. Existing SDK commit cbf9206b9a823f0911cd9be0217104a49d72380b, loader commit 26adca9a2578b5bb32ba2eac90d96bd9ac7d48a9, game/content fingerprints and environment remain referenced by ../2026-09-08-m01-native/environment.json and native-lifecycles.json. No native bindings or launch behavior changed in this increment. M01 remains VERIFIED; M02 remains the next milestone.
