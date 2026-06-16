# Checklist: ABI / Layer Requirements Quality — 041 Validation Gate Wiring

**Purpose**: Unit-test the requirements governing the frozen C-ABI and architectural-layer impact of the change (the `Engine::start()` signature change + new `dict`/`wire` headers).
**Created**: 2026-06-16
**Audience**: Gate B reviewer
**Feature**: [spec.md](../spec.md)

## Requirement Completeness

- [x] CHK014 Is the C-ABI impact of the `Engine::start()` `void`→`expected_t<void>` change addressed — i.e., is it specified whether `start()` is part of the frozen C-ABI surface ([const §X.1]) or a C++-only entry point with no C wrapper? [Completeness, Spec §FR-007, Gap] — PASS: plan.md §Constitution Check `[const §X.1]` row states "Engine::start() is a C++ API; src/capi/capi.cpp exports no engine binding, so the C ABI is untouched"; research R-5 confirms "no C-ABI wrapper (src/capi/capi.cpp exports only fixpp_version_string — verified)". Independently verified: `capi.cpp` exports only `fixpp_version_string`; `c_api.h` contains no `Engine` or `start` binding. The change is C++-only with no C-ABI impact, as specified. Complete.
- [x] CHK015 Is the layout/ABI impact of the new `SessionConfig` field specified — whether `SessionConfig` is a frozen-ABI struct and whether appending a `bool` is permitted? [Completeness, Spec §FR-001, Gap] — DD-DECIDED §arch §2.3 + const §X.1: `SessionConfig` lives in `include/fixpp/session/session_config.hpp`, which is in the `session/` C++ module. Architecture §2.3 whitelist defines the frozen C-ABI surface as `include/fix/c_api.h` only (`extern "C"` opaque-handle API, `capi/` module); `SessionConfig` is a C++-only type with no C-ABI representation — appending a `bool` changes its C++ size/layout but that is NOT a frozen C-ABI change. Confirmed: `c_api.h` has no `SessionConfig` typedef or size assertion; `capi.cpp` does not include `session_config.hpp`. `[const §X.1]` frozen-ABI scope is `include/fix/c_api.h` only. Design-doc anchor: `[arch §2.1 table row 4 "session" / §2.3 whitelist / §9 "header discipline"]`.
- [x] CHK016 Are the new public headers (`dict/field_type.hpp`, `dict/table_view.hpp`, `wire/reject_reason_map.hpp`) assigned to an explicit architectural layer consistent with `architecture.md`? [Completeness, Gap] — PASS: plan.md §Project Structure assigns `dict/field_type.hpp` and `dict/table_view.hpp` to `include/fixpp/dict/` (the `dictionary` module per arch §2.1 row 2), and `wire/reject_reason_map.hpp` to `include/fixpp/wire/` (the `wire` module per arch §2.1 row 3); plan §Constitution Check "Layering" row confirms no layer inversion; arch §2.3 whitelist permits `wire → dictionary` (so `wire/reject_reason_map.hpp` depending on `dict::` types is legal) and `session → wire + dictionary`. Explicit assignment present. Complete.

## Requirement Clarity

- [x] CHK017 Is the basis for the safe signature change stated unambiguously (zero production callers; no frozen C-ABI wrapper for `start()`)? [Clarity, Spec §FR-007 Assumptions, tasks T019] — PASS: research R-5 states "zero production callers and no C-ABI wrapper (src/capi/capi.cpp exports only fixpp_version_string — verified)"; plan §Constitution Check §X.1 row repeats this; tasks T019 specifically notes "zero production callers, no C-ABI wrapper" as the migration scope justification. Unambiguous. Complete.
- [x] CHK018 Is the dependency direction between the new `table_view` type and `Dictionary` specified (Dictionary → table_view, never the reverse) so the new header does not invert an existing include layer? [Clarity, Gap] — PASS: research R-1 states "A literal dictionary → wire link edge would create the forbidden wire ↔ dictionary cycle since §2.3 already grants wire → dictionary"; plan §Structure Decision states "The new table_view realization and as_table_view() builder are dict-layer additions (the validator already depends on dict::table_view)"; arch §2.3 whitelist allows `wire → dictionary` and `session → dictionary + wire`, so placing `table_view` in `dict/` (with `Dictionary::as_table_view()` building it) and `validator.hpp` (wire layer) depending on it is the correct direction. No layer inversion. Dependency direction is specified. Complete.

## Requirement Consistency

- [x] CHK019 Is the promotion of `field_type`/`table_view` from test-only (`tests/support/mock_dict_table.hpp`) to production headers consistent with the constraint that the validator's bound-by-value contract is unchanged? [Consistency, plan RC-A] — PASS: research R-1 and plan RC-A disposition state the validator's 6-method binding surface and by-value ownership (`validator.hpp:86` `dict_` member) are unchanged; the promotion moves the type definition to production headers so the validator gets a real complete-type include (replacing mock-include-order dependency) without altering the surface or ownership. Contracts C-1 confirms the 6-method surface is identical to what the validator already calls. Consistent. Complete.
- [x] CHK020 Does the spec confirm no existing frozen-ABI symbol changes signature, size, or layout (the only public-API change is `Engine::start()`'s return type)? [Consistency, Spec §FR-007, Notes] — PASS: plan §Constraints row states "no C-ABI change"; plan §Constitution Check §X.1 row states "the C ABI is untouched. New error reasons are existing core::error / SessionRejectReason values, not new C-ABI codes"; the only change touching a public surface is `Engine::start()` return type, which is C++-only (no C-ABI wrapper — verified). `SessionConfig` bool addition changes C++ layout but `SessionConfig` is not in the frozen C-ABI (`c_api.h` has no `SessionConfig`). Confirmed consistent: zero frozen-ABI symbol changes. Complete.

## Audit Result

| Disposition | Count |
|---|---|
| PASS | 6 |
| SPEC-FIXED | 0 |
| DD-DECIDED | 1 |
| WAIVED | 0 |
| **Total** | **7** |

### SPEC-FIXED items
None.

### DD-DECIDED items
- CHK015 — anchor `[arch §2.1/§2.3 + const §X.1]`; rationale: `SessionConfig` is a C++-only type; the frozen C-ABI surface is `include/fix/c_api.h` only (opaque handles); appending a `bool` to `SessionConfig` changes C++ layout but is not a C-ABI change. Confirmed by inspecting `c_api.h` and `capi.cpp`.

### WAIVED items
None.

Anchors spot-verified:
- `[const §X.1]` — resolves at "Article X — ABI Policy §1" in `.specify/constitution.md`; defines the frozen C-ABI as `include/fix/c_api.h`.
- `[arch §2.1]` — module table in `.specify/architecture.md` §2.1 (line ~50); `session/` module = C++ only.
- `[arch §2.3]` — whitelist in `.specify/architecture.md` §2.3 (line ~109); `dictionary | core` for dict headers; `wire | core, dictionary`; `session | core, dictionary, wire…`.
- All resolve in the signed-off architecture v0.3 revision.
