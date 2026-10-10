# Changelog

This file describes the launcher source published in this checkout. Later development releases through 0.1.13 and M09/M10 acceptance are summarized in the [development update](evidence/2026-09-27-development-update/SESSION.md); that later code is not included here.

These are development builds. Multiplayer gameplay is still in development.

## 0.1.14 - 2026-09-27

### Social progress, saved for its owner

- Updated What's new with bounded original-game tests for social rewards and the separate reward for completing a species objective.
- The tested First DNA tutorial appears on its owner's client while the peer continues playing. Earned progress and tutorial completion survive an original save/load.
- These prepared-world experiments use developer controls and a shared species. Normal social controls, separate species and the full M11 Creature milestone remain in development.

## 0.1.13 - 2026-09-27

### Explore apart. Meet again.

- Updated What's new with the completed original-game tests for meeting across two prepared Creature locations, returning home, saving and reconnecting after a restart.
- The recorded interrupted-travel tests recovered players without losing or duplicating them across the tested failure points.
- Travel remains a development-tool workflow. Normal Creature gameplay coverage is the next milestone; this notes update adds no player travel controls.

## 0.1.12 - 2026-09-24

### Join the right Creature location

- Join recognizes prepared location invitations and checks the matching original Creature save before starting SPORE.
- Two prepared Creature locations passed original-game connection trials. Travel between them and shared-world saving remain in development.

## 0.1.11 - 2026-09-22

### Resume your saved shared world

- Rejoin confirms when the server has restored the shared save and the native scene is ready in SPORE.
- Recovery currently supports the prepared experimental Creature world. Host restore commands retain protected backups.

## 0.1.10 - 2026-09-22

### Clearer shared-world checks

- Join checks the saved world and its companion world files before opening SPORE. Missing or different files are named so the mismatch can be identified.
- Players and hosts need the same updated development build. The checks do not replace existing saves or download game assets.

## 0.1.9 — 2026-09-14

### Your invitation to a shared universe

- Paste your host’s private invitation on Home to check the server and join its shared scene.
- Rejoin with the same invitation to return as the same player. Invitations stay private and are remembered only while the launcher is open.
- See when the shared scene is connected or the connection is lost. Connection details are available in Settings.
- This is the first experimental shared-scene connection. Full campaign multiplayer and Internet hosting setup are still in development.

## 0.1.8 — 2026-09-13

### Steadier worker sessions

- Live status updates keep the worker list open and preserve your selection.
- Closing the launcher during a worker start or stop lets that request finish safely.
- The diagnostic log created by normal Play no longer prevents a later worker or game launch.

## 0.1.7 — 2026-09-13

### Worker startup stays responsive

- Starting a worker now releases the busy state so its status and Stop control become available while SPORE continues running.

## 0.1.6 — 2026-09-13

### More reliable worker controls

- Stop stays attached to the selected worker session if that worker restarts while the request is being sent.
- A worker with unreadable settings no longer hides the status or controls of another healthy worker.
- A worker that fails to answer a shutdown request is stopped after the timeout, and the forced stop is recorded.

## 0.1.5 — 2026-09-13

### Worker window guidance

- Worker Settings now explains that minimizing a worker pauses its simulation and can cause it to time out. Keep worker windows open and unminimized.

## 0.1.4 — 2026-09-13

### Local worker controls

- Select, start and stop a prepared isolated worker from Settings. A worker opens a separate SPORE window on the current Windows desktop.
- Read live status for startup, scene readiness, pauses and crashes, with automatic refresh while Settings is open.
- Keep Play available when the only running SPORE processes belong to recognized isolated workers.
- Preserve worker diagnostics when the game crashes or gets stuck during startup. Full multiplayer gameplay and checkpoint recovery remain in development.

## 0.1.3 — 2026-09-08

### Your screen, your settings

- Choose Fullscreen or Windowed and a resolution directly in Settings > Display.
- Use Desktop resolution to match your primary display automatically at each launch.
- Saved display choices are applied every time you launch SPORE. Switch back to Use game settings whenever you prefer.
- A display shortcut on Home shows your saved choice. Changes made while playing take effect on your next launch.

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
