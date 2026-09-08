# Global SPORE multiplayer

Deliver a usable Windows multiplayer release of the **original SPORE**, covering Cell → Creature → Tribal → Civilization → Space in **one persistent shared universe**. Players have persistent player/species/faction identities, progress independently, occupy different locations and stages, meet and interact, disconnect, and resume after dedicated-server restarts.

Instrumented original-game processes preserve native gameplay, assets, editors, AI, combat, economy, pathfinding, world generation and progression. A C++ coordinator routes authenticated intentions and observed native outcomes, fences authority, supervises isolated workers and coordinates durable native artifacts. A human client's continued connection is not required for authority to exist.

Independent locations are partitions of the same canonical universe, not renamed private saves. Multiple players meeting must share authority. Applicable mixed-stage consequences and all four native campaign transitions are mandatory. Internet hosting and clean-install usability are part of the release. Galactic Adventures cooperative adventures are an optional extension, never a replacement campaign.

Initial operating assumptions: Windows; one pinned executable/SDK/loader/content profile; rendered workers permitted; invite-based private universe; separate species/factions by default; configurable PvP. Two real clients plus one dedicated original-game worker form the first integration target. Eight clients is a later measured experiment, not a capacity promise.

User amendment (2026-09-08): ship a **Windows desktop launcher** as the normal entry point. M01 delivers its working installation checks, save-backup and diagnostic workspace tools; guarded native startup is part of the same milestone's qualification. Extend that application with worker controls in M04, join/rejoin in M06/M09, and complete Internet hosting, configuration, diagnostics and packaging in M18/M19. A command-line tool alone does not satisfy the launcher requirement. Controls must reflect implemented capabilities and current evidence.

Launcher refinement (2026-09-08): automate as much setup as possible. Find installed copies through store/OS records and Steam libraries, validate them, preserve saves, reuse unchanged verified backups and prepare working folders automatically. Ask the player only for an unresolved choice or missing prerequisite. The main view is a polished game launcher focused on play; advanced paths, checks and diagnostic exports belong in Settings. Development prerequisites are the project's work, not a checklist for the player.

Launcher correction (2026-09-08): the user explicitly rejected mandatory separate Windows profiles and personal-save protection gates in the player flow. Play SPORE must use the normal Windows account and existing saves, with automatic compatibility checks and advanced diagnostics in Settings. Disposable accounts and backups may remain optional developer test tools; they are not player prerequisites. No account-switching or diagnostic-session workflow belongs in normal Play. This amendment supersedes the earlier launcher backup/isolation requirement for the player path while preserving the original brief as historical requirements.

No deterministic replay, safe arbitrary snapshots, concurrent multi-avatar context, headless mode or profile isolation is assumed. Unknown native capabilities remain visible and are investigated without substituting a replacement game.

Completion requires M00–M19 verified against the required native acceptance matrix. Until then this project is **partial/experimental**. Full requirements and acceptance text: `docs/implementation-brief.md`. Current execution state: `STATUS.md`.
