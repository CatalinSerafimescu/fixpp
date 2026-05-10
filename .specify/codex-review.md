# fixpp Codex Review Gate Procedure

> **Authority:** `[const §XVII]` — Codex Review Gates. This document captures
> the operational procedure for Gate A and Gate B as defined in the constitution
> and originally framed in `opus_plan.md`. Every PR must carry links to Gate A
> and Gate B outcomes before merge.

---

## 1. Triggers

### Gate A — Design review (pre-implementation, non-trivial designs)

Gate A is **mandatory** when any of the following are true (Appendix A of the
constitution):

- Touches the public C++ API or C ABI.
- Touches concurrency / threading / cancellation / executor model.
- Touches the wire format, parser, or codegen layout.
- Touches the session FSM, recovery, or message store contract.
- Touches the security surface (TLS, cert handling, PSK).
- Any new design document under `.specify/` (qualifies by default).

Trivial changes (rename a private helper, add P2 boilerplate over an existing
module) skip Gate A. **When in doubt, run it.**

### Gate B — PR review (post-implementation, every PR)

Gate B is **mandatory on every PR** before merge, regardless of feature size.
High-severity findings must be resolved or explicitly waived with rationale in
the PR description before merge.

---

## 2. Independence rule

**`[const §XVII.3]`** — the reviewer is independent of the implementer.

- When Codex implements (escalation path per `[const §XVI.8]`), the PR review
  for that PR must come from a **separate Codex session**, not the one that
  wrote the code.
- Neither Sonnet nor Opus auto-invokes Codex; gates are user-driven.

---

## 3. Local invocation

### Via Codex CLI

```bash
# Gate A — design review on a .specify doc
codex exec --model o4-mini \
  "You are a hostile code reviewer. Review the design doc at \
   .specify/<doc-id>.md against constitution.md and architecture.md. \
   Find every P0, P1, P2, P3 issue. P0 = spec violation or safety hazard; \
   P1 = correctness/soundness; P2 = maintainability/perf risk; P3 = style. \
   List findings as: SEVERITY | location | description | suggested fix. \
   Return SHIP-AS-IS, SHIP-WITH-FIXES, or REWRITE."

# Gate B — PR review
codex exec --model o4-mini \
  "You are a hostile code reviewer. Review the diff at branch <branch>. \
   Find every P0, P1, P2, P3 issue. Verify: TDD (failing test before impl), \
   no code without test, no banned patterns ([const §XV]), no C++ leakage \
   through C ABI, no thread_local for trace context. \
   List findings as: SEVERITY | file:line | description | suggested fix. \
   Return SHIP-AS-IS, SHIP-WITH-FIXES, or REWRITE."
```

### Via `codex:codex-rescue` agent (in-session)

Use the `codex:rescue` skill when an out-of-process Codex CLI call is not
available. Pass the same prompts above as the task description.

---

## 4. Verify-output-on-disk rule

**Memory: `feedback_gate_codex_quota.md`** — after every Codex agent spawn,
verify the output file actually landed on disk before declaring success:

```bash
# After Gate A
ls -lh .specify/decisions/<doc-id>.md

# After Gate B
ls -lh .specify/decisions/<doc-id>-gateb.md
```

If the file does not exist, the Codex run did not complete. Re-run before
proceeding.

---

## 5. Gate A prompts (verbatim, from `opus_plan.md` lines 165–175)

```
You are a hostile design reviewer for a modern C++23 FIX protocol library.
Review the design doc provided. The library's constraints are in
constitution.md and architecture.md. Find every issue at these severities:

P0 = specification violation, memory safety hazard, or ABI breakage
P1 = correctness or soundness problem
P2 = maintainability, performance, or testability risk
P3 = style, naming, or minor clarity issue

For each finding:
  SEVERITY | location in doc | description | suggested fix

At the end, output one of:
  SHIP-AS-IS       (zero P0/P1, ≤3 P2)
  SHIP-WITH-FIXES  (zero P0, ≥1 P1 or >3 P2 — all must be resolved)
  REWRITE          (any P0, or design is fundamentally unsound)
```

---

## 6. Gate B prompts (verbatim, from `opus_plan.md` lines 184–195)

```
You are a hostile code reviewer for a modern C++23 FIX protocol library.
Review the PR diff. The library's constraints are in constitution.md and
architecture.md. Find every issue at these severities:

P0 = spec violation, memory safety, ABI break, banned pattern ([const §XV])
P1 = correctness, soundness, test coverage (TDD rule: test must precede impl)
P2 = performance regression, maintainability, unchecked error path
P3 = style, naming, doc drift

Mandatory checks:
- Every new function has a test.
- No code without a test on main.
- No C++ headers leak through the C ABI boundary.
- No thread_local for trace context propagation.
- No std::mutex in coroutine context.
- No heap allocation on the hot path between parse and fromApp.

For each finding:
  SEVERITY | file:line | description | suggested fix

At the end, output one of:
  SHIP-AS-IS       (zero P0/P1, ≤3 P2)
  SHIP-WITH-FIXES  (zero P0, ≥1 P1 or >3 P2 — all must be resolved before merge)
  REWRITE          (any P0, or implementation is fundamentally unsound)
```

---

## 7. Recording findings

Gate A and Gate B outcomes are recorded under:

```
.specify/decisions/<doc-id>.md         # Gate A outcome
.specify/decisions/<doc-id>-gateb.md   # Gate B outcome
```

This mirrors the parent repo's `decisions/` convention. Each decision file
records:
- Date and Codex model used.
- Summary verdict (SHIP-AS-IS / SHIP-WITH-FIXES / REWRITE).
- Full findings list.
- Resolution notes (which P1/P2 were fixed, which were waived with rationale).
- Reviewer (Codex session ID or agent name).

The PR description links to both files.
