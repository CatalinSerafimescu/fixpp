# Research: Dictionary-backed inbound receive parse (066)

**Feature**: prerequisite for issue #179 / 065. **Date**: 2026-07-09. Origin: Gate A on 065 surfaced the premise error; a Fable-grade investigation (transcript 2026-07-09) confirmed it with graded source evidence.

## Decision 1 — Root cause: the shipped inbound parse is dictionary-free (CONFIRMED, read-it)

**Finding**: `Session::parse_and_dispatch_` constructs `fixpp::wire::Parser<access_mode::Index> pd_parser;` (default ctor — no dictionary) at `src/session/session.cpp:316` and parses every inbound frame with it. Every application callback (`fromApp`/`fromAdmin`/`toApp`, all dispatch sites) is handed the resulting `MessageView`. The default `Parser` ctor leaves `opaque_dict_`/`group_member_fn_` null, so the root `OffsetTable` has no membership; `OffsetTable::group()` takes the dict-free fallback (`src/wire/offset_table.cpp:441-442, 545-547`, `group_end = entries_.size()`), and group extents run to end-of-message (positional). This is the true root cause of #179 and is broader than #179 stated:
- The **last instance of any inbound group** (top-level or nested) absorbs all trailing body fields → wrong values / `OK` where `TAG_NOT_FOUND` is correct.
- The **scalar-as-group `TYPE_MISMATCH` guard** (the delimiter-membership check) is skipped → querying a scalar tag as a group returns a bogus instance.
- The **C++ typed read path is equally affected** — its generated flyweights get membership from the same root `OffsetTable` (via `entry_context`), so #179's / L-063-2's "the C++ typed path is unaffected/correct" holds ONLY for dictionary-backed (test) parses.
- **No test exercises a group read through real `Session` dispatch** — every grouped-read test uses `Parser<Index>{dict}` unit parses — which is why this survived 062/063/#175 and the #179 audit.

**Evidence** (all read-it this session unless noted): `session.cpp:316` (default Parser); `session.cpp:929` (dictionary REQUIRED at open()); `session.cpp:1171-1173` (dict feeds only the opt-in validator); `src/capi/session.cpp:109-111` (C-ABI session caches dict for outbound); `src/capi/engine.cpp:77-90` (inbound C-ABI msg wraps the borrowed view); `offset_table.cpp:441-442,545-547` (dict-free fallback). Census (ran-it): the only prod `Parser<...>` constructions are `session.cpp:316` (pd_parser) and `session.cpp:1869` (vg_parser, the validator's own parse); no production dict-backed parse exists.

## Decision 2 — Fix: dictionary-back the inbound parse via a once-built session `table_view` (reuse, not redesign)

**Decision**: Session builds `cfg_.dictionary->as_table_view()` **once** at `open()` into a stable-address member (`std::optional<table_view> inbound_tv_`, mirroring the validator's owned copy at `session.cpp:1171-1173`); `parse_and_dispatch_` constructs `Parser<access_mode::Index>{*inbound_tv_}`.

**Rationale**:
- The dict-backed `Parser` ctor already exists and is used by every typed-read test (`parser.hpp:502-540`); it installs `classify_fn_` + the context-scoped `group_member_fn_`. It stores `opaque_dict_ = std::addressof(dict_metadata)` (a **pointer**), so the `table_view` must be a stable lvalue outliving all parses — a Session member satisfies this; a per-message temporary would dangle. **read-it**: `parser.hpp:506-508`.
- The dictionary is **required** at `open()` (`session.cpp:929`), so there is no optional-dictionary path to handle — every live session already owns the dictionary.
- The 062/063 membership/extent machinery is correct and reusable as-is; this feature only changes WHICH parser the session uses. FR-005: parse/membership algorithms untouched.

**Alternatives**: (a) per-message `as_table_view()` — rejected (hot-path allocation, FR-004). (b) a C-ABI-read-layer-only dict injection (the 065 Gate-A "Option B") — rejected: the outer slice comes from the view's own `group_slices()` and the dict is ctor-only on the OffsetTable, so read-layer injection means bypassing/reimplementing `group()`, contradicts 065's FR-005, and abandons the equally-broken C++ typed path.

## Decision 3 — Strict in-group membership accepted; reconciled with the Orchestra / FIX-Latest direction (clarified 2026-07-09)

**Decision**: A counterparty field inside a group that is absent from the loaded dictionary terminates the group instance (permissive → strict, matching QuickFIX/J). Accepted; documented (FR-008).

**Rationale** (grounded in `research/orchestra-fix-latest-direction.md`, read-it): the entire FIX-Latest direction is **dictionary-driven** ("parse/validate is EP-proof … dictionary-driven"; FIX Latest arrives as "the 10th dictionary" via Orchestra→QuickFIX-XML). Strict membership is the model that direction assumes and that QuickFIX/J (the Orchestra reference impl) implements. The permissive dict-free read is the *absence* of the dictionary model, not a robustness feature. Extensions are handled by **dictionary completeness**: FIX-Latest EPs are backward-compatible additions declared (with group membership) in the Orchestra-derived dictionary → strict bounding includes them; venue/custom fields come via the planned dialect-overlay path (D-009 / `dialect_overlay`) extending the runtime dict. **Top-level** unknown-tag tolerance is unaffected (an unknown top-level tag is still indexed + readable via `get(tag)`); only group extents become strict. Preserving permissive passthrough (the rejected clarify option) would fight Orchestra (ignore its membership metadata) and diverge from QuickFIX/J, and is a larger design (distinguish unknown-passthrough from cross-group-member-terminate).

## Decision 4 — Clone/reify propagate membership (clarified 2026-07-09)

**Decision**: `fixpp_msg_clone` and `reify` views carry the dictionary so a clone/reified handle reads groups identically to its dict-backed source (no silent positional-vs-membership divergence).

**Mechanism + lifetime** (read-it):
- **Clone (cheaper)**: `fixpp_msg_clone` (`message_write.cpp:428-441`) builds an owning `MessageView` over a deep-copied frame, dict-free today (inbound `fixpp_msg.dict_ == nullptr`, `capi_internal.hpp:265`). A clone outlives the dispatch window, so it cannot borrow the session's `table_view` (that pointer is only valid during dispatch). Fix: (a) at the C-ABI inbound callback-wrap (`engine.cpp:77-90`) set the inbound handle's `dict_` to the session's cached `shared_ptr<const Dictionary>` (`src/capi/session.cpp:111`); (b) `fixpp_msg_clone` copies `dict_`, builds a **clone-owned** `table_view` (new member alongside `owned_view_`/`owned_frame_`, stable address), and constructs the clone's `MessageView` dict-backed against it. The clone's owning arena already uses `new_delete` upstream (owning handle — not a zero-heap path), so the owned `table_view` build is acceptable.
- **Reify (larger surface — CONFIRMED by spike read)**: `owning_message_handle::view()` (`reify.cpp:99-121`) re-frames dict-free from owned `bytes_`, and the factory `detail::owning_message_handle_from_frame(rmv, view, mr)` (`reify.hpp:87-89`) takes **no dictionary** — only the source view + arena. The owning handle must OUTLIVE the session, so it cannot retain the source view's borrowed `opaque_dict_` pointer (→ the session `table_view`). Fix: thread a stable membership source (a `shared_ptr<const Dictionary>` + an owned `table_view`, or the dictionary handle) through `owning_message_handle_from_frame` + the owning-handle pimpl + the **build-tree dispatch bridge** (`reify_dispatch_bridge.cpp`, which calls the factory) + `dict::reify(...)`'s own entry so it can supply the dictionary. This is the larger part of FR-007. **User decision 2026-07-09: keep reify propagation IN 066** (chose "both clone + reify in 066" after the spike surfaced this cost).

## Decision 8 — Pre-Gate-A spike (2026-07-09): regression surface, depth-2 typed, reify cost (grade-1, ran-it)

A one-line throwaway spike (local `auto tv = cfg_.dictionary->as_table_view(); Parser<Index>{tv}` in `parse_and_dispatch_`) was built (`linux-clang-debug`, clean compile) and run, then discarded. Findings:

1. **Regression surface = NIL (grade-1 measured).** `ctest -R 'capi|session|interop'` → **147/147 pass** with the inbound parse dict-backed. So SC-003's "existing suites pass / each delta explicit" is now measured, not inferred — flipping to dict-backed breaks nothing in the current suite (confirming Decision 1's E8: no existing test asserts on the positional read). Any *intended* new-behavior witness (US1/US2 strict extents, scalar-as-group) is net-new and RED-first.
2. **The 065 "depth-2 typed gap" (candidate L-065-1) is NOT real (grade-1).** `tests/session/test_exemplar_read.cpp::ReadE_ThreeLevelNested` reads the full **3-level** `NoOrders(73) → NoPartyIDs(453) → NoPartySubIDs(802)` chain via typed flyweights under a **dict-backed** parse and asserts the depth-3 members (`party_sub_id == "RD-SUB-A"`, `party_sub_id_type == 2`) — and passes. So the emitter's context threading resolves membership correctly at depth ≥ 2; 065's Decision 7 reasoning was an incorrect grade-3 inference. **Consequence for 065 re-plan**: drop candidate `L-065-1` and the depth-1 scope-out; the C-ABI cursor mirrors the (working) typed threading and can deliver correctness at all depths. (For 066 this is pure upside — dict-backing the parse makes typed reads correct at every tested depth on the shipped path.)
3. **Reify cost CONFIRMED** (Decision 4 above): the factory carries no dictionary → threading required; user kept it in 066.

## Decision 5 — vg_parser (validator's own parse) assessment (FR-006)

**Decision**: assess and record whether the opt-in strict validator's own parse (`vg_parser`, `session.cpp:1869`, dict-free today) needs dict-backing. **PLAUSIBLE**: the validator holds its own `table_view` (`session.cpp:1173`) and walks membership itself (per L-063-3), so its correctness may not depend on the parsed view's OffsetTable membership; but for consistency (and because L-063-3 notes the validator's group check is a known-weaker residual) dict-backing `vg_parser` is low-cost and removes a divergence. Resolve at implement: either dict-back it (preferred for uniformity) or record why the validator's own walk suffices. The validator is opt-in (default off, B-004-1), so it does not gate the core fix.

## Decision 6 — Test strategy: witnesses through REAL Session dispatch, RED-first (the load-bearing methodology change)

**Decision**: the correctness witnesses (SC-001/002) MUST drive a group-bearing frame through **real `Session` dispatch** (and a C-ABI engine loopback), and be RED-first on today's dict-free parse. The existing `Parser<Index>{dict}` unit tests are the membership-*logic* tier but CANNOT prove the shipped path (they masked this bug for 4 features). This directly applies the "a green that was never red proves nothing" + "witness the shipped path, not a proxy" discipline. Every intended behavior delta in the existing session/interop/C-ABI suites is an explicit, reviewed edit with a discriminating test — no silent breakage (SC-003).

## Decision 7 — Amend (not close) #179 + L-063-2 (FR-010)

**Decision**: #179 amended via a comment (posted 2026-07-09) correcting the premise (shipped parse dict-free; C++ typed path also affected; "exposed by 063" is test-scoped). L-063-2's "the C++ typed read path is UNAFFECTED" sentence is amended in `spec/behaviors-and-limitations.md` as part of this feature. 065 (the C-ABI nested-read fix) is re-planned on top of 066 and verified through real dispatch.

## Cross-cutting verified facts
- Dict-backed `Parser` ctor stores a pointer to the `table_view` (`parser.hpp:506`) → stable-address member mandatory. **read-it.**
- Membership is consulted lazily (only at `group()`/`consume_group_extent` read time; `classify_fn` only in lazy `unknown_fields`) → no per-frame membership cost on non-group traffic; no per-frame global-heap. **read-it** (`offset_table.cpp` group_member_fn use sites; `parser.hpp:295`).
- Inbound parse arena is a stack MBR (`kInboundParseArena=16384`/`kAdminParseArena=8192`, `session.cpp:292-293,301`); dict-backed nested reads build sub-`OffsetTable`s lazily from it during the callback → confirm representative fit; depth/entry caps fail closed (FR-009). **read-it.**
