# Contributing

Start with [Building](docs/BUILDING.md), [Architecture](docs/ARCHITECTURE.md)
and [Testing](docs/TESTING.md). Discuss large features or movement/rendering
changes in an issue before rewriting a subsystem.

## Make a focused change

1. Create a short-lived branch from `main`.
2. Identify the concrete failure or maintenance problem and the contracts
   that must survive the change.
3. Edit project-owned adapters or checked preparation scripts. Do not patch
   the immutable provider in place or silently update its hashes.
4. Run the affected checks; run portable CTest and the repository check for
   general source changes. State required private inputs and skipped checks.
5. Update the relevant maintained guide and the Unreleased changelog when
   behavior, setup or file formats change.
6. Submit a pull request explaining the problem, resulting behavior,
   validation and remaining limits. Keep commit titles short and specific.

Preserve application IDs, SD paths, save/companion formats, input semantics,
original simulation timing and required synchronization unless the change
explicitly addresses them. A visual change needs actual captures of affected
states. Host checks, ARM builds, Azahar and physical devices are separate
evidence levels; identify the exact build/model for each claim.

## File and style conventions

Use [the repository map](docs/REPOSITORY.md). Follow the surrounding style
within existing files; avoid whole-tree formatting and unrelated renames.
New public guides, issue forms, comments and commit messages use English.
Historical notes and imported text retain their original language.
Do not bump BUILD numbers by hand; the candidate builder reserves them.

## Reports and private data

Public issues need the visible BUILD number, device/model, launch format,
minimal reproduction, expected/actual result and a sanitized log excerpt.
Do not attach ROMs, prepared archives, saves, firmware, credentials, raw RAM
or complete process dumps. Use the community channel to arrange a suitable
private diagnostic path when needed. There is no promised response deadline.

Do not add a release, tag or binary download as part of source cleanup.
Publication of a playable package is a separate maintainer decision.
