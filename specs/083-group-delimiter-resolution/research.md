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
