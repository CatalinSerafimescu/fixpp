# Phase 1 Data Model: builder-args-dedup

**Feature**: 077-builder-args-dedup | **Date**: 2026-07-16

These are **codegen-tool-internal** entities (host-tool build-time only; no
runtime/ABI surface). They extend the existing IR (`ir.hpp`) and emitter state.

## Entity 1 — Recursive structural plan (the dedup key)

The identity a builder group-Args struct is deduplicated by.

| Field | Source | Notes |
|---|---|---|
| `no_tag` | `GroupOrderEntry::no_tag` | the group's `NumInGroup` tag (naming prefix only) |
| `delimiter_tag` | `GroupOrderEntry::delimiter_tag` | first declared member |
| ordered members | `GroupOrderEntry::members` (declaration order) | `(tag, is_group)` in order — NOT tag-sorted |
| per-member required-ness | `MessageIR.fields[tag].ref.rule` | `Required` vs not — serialization-critical, invisible in the Args struct body (all scalars emit as `optional<T>`), lives only in `writer_traits` |
| child plans | recursive | each `is_group` member contributes its own child plan's identity |

**Signature** = `delimiter + [ (tag, required, {child-signature}?) … ]`, computed
recursively. Two group occurrences are the **same plan** iff their signatures
are byte-equal. This is the unit of deduplication (replaces the current
message-rooted `type_prefix` naming in `resolve_level`, emit_builders.cpp:415).

**Validation**: a plan's child must be fully emitted before the plan references
it (post-order, `[temp.expl.spec]` — existing `collect_levels_postorder`
discipline). Cyclic/over-deep reuse bounded like the read tier's
`kMaxGroupDepth`; never reference an undefined Args type (FR-011).

## Entity 2 — Shared group Args struct (emitted artifact)

One per distinct structural plan per version, in `fixpp::<ns>::groups`.

| Property | Value |
|---|---|
| Name | `G_<no_tag>Args` if the `no_tag` has one plan; `G_<no_tag>_<ordinal>Args` for each additional plan |
| Ordinal | first-encounter index over the **bytewise-sorted** `ir.messages` × declaration-order `group_order` (deterministic) |
| Members | scalars → `optional<cpp_type>`; child groups → `span<const G_…Args>` (required) / `optional<span<…>>` (optional) — unchanged shapes, only the referenced *name* changes to the shared one |
| Companions (once per plan) | `writer_traits<G_…Args>` specialization + `_required_<tag>` / `_count_<acc>` / `_validate_entry_<acc>` helpers, in `fixpp::wire` |

**Count per version** (research.md R3): v42 29 · v44 89 · v50sp2 558 · vlatest 578.

## Entity 3 — Per-message builder / validator (emitted artifact)

Unchanged surface; bodies now reference shared `groups::G_…Args` instead of
message-rooted structs.

| Item | Shape |
|---|---|
| `<Msg>Args` | top-level struct, **per-message** (not deduped), members reference shared child plans |
| `build_<Msg>(out, args)` | serialize; inline recursive body (unchanged codegen except referenced type names) |
| `validate_<Msg>(args)` | thin wrapper over `wire::validate_required<Msg Args>` |
| `builder_registry` | per-version `{msg_type}` array of the in-scope set |

## Entity 4 — Per-version in-scope application set (FR-006)

| Version | ns | in-scope app set | count | families knob |
|---|---|---|---|---|
| FIX 4.2 | v42 | full `is_application` | 39 | — (all) |
| FIX 4.4 | v44 | `all` = `is_application` − `{BE,BF,BW,BX,BY}` · `official` = frozen 33 | 83 / 33 | `FIXPP_CODEGEN_V44_FAMILIES` (retained) |
| FIX 5.0 SP2 | v50sp2 | full `is_application` (no v44 exclusion) | 156 | — (all) |
| FIXT.1.1 | vt11 | none (admin-only) | 0 | — (no Builders.hpp) |
| FIX Latest | vlatest | full `is_application`, gated by `FIXPP_CODEGEN_FIX_LATEST` | 173 | — (all) |

`is_application` = `MessageIR::is_application` (from `msgcat`/Orchestra
`category`; ir.hpp:99) — the independent predicate FR-010 derives its expected
set from.

## Entity 5 — Builder golden(s)

Checked-in byte-exact deterministic reference output.

| Golden | Status | Path |
|---|---|---|
| v44 `official` | **regenerated** (deduped) | `specs/069-v44-all-families/contracts/golden/v44_Builders_official.golden.hpp` |
| v44 `all` | **regenerated** (deduped) | (per 069 layout) |
| v42 | **new** | `specs/077-builder-args-dedup/contracts/golden/` |
| v50sp2 | **new** | ″ |
| vlatest | **new** | ″ |

## Entity 6 — Builder-completeness census (FR-010)

Independent, non-circular check per builder-bearing version.

| Property | Value |
|---|---|
| Expected set | `{ m.msg_type : m.is_application ∧ in-scope(version) }` derived from the IR app predicate (NOT the emitter's own walk) |
| Actual set | `{ msg_type in builder_registry }` parsed from the emitted header/golden |
| Assertion | **exact-set equality** (not subset) |
| Red-provable | dropping one message makes it fail (research.md R5) |
| Re-instates | 076's descoped V-2 / V-2b legs, generalized to all builder-bearing versions |
