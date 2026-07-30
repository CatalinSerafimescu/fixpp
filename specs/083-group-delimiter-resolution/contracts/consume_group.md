# Contract — `consume_group` descend-at-delimiter

**Feature**: `083-group-delimiter-resolution` | Satisfies FR-007, FR-008, FR-009, FR-021b

## Problem

The instance scanner opens each instance at the delimiter and consumes it with a bare increment, **without descending**. The nested-group descent it already performs applies only to members scanned *after* the delimiter. When the delimiter **is** a nested group's count tag, that group's body follows immediately; those tags are not members of the outer group, so the member test fails, the inner scan breaks, the outer loop sees a non-delimiter tag and exits with one instance counted, and the declared-count check rejects.

**Silent on single-instance groups** — which is why it went unnoticed, and why the regression witness must be the two-instance form.

## Contract

**C-4.1** — When the instance-opening delimiter is itself a group **in child context**, the scanner descends into that nested group and resumes one past its extent. Otherwise it consumes the single field as today.

**C-4.2** — Descent reuses the existing depth guard and child-path construction already used for post-delimiter members. This is a **symmetry repair**: the same operation the scanner performs one line later, applied at the instance-opening position.

**C-4.3 — Depth remains bounded** by the existing nesting cap (FR-009). No new recursion limit is introduced; behaviour at and beyond the cap is unchanged.

## Invariants that MUST NOT move (FR-008)

**C-5.1** — Instance-count enforcement: a declared count that disagrees with the instances present still rejects.

**C-5.2** — Required-member enforcement: the delimiter's own required-bit is marked **before** descent, exactly as now. Descending must not skip that marking.

**C-5.3** — Extent termination: an instance still ends at the first non-member tag. Descent changes how the *delimiter* is consumed, never how the extent ends.

**C-5.4** — A genuinely invalid message still rejects. Correct resolution must not become permissiveness; a group opened with a tag that is not that context's delimiter is still an error.

## Ordering constraint — this contract lands FIRST

Non-negotiable, and the reason the phases are not interchangeable: **232 measured FIX50SP2 contexts**, plus **30 more** once the three silently-dropped groups register, have a post-fix delimiter that is a nested group's count tag. Landing recursive delimiter resolution before this contract converts a wrong-delimiter defect into a false rejection across all of them — strictly worse than the current state.

Gate between the two: after this contract lands, the nested-delimiter reproduction is green **and the delimiter pin's failure count has not moved**. This phase must fix reception without touching resolution.

## Witnesses

**W-1 — Nested-delimiter reproduction** (from fixpp#208, already reduced to a minimal shape):

| shape | before | after |
|---|---|---|
| outer group, 2 instances, delimiter is a nested group's count tag | REJECTED — instance-count mismatch | ACCEPTED, 2 instances |
| same, 1 instance | ACCEPTED | ACCEPTED *(must not regress)* |
| same, declared count ≠ instances present | REJECTED | REJECTED *(C-5.1)* |

**W-2 — Typed-read agreement** (FR-021b): a shape validating as N instances reads back as N instances.

**W-3 — Depth**: a shape at the nesting cap is bounded and well-defined, not unbounded recursion.
