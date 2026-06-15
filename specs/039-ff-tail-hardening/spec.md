# Feature Specification: F-f tail hardening bundle (LOW)

**Feature Branch**: `039-ff-tail-hardening`
**Created**: 2026-06-15
**Status**: Draft (re-scoped 2026-06-15 — US1 split out to `040-inbound-tag-overflow-hardening`)
**Input**: User description: "F-f tail hardening bundle — the residual LOW items from the Fable F-f release-gate tail after 038."

> **Split notice (2026-06-15):** The original US1 (wire tag-accumulation overflow guard) was found
> during Gate A round 1 to be a real **5-site** security fix (a defective shipped guard on the
> central inbound `scan_frame_header`, aliasing 52=SendingTime against the 038 guard, plus two more
> unguarded session/engine scanners). It was **split out to its own feature `040-inbound-tag-overflow-hardening`**
> (census + grounding: `research/reviews/opus_039-ff-tail-hardening_gate_a_adversarial_review.md`).
> This feature (039) is now the genuine LOW tail: **US2–US5**, none of which change production
> behavior — so **Gate A is not required** (no wire/C-ABI-behavior/codegen/config surface touched).

## Clarifications

### Session 2026-06-15

- Q: US2 (C-ABI sentinel) — the Fable finding (reject `INT64_MIN` in `_checked` compare) was found during planning to contradict a user-ratified frozen-ABI decision (001 `/clarify` Session 2026-05-12 / research.md D-12 / the explicit `src/capi/decimal.cpp:43-48` "do not unify without an ABI decision" comment): `_checked` deliberately validates the **exponent domain `[-38,0]` only**, and a sentinel with `exponent=0` is in-domain by that ratified contract. How should US2 proceed? → A: Downgrade to test + comment only — make NO behavioral change. Add a regression test pinning the ratified behavior (`fixpp_decimal_compare_checked`/`_equal_checked` of the `INT64_MIN` sentinel with a valid exponent returns `FIXPP_ERR_OK` with ordering 0 / equal 0) and a cross-reference comment citing AC-C6 / D-12 so the behavior is not "re-fixed". The behavioral reject-the-sentinel change, if ever wanted, is a separate ABI-decision feature, explicitly OUT OF SCOPE here.

## User Scenarios & Testing *(mandatory)*

<!--
  Four independent, separately-witnessed LOW concern groups. None change production behavior:
  US2 = test+comment, US3 = test, US4 = build-gate, US5 = doc. Implementation discipline: one
  implementer invocation per user story (phase-implementer-sonnet runaway-scope guard).
-->

### User Story 2 - C-ABI decimal sentinel behavior pinned (Priority: P1)

The C-ABI `_checked` compare/equal entry points, by a **user-ratified frozen-ABI decision** (001
`/clarify` Session 2026-05-12 / research.md D-12; explicit `src/capi/decimal.cpp:43-48` comment),
deliberately validate only the **exponent domain `[-38,0]`** — they do NOT reject the `INT64_MIN`
sentinel mantissa, because routing through `from_pod` would silently tighten the frozen C-ABI
(`[const §X.1]`, MAJOR=1). A sentinel POD with a valid exponent is therefore *in-domain* for
`_checked` and returns `FIXPP_ERR_OK` with ordering/equal 0. This behavior is currently **untested**
— a maintainer could mistake it for a defect and "re-fix" it, breaking the frozen ABI. After this
story the behavior is pinned by a regression test and a cross-reference comment; **no behavior
changes.**

**Why this priority**: First because it carries the most subtle correctness risk of the four (a
future accidental ABI break). LOW, test + doc-comment only.

**Independent Test**: Call the C-ABI `_checked` compare/equal with the `INT64_MIN` sentinel (valid
exponent) as left, right, and both operands and assert each returns `FIXPP_ERR_OK` with ordering 0
/ equal 0 — pinning the ratified contract.

**Acceptance Scenarios**:

1. **Given** the `INT64_MIN` sentinel (exponent 0) as an operand of `fixpp_decimal_compare_checked`,
   **When** called, **Then** it returns `FIXPP_ERR_OK` with `out_ordering == 0` (ratified behavior).
2. **Given** the sentinel as an operand of `fixpp_decimal_equal_checked`, **When** called, **Then**
   it returns `FIXPP_ERR_OK` with `out_equal == 0`.
3. **Given** a genuinely out-of-domain *exponent* (outside `[-38,0]`), **When** `_checked` is called,
   **Then** it returns `FIXPP_ERR_DECIMAL_INVALID` (the exponent-domain validation still holds).
4. **Given** two ordinary valid decimals, **When** `_checked` compare/equal is called, **Then** the
   result is reported exactly as today (no regression).

---

### User Story 3 - Coverage-waiver remediation (Priority: P2)

A maintainer auditing the coverage waivers finds three lines previously dispositioned "untestable"
that are in fact reachable. After this change each is either covered by a witness or carries a
properly-justified, re-measured waiver, so the coverage record is honest.

**Why this priority**: LOW, test-completeness only — no production behavior changes.

**Independent Test**: Run the new witnesses and confirm each previously-waived line is now executed
(or that its residual waiver cites a re-measured, specific justification).

**Acceptance Scenarios**:

1. **Given** the `set_next_outbound` lock-fail branch (returns `session_already_closed`), **When**
   a test forces `async_lock` to fail via the `mutex_test_access` seam, **Then** the branch executes
   and the error is asserted.
2. **Given** the 008 `OsFile` move-constructor, **When** the move-ctor witness runs, **Then** the
   move-ctor is exercised and the moved-from/-to invariants hold.
3. **Given** the 033 lines deferred to a Gate B that never measured per-line DA/BRDA, **When**
   coverage is re-measured, **Then** each is either covered or carries a specific re-measured waiver.

---

### User Story 4 - §XV.9 no-std-mutex corpus-gate extension (Priority: P2)

A maintainer adds a new session-side awaitable header. Today the §XV.9 no-`std::mutex`-in-a-coroutine
corpus gate pins only a subset of awaitable headers, so the uncovered session-side awaitable headers
can silently drag a `std::mutex` / `std::shared_mutex` into a `co_await` closure (the 013 burn shape)
without the gate catching it. After this change the gate's header list covers those uncovered
session-side awaitable headers, so the regression is caught at build time.

**Why this priority**: LOW, build/test-infra hardening — no production behavior changes.

**Independent Test**: Confirm the extended gate lists the previously-uncovered session-side awaitable
headers and that the gate still passes on the current tree (they are clean today).

**Acceptance Scenarios**:

1. **Given** the extended corpus-gate header list, **When** the gate runs on the current tree,
   **Then** it passes (the newly-added headers are clean today).
2. **Given** a hypothetical regression that pulls `std::mutex` into one of the newly-covered
   awaitable headers, **When** the gate runs, **Then** it fails (the gate's contract).

---

### User Story 5 - L-033-3 design-follow-up doc resolution (Priority: P3)

An operator reading the behaviors-and-limitations / FIXT docs finds the open L-033-3 wording
follow-up and the absent-`1137`-ack case resolved and consistent. Documentation only.

**Why this priority**: LOW, doc-only — no code, wire, or config change.

**Independent Test**: Confirm the L-033-3 wording and the absent-`1137`-ack case are documented in
the relevant spec/behaviors docs and are internally consistent with the shipped FIXT
`DefaultApplVerID` behavior.

**Acceptance Scenarios**:

1. **Given** the behaviors-and-limitations / FIXT docs, **When** an operator reads the L-033-3
   entry, **Then** the wording is resolved (no open placeholder) and the absent-`1137`-ack case is
   described.

---

### Edge Cases

- **US2**: sentinel as left, right, and both operands; out-of-domain exponent still rejects.
- **US3**: the `mutex_test_access` seam must force lock failure deterministically (no flake).
- **US4**: a candidate header that does NOT preprocess to `asio/awaitable.hpp` is out of the gate's
  scope (the gate flags only headers that BOTH pull `asio::awaitable` AND name a banned mutex,
  post-`-E`); such a header must not be claimed as an "uncovered awaitable header."

## Requirements *(mandatory)*

### Functional Requirements

**US2 — C-ABI sentinel (test + comment only; NO behavioral change)**
- **FR-006**: A regression test MUST pin the ratified C-ABI behavior: `fixpp_decimal_compare_checked`
  and `fixpp_decimal_equal_checked` of the `INT64_MIN` sentinel POD with a valid exponent return
  `FIXPP_ERR_OK` with ordering 0 / equal 0 (per AC-C6 / D-12 / the frozen-ABI decision). The
  production `_checked` behavior MUST NOT change.
- **FR-006a**: A cross-reference comment MUST be added at the test (and/or the `_checked` site if not
  already present) citing AC-C6 / D-12 so the behavior is not mistakenly "re-fixed".
- **FR-007**: The exponent-domain validation (`[-38,0]` → `FIXPP_ERR_DECIMAL_INVALID`) and compares
  of valid decimals MUST be unchanged.

**US3 — coverage-waiver remediation (test-only)**
- **FR-008**: The `set_next_outbound` lock-fail branch MUST be exercised by a witness that forces
  `async_lock` failure via the `mutex_test_access` seam and asserts `session_already_closed`.
- **FR-009**: The 008 `OsFile` move-constructor MUST be exercised by a witness.
- **FR-010**: The 033 lines previously deferred without per-line DA/BRDA measurement MUST be
  re-measured and either covered or given a specific re-measured waiver.

**US4 — §XV.9 corpus gate (build/test-infra)**
- **FR-011**: The §XV.9 no-`std::mutex` corpus-gate header list MUST be extended to cover the
  currently-uncovered session-side awaitable headers (those that preprocess to `asio/awaitable.hpp`),
  and MUST still pass on the current tree.

**US5 — doc**
- **FR-012**: The L-033-3 wording follow-up and the absent-`1137`-ack case MUST be resolved in the
  relevant spec/behaviors documentation; no code, wire, or config change.

**Cross-cutting**
- **FR-013**: No new configuration keys, no codegen regeneration, no new error codes, no wire or
  C-ABI behavior change.
- **FR-014**: ALL four user stories make no production behavior change (US2 = test+comment, US3 =
  test, US4 = build-gate, US5 = doc). Consequently **Gate A is not required** for this feature.

### Out of Scope

- **US1 — wire/session tag-accumulation overflow hardening** — SPLIT OUT to
  `040-inbound-tag-overflow-hardening` (a real 5-site security fix; see split notice above).
- Rejecting the `INT64_MIN` sentinel in the C-ABI `_checked` path (contradicts the ratified
  2026-05-12 frozen-ABI decision; a separate ABI-decision feature). US2 here only *pins* the
  existing behavior.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-003**: The C-ABI `_checked` compare/equal of the `INT64_MIN` sentinel (valid exponent) is
  pinned by a regression test to return `FIXPP_ERR_OK` + ordering/equal 0 (the ratified behavior);
  exponent-domain rejection and valid-operand results are unchanged. No production behavior changes. (US2)
- **SC-004**: Each of the three previously-waived coverage lines is either executed by a new witness
  or carries a specific, re-measured waiver — zero "untestable"-without-justification dispositions
  remain among the three. (US3)
- **SC-005**: The §XV.9 corpus gate covers the previously-uncovered session-side awaitable headers
  (the 7 that preprocess to `asio/awaitable.hpp`: `detail/has_flush_for_session_close`, `engine`,
  `file_store`, `memory_store`, `message_store`, `reconnect_fsm`, `retrieve_visitor`) and passes on
  the current tree. The set is clean by source inspection (`std::mutex` mentions are `[const §XV.9]`
  comment markers, stripped post-preprocess); the gate run is the final confirmation. Should any
  added header fail the gate post-preprocess, that is a real §XV.9 finding to surface (default-real). (US4)
- **SC-006**: The L-033-3 wording and absent-`1137`-ack case are resolved in docs with no open
  placeholder. (US5)

## Normative References

This feature adds **no new OFFICIAL normative FIX/FIXT coverage** — it is source-grounded hardening,
test-completeness, a build-gate extension, and a documentation wording fix. The references it
*touches* (not extends):

- `[const §X.1]` — frozen C-ABI MAJOR=1 (US2 pins existing `_checked` behavior; does not change it).
- `[const §IX.1]` — lcov DA/BRDA coverage basis (US3 improves coverage honesty).
- `[const §XV.9]` — no `std::*mutex` in awaitable headers (US4 extends the corpus gate).
- 001 AC-C6 / research.md D-12 — the ratified `_checked` sibling contract (US2 grounding).
- `[FIX-SL §4.3.7]` / S-025 — FIXT `DefaultApplVerID(1137)` (US5 doc context only).

## Assumptions

- US2's existing `_checked` sentinel behavior (`OK`+0 for a valid-exponent sentinel) is the ratified
  intent, not a defect (confirmed against 001 AC-C6 / D-12 / the code comment during planning and
  independently re-confirmed in Gate A round 1).
- The `mutex_test_access` seam already exists and can force `async_lock` failure deterministically
  for US3(a).
- The §XV.9 gate's header list lives in `tests/sync/CMakeLists.txt` and is the right place to extend
  for US4; the gate flags a header only if, post-`-E`, it BOTH pulls `asio::awaitable` AND names a
  banned mutex (so `business_messages.hpp`, which has no awaitable include, is correctly NOT in the
  uncovered set).
- US5 touches operator-facing docs only (`spec/behaviors-and-limitations.md` and the FIXT
  `DefaultApplVerID` notes).
