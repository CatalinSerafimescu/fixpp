# Phase 0 Research: F-f tail hardening bundle

Five independent decisions, one per user story. No NEEDS CLARIFICATION remained after `/clarify`.

## D-1 (US1) — Tag-overflow guard: detect during accumulation, reuse existing disposition

**Decision**: Add the `> 0xFFFF` check *inside* the digit-accumulation loop (per-digit / pre-multiply
guard) at both decode twins, reusing each mode's existing out-of-range disposition.

- Site A — `src/wire/offset_table.cpp` (Index mode): the loop at `:160-170` accumulates
  `tag = tag*10 + digit` (uint32); the range check `if (tag > 0xFFFFU)` is at `:176`, **after** the
  loop. A 10-digit token (e.g. `4294967330`) wraps uint32 to `34` *before* `:176`, so the check
  passes and the forged field is inserted aliased to tag 34. Fix: guard inside the loop so a token
  that would exceed `0xFFFF` is rejected before it can wrap. Disposition unchanged:
  `status_ = err_tag_out_of_range(); entries_.clear(); return;`.
- Site B — `include/fixpp/wire/parser.hpp` `field_iterator::advance` (Scan mode): the loop at
  `:334-342` accumulates into a uint32 `tag` with no range check at all; the value is truncated to
  uint16 at `:352/:355`. Fix: same in-loop guard; disposition unchanged: `done_ = true; return;`.

**Rationale**: The minimal-change / surgical principle — the defect is purely *when* the overflow is
detected, not *which* disposition applies. `err_tag_out_of_range()` (`include/fixpp/wire/errors.hpp:55`
→ `error::wire_tag_out_of_range`) already exists; no new error code (FR-003). **Reference shape
(verified):** the framer's `BodyLength` digit accumulation at `src/wire/framer.cpp:120` already does
exactly this — `if (body_length > ((max_frame_bytes - digit) / 10)) reject;` *inside* the digit loop,
before `body_length = body_length*10 + digit`. The two decode twins mirror this with bound `0xFFFF`.
(The earlier "`scan_frame_header`" name was wrong — no such symbol exists; the real guard is in
`framer.cpp`. The framer's own length accumulation is confirmed bounded, so there is no
length-overflow twin to fix — only the two *tag*-accumulation twins.)

**Overflow arithmetic (verified)**: any token that eventually wraps uint32 must first cross `0xFFFF`
(from any value ≤ `0xFFFF`, one more digit yields ≤ `655359` < 2³², so no single step wraps from the
in-range region). Therefore an in-loop `> 0xFFFF` check provably catches every wrapping token before
the wrap — the fix is complete.

**Clarified (Session 2026-06-15)**: preserve each mode's existing disposition (Index =
whole-message-absent; Scan = iteration-terminate). The intra-engine asymmetry pre-exists this fix
for *every* malformed-field case, so unifying it is out of scope.

**Alternatives considered**: (a) widen the accumulator to uint64 then check — still needs an in-loop
or post-loop bound and doesn't remove the wrap risk for pathological lengths; rejected for not being
the surgical fix. (b) Unify both modes to whole-message rejection — rejected (clarify): wider blast
radius on Scan consumers, separate concern.

## D-2 (US2) — Pin the ratified frozen-ABI behavior; do NOT change it

**Decision**: Add a regression test (in `tests/core/decimal_capi_error_test.cpp`) asserting that
`fixpp_decimal_compare_checked` / `fixpp_decimal_equal_checked` of the `INT64_MIN` sentinel POD with
a valid exponent return `FIXPP_ERR_OK` with ordering 0 / equal 0, plus a cross-reference comment.
**No production change.**

**Rationale**: The Fable F-f ⑤.5 ask ("reject the sentinel in `_checked`") contradicts a
**user-ratified frozen-ABI decision**: 001 spec AC-C6 + `/clarify` Session 2026-05-12, research.md
D-12, and the explicit `src/capi/decimal.cpp:43-48` comment — `in_canonical_domain` validates the
**exponent domain `[-38,0]` only** and deliberately does NOT reject `mantissa == INT64_MIN`, because
routing through `from_pod` "would silently tighten the frozen C-ABI contract — do not unify without
an ABI decision." The C-ABI is frozen at MAJOR=1 (`[const §X.1]`). The observable `OK+0` result is
the bare path's documented out-of-domain convention (`fixpp_decimal_compare` returns 0 when
`from_pod` rejects an input). The genuine gap Fable found is that this is *untested* — closing that
gap is in scope; changing the behavior is not.

**Alternatives considered**: (a) implement the reject — rejected: overrides a ratified decision,
breaks frozen ABI, needs a 2a/AC-C6 amendment + ABI-version decision → its own future feature. (b)
drop US2 entirely — rejected: the untested-gap + re-fix-risk is real; a pin test + comment is cheap
insurance.

**Note on layering**: C++ core `compare()` (`src/core/decimal.cpp:241`) orders the sentinel as
*greater than every finite value* (AC-C2); the C-ABI's `0` is a *separate* documented convention.
Both are intentional; the test pins the C-ABI layer only.

## D-3 (US3) — Cover three reachable "untestable"-waived lines

**Decision**:
- (a) `src/session/seqnum_manager.cpp:188` `set_next_outbound` lock-fail branch (returns
  `session_already_closed`): witness forces `async_lock` failure via the existing `mutex_test_access`
  seam (`include/fixpp/session/seqnum_manager.hpp`) and asserts the error.
- (b) `OsFile` move-ctor (`src/session/file_store.cpp:401` POSIX / `:503` Windows): witness exercises
  the move and asserts the moved-from fd/handle is invalidated.
- (c) 033 lines deferred without per-line DA/BRDA: re-measure; cover or record a specific re-measured
  waiver citing the exact lines.

**Rationale**: All three are reachable test-completeness gaps, no production change (FR-014). The
`mutex_test_access` seam and move-ctors already exist; (c) is a measurement + disposition.

**Alternatives considered**: leaving the waivers — rejected: they were dispositioned "untestable"
but are reachable; honest coverage record per `[const §IX.1]`.

## D-4 (US4) — Extend the §XV.9 corpus gate to the uncovered awaitable headers

**Decision**: Add the uncovered session-side awaitable-including headers to the explicit
`check_no_std_mutex_corpus` list at `tests/sync/CMakeLists.txt:140`. List them explicitly (glob-free
per `[const §VI.4]`), matching the existing entries' style.

**Gate semantics (verified, `tools/check_no_std_mutex_in_awaitable_headers.sh`)**: a header VIOLATES
only if, **post-preprocessing (`-E`, comments stripped)**, it BOTH (a) pulls `asio::awaitable<...>`
AND (b) names one of the six banned `std::*mutex` spellings. (`using`/`typedef` aliases are an
explicit recorded out-of-scope limitation.)

**Enumeration (done)**: 15 awaitable-including headers under `include/fixpp/{session,core/sync}`; the
corpus currently lists 8 (`async_mutex`, `async_lock_via_session_executor`, `fix_time`,
`admin_messages`, `sending_time`, `seqnum_manager`, `session_fsm`, `session`). **Uncovered (8)**:
`business_messages`, `detail/has_flush_for_session_close`, `engine`, `file_store`, `memory_store`,
`message_store`, `reconnect_fsm`, `retrieve_visitor`.

**Clean-today check (source inspection, done)**: every `std::mutex`/`std::shared_mutex` occurrence in
the uncovered set is a `[const §XV.9]` **comment marker** ("NO std::mutex …"), which `-E` strips — so
they are expected to PASS. The gate run during implement is the final confirmation.

**Rationale**: The uncovered headers pass today but ship ungated — the 013 burn shape (an awaitable
header silently dragging a banned mutex into a `co_await` closure). Strictly additive; the gate must
still pass on the current tree (SC-005). No production change.

**Default-real escalation**: if any added header *fails* the gate post-preprocess (a real transitive
mutex drag the comments missed), that is a genuine §XV.9 violation to surface and fix
(`fixpp::sync::async_mutex` is the sanctioned alternative) — NOT to silently exclude from the list.

**Alternatives considered**: glob the directory — rejected (`[const §VI.4]` glob-free corpus).

## D-5 (US5) — Resolve L-033-3 wording + absent-`1137`-ack case

**Decision**: Resolve the L-033-3 entry wording (`spec/behaviors-and-limitations.md:1096`) and
document the absent-`1137`-ack case in the FIXT `DefaultApplVerID` notes. Doc only.

**Rationale**: Fable 2.2 open design follow-up; no code/wire/config change (FR-012).

**Alternatives considered**: defer to the 001-014 B&L back-fill (Tier-4 item 9) — rejected: it's a
small, self-contained wording fix that belongs with this tail, not a release-gate sweep.
