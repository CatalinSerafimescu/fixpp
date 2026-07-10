# Phase 1 Data Model: FR-015a-lite — Codegen Writer-Emitter

**Feature**: 067 · **Date**: 2026-07-10 · Anchors: [research.md](./research.md) R1/R3/R4/R7, [contracts/generated-builder.md](./contracts/generated-builder.md).

This feature has no runtime persistent state. "Data model" here = the shape of the **generated artifacts** and the **runtime validate structures**. All types are compile-time / value types.

## 1. Generated per-message artifacts (in `Builders.hpp`, namespace `fixpp::v44`)

For each of the 33 OFFICIAL messages `<Msg>` (class-identifier via `to_identifier`, R7):

### 1.1 `struct <Msg>Args`
The typed input aggregate (R4). One member per **direct** field (top-level or, for a group's `Args`, that group's members):

| Field kind (`kind_of`) | `<Msg>Args` member type | Presence / wire semantics |
|---|---|---|
| Decimal | `std::optional<fixpp::decimal_t>` | present ⇒ emitted |
| Char | `std::optional<char>` | present ⇒ emitted |
| Int32 | `std::optional<std::int64_t>` | present ⇒ emitted via `field(tag, int64_t)` |
| **Bool** | `std::optional<bool>` | present ⇒ emitted via the char overload `field(tag, args.x ? 'Y' : 'N')` — FIX Boolean is `Y`/`N`, NOT `1`/`0` (FR-007a). Never routed through the int64 path. |
| String | `std::optional<std::string_view>` | present ⇒ emitted |
| **Length+Data pair** | ONE `std::optional<std::string_view>` (the Data value) | present ⇒ the emitter auto-derives the `Length` field from the Data and emits both, coupled (FR-007a). NOT two independent optionals. Binary Data with control bytes/SOH is out of scope for v1.0 (string path fail-closes; no bytes API). |
| Skip (DialectExtension) | *(omitted — no member)* | never emitted |
| **Optional** repeating group `No<G>` | `std::optional<std::span<const <G>Args>>` | `std::nullopt` ⇒ `No<G>` omitted entirely; engaged empty span ⇒ `No<G>=0`; engaged N-span ⇒ `No<G>=N` + N instances. Classified optional by the `No<G>` field's dict `rule != Required`. |
| **Required** repeating group `No<G>` | `std::span<const <G>Args>` | span length ⇒ `No<G>=<len>`; `validate_*` rejects `size()==0` (`size() > 0` rule). Classified required by the `No<G>` field's dict `rule == Required`. |

- Member name = `to_accessor(field_name)` (snake_case, collision-uniquified). Group member = `to_accessor(strip_no_prefix(...))` + span.
- Nested groups: `<G>Args` recursively contains a `std::span<const <SubG>Args>` (optional/required per the nested `No<sub>` rule, same as above).
- **Absent vs present-N==0**: modeling optional groups as `std::optional<std::span>` (not a plain span) is what makes "group entirely absent" (`nullopt` ⇒ omit `No<G>`) expressible and distinct from "present-empty" (engaged empty span ⇒ `No<G>=0`) — both behaviors are mandated by spec.md Edge Cases + 061 C3. A plain span collapses the two.
- `std::optional`/`std::span` chosen (not owning containers) to keep `<Msg>Args` a non-owning view aggregate — the caller owns storage; the builder never allocates for inputs (mirrors the 061 exemplar params structs, `business_messages.hpp`).

### 1.2 `expected_t<std::span<std::byte>> build_<Msg>(std::span<std::byte> out, const <Msg>Args& args) noexcept`
The generated builder. Body:
1. `wire::body_builder bb{"<msgtype>"};` (multi-char OK, e.g. `"AS"`).
2. **Top-level fields sorted by tag ascending (R1)**: for each present top-level scalar, `bb.field(tag, *args.member)` (Bool via the `'Y'`/`'N'` char overload; Length+Data as a coupled unit — FR-007a); for each present group (`nullopt` optional group ⇒ skipped entirely), at its `No<G>` tag position open `bb.group_begin(no_tag, delim_tag)`, loop the span emitting each instance's members in **dictionary member order** via `entry->set_*`, recursing for nested groups, then `bb.group_end`. The `delim_tag`, member order, and required set for each group come from THIS message's own occurrence (§3) — NOT a version-wide plan; the same `no_tag` (e.g. `NoMDEntries(268)`) has delimiter 269 in W but 279 in X.
3. `return bb.commit(out);` — pure serialize; INV-2/3/4/5 enforced by `body_builder`.

**No required-presence check here** (R4 / clarify D2): `build_` is the pure serializer; a caller wanting the fail-closed guarantee calls `validate_<Msg>` first.

### 1.3 `expected_t<void> validate_<Msg>(const <Msg>Args& args) noexcept`
Generated required-presence check (R3), SEPARATE from `build_`. Walks the message's **top-level body required set** and, recursively, each group instance's **per-occurrence group required set** (§2.2); returns `wire_required_field_missing` on the first absent required field, else success. Membership test = "is this `optional` engaged / is this tag present in this entry's Args". Presence rules: a **required** group (non-optional span) with `size()==0` is rejected (`size() > 0`); an **optional** group that is `nullopt` or engaged-empty is allowed; any *entry that exists* must carry its required members (per THIS message's occurrence set — not a version-wide table).

### 1.4 `writer_traits<<Msg>>` specialization
Binds `<Msg>` (or `<Msg>Args`) to its emitted required-presence tables so a single generic `wire::validate_required<T>(...)` (in `include/fixpp/wire/builder_validate.hpp`) can locate them. Carries: pointer/span to the top-level body required-tag table, and this message's **per-occurrence** group required-tag tables (one per group occurrence in THIS message — NOT a shared table keyed by `no_tag` alone, since the same `no_tag` differs across messages), plus the traversal shape (which Args members are groups). (Codegen may instead emit `validate_<Msg>` fully unrolled and make `writer_traits` a thin binding — decided at /implement; the observable contract is §1.3.)

## 2. Generated required-presence tables (R3 — level-scoped, header-excluded)

Emitted in `Builders.hpp` (or a sibling), reusing the `rule_row`-style shape but SCOPED:

### 2.1 Top-level body required set — per message
```
inline constexpr std::array<std::uint16_t, N> <Msg>_required_body = { … };
```
Membership rule (emitter): `{ f.ref.tag : f ∈ m.fields, f.ref.group_no_tag == 0, f.ref.rule == Required, f.ref.tag ∉ {8,9,10,34,35,49,52,56} }`.

### 2.2 Per-occurrence group required set — per message, per group occurrence
```
inline constexpr std::array<std::uint16_t, K> <Msg>_<Group>_required = { … };   // one per (message, group no_tag) occurrence
```
Membership rule: `{ f.ref.tag : f ∈ THIS message's m.fields, f.ref.group_no_tag == <group no_tag>, f.ref.rule == Required }`.
(No header exclusion inside groups — header/framing tags never appear as group members.)

**Per-message, NOT per-`no_tag`-global**: the table is emitted per (message, occurrence), keyed like 063's `(msg, parent-path, no_tag)` but resolved at codegen time from static per-message IR (no runtime membership). The same `no_tag` yields a different required set in different messages — e.g. `NoMDEntries(268)` requires `MDEntryType(269)` in W (`FIX44.xml:3023`) but `MDUpdateAction(279)` in X (`FIX44.xml:3060`). A single version-wide `<Group>_required` keyed on `no_tag` alone would over-reject one and under-reject the other. The delimiter and member ORDER used by `build_` (§1.2/§3) are derived the same per-message way.

**Provenance**: both tables derive purely from IR `FieldRef.rule` + `FieldRef.group_no_tag` off THIS message's `m.fields` (R2). NOT reused from `Validator.hpp`'s flat header-polluted `<Msg>_rules` (R3), and NOT from the read emitter's version-wide `MemberMap` union (R7).

## 3. Emitter internal structures (compile-time, in `emit_builders.cpp`)

**Shared verbatim** (type/name helpers only): `kind_of`, `to_accessor`, `to_identifier`, `strip_no_prefix`, `uniquify_accessor` (R7). **NOT reused verbatim**: the read emitter's version-wide `MemberMap` (group_no_tag → union-deduped members, first-encounter-wins across the whole message list) — that is correct for read (membership decided per-context at parse time by 062/063) but unsound for write, which has no runtime scoping. Instead the write emitter builds a **per-message group planner**: for `build_<Msg>` it walks THIS message's own `m.fields`, grouping members by `f.ref.group_no_tag` in field-run order; each group's delimiter, member ORDER, and required set come from that per-message run (nested via the message's own field tree). This is the codegen-time analogue of 063's per-context keying, simpler because it is static per-message IR (no runtime membership, no new IR — `MessageIR.fields` already carries every field's `group_no_tag` + dictionary order + `rule`; verified `ir.hpp:41`, `ir.cpp:98-99`).

New: a `kind → body_builder-call` mapping — `Decimal→field(tag,decimal_t)`, `Char→field(tag,char)`, `Int32→field(tag,int64_t)`, **`Bool→field(tag, x?'Y':'N')` (char overload, FR-007a)**, `String→field(tag,string_view)`, **Length+Data pair→ auto-derive length + string path (FR-007a)**; group-entry variants use `entry->set_decimal/set_char/set_int/set_string` (Bool via `set_char('Y'/'N')`).

**Delimiter tag** for `group_begin(no_tag, delimiter_tag)` = the group's first member in THIS message's dictionary member order (`feedback_group_delimiter_from_groupref_not_tagsorted_members`), supplied by the emitter at codegen time (author-supplied contract of `body_builder`; no runtime wire→dict edge). Because it is per-message, W's `NoMDEntries` delimiter is 269 and X's is 279 from the same `no_tag`.

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
