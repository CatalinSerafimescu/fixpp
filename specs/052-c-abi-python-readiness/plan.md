# Implementation Plan: C-ABI Python-readiness (052)

**Branch**: `052-c-abi-python-readiness` | **Date**: 2026-06-25 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `specs/052-c-abi-python-readiness/spec.md`

## Summary

Close the three gaps a pre-Python C-ABI completeness audit found blocking a pure-C/Python consumer from
the happy path, as an **additive MINOR (0.4.0 → 0.5.0)**, so PY-001 (the first real C-ABI consumer +
freeze validator) can be built before the `0→1` freeze. Three additions, **three different `[2i]`
provenances** (the design crux for Gate A): (1) **dict loader** `fixpp_dict_load_from_xml` +
`fixpp_dict_destroy` — implement the `[2i §2]`-specified-but-unbuilt symbol by "making the L-050-1 seam
real" (wrap `XmlLoader::load`); (2) **transport endpoint** `fixpp_session_config_set_tcp_endpoint` +
`fixpp_session_acceptor_bound_endpoint` — a primitive `(host,port)` session-config setter recorded as a
LOCAL Gate-A deviation (clarify A; `[2i §2]` non-goal #7 / §7.8 NOT reopened, transport handles/PoD stay
deferred); (3) **inbound field iteration** `fixpp_msg_field_count`/`fixpp_msg_field_at` over the existing
`OffsetTable` — net-new additive. **No new error codes, no `[2i §4.3]` amendment** (a simplification vs
051). A 4th group-2 symbol — `fixpp_session_config_set_reset_seqnum_policy` — is **pinned** (Gate-A r1
user decision) so the surface is a deterministic **7 symbols**, removing the prior +1 fresh-pair
establishment contingency. Technical approach is in [research.md](./research.md) D-1..D-6.

## Technical Context

**Language/Version**: C++23 (engine impl); the surface is C11-clean `extern "C"` (`include/fix/c_api/`).
**Primary Dependencies**: existing only — `fixpp::dict::XmlLoader`, `fixpp::wire` (`OffsetTable`/parser),
`fixpp::session::Engine`/`SessionConfig`. **No new third-party dependency.**
**Storage**: N/A (dict XML files already bundled at `library/dictionaries/`).
**Testing**: GoogleTest (`tests/capi/`), pure-public-header C-ABI tests; sanitizers (ASan/UBSan/TSan);
mallocnesia + counting-resource alloc gate; ABI golden + occupancy + reentrancy CI gates.
**Target Platform**: Linux (Tier 1) + Windows/MSVC (Tier 2) + libc++ (Tier 3); macOS Tier-4 TBD.
**Project Type**: C-ABI surface over a C++ FIX-engine library.
**Performance Goals**: dict load is one-time (not latency-critical); field iteration is a thin
offset-table read — `field_at` O(1), `field_count` O(1), **zero global-heap** (alias the wire buffer).
**Constraints**: additive only (no shipped-signature change, ABI layout untouched, `MAJOR` stays 0);
exception-free zero-alloc steady-state thunks (escape → `std::abort`); build caps max `-j2`, sanitizer
presets ONE AT A TIME (WSL2 OOM).
**Scale/Scope**: **7 new exported symbols** + 1 PoD type (`fixpp_msg_field_t`) + 1 C11 enum
(`fixpp_reset_seqnum_policy`) + 1 new public header (`dict.h`) + the umbrella `c_api.h` aggregation
(FR-014); a comment-only doc fix (session.h:190). The reset-policy setter is pinned (no contingency).

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

- **Article X — ABI Policy (governing).**
  - §X.1 versioned contract + Codex Gate A mandatory → **Gate A in the pipeline** (ABI-surface change).
    Additive **MINOR 0.4.0 → 0.5.0** (FR-009). No signed-off-design *amendment* this time (clarify A:
    GAP-002 is a recorded LOCAL deviation, `[2i]` not reopened) — Gate A reviews the recorded deviation.
  - §X.2 no C++ symbol leakage → all 7 new symbols are plain `fixpp_*` `extern "C"`; `fixpp_capi.map`
    (`fixpp_*; local: *`) + per-PR nm gate; each appended to `tests/abi/golden/fixpp_capi_symbols.txt`
    (FR-009). The new `dict.h` is C11-clean (only `<stdint.h>`/`<stddef.h>` + handle/error/export
    headers) and is aggregated by the umbrella `c_api.h` (FR-014). **PASS by construction.**
  - §X.3 decimal boundary PoD frozen → no decimal surface touched. **PASS (N/A).**
  - §X.4 bounded enum + reserved ranges + audit + occupancy → **ZERO new error codes** (D-5);
    `error_codes_v1.txt`, `[2i §4.3]`, and `check_capi_occupancy.sh` are **UNCHANGED**. **PASS (no diff —
    Gate A should not look for an occupancy change).**
  - §X.5 per-symbol reentrancy → every new symbol carries exactly one class (FR-008),
    `check_capi_reentrancy.sh` gate: `fixpp_dict_load_from_xml` = `FIXPP_SINGLE_THREAD`;
    `fixpp_dict_destroy` = `FIXPP_THREAD_SAFE` (full-critical-section process-global lock over `{tag_
    check, shared_ptr release, tag_=DEAD, dead-shell insert}` — not a registry-only lock — THREAD_SAFE per
    the `[2i §4.2.1]` line-415 every-`*_destroy`-is-thread-safe mandate); `fixpp_session_config_set_tcp_endpoint`
    = `FIXPP_SINGLE_THREAD`; `fixpp_session_config_set_reset_seqnum_policy` = `FIXPP_SINGLE_THREAD`
    (config setter); `fixpp_session_acceptor_bound_endpoint` = `FIXPP_THREAD_SAFE`;
    `fixpp_msg_field_count`/`fixpp_msg_field_at` = `FIXPP_REQUIRES_SESSION_LOCK` (the single conservative
    inbound-read class, matching `[2i §4.6]`). **PASS.**
  - §X.6 ABI-affecting controls → `/clarify` ✔, `/analyze` pending, Codex Gate A pending, `/plan`
    sign-off pending. **On track.**
- **Article XX — amendment process.** This feature does **NOT** reopen `[2i]` (clarify A — the
  primitive-only endpoint setter sidesteps non-goal #7's transport-*accessor* scope + the §7.8 PoD
  deferral; recorded as a LOCAL deviation per the 051 FR-008a precedent). **PASS — no amendment; Gate A
  reviews the recorded deviation prose.** (Contrast 051, which DID reopen §4.3.)
- **Article VIII §5 — exception-free zero-alloc steady-state hot path.** Field iteration reads the
  offset table and aliases the wire buffer — zero global-heap, no exception escapes (escape → abort).
  The endpoint setter / bound-endpoint readback are trivial. Only `fixpp_dict_load_from_xml` is
  construction-time (translates `XmlLoader` throws). **PASS by design**; iteration alloc-guarded under
  mallocnesia + counting-resource ([[feedback_tracking_pmr_resource_false_pass]]).
- **Article IX — coverage/sanitizers.** Per-PR ≥95% line / ≥85% branch on `src/capi/`; ASan/UBSan/TSan
  Tier-1. The live-session SC-001 round-trip is the threading risk surface → multi-threaded harness,
  sanitizer-gated ([[feedback_single_threaded_harness_masks_strand_races]]). **PASS (planned).**
- **No new dependency / no codegen / no new wire-format or `reason_class` / `fixpp_error_t` surface.**
  **PASS.**

**No violations. Complexity Tracking table not required.** The one structural novelty (a public dict
loader + the first fully-public-C-ABI live round-trip) reuses existing engine machinery; it is isolated
to `src/capi/` + a new `dict.h`.

**Post-Phase-1 re-check**: still no violation. Phase 1 confirmed: (a) iteration wraps the existing
`OffsetTable` (D-1) — no new parsing — and the inbound/clone view is **Index-mode** (`capi_internal.hpp:227`),
so D-1a is **resolved**, not carried to implement; (b) the dict loader reuses the seam's proven ownership
model + adds a `tag_` tombstone (D-2 / E-1); (c) the endpoint readback reuses the established
`state_->engine_` reach (D-3). The prior D-4 fresh-pair-establishment risk is **closed**: the
reset-policy setter is pinned (Gate-A r1 user decision), so the surface is a deterministic **7 symbols**
and there is no open implement-time symbol-count question.

## Project Structure

### Documentation (this feature)

```text
specs/052-c-abi-python-readiness/
├── spec.md
├── plan.md            # this file
├── research.md        # D-1..D-6 (source-verified)
├── data-model.md      # entities: fixpp_dict, endpoint config, fixpp_msg_field_t
├── quickstart.md      # the pure-C happy-path client (the SC-001 witness shape)
├── contracts/
│   ├── dictionary-loader.md      # fixpp_dict_load_from_xml / fixpp_dict_destroy
│   ├── transport-endpoint.md     # set_tcp_endpoint / acceptor_bound_endpoint
│   └── field-iteration.md        # fixpp_msg_field_t / field_count / field_at
└── checklists/
    └── requirements.md
```

### Source (the implementation surface)

```text
include/fix/
├── c_api.h            # EDITED — #include <fix/c_api/dict.h> (umbrella aggregation, FR-014);
│                      #          do NOT propagate the stale FIXPP_VERSION_*/"0.2.0" comment
└── c_api/
    ├── dict.h         # NEW — fixpp_dict_load_from_xml, fixpp_dict_destroy ([2i]-reserved name; coexists
    │                  #       with 2c FIXPP_APPL_VER_* constants — shared header, no collision)
    ├── session.h      # +set_tcp_endpoint, +acceptor_bound_endpoint, +set_reset_seqnum_policy
    │                  #  (+ fixpp_reset_seqnum_policy enum); fix stale :190 doc (FR-013)
    ├── message.h      # +fixpp_msg_field_t, +fixpp_msg_field_count, +fixpp_msg_field_at
    └── version.h      # MINOR 4 → 5
src/capi/
├── dictionary.cpp     # NEW — wraps XmlLoader::load (construction thunk); fixpp_dict_destroy tombstone
│                      #       (tag_→DEAD + full-critical-section lock over the bounded dead-shell registry)
├── config.cpp         # +set_tcp_endpoint (writes reconnect_endpoint + transport_send placeholder),
│                      #  +set_reset_seqnum_policy (writes reset_seqnum_policy_field)
├── session.cpp        # +acceptor_bound_endpoint (via state_->engine_)
├── message_read.cpp   # +field_count/+field_at (wrap OffsetTable entries(); Index-mode view confirmed)
└── capi_internal.hpp  # fixpp_dict gains a tag_ liveness token (E-1 tombstone)
tests/capi/
├── dictionary_load_test.cpp        # NEW — US1 (load FIX44.xml, bad path, NULL, double-destroy)
├── public_roundtrip_test.cpp       # NEW — SC-001 two-engine pure-public-header live round-trip
└── message_field_iteration_test.cpp# NEW — US3 + zero-heap alloc gate (SC-002 repeating-group/SC-003)
tests/abi/golden/fixpp_capi_symbols.txt   # +7 symbols
tools/  (check_capi_reentrancy.sh adds 7 entries; check_capi_occupancy.sh UNCHANGED)
```

**Structure Decision**: standard `src/capi/` + `include/fix/c_api/` C-ABI layout (as 049/050/051). One
new translation unit (`dictionary.cpp`) + one new header (`dict.h`) aggregated by the umbrella `c_api.h`;
the rest are additive edits to existing capi TUs. Tests are new pure-public-header files (the SC-001
two-engine witness must NOT use the seams).

## Complexity Tracking

*No Constitution Check violations — table omitted.*

## Gate A

- Round 1 applied 2026-06-25: Codex P1=2 P2=3 P3=1; Opus post-judging P1=1 P2=5 P3=1; rewrite addresses Root cause #1 (dict tombstone), #2 (SC-001 two-engine + preemptively-pinned reset-policy 7th symbol — user decision), #3 (iteration multiset/clone), + umbrella dict.h aggregation + symbol-count→7. Reviews: research/reviews/codex_052-c-abi-python-readiness_gate_a_review.md, research/reviews/opus_052-c-abi-python-readiness_gate_a_adversarial_review.md.
- Round 2 applied 2026-06-25: Codex P1=0 P2=2 P3=2; Opus post-judging P1=0 P2=2 P3=2; rewrite specifies the dict_destroy full-critical-section lock + TSan double-destroy witness, carries 051 FR-018 clone-read runtime-THREAD_SAFE reconciliation into FR-008 for field_count/field_at + a clone-iteration cross-strand witness, and strikes two stale phrasings (set-equality Independent-Test sentence, Group-2 "mechanism TBD" heading). Reviews: research/reviews/codex_052-c-abi-python-readiness_gate_a_2_review.md, research/reviews/opus_052-c-abi-python-readiness_gate_a_2_adversarial_review.md.
