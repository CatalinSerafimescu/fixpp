# Contract: `OffsetTable::group()` cap accounting

**Feature**: 085-fold-flat-cap-loop · **Date**: 2026-08-03 · **Owns**: `src/wire/offset_table.cpp` — `OffsetTable::group()` only.

**Relationship to 083's contracts**: `contracts/typed_read_splitter.md` (083) owns `offset_table.cpp` broadly and explicitly **scoped the cap loop out** — *"The `:526` cap-loop scope-out is kept as-is — it was independently verified sound at rounds 2 and 3 and was not reopened"* (083 spec.md, Gate A exhaustion session). This contract picks up exactly that scoped-out site and nothing else. It does **not** amend C-8.0, C-8.0b or C-8.0c.

---

## C-1 — The delimiter-source coupling invariant (STANDING)

**Statement.** `OffsetTable::group()` and `OffsetTable::consume_group_extent` MUST resolve their instance delimiter from the **same** entry — `entries_[count_idx + 1].tag` — for as long as `group()` relies on `consume_group_extent`'s cap to stand in for its own.

**Current satisfaction** (`main` = `c1564dd2`): `group():545,551` and `consume_group_extent():450,458` both compute `first = count_idx + 1U` and read `entries_[first].tag`, from the same `count_idx` passed at `:575`.

**Why this is a contract and not a comment.** It is the load-bearing premise of the whole feature (research.md R-1, step 1). If a future change re-points **one** of the two at the per-context dictionary store — as 083 did for the `group_slices()` splitter at `:704-711` — the two walks would partition the same extent on **different keys**, step 2's superset relation would fail, and `group()` would silently under-enforce `max_group_entries_per_instance` on the dictionary path.

**Why no test can guard it.** The failure mode is *a cap that correctly never fires, ceasing to be correct*. There is no observable difference between "the cap did not fire because no instance breached it" and "the cap did not fire because the walk that would have caught it now measures the wrong partition" — until a hostile frame arrives in production. A regression test would have to construct a frame that breaches under one delimiter and not the other, which requires the defect to already exist.

**Obligation.** The delivered source MUST carry this invariant as a comment at `group()`'s dictionary branch (FR-002), naming both line-sites and stating the consequence of breaking it. Any future feature touching either delimiter resolution MUST re-verify R-1's four steps or re-introduce an independent cap on the dictionary path.

**Status**: satisfied at delivery; **standing** thereafter.

---

## C-2 — Dictionary path: exactly one traversal

**Statement.** On the dictionary path (`opaque_dict_ != nullptr && group_member_fn_ != nullptr`), `group()` MUST perform exactly one traversal of the group's entries and MUST NOT re-walk `[first, group_end]` after `consume_group_extent` returns.

**Pre-conditions.** `count_idx` located (`:536-544`); `first < entries_.size()` (`:546`); `delim` confirmed a member of `no_tag`'s group in context (`:566`).

**Post-conditions.**

| | Value | Unchanged from pre-085 |
|---|---|---|
| `group_end` | `consume_group_extent(count_idx, ctx, ctx.depth, overflow)` | Yes |
| on `overflow` | `err_group_too_large` at `:576-578` | Yes |
| return | `group_index{no_tag, first, group_end - first}` | Yes |
| second cap check | **absent** | **Removed** |

**Justification obligation (FR-002).** The dictionary branch MUST carry a comment restating research.md R-1: the cap is already applied at `consume_group_extent:521-524` over nesting-aware boundaries, the flat partition refines that one, and the nesting-aware walk returns first — so a second walk cannot fire. The comment anchors at the branch, not at a removal site, because after removal there is no site.

**Error disposition.** Unchanged in value, origin and ordering. `err_group_too_large` still reaches callers only via `:577`, which is `consume_group_extent`'s overflow — as it already did for every frame that could reach it (research.md R-3).

---

## C-3 — Dict-free path: cap preserved byte-identically

**Statement.** On the dict-free path (`opaque_dict_ == nullptr || group_member_fn_ == nullptr`), the per-instance cap MUST remain enforced, by the **same source lines** relocated without modification.

**Pre-conditions.** As C-2 minus the membership check at `:566`, which the dict-free branch does not reach.

**Post-conditions — all identical to pre-085:**

| | Value |
|---|---|
| `group_end` | `entries_.size()` |
| delimiter | `entries_[first].tag` (wire-derived, `:551`) |
| boundary rule | `(k == group_end) \|\| (k > first && entries_[k].tag == delim)` |
| segment measure | `k - inst_start`, `inst_start` re-anchored at each boundary |
| breach | `err_group_too_large` |

**Verbatim obligation (FR-001a).** The relocated lines MUST NOT be rewritten, reindented into a different shape, parameterised, or extracted. The diff for this branch MUST read as a move. This is what reduces FR-003 from an equivalence argument to a diff inspection.

**Ordering obligation (FR-009).** `group_end = entries_.size()` MUST remain assigned before the loop, as today. No work is added on either path.

**Recorded looseness (FR-003a, fixpp#220).** The final segment ends at `entries_.size()`, so top-level fields following the group count toward the last instance. Under default `Config` this cannot breach the cap — `default_max_offset_entries == default_max_group_entries_per_instance == 4096` and `build()` clamps at the former (`:326`), so the segment is `≤ 4095`. It becomes a false positive only where a caller sets `max_group_entries_per_instance < max_offset_entries - 1`. **Preserved, not repaired**; recorded as a limitation citing fixpp#220.

---

## C-4 — Untouched by this contract

Named explicitly so a reviewer can see the boundary rather than infer it.

| Site | Why out of scope |
|---|---|
| `consume_group_extent` (`:442-528`) — signature, body, contract, dict-free bail at `:454-456` | FR-001a/FR-008. The "literal fold" alternative was rejected at `/clarify` Q1 |
| `group_slices()` instance splitter (`:712-733`) | L-063-4 **leg 1**, descoped with evidence by 083 (empty target population across all ten dictionaries; a literal implementation would break the 485-context shape that *is* reachable). Still flat, and still resolving `delim` from the **dictionary store** (`:704-711`) rather than the wire — see FR-007a |
| `group_slices_reserve_bound()` (`:599-634`) | Not reached by this change |
| Per-context delimiter store, membership callbacks, `group_context` | 083's contracts own these |

---

## Verification matrix

| Contract | Discharged by | Artifact |
|---|---|---|
| C-1 | Source inspection at delivery + comment obligation | FR-002; standing thereafter |
| C-2 | Source inspection; SC-005 | `offset_table.cpp` diff |
| C-2 (no behaviour change) | Existing corpora unchanged | SC-001, SC-002 |
| C-2 (cap still fires) | `WireOffsetTable.DoSCapPerInstanceRejectsOversizedSingleInstance` stays green | SC-003 |
| C-3 (byte-identical) | Diff reads as a move; ~16 existing dict-free tests stay green | FR-003, SC-004 |
| C-3 (cap fires, load-bearing) | **New** dict-free + tight-`Config` pin, bracketing companion, RED-under-mutation transcript | FR-004, FR-005a, SC-004a |
| C-3 (looseness recorded) | Limitation row citing fixpp#220 | FR-003a, SC-010 |
| C-4 | Diff touches no listed site | FR-008, SC-007 |
