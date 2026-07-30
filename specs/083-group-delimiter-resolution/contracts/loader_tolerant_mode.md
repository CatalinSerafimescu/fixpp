# Contract — Load disposition for an unresolvable group delimiter

**Feature**: `083-group-delimiter-resolution` | Satisfies FR-006, FR-006a, FR-006b, FR-017

Resolves the 2026-07-30 clarification: **reject by default, with an explicit opt-in tolerant mode.**

## Current behaviour (the defect)

A group whose declaration yields no first member records delimiter `0` and is **silently dropped** from the runtime dictionary. No diagnostic. Three FIX 5.0 SP2 groups are in this state today, taking six nested children with them, so the runtime registers 502 groups where the code generator emits 505 — the engine's own strict validation rejects messages its own generator produced.

This silent drop is the outlier in a loader that otherwise fails closed on every structural violation: root element not `<fix>`, missing major/minor, missing `<fields>`, missing or duplicate field `number`, unknown `type`, and a `<group>` with no matching `<field>` declaration all throw.

## Contract

**C-6.1 — Default: fail closed.** An unresolvable group delimiter, after the recursive resolution of `group_ctx_delims.md` C-1.2, rejects the load with a diagnostic **naming the offending group**. Mirrors the existing disposition rather than inventing one.

**C-6.2 — Opt-in tolerant mode.** An explicit caller opt-in downgrades C-6.1 to warn-and-skip: a diagnostic is emitted, the group stays unregistered, the load succeeds. For callers loading untrusted or partial third-party dictionaries.

**C-6.3 — The default is fail-closed.** Tolerant mode is never implicit, never inferred, never the fallback on a parse difficulty.

**C-6.4 — Symmetry.** Both loaders expose the same option with the same semantics (FR-005).

**C-6.5 — No hot-path effect.** This is load-time only. It introduces no branch on the parse or validate path.

**C-6.6 — Source compatibility.** The opt-in must not break existing callers of the public C++ load entry points. Express it as a defaulted parameter or an options object defaulting to fail-closed — never as a new required argument. This is a C++ surface change only; no C ABI symbol is involved, so Article X's frozen-surface rule is not engaged, but existing source callers must still compile unchanged.

## Sequencing precondition — a task, not an assumption

**C-7.1** — Before C-6.1 is enabled, **all ten shipped dictionaries MUST be confirmed to load under the default.**

This is ordered deliberately. After the recursive resolution the three known offenders resolve — but whether some group *unreachable from message expansion* still resolves nothing is **not measured**. If one does, the fail-closed default would refuse a dictionary that loads today, and CI would be the first to find out.

Two acceptable outcomes, one unacceptable:

- all ten load → enable C-6.1 as specified;
- some group is genuinely unresolvable → record it, and decide explicitly whether it is a dictionary defect to report or a case for tolerant mode. **Not** a reason to quietly weaken the default;
- ✗ enabling C-6.1 without running this check.

## Witnesses (FR-006b — both dispositions, or the gate is half-tested)

| # | Given | Mode | Then |
|---|---|---|---|
| W-4 | a dictionary with an unresolvable group | default | load rejected, diagnostic names that group |
| W-5 | same dictionary | tolerant | load succeeds, diagnostic emitted, group unregistered |
| W-6 | all ten shipped dictionaries | default | all load — C-7.1 |
| W-7 | FIX 5.0 SP2 | default | 505 groups registered, matching codegen; the delta from 502 is accounted for by the three named groups, not asserted as a bare number (FR-017) |

Both dispositions are documented for operators. A fail-closed path with no rejection witness, or a tolerant mode with no skip witness, leaves half the contract unproven.
