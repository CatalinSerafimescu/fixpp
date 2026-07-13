# Feature Specification: Fail-loud on nested-read sub-table allocation failure

**Feature Branch**: `073-nested-read-arena-failloud`

**Created**: 2026-07-13

**Status**: Draft

**Input**: GitHub #184 (L-065-2). User description: "Fail-loud on nested repeating-group sub-table allocation failure — a nested-group read that exhausts the fixed inbound parse arena must surface a distinct error, not a silent empty group."

## Context & Problem

When an application reads a **nested** repeating group from an inbound message, both the C-ABI read path and the typed C++ read path route into a shared primitive that builds a sub-view of the nested group's members. That sub-view is allocated from the session's fixed inbound parse arena (bounded, non-growing, backed by a null upstream so it cannot spill to the heap).

If that arena is exhausted while building the sub-view, the allocation fails and the primitive **degrades to an empty result**. The empty result is then indistinguishable from a group that is legitimately absent or has zero entries. On the C-ABI read this surfaces as "success, entry-count 0"; on the typed read it surfaces as an empty group. In both cases a **genuinely present nested group is silently reported as empty** — a silent truncation.

This is a hardening / denial-of-service-robustness concern. It is an extreme edge: it requires a single inbound message whose nested-group read exhausts the fixed arena, and it is not reachable in normal traffic. It was recorded as limitation **L-065-2** and deliberately deferred out of feature 065 (whose Gate A, research Decision 6 / FR-009, explicitly declined widening the primitive's result for the then-in-scope unreachable-overflow case). Reopening that declined decision is why this feature carries its **own Gate A**.

The silent-truncation risk exists identically on the already-shipped typed path (shipped in 062/066) and on the 065 C-ABI path — this feature closes it on **both** so they stay in parity.

## Clarifications

### Session 2026-07-13

- Q: What distinct C-ABI error should the nested read return on sub-table allocation failure? → A: Reuse the existing `FIXPP_ERR_WIRE_LIMIT_EXCEEDED` (101). The parse arena exhausting while building a nested offset sub-table is a wire-processing resource limit; the sibling C++ error `wire_offset_table_full` already maps to this code. No new error code, no `error_message()`/completeness-test churn, and it is distinct from both `OK` and `TAG_NOT_FOUND`. (The C-ABI nested read returns `fixpp_error_t` directly, so no `core::error` enumerator is appended — the feature-072 constraint does not apply.)
- Q: Should this feature also change parse-arena sizing (larger/growable) or stay fail-loud-only? → A: Fail-loud-only. The bounded, null-upstream arena is preserved (its boundedness IS the DoS-robustness property; a growable-on-demand arena is itself a resource-exhaustion vector). Any arena-sizing change is out of scope and deferred to a separate decision.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - C-ABI nested read fails loud on arena exhaustion (Priority: P1)

An application consuming the C-ABI reads a nested repeating group that is genuinely present in the inbound message. The parse arena is exhausted at the moment the nested sub-view must be built.

**Why this priority**: This is the shipped, publicly-supported read surface. A silent "entry-count 0" here causes the application to drop message content it was contractually told is present — the exact silent-loss failure class the project treats as default-real. Closing it on the C-ABI is the core deliverable.

**Independent Test**: Drive a C-ABI nested-group read whose sub-view build is forced to fail by injecting a parse arena with a capacity too small to satisfy it; assert the read returns a distinct error code (not the OK / entry-count-0 pair). Fully testable through the C-ABI alone.

**Acceptance Scenarios**:

1. **Given** an inbound message with a present nested group and a parse arena that cannot satisfy the nested sub-view allocation, **When** the application performs the C-ABI nested-group read, **Then** the read returns a **distinct error** (not `OK` with entry-count 0), and the application can tell "allocation failed" apart from "group absent / zero entries".
2. **Given** an inbound message whose nested group is genuinely **absent or has zero entries**, and a parse arena with ample capacity, **When** the application performs the C-ABI nested-group read, **Then** the read still returns the ordinary absent / zero-entry result (`OK`, entry-count 0) — the new error is **not** raised for legitimate emptiness.
3. **Given** an inbound message with a present, readable nested group and ample arena capacity, **When** the application performs the C-ABI nested-group read, **Then** the read succeeds and returns the correct entries exactly as before this feature (no behavior change on the success path).

---

### User Story 2 - Typed C++ nested read fails loud on arena exhaustion (Priority: P1)

An application consuming the typed C++ read API reads a nested repeating group that is genuinely present, and the parse arena is exhausted when the nested sub-view must be built.

**Why this priority**: The typed path routes through the same shared primitive and the same fixed arena. If only the C-ABI is hardened, the two paths diverge and the typed path retains the silent-truncation defect — a half-restructure. Parity across both consumers is a first-class requirement, not a follow-up.

**Independent Test**: Drive a typed nested-group read whose sub-view build is forced to fail by injecting an under-capacity parse arena; assert the read surfaces the allocation failure distinctly from an empty group, without terminating the process.

**Acceptance Scenarios**:

1. **Given** a typed nested-group read of a present group and a parse arena that cannot satisfy the sub-view allocation, **When** the read executes, **Then** the allocation failure is surfaced **distinctly** from an empty group, and the process does **not** terminate (the failure is delivered as a value/status, not by throwing across a non-throwing boundary).
2. **Given** a typed nested-group read of a legitimately absent or zero-entry group with ample arena capacity, **When** the read executes, **Then** it yields the ordinary empty result — the allocation-failure signal is **not** raised.
3. **Given** a typed nested-group read of a present, readable group with ample arena capacity, **When** the read executes, **Then** it yields the correct entries exactly as before (no success-path behavior change).

---

### Edge Cases

- **Legitimately empty vs. allocation-failed**: A group that is absent, has zero entries, or whose slice is empty MUST continue to read as ordinary empty and MUST NOT raise the allocation-failure signal. The failure signal is raised **only** when a present group's sub-view could not be allocated. This disjointness is the central correctness property — conflating the two would either mask real allocation failures or fabricate failures for empty groups.
- **Repeated read after a failed build**: If an application re-reads the same nested group after a build that failed under exhaustion, the read MUST again surface the allocation failure (or succeed if capacity is now available) — it MUST NOT serve a cached silent-empty result that hides the earlier failure.
- **Reads unaffected by the widening**: Top-level (non-nested) group reads, scalar-field reads, and nested reads that do not exhaust the arena MUST be unchanged.
- **Non-terminating delivery**: Because the primitive and its callers operate under a non-throwing contract, the failure MUST be delivered without throwing an exception across that boundary (which would terminate the process).

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The shared nested-group sub-view primitive MUST expose, to its callers, a result that distinguishes three outcomes: (a) a successfully built sub-view (possibly with zero entries), (b) a legitimately absent / empty input (no sub-view needed), and (c) a **sub-view allocation failure**. Today outcomes (b) and (c) are both reported as an empty result and are indistinguishable.
- **FR-002**: The allocation-failure outcome MUST originate at the point where the sub-view allocation actually fails and MUST be propagated upward from there. It MUST NOT be inferred or reconstructed at a caller by re-testing conditions that also hold for legitimate emptiness.
- **FR-003**: The C-ABI nested-group read MUST, on a sub-view allocation failure, return a **distinct error** that a client can differentiate from both success and the ordinary absent / zero-entry result. It MUST NOT report allocation failure as `OK` with entry-count 0.
- **FR-004**: The typed C++ nested-group read MUST, on a sub-view allocation failure, surface that failure **distinctly** from an empty group, delivered as a value/status without throwing across the non-throwing boundary (i.e. without terminating the process).
- **FR-005**: The widening MUST be applied **symmetrically in one pass** to every overload of the shared primitive and to **both** consumers (the C-ABI read path and the typed read path). No consumer or overload may be left on the old empty-conflating result.
- **FR-006**: On all non-failure paths — successful reads, legitimately absent groups, and zero-entry groups — observable behavior (returned entries, entry counts, error codes, absence semantics) MUST be **identical** to pre-feature behavior. This feature adds a new failure signal; it changes no existing success or absence outcome.
- **FR-007**: The allocation-failure signal MUST be raised **only** for a genuinely present group whose sub-view could not be allocated — never for an absent group, a zero-entry group, an empty slice, or a stale/expired input.
- **FR-008**: The C-ABI nested read MUST surface allocation failure by returning the existing `FIXPP_ERR_WIRE_LIMIT_EXCEEDED` code (the code to which the sibling wire resource-limit errors already map). It MUST NOT introduce a new error enumerator and MUST NOT append to the core error enum (avoiding the exhaustiveness-switch / error-completeness churn that feature 072 deliberately avoided). The returned code MUST be distinct from both `OK` and the ordinary absent result (`TAG_NOT_FOUND`).
- **FR-009**: Arena sizing is **out of scope**. The fixed inbound parse arena MUST remain bounded with a null upstream (its boundedness is the denial-of-service-robustness property; a growable-on-demand arena would itself be a resource-exhaustion vector). This feature adds the fail-loud seam only; any change to arena capacity or growability is deferred to a separate decision.

### Key Entities

- **Nested sub-view build result**: The outcome of building a nested group's member sub-view. Must carry enough information for a caller to distinguish success (with its entries), legitimate emptiness, and allocation failure — rather than collapsing the latter two into an empty result.
- **Parse arena**: The bounded, non-growing memory region from which a message's parse structures (including nested sub-views) are allocated. Its exhaustion is the trigger condition for the allocation-failure outcome. Its sizing policy is a separate concern (see FR-009).
- **C-ABI read error code**: The distinct, client-observable signal returned by the C-ABI nested read when a sub-view allocation fails — the existing `FIXPP_ERR_WIRE_LIMIT_EXCEEDED` (see FR-008).

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: A C-ABI nested-group read of a present group under arena exhaustion returns a distinct error rather than `OK` / entry-count 0, in 100% of runs of the exhaustion witness. The witness is proven to have been RED (silently reported empty) before the change and GREEN after.
- **SC-002**: A typed C++ nested-group read of a present group under arena exhaustion surfaces the failure distinctly from an empty group and does not terminate the process, in 100% of runs of the exhaustion witness. The witness is proven RED before and GREEN after.
- **SC-003**: For a legitimately absent or zero-entry nested group with ample arena capacity, neither the C-ABI nor the typed read raises the allocation-failure signal — verified by a dedicated non-failure witness on each path (0 false positives).
- **SC-004**: The full existing test suite passes unchanged on all non-failure paths, demonstrating no behavior change on success/absence outcomes (FR-006), across the sanitizer / analysis matrix the feature's diff touches.
- **SC-005**: Both the C-ABI and typed nested reads remain in parity under exhaustion: both surface a distinct failure (neither silently reports empty), verified by the symmetric pair of witnesses (FR-005).

## Assumptions

- The exhaustion condition is driven **faithfully** in tests by injecting a parse arena whose capacity is deliberately too small to satisfy the nested sub-view allocation (a bounded resource over a null upstream) — not by fabricating a 16 KiB-exhausting real message and not by flipping a post-hoc "allocation failed" flag. The injected under-capacity arena reproduces the exact failure the fixed arena would exhibit at its cap.
- The fixed inbound parse arena remains bounded with a null upstream (its denial-of-service-robustness property is preserved); this feature does not change that unless FR-009 clarification expands scope.
- The typed and C-ABI paths share the same sub-view primitive and the same arena, so a single widening of the primitive (plus both consumers) closes the defect on both — no third read path exists.
- The L-065-1 nested-context-path arithmetic is already fixed (feature 072) and is orthogonal to this allocation-failure work; this feature does not touch context-path arithmetic.
- The change is confined to the nested-read allocation-failure seam; it introduces no new arena, no new read path, and no change to outbound/write paths.
- **Known limitation / out of scope (L-073-1)**: the fail-loud signal (typed `group_view::alloc_failed()` and the C-ABI `WIRE_LIMIT_EXCEEDED`) covers the **nested** read path only. **Top-level** (non-nested) group reads — where the top-level group getter calls the sub-table's slice materialization directly — retain the same silent-truncation-on-exhaustion (the same fixed-arena family as the 066 reserve-bound mitigation `cc169700`) and are deliberately out of scope (FR-009; the "Reads unaffected by the widening" edge case above mandates top-level reads stay unchanged). Recording the operator-facing B&L row (`spec/behaviors-and-limitations.md`) is deferred to the implementation phase.
