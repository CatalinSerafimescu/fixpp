# Phase 1 Data Model: 085-fold-flat-cap-loop

**Date**: 2026-08-03 · **Branch**: `085-fold-flat-cap-loop`

This feature introduces **no new types, fields, or state**. It removes one loop from one branch and relocates it to another. This document therefore records the entities the change *reasons over* — their existing shape, the invariants the change relies on, and which of those invariants become newly load-bearing.

No entity below is added, modified, or removed by this feature. That is the point of the document: a reader checking FR-008 (no surface change) should be able to confirm it here without reading the diff.

---

## E-1 — Group extent (`OffsetTable::group_index`)

**What it is**: the half-open entry range a repeating group occupies, returned by `OffsetTable::group(no_tag)`.

| Field | Meaning | Changed by 085? |
|---|---|---|
| `no_tag` | the group's `NumInGroup` tag | No |
| `first_entry()` | index of the group's first member entry (`count_idx + 1`) | No |
| `entry_count()` | `group_end - first` | No |

**Producing sites**, both unchanged in value:

- **Dictionary path** — `group_end = consume_group_extent(count_idx, ctx, ctx.depth, overflow)` (`src/wire/offset_table.cpp:575`).
- **Dict-free path** — `group_end = entries_.size()` (`:581`).

**Invariant 085 relies on**: `group_end` is *already* the nesting-aware answer on the dictionary path before the removed loop ever ran. The removed loop never wrote `group_end`; it only read it. This is why removal cannot change E-1. *(Verified: `:584-595` contains no assignment to `group_end`.)*

---

## E-2 — Per-instance entry cap (`OffsetTable::Config::max_group_entries_per_instance`)

**What it is**: a defence-in-depth DoS bound on how many entries one group instance may contain. Breach yields `error::wire_group_too_large`.

| Property | Value | Source |
|---|---|---|
| Default | `4096` | `include/fixpp/wire/offset_table.hpp:28` (`default_max_group_entries_per_instance`) |
| Scope | per-parse, caller-supplied | `offset_table.hpp:95` (`Config` member) |
| Sibling bound | `max_offset_entries`, default `4096` | `offset_table.hpp:27,94` |
| Table clamp | `entries_.size() >= cfg_.max_offset_entries` | `src/wire/offset_table.cpp:326` |

**Relationship that matters (R-2)**: because the two defaults are **equal** and the table is clamped at the first, a flat segment bounded by `entries_.size()` can never exceed the cap under default `Config`. The dict-free breach is reachable **only** when a caller sets

```
max_group_entries_per_instance < max_offset_entries - 1
```

This relationship is not encoded anywhere in the type system — it is an arithmetic coincidence of two independent defaults. It is the reason fixpp#220 is config-dependent rather than a default-path defect, and the reason `:592` is currently dead code (R-3).

**Not changed by 085**: neither default, neither `Config` member, nor the error value.

---

## E-3 — Instance boundary rule (behavioural, not a type)

The predicate deciding where one group instance ends and the next begins. Two implementations coexist in `offset_table.cpp`, and this feature changes **which paths reach which**, not what either computes.

| | Nesting-aware | Flat |
|---|---|---|
| Site | `consume_group_extent:477-526` | `group():584-595` (relocating) and `group_slices_status():712-733` (untouched) |
| Delimiter source | wire — `entries_[first].tag` (`:458`) | `group()`: wire (`:551`) · `group_slices_status()`: **dictionary store** (`:704-711`) |
| Descends into nested groups | Yes (`:491-499`, `:510-518`) | No |
| Requires a dictionary | Yes | No |
| Applies the cap | Yes (`:521-524`) | Yes (`:591-593`) |

**Reachability, before → after:**

| Path | Before | After |
|---|---|---|
| Dictionary | nesting-aware **then** flat (flat unreachable-as-error, R-1) | nesting-aware only |
| Dict-free | flat only | flat only *(semantics preserved — FR-003 + C-3's nine-item checklist)* |

**The asymmetry FR-007a obliges us to record**: the two *surviving* flat sites do not share a delimiter source. `group()`'s dict-free check reads the wire; `group_slices_status()`'s splitter reads the per-context dictionary store (083's change, which moved 330 contexts' delimiters). Calling both "flat" is true but incomplete — they can split on different keys.

**And this asymmetry is already live over the *same* extent, benignly.** `group_slices_status`'s splitter walks `[first, group_end]` with the same boundary predicate as the flat cap loop (`:712-715` vs `:585-586`) on a delimiter resolved through `group_delim_fn_` (`:704-711`) while `group():551` reads the wire — two walks, one extent, independent sources, on shipped `main` since 083. It does not under-enforce the cap, because dictionary-path enforcement lives in the nesting-aware walk (`:521-524`), not in either flat walk. Contract **C-1** cites this as the shipped proof that divergent delimiter *sources* are not by themselves a cap defect — which is why C-1 freezes the nesting-aware walk's cap/extent coupling rather than a delimiter-source equality. *(Cross-reference added at Gate A round 1: the round-1 contract asserted the opposite of what this row already recorded.)*

---

## E-4 — Membership context (`group_context`)

Threaded into `consume_group_extent` for the child-context probes at `:492` and `:511`. **Not touched by this feature** and listed only so that its absence from the dict-free branch is explicit: on that branch there is no dictionary, hence no membership, hence no context, hence no possibility of a nesting-aware rule. That is the structural reason the flat check survives there rather than a decision that could have gone the other way.

---

## Validation rules derived from requirements

| Rule | Source | Enforced by |
|---|---|---|
| `consume_group_extent` caps the same nesting-aware instances whose extent it returns (`:477-478` opens · `:521-524` caps · `:527` returns) | Contract **C-1** — **STANDING** Split by half. **Cap enforcement exists**: the dictionary pin `WireOffsetTable.DoSCapPerInstanceRejectsOversizedSingleInstance`, proven RED by the FR-005b mutation (SC-003) — **post-relocation only**; on baseline the flat loop pre-empts and the pin stays green (measured, R-1 step 4). **Cap coupled to the returned partition**: source inspection of `:477-478`/`:521-524`/`:527` + the FR-002 source comment (SC-005a). The mutation's fixture is one **unnested** instance and cannot reach the coupling *(narrowed at Gate A round 2)* |
| `delim` at `group():551` and `consume_group_extent():458` resolve to the same entry **at `c1564dd2`** | R-1 step 1 → Contract **C-1a** — **delivery-time premise, not standing** | Source inspection at delivery (`quickstart.md` §1c). Nothing carries forward: after the removal the equality governs group *recognition* at `:566`, not cap accounting |
| Dict-path `group()` performs exactly one traversal | FR-001, SC-005 | Source inspection **plus** the FR-001b red-first structural pin `WireOffsetTable.FR001_SingleTraversalSourceInspection` (SC-005b) — RED on baseline, GREEN after the relocation *(added at Gate A round 2; this rule was discharged by a human read alone until then)* |
| Dict-free cap enforcement is semantically unchanged from pre-085 | FR-003, FR-001a | C-3's nine-item checklist walked over the diff + ~16 existing dict-free tests |
| Cap fires on both paths | FR-004 | Existing dict-path pin + **two new** dict-free cases (`WireOffsetTable.DictFreeDoSCapPerInstance*`) |
| Both pins are load-bearing | FR-005, FR-005a, FR-005b | Two mutation transcripts (dict-free site + `consume_group_extent:521-524`) + the bracketing companion |
| No public surface change | FR-008 | This document + existing ABI gates |

## State transitions

None. `OffsetTable::group()` is a `const` query with no state machine; `group_slices_` / `group_index_` caching is upstream in `group_slices_status` and is not reached by this change.
