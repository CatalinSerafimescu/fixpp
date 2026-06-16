# Phase 0 Research: Validation Gate Wiring

Resolves the design forks surfaced while planning. Each item: Decision / Rationale / Alternatives.

## R-1 — Production `table_view`: how to feed the validator from `Dictionary`

**Finding**: `wire::dictionary_driven_validator` binds a `fixpp::dict::table_view` **by value** (`validator.hpp:86`) and calls six methods on it: `field_valid_for(msg_type, tag)`, `enum_valid(tag, value)`, `required_fields(msg_type)`, `group_first_field(no_tag)`, `group_member_tags(no_tag)`, `field_type_of(tag)`. `table_view` is a **2c-owned value type that has no production instance** — only the test mock (`tests/support/mock_dict_table.hpp`) exists, and `Dictionary::as_table_view()` is explicitly noted **deferred** ("OUT of scope this PR") in `dictionary.hpp:18`. `Dictionary` natively provides `field_valid_for` ✓, `required_fields` ✓, `group_first_field` ✓, but **not** `enum_valid`, `field_type_of`, or `group_member_tags` (it has `group_fields` → `span<FieldRef>`, not `span<uint16_t>`), and `field_ref().type` is the 29-value `field_data_type`, not the 7-value `field_type`.

**Decision**: Realize a minimal production `fixpp::dict::table_view` value type (header in `include/fixpp/dict/table_view.hpp`) exposing exactly the 6-method surface the validator binds, owning its tables, and add the deferred `Dictionary::as_table_view()` builder that populates it from a `Dictionary`:
- `field_valid_for` / `required_fields` / `group_first_field` → forwarded/copied directly from `Dictionary`.
- `group_member_tags(no_tag)` → precomputed at build time by projecting `Dictionary::group_fields(no_tag)`'s `FieldRef` spans to `uint16_t` tag arrays, stored in the `table_view` (stable backing for the returned span).
- `field_type_of(tag)` → a global tag→`field_type` map built at construction by mapping `field_data_type`(29) → `field_type`(7); FIX field types are global per tag, so a tag-keyed table is well-defined (R-1a confirms the source).
- `enum_valid(tag, value)` → **returns `true`** (Phase-1; `enum_table_index` is unbacked — FR-005, 2c-deferred).
- All table construction happens **once** at `as_table_view()` time (config/session setup), never per message → §XV.1-clean.

**Rationale**: This is the memory-note "path (b)" verdict — the production `Dictionary` covers 5 of 6 checks today; only enum-value tables are missing. A value-type `table_view` matches the validator's existing by-value binding with no validator-surface change.

**Alternatives**: (a) Build the full 2c `table_view` with enum tables now — rejected: enum tables are a separate, larger 2c work item (FR-005 scopes them out). (b) Change the validator to bind `Dictionary` directly — rejected: inverts the wire→dict-via-`table_view` indirection that 2c owns and would require re-deriving the 3 missing methods inside the validator anyway.

### R-1a — source for global `field_type_of(tag)`

**Decision**: build the tag→`field_type` map from `Dictionary`'s global field metadata (the `field` / `field_by_name` descriptive surface noted in `dictionary.hpp:14-15`). **TASKS prerequisite**: confirm at implementation a global (msg-type-independent) field-type accessor exists on `Dictionary`; if only `field_ref(msg_type, tag)` is available, derive the global map by unioning across message types at build time (a tag's type is invariant across messages in FIX). Either way the map is built once.

## R-2 — Where the validate gate runs (parse placement vs the seqnum gate)

**Finding**: today `on_inbound_frame` runs only `scan_frame_header` early (`session.cpp:2308`); the **seqnum gate fires at `session.cpp:2730`** (`seqnum_mgr_.check_inbound`), and the **full `MessageView<Index>` is built later** inside `parse_and_dispatch_` (`session.cpp:309`), after the seqnum gate. The clarified requirement (QuickFIX parity, `Session.cpp:1218-1229`) is **validate BEFORE the seqnum gate**.

**Decision**: When (and only when) `validate_inbound_messages` is set, build the `MessageView<Index>` early (top of `on_inbound_frame`, after framing/`scan_frame_header`) and run `validator.validate()` **before** the seqnum gate; on failure, emit `Reject(35=3, reason)` and return without advancing the sequence number; on success, fall through to the existing guards/dispatch. When the flag is unset (default), **none of this runs** — the path is the current one, untouched.

**Decision (double-parse)**: the MVP accepts that the enabled path parses the frame twice (once for validate, once in `parse_and_dispatch_` for dispatch). Threading the already-built `MessageView` through the 6 `parse_and_dispatch_` call sites ("unify-the-parse") is the cleaner target but touches a shared template across 5 inbound + 1 outbound site and is higher-risk; it is recorded as a **follow-up optimization**, not MVP scope. The double parse is opt-in, uses the existing PMR arena (no extra heap), and is acceptable for a conformance mode.

**Rationale**: This split is what makes US1 (parity) and US2 (zero-cost default) simultaneously true. Parsing twice on the opt-in path is a CPU cost, not an alloc/correctness issue, and is invisible at default.

**Alternatives**: (a) validate after the seqnum gate (reuse the dispatch parse) — rejected: contradicts the clarified QuickFIX-parity ordering and would let a malformed message advance seqnum state. (b) unify-parse in this feature — deferred as the optimization above to keep the change surgical and Gate-A-reviewable.

## R-3 — `wire_*` → `SessionRejectReason` mapping + reject emission

**Finding**: the validator returns `core::error::wire_*` values: `wire_required_field_missing=38`, `wire_header_out_of_order=39`, `wire_field_value_out_of_range=40`, `wire_field_value_truncated=41`, `wire_unexpected_tag=42`. `emit_session_reject_(ref_seq, ref_msg_type)` (`session.cpp:1597`) builds via `build_reject(...)` but does **not** currently carry a `SessionRejectReason` or `RefTagID`.

**Decision**: map per QuickFIX `SessionRejectReason`:

| validator error | `SessionRejectReason` | meaning |
|---|---|---|
| `wire_header_out_of_order` (39) | **14** | tag specified out of required order |
| `wire_unexpected_tag` (42) | **2** | tag not defined for this message type |
| `wire_required_field_missing` (38) | **1** | required tag missing |
| `wire_field_value_out_of_range` (40) | **5** | value is incorrect (out of range) for this tag |
| `wire_field_value_truncated` (41) | **6** | incorrect data format for value |

Extend `emit_session_reject_` / `build_reject` to accept a `SessionRejectReason` (and, where the validator surfaces it, the offending `RefTagID(371)`). **TASKS prerequisite**: confirm the `SessionRejectReason` constants (1/2/5/6/14) exist in the session reject path and that `build_reject` can stamp `373` (and optionally `371`).

**Rationale**: exact QuickFIX parity is the feature's stated goal; the mapping is the standard FIX session-reject taxonomy.

**Alternatives**: a single generic reason for all validation failures — rejected: defeats the parity goal and is less useful to operators.

## R-4 — Validation scope including the establishing Logon

**Finding**: clarified scope = **all inbound messages incl. the establishing Logon** (QuickFIX validates in `next(message)` before `nextLogon`). `on_inbound_frame` handles establishing vs Active states differently.

**Decision**: insert the validate gate at a single early point in `on_inbound_frame` that is reached for **all** session states (after framing, before state-specific handling), so the establishing Logon is validated too; a dictionary-invalid Logon is rejected and the session is not established. The existing establishment checks (013 authz, FIXT `1137`, BeginString, CompID) remain and run on messages that pass validation (FR-010).

**Rationale**: matches the clarified QuickFIX-parity decision and keeps one insertion point rather than per-state hooks.

**Risk / TASKS note**: FIXT app-dict resolution keys off `DefaultApplVerID` from the Logon; Phase-1 validation uses the **session** dictionary only (the same dictionary the session already holds), so it does not depend on app-version resolution. Confirm the early gate does not run before the frame is structurally framable, and that rejecting a malformed Logon uses the establishment-appropriate reject/disconnect path.

## R-5 — `Engine::start()` `void` → `expected_t<void>`

**Finding**: `Engine::start()` returns `void` (`engine.hpp:270`, `engine.cpp:1079`); it has **zero production callers** — only ~25 test sites, via the `fx.start()` fixture wrapper. No C-ABI binding (`src/capi/capi.cpp` exports only `fixpp_version_string`). `register_session` already returns `expected_t<void>`.

**Decision**: change `start()` to `[[nodiscard]] expected_t<void>`, call `validate_engine_config(cfg)` at the top, and return its error (propagating `clock_not_set`) before any `co_spawn`. Update the test fixture wrapper + call sites to check the result.

**Rationale**: the clarified chokepoint; matches the spec's "Engine::open" = `start()` semantics; low, test-only blast radius; no C-ABI impact.

**Alternatives**: gate in `register_session` (no API change) — rejected by clarification (user chose the `start()` gate as the canonical "open" precondition).

## R-6 — Validation enabled but no dictionary configured

**Finding**: `SessionConfig::dictionary` is `std::shared_ptr<const Dictionary>` (`session_config.hpp:180`, "required"). Clarified: enabling validation with no dictionary = **config error / fail-closed**.

**Decision**: enforce at session setup/registration: if `validate_inbound_messages == true` and `dictionary == nullptr`, reject the session config (surface a config error via the `register_session` `expected_t` path) rather than silently disabling validation.

**Rationale**: fail-closed surfaces the misconfiguration loudly; consistent with the clarified decision.

**Alternatives**: silent skip — rejected by clarification.

## Normative References

- `[2b §6.5.1]` header-field order; `[2b §6.5.3]` value range; `[2b §6.5.4]` required fields; `[2b §6.5.5]` unexpected tag — the validator's rule basis.
- FIX `SessionRejectReason(373)` taxonomy: 1 (required tag missing), 2 (tag not defined for message type), 5 (value is incorrect), 6 (incorrect data format), 14 (tag out of required order).
- QuickFIX-cpp `Session.cpp:1218-1229` (validate before verify/seqnum) — parity reference for scope + ordering.
- `engine_config.hpp:188` `validate_engine_config` (`clock_not_set`) — clock gate.
