# Contributing to SporeMP

SporeMP is experimental. Shared Creature movement and reconnects have been demonstrated in a bounded original-game scene; the complete multiplayer campaign is unfinished. Read [GOAL.md](GOAL.md), [STATUS.md](STATUS.md), [MILESTONES.md](MILESTONES.md) and [AGENTS.md](AGENTS.md) before changing behavior. The milestone plan describes required work, not a list of available features.

## Choosing a change

Use an issue to describe a reproducible bug or a concrete proposal. Check existing issues and the milestone plan first. For a large feature or a change to native gameplay ownership, discuss the approach before implementing it. Keep pull requests focused on one reviewable increment and explain the player-visible result.

Write code, comments, documentation, issue reports and pull requests in English. Website translations belong in `website/locales/`; English remains the default. Preserve the original game's gameplay and keep engine dependencies inside `src/bridge`. Do not introduce a replacement simulation or assume an unverified SDK method, ABI, address, launch flag or headless mode.

## Development and checks

The native bridge and launcher target Windows. Exact toolchain versions, dependency pins and build commands are in [README.md](README.md), [docs/testing.md](docs/testing.md) and `config/dependencies.lock.json`. Fetch dependencies through the build tooling. Do not commit downloaded dependencies or generated builds.

The Python HOST/FIXTURE suite can run on Windows with Python 3.11 and PowerShell 7 without installing or launching SPORE:

```powershell
python -m unittest discover -s tests/unit -p 'test_*.py' -v
```

The website can be checked independently with Node.js 22:

```powershell
node website/build.mjs
node website/check.mjs
```

The **Host and website checks** workflow runs those checks on GitHub. Its Windows job covers Python tooling, synthetic inputs and host operations; its website job generates and checks the static site. It does not build the C++ bridge or WPF launcher, launch SPORE, exercise real multiplayer, or qualify a native milestone. Website publishing is managed by the separate [website repository](https://github.com/raph559/sporemp-site).

For native or launcher changes, run the affected executable tests from [docs/testing.md](docs/testing.md) after rebuilding. Record exact commands and outcomes. Mark unavailable checks **NOT RUN** and state the missing prerequisite. A successful build or fixture test is not original-game evidence. Do not rerun a completed native scenario unless the change, a failure or an unresolved concern calls for it.

## Native experiments and evidence

Read [docs/compatibility.md](docs/compatibility.md) and the relevant protocol under `tests/engine/` before a native experiment. Preserve personal saves. Never launch two game processes into the same live profile; environment-variable redirection alone does not prove isolation. Multi-process developer experiments require the documented disposable-profile or measured-isolation prerequisites. These developer requirements do not change the normal player's Play flow.

Keep evidence classes explicit: **BUILD**, **HOST**, **FIXTURE/MOCK**, **NATIVE** and **NETWORK-REAL**. Preserve failed experiments and their limitations. Use a fresh evidence path and record the source revision, relevant artifact hashes, pinned dependencies, tested configuration, commands, exit codes, expected and observed behavior, and next step. Do not mark a milestone verified from compilation, synthetic inputs or an analyzer result alone.

## Before opening a pull request

- Explain the problem, resulting behavior and remaining limitations.
- Include the relevant check results, separating host fixtures from original-game execution.
- Update the affected documentation and status when the evidence boundary changes; retain the complete milestone plan.
- For player-visible launcher changes, update `src/launcher/Content/release-notes.json` and `CHANGELOG.md`, newest first, and align the launcher project version.
- Review every staged file. Exclude personal saves, game binaries/assets, secrets, session invitations, certificates with private keys, build output and local diagnostic exports.

Issues and pull requests are public. Share only the smallest relevant error excerpt after removing credentials, invitation URLs, account identifiers, private addresses and personal paths. Do not upload saves, memory dumps or complete diagnostic archives. For a suspected vulnerability or exposed credential, use the repository's private **Security → Report a vulnerability** route when available; do not publish exploit details or secrets in a public issue.
