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
- **Clone**: `fixpp_msg_clone` (`message_write.cpp:428-441`) builds an owning `MessageView` over a deep-copied frame, dict-free today (inbound `fixpp_msg.dict_ == nullptr`, `capi_internal.hpp:265`). A clone outlives the dispatch window, so it cannot borrow the session's `table_view` (that pointer is only valid during dispatch). Fix: (a) at the C-ABI inbound callback-wrap (`engine.cpp:77-90`) set the inbound handle's `dict_` to the session's cached `shared_ptr<const Dictionary>` (`src/capi/session.cpp:111`); (b) `fixpp_msg_clone` copies `dict_`, builds a **clone-owned** `table_view` (new member alongside `owned_view_`/`owned_frame_`, stable address), and constructs the clone's `MessageView` dict-backed against it. The clone's owning arena already uses `new_delete` upstream (owning handle — not a zero-heap path), so the owned `table_view` build is acceptable.
- **Reify**: `owning_message_handle::view()` (`reify.cpp:99-121`) re-frames dict-free from owned `bytes_`. The owning handle's pimpl holds no dictionary today. Fix: carry the dictionary (from the reify call context) into the owning handle so the re-framed `view_cache_` is dict-backed. **Open impl detail for `/plan` sign-off / tasks**: confirm the reify entry point has the source `Dictionary`/`table_view` in hand to thread; if not, this is where reify propagation is bounded (it may require passing the dict through `dict::reify(...)`). Flagged, not hidden.

**Note**: this is the added surface beyond the core session fix (Complexity Tracking). The user chose propagate-into-066 over defer/follow-up, accepting the scope.

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
