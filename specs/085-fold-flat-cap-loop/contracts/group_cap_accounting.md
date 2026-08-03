# Contract: `OffsetTable::group()` cap accounting

**Feature**: 085-fold-flat-cap-loop · **Date**: 2026-08-03 · **Owns**: `src/wire/offset_table.cpp` — `OffsetTable::group()`. It additionally places **one standing obligation on `consume_group_extent`** (**C-1**), and no other: that function's signature, body, contract and dict-free bail are out of scope and are not modified by this feature (C-4). *(Ownership statement corrected at Gate A round 2. It previously read "`OffsetTable::group()` **only**", which contradicted C-1's normative obligation on `consume_group_extent` and C-4's listing of the same function as untouched — three statements in one file that could not all be true. The resolution is wording, not re-scoping: **govern ≠ modify**.)*

**Relationship to 083's contracts**: `contracts/typed_read_splitter.md` (083) owns `offset_table.cpp` broadly and explicitly **scoped the cap loop out** — *"The `:526` cap-loop scope-out is kept as-is — it was independently verified sound at rounds 2 and 3 and was not reopened"* (083 spec.md, Gate A exhaustion session). This contract picks up exactly that scoped-out site and nothing else. It does **not** amend C-8.0, C-8.0b or C-8.0c.

---

## C-1 — The nesting-aware walk must cap the instances whose extent it returns (STANDING)

*(Restructured at Gate A round 1. The previous C-1 froze the **premise** of the one-time removal — "`group()` and `consume_group_extent` resolve `delim` from the same entry" — as if it were a standing invariant, and asserted a consequence that is false against the tree. Both halves are corrected below: the premise is demoted to **C-1a**, a delivery-time proof obligation, and the standing property is restated as the one that actually survives the removal. See `plan.md` `## Gate A`.)*

**Statement.** In `OffsetTable::consume_group_extent`, the per-instance cap MUST be applied to the **same nesting-aware instances whose extent the function returns**. Concretely, these three must remain one walk over one partition:

| Role | Site (`main` = `c1564dd2`) | Code |
|---|---|---|
| opens an instance | `consume_group_extent`'s outer `while` / instance anchor — `:477-478` | `while (k < entries_.size() && inst < declared && entries_[k].tag == delim) { std::size_t const inst_start = k;` |
| measures + caps it | the per-instance cap check — `:521-524` | `if ((k - inst_start) > cfg_.max_group_entries_per_instance) { overflow = true; return k; }` |
| returns the extent | the function's normal exit — `:527` | `return k;` |

Any future change that moves the extent computation, alters the instance-opening rule, or re-points the delimiter `consume_group_extent` walks on (`:458`) MUST either re-establish that `:521-524` still measures the instances `:527` describes, **or** re-introduce an independent per-instance cap on `group()`'s dictionary path.

**Why this is the standing property, and the old one is not.** After 085 the dictionary path has **no second cap**. `group()`'s local `delim` (`:551`) retains exactly one dictionary-path use — the group-recognition gate at `:566`, `group_member_fn_(opaque_dict_, ctx, no_tag, delim)` — and partitions nothing. `consume_group_extent` never receives it; it recomputes its own at `:458`. So re-pointing `:551` alone after this feature changes **whether a group is recognised at all** (an `err_required_field_missing` at `:567` where the frame previously succeeded, or vice versa) — it cannot cause cap under-enforcement, because there is no longer a second walk whose partition could disagree. The property that *does* become newly load-bearing is the one above: before 085 a divergence inside `consume_group_extent` was masked by the flat re-walk; after 085 `:521-524` is the dictionary path's entire per-instance DoS defence.

**Divergent delimiter *sources* over one extent are already shipped, and are benign.** This is the empirical check that disposes of the old C-1's consequence clause. `group_slices_status` resolves its splitter delimiter from the **per-context dictionary store**:

```
src/wire/offset_table.cpp:704-711   std::uint16_t delim = entries_[first].tag;
                                    if (opaque_dict_ != nullptr && group_delim_fn_ != nullptr) { ... delim = d; }
src/wire/offset_table.cpp:712-715   std::size_t inst_start = first;
                                    for (std::size_t k = first; k <= group_end; ++k) {
                                        bool const boundary = (k == group_end) || (k > first && entries_[k].tag == delim);
```

That is the *same* interval `[first, group_end]` and the *same* boundary predicate as the flat cap loop (`:584-595`, predicate at `:586`), resolved through `group_delim_fn_` where `group():551` reads the wire — and nothing structurally ties the two answers together. Two walks, one extent, independent delimiter sources, on shipped `main` since 083. The engine does **not** under-enforce the cap, precisely because the dictionary path's cap lives in the nesting-aware walk rather than in either flat walk. `spec.md` FR-007a and `data-model.md` E-3 already record this asymmetry as pre-existing and benign — so a contract claiming that the *same* asymmetry, one function over, would be catastrophic contradicts the bundle's own record. That is what the round-1 C-1 did.

**Partly guarded by a test — and the two halves are recorded separately.** *(Narrowed at Gate A round 2. The round-1 heading read "Guarded by a test, not only by a comment", which claimed more than the evidence delivers. The Verification matrix below was already correctly scoped — "C-1 (that pin is load-bearing)" — and it is the narrative that is brought down to it.)* The old C-1 claimed no test could guard it; that claim does not carry over, and it was wrong in its own terms:

- **Half 1 — that dictionary-path cap enforcement EXISTS is directly mutation-testable, and is tested.** Delete the comparison at `:521-524` and the dictionary-path pin `WireOffsetTable.DoSCapPerInstanceRejectsOversizedSingleInstance` goes RED. That is SC-003's mutation clause and `quickstart.md` §2a-mut; it is the same evidence shape FR-005a(i) already requires for the dict-free site. **Post-relocation only**: on baseline the flat loop at `:584-595` still sits on the dictionary branch, catches the same breach one branch later and returns the same `wire_group_too_large` from `:592`, so the pin stays **GREEN** (measured 2026-08-03 at `c1564dd2` — FR-005b, `research.md` R-1 step 4). A transcript taken on baseline is a false negative and discharges nothing.
- **Half 2 — that the cap stays COUPLED to the returned nesting-aware partition is NOT test-guarded, and this contract says so.** The pin's frame (`tests/wire/offset_table_test.cpp:199-235`) is `453=1` with a single **unnested** instance of four entries against a cap of three, so its flat and nesting-aware partitions are identical; deleting the whole comparison cannot distinguish them. A future change re-anchoring `inst_start` **after a nested descent** (`:493` / `:512`) would under-count only for nested instances and leave this pin — and this mutation — green while C-1 was violated. The coupling therefore rests on **source inspection at delivery** (the three sites in the table above are one walk over one partition) **plus** the FR-002 source comment obliging the next maintainer of that walk to re-verify it or re-introduce an independent cap. A nesting-sensitive tight-cap fixture that would close this half was **considered and rejected as out of scope** — new behavioural test scope on a removal, aimed at a standing contract rather than at anything this change introduces (`plan.md` `### Round 2 — disagreements`).
- **The old "no test can guard it" was false as an absolute.** A divergent-*source* fixture **is** constructible: `:566` and `:461` test **membership** only — `group_member_fn_(…, no_tag, delim)` — never delimiter identity, so a frame whose first post-count entry is a valid member `M` ≠ the store's delimiter `D` passes both gates while making the wire-derived and store-derived sources differ.
- **But that fixture does not guard the cap.** It would guard the recognition gate at `:566` and the splitter at `:704-711`. Codex's counter-proposal is therefore aimed at a different property than the contract names, and this contract does **not** require it. Recorded in `plan.md` `## Gate A` → `### Round 1 — disagreements`.

**Obligation.** The delivered source MUST carry this invariant as a comment on `group()`'s dictionary branch (FR-002). The comment states **C-1**, not C-1a: the cap on this path is enforced solely by `consume_group_extent`'s per-instance check over the instances whose extent it returns, and a change to that walk must re-verify the cap measures that partition or re-introduce an independent cap here. Per FR-007b's anchoring rule the comment names each site by **function and role first**, with any line number appended and stamped as-of the merge commit. Its landing and its content are verified by SC-005a / `quickstart.md` §1d.

**Status**: standing from delivery onward. Strengthened, not weakened, by this feature.

---

## C-1a — Delimiter-source equality: a delivery-time proof premise (NOT standing)

**Statement.** At the reviewed commit `main` = `c1564dd2`, `group():545,551` and `consume_group_extent():450,458` both compute `first = count_idx + 1U` and read `entries_[first].tag`, from the same `count_idx` passed at `:575`. **Identical by construction.**

**What it licenses.** Exactly one thing: the **one-time** removal. It is R-1 step 1, without which step 2's superset relation (the flat cut-set over `[first, group_end]` contains every nesting-aware instance start) does not hold and FR-002's no-op claim is void.

**Why it does not bind the future.** It is a fact about the reviewed commit, not a rule about the code. Once the flat loop is gone from the dictionary branch there is no second partition, so there is no pair of keys left to agree or disagree. A reader who finds the two sources divergent *after* 085 has found a recognition-gate question (`:566`), not a cap question.

**Discharged by.** Source inspection at delivery — **primarily** `research.md` R-1 step 1, re-derived at `c1564dd2`, whose two-row table carries all four anchors (`:450`/`:458`, `:545`/`:551`) and the `:575` call; plus `quickstart.md` §1c's delivery-time re-check. *(Corrected at Gate A round 2: §1c previously named `git diff main -- src/wire/offset_table.cpp`, which **cannot display these anchors** — `consume_group_extent:450,458` is ≈130 lines above the relocation's hunks and `group():545,551` ≈30 lines above, outside default diff context. §1c now uses `git show main:src/wire/offset_table.cpp | sed -n '450p;458p;545p;551p;575p'`. The premise itself was never undischarged — R-1 step 1 always carried it — so this is a procedure fix, not a gap.)* Nothing carries forward; there is no standing obligation and no test.

**Status**: satisfied at delivery; **discharged**, not standing.

---

## C-2 — Dictionary path: exactly one traversal

**Statement.** On the dictionary path (`opaque_dict_ != nullptr && group_member_fn_ != nullptr`), `group()` MUST perform exactly one traversal of the group's entries and MUST NOT re-walk `[first, group_end]` after `consume_group_extent` returns.

**Discharged by an executable gate, not only by inspection** *(added at Gate A round 2)*. Until round 2 this contract — the feature's headline obligation — was checked by a human reading the diff once, and by nothing thereafter. **FR-001b**'s red-first structural pin `WireOffsetTable.FR001_SingleTraversalSourceInspection` now asserts it directly: the flat block must be **inside** the dict-free `else` and **absent** from `group()`'s body after the `if/else`. RED on the unmodified tree, GREEN after the relocation, and standing afterwards as a regression gate. Verified by **SC-005b**. Source inspection (`quickstart.md` §1c) is retained as the second discharge, because the pin keys on location and the nine-item C-3 checklist keys on content.

**Pre-conditions.** `count_idx` located (`:536-544`); `first < entries_.size()` (`:546`); `delim` confirmed a member of `no_tag`'s group in context (`:566`).

**Post-conditions.**

| | Value | Unchanged from pre-085 |
|---|---|---|
| `group_end` | `consume_group_extent(count_idx, ctx, ctx.depth, overflow)` | Yes |
| on `overflow` | `err_group_too_large` at `:576-578` | Yes |
| return | `group_index{no_tag, first, group_end - first}` | Yes |
| second cap check | **absent** | **Removed** |

**Justification obligation (FR-002).** The dictionary branch MUST carry a comment with **two** parts, both true after removal:

1. *Why the second walk went* (research.md R-1, the removal's justification): the cap is already applied at `consume_group_extent`'s per-instance check over nesting-aware boundaries, the flat partition refined that one, and the nesting-aware walk returns first — so the second walk could not fire.
2. *What now stands in its place* (**C-1**, the standing property): this branch's per-instance DoS defence is now solely `consume_group_extent`'s cap over the instances whose extent it returns; a change to that walk must re-verify the cap measures that partition, or re-introduce an independent cap here.

The comment anchors at the branch, not at a removal site, because after removal there is no site. Per FR-007b it names each site by function and role first, with any line number appended and stamped as-of the merge commit. Verified by SC-005a.

**Error disposition.** Unchanged in value, origin and ordering. `err_group_too_large` still reaches callers only via `:577`, which is `consume_group_extent`'s overflow — as it already did for every frame that could reach it (research.md R-3).

---

## C-3 — Dict-free path: cap preserved, semantics unchanged

**Statement.** On the dict-free path (`opaque_dict_ == nullptr || group_member_fn_ == nullptr`), the per-instance cap MUST remain enforced, by the **same source lines** relocated with no change other than the mechanical re-indentation the move into `else` forces.

**Pre-conditions.** As C-2 minus the membership check at `:566`, which the dict-free branch does not reach.

**Post-conditions — all identical to pre-085:**

| | Value |
|---|---|
| `group_end` | `entries_.size()` |
| delimiter | `entries_[first].tag` (wire-derived, `:551`) |
| boundary rule | `(k == group_end) \|\| (k > first && entries_[k].tag == delim)` |
| segment measure | `k - inst_start`, `inst_start` re-anchored at each boundary |
| breach | `err_group_too_large` |

**Semantic-preservation obligation (FR-001a).** *(Restated at Gate A round 1. The previous wording — "MUST NOT be rewritten, reindented into a different shape, parameterised, or extracted", paired with FR-003's "byte-identical" — was **unsatisfiable**: the block sits at 4-space function-body indentation and the `else` body is 8-space, so relocation necessarily re-indents, and `clang-format` is a Tier-1 gate `[const §IX.4]` that will force it. An unsatisfiable literal rule silently reverts FR-003 to the equivalence argument the bundle claimed to avoid.)*

The relocated lines MUST NOT be parameterised, extracted into a helper, merged, re-ordered, or restructured. **Mechanical re-indentation is permitted and expected**; nothing else is. The diff for this branch MUST read as a move plus a uniform indent shift. Each item below is checked against the moved block:

| # | Preserved element | As-of `main` = `c1564dd2` |
|---|---|---|
| 1 | `inst_start` is declared on its **own statement** before the loop, not merged into the for-init | `std::size_t inst_start = first;` (`:584`) |
| 2 | Loop bounds — `k` starts at `first`, condition is `k <= group_end` (inclusive), increment `++k` | `:585` |
| 3 | Boundary predicate, both disjuncts and their order | `(k == group_end) \|\| (k > first && entries_[k].tag == delim)` (`:586`) |
| 4 | Early `continue` on non-boundary (no `else` inversion) | `:587-589` |
| 5 | Segment measure — `k - inst_start`, computed at the boundary | `:590` |
| 6 | **Strict** `>` against `cfg_.max_group_entries_per_instance` (not `>=`) | `:591` |
| 7 | Breach returns `err_group_too_large<group_index>()` immediately | `:592` |
| 8 | Re-anchor **after** the check, `inst_start = k` | `:594` |
| 9 | The function's `return group_index{no_tag, first, group_end - first};` is reached with the same values, from outside the `if`/`else` | `:596` |

This is what reduces FR-003 from an equivalence argument to a checklist walked over the diff. `research.md` R-4 quotes the actual block; the delivered `else` body must match it item-for-item.

**Ordering obligation (FR-009).** `group_end = entries_.size()` MUST remain assigned before the loop, as today. No work is added on either path.

**Recorded looseness (FR-003a, fixpp#220).** The final segment ends at `entries_.size()`, so top-level fields following the group count toward the last instance. Under default `Config` this cannot breach the cap — `default_max_offset_entries == default_max_group_entries_per_instance == 4096` and `build()` clamps at the former (`:326`), so the segment is `≤ 4095`. It becomes a false positive only where a caller sets `max_group_entries_per_instance < max_offset_entries - 1`. **Preserved, not repaired**; recorded as a limitation citing fixpp#220.

---

## C-4 — Untouched by this contract

Named explicitly so a reviewer can see the boundary rather than infer it.

| Site | Why out of scope |
|---|---|
| `consume_group_extent` (`:442-528`) — signature, body, contract, dict-free bail at `:454-456`: **not modified by this feature** | FR-001a/FR-008. The "literal fold" alternative was rejected at `/clarify` Q1. **Two carve-outs, stated so this row does not contradict C-1** *(reconciled at Gate A round 2)*: (i) **C-1 governs this function as a standing obligation** — govern is not modify, and no line of it changes in the delivered diff; (ii) SC-003's mutation deletes `:521-524` **transiently**, in a working tree, and restores it — no delivered change; that is evidence-gathering, not scope. |
| `group_slices_status()` instance splitter (`:712-733`) — `group_slices()` (`:639-641`) merely delegates | L-063-4 **leg 1**, descoped with evidence by 083 (empty target population across all ten dictionaries; a literal implementation would break the 485-context shape that *is* reachable). Still flat, and still resolving `delim` from the **dictionary store** (`:704-711`) rather than the wire — see FR-007a |
| `group_slices_reserve_bound()` (`:599-634`) | Not reached by this change |
| Per-context delimiter store, membership callbacks, `group_context` | 083's contracts own these |

---

## Verification matrix

| Contract | Discharged by | Artifact |
|---|---|---|
| C-1 (cap still fires over the returned instances) | `WireOffsetTable.DoSCapPerInstanceRejectsOversizedSingleInstance` stays green | SC-003 |
| C-1 (that pin is load-bearing) | Dictionary-path mutation: delete `consume_group_extent`'s per-instance comparison (`:521-524` as-of `c1564dd2`) → pin RED → restore → green. Transcript in the `/speckit-verify` record. **ORDER-DEPENDENT — valid only post-relocation**: measured on baseline the pin stays GREEN, because the flat loop at `:584-595` still pre-empts on the dictionary branch (FR-005b, `quickstart.md` §2a-mut) | SC-003, `quickstart.md` §2a-mut |
| C-1 (partition coupling — cap measures the instances whose extent is returned) | **NOT test-guarded, and deliberately so.** Source inspection at delivery of `:477-478` / `:521-524` / `:527` as one walk over one partition, plus the FR-002 comment's standing instruction to re-verify. The row above cannot reach this: its fixture is one *unnested* instance. A nesting-sensitive fixture was considered and rejected as out of scope for a removal | Source inspection; `plan.md` `### Round 2 — disagreements` |
| C-1 (carried into the source) | The FR-002 comment exists on the dictionary branch and states C-1's standing property + FR-007b anchoring | **SC-005a**, `quickstart.md` §1d |
| C-1a (delivery-time premise) | Source inspection at `c1564dd2`; nothing standing. **Primary discharge is `research.md` R-1 step 1**, whose two-row table carries all four anchors; `quickstart.md` §1c is the delivery-time re-check and now uses `git show main:… \| sed -n '450p;458p;545p;551p;575p'` rather than `git diff`, which cannot display anchors ≈130 and ≈30 lines outside the relocation's hunks *(corrected at Gate A round 2)* | research.md R-1 step 1; `quickstart.md` §1c |
| C-2 (one traversal, no post-`consume_group_extent` re-walk) | **The FR-001b red-first structural pin** `WireOffsetTable.FR001_SingleTraversalSourceInspection` — RED on the unmodified tree, GREEN after the relocation, standing thereafter — **plus** source inspection of the diff | **SC-005b**, SC-005; `quickstart.md` §0 and §1c |
| C-2 (no behaviour change) | Existing corpora unchanged | SC-001, SC-002 |
| C-3 (semantics preserved) | The 9-item preservation checklist walked over the diff; ~16 existing dict-free tests stay green | FR-003, SC-004, SC-005 |
| C-3 (cap fires, load-bearing) | **New** dict-free + tight-`Config` pin `WireOffsetTable.DictFreeDoSCapPerInstanceRejectsOversizedInstance`, its bracketing companion `…AllowsWhenCapRaised`, and the RED-under-mutation transcript | FR-004, FR-005a, SC-004, SC-004a |
| C-3 (looseness recorded) | Limitation row citing fixpp#220 | FR-003a, SC-010 |
| C-4 | Diff touches no listed site | FR-008, SC-007 |
| FR-009 (no new allocation) | **By construction — no SC.** The change deletes a loop and moves it; it introduces no expression that can allocate, and `group()` is `noexcept` with no container operation on either branch. Recorded here so the gap is visible rather than implied. The per-call-work leg is carried by SC-006 | FR-009; see `plan.md` VIII §5 row |
