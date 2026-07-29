# Phase 1 Data Model — Strict-Validation-Path Residual Closeout

No new persistent or wire entities. This feature reshapes two in-memory validation structures.

## E-1 — FIXT.1.1 standard framing `tag → field_type` table (Concern A, new)

A static, immutable table of the FIX tags the FIXT.1.1 session layer owns **and each tag's structural datatype**: the `<header>` + `<trailer>` field tags declared in `dictionaries/FIXT11.xml` (8/9/34/49/52/56 and the full session-header set incl. e.g. ApplVerID(1128)/ApplExtID(1156), plus trailer 10/CheckSum). Tags 8/9/10 are **legitimately part of the framing set and reach Step-1** (they are not framer-stripped from the validated bytes; tag 8 is the pre-fix reject site), so the exact-set census (see **Source of truth** below) mandates them — they are not optional or "harmless if included".

- **Shape**: compile-time constant (e.g. a sorted `std::array<{std::uint16_t tag, field_type type}, N>` / span) in the dictionary layer.
- **Source of truth**: `dictionaries/FIXT11.xml` `<header>`+`<trailer>` field tags **and** their datatypes (reduced through the canonical `field_data_type → field_type` mapping, `field_type.hpp`). The constant is **pinned exact-set-equal** to it by a census test — tags AND datatypes, both directions — the constant is a materialization, FIXT11.xml is authoritative. FIXT.1.1 being a frozen protocol, the set never changes.
  - **Nested `<header>` group `NoHops` — INCLUDE (flat), disposition (Concern A, per D-2 "entire header+trailer tag set" intent + SC-003 no-false-reject).** The FIXT.1.1 `<header>` is not a flat field list: it ends with a nested `<group name='NoHops' required='N'>` (`FIXT11.xml:32–35`) whose count/member tags are legitimate header tags a conforming routed FIXT app frame may carry. The census walk therefore **recurses one level** into the `<header>`/`<trailer>` nested groups and the framing set INCLUDES the hop tags **flat** with their datatypes — `627 NoHops` (NUMINGROUP→**Int**), `628 HopCompID` (STRING→**String**), `629 HopSendingTime` (UTCTIMESTAMP→**String**), `630 HopRefID` (SEQNUM→**Int**). Accept-only semantics: these tags pass the Step-1 gate and are type-checked (627/630 malformed-numeric → Int arm rejects); the validator does **not** structurally validate the hop group. Excluding them would leave a residual false-reject of routed FIXT traffic — the exact SC-003 defect class this feature targets. The census (E-1 test) asserts them in-set both directions, so the disposition is CI-pinned, not implicit.
- **Consumed by**: `Dictionary::as_table_view()` — for versions {v50, v50sp1, v50sp2}, populates the **validator-private** `fixt_framing_tags_` set and `fixt_framing_types_` map (E-2). Read ONLY by the validator.
- **Not** attached to `message_fields()` / `field_ref()` / the shared `valid_`/`types_` stores (would change read goldens or parser behavior) — validator-private surface only.

## E-2 — Validator-private FIXT framing surface on `table_view` (Concern A, new)

Two **new validator-read-only members** on `table_view`, populated in `as_table_view()` for {v50, v50sp1, v50sp2} ONLY:

- `fixt_framing_tags_` — the FIXT framing tag set; consulted by the validator's Step-1 gate (`validator.hpp:170`) via a new `is_fixt_framing_tag(tag)` accessor, IN ADDITION to `valid_tags_for(msg_type)`.
- `fixt_framing_types_` — the framing `tag → field_type` map; consulted by the validator's type-check arm (`check_field_type` → `field_type_of`, `validator.hpp:467`) for a framing tag **before** falling back to `field_type_of`, so a malformed numeric header (`34=abc`, `1156=abc`) hits the Int arm instead of the String default.

- **Before**: for FIX50SPx, `validate()` Step-1 sees only message body fields (empty header/trailer) → misses 34/49/52/56/10 → rejects on the first header tag.
- **After**: framing tags pass Step-1 via `is_fixt_framing_tag`; their type is resolved from `fixt_framing_types_`. Other versions unchanged.
- **Load-bearing invariant (RC#1 / FR-009 / FR-010)**: the shared `valid_` store, `field_valid_for` (`table_view.hpp:259–263`), `valid_tags_for` (`table_view.hpp:273–276`), `types_`, and `field_type_of` (`table_view.hpp:398–401`) are **byte-identical** — the framing surface is separate. The inbound parser's `unknown_fields()` classification (`parser.hpp:582–584` → `field_valid_for`) is therefore unchanged **whether strict validation is on or off**. `message_fields(mt)` (golden/codegen source) is likewise untouched. This invariant is **pinned directly** (not by a blind on-vs-off `unknown_fields()` compare, which is near-vacuous since `inbound_tv_` is built flag-independently at `session.cpp:992`): `field_valid_for(msg_type, T)` / `valid_tags_for(msg_type).contains(T)` stay **false** for each framing tag `T` while `validate()` accepts it — a `valid_` re-widening flips `field_valid_for` to `true` → RED.

## E-3 — Per-group required-member store (Concern B, narrowed)

The 079 structure recording which members are required within a group context. Two backings in `Dictionary`: bare `group_required_members_impl` (`dictionary.cpp:180`) and context-scoped `msg_group_required_pairs_impl` (:202); surfaced into `table_view` via `add_group_required_member` (:416–418) and `add_group_required_member_ctx` (:475–522); consumed by `consume_group` (`validator.hpp:264`).

- **Populated by**: loader `expand_field_list` recording `(enclosing_group_no_tag, member_tag)` pairs.
- **Before (required-once-present)**: a `required='Y'` direct member of ANY present group (optional or required) is recorded → over-requires at 24 contexts.
- **After (group-gated)**: a `required='Y'` direct member is recorded **only if its immediate enclosing group is required** — QuickFIX `addXMLGroup`'s `required=="Y" && groupRequired` rule, where `groupRequired` is the immediate enclosing group's own `required=` (NOT an AND across ancestor groups; decided at design time, D-3). The reworked oracle encodes this independently from raw XML; the parity golden corroborates.
- **Invariant**: `consume_group` logic is unchanged — it enforces exactly what the store lists; the fail-closed dynamic-width mask stays.

## E-4 — Typed group-plan identity forked by enclosing-group-required (Concern B, narrowed)

Codegen `emit_builders.cpp` interns group entry plans by `(no_tag, delimiter, structural-signature)` (`:648–649`) and emits one `writer_traits<G_X>::required_checks` per interned plan (`emit_writer_traits_for_level`, `:723`). `compute_signature` (`:251–282`) folds each member's own `required` (line 269) and each nested group's `group_required` (line 261) but **NOT** the enclosing-usage required-ness — so a mixed-usage group shares one trait and one shared trait cannot both enforce (required usage) and skip (optional usage). (RC#2/F3.)

- **After**: include the effective **enclosing-group-required-ness in the plan interning identity / signature**, so a structural group used both required and optional forks into two plans — a `required` fork (populated `required_checks`, `group_check.required=true`) and an `optional` fork (empty `required_checks`, `group_check.required=false`). Present nested groups inside the optional fork still validate via their own forks (each carrying its own `group_required` already at signature line 261). No `builder_validate.hpp` change.
- **Materialization**: regenerated typed-**validator** goldens (v44/v50sp2/vlatest) under `specs/078-.../contracts/golden/`.
- **Bounded impact**: forks only structural groups used in both a required and an optional context with `required='Y'` members (measure the plan-count delta at /implement).
- **Invariant**: agrees with E-3 (runtime tier) at every affected site (FR-007); identity fork affects **validator emission only** — read/reify goldens byte-identical.

## State / transitions

None. All structures are built once at dictionary load / view construction and are immutable thereafter. No lifecycle.
