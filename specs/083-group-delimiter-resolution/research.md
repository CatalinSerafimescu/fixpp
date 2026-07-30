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

**Decision**: A new per-context delimiter table on `dict_metadata_handle`, keyed `(msg_type, parent_path, no_tag)` — the same key shape `table_view::group_ctx_` already uses — exposed by a new handle accessor and consumed by `as_table_view()` at `dictionary.cpp:510`.

**Rationale**: reuses a key convention already proven in this codebase (`make_group_ctx_key`), so the loader-side record and the table-view lookup cannot disagree about what a context *is*. PMR-allocated on the caller's resource with the rest of the handle's tables. Load-time only — no hot-path allocation, satisfying Article VIII §5.

**Hot-path cost**: none added. `table_view::group_first_field(mt, path, no_tag)` already performs exactly this lookup (`table_view.hpp:349-365`) with the `group_bit` pre-filter short-circuiting group-free traffic. This feature changes *what value is stored*, not how many lookups happen. FR-022's benchmark exists to confirm that empirically rather than by assertion.

**Alternatives considered**: keying by `(msg_type, no_tag)` only — rejected, it fails the spec's edge case of one count tag reused under two different ancestor paths within a single message, which is the exact shape of the defect.

---

## D-4 — Descend at the delimiter

**Decision**: In `consume_group` (`include/fixpp/wire/validator.hpp:357-406`), replace the bare `++i` that consumes the instance-opening delimiter (`:362`) with a query-before-consume: if `delim_tag` is itself a group **in child context**, recurse via `consume_group` and resume one past its extent; otherwise `++i` as today.

**Rationale**: this mirrors the descent the scanner *already* performs for post-delimiter members at `:376`, using the same `can_descend` depth guard and the same `child_path`. It is a symmetry repair, not a new mechanism — which keeps FR-009 (bounded depth) satisfied by the existing K=16 cap rather than by new logic.

**Why it must precede D-1's recursion**: measured — 232 FIX50SP2 contexts have a post-fix delimiter that is a nested group's count tag, plus 30 more once the three silently-dropped groups register. Landing recursive resolution first turns those into false rejections.

**Invariants that must not move** (FR-008): instance-count enforcement at `:402`, required-member masking, and extent termination at `:365`. The delimiter's required-bit is marked *before* descent (`delim_k`, `:354`), and must remain so.

---

## D-5 — Member-set injection becomes redundant, not removed

**Decision**: Leave `set_group_first_ctx`'s call to `add_group_member_ctx` (`table_view.hpp:641-646`) in place. Pin that it has become a no-op.

**Rationale**: a correctly-resolved delimiter is, by construction, a declared member of its own context — it is the first thing the member expansion emits at that level (D-1). Measurement corroborates: `missing == 0` in all ten dictionaries and the only ever-extra tag is the delimiter itself. So correcting the delimiter removes all 52 pollutions without touching the injection.

This is why the issue's fix option (b) is **unnecessary rather than insufficient**. Removing the injection would be a second, independent change with its own regression surface, delivering nothing the delimiter fix does not already deliver. FR-015 follows: member-set exactness is asserted by the *same* pin, not a second one.

---

## D-6 — Typed-read splitter: characterise, then source from the dictionary

**Decision**: FR-021a first — establish by measurement whether `offset_table.cpp:643-680` actually mis-splits. Then source the boundary delimiter from the dictionary's context store rather than from `entries_[first].tag`, and make boundary detection skip nested group extents.

**Rationale**: the splitter reads `std::uint16_t const delim = entries_[first].tag` — *the wire's* first entry after the count — and starts a new instance at every reappearance of that tag within the extent. Two distinct failure modes follow: (a) if the wire's first entry is not the true delimiter the split is wrong from the start; (b) if the same tag legitimately occurs inside a nested group within an instance, the scan splits mid-instance.

**Status honesty**: fixpp#208 recorded this as adjacent and explicitly unverified, and the baseline measurement did not cover it. The user placed it in scope; the first task is therefore evidence, not a fix. If no mis-split is reachable, that negative result is recorded and the change reduces to sourcing the delimiter consistently.

**Open design point for `/speckit-tasks`**: the splitter is reached via `group(no_tag)` and does not obviously carry the ancestor path at that point. Whether the path is available or must be threaded is a task-level investigation, flagged here so it is not discovered mid-implementation.

---

## D-7 — Load disposition: fail-closed default, explicit tolerant opt-in

**Decision**: default = throw `xml_parse_error` naming the group; opt-in tolerant mode = warn and skip. Symmetric in both loaders.

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

**Decision**: expected delimiters come from the independent oracle walk (D-8). A documented **sample** is cross-checked against a third authority: codegen's `walk_level` / `GroupOrderEntry` (`tools/codegen/fixpp-codegen/ir.cpp`), or fixpp#208's tabulated Orchestra values. The pin is demonstrated failing before the fix and the failure counts recorded (FR-014).

**Rationale**: two independent guards against the two ways this pin could be worthless. If the oracle mirrored the fixed loader's logic it would pass by construction and prove nothing — hence the third authority. If it were never observed red it would prove nothing either — hence Phase 1 running fully RED first, with counts recorded.

**Explicitly not treated as coverage** (FR-016): the 78 passing collision-membership cases. Their discriminator comes from `first_tag_only_in`, derived independently of the delimiter, so their green says nothing about delimiter correctness. A sibling feature had to add an `exclude` parameter to that helper specifically so the injected delimiter would not be chosen as a discriminator — direct evidence that these cases route *around* this defect rather than over it.

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

If the scan is deleted and nothing repopulates the global, every one of those reads 0 and the C ABI's `group_begin` rejects **all** groups — a total regression reachable through a frozen ABI, introduced by a change whose stated purpose is to fix rejections.

**Second-order benefit**: because the projection derives from the recursive per-context capture, the three groups that currently project 0 start projecting a real delimiter. So the 502→505 correction reaches the C ABI predicate too, not only the validator — those groups become constructible through `group_begin` for the first time.

**Alternatives considered**: keeping the old scan alive purely to feed the global — rejected, it preserves the drifted second traversal that D-1 exists to eliminate, and would leave the global disagreeing with the per-context table for exactly the eight tags at issue.

---

## Measurement provenance

Baseline figures in spec.md come from `delim_probe3.cpp`, run against `main` @ `0539b56d`, built against `build/linux-clang-debug/lib/libfixpp_dictionary.a`, covering all ten dictionaries. Two probe-fidelity properties matter for reproducing it:

- **Context-miss must be discriminated from wrong-answer.** `group_member_tags(mt, path, no_tag)` falls back to the bare global store on a miss (`table_view.hpp:373-376`), so an unregistered context returns the global set and reads as large-scale pollution. Discriminated exactly by span `.data()` pointer identity against the bare span. Skipping this inflated fixpp#210's headline count by 10.
- **The root-cause split is corroborated, not proven.** Attribution uses "does the runtime delimiter match *some* context's true delimiter". A broken-scan value coincidentally equal to another context's true delimiter would be misfiled. The split reproduces fixpp#208's independently-derived five tags exactly, which is strong corroboration.
