# Working contract

This is the public source checkout. Keep private raw logs, captures, credentials and account inventories under ignored `local/`; publish only reviewed evidence exports. Read `docs/public-evidence.md` before adding evidence. Historical public reports have documented privacy redactions; their original hashes refer to the private archive. New code, documentation and commit messages use English, except explicit visitor locale files.

Read `GOAL.md`, `STATUS.md`, `MILESTONES.md`, then the documents relevant to the change. The user-adopted brief is preserved verbatim in `docs/implementation-brief.md`; it is requirements/reference material, not evidence that any proposed capability works.

## Product and scope

- Deliver GLOBAL multiplayer for original SPORE: Cell, Creature, Tribal, Civilization and Space in one persistent universe. Independent progression, locations, encounters, dedicated authority and reconnects are mandatory. Galactic Adventures multiplayer is optional M20.
- Original instrumented SPORE processes execute native gameplay. The C++ coordinator handles identity, transport, ownership, supervision and persistence metadata. Do not replace native AI, combat, economy, pathfinding, generation or progression without a separate explicit scope decision from the user.
- Preserve user work and personal saves. Implement one reviewable milestone increment at a time; begin with M00/M01. Do not build empty subsystem skeletons to imply completion.

## Engineering and evidence

- Never invent SDK methods, signatures, offsets, launch flags, headless support or successful tests. Record each binding's pinned source, ABI, executable identity, thread/context requirements and runtime evidence.
- Keep engine dependencies in `src/bridge`. Serialize IDs and values across processes; never pointers or native containers. One fenced authority per mutable resource; clients submit intentions.
- Keep DllMain minimal. Use verified SDK callbacks for initialization/disposal. No blocking work, networking, game manipulation or thread joins under loader lock. Audit unavoidable SDK lifecycle work and document it.
- Reject unknown executable/content/loader configurations before unsafe bindings. An inventory hash is a candidate, not native compatibility verification.
- Never launch two game processes into a shared live profile. Environment variable redirection alone does not prove isolation. Back up and hash personal saves before any native probe; require a verified disposable OS profile/VM or measured safe path isolation.
- Milestone statuses: TODO, IN_PROGRESS, IMPLEMENTED_NOT_RUN, VERIFIED, BLOCKED. Native acceptance requires real original-game execution. Compilation, host harnesses and synthetic fixtures must be labeled separately. Record unavailable tests as NOT RUN and state the prerequisite.
- Every session records exact commands, exit codes, expected versus observed results, source/artifact hashes, SDK commit, executable/content fingerprints, OS/hardware, evidence paths and the next smallest step. Never commit game binaries, saves, secrets or third-party assets.
- A native blocker needs a minimal reproduction, actual/expected behavior, missing capability and next experiment. Continue independent legitimate work without silently reducing scope.
- Run the appropriate executable tests after implementation, update status, and keep the full M00–M20 acceptance plan intact.

## Initial workflow

For player-visible launcher updates, update src/launcher/Content/release-notes.json and CHANGELOG.md, newest first, and keep the launcher project version aligned. Notes appear in the launcher and must describe implemented changes in player-friendly language. Keep developer diagnostics out of the normal Play flow.

Use PowerShell on Windows. Build and test commands are maintained in `docs/testing.md`; compatibility and isolation gates are in `docs/compatibility.md`. Fetch only pinned dependencies through the build tooling. Generated dependencies/builds/local profiles are excluded from Git. Do not auto-install a DLL into the personal game or start native gameplay from a build/unit-test command.
