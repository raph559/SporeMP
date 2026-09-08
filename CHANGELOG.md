# Changelog

These are development builds. Multiplayer gameplay is still in development.

## 0.1.1 — 2026-09-08

### Your updates, in one place

- Added a latest-update card to the launcher's home screen.
- Added **What's new** with the complete release history, versions, dates and readable change lists.
- Bundled release notes with the launcher so they remain available offline.
- Created the project's first local Git commit, covering the existing M00/M01 work and this launcher update.

## 0.1.0 — 2026-09-08

### A simpler way to launch SPORE

- Added automatic detection of supported SPORE installations.
- Added **Play SPORE**, using the normal Windows account and existing game saves.
- Added automatic compatibility checks before native startup.
- Redesigned the home screen and moved installation options and local support reports into Settings.
- Completed M01 using native lifecycle/guard evidence and user acceptance of the simplified player flow.

Player-facing notes are maintained in src/launcher/Content/release-notes.json and embedded in the executable. Add new entries at the top when shipping a player-visible update; keep the launcher project version and this history aligned. Entries describe completed changes, not planned multiplayer features.
