# Issues — index and workflow

Local only (`notes/` is not in git). One folder per issue with an approved `spec.md` and `work.md`.

## Active Issues
- [001-self-building-release](001-self-building-release/spec.md) — Windows and Linux self-building release (AppImage + Windows Zip with bundled compiler toolchain and setup wizard) [COMPLETE]

## Workflow
1. **Report a problem / feature** → Draft `spec.md` (status `open`, spec `DRAFT`).
2. **Spec review:** User approves the spec. No code fixes or implementations before spec approval.
3. **Test first:** Write an automated test that fails before the fix.
4. **Implement & verify:** Work logged with stop/go milestones in `work.md`.
5. **Maintainer test:** User verifies only the items requiring human judgement.
6. **Commit & push:** Commit locally with `Fix #00N: ...` only after user confirmation. Push only with explicit permission.
