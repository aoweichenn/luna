# Luna validation protocol

Read this before executing project scripts or generated binaries.

## Execution boundary

The local workspace is for source reading, search, edits and diff inspection.
Run every Luna build, audit, formatter gate, test, benchmark, sanitizer or
other project execution on the Tailscale host `caw` in a fresh isolated
directory. Do not silently substitute the local host when `caw` is unavailable.

Before remote execution:

1. Confirm SSH/Tailscale connectivity, `uname -m`, operating-system details and
   Python version. The required target host is x86-64 Linux.
2. Create a uniquely named empty directory under the remote user's
   `codex-workspaces` area. Record its exact path.
3. Copy the current local tree, including uncommitted in-scope changes, into
   that directory. Exclude `.git`, `build`, `out`, `web`, `__pycache__`, logs,
   caches, generated artifacts and credentials.
4. Never overwrite an existing remote repository or workspace. Never use an
   unresolved broad path as a cleanup target.

## Gate selection

For a normal source change under `library`, `compiler` or `drivers`, run in
this order in the isolated copy:

```sh
python3 tools/selfhost.py audit
python3 tools/refmt.py --check
python3 tools/selfhost.py verify --fresh
python3 tools/selfhost.py test
```

The cold verify must execute anchor -> transition -> next -> fixed and report
byte-identical next/fixed artifacts. Record the stage timings, module/driver
closure summary, test passed/failed/skipped counts and final binary hash/size
when it matters to the task.

Scale additional evidence to the changed boundary:

- Language or semantic changes: add positive behavior and exact negative
  diagnostic cases; verify first implementation syntax is anchor-supported.
- Module graph changes: prove registry membership, import acyclicity, driver
  closure and deletion of obsolete interfaces/objects.
- Ownership or binary-format changes: use the relevant focused contract and
  malformed/corruption path in addition to the full suite.
- Build/test harness changes: compare serial and default concurrency where
  scheduling or determinism is affected.
- Performance claims: benchmark old and new implementations on the same `caw`
  workspace conditions and report commands, repetitions, statistic, output
  size and hashes. Do not hide a regression behind cached results.
- Documentation, skill or Codex configuration changes with no project-runtime
  effect: inspect links/configuration, validate the skill and run diff/whitespace
  checks; do not launch an expensive self-host cycle merely for appearance.

Use focused incremental builds only for iteration. They do not replace the
fresh fixed-point and full-suite gates required for a completed source batch.

## Anchor and release boundary

Do not alter `anchor/` during ordinary implementation or validation. When the
user explicitly requests commit and push of a completed change under
`library/`, `compiler/` or `drivers/`, the repository contract requires the
verified stage-fixed toolchain to be promoted in the same change, with
`SHA256SUMS`, provenance and source/docs/tests/session state kept coherent.

Promotion evidence must identify the exact isolated workspace and confirm the
artifact copied is the byte-identical stage-fixed result. Commit and push are
still external workflow steps and require the user's request.

## Cleanup and reporting

Remove only the isolated remote directory created for the task, after
validating its exact path and after preserving any evidence needed for an
unfinished handoff. If cleanup is deferred, report the path.

The final report must distinguish:

- local static inspection;
- remote host and isolated directory;
- commands and gates that passed;
- skipped or blocked validation and why;
- anchor, commit and push status.
