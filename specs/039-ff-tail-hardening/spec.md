# Feature Specification: F-f tail hardening bundle

**Feature Branch**: `039-ff-tail-hardening`
**Created**: 2026-06-15
**Status**: Draft
**Input**: User description: "F-f tail hardening bundle — five independent, separately-witnessed concern groups from the Fable F-f release-gate tail (the remaining LOW items after 038; US1 reclassified MED on a reachability check)."

## Clarifications

### Session 2026-06-15

- Q: On an out-of-range/overflowing tag, the two decode twins disposition differently today (Index = whole-message reject via `entries_.clear()`; Scan = truncate iteration via `done_=true`). What should US1 do? → A: Preserve each mode's existing disposition. The fix only relocates *detection* earlier (during digit accumulation); it does not change *which* disposition each mode applies. The intra-engine asymmetry pre-exists this fix for every malformed-field case, so unifying it is a separate, out-of-scope concern. The US1 witness asserts per-mode: Index → forged field's whole message is all-fields-absent; Scan → forged field is never yielded (iteration terminates at it).
- Q: US2 (C-ABI sentinel) — the Fable finding (reject `INT64_MIN` in `_checked` compare) was found during planning to contradict a user-ratified frozen-ABI decision (001 `/clarify` Session 2026-05-12 / research.md D-12 / the explicit `src/capi/decimal.cpp:43-48` "do not unify without an ABI decision" comment): `_checked` deliberately validates the **exponent domain `[-38,0]` only**, and a sentinel with `exponent=0` is in-domain by that ratified contract. How should US2 proceed? → A: Downgrade to test + comment only — make NO behavioral change. Add a regression test pinning the ratified behavior (`fixpp_decimal_compare_checked`/`_equal_checked` of the `INT64_MIN` sentinel with a valid exponent returns `FIXPP_ERR_OK` with ordering 0 / equal 0) and a cross-reference comment citing AC-C6 / D-12 so the behavior is not "re-fixed". The behavioral reject-the-sentinel change, if ever wanted, is a separate ABI-decision feature (amend 2a / AC-C6), explicitly OUT OF SCOPE here.

## User Scenarios & Testing *(mandatory)*

<!--
  These five user stories are the residual Fable F-f release-gate tail after 038 (PR #122)
  shipped the session subset. Each is an INDEPENDENT, separately-witnessed concern group on a
  different surface (wire codec / C-ABI / test-completeness / build-gate / docs). They are bundled
  into ONE feature only to keep the per-item Gate-A/Gate-B process cost bearable for LOW items —
  NOT because they share a surface. Implementation discipline: one implementer invocation per
  user story (the phase-implementer-sonnet runaway-scope guard).
-->

### User Story 1 - Wire tag-accumulation overflow guard (Priority: P1)

A FIX counterparty (TLS-authenticated, CompID↔identity bound per 015) sends an inbound frame
containing a forged multi-digit tag whose decimal value exceeds the 16-bit FIX tag space — e.g.
`4294967330=...` (= 2³² + 34). Today the field-decode path accumulates the tag into a 32-bit
integer that **wraps** to a small value (34) **before** the existing range check fires, so the
range check passes and the forged field is inserted into the live field-lookup table aliased to a
small, security-relevant tag (34 MsgSeqNum, 49/56 CompID, 52 SendingTime, …). After this change
the decoder refuses any tag whose accumulation exceeds the 16-bit tag space *during* the digit
scan and fails the field closed — exactly as the framer's `BodyLength` digit accumulation already
does (`src/wire/framer.cpp:120`, a pre-multiply bound check inside the digit loop). This closes the
two un-hardened twins: the eager `Index`-mode table build and the lazy `Scan`-mode field iterator.

**Why this priority**: This is the only item in the bundle that is a real, live wire-integrity
defect (a forged-tag aliasing / parse-differential vector reachable from any authenticated peer's
inbound frame), not a test/doc chore. It was reclassified LOW→MED on a reachability check: the
field-lookup table it corrupts is the live inbound decode path the session uses for every received
field. Bounded to MED (not HIGH) only because the peer is TLS-authenticated — there is no anonymous
MITM — but a malicious or non-conforming authenticated counterparty reaches it directly.

**Independent Test**: Construct a frame carrying a forged out-of-range tag whose 32-bit wrap aliases
to a chosen small tag (e.g. 34), decode it in BOTH `Index` and `Scan` access modes, and assert the
forged field is REJECTED (the field is not queryable under the aliased small tag and the decode
fails closed with the existing out-of-range disposition). Requires no other user story.

**Acceptance Scenarios**:

1. **Given** an inbound frame whose tag token is `4294967330` (32-bit wrap → 34), **When** the
   frame is decoded in `Index` mode, **Then** the decode fails closed with the out-of-range tag
   disposition and no entry is queryable under tag 34 from the forged field.
2. **Given** the same frame, **When** it is decoded in `Scan` mode (lazy `field_iterator`), **Then**
   iteration terminates / rejects at the offending field with the same out-of-range disposition,
   and the forged field is never yielded as tag 34.
3. **Given** an inbound frame with a legitimate maximal in-range tag (`65535`), **When** it is
   decoded in either mode, **Then** it decodes exactly as today (no regression on conforming tags).
4. **Given** an inbound frame with a forged tag that does NOT wrap but is simply out of range
   (e.g. `70000`), **When** decoded, **Then** it is rejected as today (the pre-existing
   `tag > 0xFFFF` path is preserved).

---

### User Story 2 - C-ABI decimal sentinel behavior pinned (Priority: P3)

The C-ABI `_checked` compare/equal entry points, by a **user-ratified frozen-ABI decision** (001
`/clarify` Session 2026-05-12 / research.md D-12; explicit `src/capi/decimal.cpp:43-48` comment),
deliberately validate only the **exponent domain `[-38,0]`** — they do NOT reject the `INT64_MIN`
sentinel mantissa, because routing through `from_pod` would silently tighten the frozen C-ABI
(`[const §X.1]`, MAJOR=1). A sentinel POD with a valid exponent is therefore *in-domain* for
`_checked` and returns `FIXPP_ERR_OK` with ordering/equal 0 (the bare path's documented
out-of-domain `0`). This behavior is currently **untested** — a maintainer could mistake it for a
defect and "re-fix" it, breaking the frozen ABI. After this story the behavior is pinned by a
regression test and a cross-reference comment; **no behavior changes.**

**Why this priority**: LOW, test + doc-comment only. The Fable F-f ⑤.5 finding (reject the sentinel)
was found during planning to contradict the ratified decision; the actual behavioral change is an
ABI decision and is OUT OF SCOPE here. The in-scope action is to close the genuine *untested* gap
and prevent an accidental future regression.

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

### User Story 3 - Coverage-waiver remediation (Priority: P3)

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

### User Story 4 - §XV.9 no-std-mutex corpus-gate extension (Priority: P3)

A maintainer adds a new session-side awaitable header. Today the §XV.9 no-`std::mutex`-in-a-coroutine
corpus gate pins only 8 of 28 awaitable headers, so 7 uncovered session-side awaitable headers can
silently drag a `std::mutex` / `std::shared_mutex` into a `co_await` closure (the 013 burn shape)
without the gate catching it. After this change the gate's header list covers those uncovered
session-side awaitable headers, so the regression is caught at build time.

**Why this priority**: LOW, build/test-infra hardening — no production behavior changes; closes a
latent gate gap before it can re-admit the 013 burn.

**Independent Test**: Confirm the extended gate lists the previously-uncovered session-side awaitable
headers and that the gate still passes on the current tree (they are clean today).

**Acceptance Scenarios**:

1. **Given** the extended corpus-gate header list, **When** the gate runs on the current tree,
   **Then** it passes (the newly-added headers are clean today).
2. **Given** a hypothetical regression that pulls `std::mutex` into one of the newly-covered
   awaitable headers, **When** the gate runs, **Then** it fails (demonstrated by a scoped negative
   check or documented as the gate's contract).

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

- **US1 — exact boundary**: tag token `65535` (max valid) decodes; `65536` rejects; a token that
  wraps the 32-bit accumulator to a value `≤ 0xFFFF` (e.g. `4294967330`) must reject, not alias.
- **US1 — leading zeros / very long token**: a token like `000000000034` (in-range value, many
  digits) must still decode as 34 — the guard triggers on *accumulated value* exceeding the tag
  space, not on digit count, so legitimate zero-padded tags are unaffected.
- **US1 — Length+Data interaction**: the forged-tag rejection must occur during tag accumulation,
  before any Length+Data fixed-byte-count handling keyed on the (aliased) tag.
- **US2**: sentinel as left, right, and both operands.
- **US3**: the `mutex_test_access` seam must force lock failure deterministically (no flake).

## Requirements *(mandatory)*

### Functional Requirements

**US1 — wire tag-overflow guard**
- **FR-001**: The inbound field decoder MUST reject any field whose tag token's decimal value
  exceeds the 16-bit FIX tag space (`> 0xFFFF`), detecting the overflow *during* digit accumulation
  so a value that would wrap a fixed-width accumulator is never admitted.
- **FR-002**: The guard MUST be applied at BOTH decode paths — the eager `Index`-mode table build
  (`src/wire/offset_table.cpp`) and the lazy `Scan`-mode field iterator
  (`include/fixpp/wire/parser.hpp`).
- **FR-003**: On rejection the decoder MUST fail closed using **each mode's existing** out-of-range
  tag disposition — Index mode: `status_ = err_tag_out_of_range()` + `entries_.clear()` (whole
  message → all fields absent); Scan mode: `done_ = true` (iteration terminates at the offending
  field) — introducing no new error code and NOT changing which disposition a mode applies (the fix
  only relocates *detection* to occur during accumulation).
- **FR-003a**: A behaviors-and-limitations row MUST be added documenting the forged-tag
  overflow/alias hardening and its per-mode disposition.
- **FR-004**: Legitimate in-range tags (including the maximal `65535` and zero-padded forms) MUST
  decode exactly as before (no behavioral regression on conforming input).
- **FR-005**: A forged out-of-range tag MUST NOT be queryable under any aliased small tag after
  decode in either access mode.

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
- **FR-011**: The §XV.9 no-`std::mutex` corpus gate header list MUST be extended to cover the
  currently-uncovered session-side awaitable headers, and MUST still pass on the current tree.

**US5 — doc**
- **FR-012**: The L-033-3 wording follow-up and the absent-`1137`-ack case MUST be resolved in the
  relevant spec/behaviors documentation; no code, wire, or config change.

**Cross-cutting**
- **FR-013**: No new configuration keys, no codegen regeneration, and no new error codes beyond
  reusing the existing out-of-range tag disposition.
- **FR-014**: US2, US3, US4, and US5 MUST make no production behavior change (US2 = test+comment;
  US3 = test; US4 = build-gate; US5 = doc). Only US1 changes production behavior (the wire guard).

### Out of Scope

- The resend-replay `toApp`-bypass behavioral change (Fable F-f ① — its own future feature).
- The L-033-5 "A+" `open()`-config-load-failure recommendation (Fable F-f ⑥b — its own future
  feature; only the L-033-3 *wording* follow-up is in scope here).
- **Rejecting the `INT64_MIN` sentinel in the C-ABI `_checked` path** (the original Fable F-f ⑤.5
  ask) — it contradicts the ratified 2026-05-12 frozen-ABI decision; any such change is a separate
  ABI-decision feature (amend 2a / AC-C6), not this bundle. US2 here only *pins* the existing
  behavior.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: A forged inbound tag token that 32-bit-wraps to a small security-relevant tag is
  rejected in 100% of cases across both `Index` and `Scan` decode modes; the forged field is never
  queryable under the aliased tag. (US1)
- **SC-002**: All legitimate in-range tags (boundary `65535`, zero-padded forms, existing corpus)
  decode byte-identically to the pre-change behavior — zero regressions in the wire-codec test
  suite. (US1)
- **SC-003**: The C-ABI `_checked` compare/equal of the `INT64_MIN` sentinel (valid exponent) is
  pinned by a regression test to return `FIXPP_ERR_OK` + ordering/equal 0 (the ratified behavior);
  exponent-domain rejection and valid-operand results are unchanged. No production behavior changes. (US2)
- **SC-004**: Each of the three previously-waived coverage lines is either executed by a new witness
  or carries a specific, re-measured waiver — zero "untestable"-without-justification dispositions
  remain among the three. (US3)
- **SC-005**: The §XV.9 corpus gate covers the previously-uncovered session-side awaitable headers
  and passes on the current tree. The uncovered set is clean by source inspection (all `std::mutex`
  mentions in them are `[const §XV.9]` comment markers, stripped post-preprocess); the gate run is
  the final confirmation. Should any added header fail the gate post-preprocess, that is a real
  §XV.9 finding to surface (default-real), not silently dropped. (US4)
- **SC-006**: The L-033-3 wording and absent-`1137`-ack case are resolved in docs with no open
  placeholder. (US5)

## Assumptions

- The FIX tag space is 16-bit (`0 < tag ≤ 0xFFFF`); the field type stores tags as 16-bit, so any
  tag `> 0xFFFF` is already invalid by design — the defect is only that the *overflow* slips past
  the post-accumulation check.
- The framer's `BodyLength` digit accumulation (`src/wire/framer.cpp:120`) is the reference shape:
  a pre-multiply bound check inside the digit loop (`if (acc > (LIMIT - digit) / 10) reject;`). The
  two decode twins should mirror this shape (bound = `0xFFFF`). The framer's own length accumulation
  is confirmed already-bounded (no length-overflow twin to fix).
- The peer is TLS-authenticated with CompID↔identity binding (015), bounding US1 to a
  malicious/non-conforming authenticated counterparty rather than an anonymous MITM.
- The `mutex_test_access` seam already exists and can force `async_lock` failure deterministically
  for US3(a).
- The §XV.9 gate's header list lives in `tests/sync/CMakeLists.txt` (T066) and is the right place to
  extend for US4.
- US5 touches operator-facing docs only (`spec/behaviors-and-limitations.md` and the FIXT
  `DefaultApplVerID` notes).
