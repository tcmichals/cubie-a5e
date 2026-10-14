# Instructions for AI assistants

This file is the single entry point for every AI tool used on this project (Claude Code,
Gemini / Antigravity, Cursor, ...). Tool-specific files (`CLAUDE.md`, `.cursorrules`,
`.agents/rules/`) only point here or hold detail; the rules themselves live in the project
documents below, which people read too. Do not copy rules into tool-specific files.

## Read before changing code, bindings, docs or tests

1. **[upstream-remoteproc/DESIGN_DECISIONS.md](upstream-remoteproc/DESIGN_DECISIONS.md)** —
   the owner's decisions (DRAM firmware, memory-region layout, shared remap register,
   KUnit, A733 placeholders, workflow). A review that disagrees does not change them:
   raise it with the owner.
2. **[upstream-remoteproc/UPSTREAM_LESSONS_LEARNED.md](upstream-remoteproc/UPSTREAM_LESSONS_LEARNED.md)** —
   what kernel maintainers and review bots told us, and the pre-submission checklist.
3. **[TODO.md](TODO.md)** — the only task tracker. The v3 fix tracker is §5.6a. Do not
   create other TODO files.

## Detailed rules

The files in [.agents/rules/](.agents/rules/) are plain Markdown and apply to every tool:
kernel branch discipline, upstream patch submission, kernel review prompt, device tree,
RISC-V firmware C++, testing, git identity, USB bring-up.

## Project overview

[README.md](README.md) and [.cursorrules](.cursorrules) describe the repo layout and build.

## Ground rules

- Work happens on more than one PC: commit and push `linux-cubie` (`cubie-linux-7.1`) and
  `cubie-a5e` (`main`) when a piece of work is done.
- Never push upstream submission branches or send patches to a mailing list without the
  owner's explicit go-ahead.
- Develop and compile-check in `linux-cubie`. `bld.a5e/` and `bld.a7a/` are Buildroot
  output, for builds only: never edit or run `make` inside them (see DESIGN_DECISIONS.md).
- Hardware facts come from silicon readbacks or the vendor BSP, not from project docs alone.
- Commits must never include `Co-Authored-By` trailers for AI assistants or any AI metadata. All commits must strictly use `Tim Michals <tcmichals@gmail.com>`.
