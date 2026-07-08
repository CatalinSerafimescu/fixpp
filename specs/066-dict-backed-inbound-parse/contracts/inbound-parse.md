# Contract: Dictionary-backed inbound receive parse (066)

Observable behavior of inbound message reads after 066. No exported C symbol / header / error-enum / version change; the delta is that inbound-dispatched `MessageView`s (and their clones/reify handles) now carry dictionary membership.

## C1 — Inbound group extents are membership-bounded (the fix)

**Given** a live session with its configured dictionary, and an inbound message carrying a repeating group `G` followed by trailing fields:
- Through **real session dispatch**, each instance of `G` (including the last) is bounded at the first tag that is not a member of `G` in its context — trailing non-member fields are NOT part of any instance.
- A trailing field queried on the last instance → `TAG_NOT_FOUND` (C-ABI `fixpp_group_get_field_*`) / absent (C++ typed). *(Was: `OK` + wrong value, extent to end-of-message.)*
- Holds for **both** the C-ABI read path and the C++ typed flyweight path (same `OffsetTable`).

## C2 — Scalar-as-group returns TYPE_MISMATCH (contract restored)

**Given** an inbound-dispatched message: `fixpp_msg_get_group(msg, <scalar tag>, …)` → `FIXPP_ERR_TYPE_MISMATCH` (the documented E-2/CA-010 result), not a spurious group instance.

## C3 — Strict in-group membership (INTENDED behavior change; QuickFIX/J-aligned)

- A counterparty field inside `G` that is **not declared** in the loaded dictionary terminates the instance (`consume_group_extent` breaks on the first non-member) — permissive → strict. Documented (B-066/L-066).
- **Extension story**: keep the loaded dictionary current (FIX-Latest EPs via the Orchestra-derived dictionary declare their fields as members) and use the dialect-overlay path (D-009 / `dialect_overlay`) for venue/custom fields; those become members → included. **Top-level** unknown tags remain tolerated (indexed + `get(tag)`-readable); only group extents are strict.

## C4 — Clone / reify read identically to source (FR-007)

- `fixpp_msg_clone(src)` → the clone reads any repeating group with the SAME membership-bounded result as `src` (clone carries an owned `table_view`).
- A `reify`/`reify_as<Msg>` owning handle reads groups membership-bounded (its re-framed view is dict-backed).

## C5 — No regression / no new surface

- Non-group traffic and all admin/session messages: behavior unchanged; per-message cost ≈ unchanged (membership is lazy, consulted only on a group read).
- No exported C symbol / header / error value / version change (`tests/abi` golden byte-identical).
- No new global-heap allocation on the inbound parse+read path (the `table_view` is built once at `open()`; per-message membership + nested sub-views come from the existing per-message stack arena). The parse arena is not overflowed by representative group-bearing messages.

## C6 — Fail-closed on pathological input

- A deeply-nested / oversized inbound group fails closed within the existing group-depth (`kMaxGroupDepth=16`) and per-instance entry caps and the stack parse arena — never over-read, corrupt, or silently truncate.

## C7 — Prerequisite satisfied

- After 066, feature 065's real-dispatch C-ABI nested-read witness (issue #179) reads membership-correctly — RED before 066(+065), GREEN after (SC-005).
