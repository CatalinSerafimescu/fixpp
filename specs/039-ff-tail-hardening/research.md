# Phase 0 Research: F-f tail hardening bundle (LOW)

Four independent decisions, one per user story (US2–US5). US1 was split out to
`040-inbound-tag-overflow-hardening` (see spec.md split notice). No NEEDS CLARIFICATION remained.

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
the bare path's documented out-of-domain convention. The genuine gap is that this is *untested* —
closing that gap is in scope; changing the behavior is not. **Independently re-confirmed sound in
Gate A round 1** (both Codex and the Opus adversarial review).

**Alternatives considered**: (a) implement the reject — rejected: overrides a ratified decision,
breaks frozen ABI, needs a 2a/AC-C6 amendment → its own future feature. (b) drop US2 — rejected: the
untested-gap + re-fix-risk is real; a pin test + comment is cheap insurance.

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
`check_no_std_mutex_corpus` list at `tests/sync/CMakeLists.txt:140`. List them explicitly, matching
the existing entries' style (a local CMake-gate convention).

**Gate semantics (verified, `tools/check_no_std_mutex_in_awaitable_headers.sh`)**: a header VIOLATES
only if, **post-preprocessing (`-E`, comments stripped)**, it BOTH (a) pulls `asio::awaitable<...>`
AND (b) names one of the six banned `std::*mutex` spellings. (`using`/`typedef` aliases are an
explicit recorded out-of-scope limitation.)

**Enumeration (corrected per Gate A round 1)**: the uncovered set is the **7** headers that
preprocess to `asio/awaitable.hpp`: `detail/has_flush_for_session_close`, `engine`, `file_store`,
`memory_store`, `message_store`, `reconnect_fsm`, `retrieve_visitor`. **`business_messages.hpp` is
NOT in the set** — it self-declares "No asio::awaitable in this header" (`:30`), so it is out of the
gate's scope (Codex/Opus Gate A round 1 P2). The exact residual should be re-confirmed by running the
script's preprocess criterion during implement; note also that a few *existing* corpus entries
(`admin_messages`, `sending_time`, `session_fsm`) are no-op (no awaitable include) — harmless, but a
sign the list was not curated against the gate's own awaitable criterion.

**Clean-today check (source inspection)**: every `std::mutex`/`std::shared_mutex` occurrence in the
uncovered set is a `[const §XV.9]` **comment marker** ("NO std::mutex …"), which `-E` strips — so
they are expected to PASS. The gate run during implement is the final confirmation.

**Citation correction (Gate A round 1 P2)**: do NOT cite `[const §VI.4]` for the "glob-free list"
property — Article VI §4 is bidirectional coverage-index traceability, not a glob rule. The explicit
list style is a local CMake-gate convention; cite that, not the constitution.

**Default-real escalation**: if any added header *fails* the gate post-preprocess (a real transitive
mutex drag the comments missed), that is a genuine §XV.9 violation to surface and fix
(`fixpp::sync::async_mutex` is the sanctioned alternative) — NOT to silently exclude from the list.

## D-5 (US5) — Resolve L-033-3 wording + absent-`1137`-ack case

**Decision**: Resolve the L-033-3 entry wording (`spec/behaviors-and-limitations.md:1096`) and
document the absent-`1137`-ack case in the FIXT `DefaultApplVerID` notes. Doc only.

**Rationale**: Fable 2.2 open design follow-up; no code/wire/config change (FR-012).

**Alternatives considered**: defer to the 001-014 B&L back-fill (Tier-4 item 9) — rejected: it's a
small, self-contained wording fix that belongs with this tail.
