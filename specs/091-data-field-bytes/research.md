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
names the **Data** tag only. The Length tag comes from the standard table (R-2), and the Length
value is the decimal octet count.

**Rationale.**
- It matches `fixpp_msg_set_data` / `fixpp_entry_set_data` (#428) in shape: the Data tag is the
  key, the Length is derived, arbitrary octets are accepted, and an empty value is refused. It is
  **not** an exact mirror; two divergences are intentional and disclosed (B&L):
  - **Pair set at set time:** the C-ABI setter honours dictionary pairs from the handle's own
    session dictionary; `field_data` honours the standard table only (R-2, FR-009).
  - **Repeated call:** `upsert_pair` (`src/capi/message_write.cpp`) overwrites an adjacent pair or
    refuses a partial one; `field_data` appends a second pair, as `field()` appends (C-1.11).
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

## R-2 — Pair lookup: standard table at set time; the builder's `dict_hooks` at commit

*Revised at Gate A round 1 (owner ruling 1, Clarifications Session 2026-09-24 (Gate A round 1)); the
specify-time "honour dictionary pairs at set time" decision is superseded for the set-time
operation.*

**Decision.**
- **Set time.** `field_data` / `set_data` resolve the Length tag with
  `dict_hooks::none().length_tag_for_data(data_tag)` — the standard table only, whatever hooks the
  builder holds.
- **Commit time.** The constructor becomes
  `explicit body_builder(std::string_view msg_type, dict_hooks hooks = dict_hooks::none()) noexcept`.
  `dict_hooks` is trivially copyable (a `static_assert` in `dict_hooks.hpp`) and is stored by value.
  The commit check (R-4) builds its `length_data_checker` from the stored hooks, so a dictionary's
  own pairs are checked at commit (B-426-3 precedence).
- **Relation.** Set-time pairs ⊆ commit-time pairs (every hooks value answers the standard pairs
  first), so nothing `field_data` appends is refused at commit.

**Why the constructor parameter stays.** It is needed only for the commit check on a custom pair
written by hand through `field()`/`set_string`. Without it, commit would treat 5001/5002 as plain
tags and never check that pair's well-formedness. It carries the `dict_hooks` lifetime precondition
(`include/fixpp/wire/dict_hooks.hpp`: the dictionary a `for_table_view` bundle points at must outlive
every holder); `body_builder` is a non-movable scoped local, the shortest-lived holder in the
library. C-1 states it.

**Rationale (the injection argument, Opus Gate A r1 NEW P1-B).**
- Nothing ties a caller-supplied `dict_hooks` to the dictionary of the session that sends the
  payload. `Session::send_impl` scans and partitions with its own session hooks; a SOH in a tag that
  is not a pair *there* is a field boundary, and the scanner refuses only its framing-tag set. So a
  set-time path honouring caller hooks could emit `5002=abc<SOH>1=EVIL` that a peer reads as
  Account(1)=EVIL.
- With set-time standard-only, SOH reaches the wire only in a standard Data tag, which every fixpp
  scanner reads by count. A custom pair can only be written through `field()`/`set_string`, whose
  INV-2 keeps it printable ASCII.
- `dict_hooks` lives in the wire layer, so `body_builder` gains no dictionary include.

**Generated builders pass nothing** and get `none()`. `fixpp-codegen` is not an installed tool
(`tools/codegen/fixpp-codegen/CMakeLists.txt`, "NEVER installed"); it runs at configure time over the
shipped dictionaries only (`cmake/Codegen.cmake`). Codegen over a custom dictionary is unsupported,
and R-7's compile-time assertion makes such a run fail loudly rather than emit an unknown pair.
⚠️ The earlier claim that "the standard table is by construction the union of those dictionaries'
pairs, so every pair a generated builder can emit is standard" was true but **insufficient**: it
says nothing about pairs a single dictionary's loader *misses* (R-11).

**Alternatives rejected.**
- Set-time dictionary pairs via caller hooks (the specify-time decision): the injection path above.
- (b) A verifiable session binding (a dictionary identity that `send_impl` checks): the right way to
  restore C-ABI parity; deferred to the follow-up *verifiable session binding for dictionary pairs,
  option (b)*, fixpp#505.
- (c) A documented precondition "hooks must come from the sending session's dictionary": cannot be
  enforced.
- A per-call `dict_hooks` parameter: lets set and commit disagree.
- A `table_view const&` parameter: puts a dictionary edge on `body_builder`, which its header
  forbids ("no wire→dictionary edge").

## R-3 — Atomicity and refusal order

**Decision.** `field_data` / `set_data` do everything in this order:
1. **Handle checks** (`set_data` only): the handle has an owner and the entry is the innermost open
   one. Otherwise `wire_invalid_field_format`, the same as `set_string`. This is the only guard
   between a default-constructed or stale handle and the unchecked indexing in `resolve_group` /
   `resolve_instance`, so it is pinned by its own clause and mutants (C-1.4b), not inherited from
   `set_string`'s tests.
2. `is_framing_tag(data_tag)` → `wire_field_value_out_of_range`.
3. `length_tag = dict_hooks::none().length_tag_for_data(data_tag)` (R-2). If it is 0 →
   `wire_unexpected_tag`.
4. `value.empty()` → `wire_field_value_out_of_range`.
5. Append the Length node, then the Data node, into the same container.

The former runtime "framing Length" check is gone: with the standard table only it is unreachable,
since no standard pair tag is a framing tag. That property is asserted at compile time instead — a
`static_assert` over `core::detail::standard_length_data_pairs` that no row names a framing tag
(`is_framing_tag` in `body_builder.cpp` becomes `constexpr` for this) — so a future table row that
broke it fails the build rather than reaching a dead runtime branch.

If the second append throws `bad_alloc`, the Length node is popped as well. The result is **both
nodes or neither** in INV-4's terms: the container is back at its pre-call size, so a later commit
serializes exactly what it would have without the call. Every refusal in steps 1–4 happens before
any append.

**What INV-4 does not promise.** The arena is a null-upstream `monotonic_buffer_resource`; `pop_back`
does not return storage to it. A failed call therefore leaves consumed capacity behind, exactly as a
failed `field()`, `group_begin()` or `add_entry()` does today (`append_bytes_field`); repeated
failures can exhaust the fixed arena. A checkpointable arena (Codex r1 P1-1) was
rejected as disproportionate for a 16 KB scoped local; the non-reclamation is stated in the API
comment (FR-006).

**Oracle.** Every refusal and rollback clause is observed by **commit and byte-compare**: after the
failed call, commit, and require the body to equal byte-for-byte the body committed by an identical
builder that never made the call. `body_builder` has no size accessor, so "container size unchanged"
is not directly observable; the committed bytes are (C-1.4, C-1.5).

**Rationale.** FR-004 / FR-004a / FR-006. The ordering puts all refusals first, so a single rollback
path covers only arena exhaustion.

**Two surfaces, one order.** `field_data` and `set_data` are independent public members, and in
`body_builder.cpp` each existing `entry_handle` setter re-implements its checks before forwarding,
so the shared order above is not structural by default. The **preferred implementation** is one
static `append_data_field(into, data_tag, value)` holding steps 2–5, which both members call (the
file's `append_*_field` style). It is not the witness: C-1.4, C-1.5's nested twin and C-1.9 run the
refusals and the rollback through both surfaces, whichever implementation is chosen.

## R-4 — Commit-time pair check (FR-008): fold into the existing INV-5 tree walk

**Decision.** `commit()` already walks the whole accumulator tree once (`validate_group_grammar`,
INV-5). That walk is extended: for each container (the top-level `entries_`, and each
`group_instance::fields`) it feeds every node, in order, to a `wire::length_data_checker` built from
`hooks_`, then calls `finish()`.
- A scalar node is fed as `observe(tag, value_bytes)`.
- A group node is fed as `observe(no_tag, {})`.

Any failure → `wire_invalid_field_format`, `out` untouched.

**Rationale.**
- FR-008 (owner decision): refuse at commit. Reuse `length_data_checker` (O-4), which makes this its
  second caller.
- The walk already exists, but today it skips every scalar (`if (!e.is_group) continue;` in
  `validate_group_grammar`), so the per-scalar work is **new**: `observe`'s `failed_` and
  `awaited_data_` branches, two `standard_pair_tag_bit` tests, two `length_pair_ == nullptr` tests
  (`dict_hooks.hpp`), plus one `dict_hooks` copy into each per-container checker. It is small, and
  SC-005 is **measured** (R-10), not predicted from this list.
- A group node is fed to the checker as `observe(no_tag, {})`, with an **empty** value. This is
  **normative** (Gate A r3). The empty value is the guard. `body_builder::group_begin` refuses only
  framing tags, so a hand-written group may carry a pair-half tag, and with an empty value
  `length_data_checker::observe` refuses both halves (`include/fixpp/wire/length_data_check.hpp`):
  - a Data tag fails the "Length not right before it" check;
  - a Length tag fails on its empty count.
  Feeding the count digits instead, as the C-ABI commit does (`src/capi/message_write.cpp`), would
  let a group tagged 354 with one instance start waiting for a 355 of that length. A sibling 355 of
  one byte would then pass. C-1.7's group-tag cases and the quickstart §3 count-digits mutant pin
  this. The C-ABI's own group path is a separate, pre-existing defect, not 091 scope:
  `check_length_data` (`src/capi/message_write.cpp`) feeds a group node its count digits, so a group
  tagged 354 with one instance plus a sibling one-byte 355 commits through the C-ABI (reachable on a
  dictionary-free session, which opens groups on any tag). Follow-up, fixpp#506:
  *C-ABI commit feeds a group node's count digits to the Length+Data check*.
- The recursion depth is the one the INV-5 walk already uses. No new stack shape.

**Alternative rejected.** A separate second walk: twice the tree traversal for no benefit.

**Consequence to disclose (B&L).** A hand-written C++ caller that today emits a malformed pair
through `field()` gets `wire_invalid_field_format` at commit, where before it produced a malformed
frame. A literal-tag grep,
`git grep -n "field(\(90\|91\|95\|96\|212\|213\|354\|355\)\b" -- src tests bench bindings`, finds
**no C++ `body_builder` caller that writes one of those pairs with a literal tag number**. Every hit
is in the C-ABI test file `tests/capi/length_data_setters_test.cpp`, which is a separate code path.
⚠️ The grep cannot see a caller that uses a named constant or a computed tag, so it is **not** a
blast-radius proof. The full ctest run once the commit check lands is also blind to **generated**
code no test builds. The generated side is covered structurally instead: FR-008 refuses a generated
message only if a standard pair is left uncoupled, and C-2.2's census proves none is (after R-11).
This is still a behaviour change for external C++ callers, so it gets a B-091-* row.

## R-5 — Golden regeneration population

The population is re-derived, never pinned. Recipe for the files the old emitter coupled (the
`r_data` local is produced only by the coupled branch of `emit_level_body`):

```bash
G=specs/078-precompiled-builder-libs/contracts/golden
for v in v42 v44 v50sp2 vlatest; do echo "$v files=$(find $G/$v -type f | wc -l) coupled=$(grep -rl r_data $G/$v | wc -l)"; done
```

⚠️ This population is defined by the **old emitter's coupling decision**, so it cannot see a standard
pair the loader never coupled (R-11). It sizes the diff; it is not the census. The census (C-2.2)
takes its expected set from the IR against the standard table.

- **FR-011a widens the diff.** Every message that can carry an `Encoded*` field gains a
  `message_encoding` Args member (R-8). Census recipe: over the QuickFIX XML, recursing through
  components and groups, select standard-table Data halves whose name **contains** `Encoded`, and
  list the app messages whose expansion holds one.
  - Positive control: run a "begins with `Encoded`" rule beside it; it must miss exactly
    `DerivativeEncodedIssuer`, `DerivativeEncodedSecurityDesc` and
    `InstrumentScopeEncodedSecurityDesc` in FIX50SP2.
  - False-hit check: read the Data halves **not** selected by eye. They are the XML, signature,
    password and formula fields (`RawData`, `XmlData`, `SecureData`, `Signature`, `*SecurityXML`,
    `EncryptedPassword`, `*PaymentStreamFormula*`), none of them encoded text.
  - The selection keys on the standard table (R-8), so `EncodedLegDocumentationText(2493)` and
    `EncodedPostTradePaymentDesc(2814)` are selected on v50sp2 with or without R-11.
- **Two read-tier digest pins move (amended at Gate A round 1).** The SHA-256 pins in
  `tests/codegen/read_tier_byte_diff_test.cmake` cover the read-tier `Fields`/`Messages`/`Reify`/
  `Validator` headers. The builder change itself does not reach them (`struct NewOrderSingleArgs`
  exists only under the 078 builder goldens: `git grep -l "struct NewOrderSingleArgs" -- specs/`).
  The **loader** change (R-11) does: `v50sp2/Fields.hpp` emits each `FieldRef`'s
  `length_pair_data_tag` (`emit_fields.cpp`), and `v50sp2/Validator.hpp` emits `ir.length_pairs` as
  `length_data_pairs` (`emit_validator.cpp`). Both are rebaselined (C-2.5); every other pin must stay
  unchanged. The generated `validator::length_data_pairs` array is read only by
  `tests/codegen/length_data_table_test.cpp`, which compares it with the same dictionary, so the two
  move together.
- **The regeneration therefore must show:** every other pinned hash unchanged, and the 078
  builder-golden diff limited to FR-013's transformations (a)–(c), validated by C-2.4.

## R-6 — Groups whose delimiter is a Length tag

Recipe (re-derive; measured 2026-09-24): for each of `dictionaries/*.xml`, parse with
`xml.etree.ElementTree`, and for every `<group>` element at any depth, recursing through
`<component>` definitions, take its first child `<field>`; report the group if that field's tag is a
Length tag of `core::detail::standard_length_data_pairs`. For the Orchestra file, do the same over
`fixr:group` first members. Positive control: the Orchestra side must read one `lengthId` per
standard-table row (the same set, not just the same count), or the parse is wrong.

- **FIX50SP2** has three groups whose delimiter is a Length tag:
  - `NoPaymentStreamFormulas` (delimiter `PaymentStreamFormulaLength` 43109);
  - `NoLegPaymentStreamFormulas` (43110);
  - `NoUnderlyingPaymentStreamFormulas` (43111).
- **vlatest** has the same three: `PaymentStreamFormulaMathGrp`, `LegPaymentStreamFormulaMathGrp`,
  `UnderlyingPaymentStreamFormulaMathGrp`.
- **All other dictionaries have none.**

**Decision.** `set_data` appends the **Length node first**, so an entry whose delimiter is the
Length tag satisfies INV-5 (`inst.fields.front().tag == delimiter_tag`) unchanged. Codegen emits a
coupled item at the Length's position (`resolve_level` keys the coupled `LevelItem` on the Length tag
and skips the Data tag).

⚠️ **On v50sp2 this holds only after R-11.** On the unfixed loader, 43109/42684, 43110/42486 and
43111/42982 are **not** coupled (`resolve_level` couples only when `ref.length_pair_data_tag != 0`),
so the three formula groups carry an int Length member and a separate string Data member routed
through `set_string`, which refuses SOH. The witness below cannot be written until R-11 lands; that
is part of what makes R-11 in scope. vlatest couples all three already (#427).

**Witness.** A v50sp2 builder test, after R-11, builds one `NoPaymentStreamFormulas` entry whose
Data holds SOH, and asserts both commit success and a re-parse. RED condition on the unfixed
loader: the SOH value is not emitted verbatim with a matching Length (the uncoupled Data member goes
through `set_string`, which refuses it).

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

**Plus an exact census (C-2.2), replacing the file count.** A file count keyed on `r_data` inherits
the old emitter's coupling decision and hides a lost call site in a file that keeps another. The
census instead:
- derives the **expected** multiset of (message, structural path, Length tag, Data tag, top/nested
  arm) from the IR, taking pair-ness from `core::detail::standard_length_data_pairs` — **not** from
  `FieldRef::length_pair_data_tag` — for every standard pair whose two halves appear at one level;
- parses the **actual** multiset from the regenerated call sites (`field_data(`/`set_data(` plus the
  preceding `static_assert`'s Length tag);
- requires equality, and requires no `field_data`/`set_data` on a tag outside the expected set.
- adds an orphan-half check: no standard-table tag passed to any `field(`/`set_<kind>(` call other
  than `field_data`/`set_data` (FR-008 proviso).
- **Positive controls:** (1) on the output of the new emitter over the unfixed loader (the "loader
  walk removed" mutant) it must report exactly the five uncoupled pairs of R-11 as missing, and the
  orphan-half check exactly their ten tags; the pre-change goldens cannot serve, since they hold no
  coupled call at all; (2) deleting one coupled emission from a regenerated golden, and swapping one
  arm (top ↔ nested), must each be reported.

**`gen_util.hpp`.** No new `TypeKind` is needed: the coupled item already carries `coupled=true` and
`data_tag`, and the Args member type is unchanged (FR-011). The stale comment claiming the Data
half's kind "is already String" is updated.

## R-8 — `message_encoding` Args member (FR-011a)

**Decision.**
- **Which messages.** The generator computes, per message, whether any member at any depth is the
  Data half of a **standard** pair (`core::detail::standard_length_data_pairs`) whose `FieldIR` name
  **contains** `Encoded` ("starts with" misses three FIX50SP2 fields, R-5). The recursion walks the
  same `group_order` tree `resolve_level` walks. Keying on the standard table, not on
  `FieldRef::length_pair_data_tag`, keeps the selection independent of loader coupling (R-11).
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

## R-10 — Performance baseline (SC-005)

*Revised at Gate A round 2 (RC-B): the instrument was frozen at `4749f589` before the design
settled; a rebase, a bench fix or a toolchain bump each invalidates a frozen artifact.*

**Instrument.** `bench/wire/builder_bench.cpp` (commit `4749f589`), four cases:
`BM_Build_NOS_NoGroup`, `BM_Build_NOS_WithGroup`, `BM_Build_NOS_AsciiEncodedText`,
`BM_BodyBuilder_Raw_10Fields`. NoGroup, WithGroup and Raw MUST pre-check the exact body they time;
AsciiEncodedText checks a marker (it is exempt from the budget). *Dated, 2026-09-24 (read at
`4749f589`; re-derive by reading each case's precheck call):* only NoGroup did (the file's own header
says it "checks a body substring"); WithGroup passed an inexact group-header marker and Raw checked
only a tag-60 suffix. Before the paired run, the bench pins complete `kWithGroupBody` and
`kRawBody`, as `kNoGroupBody` already is, and compares exactly; the positive control is a one-line
mutant per case (drop one scalar field; drop one of the three `kParties` entries), each producing
`SkipWithError` (quickstart §3). A candidate that silently drops work is also a functional defect the
round-trip suites catch (C-2.6, the flipped `_418` pins), so this protects the timing, not
correctness.

**Base (Article VIII §2 shape, owner intent kept).** The owner fixed the comparand's **source**:
production code equal to unmodified `main`, never a post-change re-baseline (spec Clarifications,
Session 2026-09-24 (Gate A round 2)). Article VIII §2 fixes the **procedure**. Both hold when:
- **base** = `builder_bench` built in the measurement session, same preset and toolchain, from a
  detached worktree at the **current** `git merge-base HEAD origin/main` with this branch's final
  `bench/wire/builder_bench.cpp` + `bench/wire/CMakeLists.txt` applied and nothing else; the
  production-code diff against the merge-base (`src include tools cmake`) MUST be empty. At the
  merge-base the branch started from (`48589054`), that tree is `4749f589`'s content with the final
  bench source;
- **candidate** = the branch build, after `fixpp-codegen` rebuild plus
  `rm -rf build/linux-clang-release/_codegen` (otherwise the "after" build times the old generated
  code);
- the frozen `/mnt/wsl/fixppbuild/091-baseline/builder_bench.base` is a drift cross-check only.

**Noise-floor precondition.** Read from the **base legs of the same A-B-A-B session**, no extra runs:
per case, (max − min) / min of the medians over all base legs; the floor is the maximum over
NoGroup / WithGroup / Raw. Above **1 %** the verdict is **inconclusive** and goes to the owner, never
compared. Why 1 %: it is budget/3, so a 3 % verdict stays resolvable; Codex r2's 0.6 % would sit at
the recorded A/A maximum (dated, below).
*Dated measurement, 2026-09-24 (A/A of the frozen binary, `taskset -c 3`, 15 repetitions,
`min_time=0.2s`, medians):* every per-case median delta was below 1 %. It informs the threshold only;
it is not the precondition.

**Comparison method.**
- Run base and candidate alternately (A-B-A-B, ≥4 pairs) on the same pinned core, each leg writing a
  fresh output path; a missing or empty leg fails the run.
- Compare the per-tree **minimum** of the medians. Budget +3 % on `NoGroup` / `WithGroup` / `Raw`.
  `AsciiEncodedText` is reported but exempt: it is the path this feature changes on purpose.
- Over budget → report to the owner (SC-005), never silently relax.

**`xml_loader_bench`.** FR-017 adds a walk over every component and group, and
`bench/dictionary/xml_loader_bench` is a `paired` row in `bench/ci-suite.txt` timing the FIX50SP2
load. It is measured A-B-A-B with the same base worktree before pushing; pass condition Article VIII
§2's budget (a slowdown ≤ +5 %), over it → the §2 approval path.

**Compile-time surface.** Every builder translation unit gains the per-call-site `static_assert`s
(R-7) and `dict_hooks.hpp` through `body_builder.hpp`. That matters most for `vlatest`, the largest
generated tier. Re-measure with the existing `bench/codegen/vlatest_builders_compile_bench`, before
and after, and report the delta; it has no budget. Measuring here keeps CI from being the first to
see it.

**Not affected: the `nm` symbol witnesses** (`tests/codegen/test_078_nm_*`). Their patterns in
`tests/codegen/CMakeLists.txt` pin `fixpp::v44::build_*` / `validate_*` / `writer_traits` only,
never `body_builder` members, so switching to `field_data`/`set_data` moves none of them. Recipe:
`grep -n 'FIXPP_PRESENT\|FIXPP_ABSENT\|FIXPP_UNDEFINED_PRESENT' tests/codegen/CMakeLists.txt`.

**CI registration.** The bench is added to `bench/ci-suite.txt` with tier-2 value `no`. It is a
candidate-only addition under Article VIII §2a, so it is execution- and schema-gated this PR; `no` is
chosen deliberately because §2a makes `paired` irreversible.

## R-11 — The QuickFIX XML loader misses standard pairs declared adjacently only inside components or groups (Gate A round 1, owner ruling 2)

**Finding (Opus Gate A r1 NEW P1-A).** `LoaderState::detect_length_pairs`
(`src/dictionary/xml_loader.cpp`) records a pair when a LENGTH field is immediately followed by a
DATA/XMLDATA field (a) in `<fields>` declaration order, or (b) among the direct `<field>` children of
the header, the trailer, or a `<message>`. It never visits a `<component>` definition or a
`<group>`. FIX 5.0 SP2 declares five standard pairs Data-first or far apart in `<fields>` and
adjacently only inside components/groups (`LegStipulations`, `PostTradePayment`, the three
`*PaymentStreamFormula` groups): 2494→2493, 2815→2814, 43109→42684, 43110→42486, 43111→42982. The
loader records none of them, so codegen leaves them uncoupled on v50sp2.

**Why no test saw it.** `LengthDataPairs.HeaderEqualsShippedDictionaryUnion`
(`tests/wire/length_data_pairs_drift_test.cpp`) compares the standard table with the **union** of all
ten dictionaries; Orchestra's `lengthId` supplies the five pairs, so the union matches while
FIX50SP2.xml alone lacks them. `.specify/426-428-length-data-pairs.md` (§ on #418, "for the shipped
dictionaries the §2 drift test is what keeps the two in step") is therefore a **known-stale claim**
per dictionary; this feature makes it true (FR-018). The 426-428 note itself is not edited.

**Decision.** Extend the secondary walk to every `<component>` definition and every `<group>` at any
depth. The group walk is **recursive to any depth** and covers every `<group>` under `<fix>`, whatever
its parent: `<header>`, `<trailer>`, `<message>`, `<component>` or another `<group>`, however deep. **In those new containers, adjacency is broken by any non-`<field>` child** (a component
reference or nested group between two fields), which is the semantics the model below uses. The
existing direct-`<field>` walk of header, trailer and messages (`container.children("field")`, which
*skips* non-field siblings) is left **unchanged**, so no pair it records today can be lost. The
groups inside those containers belong to the new group walk.
- The loader's primary-walk comment "In practice the global-fields path already captures all
  standard pairs" is false and is **deleted**; the function's header comment names the superseding
  decision ("091 (fixpp#418) Gate A r1 — secondary walk descends into components and groups").
- **Visit order** (it decides conflicts): `<fields>`, then header, trailer, messages (as today),
  then `<component>` definitions in document order, then `<group>` elements in document order.
  `mark_pair` stays first-writer-wins per Length, so a Length adjacent to Data A in one component
  and Data B in a later one pairs with A (C-2.5a arm (iv)). Because every component definition
  is visited before any group, a Length adjacent to Data A inside a group and to Data B in a
  component pairs with B wherever the group sits (C-2.5a arm (vii)); a depth-first walk that visits
  a group right after its container would differ, and arm (vii) is the witness.
- **Inbound, shipped dictionaries: unaffected.** Every scanner resolves via
  `dict_hooks::data_tag_for_length`, which answers a standard tag from the table alone
  (`include/fixpp/wire/dict_hooks.hpp`); the five pairs are standard, so on the shipped dictionaries
  `table_view::has_nonstandard_pair()` does not flip.
- **Inbound, user-loaded dictionaries: changed, disclosed.** `XmlLoader` is a public runtime API. A
  custom pair declared adjacently only inside a component or group was not paired before and is
  after, so for that user `has_nonstandard_pair()` flips, `for_table_view` installs the pair
  callback on every scanner, inbound parsing reads the Data by count, `fixpp_msg_set_data` accepts
  it, the SOH-in-value refusal no longer applies to its Data tag (`fixpp_msg_set_string` /
  `fixpp_entry_set_string` at set time; any setter at commit when the value is correctly
  Length-prefixed), and the C-ABI and `body_builder` commit checks check it. Arguably a correction (the dictionary
  declares the adjacency), but a wire-behaviour change on a public API: disclosed in B-091-4 and
  witnessed by C-2.5a; its C-ABI half is FR-019's population, derived by the recipe below and
  witnessed by FR-019's tests.
- **Why a synthetic witness.** On the shipped dictionaries, "break on a non-field child" and "skip
  it" yield the same pair set (Opus Gate A r2, measured; re-derive with the model recipe below run
  in both modes), so an implementation reusing the message walk's skip would pass every shipped-
  dictionary test. Only C-2.5a arm (iii) observes the rule. The same holds for **depth**: a
  walk over components and only their direct `<group>` children can yield the same pair set on
  every shipped dictionary as the recursive walk (re-derive by running the model recipe below in
  both modes). So only the C-2.5a arms that place a pair in a group whose parent is not a component
observe "any depth".
- **What moves.** `Dictionary::length_pair_data_tag`, `table_view::length_pair_data_tag` and
  `FieldRef::length_pair_data_tag` on FIX 5.0 SP2 now report the five pairs (a correction, disclosed
  in the B&L file); codegen couples them in the v50sp2 goldens; the caller-set Length members are
  deleted (FR-011 carve-out); two read-tier pins are rebaselined (R-5, C-2.5).
- **What it removes.** Without it, FR-008 would refuse v50sp2 `PayManagementRequest`/
  `PayManagementReport` whenever `EncodedPostTradePaymentDesc` is set, because their top level
  emits 2814 before 2815.

**Model recipe (re-derive; do not trust recorded counts).** For each `dictionaries/FIX*.xml`: compute
the current loader's pair set (`<fields>` adjacency plus direct-child adjacency in header, trailer
and each message), then the extended set (also every `<component>` with children and every
`<group>`, adjacency broken by any non-field child). The model's message-level walk breaks on a
non-field child where the loader skips it; that difference can only *add* pairs on the loader side,
and the union drift test would report any non-standard one. Report, per dictionary, the pairs added, any
added pair that is not a standard-table row, and any standard pair still unpaired whose two tags
both occur in some message expansion. **Condition to hold:** added pairs are exactly the five above
and only for FIX50SP2; no non-standard row anywhere; nothing still unpaired within the expansion
probe set. (A standard pair declared in `<fields>` but used by no message expansion — e.g. the
trailer's SignatureLength/Signature in the FIX 5.0 dictionaries, whose trailer lives in FIXT 1.1 —
is outside the probe set and is not a failure.)

**Per-dictionary drift arm (FR-018).** In `tests/wire/length_data_pairs_drift_test.cpp`, beside the
union test, one arm per shipped dictionary, Orchestra FIX Latest included: probe every Length-typed `FieldRef` in every message
expansion (the same `message_fields()` probe `add_pairs` uses); for every standard pair whose Length
**and** Data tags both occur in that dictionary's expansions, require
`dict.length_pair_data_tag(length) == data`. Written first; **RED on the unfixed loader** for
FIX50SP2 with exactly the five pairs named; GREEN after the fix. Each arm asserts that its probed
standard-pair set is non-empty and prints it, so a wrong path, an empty `message_fields()` or a wrong
Length-type filter fails rather than passes; a quickstart §3 mutant emptying one non-FIX50SP2 leg's
probe shows that assertion able to fire. The union test stays and continues to catch an
**over**-pairing (a non-standard row).

**C-ABI 1.9 population recipe (FR-019; re-derive at the implementation head, do not trust a
recorded list or count).** Loop 3 round 1 (Opus judging) derived it; `codegraph_callers` may replace
the greps. Run in the library root:
1. **Pair-sensitive primitives:** `grep -rnE
   "session_hooks|pair_hooks|for_table_view|length_data_carry|length_data_checker|has_nonstandard_pair|length_pair_data_tag|length_tag_for_data"
   src include`. Keep the files on a runtime path (`src/capi/*`, `src/session/*`,
   `include/fixpp/session/*`, `include/fixpp/wire/{parser,validator}.hpp`,
   `src/wire/offset_table.cpp`); `src/dictionary/*` is the loader, the root cause.
2. **C-ABI direct callers:** `grep -nE "pair_hooks|soh_outside_data|check_length_data"
   src/capi/message_write.cpp`, each hit mapped to its enclosing `FIXPP_API_EXPORT` function
   (`soh_outside_data` and `check_length_data` are static helpers: map their callers).
3. **C-ABI → session:** `grep -rnE "engine_->send\(" src/capi` (the exports that reach
   `Session::send_impl`).
4. **C-ABI → inbound view:** every `FIXPP_API_EXPORT` in `src/capi/message_read.cpp` reads a view
   `Session::parse_and_dispatch_` built with the session dictionary, or a clone sharing its
   membership; delivery itself is `fixpp_session_register_callback`, and the toApp view is
   `fixpp_session_register_send_callback` (same `parse_and_dispatch_`).
5. **The rest:** every other symbol of `tests/abi/golden/fixpp_capi_symbols.txt` (the authority
   data-model.md Appendix A is gated on; it includes declarations with no `FIXPP_API_EXPORT` and
   those outside `include/fix/c_api/`) is checked for a path to steps 1–4 and classified.

**Classification** (`[const §X.7]`): a success turned into a failure is BREAKING whatever the
documentation said; a failure turned into a different failure is BREAKING (§X.7: a result other
than the one documented for that input); a failure turned into a success, or a change in output the
documentation leaves unspecified, is additive. A declaration that is BREAKING has its note name every effect it shows. An effect reached only through engine internals
with no declaration of its own goes in the `version.h` history comment. The classes are FR-019's;
the per-export result is data-model.md Appendix A, whose row set plan Phase 0b diffs against
`tests/abi/golden/fixpp_capi_symbols.txt` (the diff proves the row set only; this recipe re-verifies
each row's class).

**Alternatives rejected (owner).** (a) Couple from the standard table in codegen while leaving the
loader: fixes builders but leaves the dictionary API answering wrong on FIX50SP2. (c) Scope the five
pairs out: leaves #418 open for them, needs an FR-016 carve-out, and leaves FR-008 refusing two
generated messages.
