# Contract — Per-context delimiter path (loader → handle → table view)

**Feature**: `083-group-delimiter-resolution` | Satisfies FR-001..FR-005, FR-006d, FR-010, FR-011, FR-015, FR-023, FR-023a, FR-023b
*(header traceability corrected 2026-07-31, Gate A round 2 — it was one round behind the body: FR-011 has been satisfied by C-3.2 since round 0, and round 1 added FR-023/C-3.4, FR-023a/C-3.4's tolerant-mode clause, FR-023b/C-7.3 and FR-006d/C-3.6 without updating this line.)*

## Surface

Three seams, all internal to the library. *(Corrected 2026-08-02, Gate B r1 F2/IND-5 — the sentence below was a blanket claim that is false at source; C-8.1 of `contracts/typed_read_splitter.md` explicitly authorizes and names the actual widening.)* **No C ABI symbol changes.** `include/fixpp/wire/offset_table.hpp`'s `OffsetTable` ctors and `include/fixpp/wire/parser.hpp`'s `MessageView<Index>` ctors each gain one **defaulted** `group_delim_fn_t` parameter (C-8.1) — a public C++ header signature widening, source-compatible for every existing caller. C-8.3's narrower promise — `group_slices(no_tag)` / `group_slices_status(no_tag)` keep their signatures — holds exactly, and the C ABI freeze holds byte-for-byte (verify Step 5: `nm` golden 72 = 72 identical, `include/fix/c_api*` diff empty).

| Seam | Direction | Contract |
|---|---|---|
| loader walk → record sink | write | one record per `(msg_type, parent_path, no_tag)` context reached during **message** expansion |
| metadata handle | store | immutable after load; PMR-allocated on the caller's resource |
| handle → `as_table_view()` | read | the delimiter fed to `set_group_first_ctx`, replacing the global lookup |

## Producer contract (both loaders)

**C-1.1** — A record is emitted for every group context reached during per-message expansion, and for no other. The component-cache and group-cache expansions are not message-scoped and MUST emit nothing.

**C-1.2** — The recorded delimiter is the **first member emitted at that group's level**, in document order. Because component members expand inline at the enclosing level and a nested group's count tag is emitted at the outer level before descent, this value is the FIX delimiter with no additional traversal. Implementations MUST NOT reintroduce a separate scan to compute it (research.md D-1).

**C-1.3** — `parent_path` is outermost-first and **excludes** the group's own `no_tag`, matching the existing context-store convention.

**C-1.4** — If a context's first emission never occurs, the delimiter is unresolvable and the FR-006 disposition applies (see `loader_tolerant_mode.md`). Recording `0` is not permitted. **This clause is about a context that is produced at all** *(reconciled 2026-07-30, Gate A round 1)*: a group declaration that produces **zero** contexts, because no message expansion reaches it, is not covered here and is **not** the fail-closed condition — see C-3.6 and `loader_tolerant_mode.md` C-6.1a, which is the governing granularity. Read without that pointer, this clause's per-context predicate never fires for such a group and the silent drop FR-006 exists to kill survives inside the requirement meant to kill it.

**C-1.4a — No silent skip.** Both loaders currently **throw** on an unresolvable field/component/group reference rather than skipping it. This disposition MUST be preserved: C-1.2's "first emission is the delimiter" holds only if no leading child can be silently dropped. A skip would make the *second* child the captured delimiter, silently, and only on dictionaries containing such a reference.

**C-1.4b — The global lookup stays populated.** The bare `group_first_field(no_tag)` accessor MUST continue to return a non-zero value for every registered group, derived from this table as a first-seen projection. It is used as an *is-this-tag-a-group* **predicate**, including at C-ABI construction sites; leaving it unpopulated after the old scan is deleted would make the C ABI reject all groups. See research.md D-10 for the consumer list.

**C-1.5 — Symmetry.** The QuickFIX-XML and Orchestra loaders MUST implement C-1.1..C-1.4 with identical observable behaviour. Measured evidence that this is not optional: Orchestra has zero broken-scan and zero unregistered groups yet still exhibits 30 wrong-delimiter contexts, so the defect is present in both and a one-loader fix is a half-restructure.

## Store contract

**C-2.1** — Immutable after load; no mutation path is exposed.

**C-2.2** — Keyed identically to the existing group context store, so a key that resolves in one resolves in the other.

**C-2.3** — Load-time allocation only. No allocation on the parse/validate path.

## Consumer contract

**C-3.1** — `as_table_view()` sources the delimiter for `set_group_first_ctx` from this table, not from the global first-seen lookup.

**C-3.2** — The false comment asserting the per-context member set "stays exact regardless" of divergent delimiters is corrected in the same change (FR-011). It is not merely wrong; it is the reason the defect survived inspection.

**C-3.3** — `set_group_first_ctx`'s existing member injection is **retained** — the unconditional `add_group_member_ctx(msg_type, parent_path, no_tag, first);` at `include/fixpp/dict/table_view.hpp:645`, inside the function at `:641-646` (**citation corrected again 2026-07-31, Gate A round 3** — round 1 said the call was in `:641-645`, round 2 "corrected" it to a call at `:646` inside `:641-647`, and **both are off by one**: verified at source, `:641-642` is the signature, `:643` `set_group_bit`, `:644` the `group_ctx_` write, **`:645` the `add_group_member_ctx` call**, `:646` the closing brace. Round 2's own note that *"D-5's `:641-646` was right"* was right about the **function span** and wrong about the call. Found while verifying the member-set chain W-10's second exclusion depends on — not reported by any reviewer). Once C-1.2 holds, the injected tag is always already a declared member, so the call is a no-op. The pin asserts this rather than the code removing it (research.md D-5). Retention is additionally load-bearing for the hand-built-fixture fallback documented at `include/fixpp/dict/table_view.hpp:346-349`. **`plan.md`'s Source Code tree must agree with this clause** — it is the map `/speckit-tasks` reads, so an outlier there becomes a task.

**C-3.4 — Entity-2 completeness is a LOAD-TIME invariant; there is no consumer-side fallback.** *(added Gate A round 1, 2026-07-30 — research.md D-11, spec FR-023.)*

Every group context that `as_table_view()` enumerates and registers MUST have a record in this table. That is enforced in the loaders' `finalize()`, where a violation is a load rejection under the `loader_tolerant_mode.md` disposition — **not** at `as_table_view()`, which is contractually **non-throwing** (established by 072; `spec/behaviors-and-limitations.md` L-063-4: *"Enforced in `LoaderState::finalize()` before any `table_view` is built; `as_table_view()` stays non-throwing"*) and MUST remain so. A lookup miss at the consumer is therefore unreachable by construction.

Where a miss is nonetheless observable — a debug assertion, verification code, a future consumer — it MUST NOT be resolved by a silent fallback. Both candidates reinstate a previously-fixed defect:

| candidate fallback | what it reinstates |
|---|---|
| `group_first_field(no_tag)` | the dictionary-global first-seen delimiter — **the defect this feature removes**, silently restored for exactly the contexts the new table failed to cover, and invisible to FR-012's pin if that pin measures the table rather than the registered store |
| `members.front()` | the lowest-tag member of a tag-sorted set — a **worse** defect, already fixed once (`src/dictionary/dictionary.cpp:498-506`; FIX44 `NoPartyIDs(453)`: lowest member 447, real delimiter 448) and pinned by `tests/wire/validator_production_table_view_test.cpp::ValidatorProductionTableView.GroupDelimiterFromWireNotTagSortedMember` |

**Tolerant-mode interaction** (the question FR-006a leaves open, answered here): tolerant mode operates at **load**, so a tolerantly-skipped group never enters the consumer's enumeration and the invariant holds unchanged in both dispositions. Tolerant mode is not a licence to register a context whose delimiter is unknown (FR-023a).

**Why the invariant is SATISFIABLE — the hybrid-path leg, discharged by measurement rather than by assumption.** `as_table_view()` does not consume a recorded path; it **reconstructs** one, walking upward through `immediate_parent` (`src/dictionary/dictionary.cpp:489-496`, using the map built at `:439-444`). That map is `immediate_parent[fr.tag] = fr.group_no_tag` over the **deduped** `all_fields`, so each hop uses whichever occurrence of that ancestor tag survived. If any tag ever had an occurrence suppressed, the reconstructed chain could be a **hybrid** that no single occurrence ever had — a key the loader never recorded, and hence a C-3.4 violation on a shipped dictionary at load. That is the mechanism that would make FR-023 unsatisfiable rather than merely unreached.

It is discharged: **zero `(msg_type, no_tag)` pairs are declared under more than one ancestor path in any of the ten shipped dictionaries** (orchestrator Gate A round-1 measurement). With no duplicate-tag `FieldRef` there is no suppressed occurrence, so every hop's surviving occurrence *is* that tag's only occurrence, and every reconstructed key equals a key the loader recorded. Note what this rests on: the invariant holds **because of a measurement**, not by pure construction. Hence C-7.3.

**C-3.4a — How the CHECKED SET is computed, and the one leg that is conditional.** *(added Gate A round 2, 2026-07-31 — N17. The argument above discharges the key's **shape**; it says nothing about the **set of keys**, and FR-023's promise is about the set.)*

`as_table_view()`'s enumeration is a filter in a different translation unit at a later time. Its surviving set is, exactly:

```cpp
for (auto const& fr : all_fields) {                              // all_fields = message_fields(mt), tag-deduped
    if (fr.type != field_data_type::NumInGroup) { continue; }    // src/dictionary/dictionary.cpp:446
    …
    if (members.empty()) { continue; }                           // :463 — "plain scalar reuse of the tag"
    …
    tv.set_group_first_ctx(mt, path, no_tag, delim);             // :518
}
```

**Two predicates, not one.** The `!members.empty()` exclusion at `:463` is the one C-3.4 did not mention at all, and by itself it makes the consumer's set a **strict subset** of any naive `<group>`-element enumeration: a tag typed `NumInGroup` that a given message reuses as a plain scalar contributes **no** context for that message.

**The checked set at `finalize()` is therefore defined as follows, and not as "every context the loader recorded".** `finalize()` lives in `LoaderState`, before any `table_view` exists, so it cannot ask `as_table_view()` what it will enumerate; it MUST recompute the consumer's predicate from the loader's own per-message expansion. For each message type `mt` and each group context the loader reached during `mt`'s expansion (C-1.1), the context is **in the checked set** iff both consumer predicates hold on `mt`'s deduped field run: the count tag's `FieldRef` type is `NumInGroup`, **and** at least one `FieldRef` in that run has `group_no_tag == no_tag`. Contexts failing either predicate are **out** of the checked set and are C-3.7 dead data, not violations. Only contexts **in** the checked set must have an Entity-2 record; a missing record for one is the C-3.4 load rejection.

**Where this is conditional, stated rather than asserted.** The above claims the loader can reproduce the consumer's predicate. That claim rests on `fr.type == NumInGroup` meaning the same thing on both sides — and **that is precisely the predicate research.md D-12 shows is not understood**: a `(R,{},146)` entry demonstrably exists on FIX42 while tag 146 is `INT`-typed there, so a context reaches `set_group_first_ctx` by a route the documented gate does not explain. If D-12 lands on **branch (a)** (an unaccounted population path), the recomputed set is a *different* set from the consumer's and this check would pass while FR-023's promise fails — the failure mode C-7.3 exists to catch empirically. **Set equality between the checked set and `as_table_view()`'s enumeration is therefore asserted CONDITIONALLY on D-12 resolving to branch (b)**, and D-12's Phase-1 resolution (research.md D-12, Task) is a precondition of this clause as much as of D-1. On branch (a), D-1/D-3 are amended before Phase 3 and this clause is re-derived against whatever the newly-found path enumerates — it is not weakened, and the check is not enabled against an undefined set.

**C-7.3 — Precondition, alongside `loader_tolerant_mode.md` C-7.1 and C-7.2.** Before the FR-023 check is enabled, confirm all ten shipped dictionaries satisfy the invariant — every context `as_table_view()` registers has an Entity-2 record. **Run this as a set comparison, both directions**, so it also empirically checks C-3.4a's conditional leg: for each dictionary, enumerate what `as_table_view()` actually registers and diff it against the checked set C-3.4a defines. A context registered with no record is the C-3.4 violation; a checked-set member the consumer never registers is C-3.7 dead data and is recorded, not failed. A non-empty first difference on a shipped dictionary is branch (a) of D-12 in a different disguise. Same shape and same reasoning as C-7.1: this is a **new fail-closed load path**, and a fail-closed path must never meet the shipped set for the first time in CI. If a dictionary fails it, that is branch (a) of research.md D-12 arriving in a different disguise, and it is a Phase-3 blocker, not a reason to weaken the check.

**Witnesses**: the FR-006b rejection witness in `tests/dictionary/loader_disposition_test.cpp`, extended with three named cases so the branch is covered rather than landing as a silent uncovered error path (`[const §IX.1]`):

- `LoaderDisposition.ContextWithoutDelimiterRecordRejectedAtFinalize` — constructs a dictionary reaching the invariant and asserts the load is rejected (C-3.4);
- `LoaderDisposition.ContextWithoutDelimiterRecordTolerantModeSkipsGroup` — the tolerant-mode twin, asserting the group is skipped rather than half-registered (FR-023a). *(Named at Gate A round 2: round 1 promised "its tolerant-mode twin" in prose while Project Structure listed no such case, so the FR-023a branch had no artifact.)*
- `LoaderDisposition.ScalarReuseOfGroupTagIsNotACompletenessViolation` — a message reusing a `NumInGroup`-typed tag as a plain scalar contributes **no** context to the consumer (`src/dictionary/dictionary.cpp:463`) and MUST NOT trip C-3.4. This is the C-3.4a `!members.empty()` leg, and without it the checked set is a superset of the consumer's and the fail-closed path rejects a dictionary the consumer is happy with.

**C-3.5 — Enumeration granularity: the consumer is coarser than this table, by construction, and that is a known structural limit.** *(added Gate A round 1, 2026-07-30.)*

`as_table_view()` iterates this message's own expansion — `all_fields = message_fields(mt)`, as its own comment at `src/dictionary/dictionary.cpp:429-431` states, with the group loop at `:445-450`. Inside `append_run`, `src/dictionary/xml_loader.cpp:877-885` sorts that vector by tag and `std::ranges::unique`s it **by tag**, so `message_fields(mt)` holds **exactly one `FieldRef` per tag per message**. Consequently:

- `set_group_first_ctx(mt, path, no_tag, delim)` (`src/dictionary/dictionary.cpp:518`, fed by the delimiter resolved at `:510-511`) executes **at most once per `(mt, no_tag)`**, with `path` reconstructed from the surviving `FieldRef`'s `group_no_tag` plus the `immediate_parent` chain (`src/dictionary/dictionary.cpp:439-444`, itself built from the same deduped vector); *(both citations corrected 2026-07-31, Gate A round 3, N24 — `:508-509` are comment lines and `:437-443` disagreed with the `:439-444` C-3.4 and D-11 already carry)*
- so the runtime store can represent **at most one context per `(msg_type, no_tag)`**, regardless of how finely this table is keyed.
- The sort is `std::ranges::sort`, **not** `stable_sort` — the comment at `src/dictionary/xml_loader.cpp:875` says "Stable sort by tag" and the code below it does not implement one. If a duplicate-tag `FieldRef` ever arose, **which one survives is unspecified** and may differ across standard-library implementations.

**Measured reachability**: zero. No shipped dictionary declares a `(msg_type, no_tag)` pair under more than one ancestor path — 0 in all ten (orchestrator Gate A round-1 measurement, 2026-07-30). So no context is currently lost to this limit, and the unstable sort has nothing to be unstable over. It is recorded because it is a real constraint on the design, because it is **inherited from the 063 store rather than introduced here** (`063/data-model.md:9` reasoned only about the same-parent-path case, which is a different claim), and because a maintainer who later lifts it needs to find this clause rather than rediscover it. Repairing it would reach `message_fields`/`append_run` — 063's store shape, outside this feature's scope and outside D-8's purely-additive constraint.

**C-3.6 — A declared group with zero contexts is not the fail-closed condition.** A group declared in the dictionary but reachable from no message expansion produces no contexts at all, because C-1.1 emits only from the per-message expansion sites. That is **not** an unresolvable delimiter and MUST NOT trip `loader_tolerant_mode.md` C-6.1; its diagnostic is informational (spec FR-006d). Without this clause, C-1.4's per-context predicate and C-6.1's per-declaration predicate disagree — read one way no predicate ever fires and the silent drop survives inside the requirement meant to kill it; read the other way every message-unreachable group in every shipped dictionary trips fail-closed. C-7.1's precondition measurement has a defined answer space only once this is fixed.

**C-3.7 — Records unreachable to the consumer are dead data, not an error.** Given C-3.5, this table may in principle hold a record the consumer can never look up. That is acceptable and requires no diagnostic: the table is keyed for correctness and for the day the C-3.5 limit is lifted (research.md D-3), not for the consumer's current granularity. It is *not* acceptable in the other direction — a context the consumer registers with no record here is the C-3.4 violation.

## Lookup-miss behaviour — unchanged, and load-bearing for tests

The context-keyed accessor falls back to the bare global store on a miss. This is deliberate and MUST NOT change: hand-built test fixtures never populate the context store and depend on the fallback.

**Consequence for any verification code** — a context miss returns the *global* member set, so a miss is indistinguishable from a wrong answer unless discriminated explicitly. Discriminate by comparing the returned span's data pointer against the bare span's. Omitting this inflated the originally reported defect count by 10 contexts.

## Post-conditions (measured baseline → target)

Heading corrected 2026-07-30 (Gate A round 1): the previous heading read `Post-conditions (measured)` while the first row's "After" value rests on **365**, which is a projection. A contract is the document an implementer treats as the acceptance target, so a projected figure under a measurement heading is the wrong kind of error to leave in it.

*(Figures superseded 2026-08-02, Gate B r1 F2/IND-4 — the 335/52/30 row below is the pre-`/implement` scratch-probe projection this file was never repointed to correct; `spec.md:107` and `spec.md:370` record the re-measured, authoritative figures — **330 wrong / 48 polluted / 30 unregistered** — after `spec.md:362-366`'s T012/T013/T014 finding that FIX42 registers **no** group contexts at all, which moves the wrong/polluted cells by exactly FIX42's contribution. See `spec.md:370` for the full reconciliation.)*

| Property | Before | After | provenance |
|---|---|---|---|
| contexts with wrong delimiter | **330 measured** *(was 335 — see note above)* — scored over the contexts that resolve a delimiter today, so excluding the 30 that resolve none | **0 wrong**, over the **365** contexts in the *affected set* | 330 measured; 30 **projected** until the Phase-1 pin measures them (SC-001, SC-015) |
| contexts with polluted member set | **48 measured** *(was 52 — see note above)* | 0 | measured |
| contexts unregistered | **30 measured** | 0 | measured |
| FIX50SP2 registered groups | **502 measured** | 505, matching codegen | measured; delta accounted for by three named groups (FR-017) |

**365 is the size of the affected set, not the context population.** The context population is spec.md's Baseline `contexts` column — 56,246, rising to ~56,276. "0 of 365" therefore reads as "0 wrong, over the 365 contexts in the affected set (335 measured + 30 projected)", never as a rate over the dictionaries.
