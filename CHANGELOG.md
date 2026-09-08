# Changelog

These are development builds. Multiplayer gameplay is still in development.

## 0.1.2 — 2026-09-08

### A new home for your universe

- Rebuilt the home screen around the original illustration, a prominent Play button and a separate readiness strip.
- Added permanent Home, What's new and Settings navigation with visible selection and keyboard focus.
- Replaced the release-notes overlay with a searchable version list and a full reading pane. Search includes versions, dates, titles, summaries and changes; unmatched searches have a clear reset action.
- Simplified Settings into installation management, existing-save information and a collapsed troubleshooting section.
- Refined typography, contrast, spacing, button states and scrolling for smaller windows. Button labels now correctly inherit their control's text color.
- Added Ctrl+1 / Ctrl+2 / Ctrl+comma navigation, Ctrl+F search and Escape to return Home. Page transitions respect Windows animation preferences.
- Kept the existing Play behavior and bundled offline notes. After the user requested background work only, the update was built in a separate folder without game launches or desktop control.

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
