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
- **v42 — DEFERRED (discovered at /implement, issue #196):** FIX 4.2 types its
  `NumInGroup` count fields as legacy XML `INT` (not `NUMINGROUP`), so the emitter
  materializes **zero** typed repeating groups (L-063-1); a scalar-only v42 builder
  would silently omit `required='Y'` groups (e.g. `NewOrderList`/`NoOrders`) → invalid
  FIX 4.2. Fixing it regenerates the v42 read golden (out of FR-009 scope). v42 emits NO
  builders in 077. **Builder-bearing versions are v44 / v50sp2 / vlatest.**
- **v50sp2 / vlatest** — emit builders for their genuine
  `is_application` set (156 / 173). The v44 `kN002N003Excluded`
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

## R2-recon (T005, /implement)

Independent raw-XML/Orchestra walk (NOT `ir(V).messages` — a standalone
Python re-implementation of `walk_level`/`walk_orchestra_level`'s
declaration-order + component/group expansion, cross-checked against
`ir.cpp` by source read), run against `dictionaries/FIX42.xml`,
`dictionaries/FIX44.xml`, `dictionaries/FIX50SP2.xml`, and
`dictionaries/orchestra/OrchestraFIXLatest.xml`.

### (1) The assigned 58-vs-59 question — RESOLVED, and the R2 Scope Note's
### guess was wrong about *which* group closes the gap

`spec.md`'s "59 distinct read-tier group flyweights" is confirmed by direct
count against the checked-in golden
(`specs/003-dictionary-codegen/contracts/golden/v44_Messages.golden.hpp`:
`grep -c '^class G_'` = 59, e.g. `class G_627 { … }` is present as the
*first* group in the file).

The R2 census's "58" is **not** "the application-message group set" as the
Scope Note speculated. It is the distinct-`no_tag` count over **all 93 v44
messages' BODY only** (app + admin, `<message>` node scope — the same scope
`populate_group_order`/`MessageIR.group_order` uses, INV-2 body-only). The
reconciling element between 58 and 59 is **`NoHops`/627** — a `<header>`-only
group (`<header><group name="NoHops">…</group></header>`, present via the
framing envelope on *every* message, admin or app) — not an admin-message
body group. `NoHops` is excluded from `group_order`/the builder's scope by
construction (body-only re-parse never visits `<header>`) but *is* read by
the (whole-wire) read tier, hence it's in 59 but not 58.

The admin-message body set contributes a **different** single group:
`NoMsgTypes`/384 (`MsgTypeGrp`), found only in `Logon`(`A`)'s body. This is
real but is the cause of a *different* discrepancy (below), not the
58-vs-59 one.

### (2) App-message counts — VERIFIED against raw XML/Orchestra (all four match the pinned values)

| Version | raw-XML/Orchestra count | pinned | verified |
|---|---|---|---|
| v42 | `msgcat='app'` = 39 | 39 | ✓ |
| v44 | `msgcat='app'` = 85, minus `{BE,BF}` (both confirmed `msgcat='app'`) = 83 | 83 | ✓ |
| v50sp2 | `msgcat='app'` = 156 (0 admin messages — FIXT session split to FIXT11.xml, confirmed) | 156 | ✓ |
| vlatest | `category != "Session"` = 173 (8 `Session`-category messages: `0,1,2,3,4,5,A,n` — matches v44's admin msgtype set exactly) | 173 | ✓ |

### (3) Plan counts (29/89/558/578) — NOT app-only; census-derived; MATERIAL correction found; ⚠️ SCOPE-DECISION NEEDED before any golden freeze (T010/T013/T017/T023)

Reproducing the R2/R3 recursive-signature census independently (delimiter +
ordered `(tag, required, {child-signature})`, keyed `(no_tag, signature)`,
exactly Entity 1's definition) over **ALL messages (app+admin) per version**
reproduces the pinned figures **exactly**: v42 29 plans/18 no_tags, v44 89/58,
v50sp2 558/505, vlatest (full `<fixr:structure>` incl. `StandardHeader`/
`StandardTrailer`) 578/524 — confirming the census methodology is sound and
matches R2/R3's own numbers bit-for-bit. **But this is the ALL-MESSAGES
scope, not the builder's actual `is_application`-filtered emission scope**
(data-model Entity 4; `vt11`'s "0 builders" edge case is exactly this
principle already). Restricting the SAME census to **app-only** messages
(matching what the current v44 emitter — and T009's per-version
`is_application` predicate — actually visits) gives:

| Version | ALL-messages plans (pinned) | APP-only plans (builder's actual scope) | delta | cause |
|---|---|---|---|---|
| v42 | 29 | **28** | −1 | `NoMsgTypes`/384 (Logon, admin-only) |
| v44 | 89 | **88** | −1 | `NoMsgTypes`/384 (Logon, admin-only) |
| v50sp2 | 558 | **558** | 0 | 0 admin messages — no delta |
| vlatest | 578 | **576*** | −2 | `NoMsgTypes`/384 (Logon/Session, admin-only) **+** `HopGrp`/627 (`StandardHeader` componentRef id 1024, header-only — see methodology note below) |

`*` vlatest additionally needed a **methodology correction**: the v42/v44/
v50sp2 raw-XML walk starts at each `<message>` element (no `<header>`/
`<trailer>` — those are separate top-level XML elements, never walked), so
it was already body-only by construction. The first vlatest pass instead
walked the *full* `<fixr:structure>` (Orchestra has no separate header/
trailer elements — `StandardHeader`/`StandardTrailer` are ordinary
`componentRef`s *inside* `<fixr:structure>`, `orchestra_loader.cpp:730-732`),
which pulled in `HopGrp`/627 (`StandardHeader`'s `groupRef id="2085"`) —
present on every message, admin or app. Re-running with the top-level
`StandardHeader`(1024)/`StandardTrailer`(1025) `componentRef`s excluded
(mirroring `emit_builders.cpp`'s `top_level_synthetic_members`
header/trailer exclusion at the entry point, so the recursion never
reaches `HopGrp`) gives 577 ALL-messages / **576 APP-only** — the number in
the table above. This asymmetry (present only for vlatest's Orchestra walk)
is now closed; v42/v44/v50sp2 needed no correction.

**Why this is a scope-decision, not just a note-worthy delta**: T008 (the
dedup mechanism) is unwritten as of this task. Whether the shipped golden's
struct count is the ALL-messages figure or the APP-only figure depends
entirely on **how T008 discovers/interns plans** — over `ir.messages`
unfiltered, or over the same `is_application`-filtered set the emitter
already visits per-message (mirroring the *existing* v44 emitter, which
never touches `Logon`/`NoMsgTypes` today because `Logon` is `msgcat=admin`
and out of the OFFICIAL/ALL builder scope). The current (pre-077) v44
emitter's proven behavior and Entity 4's `vt11` edge case (0 application
messages ⇒ 0 builders) both point toward APP-only discovery — which would
make the **pinned 29/89/558/578 wrong by one (v42/v44/vlatest) or two
(vlatest)**, and the corrected figures are **28/88/558/576**. But this
census cannot itself decide T008's design; it can only show that the two
candidate answers differ and by how much, and name the reason. **No golden
expectation (T010, T013, T017) or completeness-census pin (T023) may be
frozen against either set of numbers until the orchestrator names which
discovery scope T008 uses** — recommend re-confirming against the T010
regenerated golden's actual struct count once T008 lands, and updating
Entity 2's "Count per version" table (data-model.md) to whichever this
resolves to (currently reads 29/89/558/578, unverified against the
builder's real is_application-filtered scope).

### (4) T004 cross-reference — required-ness source for vlatest

For the record (T004 detail, not re-litigated here): the *builder's*
required-ness source is `MessageIR.fields[tag].ref.rule` (`ir.hpp`
Entity-1-cited field), populated for vlatest via
`dict.message_fields()` → `OrchestraLoaderState::expand_field_list`
(`orchestra_loader.cpp:473-570`, `.rule` set at lines 494/543 per XML
occurrence) → tag-deduped per message (`orchestra_loader.cpp:684-709`,
`std::ranges::sort`+`unique` by tag, **not stable** — order among
same-tag duplicates after sort is unspecified). This is a *different*,
lossy path from `ir.cpp`'s own `occurrences[].rule` (076's lossless
per-occurrence record, `populate_orchestra_projection`/
`walk_orchestra_level`, `ir.cpp:350-358/392-400`), which the completeness
census manifest uses but the builder does not read. An independent raw-XML
census (same script as above) found **zero** intra-message tag-reuse with
conflicting `presence` across all 181 EP303 messages — the tag-keyed lookup
is empirically sound for the current corpus (mirrors v44's existing N3
census finding), though the unstable-sort dedup remains an unguarded
invariant (no load-time or build-time assertion) that a future EP303
revision could silently violate. Not a T008 blocker; flagged as a residual
risk, not fixed here (out of this task's scope).
