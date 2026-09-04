# SUPERVISOR_LOOP.md — running the REFACTOR/ migration as a supervised loop

**Audience: the supervisor agent** (a more capable model, e.g. Opus, run
in a loop by Luna). You do not implement. You decompose, delegate to Sonnet
subagents, verify their work, record it, and repeat. This document is
everything you need — read it fully before your first loop iteration, and
re-read the "Required reading" list any time you're unsure of current state,
since it changes as work lands.

If anything below conflicts with what you observe in the repo, the repo
wins — this document describes a process, not a frozen source of truth about
code state. Update the "Current scope" section yourself as stages close.

### NOTES

See `TOOLS.md`'s own "NOTES" section (rm/trash, python-via-uv, listfile
location, casc/wow paths, scratch-dir scripting, no root finds) — same
notes, kept in one place; don't duplicate them here.



---

## Required reading, in order, before your first task pick

1. `/home/luna/.claude/CLAUDE.md` (global rules — Write scope, User-created
   data, Network writes, the approval matrix). These bind every subagent you
   spawn, not just you.
2. `CLAUDE.md` (this project — Boundaries, current Resume state).
3. `REFACTOR/README.md` — the four-stage pipeline, invariants I1-I8, the
   Migration order and its gates. **You are running Migration order stages
   2 and 3.** Stage 1 is landed; stages 4-6 are out of scope for this loop
   (they depend on stage 3 being done, and the bundle-writer/Blender-addon
   work is a separate, later loop).
4. `REFACTOR/AUDIT.md` — **this is your task backlog.** Every open
   subsection (`##`/`###` heading not yet removed) is a candidate bounded
   task. Items already marked closed/fixed inline are removed from this
   file per its own convention — trust what's still in the file, not memory
   of what used to be there.
5. `REFACTOR/CANONICAL_MODEL.md` — stage 3's design, already settled. Do
   not re-litigate it; if a subagent's investigation finds it wrong or
   underspecified for a real case, that's an escalation (see below), not a
   silent judgment call.
6. `REFACTOR_LOG.md` (repo root, **not** inside `REFACTOR/`) — the running
   narrative of every REFACTOR-tagged change so far, in a What/Why/Verified
   format. Read at least the last ~5 entries to know what state the
   codebase is actually in before assigning anything — `AUDIT.md` describes
   remaining gaps, this log describes what already closed them.
7. `REFACTOR/LOOP_STATE.md` (this loop's own state file — create it from
   the template at the bottom of this document if it doesn't exist yet).
   This is the source of truth for what's currently assigned, in review, or
   blocked. **Read it before picking a task, every single loop tick** —
   another instance of you may have crashed mid-task, and a task marked
   `in-progress` with no recent commit is a stale claim to investigate, not
   a task to skip.
8. More relevant reading ~/nix/claude-rules/<file>.md (list files)

## Current scope (2026-09-03)

- **In scope**: `AUDIT.md` §2 (`m2::Model`, the missing internal
  representation — Migration order stage "2. `m2::Model` aggregate") and,
  once that's landed and its gate passes, stage 3 (`canon::`, per
  `CANONICAL_MODEL.md`). `AUDIT.md`'s other open sections (§1.1's remaining
  Python/Blender mirror conversions, §4 Blender-side I3 violations, §5
  slot-as-identity, §6 untraceable naming, §8 corpus-tooling constants) are
  independently biteable and **also in scope** if you judge them smaller
  and safer than pushing on stage 2/3 — they don't block each other.
- **Out of scope, do not assign**: anything under Migration order stages
  4-6 (bundle writer, Blender addon consuming it, CLI regroup) — those need
  stage 3 done first, or are separate work Luna hasn't started this loop
  for. Also out of scope: re-opening the two settled/deferred design
  questions in `BUNDLE_FORMAT.md` and `RESOURCE_CATALOG.md` (physical
  storage shape is **settled**: loose files, ZFS dedup, no
  content-addressing wrapper — don't let a subagent "helpfully" revisit
  this). If real work seems to require touching `REFACTOR/README.md`'s
  Invariants or Migration order sections, or either of those two design
  docs' settled/open sections, stop and escalate — that's a design change,
  not an implementation task.

---

## The loop, one iteration

1. **Orient.** Read `REFACTOR/LOOP_STATE.md`. Reconcile it against reality:
   for any task marked `in-progress`, check `git log` for a matching
   `[UNVERIFIED/STAGING]` commit. If one exists, treat this iteration as a
   **verify** iteration for that task (skip to step 5). If none exists and
   the state file's timestamp is stale (no plausible reason a subagent
   would still be running), mark it `stalled`, note why, and pick a
   different task.

2. **Pick or create a task.** If `LOOP_STATE.md` has a `ready` task, take
   the smallest one. If not, open `AUDIT.md`, find the smallest open
   subsection not already represented in `LOOP_STATE.md`'s history, and
   turn it into a task entry (see "Bounded task sizing" below for what
   "smallest" means and when to split one further). Append it to
   `LOOP_STATE.md` as `ready` before moving on — the file is the backlog,
   don't hold tasks only in your own context.

3. **Assign.** Mark the task `in-progress` in `LOOP_STATE.md` (with a
   timestamp) and commit that state-file change yourself, immediately —
   this is cheap, low-risk, and is what makes a crash mid-loop
   recoverable. Then spawn exactly one Sonnet subagent
   (`Agent` tool, `model: "sonnet"`) with a self-contained brief (template
   below). Run it in the foreground — your very next action is verifying
   its result, and there's nothing else useful for you to do meanwhile.

4. **Wait for the subagent**, then read its final report and the commit(s)
   it made. Do not trust the subagent's self-report of "tests pass" or
   "verified against real data" — that claim gets checked by you in the
   next step, not taken on faith.

5. **Verify**, from scratch, yourself:
   - `git log` — confirm exactly one (or a small, coherent, explained set
     of) new commit(s) exist, each prefixed `[UNVERIFIED/STAGING]`, with a
     real why-focused message (not "fix stuff").
   - `git show --stat` on each — confirm the diff matches the assigned
     task's scope. Flag and reject (see "On failure") anything that
     touched files outside that scope without a stated reason.
   - Build and run the **full** test suite fresh yourself
     (`direnv exec . <build+test command from this project's CLAUDE.md /
     nix env>`) — don't reuse the subagent's own claimed result.
   - Check the change against the relevant invariants (I1-I8,
     `REFACTOR/README.md`) and against the specific stage's gate if the
     task closes one (Migration order section of `README.md`).
   - Check no `CLAUDE.md` hard limit was crossed: no `git push`, no
     network writes, no edits outside the work dir, no destructive `rm`
     without it having been asked about first (subagents can't get
     interactive permission from Luna — if one needed to delete something
     non-trivial, that should show up as a flagged question in its report,
     not a silent action).
   - If the task claimed a "verified against real data" step (this
     project's own house style, see `CLAUDE_HISTORY.md` for the pattern),
     spot-check that claim — re-run the specific command against the
     specific real fixture cited, don't just read the prose.

6. **Record the verdict.**
   - **Pass**: update `LOOP_STATE.md`'s entry to `verified`, with the
     commit hash(es) and a one-line note of what you specifically checked.
     Append a `REFACTOR_LOG.md` entry in its existing What/Why/Verified
     format if the subagent didn't already write one (prefer requiring the
     subagent to write it as part of its own bounded task — see the brief
     template — so you're reviewing prose, not authoring it fresh). Commit
     the state-file update. **Do not amend or rewrite the subagent's
     `[UNVERIFIED/STAGING]` commit** — CLAUDE.md's hard rule against
     rewriting history applies to you too; the tag stays in git history as
     an honest record of how the work was produced, your verification is a
     separate, additive commit.
   - **Fail**: mark `failed` in `LOOP_STATE.md` with the concrete reason
     (failing test name, invariant violated, scope creep found, claim that
     didn't reproduce). Decide: if the fix is itself small and bounded,
     spawn one corrective subagent next iteration with the failure
     explained. If the failure suggests the task was mis-scoped (too big,
     ambiguous, or the design doc it depended on was wrong), do not just
     retry — split it smaller, or escalate.

7. **Continue or stop.** If there's a next `ready`/creatable task and
   nothing this iteration surfaced needs Luna, continue the loop. If you
   hit a genuine escalation condition (below), stop scheduling further
   iterations and say clearly, in your own words, what's blocked and why —
   don't guess past it.

---

## Bounded task sizing

A task is right-sized when a single Sonnet subagent can, in one sitting:
investigate the real, current code (not assume from `AUDIT.md`'s prose,
which may already be stale by the time you read it); implement; add or
update tests; verify against real project data where this project's own
convention calls for it (corpus fixtures, real `.m2`/`.db2` files — see
`CLAUDE_HISTORY.md` for the pattern); write its own `REFACTOR_LOG.md`
entry; commit.

Concretely:
- **One `AUDIT.md` subsection, or a clearly separable piece of one**, if
  the subsection itself names multiple independent sub-items (`§1.1`'s
  table has several still-unconverted rows — those are separable tasks,
  not one task).
- **Never spans a stage boundary.** A stage-2 (`m2::Model`) task does not
  start writing `canon::` types, even if it would be convenient. Stage 3
  starts only once stage 2's own gate (README.md Migration order, item 2)
  is met and verified.
- **Never touches `REFACTOR/README.md`'s Invariants or Migration order**,
  or the two settled/deferred design sections in `BUNDLE_FORMAT.md` /
  `RESOURCE_CATALOG.md`. A subagent proposing to change these is out of
  scope — redirect it to record the question instead (see Escalation).
- If an `AUDIT.md` item looks like it touches more than ~5 genuinely
  different call sites or files, split it into multiple `LOOP_STATE.md`
  entries yourself before assigning any of them — don't hand a subagent
  something wide enough to become the kind of mid-design refactor that
  gets thrown away.

## Subagent brief template

Subagents start cold — no memory of this document, this conversation, or
prior loop iterations. Every brief must be self-contained:

```
You're implementing one bounded step of husk's REFACTOR/ migration
(target four-stage pipeline: parse -> resolve -> canon -> write).
Read REFACTOR/README.md first for the invariants (I1-I8) — they bind
this task. [Then, if relevant: read REFACTOR/CANONICAL_MODEL.md /
AUDIT.md's cited section directly, don't take my summary as authoritative.]

Task: <exact AUDIT.md citation, file:line, and what "done" means>.

Scope: touch only <named files/areas>. If you find you need to touch
something outside that to make this correct, stop and report why rather
than expanding scope silently.

Steps:
1. Investigate the real current code at the cited location — AUDIT.md's
   description may be stale.
2. Implement.
3. Add/update tests. Run the full suite (<build/test command>), not just
   the new tests.
4. If this project's own convention calls for verifying against real
   corpus/fixture data (check CLAUDE_HISTORY.md's pattern), do that and
   record the exact command and result, not just "verified".
5. Append one entry to REFACTOR_LOG.md in its existing What/Why/Verified
   format (read the file's existing entries for the exact shape first).
6. Remove the closed item from AUDIT.md per that file's own "closed items
   get removed outright" convention, if this task fully closes it — leave
   it if only partially closed, and say what's left in your report.
7. Commit with a message starting literally "[UNVERIFIED/STAGING] " and
   ending with the project's usual Co-Authored-By line. One commit per
   coherent change; don't bundle unrelated cleanup in.

Hard rules (from this project's global CLAUDE.md, binding on you too):
- Never `git push`. Never amend or rewrite existing commits — new commits
  only.
- Work dir and /tmp only for writes. Never touch anything outside this
  repo's working tree.
- No destructive operations (rm on tracked files/dirs, force operations)
  without stopping to ask first — you cannot get interactive approval, so
  if you believe one is genuinely needed, stop and report that instead of
  doing it or skipping the goal silently.
- Any Python tooling work uses `uv`/the existing tools/venv — never
  system pip, never a new ad hoc venv.
- Don't add scope, abstractions, or "while I'm here" cleanup beyond the
  named task.

Report back: what you did, exact commit hash(es), exact test command and
result, exact verification command and result if applicable, and
anything you found that seems like it needs a human or design decision
rather than an implementation choice.
```

## Escalation — stop and wait for Luna, don't guess

Stop the loop (don't spawn another subagent, don't schedule another
iteration) and clearly state what's blocking, when:

- A task turns out to require a design decision `CANONICAL_MODEL.md` or
  `README.md` doesn't already settle, and picking one yourself would be a
  judgment call with real downstream cost (this is the same class of
  thing the two design docs' now-closed "Open question" sections were).
- A subagent's investigation contradicts a "Settled" section in
  `CANONICAL_MODEL.md`, `RESOURCE_CATALOG.md`, or `BUNDLE_FORMAT.md`
  against real data — don't silently pick a side.
- A destructive operation genuinely looks necessary (deleting tracked
  files/directories beyond a closed `AUDIT.md` item's own described
  cleanup, restructuring something wide).
- Two consecutive verify-failures on the same task with different root
  causes — that's a signal the task itself is mis-scoped, not that a
  third attempt will land it.
- Anything that would require network write access, touching
  outside the work dir, or otherwise crossing a `CLAUDE.md` hard limit.

---

## `LOOP_STATE.md` template

Create `REFACTOR/LOOP_STATE.md` with this shape if it doesn't exist:

```markdown
# LOOP_STATE.md — supervisor loop state for the REFACTOR/ migration

Live state, not a log — REFACTOR_LOG.md is the narrative record. Update
this file in place; don't let entries pile up past `verified`/`stalled`
history a few iterations deep (trim old verified/stalled rows once
they're reflected in REFACTOR_LOG.md and no longer useful as a map).

| Task | Source | Status | Assigned | Commit(s) | Notes |
|---|---|---|---|---|---|
| <short id> | AUDIT.md §N | ready / in-progress / verified / failed / stalled | <timestamp or -> | <hash or -> | <one line> |
```
