# Phase 0 Research: Typed builder tier via group-Args deduplication

**Feature**: 077-builder-args-dedup | **Date**: 2026-07-16

This resolves the one `/plan`-deferred unknown that gates the design: **how to
key the deduplicated builder Args**, and the per-version scope/sizing that
falls out of the same investigation.

## R1 — Root cause (confirmed by source read)

`emit_builders.cpp::resolve_level` names every nested group's Args struct by a
**message-rooted path prefix**: `child_prefix = type_prefix + to_identifier(stripped)`
(emit_builders.cpp:415), where `type_prefix` starts at the message id
(emit_builders.cpp:713) and accumulates down every path. `emit_args_struct`
(emit_builders.cpp:422) is therefore called **once per message, per structural
path** — the same repeating group (e.g. `NoPartyIDs`/453) reappears as
`ExecutionReportPartyIDsArgs`, `NewOrderSinglePartyIDsArgs`, … On FIX Latest's
depth-7 reused components this is the 137 MB / ~53 k-struct blowup (L-076-1).

The `if (ir.ns != "v44") return {}` gate (emit_builders.cpp:658) is why no
version but v44 has a builder tier today.

## R2 — Why the read tier's key does NOT transfer (the load-bearing finding)

The read tier (`emit_messages.cpp`) dedups by a **`no_tag`-keyed union**: it
builds `MemberMap gmm : group_no_tag -> union of member fields, deduped by tag`
(emit_messages.cpp:408-419) and emits one superset `G_<no_tag>` flyweight.
Reading is **order-independent** (accessors scan by tag) and
**required-ness-agnostic**, so a superset serves every message that contains
the group.

A **builder cannot union**. It is:
- **serialization-order-sensitive** — `build_<Msg>` emits fields in declaration
  order from `MessageIR.group_order` (per-message, NOT tag-sorted); and
- **required-ness-sensitive** — `validate_<Msg>` / `writer_traits` bake in each
  member's `FieldRef.rule`.

So a builder shared-Args struct is correct only if a group's *ordered members +
required-ness + children* are invariant across every message/path that contains
it. **The read tier compiling proves nothing about this** (it unions away the
variance). This distinction is what the earlier spec draft ("keyed by
`no_tag`") missed.

### The discriminating census (grade-1, ran it)

An IR-level census (throwaway program linking the real `build_ir()` +
`libfixpp_dictionary` + pugixml; sources in the feature scratch record) walked
every version's `group_order`, computing per group occurrence a **recursive
structural signature** = `delimiter + [ (tag, required, {child-signature if
group}) … ]`, then counted distinct signatures per `no_tag`.

**Result — `no_tag` alone is NOT a sound key:**

| Version | app msgs | admin | distinct `no_tag`s | `no_tag`s with >1 structural plan | max plans / `no_tag` |
|---|---|---|---|---|---|
| v42 | 39 | 7 | 18 | 7 | 3 (`NoOrders`/73) |
| v44 | 85 | 8 | 58 | 12 | 8 (`NoLegs`/555) |
| v50sp2 | 156 | 0 | 505 | 22 | — |
| vt11 | 0 | 8 | 0 | 0 | — |
| vlatest | 173 | 8 | 524 | 22 | — |

**Scope note (v44 58 vs spec.md read-tier 59).** This census's "distinct
`no_tag`s = 58" is the **application-message** group set (the builder's scope);
`spec.md:15`'s "v44 59 distinct read-tier group flyweights" counts the read
tier, which covers **all** messages including the 8 admin/session ones — so the
+1 is most plausibly one admin-only group not in the app census. This direction
is **not yet verified against source** (v50sp2 505 and vlatest 524 match exactly
between the two, which is consistent with FIX50SP2/EP303 having admin split into
FIXT11); `/tasks` reconciles it by re-running the app-only vs all-message count
before pinning any golden expectation.

These are **genuinely different groups** sharing a `NumInGroup` count tag:
`NoOrders`/73 carries a full order in `NewOrderList`, status fields in
`ListStatus`, alloc refs in `Allocation`. Forcing them into one shared struct
would corrupt serialization and validation.

## R3 — Design decision: key by recursive structural identity

**Decision**: dedup builder group-Args by the key **`(no_tag, recursive
structural signature)`** — the recursive signature (delimiter + ordered
members + required-ness + child signatures) discriminates variants *within* a
count tag; pairing it with `no_tag` closes the reverse direction the census
below did not measure (two distinct `no_tag`s whose bodies share a byte-identical
signature stay separate plans, since their count tags differ). Emitted once into
`fixpp::<ns>::groups`, named:
- `G_<no_tag>Args` when a `no_tag` maps to exactly one signature; and
- when a `no_tag` maps to two or more signatures, no bare name and **all**
  variants ordinaled `G_<no_tag>_1Args` … `G_<no_tag>_kArgs`, ordinal assigned
  by **first-encounter over the bytewise-sorted message list** (deterministic).

`writer_traits<T>` specializations and the `_required_` / `_count_` /
`_validate_entry_` helpers are emitted **once per distinct plan** (not per
message), in post-order (children before parents — preserves the existing
`[temp.expl.spec]` completeness discipline in emit_builders.cpp:489-497).

**Rationale**: correct by construction (identical subtrees collapse; genuine
variants stay distinct), and it *does* tame the blowup:

The **emit N** column is the `/plan` census's distinct-plan figure, counted as
**distinct signatures per `no_tag`** (the R3 dedup key). Under the
`(no_tag, signature)` key this IS the **exact** distinct-`(no_tag, signature)`-pair
count for the dictionaries the census measured: summing distinct signatures per
`no_tag` is by definition the count of distinct pairs, and two *distinct* `no_tag`s
sharing a byte-identical signature are already two separate pairs (their count tags
differ), already counted separately — nothing is under-counted, nothing ticks up.
The only residual caveat is **census-model-vs-shipped-emitter fidelity** (this
throwaway census's signature model vs the shipped emitter's dedup), which is
**pinned deterministically by the regenerated golden at `/implement`** — the golden
size, the completeness count, and the `G_<no_tag>[_ord]` naming all key off that
regenerated figure.

| Version | naive per-path group structs | **distinct `(no_tag, signature)` pairs (emit N)** | reduction |
|---|---|---|---|
| v42 | 38 | 29 | 1.3× |
| v44 | 730 | 89 | 8× |
| v50sp2 | 25,927 | 558 | 46× |
| vlatest | 26,806 | **578** | **46×** |

578 distinct plans is the **same order as the read tier's 524** unioned
flyweights (whose single-file `Messages.hpp` is ~9.7 MB and compiles) — so a
single-TU deduped `Builders.hpp` in the ~10 MB regime is the expected outcome
(SC-001/SC-002).

### Alternatives considered

- **`no_tag`-keyed (spec's original framing)** — REJECTED: unsound (R2 census).
  Would need per-message override structs anyway, defeating the dedup.
- **Content-hash names (e.g. `G_<hash>Args`)** — REJECTED for readability /
  golden-diff legibility; `no_tag` + ordinal is deterministic, human-readable,
  and keeps continuity with the read tier's `G_<no_tag>` naming.
- **File-splitting (per-message/per-category folders + `all.hpp`)** — OUT OF
  SCOPE (spec); it re-aggregates the same 53 k structs and does not address the
  duplication. Revisit only on measured post-dedup compile cost.

## R4 — Per-version scope (FR-006)

- **vt11** — 0 application messages (8 admin/session). **No builder output**
  (consistent with clarify: admin builders are OUT). Not an error.
- **v42 / v50sp2 / vlatest** — emit builders for their genuine
  `is_application` set (39 / 156 / 173). The v44 `kN002N003Excluded`
  set `{BE,BF,BW,BX,BY}` is **v44-specific** (frozen for its 067/069 golden;
  BW/BX/BY are FIX 5.0 messages absent from FIX44). Per clarify, other versions
  do **not** inherit it — they emit their full application set. (v50sp2 has 0
  admin messages: the FIXT session layer is split into FIXT11.xml, so
  FIX50SP2.xml is entirely application.)
- **v44** — unchanged in-scope set: `all` = 83 (`is_application` 85 minus
  `{BE,BF}`; BW/BX/BY absent from FIX44), `official` = frozen 33. Both goldens
  regenerated to deduped output (FR-007a).

`is_application` is the app/admin **rule** (read `msgcat`/`category`, set on
`MessageIR.is_application` — ir.hpp:99) the emitter's own IR uses. The FR-010
completeness census does **not** read `MessageIR.is_application` (that would be
circular — the emitter's own IR); it independently re-implements the identical
rule on a standalone raw-XML/Orchestra walk (contracts §C1's `raw_walk(V)`),
which is what makes `expected(V)` non-circular.

## R5 — Determinism, ordering, and the fail-closed completeness gate

- **Determinism**: distinct-plan discovery + ordinal assignment run over the
  already-bytewise-sorted `ir.messages` and declaration-order `group_order`, so
  output is byte-stable (the existing `codegen_determinism_test` covers it).
- **Dependency ordering / cycle bound**: keep the read tier's discipline —
  emit a plan's children before the plan (post-order), bound pathological
  cyclic/over-deep reuse like `kMaxGroupDepth`, and never reference an
  undefined Args type (FR-011).
- **Completeness gate (FR-010)**: for every builder-bearing version assert
  `{ emitted build_<Msg> } == { independently-derived in-scope application set }`
  (exact-set equality), and prove it can go red by dropping a message. This
  re-instates 076's descoped V-2/V-2b legs and generalizes them.
- **Structural-key safety pin**: the plan-variant discovery is itself the
  named-invariant regression pin — a future dictionary bump that introduces a
  new structural variant of an existing `no_tag` is absorbed automatically
  (a new `_<ordinal>` struct), and the golden diff makes it visible.

## R6 — Constitution amendment (Gate A)

Codegen is an Appendix-A mandatory Gate-A trigger. Delivering FIX Latest
builders re-narrows **Article I §1** (drops the "typed `build_<Msg>` builder
codegen" post-1.0 carve-out for FIX Latest); delivering v42/v50sp2 builders
reclassifies **Article XVIII §7** (v42/v50sp2 app-message builder widening,
currently v1.x-deferred) as v1.0-delivered-by-077. Folded into Gate A per the
074/075/076 precedent.
