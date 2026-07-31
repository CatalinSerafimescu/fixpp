# Phase 0 Research — Group Delimiter Resolution

**Feature**: `083-group-delimiter-resolution` | **Date**: 2026-07-30

All Technical Context unknowns are resolved below. No `NEEDS CLARIFICATION` remains.

---

## D-1 — Delimiter source: capture the first emission, do not add a scan

**Decision**: Capture a group's delimiter as **the first `FieldRef` emitted at that group's level during message expansion**. Do not write a document-order scan.

**Rationale**: `LoaderState::expand_field_list` (`src/dictionary/xml_loader.cpp:525-670`) already emits in document order, and two properties verified in source make the first emission exactly the FIX delimiter:

1. A `<component>` child is expanded **inline at the same level** — `expand_field_list(def.node, …, enclosing_group_no_tag, …)` at `:576-579` passes the *enclosing* group through unchanged. So component members appear at the enclosing group's level, in document order, to arbitrary depth. **This is FR-003 for free.**
2. A `<group>` child's own count-tag `FieldRef` is built with `no_fr.group_no_tag = enclosing_group_no_tag` (`:594`) and `out.push_back(no_fr)` (`:597`) **before** the recursion at `:667` descends into it. So a nested group's count tag appears at the outer level, in position. **This is FR-004 for free.**

Implementation is a per-group frame carrying `delim = 0`; the first emission at that level sets it; on pop the record `(msg_type, path, no_tag) → delim` is emitted. O(1) per group, no second traversal, no extra allocation on any hot path (this is load-time only).

**Verified in BOTH loaders, not inferred from one.** `OrchestraLoaderState::expand_field_list` (`src/dictionary/orchestra_loader.cpp:520-645`) has the same three properties:

| Property | XML loader | Orchestra loader |
|---|---|---|
| iterates children in document order | `:526` | `:526` |
| component ref expands **inline at the enclosing group's level** | `:576-579` | `:573-577` — passes `enclosing_group_no_tag` through unchanged |
| nested group's count tag pushed at the **outer** level *before* descent | `:594-597`, recursion `:667` | `:606-609`, recursion after `:640` |

So D-1 applies unmodified to both, and C-1.5's symmetry requirement is satisfiable by the same code shape rather than by two different mechanisms. (Orchestra additionally skips a group's own `<fixr:numInGroup>` child during descent because it matches none of the three branches — the count tag is therefore never re-emitted as a member of itself.)

**No silent skip can shift the delimiter.** D-1's correctness needs "first emission == first declared child". Both loaders **throw** on an unresolvable reference rather than skipping it — XML at `:535-541` (`<field name=…> not declared`), Orchestra at `:531-535`, `:562-566`, `:587-591`, `:594-598`. Had either skipped, a leading unresolvable field would silently make the *second* child the captured delimiter, on exactly the dictionaries hardest to notice it on. This disposition must be preserved by the change (contract C-1.2).

**Why this is the load-bearing decision, not merely the cheap one**: the defect being fixed exists *because* `first_field_tag`'s scan (`:610-641`) is a **separate** traversal that drifted out of step with the member expansion — it stops one level into a component while the member expansion recurses fully. Adding a third traversal recreates precisely that failure mode. Deriving delimiter and member set from one walk makes them **unable** to disagree.

**Alternatives considered**:

- *Fix the existing `first_field_tag` scan recursively, keep it separate.* Rejected: preserves two traversals that must stay in lockstep by convention rather than by construction. This is the arrangement that failed.
- *Add a declaration-order index to `FieldRef`.* Rejected outright: `FieldRef` is ABI-pinned at 16 bytes (`static_assert(sizeof(FieldRef) == 16)`, `field_ref.hpp:94`) and is `constexpr` static storage for codegen versions. Growing it is an ABI break for a load-time convenience.
- *Sort `all_fields` stably by declaration order instead of by tag.* Rejected: `all_fields` being tag-sorted is relied on by binary-search lookups across the dictionary layer; re-ordering it is a far larger blast radius than a side table.

---

## D-2 — Ancestor path: thread a stack through `expand_field_list`

**Decision**: Add a `std::vector<std::uint16_t>& group_path` parameter, pushed/popped around the nested-group recursion at `:667`. Emit records **only** from the message-expansion call sites.

**Rationale**: `expand_field_list` currently carries only `enclosing_group_no_tag` — the *immediate* parent, not the path. `as_table_view()` reconstructs the path afterwards by walking an `immediate_parent` chain, which works only because it operates per message. The loader needs the real path at capture time to key the record.

**Critical scoping detail**: `expand_field_list` is called from **three** places, and only one is context-bearing:

| Call site | Purpose | Emits ctx delimiters? |
|---|---|---|
| `:927-931` (header / `md.node` / trailer) | per-message expansion, `msg_type` known | **Yes** |
| `:968` (per component) | populates the component cache | **No** |
| `:1056` (per group) | populates `group_fields_` | **No** |

The latter two are not message-scoped; emitting from them would manufacture contexts with no message type and corrupt the store. Gate emission on an output-sink pointer that is null at those two call sites.

**Alternatives considered**: reconstructing the path post-hoc in `as_table_view()` as it does today — rejected because the delimiter must be captured *during* the document-order walk, and `as_table_view()` only sees the tag-sorted result.

---

## D-3 — Storage: side table on the metadata handle, keyed like `group_ctx_`

**Decision**: A new per-context delimiter table on `dict_metadata_handle`, keyed `(msg_type, parent_path, no_tag)` — the same key shape `table_view::group_ctx_` already uses — exposed by a new handle accessor and consumed by `as_table_view()` at `dictionary.cpp:510-511`.

**Rationale**: reuses a key convention already proven in this codebase (`make_group_ctx_key`), so the loader-side record and the table-view lookup cannot disagree about what a context *is*. PMR-allocated on the caller's resource with the rest of the handle's tables. Load-time only — no hot-path allocation, satisfying Article VIII §5.

**Hot-path cost**: none added. `table_view::group_first_field(mt, path, no_tag)` already performs exactly this lookup (`table_view.hpp:349-365`) with the `group_bit` pre-filter short-circuiting group-free traffic. This feature changes *what value is stored*, not how many lookups happen. FR-022's benchmark exists to confirm that empirically rather than by assertion.

**Alternatives considered**: keying by `(msg_type, no_tag)` only — **rejected, for three reasons, none of which is the one this decision originally gave.**

*Rationale rewritten 2026-07-30 (Gate A round 1).* The original rejection read: *"it fails the spec's edge case of one count tag reused under two different ancestor paths within a single message, which is the exact shape of the defect."* That reason is **retired**, on measurement and on inspection:

- **The shape is not reachable.** Zero `(msg_type, no_tag)` pairs are declared under more than one ancestor path, in any of the ten shipped dictionaries (orchestrator Gate A round-1 measurement, 2026-07-30 — see spec.md's retired Edge Case for the per-dictionary counts). It is also not "the exact shape of the defect": the measured defect is entirely **cross-message** divergence, and `(msg_type, no_tag)` already carries `msg_type`, so it would discriminate that perfectly. Rejecting an alternative on grounds the alternative satisfies is not a rationale.
- It also could not be honoured if it were reachable — see **D-11** and `contracts/group_ctx_delims.md` C-3.5: the consumer registers at most one context per `(msg_type, no_tag)` because `message_fields(mt)` is deduped by tag.

The reasons path keying is nonetheless correct, in order of weight:

1. **Key-shape agreement with the consumer is the whole point of the table.** C-2.2 requires this table to be keyed identically to `table_view::group_ctx_`, which *is* path-keyed (`make_group_ctx_key(msg_type, parent_path, no_tag)`). A `(msg_type, no_tag)` table would force a lossy projection at every single lookup — and that projection is exactly where a consumer-side miss gets manufactured (D-11 / FR-023). Two stores that disagree about what a context *is* is the class of defect this feature exists to remove; introducing a second such disagreement to save a key field would be a poor trade.
2. **The path is already in hand at capture time.** D-2 threads the real ancestor path through `expand_field_list` because the delimiter must be captured *during* the document-order walk. Keying coarser would mean deliberately discarding a key the walk produces naturally, and synthesising one it does not.
3. **The structural limit is at the consumer, not at the store.** The tag-dedup is a property of `message_fields`/`as_table_view()`, not of this table. Recording the finer key means that when that limit is lifted — by #196/082's structural group detection, or by a `message_fields` fix — Entity 2 already carries correct data and nothing has to be re-derived. Records currently unreachable to the consumer are documented **dead data, not an error** (C-3.7), which is a materially different disposition from a store that never recorded them.

---

## D-4 — Descend at the delimiter

**Decision**: In `consume_group` (`include/fixpp/wire/validator.hpp:357-406`), replace the bare `++i` that consumes the instance-opening delimiter (`:362`) with a query-before-consume: if `delim_tag` is itself a group **in child context**, recurse via `consume_group` and resume one past its extent; otherwise `++i` as today.

**Rationale**: this mirrors the descent the scanner *already* performs for post-delimiter members at `:376`, using the same `can_descend` depth guard and the same `child_path`. It is a symmetry repair, not a new mechanism — which keeps FR-009 (bounded depth) satisfied by the existing K=16 cap rather than by new logic.

**Why it must precede D-1's recursion**: measured — 232 FIX50SP2 contexts have a post-fix delimiter that is a nested group's count tag, plus 30 more once the three silently-dropped groups register. Landing recursive resolution first turns those into false rejections.

**Invariants that must not move** (FR-008): instance-count enforcement at `:402`, required-member masking, and extent termination at `:365`. The delimiter's required-bit is marked *before* descent (`delim_k`, `:354`), and must remain so.

---

## D-4a — The SAME repair at the SAME asymmetry, in the offset table *(NEW — Gate A round 3, 2026-07-31, user scope amendment)*

**Decision**: apply D-4's descend-at-the-delimiter repair at the **second** site that implements the same scan — `OffsetTable::consume_group_extent` (`src/wire/offset_table.cpp:438-503`). Before the bare `++k` at `:475`, probe whether `delim` heads a group in the **child** context already computed at `:466`, and if so advance via the existing `consume_group_extent` recursion under the existing `kMaxGroupDepth` guard, resuming one past its extent.

**Why it was missed for three rounds, recorded because the mechanism generalises.** The two functions are structurally the same scan in two subsystems, and the bundle *correctly diagnosed the defect in one while asserting its absence in the other* — the `feedback_half_restructure_symmetric_api` shape, reached not by forgetting the second site but by **censusing** it (FR-021c) and then reasoning about the wrong property. The census asked *"where does a delimiter come from?"* and answered it well; the defect is *"where does the walk descend?"*, and a site can be sound on the first question and broken on the second. Round 2's role column names both roles at `:454` and still scored the traversal correct, because the phrase *"already nesting-aware"* was checked against the descent that exists (`:485-488`) rather than against the position that lacks one (`:475`).

**Why the splitter cannot compensate.** `group_slices_status` takes `group_end = first + gi->entry_count()` (`:648`) from `group()`, which takes it from `consume_group_extent` (`:550`). C-8.5's nested-extent skipping operates strictly *inside* `[first, group_end)`. **A truncated extent cannot be widened by a smarter split** — which is why this is a scope question and not an implementation detail `/speckit-tasks` could absorb.

**Why the timing is what makes it a defect rather than a latent flaw.** Pre-083 the two paths agree *because both are broken*: validation rejects such messages outright (wrong `delim_tag` → `wire_required_field_missing`), so no caller reaches the read path. Phase 2 (FR-007) fixes validation. Without D-4a the feature therefore **converts a loud rejection into silent instance loss** across the 262 contexts, through `MessageView::group<>()` and the C-ABI top-level group getter. The harm is created by this feature; the code shape predates it.

**Bounded, not new, mechanism**: same probe shape as `:485-486`, same `child`, same depth guard, same overflow early-return. The overflow return is load-bearing for **termination in bounded time**, not diagnostics — the depth-cap branch returns its input index unchanged (`:444`), so a descent that swallows `overflow` does not advance `k` and the outer loop at `:473` re-tests the identical entry. **It does not spin** *(corrected at Gate A fresh loop round 1)*: that loop carries `inst < declared` and `++inst` at `:500` is unconditional, so the walk performs exactly `declared` **no-op iterations** — a **DoS, not a hang**, since `parse_bounded_u32` saturates `declared` at `UINT32_MAX` (`include/fixpp/wire/tag_scan.hpp:63-79`) and no instance-count cap exists. **This bundle's own D-4a lesson, applied to D-4a**: the spin argument is correct for the sibling `consume_group` (`include/fixpp/wire/validator.hpp:357`, whose outer loop has **no** instance bound) and was transplanted here without being re-derived against this site — a site sound on the first question and broken on the second, one level up. See C-8.0c.3, and W-10a leg 4 for the discriminator the wrong claim had left non-discriminating.

**Landed in**: spec **FR-021e** / **SC-016** / US5 scenario 3; `contracts/typed_read_splitter.md` **C-8.0c** (+ C-8.0b's first bullet withdrawn, C-8.6a, W-9 fixture (c), **W-10 re-stated**, **W-10a** new); `contracts/consume_group.md` **C-4.4** (pointer); `plan.md` Phase-4 gate + Article IX §1 branch (11); `data-model.md` Entity 6; `quickstart.md` §4.

### D-4a per-instance cap assessment — MEASURED (T024), and the bundle's stated reason is falsified

C-8.0c.4 recorded this as *"inferred, to be confirmed"*: **no shipped shape breaches `default_max_group_entries_per_instance = 4096`** (`include/fixpp/wire/offset_table.hpp:28`), *"because … no group instance in the ten dictionaries approaches 4096 entries."* Measured at T024 by a scratch probe over all ten dictionaries — for every group in every message, the **static per-instance entry count** (direct members, plus one instance's worth of each nested group, recursively), computed **twice**: post-C-8.0c, and pre-C-8.0c (where a group whose delimiter is a nested count tag truncates to exactly **1** entry, since the bare `++k` lands inside the nested group and `:478` breaks the instance).

**Re-measured after Phase 5 with corrected provenance** *(the first run classified the C-8.0c population from `Dictionary::group_first_field(g)` — **store #1, the global** — which Phase 5 turned into a first-seen projection. That is T012's provenance error one layer over, and it reported FIX50SP2's population as 0. The table below classifies from the **per-context store #3** (`table_view::group_first_field(mt, path, no_tag)`), which is what the runtime actually resolves.)*

| dictionary | groups | C-8.0c popn | max **pre** | max **post** | max Δ | groups > 4096 |
|---|---:|---:|---:|---:|---:|---:|
| FIX40 / FIX41 / FIX42 | 0 | 0 | 0 | 0 | 0 | **0** |
| FIX43 | 234 | 0 | 133 | 133 | 0 | **0** |
| FIX44 | 823 | 0 | 229 | 229 | 0 | **0** |
| FIX50 | 1114 | 0 | 301 | 301 | 0 | **0** |
| FIX50SP1 | 1309 | 0 | 326 | 326 | 0 | **0** |
| FIX50SP2 | 25927 | **240** | 4028 | **4046** | **1263** | **0** |
| FIXT11 | 8 | 0 | 3 | 3 | 0 | **0** |
| Orchestra FIX Latest | 26806 | **245** | 4064 | **4082** | **1265** | **0** |

**Cross-validation**: 240 + 245 = **485**, matching SC-016's corrected figure and the checked-in census pin's `nested_total` column **cell for cell** (FIX50SP2 240, Orchestra 245). Two independent measurements — this probe and `delimiter_census_test` — agree exactly, which is what makes the population figure trustworthy rather than merely repeated.

**The conclusion holds: zero groups cross the cap, so C-8.0c creates no new SC-007 rejection on any shipped schema shape.** The *reason* given does not. The global maximum is **4082 against a cap of 4096 — 14 entries of headroom, 99.66 % of the cap** (Orchestra `X/268`; then Orchestra `DC/1889` at 4062 and FIX50SP2 `DC/1889` at 4046, 50 entries clear). "No shipped shape approaches 4096" is false; five shapes sit above 4000. The assessment survives by a **thin margin that has to be stated**, not by the wide one the clause assumed.

**Where C-8.0c's growth actually lands — small at the top, large at the bottom.** Both dictionaries with a C-8.0c population are also the two nearest the cap. The largest deltas are the C-8.0c groups **themselves** — `CD/1499` **1 → 1266** (Orchestra) and **1 → 1264** (FIX50SP2), `CC/1499` **1 → 1262** / **1 → 1260** — which is the truncation the repair removes, quantified. The shapes near the cap grew by only **18–23** (`X/268` 4064 → 4082, `y/146` 3966 → 3989), because they merely *contain* a C-8.0c group rather than being one. So the repair spends ~18 of each dictionary's remaining headroom: Orchestra from ~32 to **14**, FIX50SP2 from ~68 to **50**.

**Why the deduped source does not understate it** *(checked, because a 14-entry margin cannot rest on an unverified assumption)*. The probe builds its parent→children map from `Dictionary::message_fields(mt)`, which is **deduped and tag-sorted** — so a tag reachable under two distinct groups in one message would lose an edge and the count would be a *lower* bound rather than the bound. Two things close it. **(a) Measured**: across every message of FIX50SP2 and Orchestra FIX Latest — the only two dictionaries anywhere near the cap — there are **0** messages with a repeated tag in the field run and **0** tags carrying more than one distinct `group_no_tag`. There is no edge to lose. **(b) Structural, and the stronger leg**: the runtime's *own* membership sets are built from this **same** deduped run (`src/dictionary/dictionary.cpp:449-462` iterates `all_fields = message_fields(mt)`), and `consume_group_extent`'s instance-termination test at `:478` consults exactly those sets. A tag the dedup dropped from group B is **not a member of B at runtime**, so an entry carrying it inside B's instance breaks the inner loop and *ends* the instance rather than extending it. 4082 is therefore the bound on what the walk can count, not a floor under it.

**What this measure is NOT, stated so it is not over-read.** It is a **static schema** bound — one instance of every nested group, every declared optional field present. It is **not** an upper bound on wire entries: a message carrying ≥ 2 instances of a nested group inside one outer instance exceeds it, and can cross 4096 **with or without** C-8.0c. What C-8.0c changes, on the **485** contexts of the C-8.0c population (FIX50SP2 240 + Orchestra 245), is that the crossing becomes reachable at a **lower nested-instance count**. That is the **fail-closed** direction relative to today, where the identical message is **silently truncated** instead — rejection replaces silent instance loss, which is D-4a's whole point. FR-015's caller-tunable `Config::max_group_entries_per_instance` is the mitigation and already exists.

---

## D-5 — Member-set injection becomes redundant, not removed

**Decision**: Leave `set_group_first_ctx`'s call to `add_group_member_ctx` in place — the call is at `table_view.hpp:645`, inside the function at `:641-646` *(line verified at source 2026-07-31, Gate A round 3; see `group_ctx_delims.md` C-3.3 for why the round-1 and round-2 values were both off by one)*. Pin that it has become a no-op.

**Rationale**: a correctly-resolved delimiter is, by construction, a declared member of its own context — it is the first thing the member expansion emits at that level (D-1). Measurement corroborates: `missing == 0` in all ten dictionaries and the only ever-extra tag is the delimiter itself. So correcting the delimiter removes all 52 pollutions without touching the injection.

This is why the issue's fix option (b) is **unnecessary rather than insufficient**. Removing the injection would be a second, independent change with its own regression surface, delivering nothing the delimiter fix does not already deliver. FR-015 follows: member-set exactness is asserted by the *same* pin, not a second one.

---

## D-6 — Typed-read splitter: characterise, then source from the dictionary

**Decision**: FR-021a first — establish by measurement whether `offset_table.cpp:643-680` actually mis-splits. Then source the boundary delimiter from the dictionary's context store rather than from `entries_[first].tag`, and make boundary detection skip nested group extents. **Amended 2026-07-31 (Gate A round 3, user scope amendment on N23)**: and **repair the extent walk the splitter's bound comes from** — `consume_group_extent` must descend when the instance-opening delimiter is itself a nested group's count tag (`contracts/typed_read_splitter.md` C-8.0c, spec FR-021e). Without that, a correct split is applied to a truncated extent and delivers nothing on the 262 contexts that take that shape post-fix. See D-4a below: it is D-4's mechanism at a second site.

**Rationale**: the splitter reads `std::uint16_t const delim = entries_[first].tag` — *the wire's* first entry after the count — and starts a new instance at every reappearance of that tag within the extent. Two distinct failure modes follow: (a) if the wire's first entry is not the true delimiter the split is wrong from the start; (b) if the same tag legitimately occurs inside a nested group within an instance, the scan splits mid-instance.

**Status honesty, corrected 2026-07-30 (Gate A round 1)**: fixpp#208 recorded this as adjacent and explicitly unverified — but **the repository does not**. `spec/behaviors-and-limitations.md` **L-063-4** (`:1701`; `spec/coverage-index.md:995-999`; issue **#180**) already characterises this exact defect: it names the flat splitter and the redundant cap loop by line (`offset_table.cpp:548-558,596-599`), states the correct mechanism (*"the extent walk would still be correct; only the splitter would err"*), records a measurement (*"Fable audit 2026-07-08: 0 nested/parent delimiter collisions across all 6 group-bearing vendored dicts"*), points at an existing synthetic reproduction (`tests/wire/nested_group_extent_test.cpp:511-519`), explicitly corrects an earlier overstatement, and carries a partial mitigation already shipped by 072 (the load-time guard of D-10). FR-021a's characterisation **starts from L-063-4, not from zero** (FR-021d), and L-063-4's row is updated at close-out. Note that its "0 collisions" measurement was taken against the **pre-fix** delimiters this feature changes in 335 contexts, so it must be re-derived here regardless (FR-012a / SC-014). If no mis-split is reachable under the post-fix delimiters, that negative result is recorded and the change reduces to sourcing the delimiter consistently.

**Design point RESOLVED, not deferred (2026-07-30, Gate A round 1)**. The previous text left *"whether the path is available or must be threaded"* to `/speckit-tasks`, while FR-021/021b, data-model Entity 6 and plan.md's Phase-4 gate all required the boundary to agree. A must-fix scope item cannot rest on an open lookup surface. The surface is now specified in **`contracts/typed_read_splitter.md`** (C-8.x), which names the internal seam, the disposition of the other three wire-derived delimiter derivations in the same file, and the test artifact.

**Scope, censused rather than assumed (FR-021c)**: `src/wire/offset_table.cpp` derives a delimiter from the wire at **four** sites, of which **two** are in scope after the round-3 amendment — the splitter at `:656` (delimiter source), and the extent walk's **delimiter-position descent** at `:454` (traversal, not source).

**Role column corrected 2026-07-31 (Gate A round 2, N16).** The round-1 table said "three of the four are membership probes". Two of those three also carry an **instance-boundary rule** derived from the *same* wire local, and mis-labelling them made C-8.0 read as forbidding a change to a boundary rule — including the one L-063-4 names as half of its own recommended fix.

**Scope column corrected 2026-07-31 (Gate A round 3, N23 — user scope amendment).** Round 2's `:454` row asserted the walk is "already nesting-aware … so this boundary rule is not defective". Verified at source, it is nesting-aware **only for post-delimiter members**: the recursion at `:485-488` sits inside the inner loop at `:476`, while the instance-opening delimiter is consumed by a bare `++k` at `:475`. On the delimiter-is-a-nested-group's-count-tag shape the walk therefore truncates the extent to one instance — the shape User Story 2 is about and the post-fix shape of **262 of the 365** affected contexts.

| line | site | role(s) the one wire `delim` serves | in scope |
|---|---|---|---|
| `:454` | `consume_group_extent` | **two.** (i) membership probe confirming the count field really heads a group (`:457`); (ii) instance open/close rule for the extent walk (`:473`, `:476`) + per-instance cap (`:496-499`). Nesting-aware for **post-delimiter members only** (`:485-488`); the delimiter itself is consumed bare at `:475`. | **partly** — local + probe stay wire-derived (C-8.0); the **missing delimiter-position descent is in scope** (C-8.0c / FR-021e) |
| `:526` | `group(no_tag)` | **two.** (i) membership probe / group-recognition (`:541`); (ii) instance-boundary rule in the post-extent cap loop `:559-570` (test `:561`, cap `:566`) — flat, and the *"redundant flat cap loop"* of L-063-4. | no |
| `:597` | `group_slices_` reserve estimator | **one.** `entries_[e+1U].tag` is only the fourth argument to `group_member_fn_` (`:598`) — a membership probe sizing the reservation. | no |
| `:656` | `group_slices_status` — **the splitter** | the instance boundary rule, flat and dictionary-blind | **yes** |

The **membership-probe role** at all three out-of-scope sites stays wire-derived: naively re-pointing it at the dictionary turns it into a tautology, so a blanket class-fix would be wrong. That is precisely why the scoping is stated here — a repeated idiom must be censused before a class-fix is scoped, or `/speckit-tasks` decides it arbitrarily.

**One out-of-scope boundary rule remains, scoped out on its own grounds** (`contracts/typed_read_splitter.md` C-8.0b): `:526`'s only consumer is a `max_group_entries_per_instance` check `consume_group_extent` already applies over the correct boundaries, so the exposure is a false positive/negative on a defense-in-depth DoS cap, never wrong returned data.

**`:454`'s scope-out is WITHDRAWN** *(2026-07-31, Gate A round 3 — user scope amendment on N23).* Round 2's ground — *"`:454`'s already descends through nested extents, so FR-021's failure mode (b) never reaches it and the `group_end` bound the splitter consumes is already right"* — is false at source for a **third** failure mode round 2's (a)/(b) taxonomy does not name: **(c) the outer delimiter IS a nested group's count tag.** There, `:475`'s bare increment consumes only the nested count field, `:478`'s outer-membership test fails on the nested group's first member, and the walk exits with one instance. Since `group_slices_status` takes its bound from that extent (`:648` ← `:550`), the splitter cannot recover it and **FR-021b / SC-010 would be false for 262 of the 365 affected contexts as scoped** — silently, through `MessageView::group<>()` and the C-ABI top-level group getter. The user brought the repair in scope as **C-8.0c** (FR-021e, SC-016, W-10a). It is a **traversal** change at that site, not a source change: the wire local and the probe stay wire-derived, so C-8.0 is untouched and the round-2 census decision is not reopened.

**Recorded residual, not elided** *(re-stated at round 3 — the residual shrank, and by how much matters)*: after Phase 4 this file holds **three** instance-boundary rules — dictionary-sourced and nesting-aware at `:656`; wire-derived but nesting-aware at **both** positions at `:454` (C-8.0c); wire-derived and **flat** at `:561`. The disagreement FR-021 exists to remove is therefore narrowed to **one** site, `:561`, whose exposure is a cap check and never wrong returned data. That is why **C-8.5 still does not claim to "discharge part of #180"**: L-063-4's fix has two legs and this feature lands only the first (*"make the splitter nesting-aware"*), not the second (*"fold the redundant flat cap loop into the same traversal"*). #180 stays open.

**#180's second leg is unchanged by this amendment — but L-063-4 gains a correction that is not one of its legs.** Re-checked at round 3: leg 2 is still, exactly, `group()`'s `:559-570` flat cap loop, and it is still outstanding. What the amendment adds is a **third** close-out disposition: L-063-4's assertion that *"`consume_group_extent()` correctly computes the nesting-aware `group_end` for the outer group"* is **wrong for mode (c)**. Its audit (*"0 nested/parent delimiter collisions"*) is about a nested delimiter **equal to** its parent's, taken against the **pre-fix** delimiters; mode (c) is a different shape, is present in 262 contexts, and becomes reachable **because** this feature corrects the delimiters. FR-021d sends the characterisation to start from L-063-4, so without this paragraph the bundle inherits the wrong leg — which is precisely what happened at rounds 1 and 2.

`:597` needs its own sentence, because it is the one whose *inputs* this feature changes. It sizes a reservation in the fixed 16 KiB inbound parse arena, whose under-reserve failure mode is a **documented silent truncation** (`spec/behaviors-and-limitations.md` L-073-1 top-level; L-065-2 for the nested twin). D-3's impact statement is scoped to lookup *cost* and says nothing about what changed *values* do to a consumer that sizes memory from them. **Assessment (inferred, to be confirmed by the FR-021c task, not asserted as measured):** the risk is low — the tag removed from a polluted member set is the global first-seen delimiter, which is by definition not what a message in that context puts on the wire, so the probe's outcome should be unchanged. "Low, and here is why" belongs in the bundle; the task confirms it against the post-fix member sets.

---

## D-7 — Load disposition: fail-closed default, explicit tolerant opt-in

**Decision**: default = throw naming the group; opt-in tolerant mode = warn and skip. **Behaviour symmetric across both loaders; exception type per loader** — `XmlLoader` throws `xml_parse_error`, `OrchestraLoader` throws `orchestra_parse_error`.

**Why not one shared type (corrected 2026-07-30, Gate A round 1)**: the original decision said *"throw `xml_parse_error` … symmetric in both loaders"*, which would break the Orchestra fuzz harness on every input containing an unresolvable group. `[const §VII.7]` makes new parser-touching code without a fuzz harness a Gate B blocker, and both loaders already have one that enumerates a **documented exception set** and rethrows anything else as an invariant violation:

- `tests/fuzz/fuzz_orchestra_loader.cpp:52-80` — set = `orchestra_parse_error`, `unknown_version_error`, `group_delimiter_collision_error`, `xml_oom_error`, with a terminal `catch (const std::exception&) { throw; }`.
- `tests/fuzz/fuzz_dict_xml_loader.cpp:50-72` — same shape for `xml_parse_error` / `unknown_version_error` / `xml_oom_error`.

`include/fixpp/dict/error.hpp:98` declares `class orchestra_parse_error : public xml_parse_error`, so the Orchestra harness catches the **derived** type. A base `xml_parse_error` thrown from `OrchestraLoader` is *not* caught by `catch (const orchestra_parse_error&)`; it falls through to the terminal rethrow and libFuzzer logs a crash. The XML side is already fine — its harness catches the base.

Throwing the per-loader type therefore lands inside both existing sets with **no harness widening**, and mirrors the taxonomy `include/fixpp/dict/orchestra_loader.hpp:33-37` already publishes (`orchestra_parse_error`, `unknown_version_error`, `group_delimiter_collision_error`, `xml_oom_error`) — that doc comment is updated in the same change (FR-006c). It is also the shape 072 used for `group_delimiter_collision_error`, and it keeps the route `include/fixpp/dict/error.hpp:23-26` records 072 deliberately taking: **no `core::error` enum slot is added**, so the no-`default` `error_message()` `-Wswitch`/`-Werror` co-edit and the `test_020_error_completeness.cpp` slot pin stay untouched. Callers wanting one catch still have it — the derived type *is* an `xml_parse_error`.

**Fuzz artifacts named** (they were absent from Project Structure): `tests/fuzz/fuzz_dict_xml_loader.cpp` and `tests/fuzz/fuzz_orchestra_loader.cpp` (documented-set comments updated to include the new throw's trigger), and `tests/fuzz/fuzz_wire_validator.cpp` (nested-delimiter corpus seed for the `consume_group` descent).

**Rationale**: mirrors the loader's existing disposition rather than inventing one — `xml_loader.cpp` already throws for root-not-`<fix>`, missing `<fields>`, missing/duplicate `number`, bad `type`, and a `<group>` with no matching `<field>` declaration (`:352-470`, `:583`). A silent `first_field_tag = 0` drop is the outlier, and it is what concealed three groups.

**Precondition, sequenced as a task not an assumption** (FR-006b): confirm all ten shipped dictionaries still load under the fail-closed default *before* enabling it. After D-1's recursion the three known offenders resolve, but whether any group unreachable from message expansion still resolves nothing is **not yet measured**. If one does, the tolerant mode is the release valve — but that must be a known state, not a surprise at CI.

**Alternatives considered**: warn-only (rejected by the user's clarification; also inconsistent with every sibling violation), and fail-closed with no escape (rejected by the user, who wanted third-party/partial dictionaries loadable).

---

## D-8 — Oracle extension must be additive

**Decision**: add a **new** `group_delims` map to `DictOracle` beside `group_members`. Do not reshape `group_members`.

**Rationale**: `group_members` is `std::map<GroupContextKey, std::set<std::uint16_t>>` — a `set`, so it cannot express order, which is why nothing pinned a delimiter. The temptation is to change it to an ordered container. That would break census pins consumed by `082-structural-group-detection`, which is **parked on an unbuildable branch** — the breakage could not be observed until 082 resumes. Additive extension keeps both features independent.

The new map is populated by a document-order walk **twinned with** `qfix_walk` / `orch_walk` but sharing no code with the loaders (FR-013 non-circularity).

---

## D-9 — Keeping the pin non-circular and provably red

**Decision**: expected delimiters come from the independent oracle walk (D-8). A documented **sample** is cross-checked against a third authority: codegen's `walk_level` / `GroupOrderEntry` (`tools/codegen/fixpp-codegen/ir.cpp`), or fixpp#208's tabulated Orchestra values. The pin is demonstrated failing before the fix and the failure counts **reconciled against spec.md's 335 / 52 / 30 / 232**, not merely recorded (FR-014 + SC-015 — see "Measurement provenance" below for why recording alone is insufficient).

**Rationale**: two independent guards against the two ways this pin could be worthless. If the oracle mirrored the fixed loader's logic it would pass by construction and prove nothing — hence the third authority. If it were never observed red it would prove nothing either — hence Phase 1 running fully RED first, with counts recorded.

**Explicitly not treated as coverage** (FR-016): the 78 passing collision-membership cases. *(Provenance note, Gate A round 1: fixpp#210 quotes 69 for the same suite. The cases are derived at **runtime**, per dictionary, by `derive_cases_for_dict` in `tests/dictionary/collision_membership_guards_test.cpp:91`, so the count is a property of the dictionaries and of the sibling features that have since widened the set — 082 plausibly among them — not a fixed literal. Nothing in this feature depends on which number is right, because FR-016 forbids citing the suite as delimiter coverage at **any** cardinality. The discrepancy is recorded rather than reconciled, so a later reader does not mistake it for a measurement error.)* Their discriminator comes from `first_tag_only_in`, derived independently of the delimiter, so their green says nothing about delimiter correctness. A sibling feature had to add an `exclude` parameter to that helper specifically so the injected delimiter would not be chosen as a discriminator — direct evidence that these cases route *around* this defect rather than over it.

---

## D-10 — The global lookup must be REPOPULATED, not dropped

**Decision**: the bare `group_first_field(no_tag)` global remains populated, **derived from the per-context table** (first-seen projection) rather than from the deleted one-level scan. It is not removed.

**Rationale**: the global is not only a delimiter source — it is used as an *is-this-tag-a-group* **predicate** at several sites, some of them reachable through the GA-frozen C ABI:

| Site | Use |
|---|---|
| `src/capi/message_write.cpp:157` | `group_first_field(tag) != 0` → "is this a group" |
| `src/capi/message_write.cpp:812`, `:923` | same predicate, gating `FIXPP_ERR_TYPE_MISMATCH` |
| `src/dictionary/dictionary.cpp:402-405` | legacy bare store population `continue`s when the value is 0 |
| `include/fixpp/wire/validator.hpp:458-459` | the bare pure-virtual override |
| `include/fixpp/dict/table_view.hpp:364`, `:377` | the context-miss fallback that hand-built test fixtures depend on |
| `src/dictionary/xml_loader.cpp:1016-1017` + `src/dictionary/orchestra_loader.cpp:895-896` | **the one that THROWS** — `finalize()`'s fail-closed rejection of a nested delimiter equal to its immediate parent's (072, issue #180) |

**The sixth consumer was missing from the original census and it is the only *write-order-sensitive* one (added 2026-07-30, Gate A round 1).** Every other row is a read site; this one rejects the load. It reads `g.first_field_tag`, written at `src/dictionary/xml_loader.cpp:644` from the scan at `:610-641` — the scan this feature deletes — and at `src/dictionary/orchestra_loader.cpp:629` from `first_member_tag`, which D-1 replaces. Two legs, of which only the second is undetectable:

- **Caught.** "The guard starts throwing on a shipped dictionary" is caught by the Phase-3 gate ("all ten still load under the fail-closed default"). Conceded; not the finding.
- **Undetectable — the census can go vacuous without failing.** `tests/dictionary/reused_tag_census_test.cpp::NestedGroupDelimiterCensus` corroborates the guard by deriving its own delimiters from a raw per-`<group>` walk with parent-delimiter threading. The guard it corroborates will now derive its delimiters from a **first-seen projection of a per-context table**. If the two stop meaning the same thing, the census still passes and stops pinning — the "non-circular census built from an independent walk stops pinning the shipped artifact" failure mode, on a gate whose entire value is that it is non-vacuous. Worse, `spec/behaviors-and-limitations.md` L-063-4's *"0 nested/parent delimiter collisions across all 6 group-bearing vendored dicts"* audit was performed against the **pre-fix, wrong** delimiters, and this feature changes 335 of them; that audit is the sole basis for L-063-4's "real-dict-unreachable" disposition and transitively for FR-021's low priority, and **it does not survive this feature**.

**Therefore (FR-012a / SC-014):** the projection's write must be ordered explicitly relative to message expansion (`xml_loader.cpp:927`) and the guard (`:1016`) — the guard must read post-projection values, never a half-populated global — and re-censusing nested/parent delimiter collisions under the **post-fix** delimiters across all ten dictionaries, plus re-deriving L-063-4's reachability disposition, is a **Phase-3 precondition alongside C-7.1**, not a close-out chore.

If the scan is deleted and nothing repopulates the global, every one of those reads 0 and the C ABI's `group_begin` rejects **all** groups — a total regression reachable through a frozen ABI, introduced by a change whose stated purpose is to fix rejections.

**Second-order benefit**: because the projection derives from the recursive per-context capture, the three groups that currently project 0 start projecting a real delimiter. So the 502→505 correction reaches the C ABI predicate too, not only the validator — those groups become constructible through `group_begin` for the first time.

**Alternatives considered**: keeping the old scan alive purely to feed the global — rejected, it preserves the drifted second traversal that D-1 exists to eliminate, and would leave the global disagreeing with the per-context table for exactly the eight tags at issue.

---

## D-11 — The consumer is specified, not only the producer *(NEW — Gate A round 1, 2026-07-30)*

**Decision**: the per-context delimiter table's **completeness with respect to `as_table_view()` is a load-time invariant enforced in `finalize()`**, not a runtime condition handled at lookup. A consumer-side miss is thereby unreachable by construction, and no silent fallback exists to hide one.

**Why this decision was missing.** D-1/D-2/D-3 specify capture, threading and storage with source verification in both loaders. The only consumer — `Dictionary::as_table_view()` — got one line of Project Structure and one contract clause (C-3.1: it "sources the delimiter … from this table"). C-3.1 never said what happens when the table has **no record** for the key being registered. `data-model.md` forbids *storing* `0` and C-1.4 forbids *recording* `0`; both are producer-side rules. Post-083 the resolution chain that `src/dictionary/dictionary.cpp:510-511` currently terminates cleanly *(citation normalised 2026-07-31, Gate A round 3, N24 — `:508-509` are the comment lines above it, and three other loci in the bundle already said `:510`)* —

```cpp
std::uint16_t const gr_delim = group_first_field(no_tag);
std::uint16_t const delim = gr_delim != 0 ? gr_delim : members.front();
```

— becomes `Entity 2 → ?`. Everything downstream (FR-001's per-path promise, FR-010's exactness, D-5's "no-op by construction") rests on a consumer nobody specified.

**Why neither silent fallback is available**, which is what forces a defined disposition rather than a default:

1. `group_first_field(no_tag)` — the dictionary-global first-seen value. This is **the defect this feature exists to remove**, silently reinstated for exactly the contexts the new table failed to cover, and invisible to FR-012's pin if that pin measures the table rather than the registered store.
2. `members.front()` — the lowest-tag member of a tag-sorted set. `dictionary.cpp:498-506` records in situ that this is a *worse* bug already fixed once (FIX44 `NoPartyIDs(453)`: lowest member 447, real delimiter 448) and pinned by `tests/wire/validator_production_table_view_test.cpp::ValidatorProductionTableView.GroupDelimiterFromWireNotTagSortedMember`. Reintroducing it as a fallback regresses a pinned fix.

**Why `finalize()` and not `as_table_view()`.** A hard check at `as_table_view()` would be a **new fatal path on a function 072 established as non-throwing** — "Enforced in `LoaderState::finalize()` before any `table_view` is built; `as_table_view()` stays non-throwing" (`spec/behaviors-and-limitations.md` L-063-4). Moving the check to load keeps that disposition intact, mirrors an existing fail-closed site rather than inventing one (the same argument D-7 makes), reuses FR-006's disposition and diagnostic rather than adding a second error taxonomy, and gives FR-006a's tolerant mode a clean answer: a tolerantly-skipped group never enters the consumer's enumeration, so the invariant holds unchanged in both dispositions (FR-023a). It also means the new branch is covered by FR-006b's existing witnesses rather than landing as an uncovered error path (`[const §IX.1]`).

**Alternatives considered**: (a) check at `as_table_view()` and let it throw — rejected, it silently reverses a disposition 072 established and pinned; (b) log-and-continue on a miss — rejected, it is silent fallback (1) with a log line, and the log is not on any gate; (c) leave undefined — rejected, that is the finding.

**What makes the invariant satisfiable, and why it still gets a precondition (FR-023b / C-7.3).** `as_table_view()` does not consume a recorded path — it **reconstructs** one, walking upward through `immediate_parent` (`src/dictionary/dictionary.cpp:489-496`, from the map built at `:439-444` over the tag-deduped `all_fields`). Each hop therefore uses whichever occurrence of that ancestor tag survived dedup, so a suppressed occurrence could yield a **hybrid** chain that no single occurrence ever had — a key the loader never recorded, i.e. a violation on a *shipped* dictionary at load. That is discharged by the same measurement that retires Edge Case #1: with zero multi-path pairs in all ten dictionaries there is no suppressed occurrence, every hop's survivor is that tag's only occurrence, and every reconstructed key equals a recorded key. **But the invariant then holds because of a measurement, not by pure construction**, and this feature is adding a *third* new fail-closed load path — so it gets a shipped-set precondition of the same shape as C-7.1 and C-7.2, not an assumption.

**Corroborating evidence that this is not theoretical**: D-12, below. There is a per-context entry in FIX42 that the documented population path cannot produce. Whichever branch explains it, a population path exists that D-1's account does not cover — which is precisely the condition that manufactures a miss.

**Landed in**: `contracts/group_ctx_delims.md` `## Consumer contract` C-3.4, **C-3.4a** (Gate A round 2 — how the checked set is computed at `finalize()`, including the `!members.empty()` exclusion at `src/dictionary/dictionary.cpp:463`, with set-equality conditional on D-12 branch (b)), C-3.5..C-3.7 and C-7.3; spec FR-023 / FR-023a / FR-023b; `data-model.md` Entity 2.

---

## D-12 — OPEN: how does an `INT`-typed FIX42 tag acquire a per-context entry? *(NEW — Gate A round 1, 2026-07-30 — unresolved)*

**Status: OPEN. This is not a settled question and must not be read as one.** It does not hold Gate A — the numbers it touches are verified independently — but resolving it is a **Phase 1 task, sequenced ahead of the loader change**, because one of its two branches would mean D-1's capture does not cover every context that exists today.

**What is settled.** A Gate A round-1 challenge argued that the FIX42 baseline row (38 contexts / 8 wrong / 4 polluted / 0 unregistered) could not have been measured on `0539b56d`, because per L-063-1 `as_table_view()` registers zero groups for the `INT`-typed FIX40/41/42. The orchestrator measured it. **The challenge is refuted and the row stands**, pointer- and size-level:

```
=== FIX42.xml ===
  no_tag=146  ctx(R,{},146): size=32  data=0x559a425c2050
              bare(146):     size=19  data=0x559a425e1fb0   -> REAL CTX ENTRY
  no_tag=382  ctx(R,{},382): size=4   data=0x559a425e29f0
              bare(382):     size=4   data=0x559a425e29f0   -> BARE-FALLBACK
```

Different storage and different sizes for 146; tag 382 in the same dictionary and the same call shape resolves to a genuine bare fallback with identical pointer and size, so the discriminator is working. Whole-dictionary: FIX42 yields **CTX-HIT=38, BARE-FALLBACK=0, EMPTY=0**, with 18 distinct registering `no_tag`s (33 73 78 124 136 146 199 215 267 268 295 296 382 384 386 398 420 428). The 335 and 52 totals do not move, and FR-012's "no carve-out" needs no position on the INT-typed dictionaries.

**What is not settled — the mechanism.** The premise of the challenge is *true*: `dictionaries/FIX42.xml:2084` types tag 146 `INT` (versus `dictionaries/FIX44.xml:4274`, which types the same tag `NUMINGROUP`), and `Dictionary::field_ref("R", 146)` on FIX42 returns `Int`, not `NumInGroup` — verified for messages `R`, `V`, `c`, `d`, `B`. Yet `dictionary.cpp`'s context-store loop gates on `fr.type == field_data_type::NumInGroup` (`src/dictionary/dictionary.cpp:398` — the legacy bare-store loop — and `:446` — the context-store loop; citation corrected 2026-07-31, Gate A round 3, N24: the first gate is at `:398`, not `:407`), and `src/dictionary/xml_loader.cpp:70` is the **only** site assigning `NumInGroup`, from the XML token `NUMINGROUP`. An `INT`-typed 146 should not pass that gate. It demonstrably does.

**Two branches, both consequential, neither established:**

- **(a) An unaccounted population path** reaches `set_group_first_ctx` / `add_group_member_ctx` that D-1/D-3's description does not cover. If so, **this bundle's account of where contexts come from is incomplete and D-1's loader-side capture may not cover every context that exists today** — the delimiter for such a context would come from somewhere D-1 never writes, and the FR-023 completeness invariant would fail at load on a shipped dictionary. This is the branch that makes it a Phase 1 blocker for Phase 3.
- **(b) The type is promoted** somewhere between `<fields>` parsing and the context loop. If so, **L-063-1's "FIX40/41/42 are group-blind" shorthand is narrower than stated** — which bears directly on **#196 / 082**, whose whole premise is relaxing that gate, and on SC-008's claim that FIX40/FIX41/FIXT11 are the unaffected set.

**Task, re-posed 2026-07-31 (Gate A round 2, N18).** The round-1 wording — *"instrument the two `set_group_*_ctx` call paths and record which one produces `(R,{},146)`"* — **cannot resolve D-12**, and a Phase-1 gate whose task cannot answer its own question collapses the two nets protecting D-1's account to one. There is exactly **one** production call site for each (`src/dictionary/dictionary.cpp:518` for `set_group_first_ctx`, `:520` for `add_group_member_ctx`, plus the internal call `set_group_first_ctx` itself makes at `include/fixpp/dict/table_view.hpp:645` — corrected from `:646` at Gate A round 3, see `group_ctx_delims.md` C-3.3), and **both sit in the same loop body behind the same gate** — `if (fr.type != field_data_type::NumInGroup) { continue; }` at `:446`. Whichever fired, it fired through the gate the puzzle says cannot pass, so "which call path" has no discriminating answer.

**Task (Phase 1, before the Phase-3 loader change) — instrument the PREDICATE, not the call site.** On a FIX42 load, for `tag == 146` under message `R`:

1. log `fr.type` **as the `:446` loop over `message_fields("R")` sees it** — the value the gate actually tests;
2. compare it against `Dictionary::field_ref("R", 146).type` — the accessor the orchestrator's round-1 measurement used, and which `include/fixpp/dict/dictionary.hpp:95` documents as returning a **composed** `FieldRef` rather than a raw run entry;
3. and log the same pair for a control tag that behaves as expected in the same dictionary (e.g. 382, measured as a genuine bare fallback), so a null result is distinguishable from broken instrumentation.

**Both outcomes terminate the task:**

- **The two disagree** (loop sees `NumInGroup`, `field_ref` reports `Int`) ⇒ **branch (b)**, with a precise mechanism: the type is promoted or composed somewhere between `<fields>` parsing and the context loop. No loader-side risk to D-1; amend L-063-1 and this feature's #196/082 handoff note.
- **They agree** (both `Int`) ⇒ a context entry exists that the documented loop **provably cannot have written** ⇒ **branch (a)**: a population path exists that D-1's capture account does not cover. Amend D-1/D-3 before Phase 3, and re-derive `contracts/group_ctx_delims.md` C-3.4a's checked set against the newly-found path — its set-equality leg is explicitly conditional on branch (b).

Either outcome is recorded; neither is assumed.

### T013 — the predicate instrumented directly on the current tree: branch (c), not (a) or (b) *(2026-07-31, Phase 1)*

**The task's own two-branch framing does not fit the data. A third branch, foreshadowed by T008's Escalation 1, is what the instrumentation shows: no `(R,{},146)` context entry exists at all, on either store the round-1 log named.**

**Provenance check first, because it bears on how the disagreement below is read.** `git log --oneline -1 -- src/dictionary/dictionary.cpp src/dictionary/xml_loader.cpp` returns `710e658e` (081-strict-validation-residuals) for both files, and `git merge-base --is-ancestor 710e658e HEAD` holds on `083-group-delimiter-resolution`. Neither file has changed since before round-1's measurement (2026-07-30) through this task (2026-07-31); 082's structural-group-detection commits (`036438a9`, `780f327f`) are confirmed **not** ancestors of this branch's `HEAD` (`git merge-base --is-ancestor 036438a9 HEAD` fails). The code under measurement is byte-identical to what round-1 measured. A code change cannot explain the disagreement below.

**Leg 1 — the predicate as the `dictionary.cpp:445` loop over `message_fields("R")` sees it.** Iterating `Dictionary::message_fields("R")` on a fresh `XmlLoader::load(FIX42.xml)` and matching `fr.tag == 146`: **PRESENT** (the group's own count-tag *is* emitted into this message's field run — declared at `dictionaries/FIX42.xml:843`, `<group name='NoRelatedSym' required='Y'>` inside `<message name='QuoteRequest' msgtype='R'>`), `fr.type = 0 (Int)`, `fr.group_no_tag = 0`, `fr.rule = Required`. Root cause verified at the type source: `dictionaries/FIX42.xml:2084` has exactly one `<field number='146' name='NoRelatedSym' type='INT' />` declaration in the whole file, and `xml_loader.cpp:592`'s `no_fr.type = nit->second.type` assigns the group's own count-tag `FieldRef` its type from that **one global per-tag declaration** — there is no promotion step between `<fields>` parsing and the emission into `out` (`message_fields`'s backing store). The loop's gate (`fr.type != field_data_type::NumInGroup`, i.e. `!= 3`) therefore correctly `continue`s past tag 146 in FIX42; it is not a bug in the gate, it is FIX42 declaring the tag `INT`.

**Leg 2 — the composed accessor.** `Dictionary::field_ref("R", 146)` on the same load: **PRESENT**, `type = 0 (Int)`, `rule = Required` — **identical** to Leg 1, not merely consistent with it. This is not a coincidence of this one tag: `field_ref_impl` (`dictionary.cpp:67-81`) binary-searches `tag` directly into the **same** `fields_` backing array, over the **same** per-message run (`find_msg_fields`), that `message_fields_impl` (`dictionary.cpp:236-243`) returns as a span. The two accessors read the identical stored `FieldRef` struct from the identical storage; `field_ref` performs a lookup, not a composition that could diverge in `.type`. `include/fixpp/dict/dictionary.hpp:95`'s "composed FieldRef" doc comment describes what a `FieldRef` conceptually represents (a field resolved in a MsgType context), not a runtime step capable of promoting a stored type — there is no code path between `<fields>` parsing (`xml_loader.cpp`'s `by_name_`/`GlobalFieldInfo` table, populated once per tag) and either accessor that could make them disagree. **Leg 1 and Leg 2 agreeing is therefore not evidence for branch (b)** — branch (b) requires a *mechanism* that promotes the type between the two observation points, and source-tracing both accessors to their shared backing store shows no such mechanism exists to find.

**Leg 3 — the control, tag 382, three variants (to bound the control against broken instrumentation).**
- `(msg="R", tag=382)` — same message as the subject: **ABSENT** from `message_fields("R")` entirely (`dictionaries/FIX42.xml:841-874`'s `QuoteRequest` body never declares `NoContraBrokers`); `field_ref("R", 382).rule == NotDeclared`. Both accessors agree on absence, via two different code paths (raw span iteration vs. binary search) — instrumentation is not merely returning zeroed garbage, it discriminates absence correctly.
- `(msg="8", tag=382)` — the message that *does* declare `<group name='NoContraBrokers'>` (`dictionaries/FIX42.xml:148`, inside `ExecutionReport`): **PRESENT**, `type = 0 (Int)`, `rule = Optional`. `dictionaries/FIX42.xml:2612`'s single `<field number='382' ... type='INT' />` declaration is dictionary-global, exactly as tag 146's is — so 382 is `Int`-typed in FIX42 in the one message that actually declares it as a group, too. **This directly falsifies round-1's own characterisation of 382 as "a genuine bare fallback" with `size=4, data=0x...` (non-null, non-zero) on the current tree**: measured directly (see three-store probe below), `(8,{},382)` is **UNREGISTERED** on both store #2 and store #3 — not a bare-fallback hit, an empty miss.
- **Discriminator sanity check (not part of the FIX42 measurement, a control on the control):** the same `.data()`-pointer discriminator applied to `FIX44 (D,{},453)` — `NoPartyIDs`, genuinely `NUMINGROUP`-typed at `dictionaries/FIX44.xml:5221` — reports **REGISTERED**, with distinct non-null pointers (`store#2 data=0x...870 size=6`, `store#3 data=0x...8f0 size=4`). The mechanism correctly reports "registered" when a registered context genuinely exists; the FIX42/382 and FIX42/146 "unregistered" results are not an artifact of a broken always-false discriminator.

**Direct three-store probe** (per T008's taxonomy: #1 = `Dictionary::group_first_field`, the loader-level global; #2 = `table_view`'s own legacy bare store; #3 = the context store):

```
(R,{},146):  store#1=46 (non-zero)   store#2 delim=0 members{data=nullptr,size=0}   store#3 delim=0 members{data=nullptr,size=0}   => UNREGISTERED
(R,{},382):  store#1=375 (non-zero)  store#2 delim=0 members{data=nullptr,size=0}   store#3 delim=0 members{data=nullptr,size=0}   => UNREGISTERED
(8,{},382):  store#1=375 (non-zero)  store#2 delim=0 members{data=nullptr,size=0}   store#3 delim=0 members{data=nullptr,size=0}   => UNREGISTERED
```

Store #1 is non-zero for both tags (the loader's structural `<group>`-XML detection runs independently of the type gate — this is D-10's documented behaviour). Stores #2 and #3 — the two the round-1 log actually compared — are **both empty for every case tried in FIX42**, subject's tag and control tag alike, in every message either is declared in.

**Does a `(R,{},146)` context entry exist? Plainly: no.** Neither store #2 nor store #3 holds a record for it. There is nothing for an unaccounted population path (branch (a)) to have written, and no promotion mechanism exists for branch (b) to name (leg 1 and leg 2 agree because they read the same storage, not because a promotion occurred). **Branch (c) — the entry does not exist — is what the data shows.**

**Cross-validation against the checked-in pin (not merely against this scratch probe).** `dictionary_delimiter_census_test --gtest_filter='DelimiterCensus.RedCountsReconcileWithSpecBaseline'`, run live on this tree: `FIX42: 38 unregistered context(s)` (matching T008 Escalation 1's "FIX42 unregistered=38 of 38" exactly), and its capped detail listing explicitly includes `[unregistered] FIX42 msg=8 no_tag=382` — independently confirming this task's `(8,{},382)` result via a wholly separate code path (the pin's own oracle-driven enumeration and `.data()` discrimination, not this task's probe). `(R,{},146)` is not itself named in the capped detail output (`kMaxDetailLinesPerDict` truncates it — `delimiter_census_test.cpp:131-168`), but the uncapped **total** of 38-of-38 unregistered leaves no room for it to be an exception: every FIX42 context the pin's oracle enumerates is unregistered on this store, `(R,{},146)` included.

**Reconciliation against round-1's record — disagreement stated, not silently overwritten.** Round-1 (`research.md:271-277`, this file) logged `ctx(R,{},146): size=32 data=0x...` (distinct from the bare span, i.e. a genuine hit) and `bare(382): size=4 data=0x...` matching `ctx(382)`'s pointer (a genuine bare-fallback hit), concluding "FIX42 yields CTX-HIT=38, BARE-FALLBACK=0, EMPTY=0". This task's measurement — built against the identical, unchanged production code, the identical `FIX42.xml` (confirmed the only file of that name in `dictionaries/`), the identical `as_table_view()` — finds the opposite for both data points: `(R,{},146)` unregistered, not a hit; `(8,{},382)`/`(R,{},382)` unregistered, not a bare fallback. **I believe this task's measurement, not round-1's**, for three reasons: (1) the code has not changed between the two measurements (git provenance above), so both runs should observe the same production behaviour if run the same way; (2) this task's discriminator is independently proven non-vacuous on FIX44/453 (round-1's log offers no equivalent proof its own discriminator was live rather than reading uninitialized/stale memory); (3) this task's result is independently cross-validated against the already-committed, separately-authored `dictionary_delimiter_census_test` pin, which agrees exactly (38-of-38, `msg=8 no_tag=382` named explicitly) — round-1's result has no such second corroborating witness in this file.

**A partial, non-conclusive reconciliation attempt, offered because the brief asked whether round-1's construction is reproducible under any variation — it is not exactly, but a near-miss exists and is worth recording.** Round-1's "CTX-HIT" framing for tag 146 under message "R" *is* reproducible, almost — **in a different dictionary**: `dictionaries/FIX44.xml:4274` types tag 146 `NUMINGROUP` (not `INT`), and FIX44 also has a `msgtype='R'` `QuoteRequest` (`dictionaries/FIX44.xml:676`) that declares `NoRelatedSym`. Measured directly: `FIX44 (R,{},146)` **is** genuinely REGISTERED (`store#2 size=106`, `store#3 size=100`, distinct pointers) with `delim=55`. This confirms tag 146 under message R *can* be a real, non-trivial registered NumInGroup context — just not in FIX42, and not at member-count 32 (FIX44's is 100). I could not find a `(msg_type, {}, 146)` combination in either FIX42 or FIX44 that produces exactly `size=32`, so I cannot assert round-1 loaded the wrong file and mislabeled it — that specific hypothesis is **not confirmed**, only shown plausible in direction (a NUMINGROUP-typed sibling dictionary sharing the same tag and message type exists and is easy to conflate with FIX42 by hand). I did not chase this further, per this task's scope (measure and report the current-tree predicate; the exact provenance of round-1's number is a different question than D-12's own).

**Disposition: D-12 is not resolved into branch (a) or (b). Recorded as branch (c) — the population account (D-1/D-3) is not shown incomplete, because there is no orphan entry for it to explain.** Consequences for downstream text that assumed (a) or (b) was the outcome:

- `contracts/group_ctx_delims.md` C-3.4a's set-equality leg, stated conditional on branch (b) resolving — branch (b) did not resolve; that condition is now **false**, and the leg's conditional framing needs the orchestrator's re-derivation (T013 does not amend contracts/, per scope).
- T008's Escalation 1 hypothesized "the discrepancy is most likely explained by L-063-1/#196's INT-typed-`NumInGroup` group-blindness... reaching stores #2/#3 in a way store #1... did not exercise" — this task confirms that hypothesis at source (leg 1/leg 2's shared-storage trace) rather than leaving it as an untested guess: there is no separate "reaching" mechanism, stores #2/#3 simply never receive an entry for an `Int`-typed count tag, by the same single gate, everywhere, in every message, in FIX42.
- Round-1's "The challenge is refuted and the row stands" conclusion (`research.md:267`) does not stand as measured on this tree; this section supersedes it for the `(R,{},146)`/`(8,{},382)` data points specifically. The row's aggregate totals ("335 and 52... do not move") are a separate claim this task was not scoped to re-derive and does not touch.

**Reproduction.** `t013_probe.cpp` (not checked in): loads `FIX42.xml` and `FIX44.xml` via `XmlLoader::load` into separate 32 MiB `pmr::monotonic_buffer_resource`s, prints Leg 1 (raw `message_fields(msg_type)` scan for the target tag), Leg 2 (`field_ref(msg_type, tag)`), and a three-store probe (`Dictionary::group_first_field`, `table_view::group_first_field/group_member_tags(no_tag)`, `table_view::group_first_field/group_member_tags(msg_type, {}, no_tag)`) for `(R,146)`, `(R,382)`, `(8,382)` in FIX42, plus the FIX44 `(D,453)` sanity check and `(R,146)` reconciliation probe. Compile/link: `clang++ -std=c++23 -stdlib=libstdc++ -I include -DFIXPP_DICT_DATA_DIR="\"$(pwd)/dictionaries\"" t013_probe.cpp -o t013_probe -Wl,--start-group build/linux-clang-debug/lib/libfixpp_dictionary.a build/linux-clang-debug/lib/libfixpp_core.a /home/catalin/.conan2/p/b/pugix5e92502c960d6/p/lib/libpugixml.a -Wl,--end-group` (pugixml path read from `build/linux-clang-debug/pugixml-debug-x86_64-data.cmake`'s `pugixml_PACKAGE_FOLDER_DEBUG`; may differ per Conan cache layout). Cross-check: `build/linux-clang-debug/bin/dictionary_delimiter_census_test --gtest_filter='DelimiterCensus.RedCountsReconcileWithSpecBaseline'`, grep for `FIX42:` and `no_tag=382`.

**Files touched for T013**: this section only. Scratch program not checked in.

---

## D-13 — The C-ABI construction path reaches Entity 2 via a session-cached `table_view` *(NEW — Gate A round 2, 2026-07-31)*

**Decision**: `validate_group_grammar` resolves the per-context delimiter through a `table_view` **built once per session at `fixpp_session_open`** and reached by a non-owning pointer on the message handle — **not** by calling `Dictionary::as_table_view()` at the check.

**Why a decision was needed at all.** FR-018 was specified as "thread the context through the existing recursion", and `contracts/capi_group_grammar.md` C-9.2 said how the *context* gets there — but not how the *store* does. The check holds a `const fixpp::dict::Dictionary*` (`src/capi/message_write.cpp:710-711`), whose only `group_first_field` is the context-free overload (`include/fixpp/dict/dictionary.hpp:109-111`); the context-keyed one is a `table_view` member (`include/fixpp/dict/table_view.hpp:349-365`). Leaving the bridge unnamed would hand `/speckit-tasks` a choice it cannot make safely.

**Why the obvious route is refused.** `Dictionary::as_table_view()` is public (`include/fixpp/dict/dictionary.hpp:212`), so `dict->as_table_view().group_first_field(mt, path, e.tag)` compiles — and is a **constitution violation**, not merely a slow path. Both the declaration (`dictionary.hpp:210-212`) and the definition (`src/dictionary/dictionary.cpp:357-358`) carry `[const §XV.1]`: construction only at config time, never rebuilt on the per-message hot path. `validate_group_grammar` runs inside `fixpp_msg_commit` (`src/capi/message_write.cpp:755`). It would also rebuild the entire view per message *and per field run*.

**Why the chosen route costs nothing structural.** The pattern already exists twice in-tree: `fixpp::session::Session` caches an inbound `table_view` at config time (`src/session/session.cpp:992`), and `fixpp_msg` already owns one for the clone path (`src/capi/capi_internal.hpp:285`). `fixpp_session` already caches `dict_` at open time (`src/capi/session.cpp:109-111`, into the member declared at `src/capi/capi_internal.hpp:493`) and `fixpp_msg_create_outbound` already copies it into the handle (`src/capi/message_write.cpp:289-291`, into the member declared and documented at `capi_internal.hpp:261-266`); the view rides the same propagation. *(Citations sharpened 2026-07-31, Gate A round 3, Codex #1 — the round-2 text cited the destination member's declaration where it meant the two copy statements.)* Both structs are internal to `src/capi/capi_internal.hpp`, so no exported symbol and no client-visible layout moves — C-9.3 holds byte-for-byte. Cost: one `as_table_view()` per opened session, zero per message.

**Why it needs its own witness.** The banned route and the chosen route are **behaviourally identical** — W-11 and W-12 pass under both. W-11a (`CapiGroupDelimiterCtx.CommitDoesNotRebuildTableViewPerMessage`) is therefore the only artifact that can tell them apart, and without it `[const §XV.1]` compliance would rest on inspection.

**Null disposition, stated so it cannot collide with C-9.4.** `fixpp_msg::dict_` is `nullptr` for inbound dispatch-window handles; the cached view is absent on exactly the same handles from exactly the same source, so "no dictionary" and "no view" are one state and C-9.4's dict-free behaviour is unchanged (W-11b). A dictionary-present handle that cannot reach the view **fails closed** rather than falling back to the bare global — that fallback is `group_ctx_delims.md` C-3.4's first refused candidate and would reinstate the defect FR-001 removes.

**Landed in**: spec **FR-018b**; `contracts/capi_group_grammar.md` **C-9.2a**, W-11/W-11a/W-11b/W-12; `data-model.md` Entity 7; `plan.md` Source Code tree + Article XV row + FR-022 path (3).

---

## Measurement provenance

Baseline figures in spec.md come from `delim_probe3.cpp`, run against `main` @ `0539b56d`, built against `build/linux-clang-debug/lib/libfixpp_dictionary.a`, covering all ten dictionaries. Two probe-fidelity properties matter for reproducing it:

- **Context-miss must be discriminated from wrong-answer.** `group_member_tags(mt, path, no_tag)` falls back to the bare global store on a miss (`table_view.hpp:373-376`), so an unregistered context returns the global set and reads as large-scale pollution. Discriminated exactly by span `.data()` pointer identity against the bare span. Skipping this inflated fixpp#210's headline count by 10.
- **The root-cause split is corroborated, not proven.** Attribution uses "does the runtime delimiter match *some* context's true delimiter". A broken-scan value coincidentally equal to another context's true delimiter would be misfiled. The split reproduces fixpp#208's independently-derived five tags exactly, which is strong corroboration.

### The probe is not checked in — so the pin, not the probe, is the authority of record *(added 2026-07-30, Gate A round 1)*

`delim_probe3.cpp` is a scratch file and is **not in the working tree**. Every success criterion is an exact delta from its output (335 / 52 / 30 / 232), and neither Gate A, Gate B, CI, `/speckit-verify`, nor a future maintainer auditing SC-001 can re-run it. That is tolerable only because Phase 1 rebuilds the measurement inside a checked-in pin — but rebuilding it is not the same as agreeing with it, and nothing previously required the two to agree.

**Reconciliation is now an explicit Phase-1 exit criterion (SC-015)**, replacing "the measured failure counts recorded":

- the pin's observed RED counts are compared against **335 / 52 / 30 / 232**;
- any delta is explained *here*, in this section, and spec.md is amended, **before Phase 2 begins**;
- a delta is not automatically a spec error — it may falsify the probe, the pin, or the spec, and the explanation must say which.

**Authority of record**: from the moment `tests/dictionary/delimiter_census_test.cpp` first runs RED, **that pin is the authority for every figure in spec.md's Baseline table**, and `delim_probe3.cpp` is history. Without this line a later reader chasing SC-001 is chasing a deleted scratch file.

The two probe-fidelity properties above are requirements **on the pin**, not just on the probe: the pin must discriminate context-miss from wrong-answer by span `.data()` identity, and must score the 30 unregistered contexts rather than `continue` past them.

### T007 — third-authority corroboration of a documented sample (FR-013)

**Authority used, and why it is independent.** `tools/codegen/fixpp-codegen/ir.cpp`'s `build_ir()` — the **real** codegen walker, not a re-implementation. It root-sniffs the XML (`ir.cpp:564-577`: `<fix>` → `XmlLoader` dispatch, `fixr:repository` → `OrchestraLoader` dispatch) and then re-parses the same file a **second time** with a codegen-tool-local pugixml walk: `walk_level` (`:61-102`) for the `<fix>` schema, `walk_orchestra_level` (`:335-416`) for the `fixr:` schema — both populate `MessageIR::group_order`, a `std::vector<GroupOrderEntry>` per message carrying `parent_path`/`no_tag`/`delimiter_tag` at every nesting depth. It is independent of **both** the D-8 oracle (a separate pugixml walk sharing no code — `tests/dictionary/required_scope_oracle.hpp`'s `qfix_walk`/`orch_walk`) and the loader under fix: it consults `fixpp::dict::Dictionary` only for `field_by_name`/`field_ref` (name→tag and tag→type resolution), never for group structure or delimiters — `entry.delimiter_tag = entry.members.front().tag` (`ir.cpp:96`, `:410`) comes entirely from its own declaration-order walk of the XML tree.

**Mechanism.** A scratch program (not checked in; build+run commands below for reproduction) that, per dictionary: (1) calls `build_quickfix_oracle`/`build_orchestra_oracle` (T005's oracle, unmodified, called not forked) to get `oracle.group_delims`; (2) calls the real `fixpp::codegen::build_ir()` on the same XML file and flattens every message's `group_order` into a map keyed identically, `(msg_type, parent_path, no_tag) → delimiter_tag`; (3) separately loads the dictionary via `XmlLoader`/`OrchestraLoader` (mirroring `delimiter_census_test.cpp`) purely to read today's **bare global** `table_view::group_first_field(no_tag)`, used only for sample selection (see below), never as part of the oracle-vs-codegen comparison itself.

Reproduction: compile `ir.cpp` and the scratch `.cpp` with clang++ (`-std=c++23 -stdlib=libstdc++ -I<repo>/include -I<repo>/tools/codegen/fixpp-codegen -isystem <pugixml include>`), link both objects against `build/linux-clang-debug/lib/{libfixpp_dictionary,libfixpp_wire,libfixpp_dict_dispatch_bridge,libfixpp_core}.a` and pugixml's static lib inside a `--start-group`/`--end-group` pair (same libraries `dictionary_required_scope_census_test` already links, see `tests/dictionary/CMakeLists.txt:186-199`).

**Sample selection rule** (stated so a reader can reconstruct it): for each of five dictionaries — FIX42, FIX44, FIX50SP2, FIXT11 (all `<fix>`-schema, QuickFIX-XML) and Orchestra FIX Latest (`fixr:`-schema) — walk `oracle.group_delims` in its natural sorted key order `(msg_type, parent_path, no_tag)` and take, in priority order (no double-counting): every context named in requirement 4 (FIX50SP2 count tags 1499/1669/1919); then up to 4 **divergent** contexts (oracle delimiter ≠ that dictionary's bare/global `group_first_field(no_tag)`); then up to 2 **nested** contexts (non-empty `parent_path`) regardless of divergence; then 1 non-divergent **control** context. This yielded 36 rows (5 dictionaries; both loader families; several dictionaries each) — full table below.

**Two flavours of "divergent" turned out to matter, and are labelled separately, not averaged together.** *Divergent-by-conflict* — bare global holds a **different real** delimiter (e.g. `FIX44 AX {} 124`: bare=32, oracle=17; `FIX50SP2 6 {} 40204`: bare=40205, oracle=40209) — is the shape FR-001 is about, and 12 of the 36 rows are this shape (FIX44 ×4 msg types sharing one no_tag/value pair, FIX50SP2 ×4, Orchestra ×4). *Divergent-by-absence* — bare global is 0 dictionary-wide (FIX42's rows, an instance of L-063-1's `INT`-typed group-blindness — not delimiter divergence in FR-001's sense) or per-context (the three FIX50SP2 named groups, the "30 newly-registering" set) — is a different fact and is marked separately in the table's `bare_global` column rather than folded into one undifferentiated "divergent" bucket.

**The nested requirement is not vacuously satisfied.** 10 of the 36 rows are simultaneously nested (`parent_path` non-empty) **and** divergent — 2 divergent-by-conflict (`FIX50SP2 6 {555} 41599`, `FIX50SP2 6 {711} 42060`) and 8 divergent-by-absence (`FIX42 E {73} 78`/`386`; the six named FIX50SP2 rows under `{1677}`/`{146}`) — so a path-handling off-by-one on either side (oracle or codegen) would have to surface as a `DIFFER`, not agree by coincidence on a root-level shape.

**Both loader families are covered by the same authority, not by "one covers the other."** `build_ir()`'s root-sniff dispatches every one of the five sampled files to the matching walker automatically — `walk_level` served the four `<fix>` dictionaries, `walk_orchestra_level` served Orchestra FIX Latest. No fallback, no manual per-family branching in the scratch program.

**Sample table** (`dictionary | msg_type | parent_path | no_tag | oracle_delim | bare_global | divergent | codegen_delim | agree | selection_reason`):

```
FIX42|6|{}|199|104|0|YES|104|AGREE|divergent
FIX42|6|{}|215|216|0|YES|216|AGREE|divergent
FIX42|8|{}|382|375|0|YES|375|AGREE|divergent
FIX42|A|{}|384|372|0|YES|372|AGREE|divergent
FIX42|E|{73}|78|79|0|YES|79|AGREE|nested
FIX42|E|{73}|386|336|0|YES|336|AGREE|nested
FIX44|0|{}|627|628|628|no|MISSING|NO_CODEGEN_ENTRY|control
FIX44|6|{555}|604|605|605|no|605|AGREE|nested
FIX44|6|{555}|683|688|688|no|688|AGREE|nested
FIX44|AX|{}|124|17|32|YES|17|AGREE|divergent
FIX44|AY|{}|124|17|32|YES|17|AGREE|divergent
FIX44|AZ|{}|124|17|32|YES|17|AGREE|divergent
FIX44|BA|{}|124|17|32|YES|17|AGREE|divergent
FIX50SP2|6|{}|199|104|104|no|104|AGREE|control
FIX50SP2|6|{}|40204|40209|40205|YES|40209|AGREE|divergent
FIX50SP2|6|{453}|802|523|523|no|523|AGREE|nested
FIX50SP2|6|{555}|604|605|605|no|605|AGREE|nested
FIX50SP2|6|{555}|41599|41604|41601|YES|41604|AGREE|divergent
FIX50SP2|6|{711}|42060|42065|42061|YES|42065|AGREE|divergent
FIX50SP2|7|{}|40204|40209|40205|YES|40209|AGREE|divergent
FIX50SP2|CC|{}|1499|453|0|YES|453|AGREE|named-FIX50SP2
FIX50SP2|CD|{}|1499|453|0|YES|453|AGREE|named-FIX50SP2
FIX50SP2|CM|{1677}|1669|1529|0|YES|1529|AGREE|named-FIX50SP2
FIX50SP2|CR|{1677}|1669|1529|0|YES|1529|AGREE|named-FIX50SP2
FIX50SP2|CS|{1677}|1669|1529|0|YES|1529|AGREE|named-FIX50SP2
FIX50SP2|CT|{1677}|1669|1529|0|YES|1529|AGREE|named-FIX50SP2
FIX50SP2|DE|{1677}|1669|1529|0|YES|1529|AGREE|named-FIX50SP2
FIX50SP2|y|{146}|1919|1920|0|YES|1920|AGREE|named-FIX50SP2
FIXT11|0|{}|627|628|628|no|MISSING|NO_CODEGEN_ENTRY|control
Orchestra FIX Latest|0|{}|627|628|628|no|628|AGREE|control
Orchestra FIX Latest|6|{453}|802|523|523|no|523|AGREE|nested
Orchestra FIX Latest|6|{555}|604|605|605|no|605|AGREE|nested
Orchestra FIX Latest|AK|{}|73|11|2887|YES|11|AGREE|divergent
Orchestra FIX Latest|AS|{}|73|11|2887|YES|11|AGREE|divergent
Orchestra FIX Latest|AX|{}|124|17|32|YES|17|AGREE|divergent
Orchestra FIX Latest|AY|{}|124|17|32|YES|17|AGREE|divergent
```

**Outcome: 34 AGREE, 0 DIFFER, 2 no-data (not disagreements — see below).** Every row where the third authority has an entry agrees with the oracle's declaration-order-first-member delimiter, including all three previously-zero FIX50SP2 groups (1499/1669/1919) and every divergent-by-conflict row. **No disagreement was found; had one been found, it would be reported here per the task brief, not adjusted away.**

**The two `NO_CODEGEN_ENTRY` rows, recorded prominently, not rounded into the agree count.** `(FIX44, "0", {}, 627)` and `(FIXT11, "0", {}, 627)` — `NoHops`, declared inside `StandardHeader`. Cause, verified at source: `populate_group_order` (`ir.cpp:147-199`) roots its walk at `it->second` — the `<message>` node only (`:180`, `walk_level(it->second, …)`) — and never walks `root.child("header")`/`root.child("trailer")`; this is INV-2's documented body-only design for the write emitter (`ir.cpp:138-139`: "rooted at each message's own `<message>` XML node (NOT header/trailer...)"). The oracle, by contrast, explicitly walks `header` and `trailer` for every message (`required_scope_oracle.hpp:230-239`, `include_header_trailer` defaulting `true`) — that is Contract 1's header/trailer carve-out, a 079 decision unrelated to codegen scope. **This is a scope gap, not a disagreement**: the codegen third authority has zero data for header/trailer-scoped group contexts, by design, in the `<fix>`-schema dictionaries. Corroborating evidence that the expected value (628, `HopCompID`) is nonetheless right: Orchestra has **no separate header/trailer elements** — `StandardHeader`/`StandardTrailer` are ordinary `componentRef`s inside each message's own `<fixr:structure>` (`ir.cpp:274`, `orchestra_loader.cpp:730-732`) — so `walk_orchestra_level` walks straight through them, and `Orchestra FIX Latest|0|{}|627|628|628|no|628|AGREE` **does** corroborate the same `(msg_type="0", {}, 627)` context, cross-family. FIXT11 additionally shows `codegen contexts=0` for the whole dictionary (below) — FIXT11.xml's `<message>` bodies (session-layer admin messages) declare zero groups at body level; every one of its 8 oracle-recorded group contexts is header/trailer-scoped.

**Reverse-direction check (does the codegen enumerate a context the oracle never recorded?).** Per dictionary, every codegen `group_order` key was checked for presence in `oracle.group_delims`:

```
FIX42: codegen contexts=38 oracle contexts=38 codegen-only=0
FIX44: codegen contexts=730 oracle contexts=823 codegen-only=0
FIX50SP2: codegen contexts=25927 oracle contexts=25927 codegen-only=0
FIXT11: codegen contexts=0 oracle contexts=8 codegen-only=0
Orchestra FIX Latest: codegen contexts=26806 oracle contexts=26806 codegen-only=0
```

Zero codegen-only contexts in every dictionary: the codegen never enumerates a context the oracle lacks. The asymmetry runs entirely one direction — oracle-enumerates-more (FIX44: 823 vs 730 = 93 more; FIXT11: 8 vs 0) — and is fully accounted for by the header/trailer scope gap above, not by a hole in either walker's coverage of the message body.

**C-1.4a inertness on this sample — shown, not inferred from absence of disagreement.** The brief's concern: `walk_level` `continue`s past an unresolvable field/component/group reference (`ir.cpp:71`, `:77`, `:84` — "defensive only") where the loader must throw (C-1.4a); if either side silently skipped a reference, the *second* child would be captured as the delimiter instead of the first, and that would show up as a `DIFFER`. The oracle's `qfix_walk`/`orch_walk` throws on exactly the same three unresolvable-reference shapes — unresolvable `<field name=>` (`required_scope_oracle.hpp:130`), unresolvable `<group name=>` (`:152`), unresolvable `<component name=>` (`:180`) — and both oracle runs (all five dictionaries) completed without throwing. Since the oracle's throw and `walk_level`'s `continue` guard the identical set of malformed shapes, a completed oracle run is proof (not inference) that no reference in these five files was unresolvable, so none of `walk_level`'s three `continue` arms fired on this corpus. The check is therefore **shown inert on the sampled corpus**, not merely "no disagreement observed, so presumably inert."

**Files touched for T007**: this section only. Scratch program not checked in (per task brief); rebuild commands above are sufficient for reproduction against `build/linux-clang-debug` as configured at this commit.

### T008 — baseline-measuring the 30 unregistered contexts (FR-002/FR-006/SC-001/SC-003/SC-015)

**Basis for the enumeration.** A context "exists" for this task exactly when the independent oracle (T005's `oracle.group_members`, `required_scope_oracle.hpp`) records it — a non-empty direct member set, mirroring the loader's own `if (members.empty()) continue` skip (`dictionary.cpp:463-465`). All numbers below are read off a fresh scratch program (`t008_probe.cpp`, not checked in; build/run commands under "Reproduction" below) built against the current `build/linux-clang-debug` libraries (confirmed non-stale: `libfixpp_dictionary.a` mtime `2026-07-31T13:38` postdates `dictionary.cpp`'s last commit `710e658e` at `2026-07-29T22:15`), and cross-checked against a **live re-run of the already-committed `dictionary_delimiter_census_test`** (T006), not merely against T006's original commit-time output.

**There are THREE distinct delimiter stores, and the task brief's two-surface framing maps to only two of them — the third is where the FIX42 anomaly below lives:**

| # | Accessor | Populated by | Gates on |
|---|---|---|---|
| 1 | `Dictionary::group_first_field(no_tag)` — the brief's "bare store" | the **loader**, directly from `<group>` XML structure (`GroupRef.first_field_tag`) | nothing message-type-specific — one global value per `no_tag` |
| 2 | `table_view::group_first_field(no_tag)` / `group_member_tags(no_tag)` — table_view's OWN "legacy bare store" | `dictionary.cpp:397-420`, which **reads store #1** and additionally requires `fr.type == NumInGroup` in `message_fields(mt)` for **some** message | store #1 AND the per-message NumInGroup type gate |
| 3 | `table_view::group_first_field(mt, path, no_tag)` / `group_member_tags(mt, path, no_tag)` — the "context store" | `dictionary.cpp:445-525` (063 Defect-A loop): members computed from **this message's own** `group_no_tag`-tagged fields; delimiter = store #1's value, **falling back to `members.front()` if store #1 is 0** (`:510-511`) | the per-message NumInGroup type gate (same as #2); delimiter derivation additionally reads store #1 but does NOT require it non-zero |
| — | `delimiter_census_test.cpp`'s "registered" discriminator | compares store #3's span `.data()` against **store #2's** span `.data()` | — |

The brief's "bare store" is store #1; the brief's "context store" is store #3, discriminated against store #2 (T006's own convention, not against store #1). Store #1 vs store #2 can diverge, and that divergence — not the 083 defect — is what the FIX42 finding below turns on.

**Population, precisely.** "Three named groups (1499, 1669, 1919) plus six nested children (1529, 1534, 1540, 1559, 1918, 1920), one context per message type" does **not** mean "every oracle context whose `no_tag` is one of these nine" — that set has **43** rows, because five of the six children (1534 once, 1540 twelve times) also occur as ordinary, unrelated groups elsewhere in FIX50SP2 (e.g. `1540` under `{1656}` in `BU`/`BV`/`CU`, standalone in `CN`/`CO`/`DH`/`DI`, under `{1772,1773,1656}` in `CV`/`CZ`/`DA`/`DB`; `1534` standalone in `CL`). It also does **not** mean "every oracle context reachable by walking down from one of the three roots through *any* descendant" — that subtree has **244** rows for `1499` alone (`CC`/`CD`), because `1499`'s own true delimiter (`453`) opens a `NoLegSecurityAltID`-shaped component tree that cascades dozens of levels deep (`146→453→454→864→1018→1483→…`), none of which spec.md names or claims as part of "the 30".

The population that reproduces spec.md's **30** exactly is: each root's own occurrences, **plus** its *named* children's occurrences **filtered to the specific ancestor-path chain spec.md names** (not the tag's occurrences elsewhere):

| root | own occurrences | named child (path chain) | child occurrences | subtotal |
|---|---|---|---|---|
| `1499` | `CC`, `CD` (2) | *(none named)* | — | 2 |
| `1669` | `CM`,`CR`,`CS`,`CT`,`DE` under `{1677}` (5) | `1529` under `{1677,1669}` (5); `1534` under `{1677,1669}` (5); `1559` under `{1677,1669,1529}` (5); `1540` under `{1677,1669,1534}` (5) | 20 | 25 |
| `1919` | `y` under `{146}` (1) | `1918` under `{146,1919}` (1); `1920` under `{146,1919}` (1) | 2 | 3 |
| **total** | | | | **30** |

**Full enumeration + both-surface scoring of the 30** (`bare` = store #1, `ctx_reg` = store #3-vs-store #2 per T006's own convention, `oracle` = the declaration-order-first-member expected value):

```
no_tag msg  path              oracle  bare(#1)   ctx_reg  ctx_delim  verdict
1499   CC   {}                453     0(unreg)   YES      146        WRONG (bare-unreg, ctx-wrong)
1499   CD   {}                453     0(unreg)   YES      146        WRONG (bare-unreg, ctx-wrong)
1669   CM   {1677}             1529   0(unreg)   YES      1529       bare-unreg, ctx-CORRECT (coincidence, see below)
1669   CR   {1677}             1529   0(unreg)   YES      1529       bare-unreg, ctx-CORRECT
1669   CS   {1677}             1529   0(unreg)   YES      1529       bare-unreg, ctx-CORRECT
1669   CT   {1677}             1529   0(unreg)   YES      1529       bare-unreg, ctx-CORRECT
1669   DE   {1677}             1529   0(unreg)   YES      1529       bare-unreg, ctx-CORRECT
1529   CM   {1677,1669}        1530   1530       YES      1530       CORRECT
1529   CR   {1677,1669}        1530   1530       YES      1530       CORRECT
1529   CS   {1677,1669}        1530   1530       YES      1530       CORRECT
1529   CT   {1677,1669}        1530   1530       YES      1530       CORRECT
1529   DE   {1677,1669}        1530   1530       YES      1530       CORRECT
1534   CM   {1677,1669}        1535   1535       YES      1535       CORRECT
1534   CR   {1677,1669}        1535   1535       YES      1535       CORRECT
1534   CS   {1677,1669}        1535   1535       YES      1535       CORRECT
1534   CT   {1677,1669}        1535   1535       YES      1535       CORRECT
1534   DE   {1677,1669}        1535   1535       YES      1535       CORRECT
1559   CM   {1677,1669,1529}   1769   1769       YES      1769       CORRECT
1559   CR   {1677,1669,1529}   1769   1769       YES      1769       CORRECT
1559   CS   {1677,1669,1529}   1769   1769       YES      1769       CORRECT
1559   CT   {1677,1669,1529}   1769   1769       YES      1769       CORRECT
1559   DE   {1677,1669,1529}   1769   1769       YES      1769       CORRECT
1540   CM   {1677,1669,1534}   1541   1541       YES      1541       CORRECT
1540   CR   {1677,1669,1534}   1541   1541       YES      1541       CORRECT
1540   CS   {1677,1669,1534}   1541   1541       YES      1541       CORRECT
1540   CT   {1677,1669,1534}   1541   1541       YES      1541       CORRECT
1540   DE   {1677,1669,1534}   1541   1541       YES      1541       CORRECT
1919   y    {146}              1920   0(unreg)   YES      1918       WRONG (bare-unreg, ctx-wrong)
1918   y    {146,1919}         1816   1816       YES      1816       CORRECT
1920   y    {146,1919}         1921   1921       YES      1921       CORRECT
```

**Totals over the 30:**
- **Bare store (#1)**: 8 unregistered (the three roots' own 8 occurrences — 1499×2, 1669×5, 1919×1), 22 registered (all six named children, in every occurrence).
- **Context store (#3, T006's own criterion)**: **30/30 registered, 0 unregistered.** Of the 30: **3 wrong** (1499×`CC`, 1499×`CD`, 1919×`y`), **27 correct** (all 22 named-children rows, plus all 5 of 1669's own rows).
- **Mechanism confirmed, not inferred**: for every one of the 8 bare-unregistered rows, `ctx_delim == min(member set)` exactly (measured: 1499→146, 1669→1529, 1919→1918, all matching `min()`), confirming `dictionary.cpp:511`'s `members.front()` fallback (`all_fields` is tag-sorted, so `.front() == min()`) is the mechanism, and that it is what makes 1669's rows land on the *correct* value: `1529` is coincidentally 1669's lowest-tag member as well as its true declaration-order delimiter, purely by luck of tag numbering. It is not coincidental for `1499` (min=146, true=453) or `1919` (min=1918, true=1920).

**Reconciliation — "30 resolve no delimiter at all today" is false on the surface validation actually consults.** Spec.md's framing ("Today `group_first_field` returns 0 for these... so they cannot be scored for delimiter *correctness*... Once FR-006 makes their three parent groups register, they enter the delimiter population for the first time") describes the **bare/store-#1** surface accurately (8/30 unregistered there — always exactly the three roots' own occurrences, never the six children) but is **false** for the **context store** — the store `validator.hpp`/the parser lambda actually query first (D-10's own words: "Every context-aware consumer... queries THIS store first"). On that surface, **all 30 already register today**, and only **3 of the 30** (10%) resolve the wrong value; the other 27 are already correct, either genuinely (the six children, whose own declarations were never broken — only their *parents*' one-level scan was) or by the coincidence documented above (1669).

**This does not mean Story 2's reception concern over-counts the 30.** Delimiter-*value* correctness (measured above) is orthogonal to the *reception* defect FR-007/FR-021e fix (whether `consume_group`/`consume_group_extent` descend when the delimiter tag itself opens a nested group). All three roots' **true** (oracle) delimiters are themselves group `no_tag`s in this dictionary — confirmed at source: `453` registers its own context (`CC`/`CD` under path `{1499}`), `1529` and `1920` likewise register their own contexts in the table above. So even 1669's five *already-correctly-valued* contexts still hit the reception defect on the wire today, because the receiver doesn't descend at the delimiter regardless of whether the stored value happens to be right. **SC-004/SC-016's "232 + 30 = 262 nested-delimiter contexts" claim is not falsified by the "3 wrong / 27 correct" split above** — it is a different axis (value-correctness vs. receiver-descent), and this task was not scoped to re-verify SC-016's own count; that reconciliation is noted as a fact worth the orchestrator confirming at T012, not asserted here.

**Escalation 1 — the FIX42 anomaly (out of scope for T008, discovered as a byproduct, contradicts a "settled" Gate A finding).** Instrumenting the FIX42 spot-check `(msg="R", path={}, no_tag=146)` that research.md's **D-12** (Gate A round 1) reported as `CTX-HIT` (`size=32`, distinct storage from the bare fallback) now measures, on the current tree: `ctx146` and `bare146` (store #3 and store #2) both `data()==nullptr, size()==0` — **not registered**, and `dict.group_first_field(146)` (store #1) returns **46**, non-zero. This means store #1 (loader-level, `<group>`-XML-structural) resolves 146 fine, but stores #2/#3 (both gated on `fr.type==NumInGroup` inside `message_fields("R")`) never populate an entry for it at all — a **different** failure mode from the 1499/1669/1919 case above (those pass the type gate and only fail store #1; FIX42's 146 apparently fails the type gate itself). This is corroborated, not a probe artifact: a **live re-run** of the already-committed `dictionary_delimiter_census_test` (`DelimiterCensus.RedCountsReconcileWithSpecBaseline`) shows **FIX42 unregistered=38 of 38** (all of them, under store #3-vs-#2), and **FIX40 (6 of 6 contexts), FIX41 (10 of 10)** unregistered the same way, plus **FIX43 (1 of 235)** at smaller scale — none of which spec.md's Baseline table lists as having *any* unregistered contexts (its FIX40/FIX41/FIX42/FIX43 rows all show `0`). D-12 is explicitly marked "OPEN... not settled" in spec.md's Assumptions and research.md; this measurement does not resolve it, but it directly contradicts D-12's round-1 "CTX-HIT=38, BARE-FALLBACK=0" refutation using the SAME accessor pair the pin itself uses. Recommend the orchestrator re-open D-12 rather than treat it as refuted; the discrepancy is most likely explained by L-063-1/#196's INT-typed-`NumInGroup` group-blindness (a documented, out-of-scope-for-083 defect) reaching stores #2/#3 in a way store #1 (and whatever probe D-12's round-1 measurement used) did not exercise, but this is a hypothesis, not a finding — I did not chase it further, per scope.

**Escalation 2 — T006's checked-in "unregistered" bucket has already drifted from spec.md's Baseline-table meaning of "unregistered", independent of the 30.** A live re-run of `DelimiterCensus.RedCountsReconcileWithSpecBaseline` today gives (dictionary → wrong / wrong-nested / polluted / unregistered): FIX40 `0/0/0/6`, FIX41 `0/0/0/10`, FIX42 `0/0/0/38`, FIX43 `4/0/0/1`, FIX44 `10/0/6/0`, FIX50 `10/0/6/0`, FIX50SP1 `12/0/6/0`, **FIX50SP2 `264/235/14/0`**, FIXT11 `0/0/0/0`, Orchestra `30/0/16/0` — **total wrong=330, wrong-nested=235, polluted=48, unregistered=55**, contexts=56,276. Every one of the 55 unregistered contexts is in FIX40/FIX41/FIX42/FIX43 (the Escalation-1 phenomenon); **FIX50SP2 — the dictionary spec.md's "30" is about — measures `unregistered=0`,** and its wrong-delimiter count is `264 = 261 (spec's measured baseline) + 3` (exactly the 3 named-root rows found wrong above), cross-validating this task's own measurement against the pin's independently-computed total. **FR-012/FR-016 forbid any carve-out or exclusion list on this pin.** If store #3-vs-#2 stays the pin's registration criterion through Phase 3 and 083 does not also touch #196's INT-typed carve-out (explicitly out of scope — #196 is `CLAUDE.md`'s "next candidate", not 083's), **SC-003 ("unregistered contexts falls from 30 to 0") is unsatisfiable by this feature's own pin as currently written**, because the pin's unregistered bucket will keep reporting 55, permanently, regardless of what 083 fixes — a pre-existing, unrelated defect family occupies the same bucket name. This is a pin-design question for T012, not something T008 should fix by re-pointing the pin.

**Reconciliation against "335→365" (SC-001), restricted to FIX50SP2 — the only dictionary the 30 touches, and the only reconciliation this task was briefed to perform.** wrong 261→264 (+3, matching the 3 named-root rows found wrong here), wrong-nested 232→235 (+3, the same 3 — cross-validated independently by the live pin's own `nested` computation, which derives "nested" from a completely different code path than this task's manual check), polluted unchanged at 14, unregistered 30(projected)→**0 (measured)**.

The live pin's dictionary-wide total (`wrong=330`) is **not** `335 + 3`; every other dictionary's `wrong` count matches spec.md's Baseline row exactly (FIX43 `4`, FIX44 `10`, FIX50 `10`, FIX50SP1 `12`, Orchestra `30`, all unchanged) except **FIX42**, whose live `wrong=0` diverges from spec.md's Baseline row of `8` — a **separate** live-vs-baseline delta this task did not re-derive or explain (it belongs to Escalation 1, the FIX42 anomaly, not to the 30-context population). The arithmetic closes exactly once that delta is included: `330 = 335 − 8 (FIX42, unexplained) + 3 (FIX50SP2, this task's finding)`. **This task closes the FIX50SP2 leg only** (`261→264`, `232→235`, `30→0`); the FIX42 leg (`8→0`) is Escalation 1's territory and should not be asserted resolved here.

The one number this task *can* close precisely: **SC-001's "365 = 335 + 30" characterization of the 30 as *entirely* wrong-delimiter-once-registered is not correct** — of the 30, only **3** are wrong on the context-store surface (measured), 27 are already correct there. The **30** count itself (population size) reconciles exactly against spec.md's projection, once the population is defined as "root occurrences plus named-children occurrences under the named path chain" rather than "every context whose `no_tag` is one of the nine" (43) or "the full descendant subtree" (244).

**Reproduction.** `t008_probe.cpp` (not checked in): builds `build_quickfix_oracle(FIX50SP2.xml)`, loads FIX50SP2 via `XmlLoader` into a 32 MiB `pmr::monotonic_buffer_resource`, calls `dict.as_table_view()`, and scores every oracle-enumerated context whose `no_tag` is in `{1499,1669,1919,1529,1534,1540,1559,1918,1920}` plus a subtree-filtered and a FIX42-spot-check pass. Compile/link exactly as T007's reproduction command (`tests/dictionary/CMakeLists.txt:186-199`'s library set): `clang++ -std=c++23 -stdlib=libstdc++ -I<repo>/include -I<repo>/tests -I<repo>/tests/dictionary -isystem <pugixml include> -DFIXPP_DICT_DATA_DIR=\"<repo>/dictionaries\" t008_probe.cpp -o t008_probe -Wl,--start-group build/linux-clang-debug/lib/{libfixpp_dictionary,libfixpp_wire,libfixpp_dict_dispatch_bridge,libfixpp_core}.a <pugixml lib>/libpugixml.a -Wl,--end-group`. Cross-check: `ctest --test-dir build/linux-clang-debug -R delimiter_census -V` (or run `dictionary_delimiter_census_test --gtest_filter='DelimiterCensus.RedCountsReconcileWithSpecBaseline'` directly) reproduces the live totals cited above.

**Recommended disposition for spec.md — not applied.** Per the task brief's escape valve ("if the right amendment is not obvious, do not guess"): the population size (30) is right, but the sentence built on top of it — "these contexts resolve no delimiter at all today... they enter the delimiter population for the first time" — is wrong on the surface that matters for validation, and the affected loci span the Baseline table (FIX50SP2 row + its footnote), SC-001 (335/365), SC-004, and SC-016, not one figure. That is a restructure, not a surgical single-line edit, so I have not touched spec.md. If the orchestrator wants a starting point at T012: reframe the FIX50SP2 Baseline-row footnote and SC-001 to say the 30 are unregistered on the **bare-store surface only** (which gates the C-ABI construction predicate, FR-019a class (c)) and **already registered, 27-of-30 already correct, 3-of-30 wrong**, on the **context-store surface** validation consults — and treat "wrong-delimiter, once registered" as **3**, not 30, when sizing what changes on Phase 3. Separately, Escalation 1 (FIX42) and Escalation 2 (the pin's unregistered bucket already including 55 pre-existing #196-caused unregistered contexts, none of them FIX50SP2) are new findings with their own blast radius on SC-003 and D-12 and are not folded into this recommendation — they need their own orchestrator decision before Phase 3, independent of the 30.

**Files touched for T008**: this section only. Scratch program not checked in.
