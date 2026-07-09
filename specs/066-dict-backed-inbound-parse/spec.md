# Feature Specification: Dictionary-backed inbound receive parse

**Feature Branch**: `066-dict-backed-inbound-parse`

**Created**: 2026-07-09

**Status**: Draft

**Input**: Prerequisite for issue #179 / feature 065. Gate A on 065 (+ a Fable investigation, 2026-07-09) established that the shipped inbound receive parse is dictionary-free, so every `MessageView` delivered to an application callback (C-ABI and C++ typed) has no group membership — making all inbound repeating-group reads positional/membership-free on the real path. This feature threads the session's (already-required) dictionary into the inbound parse so inbound messages carry membership, which is the precondition for any correct grouped read.

## Clarifications

### Session 2026-07-09

- Q: When dict-backing makes group reads membership-bounded, a counterparty field inside a group that isn't in the loaded dictionary terminates the instance (permissive → strict). Accept, or preserve permissive passthrough? → A: **Accept strict** (match QuickFIX/J). It is required for membership-correct extents and IS the dictionary-driven model the Orchestra / FIX-Latest direction assumes ([[project_orchestra_fix_latest_direction]] / `research/orchestra-fix-latest-direction.md`). EP/venue extensions are handled by **dictionary completeness**, not permissive parsing: FIX-Latest EPs are backward-compatible additions declared (with group membership) in the Orchestra-derived dictionary → strict bounding includes them; venue/custom fields come via the planned **dialect-overlay** path (D-009 / `dialect_overlay`) extending the runtime dict. Top-level unknown-tag tolerance is unaffected — only group extents become strict. Document as a Behaviors & Limitations row + release note (FR-008).
- Q: Cloned (`fixpp_msg_clone`) and `reify` views are dict-free today; once the source is dict-backed a clone would read groups differently from its source. Policy? → A: **Propagate the dictionary membership into clones and reify views IN 066**, so a clone reads identically to its source (no silent divergence). In scope for this feature (FR-007).

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Membership-correct inbound repeating-group reads (Priority: P1)

An application receiving a FIX message through a live session **whose dictionary registers groups (e.g. FIX44 / FIX50 / FIXT)** reads a repeating group (via the C-ABI `fixpp_group_*` accessors or the C++ typed flyweights). The group's instances and per-instance fields resolve by **dictionary membership** — each instance is bounded at the first tag that is not a member of that group in its context — exactly as the (dictionary-backed) validator and typed-read tests already do. (FIX 4.0/4.1/4.2 dictionaries register zero groups — inherited **L-063-1** — so a FIX4x session becomes strict-but-group-blind under dict-backing; see the FIX4x limitation in Edge Cases / FR-008.)

**Why this priority**: Today the inbound parse is dictionary-free (`Session::parse_and_dispatch_` uses a default `Parser`), so `OffsetTable::group()` takes its membership-free fallback and a group's extent runs to **end of message**. The last instance of any inbound group absorbs every subsequent body field; a field queried on that instance returns `OK` + a wrong value instead of `TAG_NOT_FOUND`. This is a reachable **silent wrong value** on every real inbound message that carries a group followed by more fields — the root cause behind issue #179, and broader than #179 stated (it affects top-level groups and the C++ typed path too, not only the C-ABI nested read).

**Independent Test**: Drive a group-bearing frame (e.g. a FIX44 `ExecutionReport` with `NoLegs(555)` followed by a trailing outer field) through **real `Session` dispatch** and assert in the application callback that a trailing outer field queried on the last group instance returns `TAG_NOT_FOUND`, and each instance's own members read correctly. This must be RED on today's dict-free parse and GREEN after.

**Acceptance Scenarios**:

1. **Given** a live session with its configured dictionary, **When** an inbound message carrying a repeating group followed by trailing fields is dispatched, **Then** the group's last instance is bounded by membership (trailing non-member fields are NOT part of it), for both the C-ABI and C++ typed read paths.
2. **Given** the same, **When** the application reads each instance's declared members, **Then** every value is correct and the instance count matches the wire.

---

### User Story 2 - Scalar-as-group query returns the documented result (Priority: P1)

An application queries a scalar tag as if it were a repeating group on an inbound message.

**Why this priority**: Dictionary-free, the "is this count field really a group" delimiter-membership check is skipped, so querying a scalar tag as a group returns a bogus instance spanning the rest of the message instead of the documented `FIXPP_ERR_TYPE_MISMATCH` (C-ABI E-2 / CA-010 contract). Restoring membership restores this contract.

**Independent Test**: Through real session dispatch, `fixpp_msg_get_group(msg, <a scalar tag>, …)` returns `FIXPP_ERR_TYPE_MISMATCH` (not a spurious group).

**Acceptance Scenarios**:

1. **Given** a dispatched inbound message, **When** a non-group scalar tag is queried as a group, **Then** the result is `FIXPP_ERR_TYPE_MISMATCH` (present-but-not-a-group), matching the dictionary-backed contract.

---

### User Story 3 - No regression for non-group traffic, admin, or performance (Priority: P1)

Every message that reads correctly today continues to; admin/session messages (Heartbeat/Logon/…) — which flow through the **same** dict-backed parse site, not a separate one — are unaffected because they carry no read repeating groups and membership is lazy (not because they bypass the change); the inbound parse remains free of new global-heap allocation and within its stack parse-arena budget.

**Why this priority**: The change is on the inbound hot path. It must not regress existing session/interop behavior, must not introduce per-message global-heap allocation, and must not overflow the parse arena.

**Independent Test**: The full session + interop + C-ABI test suites pass; the allocation-discipline gate shows no new global-heap allocation on the parse+read path; a representative group-bearing message parses+reads within the existing stack arena.

**Acceptance Scenarios**:

1. **Given** the existing session/interop/C-ABI suites, **When** the inbound parse is dictionary-backed, **Then** all pass (with any intended behavior changes updated as explicit, reviewed test edits — not silent breakage).
2. **Given** an inbound message with no repeating groups (incl. all admin messages), **When** dispatched, **Then** behavior and cost are effectively unchanged (membership is consulted lazily, only on a group read).

---

### Edge Cases

- **Counterparty field inside a group not present in the loaded dictionary** — INTENDED BEHAVIOR CHANGE (permissive → strict): membership bounding terminates the group instance at that unknown field (`consume_group_extent` breaks on the first non-member). This matches QuickFIX's `DataDictionary` behavior but is a change from today's permissive dict-free read. It MUST be surfaced (a Behaviors & Limitations row) and was confirmed during `/speckit-clarify` (2026-07-09). Scope note: the loaded dictionary should declare all groups the counterparty actually sends; a session speaking a superset dialect needs a matching dictionary.
- **Cloned inbound message (`fixpp_msg_clone`) and `reify` handles** — these currently build deliberately dictionary-free views, so a clone would read a group DIFFERENTLY from its source once the source is dictionary-backed. Resolved (clarified 2026-07-09): **propagate membership into the clone/reify view** so a clone reads identically to its source; in scope for 066 (FR-007). Both clone and reify use the SAME mechanism: copy the source view's membership into an **owned** `table_view` via a new internal `MessageView` accessor (lifetime: the clone/reify owns its frame copy, so it needs an owned membership source — an owned `table_view` copy, self-contained — not a borrowed pointer into a transient).
- **FIX 4.0/4.1/4.2 dictionaries register zero groups (inherited L-063-1)** — their group-count fields are typed legacy XML `INT`, not `NUMINGROUP`, so `as_table_view()` registers no groups. Dict-backing a FIX4x session therefore flips its inbound group reads to `TYPE_MISMATCH`/absent (strict-but-group-blind) — a present-correct→absent change for FIX4x. 066's membership-correct claims are **scoped to group-registering dictionaries** — per L-063-1 the six group-bearing vendored dicts are **FIX43 / FIX44 / FIX50 / FIX50SP1 / FIX50SP2 / FIXT.1.1** (only FIX 4.0/4.1/4.2 register zero groups); the FIX4x gap is a documented limitation row (FR-008, dedicated L-066-x tied to L-063-1), and structural INT-count group registration is out of scope for 066.
- **Parse arena sizing** — dictionary-backed nested reads build sub-`OffsetTable`s lazily from the per-message stack arena during the callback. The existing `kInboundParseArena` (16 KiB) / `kAdminParseArena` (8 KiB) must still suffice for representative nested reads; a pathological deeply-nested message must fail closed (defined error), never over-read or corrupt.
- **Opt-in strict validator's own parse** (`vg_parser`, dictionary-free today) — assess whether it must also be dictionary-backed for its group-validation to be correct, or whether the validator's own `table_view` walk already covers it (FR-006).
- **No dictionary / directly-constructed session** — `open()` already requires a dictionary; a session without one is a config error before dispatch. So there is no optional-dictionary path to handle on the read side.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The inbound receive parse that produces the `MessageView` delivered to application callbacks (`Session::parse_and_dispatch_`, the single parse site for both admin and app) MUST construct its `Parser` dictionary-backed, using the session's configured dictionary, so the resulting `MessageView`'s `OffsetTable` carries the group-membership predicate and context. **Membership-correct grouped reads are scoped to dictionaries whose `table_view` actually registers groups (e.g. FIX44 / FIX50 / FIXT).** For FIX 4.0/4.1/4.2 (inherited **L-063-1**: group-count fields typed `INT`, not `NUMINGROUP` → `as_table_view()` registers zero groups), dict-backing makes group reads strict-but-group-blind (`TYPE_MISMATCH`/absent); structural INT-count group registration is out of scope for 066 (see the FIX4x limitation, FR-008).
- **FR-002**: The session MUST build the dictionary's `table_view` **once** (at `open()`, when the dictionary is known) and hold it in a member whose address is **stable across the session lifetime**. The `Parser` is reconstructed per message and stores `std::addressof(*inbound_tv_)` only for that parse's duration (`parser.hpp:508`); the binding requirement is therefore that the address not move **during a parse** (guaranteed by serialized dispatch). It MUST NOT rebuild the `table_view` per message. `open()`/reconnect behavior: `inbound_tv_` is built once and is **not reseated per message**; if `open()` rebuilds it on reconnect it does so between (never during) parses — confirm/guard at implement.
- **FR-003**: For **group-registering dictionaries (e.g. FIX44 / FIX50 / FIXT)**, both the C-ABI grouped read (`fixpp_msg_get_group` / `fixpp_group_*`) and the C++ typed grouped read MUST produce membership-correct results on inbound-dispatched messages (group extents membership-bounded; scalar-as-group → `TYPE_MISMATCH`), because both derive membership from the same dictionary-backed `OffsetTable`. (FIX4x dictionaries register zero groups per L-063-1 — out of scope, FR-008 limitation row.)
- **FR-004**: The inbound parse+read path MUST remain free of new **global-heap** allocation: the `table_view` is built once at `open()`; per-message membership lookups and lazily-built nested sub-views allocate only from the existing per-message stack parse arena (as the dictionary-backed typed-read path already does).
- **FR-005**: Admin/session messages flow through the **SAME** dict-backed parse site (`parse_and_dispatch_`) as app messages — they are NOT a separate or bypassed parse. Non-group traffic and admin messages MUST be behaviorally unchanged, because membership is consulted **lazily** (only on a group read) and admin messages carry no read repeating groups — not because admin bypasses the change. An admin callback that reads a group would become membership-bounded (a correct behavior change). The change MUST NOT alter session-protocol handling.
- **FR-006**: The feature MUST determine and record whether the opt-in strict validator's own inbound parse (`vg_parser`) also requires dictionary-backing for correct group validation, and either apply the same treatment or document why the validator's own `table_view` walk already suffices.
- **FR-007**: Cloned (`fixpp_msg_clone`) and `reify` views MUST **propagate the dictionary membership** (clarified 2026-07-09), so a clone/reify handle reads a repeating group identically to the dict-backed source it was derived from (no silent positional-vs-membership divergence). This is in scope for 066.
- **FR-008**: The intended behavior change — a counterparty field inside a group that is absent from the loaded dictionary now terminates the group instance (permissive → strict, matching QuickFIX/J) — is **accepted** (clarified 2026-07-09) and MUST be documented as a Behaviors & Limitations row + release note, stating the extension story: the **presently-shipped** path for a superset counterparty is to **keep the loaded dictionary current** (Orchestra/EP additions declared with group membership → strict bounding includes them); the `dialect_overlay` config knob exists but membership-extension via overlay is the **planned** path (**D-009 — currently `backlog`/unshipped** per `spec/feature-catalogue.md:126`), not a currently-functional escape hatch. Top-level unknown-tag tolerance is unchanged (only group extents become strict). This B&L work MUST also add an explicit **FIX4x negative/limitation row (a dedicated L-066-x tied to L-063-1)**: a FIX 4.0/4.1/4.2 session's inbound group reads become `TYPE_MISMATCH`/absent under dict-backing (its dictionary registers zero groups — L-063-1) until structural group registration for legacy `INT` counts exists (out of scope for 066).
- **FR-009**: Arena fit MUST be **witnessed**, not assumed (dict-backed nested reads build sub-`OffsetTable`s from the stack arena — a NEW cost on both arenas). A representative group-bearing **app** message MUST parse+read within `kInboundParseArena=16384` AND a group-bearing **admin** message within `kAdminParseArena=8192`, each with no heap fallback and a successful read, plus a near-cap / headroom probe. A pathological inbound message (deeply nested / oversized group) MUST fail closed within the existing parse-arena and group-depth/entry caps — never over-read, corrupt, or silently truncate.
- **FR-010**: Issue #179 and the L-063-2 Behaviors & Limitations row MUST be amended to correct the now-known-false claim that "the C++ typed read path is unaffected" (it is membership-free on the shipped path until this feature lands). The amended row(s) MUST cross-reference the new FIX4x limitation row (FR-008, tied to L-063-1) so the group-registering-dict scope of 066's correctness is discoverable from the L-063 cluster.

### Key Entities *(include if data involved)*

- **Session `table_view` member**: the once-built, stable-address dictionary metadata (membership predicate + field classification) the inbound `Parser` binds to. Analogous to the copy the strict validator already holds; this feature gives the parse its own stable instance (or a shared one).
- **Inbound `MessageView` / `OffsetTable`**: the parsed view handed to callbacks; gains a non-null membership predicate + dictionary pointer + root group context, enabling membership-bounded group extents.
- **Dictionary (`cfg_.dictionary`)**: the required, already-configured `shared_ptr<const Dictionary>`; source of `as_table_view()`.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: A group-bearing inbound frame **on a group-registering dictionary (e.g. FIX44 / FIX50 / FIXT)** driven through **real `Session` dispatch** reads membership-correctly — a trailing outer field queried on the last group instance returns `TAG_NOT_FOUND` (0 silent wrong values) — for both the C-ABI and C++ typed read paths; RED on the pre-change dict-free parse, GREEN after. (FIX4x is out of scope per L-063-1 — FR-008 limitation row.)
- **SC-002**: Querying a scalar tag as a group on an inbound-dispatched message returns `FIXPP_ERR_TYPE_MISMATCH` in 100% of cases (documented contract restored).
- **SC-003**: 100% of the existing session + interop + C-ABI test suites pass after the change; every behavior delta is an explicit, reviewed test update (no silent breakage), and each intended change is pinned by a discriminating test. **Required witnesses (explicit — the pre-Gate-A spike proved only a current-suite smoke of 147/147, not these)**: a **validator-ON** session run; an admin / no-group regression; the alloc-discipline gate; ASan/UBSan/TSan lifetime for the session `inbound_tv_`, the clone-owned `table_view`, and the reify owned membership; the `tests/abi` golden byte-unchanged; and at least one captured **group-bearing interop fixture** (or a documented reason none exists).
- **SC-004**: Zero new global-heap allocations on the inbound parse+read path (allocation-discipline gate). Arena fit is **witnessed** (FR-009): a representative group-bearing **app** message reads within `kInboundParseArena=16384` AND a group-bearing **admin** message within `kAdminParseArena=8192` — each with no heap fallback — with a near-cap headroom probe, and a pathological deeply-nested message fails closed.
- **SC-005**: After this feature, issue #179's C-ABI nested-read fix (065) reads membership-correctly through real session dispatch — the prerequisite is satisfied (verified by 065's real-dispatch witness, which is RED before 066+065 and GREEN after).

## Assumptions

- The dictionary is **required** at `open()` (`session.cpp` null-dict guard), so there is no optional-dictionary path to handle on the inbound read side; every live session already owns the dictionary this feature threads into the parse.
- The dictionary-backed `Parser` ctor (`parser.hpp`) and the 062/063 membership/extent machinery are correct and reusable; this feature routes the already-supported dictionary-backed parse into the session inbound path — it does not modify the parse/membership algorithms.
- Building `as_table_view()` once at `open()` is acceptable cost (open is not the hot path; the strict validator already pays it when enabled); per-Dictionary caching across sessions is a possible later optimization, out of scope here.
- Membership is consulted lazily (only on a group read), so admin/no-group traffic incurs negligible per-message overhead.
- The dict-free parse was an oversight, not a deliberate design choice (the 019 app-callbacks design doc describes the callback view as "dict-backed / dict-aware"), so restoring dictionary-backing aligns the implementation with its original design intent.
