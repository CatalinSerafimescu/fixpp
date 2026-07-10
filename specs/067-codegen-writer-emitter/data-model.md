# Phase 1 Data Model: FR-015a-lite — Codegen Writer-Emitter

**Feature**: 067 · **Date**: 2026-07-10 · Anchors: [research.md](./research.md) R1/R3/R4/R7, [contracts/generated-builder.md](./contracts/generated-builder.md).

This feature has no runtime persistent state. "Data model" here = the shape of the **generated artifacts** and the **runtime validate structures**. All types are compile-time / value types.

## 1. Generated per-message artifacts (in `Builders.hpp`, namespace `fixpp::v44`)

For each of the 33 OFFICIAL messages `<Msg>` (class-identifier via `to_identifier`, R7):

### 1.1 `struct <Msg>Args`
The typed input aggregate (R4). One member per **direct** field (top-level or, for a group's `Args`, that group's members):

| Field kind (`kind_of`) | `<Msg>Args` member type | Presence semantics |
|---|---|---|
| Decimal | `std::optional<fixpp::decimal_t>` | present ⇒ emitted |
| Char | `std::optional<char>` | present ⇒ emitted |
| Int32 / Bool | `std::optional<std::int64_t>` | present ⇒ emitted (Bool encoded as int per existing wire convention) |
| String | `std::optional<std::string_view>` | present ⇒ emitted |
| Skip (DialectExtension) | *(omitted — no member)* | never emitted |
| Repeating group `No<G>` | `std::span<const <G>Args>` | span length ⇒ `No<G>=<len>`; each element ⇒ one instance |

- Member name = `to_accessor(field_name)` (snake_case, collision-uniquified). Group member = `to_accessor(strip_no_prefix(...))` + span.
- Nested groups: `<G>Args` recursively contains a `std::span<const <SubG>Args>`.
- `std::optional`/`std::span` chosen (not owning containers) to keep `<Msg>Args` a non-owning view aggregate — the caller owns storage; the builder never allocates for inputs (mirrors the 061 exemplar params structs, `business_messages.hpp`).

### 1.2 `expected_t<std::span<std::byte>> build_<Msg>(std::span<std::byte> out, const <Msg>Args& args) noexcept`
The generated builder. Body:
1. `wire::body_builder bb{"<msgtype>"};` (multi-char OK, e.g. `"AS"`).
2. **Top-level fields sorted by tag ascending (R1)**: for each present top-level scalar, `bb.field(tag, *args.member)`; for each top-level group at its `No<G>` tag position, open `bb.group_begin(no_tag, delim_tag)`, loop the span emitting each instance's members in **dictionary member order** via `entry->set_*`, recursing for nested groups, then `bb.group_end`.
3. `return bb.commit(out);` — pure serialize; INV-2/3/4/5 enforced by `body_builder`.

**No required-presence check here** (R4 / clarify D2): `build_` is the pure serializer; a caller wanting the fail-closed guarantee calls `validate_<Msg>` first.

### 1.3 `expected_t<void> validate_<Msg>(const <Msg>Args& args) noexcept`
Generated required-presence check (R3), SEPARATE from `build_`. Walks the message's **top-level body required set** and, recursively, each group instance's **`<Group>_rules`** set; returns `wire_required_field_missing` on the first absent required field, else success. Membership test = "is this `optional` engaged / is this tag present in this entry's Args". Group span may be empty (a group with zero required-in-itself instances is allowed; an *entry that exists* must carry its required members).

### 1.4 `writer_traits<<Msg>>` specialization
Binds `<Msg>` (or `<Msg>Args`) to its emitted required-presence tables so a single generic `wire::validate_required<T>(...)` (in `include/fixpp/wire/builder_validate.hpp`) can locate them. Carries: pointer/span to the top-level body required-tag table, and the per-group required-tag tables keyed by `no_tag`, plus the traversal shape (which Args members are groups). (Codegen may instead emit `validate_<Msg>` fully unrolled and make `writer_traits` a thin binding — decided at /implement; the observable contract is §1.3.)

## 2. Generated required-presence tables (R3 — level-scoped, header-excluded)

Emitted in `Builders.hpp` (or a sibling), reusing the `rule_row`-style shape but SCOPED:

### 2.1 Top-level body required set — per message
```
inline constexpr std::array<std::uint16_t, N> <Msg>_required_body = { … };
```
Membership rule (emitter): `{ f.ref.tag : f ∈ m.fields, f.ref.group_no_tag == 0, f.ref.rule == Required, f.ref.tag ∉ {8,9,10,34,35,49,52,56} }`.

### 2.2 Per-group required set — per repeating group
```
inline constexpr std::array<std::uint16_t, K> <Group>_required = { … };   // one per group no_tag
```
Membership rule: `{ f.ref.tag : f ∈ m.fields, f.ref.group_no_tag == <group no_tag>, f.ref.rule == Required }`.
(No header exclusion inside groups — header/framing tags never appear as group members.)

**Provenance**: both tables derive purely from IR `FieldRef.rule` + `FieldRef.group_no_tag` (R2). NOT reused from `Validator.hpp`'s flat header-polluted `<Msg>_rules` (R3).

## 3. Emitter internal structures (compile-time, in `emit_builders.cpp`)

Reused verbatim from `emit_messages.cpp` (R7): `MemberMap` (group_no_tag → deduped members), `GroupPlan`/`plan_dfs` (deps-first nested traversal), `group_cls`/delimiter detection (`NumInGroup`). New: a `kind → body_builder-call` mapping (`Decimal→field(tag,decimal_t)`, `Char→field(tag,char)`, `Int32/Bool→field(tag,int64_t)`, `String→field(tag,string_view)`; group-entry variants use `entry->set_decimal/set_char/set_int/set_string`).

**Delimiter tag** for `group_begin(no_tag, delimiter_tag)` = the group's first member in dictionary member order (`feedback_group_delimiter_from_groupref_not_tagsorted_members`), supplied by the emitter at codegen time (author-supplied contract of `body_builder`; no runtime wire→dict edge).

## 4. Invariants (inherited + new)

| Inv | Source | Statement |
|---|---|---|
| INV-2 | body_builder (061) | body-only; no framing `{8,9,34,49,52,56,10}`. |
| INV-3 | body_builder (061) | canonical decimals. |
| INV-4 | body_builder (061) | fail-closed atomic `commit()`; `out` untouched on error. |
| INV-5 | body_builder (061) | group grammar: count-precedence, non-empty + delimiter-first instances. |
| INV-ORDER | this feature (R1) | top-level tag-ascending; group-entry dictionary member order; instances follow their `No<G>` tag position. |
| INV-VALIDATE | this feature (R3) | required-presence checked at top level (header-excluded) + every group entry; error `wire_required_field_missing`; OFF the serialize path. |

## 5. State transitions

None — builders and validate are pure functions over value inputs. The only "lifecycle" is `body_builder`'s internal open-group LIFO stack, already specified/tested in 061 (`tests/wire/test_body_builder.cpp`).
