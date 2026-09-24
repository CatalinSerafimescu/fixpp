# Research — 091 data-field-bytes (fixpp#418)

Every measurement below was taken on 2026-09-24 against `48589054` (origin/main) or the branch
head whose production code is byte-identical to it. Each carries its **recipe** so it can be
re-derived. Per the repo rule, re-derive before relying on a figure.

---

## Superseded input

`.specify/418-data-field-bytes.md` (v1, 2026-09-11, branch `fix/418-data-field-bytes`,
commit `68c8c769`) is **input only, not authority**:
- **Rejected at scoped Gate A round 1** (#418 comment 2026-09-12): its `field_bytes(tag, span)` /
  `set_bytes(tag, span)` raw setters defeat the injection guard.
- **§4 is stale.** It cites `fixpp_msg_set_bytes`/`set_string` as C-ABI precedent. #428 (C-ABI 1.6)
  has since shipped `fixpp_msg_set_data` plus a commit-time pair refusal, and that is the precedent
  now.
- **§3's "vlatest 0" is stale**, because #427 made the Orchestra loader keep `lengthId` (R-5).

What carries over: the two-stage witness idea, the mutant that re-applies the content guard, and
L-067-2's move to the closed file.

---

## R-1 — API shape: a Data-tag-keyed atomic operation, named like the C-ABI twin

**Decision.** `body_builder::field_data(std::uint16_t data_tag, std::span<const std::byte> value)`
and `entry_handle::set_data(std::uint16_t data_tag, std::span<const std::byte> value)`. The caller
names the **Data** tag only. The Length tag comes from the builder's `dict_hooks`, and the Length
value is the decimal octet count.

**Rationale.**
- It mirrors `fixpp_msg_set_data` / `fixpp_entry_set_data` (#428) exactly: the Data tag is the key,
  the Length is derived, and an empty value is refused. One mental model serves both surfaces.
- The caller cannot supply a Length at all, so "Length disagrees with the value" is unreachable on
  this path by construction.
- `set_data` sits next to `set_string` / `set_int` / `set_decimal`, matching the "type in the name"
  discipline.

**Alternatives rejected.**
- `field_bytes(tag, span)` / `set_bytes(tag, span)`: rejected at Gate A r1 (the injection hole).
- `field_pair(length_tag, data_tag, span)`: lets the caller name a mismatched pair, which is one
  more refusal to write and test for no gain.
- A `std::string_view` overload: a string_view can hold any octet. But two overloads with different
  guards, sitting next to `field(tag, string_view)`, is exactly the confusion that makes a reviewer
  miss which path skips the content check. One span entry point keeps the "arbitrary bytes" path
  lexically distinct. Callers convert with `std::as_bytes(std::span{sv})`.

## R-2 — Pair lookup: `dict_hooks` stored in `body_builder`, default `none()`

**Decision.** The constructor becomes
`explicit body_builder(std::string_view msg_type, dict_hooks hooks = dict_hooks::none()) noexcept`.
`dict_hooks` is trivially copyable (a `static_assert` in `dict_hooks.hpp`) and is stored by value.
`field_data`, `set_data` and the commit check (R-4) all read the **same** stored value, so a pair
accepted when the field is set is never refused at commit (FR-009).

**Rationale.**
- FR-009 (owner decision): honour dictionary pairs with B-426-3 precedence.
- `dict_hooks::length_tag_for_data` already implements that precedence (standard table first, a
  dictionary pair only when neither tag is standard). No new pair code is needed (O-4 of
  `.specify/426-428-length-data-pairs.md`).
- `dict_hooks` lives in the wire layer, so `body_builder` gains no dictionary include.
  `for_table_view` is defined in `parser.hpp`, and only a caller that already holds a `table_view`
  reaches it.

**Generated builders pass nothing** and get `none()`, the standard table. This is sufficient,
measured: `fixpp-codegen` is not an installed tool. It runs at configure time over the shipped
dictionaries only (`docs/src/dictionary/codegen.md`, "the generated headers are produced at
configure time"). The standard table is by construction the union of those dictionaries' pairs
(`LengthDataPairs.HeaderEqualsShippedDictionaryUnion`). Every pair a generated builder can emit is
therefore standard. R-7's compile-time assertion witnesses this per call site, so a future custom
dictionary run through codegen fails loudly instead of silently emitting an unknown pair.

**Alternatives rejected.**
- Standard table only: rejected by the owner (FR-009). The C-ABI would then accept a pair the C++
  builder refuses.
- A per-call `dict_hooks` parameter: lets set and commit disagree.
- A `table_view const&` parameter: puts a dictionary edge on `body_builder`, which its header
  forbids ("no wire→dictionary edge").

## R-3 — Atomicity and refusal order

**Decision.** `field_data` / `set_data` do everything in this order:
1. **Handle checks** (`set_data` only): the handle has an owner and the entry is the innermost open
   one. Otherwise `wire_invalid_field_format`, the same as `set_string`.
2. `is_framing_tag(data_tag)` → `wire_field_value_out_of_range`.
3. `length_tag = hooks_.length_tag_for_data(data_tag)`. If it is 0 → `wire_unexpected_tag`.
4. `is_framing_tag(length_tag)` → `wire_field_value_out_of_range`. This can only happen with a
   dictionary pair, as in the C-ABI's B-428-3.
5. `value.empty()` → `wire_field_value_out_of_range`.
6. Append the Length node, then the Data node, into the same container.

If the second append throws `bad_alloc`, the Length node is popped as well. The result is **both
nodes or neither**: the container's size after a failure equals its size before (INV-4). Every
refusal in steps 1–5 happens before any append.

**Rationale.** FR-004 / FR-004a / FR-006. The ordering puts all refusals first, so a single rollback
path covers only arena exhaustion.

## R-4 — Commit-time pair check (FR-008): fold into the existing INV-5 tree walk

**Decision.** `commit()` already walks the whole accumulator tree once (`validate_group_grammar`,
INV-5). That walk is extended: for each container (the top-level `entries_`, and each
`group_instance::fields`) it feeds every node, in order, to a `wire::length_data_checker` built from
`hooks_`, then calls `finish()`.
- A scalar node is fed as `observe(tag, value_bytes)`.
- A group node is fed as `observe(no_tag, {})`. A NumInGroup tag is never a pair tag, so it passes,
  unless a Length is waiting, in which case adjacency is broken and the check correctly fails.

Any failure → `wire_invalid_field_format`, `out` untouched.

**Rationale.**
- FR-008 (owner decision): refuse at commit. Reuse `length_data_checker` (O-4), which makes this its
  second caller.
- The walk already exists, so the added cost per scalar field is two constexpr bit tests
  (`standard_pair_tag_bit` inside `data_tag_for_length` / `length_tag_for_data`) and nothing else
  when neither hits. That is the minimum any correct check can cost, which puts SC-005's 3 %
  within reach without a special fast path.
- The recursion depth is the one the INV-5 walk already uses. No new stack shape.

**Alternative rejected.** A separate second walk: twice the tree traversal for no benefit.

**Consequence to disclose (B&L).** A hand-written C++ caller that today emits a malformed pair
through `field()` gets `wire_invalid_field_format` at commit, where before it produced a malformed
frame. A literal-tag grep,
`git grep -n "field(\(90\|91\|95\|96\|212\|213\|354\|355\)\b" -- src tests bench bindings`, finds
**no C++ `body_builder` caller that writes one of those pairs with a literal tag number**. Every hit
is in the C-ABI test file `tests/capi/length_data_setters_test.cpp`, which is a separate code path.
⚠️ The grep cannot see a caller that uses a named constant or a computed tag, so it is **not** a
blast-radius proof. The real measurement is the full ctest run once the commit check lands: any
newly failing test is a caller the grep missed. This is still a behaviour change for external C++ callers, so it gets a
B-091-* row.

## R-5 — Golden regeneration population

Measured. The recipe greps for the `r_data` local, which only the coupled branch of
`emit_level_body` produces:

```bash
G=specs/078-precompiled-builder-libs/contracts/golden
for v in v42 v44 v50sp2 vlatest; do echo "$v files=$(find $G/$v -type f | wc -l) coupled=$(grep -rl r_data $G/$v | wc -l)"; done
```

| version | files | files with a coupled emission |
|---|---|---|
| v42 | 226 | 74 |
| v44 | 506 | 154 |
| v50sp2 | 1341 | 286 |
| vlatest | 1445 | **320** (was 0 before #427) |

- **FR-011a widens the diff beyond these counts.** Every message that can carry an `Encoded*` field
  gains a `message_encoding` Args member (R-8). The census works like this:
  - It is measured over the QuickFIX XML, recursing through components and groups.
  - "Encoded field" means a standard-table Data half whose name **contains** `Encoded`.
  - Positive control: a "begins with `Encoded`" rule was run beside it, and misses exactly
    `DerivativeEncodedIssuer`, `DerivativeEncodedSecurityDesc` and
    `InstrumentScopeEncodedSecurityDesc` in FIX50SP2.
  - False-hit check: the Data halves **not** selected were read by eye. They are the XML, signature,
    password and formula fields (`RawData`, `XmlData`, `SecureData`, `Signature`, `*SecurityXML`,
    `EncryptedPassword`, `*PaymentStreamFormula*`), none of them encoded text.

  | Dictionary | Data halves selected | App messages with an `Encoded*` field |
  |---|---|---|
  | FIX42 | 10 | 37 of 39 |
  | FIX44 | 12 | 77 of 85 |
  | FIX50SP2 | 67 | 142 of 156 (the prefix rule gave 141) |

- **Digest pins are not expected to move.** The SHA-256 pins in
  `tests/codegen/read_tier_byte_diff_test.cmake` cover the read-tier `Fields`/`Messages`/`Reify`/
  `Validator` headers. `struct NewOrderSingleArgs` exists only under the 078 builder goldens
  (`git grep -l "struct NewOrderSingleArgs" -- specs/`).
- **The regeneration therefore must show:** every pinned hash unchanged, and the 078 builder-golden
  diff limited to (a) the coupled-call rewrite and (b) the `message_encoding` member and its emit
  (FR-013).

## R-6 — Groups whose delimiter is a Length tag

Measured (a Python census over `dictionaries/*.xml` and the Orchestra file: each group's first field,
recursing through components, against the 84 standard Length tags). The Orchestra side read 84
`lengthId`s, equal to the table's 84 rows, which serves as the positive control.

- **FIX50SP2** has three groups whose delimiter is a Length tag:
  - `NoPaymentStreamFormulas` (delimiter `PaymentStreamFormulaLength` 43109);
  - `NoLegPaymentStreamFormulas` (43110);
  - `NoUnderlyingPaymentStreamFormulas` (43111).
- **vlatest** has the same three: `PaymentStreamFormulaMathGrp`, `LegPaymentStreamFormulaMathGrp`,
  `UnderlyingPaymentStreamFormulaMathGrp`.
- **All other dictionaries have none.**

**Decision.** `set_data` appends the **Length node first**, so an entry whose delimiter is the
Length tag satisfies INV-5 (`inst.fields.front().tag == delimiter_tag`) unchanged. Codegen already
emits the coupled item at the Length's position, in dictionary order (`resolve_level` keys the
coupled `LevelItem` on the Length tag and skips the Data tag), so the generated order does not move.

**Witness.** A v50sp2 builder test builds one `NoPaymentStreamFormulas` entry whose Data holds SOH,
and asserts both commit success and a re-parse.

## R-7 — Codegen: one call replaces the two, and a compile-time pair witness per call site

**Decision.** `emit_level_body`'s coupled branch emits one call instead of the two
(`r_len`, then `r_data`):
- top level: `bb.field_data(<data_tag>, ::std::as_bytes(::std::span{*args.<m>}))`;
- nested: `<owner>.set_data(<data_tag>, ...)`.

The result local is renamed `r_pair`, so the old `r_data` token disappears from generated output.

Directly before each such call, the generator emits:

```cpp
static_assert(::fixpp::wire::dict_hooks::none().length_tag_for_data(<data_tag>) == <length_tag>);
```

`dict_hooks::none()` and `length_tag_for_data` are both `constexpr`.

**Rationale.**
- FR-010: both arms change together, keeping the old asymmetric-restructure trap closed.
- FR-012 needs a census that can fire. The assertion is a **compile-time census per call site**:
  - it fails the build if codegen ever routes a tag that the standard table does not pair with the
    Length the dictionary declared;
  - that also covers "a non-Data field routed through `field_data`", because such a tag maps to 0;
  - it needs no grep over output, so it cannot report clean for lack of matches.
- **Positive control:** mutate the emitter to pass `item.tag` (the Length) instead of
  `item.data_tag`, and the v44 builders must fail to compile.

**Plus a count census.**
- Every generated file that previously held `r_data` holds `field_data(` or `set_data(` instead.
- Across the regenerated 078 goldens: `grep -rl r_data` = 0, and `grep -rlE 'field_data\(|set_data\('`
  equals R-5's per-version counts (74/154/286/320).
- Proven able to fire by running it on the **old** goldens first, where it must report 834 files
  still holding `r_data`.

**`gen_util.hpp`.** No new `TypeKind` is needed: the coupled item already carries `coupled=true` and
`data_tag`, and the Args member type is unchanged (FR-011). The stale comment claiming the Data
half's kind "is already String" is updated.

## R-8 — `message_encoding` Args member (FR-011a)

**Decision.**
- **Which messages.** The generator computes, per message, whether any member at any depth is the
  Data half of a pair whose `FieldIR` name **contains** `Encoded` ("starts with" misses three FIX50SP2
  fields, R-5). The recursion walks the same
  `group_order` tree `resolve_level` walks.
- **The member.** For those messages it adds `std::optional<std::string_view> message_encoding;` to
  the top-level Args.
- **The emit.** At top level only, it emits `bb.field(347, *args.message_encoding)` **as the first
  body field**, before any other Args member, when the member is set.
- **Accessor collision.** It is resolved by the existing `uniquify_accessor` / `used_accessors`
  mechanism, which is already reserved at the top level.

**Rationale.**
- 347 is a header-class tag (`is_send_header_tag` includes it), so `Session::send_impl`'s header
  pass moves it into the header (fixpp#422). Emitting it first keeps the frame readable even without
  that move.
- The value goes through `field(tag, string_view)`, so INV-2's clean guard applies. Encoding names
  (`UTF-8`, `Shift_JIS`, `ISO-2022-JP`) are printable ASCII.
- It is not enforced (owner decision). The absence of enforcement goes in the B&L file.

**Source compatibility (SC-004).**
- Designated-initializer callers keep compiling.
- A **positional** aggregate-initializer caller would shift **only if** the new member were inserted
  before its last positional field. The member is therefore **appended last** in the Args struct.
  Recipe to confirm: the generated Args member order is otherwise dictionary order; check the v44
  `NewOrderSingleArgs` golden diff shows the member as the final one.

## R-9 — Session header reorder is already pair-aware

Read in `src/session/session.cpp` (`Session::send_impl`, the fixpp#422 "header partition pass"):
- Both passes scan with `wire::length_data_carry`.
- A counted (Data) field inherits its Length's header classification
  (`field->counted ? prev_header : is_send_header_tag(...)`).
- So 90/91 and 212/213 move together, and a Data value holding SOH is not split.

**No session change is needed.** A witness test drives an XmlData(212/213) value holding SOH
through `send_impl` and re-parses the frame (spec edge case).

**Known bound.** The carry uses the standard table. A dictionary-only pair in the **header** is the
L-426-1 residual and is out of scope.

## R-10 — Performance baseline (SC-005)

**Instrument.** `bench/wire/builder_bench.cpp` (commit `4749f589`), four cases:
`BM_Build_NOS_NoGroup`, `BM_Build_NOS_WithGroup`, `BM_Build_NOS_AsciiEncodedText`,
`BM_BodyBuilder_Raw_10Fields`. Each case pre-checks the exact body it times, and was shown to report
an error under a mutated expected body.

**Baseline.**
- The binary was built from `4749f589`, whose production code equals `origin/main`, in
  `build/linux-clang-release`, and frozen at `/mnt/wsl/fixppbuild/091-baseline/builder_bench.base`.
- It links statically (no fixpp shared libraries, per `ldd`), so later rebuilds cannot change it.

**Noise floor.** An A/A run (the same binary twice, `taskset -c 3`, 15 repetitions, `min_time=0.2s`,
medians) gave per-case median deltas of **+0.07 %, +0.57 %, −0.09 %, +0.51 %**, with a CV of
0.55–2.13 %. **A 3 % budget is resolvable on this host.**

**Comparison method (the Article VIII §2 shape).**
- Rebuild the candidate `builder_bench` (after `fixpp-codegen` rebuild plus
  `rm -rf build/linux-clang-release/_codegen`, otherwise the "after" build times the old generated
  code).
- Run base and candidate alternately (A-B-A-B, ≥4 pairs) on the same pinned core.
- Compare the per-tree **minimum** of the medians. Budget +3 % on `NoGroup` / `WithGroup` / `Raw`.
  `AsciiEncodedText` is reported but exempt: it is the path this feature changes on purpose.
- Over budget → report to the owner (SC-005), never silently relax.

**Compile-time surface.** Every builder translation unit gains the per-call-site `static_assert`s
(R-7) and `dict_hooks.hpp` through `body_builder.hpp`. That matters most for `vlatest`, the largest
generated tier. Re-measure with the existing `bench/codegen/vlatest_builders_compile_bench`, before
and after, and report the delta; it has no budget. Measuring here keeps CI from being the first to
see it.

**Not affected: the `nm` symbol witnesses** (`tests/codegen/test_078_nm_*`). Their patterns in
`tests/codegen/CMakeLists.txt` pin `fixpp::v44::build_*` / `validate_*` / `writer_traits` only,
never `body_builder` members, so switching to `field_data`/`set_data` moves none of them. Recipe:
`grep -n 'FIXPP_PRESENT\|FIXPP_ABSENT\|FIXPP_UNDEFINED_PRESENT' tests/codegen/CMakeLists.txt`.

**CI registration.** The bench is added to `bench/ci-suite.txt`. It is a candidate-only addition
under Article VIII §2a, so it is execution- and schema-gated this PR.
