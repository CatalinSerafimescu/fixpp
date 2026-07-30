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

**Why the path is required, not just `msg_type`**: one count tag may occur under two different ancestor paths in a single message. Keying on `(msg_type, no_tag)` alone reintroduces the same class of collision this feature exists to remove.

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

**Relationship to the existing global `GroupDef.first_field_tag`**: superseded for context-aware consumers. The one-level component scan that produces it is deleted; whether the global field itself is retained for the legacy bare-store path is a task-level detail, but it must no longer be the source for `set_group_first_ctx`.

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

**Invariant to pin (FR-021b)**: a message that validates as N instances reads back as N instances, for every affected context.

---

## Entity 7 — Construction-side delimiter check *(existing, gains context)*

The C-ABI builder's commit-time check that each group instance opens with the delimiter.

**Change (FR-018)**: resolves via Entity 2 instead of a context-free lookup. The walk it performs is already recursive over nested instances and already has the message type available, so it carries the ancestor path down that existing recursion.

**Hard constraint (FR-018a)**: no exported C ABI signature changes — the ABI is GA-frozen at 1.5.0. The affected function is file-static; this is a behaviour change reachable through the frozen surface, disclosed via FR-019, not a surface change.
