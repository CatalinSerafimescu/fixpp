# Phase 0 Research: Validation Gate Wiring

Resolves the design forks surfaced while planning. Each item: Decision / Rationale / Alternatives.

## R-1 — Production `table_view`: how to feed the validator from `Dictionary`

**Finding**: `wire::dictionary_driven_validator` binds a `fixpp::dict::table_view` **by value** (`validator.hpp:86`, stored as a `dict_` member; the class is HEADER-ONLY by design per the seam note at `validator.hpp:13-23`) and calls six methods on it: `field_valid_for(msg_type, tag)`, `enum_valid(tag, value)`, `required_fields(msg_type)`, `group_first_field(no_tag)`, `group_member_tags(no_tag)`, `field_type_of(tag)`. **Buildability blocker (RC-A):** *neither* `dict::table_view` *nor* the 7-value `dict::field_type` enum the validator switches on (`validator.hpp:295` `using ft = fixpp::dict::field_type;`) exists in `include/` — both are defined ONLY in the test mock (`tests/support/mock_dict_table.hpp:30` defines `enum class field_type : std::uint8_t { String, Int, Float, Char, Boolean, Data, Length };`; a grep for `enum class field_type` across `include/` returns nothing). `validator.hpp` only forward-declares `table_view` and relies on tests including the complete mock BEFORE it. Production `dict::` ships the 29-value `field_data_type` (`field_ref.hpp:29`), NOT `field_type`. `Dictionary` natively provides `field_valid_for` ✓, `required_fields` ✓, `group_first_field` ✓, but **not** `enum_valid`, `field_type_of`, or `group_member_tags` (it has `group_fields` → `span<FieldRef>`, not `span<uint16_t>`). `Dictionary::as_table_view()` is explicitly noted **deferred** ("OUT of scope this PR") in `dictionary.hpp:18`.

**Decision**: To make production instantiation real (not include-order-dependent), define BOTH types in the dict layer and give the validator a complete-type include path:
- **Define the 7-value `dict::field_type` enum in a production header** (`include/fixpp/dict/field_type.hpp`, the production home — NOT the test mock), matching the mock's 7 values (`String, Int, Float, Char, Boolean, Data, Length`).
- Realize a minimal production `fixpp::dict::table_view` value type (header in `include/fixpp/dict/table_view.hpp`) exposing exactly the 6-method surface the validator binds, owning its tables.
- Give `validator.hpp` a **real complete-type include** of `field_type.hpp` + `table_view.hpp` (so it no longer depends on the mock being included first), and add a **compile-witness TU** that instantiates `dictionary_driven_validator` from a production `table_view` (proving the production path compiles without the mock).
- Add the deferred `Dictionary::as_table_view()` builder that populates a `table_view` from a `Dictionary`:
- `field_valid_for` / `required_fields` / `group_first_field` → forwarded/copied directly from `Dictionary`.
- `group_member_tags(no_tag)` → precomputed at build time by projecting `Dictionary::group_fields(no_tag)`'s `FieldRef` spans to `uint16_t` tag arrays, stored in the `table_view` (stable backing for the returned span).
- `field_type_of(tag)` → a global tag→`field_type` map built at construction by mapping `field_data_type`(29) → `field_type`(7). This assumes a tag's `field_data_type` is **invariant across every msg_type** in the loaded dictionary — an unconfirmed cross-msg-type invariant, NOT a settled fact: `Dictionary` exposes no global `field(tag)`, only `field(msg_type, tag)` (`dictionary.hpp:107`) and `field_by_name(name)` (`dictionary.hpp:112`). R-1a carries the prerequisite to confirm/derive this at implementation.
- `enum_valid(tag, value)` → **returns `true`** (Phase-1; `enum_table_index` is unbacked — FR-005, 2c-deferred).
- All table construction happens **once** at `as_table_view()` time (config/session setup), never per message → §XV.1-clean.

**Rationale**: This is the memory-note "path (b)" verdict — the production `Dictionary` covers 5 of 6 checks today; only enum-value tables are missing. A value-type `table_view` matches the validator's existing by-value binding with no validator-surface change.

**Alternatives**: (a) Build the full 2c `table_view` with enum tables now — rejected: enum tables are a separate, larger 2c work item (FR-005 scopes them out). (b) Change the validator to bind `Dictionary` directly — rejected: inverts the wire→dict-via-`table_view` indirection that 2c owns and would require re-deriving the 3 missing methods inside the validator anyway.

### R-1a — source for global `field_type_of(tag)`

**Decision**: build the tag→`field_type` map from `Dictionary`'s global field metadata (the `field` / `field_by_name` descriptive surface noted in `dictionary.hpp:14-15`). **TASKS prerequisite**: confirm at implementation a global (msg-type-independent) field-type accessor exists on `Dictionary`; if only `field_ref(msg_type, tag)` is available, derive the global map by unioning across message types at build time (a tag's type is invariant across messages in FIX). Either way the map is built once.

## R-2 — Where the validate gate runs (parse placement vs the seqnum gate)

**Finding**: `on_inbound_frame` is a `switch (fsm_state_)` (`session.cpp:1711`) with **fully separate per-state bodies** and **NO shared post-framing point**: `NotConnected` (`session.cpp:1712`), `LogonReceived`+`Active` (`session.cpp:2299-2300`), `LogoutSent` (`session.cpp:3322`), `LogonSent` (`session.cpp:3341`), `Disconnected` (`session.cpp:3730`). The processing arms run `scan_frame_header` early and the seqnum gate (`seqnum_mgr_.check_inbound`) per-arm; the full `MessageView<Index>` is built later inside `parse_and_dispatch_`, after the seqnum gate. The `LogoutSent`/`Disconnected` arms **drain** all non-Logout inbound with no seqnum advance and no dispatch. The guard-precedence preamble (`session.cpp:1682-1708`) documents a no-reject-loop guard that **exempts `Reject(35=3)` and `Logout(35=5)`**. The clarified requirement (QuickFIX parity, `Session.cpp:1218-1229`) is **validate BEFORE the seqnum gate**.

**Decision**: There is no single insertion point. When (and only when) `validate_inbound_messages` is set, insert the validate gate **per-arm, before each state's seqnum gate, in the inbound-processing states only** (`NotConnected`, `LogonSent`, `LogonReceived`, `Active`): build the `MessageView<Index>` in that arm, run `validator.validate()`, and on failure emit `Reject(35=3, reason)` and return without advancing the sequence number (subject to the preserved no-reject-loop exemption for `35=3`/`35=5`); on success, fall through to the existing guards/dispatch. **In the Logon-bearing arms (`NotConnected` `session.cpp:1712` / `LogonSent` `session.cpp:3341`) the gate runs BEFORE `interpret_logon()`** (`admin_messages.cpp:217` — the lead statement, which silently Disconnects with NO `Reject` on CompID/BeginString/MsgType failure) and before the SendingTime guard (~1825) / `1137` / `check_inbound` (~1890): validate-first. Otherwise a dict-invalid Logon that also fails `interpret_logon` would hit the silent-Disconnect path instead of `Reject(35=3)` (contradicting FR-003). Overlap precedence follows one rule — **validation rejects iff the message violates the dictionary; a dict-clean message keeps its existing disposition** (FR-010); the per-row witness list lives in data-model "Logon-arm overlap precedence" + contracts C-2/C-3. The `LogoutSent`/`Disconnected` **drain** arms are NOT touched — they keep draining (no validation, no seqnum advance, no dispatch). When the flag is unset (default), **none of this runs** — every arm is the current one, untouched. Because the MessageView build is per-arm, the early-build cost (New-6) is duplicated into each validating arm; this inflates the per-arm change surface and is reflected in TASKS.

**Decision (double-parse)**: the MVP accepts that the enabled path parses the frame twice (once for validate, once in `parse_and_dispatch_` for dispatch). Threading the already-built `MessageView` through the 6 `parse_and_dispatch_` call sites ("unify-the-parse") is the cleaner target but touches a shared template across 5 inbound + 1 outbound site and is higher-risk; it is recorded as a **follow-up optimization**, not MVP scope. The double parse is opt-in, uses the existing PMR arena (no extra heap), and is acceptable for a conformance mode.

**Rationale**: This split is what makes US1 (parity) and US2 (zero-cost default) simultaneously true. Parsing twice on the opt-in path is a CPU cost, not an alloc/correctness issue, and is invisible at default.

**Alternatives**: (a) validate after the seqnum gate (reuse the dispatch parse) — rejected: contradicts the clarified QuickFIX-parity ordering and would let a malformed message advance seqnum state. (b) unify-parse in this feature — deferred as the optimization above to keep the change surgical and Gate-A-reviewable.

## R-3 — `wire_*` → `SessionRejectReason` mapping + reject emission

**Finding**: the validator returns `core::error::wire_*` values: `wire_required_field_missing=38`, `wire_header_out_of_order=39`, `wire_field_value_out_of_range=40`, `wire_field_value_truncated=41`, `wire_unexpected_tag=42`. **Correction (RC-C):** `build_reject(...)` (`admin_messages.cpp:613-700`) **ALREADY accepts** `int ref_tag_id` + `int session_reject_reason` and **already emits tags `371` (when >0) and `373`** (`admin_messages.cpp:679,697`). The unparameterized one is the **caller** `emit_session_reject_(ref_seq, ref_msg_type)` (`session.cpp:1597`), which is **hardwired to `RefTagID=0, reason=3`** (`session.cpp:1624-1625`). So the work is to **extend/overload the caller `emit_session_reject_` to thread a `SessionRejectReason` (and optional `RefTagID`) through to the already-capable `build_reject` — reusing `build_reject` UNCHANGED.**

**Decision**: map per QuickFIX `SessionRejectReason`, faithful to what the validator actually emits:

| validator error (slot) | `SessionRejectReason` | when |
|---|---|---|
| `wire_header_out_of_order` (39) | **14** | header field out of standard order |
| `wire_unexpected_tag` (42) | **2** | tag not defined for this message type |
| `wire_required_field_missing` (38) | **1** | required field missing **and all repeating-group structure failures** (delimiter misplacement `validator.hpp:206`, NumInGroup count mismatch `validator.hpp:253`) — the validator surfaces group-structure failures as `wire_required_field_missing`; there is no distinct group reason in Phase-1 |
| `wire_field_value_out_of_range` (40) | **5** | value not type-conformant — emitted by the **type arm** (Int/Char/Float-format, `validator.hpp:296-343`); the enum arm (`validator.hpp:143`) also yields 40 but is **dead in Phase-1** (`enum_valid`→true), so reason 5 is reachable only via the type arm |
| `wire_field_value_truncated` (41) | **6** | **Float/decimal precision-loss ONLY** — `wire_field_value_truncated` fires only on the Float `decimal_precision_loss` remap (`validator.hpp:309-312`), NEVER as a generic "bad format value" |

Extend/overload **`emit_session_reject_`** (the caller) to accept a `SessionRejectReason` and an optional offending `RefTagID(371)`, passing them through to the existing `build_reject`. **TASKS prerequisite**: confirm the `SessionRejectReason` constants (1/2/5/6/14) used at the call sites; `build_reject` already stamps `373` (and `371` when >0), so no `build_reject` change is required.

**Rationale**: exact QuickFIX parity is the feature's stated goal; the mapping is the standard FIX session-reject taxonomy.

**Alternatives**: a single generic reason for all validation failures — rejected: defeats the parity goal and is less useful to operators.

## R-4 — Validation scope including the establishing Logon

**Finding**: clarified scope = **all inbound messages incl. the establishing Logon** (QuickFIX validates in `next(message)` before `nextLogon`). `on_inbound_frame` handles establishing vs Active states differently.

**Decision**: There is **no single early point** — `on_inbound_frame` is a per-state `switch` with no shared post-framing hook (R-2). Insert the validate gate **per-arm, before each state's seqnum gate, in the inbound-processing states** (`NotConnected`, `LogonSent`, `LogonReceived`, `Active`), so the establishing Logon (handled in the `NotConnected`/`LogonSent` arms) is validated too. In those Logon-bearing arms the gate runs **before `interpret_logon()` and the establishment checks** (validate-first — see R-2), because `interpret_logon()` is the lead statement that silently Disconnects (no `Reject`) on a CompID/BeginString/MsgType failure; so a dictionary-invalid Logon is rejected with `Reject(35=3)` and the session is not established, instead of silently disconnecting. The `LogoutSent`/`Disconnected` drain arms are **not** in scope. The existing establishment checks (013 authz, FIXT `1137`, BeginString, CompID) remain and run on messages that **pass** validation — i.e. dict-clean messages keep their existing disposition unchanged (FR-010); the overlap-precedence witness rows are in data-model + C-2/C-3.

**Rationale**: matches the clarified QuickFIX-parity decision (validate-before-seqnum on the processed messages) while preserving the drain-state and no-reject-loop semantics the FSM already implements.

**Risk / Phase-1 limitation (FIXT two-dictionary — New-3)**: QuickFIX validates a FIXT **application** message against BOTH the session dictionary AND a separate application dictionary resolved from `DefaultApplVerID` (`Session.cpp:1218-1229`, the parity oracle). Phase-1 validation uses the **session** dictionary only (the dictionary the session already holds), so it does NOT resolve an app dictionary by version. For a FIXT session the session dictionary is FIXT.1.1 (transport/admin); validating an app message against it would over-reject app-defined tags as `wire_unexpected_tag`. Therefore **full FIXT two-dictionary app-message validation is a documented, deferred Phase-1 limitation** (parallel to the FR-005 enum deferral; spec Out-of-Scope + Clarifications 2026-06-16 + a B&L row); the "QuickFIX parity" headline carries this caveat. **TASKS note**: confirm the per-arm gate does not run before the frame is structurally framable, and that rejecting a malformed Logon uses the establishment-appropriate reject/disconnect path.

## R-5 — `Engine::start()` `void` → `expected_t<void>`

**Finding**: `Engine::start()` returns `void` (`engine.hpp:270`, `engine.cpp:1079`); it has **zero production callers** and **no C-ABI wrapper** (`src/capi/capi.cpp` exports only `fixpp_version_string` — verified). The blast radius is **dozens of test/interop call sites plus the `fx.start()` fixture wrapper** (`start(` appears across ~35 test files / ~71 matches — a test-only blast radius). `register_session` already returns `expected_t<void>`.

**Decision**: change `start()` to `[[nodiscard]] expected_t<void>`, call `validate_engine_config(cfg)` at the top, and return its error (propagating `clock_not_set`, `core::error` slot 54) before any `co_spawn`. **TASKS migration task**: update the `fx.start()` fixture wrapper and every direct `engine.start()` test/interop call site to check the result (`expected_t<void>`).

**Rationale**: the clarified chokepoint; matches the spec's "Engine::open" = `start()` semantics; low, test-only blast radius; no C-ABI impact.

**Alternatives**: gate in `register_session` (no API change) — rejected by clarification (user chose the `start()` gate as the canonical "open" precondition).

## R-6 — Validation enabled but no dictionary configured

**Finding**: `SessionConfig::dictionary` is `std::shared_ptr<const Dictionary>` (`session_config.hpp:180`, "required"). Clarified: enabling validation with no dictionary = **config error / fail-closed**.

**Decision**: enforce at session setup/registration: if `validate_inbound_messages == true` and `dictionary == nullptr`, reject the session config by returning `core::error::invalid_session_config` (slot 53, `error.hpp:148`, `[2d §4.5]`) via the `register_session` `expected_t<void>` path, rather than silently disabling validation. (Note: `register_session` today returns `session_invalid_argument(119)` on a *duplicate id*; the new validation arm returns the distinct `invalid_session_config(53)`.)

**Rationale**: fail-closed surfaces the misconfiguration loudly; consistent with the clarified decision.

**Alternatives**: silent skip — rejected by clarification.

## Normative References

- `[2b §6.5.1]` header-field order; `[2b §6.5.3]` value range; `[2b §6.5.4]` required fields; `[2b §6.5.5]` unexpected tag — the validator's rule basis.
- FIX `SessionRejectReason(373)` taxonomy: 1 (required tag missing — and group-structure failures in Phase-1), 2 (tag not defined for message type), 5 (value is incorrect / out of range), 6 (incorrect data format — Float/decimal precision-loss only in Phase-1), 14 (tag out of required order).
- `[FIX50SP2 §2.1]` (Session-level error processing) — the FIX 5.0 SP2 / FIX-SL section enumerating the `SessionRejectReason(373)` values used here: 1 (required tag missing — and group-structure failures in Phase-1), 2 (tag not defined for message type), 5 (value is incorrect / out of range), 6 (incorrect data format — Float/decimal precision-loss only in Phase-1), 14 (tag out of required order).
- `[2d §4.4]` `validate_engine_config` → `clock_not_set` (`error.hpp:155`, slot 54); `[2d §4.5] / [2d §6.1]` `invalid_session_config` (`error.hpp:148`, slot 53) — the FR-011 config fail-closed error.
- QuickFIX-cpp parity reference for scope + ordering. The file lives at the **parent workspace root, OUTSIDE this submodule subtree** (`research/G19-fix-fpml-iso20022/` is the submodule), so it is **not reachable from the submodule cwd**: `<parent-workspace-root>/reference-engines/quickfix-cpp/src/C++/Session.cpp:1218-1229`. Excerpt quoted here as the reviewable oracle (validate precedes `nextLogon`):

  ```cpp
  if (m_sessionID.isFIXT() && message.isApp()) {
    DataDictionary::validate(message, &sessionDataDictionary, &applicationDataDictionary);
  } else {
    sessionDataDictionary.validate(message);   // validate BEFORE nextLogon
  }
  if (msgType == MsgType_Logon) { nextLogon(message, now); ... }
  ```

  Lines 1218-1229 also expose the FIXT two-dictionary path Phase-1 defers (R-4 / New-3).
