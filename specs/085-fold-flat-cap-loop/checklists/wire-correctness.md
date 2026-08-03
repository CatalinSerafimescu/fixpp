# Checklist: Wire correctness & semantic preservation

**Feature**: `085-fold-flat-cap-loop` · **Created**: 2026-08-03 · **Audience**: Gate B
**Domain**: the `src/wire/offset_table.cpp` change itself — does the relocation preserve what it must, and is the removal actually safe?

> **Why this domain exists.** The feature's entire risk is that a change advertised as a no-op is not one. The redundancy argument (`research.md` R-1) is a *proof*, and a proof's premises can rot. These items check the premises, the preservation, and the one thing no test can see.

## Premises of the removal

- [ ] CHK001 Is R-1 **step 1** (both walks read `entries_[count_idx + 1].tag` — `group():545,551` vs `consume_group_extent():450,458`, same `count_idx` passed at `:575`) re-verified against the tree being implemented on, not inherited from `/plan`? [A-001, C-1a, T001]
- [ ] CHK002 Is R-1 **step 2** (`consume_group_extent` opens an instance only where `entries_[k].tag == delim`, `:477`, so the flat cut-set is a **superset**) still true of the delivered `consume_group_extent`? [R-1, T001]
- [ ] CHK003 Is R-1 **step 4** (the nesting-aware walk runs first and returns `err_group_too_large` at `:576-578` on breach, **before** `:584`) still true — i.e. is `:576-578` still unconditional? [R-1, T001]
- [ ] CHK004 Are all five of `consume_group_extent`'s exceptional exits still accounted for — including that the depth guard returns `count_idx` (not `first`) but sets `overflow` first, so the underflowing `group_end - first` state stays unreachable? [R-1, T001]
- [ ] CHK005 If any premise fails, is the feature **re-scoped rather than patched**, per T001's stop-gate? [A-001, T001]

## Semantic preservation of the relocated block

- [ ] CHK006 Was **C-3's checklist walked item-for-item against the contract table**, not against a summary? (An abbreviated restatement is what `/speckit-analyze` F1 caught; the dropped items were the load-bearing ones.) [FR-001a, C-3, T007]
- [ ] CHK007 C-3 #1 — is `inst_start` still declared on its **own statement** before the loop, not merged into the for-init? [C-3, T007]
- [ ] CHK008 C-3 #3 — is the boundary predicate unchanged in **both disjuncts and their order**? [C-3, T007]
- [ ] CHK009 C-3 #4 — is the **early `continue` on non-boundary** retained, with **no `else`-inversion**? *(The single most likely unintended edit: semantically identical, invisible to T006's indentation-keyed pin, and green under every behavioural test.)* [C-3, FR-001a, T007]
- [ ] CHK010 C-3 #6 — is the comparison still **strict `>`**, not `>=`? [C-3, T007]
- [ ] CHK011 C-3 #7/#8 — does a breach return **immediately**, and is `inst_start = k` re-anchored **after** the check? [C-3, T007]
- [ ] CHK012 C-3 #9 + FR-009's ordering obligation — is the shared `return group_index{...}` reached with the same values from outside the `if`/`else`, and is `group_end = entries_.size()` still assigned **before** the loop? [C-3, FR-009, T007]
- [ ] CHK013 Does the diff for the `else` branch read as a **move plus a uniform indent shift** — nothing parameterised, extracted, merged, re-ordered or restructured? [FR-001a, C-3, T007]

## The dictionary path after removal

- [ ] CHK014 Does `group()`'s dictionary branch contain **exactly one** traversal of the group's entries — no re-walk of `[first, group_end]` after `consume_group_extent` returns? [FR-001, C-2, SC-005]
- [ ] CHK015 Is the FR-002 comment present on the dictionary branch, and does it state **both** required parts — why the second walk went, **and** C-1 as the standing property? [FR-002, SC-005a, T008]
- [ ] CHK016 Does that comment **avoid** stating the C-1a delimiter-source equality as a standing invariant? *(It is a discharged delivery-time premise. The round-1 contract asserted the opposite and was false against the tree — `group_slices_status:704-715` already diverges benignly.)* [C-1, C-1a, FR-002]
- [ ] CHK017 Is the returned `group_index` — `no_tag`, `first_entry()`, `entry_count()` — unchanged in value on every frame? [E-1, FR-002, SC-002]
- [ ] CHK018 Is the error **disposition, origin and ordering** unchanged: `err_group_too_large` still reaching callers only via `:577`? [C-2, SC-002]

## The dict-free path

- [ ] CHK019 Is the cap still enforced on the dict-free path — i.e. was the loop **relocated**, not deleted? *(Deleting it outright is a silent fail-open; that is the whole reason this feature is not a one-line removal.)* [FR-003, C-3]
- [ ] CHK020 Are the dict-free path's delimiter source (wire, `:551`), segmentation, extent bound (`entries_.size()`) and error value all unchanged? [FR-003, C-3]
- [ ] CHK021 Do the ~16 existing dict-free `OffsetTable{frame, mr}` tests stay green — including the dict-free `group(453)` at `offset_table_test.cpp:150-157`? [FR-003, R-5, T011]
- [ ] CHK022 Is the dict-free trailing-field over-count **preserved, not tightened** — with its default-config unreachability stated? [FR-003a, A-005, SC-010]

## What no test can see

- [ ] CHK023 Is **C-1** (the nesting-aware walk caps the same instances whose extent it returns — `:477-478` opens, `:521-524` caps, `:527` returns) recorded as **standing**, and **C-1a** as **discharged**? [C-1, C-1a]
- [ ] CHK024 Is the scope of what FR-005b's mutation proves stated honestly — **C-1's cap-existence half only**, on an *unnested* instance, with the partition coupling left to source inspection plus the FR-002 comment? [FR-005b, SC-003]
- [ ] CHK025 Is FR-008 satisfied — `include/fixpp/wire/offset_table.hpp` untouched, no signature, `Config` member, default, exported symbol, error enum value or C-ABI version changed? [FR-008, SC-007, T021]
- [ ] CHK026 Is FR-009's allocation leg true by construction — no `new`/`delete`/container growth introduced on either path? [FR-009, `[const §VIII.5]`]

## Notes

- Items are dispositioned at `/speckit-checklist-audit` (pipeline step 9) as PASS / SPEC-FIXED / DD-DECIDED §X / WAIVED:`<reason>`. Completeness/Clarity/Consistency gaps may **not** be WAIVED.
- CHK009 is the item to spend real attention on. It is the only one whose failure mode is invisible to every automated gate this feature ships.
