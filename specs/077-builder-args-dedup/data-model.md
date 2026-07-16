# Phase 1 Data Model: builder-args-dedup

**Feature**: 077-builder-args-dedup | **Date**: 2026-07-16

> **[SUPERSEDED — v42 builders DESCOPED post-Gate-A]** Where this document lists
> `v42` as a builder-bearing version (e.g. struct-count tables), that is
> superseded: FIX 4.2 types NumInGroup as legacy `INT` ⇒ 0 typed groups
> (L-063-1), so v42 builders are DESCOPED and tracked as issue #196 / L-077-1
> (waiver W-077-1). Delivered builder-bearing versions are
> {v44, v50sp2, vlatest}.

These are **codegen-tool-internal** entities (host-tool build-time only; no
runtime/ABI surface). They extend the existing IR (`ir.hpp`) and emitter state.

## Entity 1 — Recursive structural plan (the dedup key)

The identity a builder group-Args struct is deduplicated by.

| Field | Source | Notes |
|---|---|---|
| `no_tag` | `GroupOrderEntry::no_tag` | the group's `NumInGroup` tag — **part of the dedup key** (see below) AND the `G_<no_tag>` naming prefix |
| `delimiter_tag` | `GroupOrderEntry::delimiter_tag` | first declared member |
| ordered members | `GroupOrderEntry::members` (declaration order) | `(tag, is_group)` in order — NOT tag-sorted |
| per-member required-ness | `MessageIR.fields[tag].ref.rule` | `Required` vs not — serialization-critical, invisible in the Args struct body (all scalars emit as `optional<T>`), lives only in `writer_traits` |
| child plans | recursive | each `is_group` member contributes its own child plan's identity |

**Recursive signature** = `delimiter + [ (tag, required, {child-signature}?) … ]`,
computed recursively — the variant discriminator *within* a `no_tag`.

**Dedup key = `(no_tag, recursive_signature)`.** Two group occurrences are the
**same plan** iff they share the same `no_tag` **and** their recursive
signatures are byte-equal. Including `no_tag` in the key (the recursive
signature by itself excludes the group's own count tag) keeps the
`G_<no_tag>[_ord]Args` naming contract (FR-001 / G1a) always well-defined and
makes the distinct-plan counts mean exactly "# distinct `(no_tag, signature)`
pairs" (Entity 2). Two *different* `no_tag`s whose bodies happen to share a
byte-identical signature therefore stay separate plans (their count tags
differ) — the cross-`no_tag` collision the census did not measure is closed by
construction. This is the unit of deduplication (replaces the current
message-rooted `type_prefix` naming in `resolve_level`, emit_builders.cpp:415).

**Validation**: a plan's child must be fully emitted before the plan references
it (post-order, `[temp.expl.spec]` — existing `collect_levels_postorder`
discipline). Cyclic/over-deep reuse bounded like the read tier's
`kMaxGroupDepth`; never reference an undefined Args type (FR-011).

## Entity 2 — Shared group Args struct (emitted artifact)

One per distinct structural plan per version, in `fixpp::<ns>::groups`.

| Property | Value |
|---|---|
| Name | `G_<no_tag>Args` if the `no_tag` has exactly one plan; if it has two or more plans, no bare name and ALL variants ordinaled `G_<no_tag>_1Args` … `G_<no_tag>_kArgs` |
| Ordinal | first-encounter index over the **bytewise-sorted** `ir.messages` × declaration-order `group_order` (deterministic) |
| Members | scalars → `optional<cpp_type>`; child groups → `span<const G_…Args>` (required) / `optional<span<…>>` (optional) — unchanged shapes, only the referenced *name* changes to the shared one |
| Companions (once per plan) | `writer_traits<G_…Args>` specialization + `_required_<tag>` / `_count_<acc>` / `_validate_entry_<acc>` helpers, in `fixpp::wire` |

**Count per version** — distinct `(no_tag, recursive_signature)` plans over the
builder's real scope (`is_application`-filtered, header/trailer-excluded — the
current v44 emitter's scope; Entity 4). **T005 (2026-07-16) CORRECTED** the
research.md R3 figures (29/89/558/578), which were censused over ALL messages
(app+admin, and for vlatest incl. header componentRefs): the app-scope pins are
**v42 28 · v44 88 · v50sp2 558 · vlatest 576** (−1 `NoMsgTypes`/384 Logon on
v42/v44; v50sp2 unchanged, 0 admin; vlatest −1 NoMsgTypes −1 `HopGrp`/627). Sole
caveat = census-vs-emitter fidelity, pinned EXACTLY by the regenerated golden's
`struct G_` count at `/implement` (T010/T014/T017) — investigate any deviation
before freezing.

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
| FIX 4.2 | v42 | **DEFERRED — no builders** (L-063-1: FIX 4.2 types `NumInGroup` as `INT` ⇒ 0 typed groups ⇒ a scalar-only builder would silently omit required groups; issue #196) | 0 | — |
| FIX 4.4 | v44 | `all` = `is_application` − `{BE,BF,BW,BX,BY}` · `official` = frozen 33 | 83 / 33 | `FIXPP_CODEGEN_V44_FAMILIES` (retained) |
| FIX 5.0 SP2 | v50sp2 | full `is_application` (no v44 exclusion) | 156 | — (all) |
| FIXT.1.1 | vt11 | none (admin-only) | 0 | — (no Builders.hpp) |
| FIX Latest | vlatest | full `is_application`, gated by `FIXPP_CODEGEN_FIX_LATEST` | 173 | — (all) |

`is_application` = `MessageIR::is_application` (from `msgcat`/Orchestra
`category`; ir.hpp:99) is the rule the emitter's own IR uses to build this
in-scope table. **FR-010's completeness census (contracts §C1) MUST NOT read
this field directly** — doing so would re-derive `expected(V)` from the same
`ir(V).messages` walk the emitter consumes (circular; see data-model Entity 6
/ the 075/076 blind-corpus lesson). Instead the census independently
RE-IMPLEMENTS the identical `msgcat`/`category` rule on a standalone raw-XML /
Orchestra walk (`raw_walk(V)`, contracts §C1), evaluated outside `build_ir()`.

## Entity 5 — Builder golden(s)

Checked-in byte-exact deterministic reference output.

Five goldens — one **regenerated** (v44 `official` already exists at the 069
path), four **newly created** (there is NO checked-in `v44_Builders_all` golden
today — 069 verified `all` mode by differential round-trip vs the runtime-XML
path + 8 QuickFIX goldens, not a checked-in all-builder golden; and v42 /
v50sp2 / vlatest are brand-new tiers). New goldens land under this feature's
own `contracts/golden/` dir (plan.md:124). Naming follows the read-tier
convention `<ns>_<Tier>[_variant].golden.hpp`.

| Golden | Status | Path |
|---|---|---|
| v44 `official` | **regenerated** (deduped) | `specs/069-v44-all-families/contracts/golden/v44_Builders_official.golden.hpp` |
| v44 `all` | **new** (no prior `all` golden existed) | `specs/077-builder-args-dedup/contracts/golden/v44_Builders_all.golden.hpp` |
| v42 | **new** | `specs/077-builder-args-dedup/contracts/golden/v42_Builders.golden.hpp` |
| v50sp2 | **new** | `specs/077-builder-args-dedup/contracts/golden/v50sp2_Builders.golden.hpp` |
| vlatest | **new** | `specs/077-builder-args-dedup/contracts/golden/vlatest_Builders.golden.hpp` |

## Entity 6 — Builder-completeness census (FR-010)

Independent, non-circular check per builder-bearing version.

| Property | Value |
|---|---|
| Expected set | `{ m.msg_type : m is application ∧ in-scope(version) }` derived from a **raw-XML / Orchestra walk independent of `emit_builders`** (a standalone parser over `FIX42/44/50SP2.xml` and the `<fixr:repository>`, the app/admin partition read from `msgcat`/`category` at the source, NOT from the emitter's own `ir(V).messages` walk) — mirroring 076's V-1 raw-XML census (N-1). See builder-completeness.md C1. |
| Actual set | `{ msg_type : fixpp::<ns>::build_<Msg> ∧ validate_<Msg> exist }` — proven by a census TU that takes the **address of every expected `build_<Msg>`/`validate_<Msg>`** (compile-time ODR-use ⇒ existence, the entry points FR-010 names); the `builder_registry` text-parse is retained as a **secondary consistency check**, not the completeness measure (builder-completeness.md C2) |
| Assertion | **exact-set equality** (not subset) |
| Red-provable | a **committed test-only mutation seam** (drops one in-scope message from the emitter) makes it fail — a real mechanism, not "documented in the test" (research.md R5, builder-completeness.md C3b) |
| Re-instates | 076's descoped V-2 / V-2b legs at 076's raw-XML independence strength, generalized to all builder-bearing versions |
