# C++ conventions

These apply to `src/` and `tests/`. They follow the C++ Core Guidelines where those fit code that hooks a 2008
32-bit game; where the game's ABI forces something else, the exception is local and commented.

## Tooling

| Check | Command | Holds for |
|---|---|---|
| Format | `pwsh -NoProfile -File tools/build/format.ps1` (`-Check` to verify, `-Files a.cpp,b.h` for some) | all C++ |
| Warnings | `/W4 /WX` in every target; `tools/build/check-file.ps1 <file>` compiles one file without a build tree | all C++ |
| Lint | `.clang-tidy`, for the sources that do not include the game SDK (clang cannot parse it) | `src/network`, `src/worker`, pure-logic bridge files |

Formatting is never done by hand. Inline assembly is the one thing the formatter breaks: fence it with
`// clang-format off` and `// clang-format on`, and read the stack pointer in tests with
`SPOREMP_READ_STACK_POINTER` (`tests/unit/stack_pointer.h`).

## Functions

- One job per function, and a name that says it. A function longer than about sixty lines, or with a comment
  heading each block, is several functions.
- No repeated blocks: a second copy becomes a helper.
- Helpers private to a file live in its anonymous namespace. A header declares only what other files use.
- Return early on failure; keep the main path at one indentation level where possible.
- Rules that need no game (admission, packing, timing) are `constexpr` functions in a header and have host tests.

## Names

- Full words: `entity`, `packet`, `connection`, `animal`, not `e`, `p`, `c`, `a`. Loop counters and the two ends of
  a comparison may stay short.
- A parameter has the same name in the declaration and the definition.
- A name says what the thing is in the game, not where it lives in memory. An offset gets a named constant that
  says what is read there and where that was established.

## The game boundary

- Never invent an SDK method, signature or offset. A raw offset or address carries a comment or constant naming
  the static export or trial that established it, and is checked against pinned bytes before it is used.
- `reinterpret_cast` and pointer arithmetic appear only in the function that implements one named read or write of
  game memory; callers use that function.
- Engine dependencies stay in `src/bridge`. Nothing in `src/network` or `src/worker` includes an SDK header.
- A hook keeps the original's calling convention and calls the original exactly once unless its comment says why not.

## State and types

- File-scope mutable state is declared together at the top of the file's anonymous namespace, with a comment on
  what owns it and which thread touches it. Prefer a struct that groups related state over loose globals.
- `enum class` over integer codes; `constexpr` over `#define`; `std::array` over C arrays for owned storage.
- Initialise every variable where it is declared. No owning raw pointers in project code.
- Values that cross processes are IDs and scalars, never pointers or native containers.

## Diagnostics

- An event row is written by one function per event, which owns its field list. Event and field names are part of
  the trial records' schema: renaming one is a behaviour change and needs the scenarios updated in the same commit.
- A diagnostic added for one investigation is removed, or kept with a cap and a comment, when the investigation ends.

## Comments

Say why, and what was measured. Keep the trial or static export a fact came from. Do not narrate what the next
line does.

## Refactoring safely

A cleanup changes no behaviour. The host tests and the replay suite must pass unchanged, and bridge changes are run
through the gate scenarios in `tests/engine/scenarios/` before they are relied on. Keep the order of calls into the
game, every event name and field, every constant, and every public signature.
