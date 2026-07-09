# Contract: Dictionary-backed inbound receive parse (066)

Observable behavior of inbound message reads after 066. No exported C symbol / header / error-enum / version change; the delta is that inbound-dispatched `MessageView`s (and their clones/reify handles) now carry dictionary membership.

## C1 — Inbound group extents are membership-bounded (the fix)

**Scope**: this contract holds for dictionaries whose `table_view` **registers groups** — the six group-bearing vendored dicts per L-063-1 (FIX43 / FIX44 / FIX50 / FIX50SP1 / FIX50SP2 / FIXT.1.1). FIX 4.0/4.1/4.2 register zero groups (inherited **L-063-1**: `INT`-typed count fields), so under dict-backing their group reads become `TYPE_MISMATCH`/absent (strict-but-group-blind) — out of scope for 066, documented as an FIX4x limitation row (L-066-x tied to L-063-1).

**Given** a live session with a **group-registering** configured dictionary, and an inbound message carrying a repeating group `G` followed by trailing fields:
- Through **real session dispatch**, each instance of `G` (including the last) is bounded at the first tag that is not a member of `G` in its context — trailing non-member fields are NOT part of any instance.
- A trailing field queried on the last instance → `TAG_NOT_FOUND` (C-ABI `fixpp_group_get_field_*`) / absent (C++ typed). *(Was: `OK` + wrong value, extent to end-of-message.)*
- Holds for **both** the C-ABI read path and the C++ typed flyweight path (same `OffsetTable`).

## C2 — Scalar-as-group returns TYPE_MISMATCH (contract restored)

**Given** an inbound-dispatched message: `fixpp_msg_get_group(msg, <scalar tag>, …)` → `FIXPP_ERR_TYPE_MISMATCH` (the documented E-2/CA-010 result), not a spurious group instance.

## C3 — Strict in-group membership (INTENDED behavior change; QuickFIX/J-aligned)

- A counterparty field inside `G` that is **not declared** in the loaded dictionary terminates the instance (`consume_group_extent` breaks on the first non-member) — permissive → strict. Documented (B-066/L-066).
- **Extension story**: the presently-shipped extension path = keep the loaded dictionary current (Orchestra/EP additions declare their fields as members → strict bounding includes them); the `dialect_overlay` membership extension is planned (**D-009, currently backlog/unshipped**), not a currently-available escape hatch. **Top-level** unknown tags remain tolerated (indexed + `get(tag)`-readable); only group extents are strict.

## C4 — Clone / reify read identically to source (FR-007)

- `fixpp_msg_clone(src)` → the clone reads any repeating group with the SAME membership-bounded result as `src` (clone carries an owned `table_view`).
- A `reify`/`reify_as<Msg>` owning handle reads groups membership-bounded (its re-framed view is dict-backed).
- **Unified mechanism (b)** (implementable, decided at Gate A): **both** clone and reify copy the source view's membership into an **owned** `table_view` via a NEW internal `MessageView` membership-copy accessor, then bind their re-framed `MessageView` dict-backed against that owned copy. At clone time `fixpp_msg_clone(src)` holds `src`'s live dict-backed inbound view (post-066 the inbound view is dict-backed) and already owns a clone-owned `table_view` member regardless — so it copies membership via the accessor, with **no** inbound-handle `dict_`-threading and **no** rebuild from a retained dict. The reify owning handle CANNOT retain the source dict (it outlives the session), so it likewise copies the source view's membership via the **same** accessor. A copied `table_view` is self-contained (`include/fixpp/dict/table_view.hpp:185-192`; spans stable-for-lifetime `:204,221`), so it safely outlives the source session/`Dictionary` without a `shared_ptr<const Dictionary>` pin. This needs **no public `reify(...)` / factory signature change** (the `owning_message_handle_from_frame` factory, `reify.hpp:87`, is unchanged) and **no codegen dispatch-emitter edit**.
- **Precondition**: the accessor re-concretizes the source view's type-erased `opaque_dict_` (`void const*`) back to a `table_view`, so it yields membership only when the source view is dict-backed (`opaque_dict_` non-null). A dict-free source yields no membership (empty copy) → the clone/reify stays dict-free (the correct degenerate case).

## C5 — No regression / no new surface

- Non-group traffic and all admin/session messages: behavior unchanged; per-message cost ≈ unchanged (membership is lazy, consulted only on a group read).
- No exported C symbol / header / error value / version change (`tests/abi` golden byte-identical).
- No new global-heap allocation on the inbound parse+read path (the `table_view` is built once at `open()`; per-message membership + nested sub-views come from the existing per-message stack arena). The parse arena is not overflowed by representative group-bearing messages.

## C6 — Fail-closed on pathological input

- A deeply-nested / oversized inbound group fails closed within the existing group-depth (`kMaxGroupDepth=16`) and per-instance entry caps and the stack parse arena — never over-read, corrupt, or silently truncate.

## C7 — Prerequisite satisfied

- After 066, feature 065's real-dispatch C-ABI nested-read witness (issue #179) reads membership-correctly — RED before 066(+065), GREEN after (SC-005).
