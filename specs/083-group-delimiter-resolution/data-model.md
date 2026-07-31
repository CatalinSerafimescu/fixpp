# Phase 1 Data Model — Group Delimiter Resolution

**Feature**: `083-group-delimiter-resolution` | **Date**: 2026-07-30

Exactly one new persistent structure is introduced (Entity 2). Everything else is an existing entity whose *population rule* changes.

---

## Entity 1 — Group context key *(existing, unchanged shape)*

The coordinate at which delimiters and member sets must be exact.

| Field | Type | Meaning |
|---|---|---|
| `msg_type` | string | the message this occurrence belongs to |
| `parent_path` | sequence of count tags | ancestor groups, outermost first, **excluding** this group's own count tag |
| `no_tag` | count tag | this group's own `NumInGroup` tag |

**Invariant**: `parent_path` excludes `no_tag`. This convention is already shared by `table_view`'s context store and the test oracle's `GroupContextKey`; the new table adopts it unchanged so the three cannot disagree about what a context is.

**Why the path is required, not just `msg_type`** *(rationale corrected 2026-07-30, Gate A round 1)*: **not** because one count tag occurs under two ancestor paths in a single message — that shape is measured at **zero occurrences in all ten shipped dictionaries**, and the runtime could not represent it anyway (Entity 2's note on consumer granularity). The reasons are:

1. **Key-shape agreement with the consumer.** `table_view::group_ctx_` is path-keyed (`make_group_ctx_key(msg_type, parent_path, no_tag)`). A coarser table would need a lossy projection at every lookup, and that projection is where a consumer-side miss is manufactured (research.md D-11).
2. **The path is in hand at capture time.** The loader threads it through the document-order walk anyway (research.md D-2); discarding it would mean synthesising a key the walk does not produce.
3. **The limit that makes the finer key currently redundant lives at the consumer, not here** — so when it is lifted (#196/082, or a `message_fields` fix), this table already carries correct data.

See research.md D-3 for the full rewritten rationale.

---

## Entity 2 — Per-context delimiter table *(NEW)*

The feature's only new runtime structure.

| Field | Type | Meaning |
|---|---|---|
| key | Entity 1 | the context |
| `delimiter` | count/field tag | that context's document-order first member |

**Population**: written once at load, from the first `FieldRef` emitted at the group's level during message expansion (research.md D-1). Never mutated after load.

**Lifetime / allocation**: PMR-allocated on the caller's `memory_resource` alongside the metadata handle's other tables. Load-time only — no allocation on any hot path.

**Validation rules**:

- `delimiter != 0` for every registered context. A context that would record `0` is the FR-006 fail-closed condition, not a storable state.
- `delimiter` MUST be a member of that context's own member set. This is guaranteed by construction (it is the first member emitted) and is what makes FR-015 true — the delimiter pin *implies* member-set exactness rather than needing a second assertion.
- The table MUST be populated only from the per-message expansion call sites. Component- and group-cache expansions are not message-scoped (research.md D-2) and must contribute nothing.
- **Completeness with respect to the consumer** *(added Gate A round 1; spec FR-023, `contracts/group_ctx_delims.md` C-3.4)*: every context `Dictionary::as_table_view()` enumerates and registers MUST have a record here. This is enforced as a **load-time invariant in the loaders' `finalize()`**, where a violation is a load rejection under the FR-006 disposition — **not** at `as_table_view()`, which is contractually non-throwing (072, L-063-4) and stays so. There is therefore no consumer-side lookup-miss state, and **no silent fallback exists**: falling back to `group_first_field(no_tag)` would reinstate the defect this feature removes, and to `members.front()` a worse one already fixed and pinned (FIX44 `NoPartyIDs(453)`: lowest member 447, real delimiter 448).

**Consumer granularity — this table is finer than what consumes it, deliberately** *(added Gate A round 1; C-3.5)*: `as_table_view()` iterates `message_fields(mt)`, which is sorted and `unique`d **by tag** (`src/dictionary/xml_loader.cpp:877-885`), so the runtime store can hold at most one context per `(msg_type, no_tag)` however finely this table is keyed — and the sort is `std::ranges::sort`, not `stable_sort`, so a duplicate's survivor would be unspecified. Measured reachability of that shape: **zero pairs in all ten dictionaries**, so nothing is currently lost and the unstable sort has nothing to be unstable over. Records this table holds that the consumer cannot reach are **dead data, not an error** (C-3.7); the reverse — a registered context with no record — is the C-3.4 violation.

**Relationship to the existing global `GroupDef.first_field_tag`**: superseded as the *source of truth*, but **not deleted and not left unpopulated**. The one-level component scan that produces it is deleted; the global field itself is **retained and repopulated as a first-seen projection of this table** (research.md D-10) — this is not a task-level detail, because the global doubles as an *is-this-tag-a-group* predicate at C-ABI construction sites and at 072's load-time collision guard, and leaving it at 0 would make `group_begin` reject **all** groups through a GA-frozen ABI. It must no longer be the source for `set_group_first_ctx`.

---

## Entity 3 — Group declaration record *(existing, population rule changes)*

The loader's per-group record, currently deduplicated by count tag with **first-seen-wins**. That deduplication is the structural cause of context divergence: the first message read decides the delimiter for every other message.

**Change**: the record stops being the delimiter authority. It may remain the carrier of group *structure*, but delimiter resolution reads Entity 2. The one-level component scan is removed rather than fixed in place (research.md D-1).

---

## Entity 4 — Context member set *(existing, becomes exact)*

Tags a context's declaration declares as direct members, used to find where an instance ends.

**Change**: no code change. It becomes exact as a consequence of Entity 2 being correct — the injected delimiter that polluted 52 contexts is now always already a declared member, so the injection is a no-op (research.md D-5).

**State transition** (the whole feature, expressed as one table):

| | delimiter | member set |
|---|---|---|
| before | global, first-seen — wrong in 335 contexts | polluted in 52 |
| after | per-context, document order | exact everywhere |

---

## Entity 5 — Independent document-order walk *(test-only, NEW field)*

The oracle that produces expected values for the pin, sharing no code with the loaders.

**Change**: gains a **new** per-context delimiter map. The existing member-set map is `std::map<key, std::set<tag>>` — a `set`, which cannot express order, which is precisely why no delimiter was ever pinned.

**Constraint (research.md D-8)**: the extension MUST be additive. The existing member-set map is consumed by census pins on a parked, currently-unbuildable sibling branch; reshaping it would break pins whose breakage cannot be observed until that branch resumes.

---

## Entity 6 — Group instance boundary *(existing, two producers must converge)*

Where one instance of a repeating group ends and the next begins.

Today there are **two** producers using **different** rules:

| Producer | Delimiter source | Consequence |
|---|---|---|
| inbound validation | the dictionary | correct once Entity 2 is correct |
| typed-read splitter | **the wire** — the first entry after the count | can disagree with validation |

**Change (FR-021)**: both derive the boundary from Entity 2, and boundary detection must skip nested group extents so a delimiter tag recurring at depth cannot split an instance in two.

**Surface — specified, not deferred** *(Gate A round 1; key and census corrected Gate A round 2, 2026-07-31)*: `contracts/typed_read_splitter.md`. The splitter reaches Entity 2 through a `group_delim_fn_t` sibling of `OffsetTable`'s existing `group_member_fn_t` callback (`include/fixpp/wire/offset_table.hpp:78-79`), keyed by **`stored_group_context()`**'s path (`include/fixpp/wire/offset_table.hpp:327`, seeded by `set_group_context`, `:193-204`) — the **same key** the validator descent uses, which is what makes the FR-021b agreement structural rather than coincidental. **Not `group_context_for(no_tag)`**: that returns `stored_group_context().pushed(no_tag)` (`src/wire/offset_table.cpp:424-426`) and `pushed()` appends `no_tag` to `parent_path` (`include/fixpp/wire/group_view.hpp:56-62`), so it would query `(msg_type, parent_path + no_tag, no_tag)` — one element too long and a violation of this entity's own "excludes `no_tag`" invariant (Entity 1). Round 1's summary here named that accessor; the contract (C-8.2) was always right and this line now matches it.

Only `src/wire/offset_table.cpp:656` changes its delimiter **source**. `:454`, `:526` and `:597` keep their **membership-probe** role wire-derived (FR-021c, C-8.0) — but `:454` and `:526` each also carry an **instance-boundary rule** from the same local (`:473`/`:476`/`:496-499` and `:559-570`). `:526`'s is scoped out on its own grounds (C-8.0b). `:597`'s exposure — it sizes a reservation in the fixed 16 KiB arena whose under-reserve mode is a documented silent truncation (L-073-1 / L-065-2) — is assessed rather than assumed benign.

**A THIRD producer, and the one that has to move first** *(added Gate A round 3, 2026-07-31 — user scope amendment on N23; FR-021e / C-8.0c)*. The table above is incomplete as round 2 wrote it: **before** either producer splits anything, `consume_group_extent` (`:438-503`) decides how far the group *reaches*, and the splitter is bounded by its answer (`:648` ← `:550`). That walk descends into nested extents for post-delimiter members (`:485-488`) but consumes the instance-opening delimiter with a bare `++k` (`:475`), so when the delimiter **is** a nested group's count tag it truncates the extent to one instance:

| Producer | Delimiter source | Nesting-aware? | Disposition |
|---|---|---|---|
| inbound validation (`validator.hpp`) | dictionary | yes at both positions **after C-4.1** | Phase 2 |
| offset-table **extent walk** (`:454`) | wire (unchanged) | post-delimiter only → **both positions after C-8.0c** | Phase 4 — **repaired**, was scoped out at round 2 |
| typed-read **splitter** (`:656`) | **wire → dictionary** (C-8.1/C-8.2) | yes (C-8.5) | Phase 4 |
| `group()` cap loop (`:561`) | wire | no — flat | out of scope (C-8.0b); feeds a DoS cap only |

**A correct split of a truncated extent is still wrong**, which is why FR-021b's agreement invariant below could not have held on the 262 contexts whose post-fix delimiter is a nested group's count tag until C-8.0c landed.

**Invariant to pin (FR-021b)**: a message that validates as N instances reads back as N instances, for every affected context — **and on the same boundaries**, since two different splits can yield the same count. Witness `TypedReadSplitAgreement.ValidatedInstanceCountEqualsTypedReadInstanceCount`, whose fixture must be a **divergent** context under a **non-empty parent path** (C-8.2 / W-9): the context-keyed accessor falls back to the bare global on a miss (`include/fixpp/dict/table_view.hpp:349-365`), so a root-level or non-divergent fixture would pass even with a key off by one path element. **W-9 runs over two fixture shapes, not one** *(round 3)*: mode **(b)**, the delimiter tag reappearing at greater depth, and mode **(c)**, the delimiter *being* a nested group's count tag. Round 2 mandated (b) alone, which is exactly the shape the extent walk already handled — so W-9 was structurally incapable of detecting the C-8.0c defect. Mode (c)'s extent is pinned separately and directly by **W-10a** (`TypedReadSplitAgreement.ExtentWalkDescendsAtNestedGroupDelimiter`, SC-016), and **W-10's own fixture is now constrained to exclude mode (c)**, since as round 2 wrote it that pin asserted the truncated extent was correct.

---

## Entity 7 — Construction-side delimiter check *(existing, gains context)*

The C-ABI builder's commit-time check that each group instance opens with the delimiter — `validate_group_grammar`, file-static at `src/capi/message_write.cpp:710-730`, whose delimiter leg at `:719` reads the dictionary-**global** `group_first_field(e.tag)` today.

**Change (FR-018)**: resolves via Entity 2 instead of a context-free lookup. The walk it performs is already recursive over nested instances (`:724-726`) and already has the message type available at both commit sites (`:762`, `:781`), so it carries the ancestor path down that existing recursion. **Contract: `contracts/capi_group_grammar.md`** *(added Gate A round 1 — this surface previously had a research paragraph, no contract and no test artifact, while carrying a disclosed behaviour change through a GA-frozen ABI).*

**Route to Entity 2 (FR-018b / C-9.2a)** *(added Gate A round 2, 2026-07-31)*: the check holds a `Dictionary`, whose only `group_first_field` is the bare overload (`include/fixpp/dict/dictionary.hpp:109-111`); the context-keyed one is on `table_view` (`include/fixpp/dict/table_view.hpp:349-365`). Calling `dict->as_table_view()` at the check is **barred by `[const §XV.1]`** (`include/fixpp/dict/dictionary.hpp:210-212`, `src/dictionary/dictionary.cpp:357-358` — config-time construction only, never the per-message hot path), and the check runs inside `fixpp_msg_commit` (`src/capi/message_write.cpp:755`). The route is instead a **session-owned `table_view` built once at `fixpp_session_open`**, beside the `dict_` the session caches there (assignment at `src/capi/session.cpp:109-111`, into the member declared at `src/capi/capi_internal.hpp:493`), with a non-owning pointer copied into the message handle exactly as `dict_` is copied at `src/capi/message_write.cpp:289-291` (into the member declared and documented at `capi_internal.hpp:261-266`) — the same pattern `Session::inbound_tv_` (`src/session/session.cpp:992`) and `fixpp_msg::owned_tv_` (`capi_internal.hpp:285`) already use. Both are internal structs, so no exported surface moves. *(Copy-site citations added 2026-07-31, Gate A round 3, Codex #1 — round 2 cited the destination member's declaration for the copy itself.)*

**Not converted (C-9.5)**: the three sites that use the same global as an *is-this-a-group* **predicate** — `src/capi/message_write.cpp:157`, `:812`, `:923` — keep the bare query, answered by D-10's repopulated projection. Making them context-keyed would reject a group whose context the caller has not yet established, i.e. every group, through the frozen ABI.

**Hard constraint (FR-018a)**: no exported C ABI signature changes — the ABI is GA-frozen at 1.5.0. The affected function is file-static; this is a behaviour change reachable through the frozen surface, disclosed via FR-019, not a surface change. The disclosure must **enumerate** the five groups whose delimiter moves, with old and new opening tag (C-9.6), because that enumeration is what lets the SC-007 audit tell an intended new rejection from a regression. Witnesses W-11/W-11a/W-11b/W-12/W-13 in `tests/capi/capi_group_delimiter_ctx_test.cpp` — W-11a is the only one that distinguishes the FR-018b route from the constitutionally-barred per-commit rebuild, which is behaviourally identical.
