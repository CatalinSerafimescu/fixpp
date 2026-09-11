# #418 — an arbitrary-bytes emit path for DATA fields

> **Status: v1, 2026-09-11.** First draft, not yet reviewed. Input to a scoped Gate A, on the
> `#215`/`.specify/215-dictionary-view.md` precedent: one public-API decision, not a module design.
> Trigger: `[const §XVII.1]` first bullet — the recommended shape adds two public members to
> `wire::body_builder`/`entry_handle` and changes what the generated builders emit for every
> `DATA`-typed field.
>
> **Scope.** Settles (a) the `body_builder` public API shape, (b) the codegen builder change and its
> regeneration cost, (c) whether the C-ABI/Python surfaces need anything, (d) the witness plan for
> both stages of issue #418 item 3, and (e) `L-067-2`'s disposition once the fix lands. It does
> **not** implement the fix — this task is design only, per its brief. Every claim about current code
> below was read from the tree at `fix/418-data-field-bytes` off `origin/main` `27c89864`, cited by
> function/struct name, never by line number (`tools/check_line_citations.py` gate).

---

## 1. What is being decided

`wire::body_builder::field(tag, std::string_view v)` and `entry_handle::set_string` both route
through `append_string_field`, which rejects any value for which `is_clean_field_value` is false —
every byte outside `0x20-0x7E`. The generated builders route a coupled Length+Data member (e.g.
`EncodedTextLen(354)`/`EncodedText(355)`) through this same string path: `resolve_level`'s coupled
branch sets `item.kind = TypeKind::String` unconditionally, and `emit_level_body`'s coupled branch
calls `bb.field(...)`/`owner_expr.set_string(...)` for the Data half. So a `DATA`-typed field —
whose whole purpose under `MessageEncoding(347)` is to carry non-ASCII bytes — cannot carry any.

The fix issue #418 names: an emit path where the Length is derived from the byte count and the Data
content is exempt from `is_clean_field_value`, since the Length (not content inspection) is what
makes embedded SOH framing-safe on the wire.

---

## 2. (a) Public API shape

**Recommendation: two new, distinctly-named members — `body_builder::field_bytes(tag,
std::span<const std::byte>)` and `entry_handle::set_bytes(tag, std::span<const std::byte>)`** — not
an overload of `field()`/`set_string`. Three reasons, not taste: (1) `entry_handle` already names
the type in the method (`set_string`/`set_char`/`set_int`/`set_decimal`); `set_bytes` is the same
discipline, not a new one. (2) The C-ABI already made this exact choice — `src/capi/
message_write.cpp`'s `fixpp_msg_set_bytes` sits beside `fixpp_msg_set_string` as a distinct,
type-agnostic entry point ("No dict type check for set_bytes (type-agnostic escape)", the
function's own comment); `body_builder` gains the C++-primitive twin of a shape the C-ABI has
shipped since before #418. (3) A distinct name is a literal token a codegen-level census witness
(§5) can grep for, unlike an overload resolved by argument type — this repo's census-gate precedent
(`215-dictionary-view.md` §6 seam 7) wants exactly that.

Both new members call `append_bytes_field` directly — the same no-content-guard primitive
`append_int_field`/`append_decimal_field` already use for pre-encoded bytes; `append_string_field`
is what layers `is_clean_field_value` on top of it today, and stays untouched.

**INV-2 stays exactly as strong as it is today — this adds a second, narrower entry point, it does
not widen the first one.** `append_string_field` runs two conjuncts: `is_framing_tag` and
`is_clean_field_value`. `field_bytes`/`set_bytes` keep the first (no caller can smuggle tag `8`/`9`/
`34`/`49`/`52`/`56`/`10` through either path) and drop only the second, because on this path
SOH-safety comes from the paired Length prefixing the value, not from scanning its content.
`field()`/`set_string` are untouched; every existing STRING-field caller keeps its exact guard. This
is the claim the issue's stage-two tests must prove empirically (SOH inside a `field_bytes` value
frames correctly via the Length — §5), not merely assert by construction.

**Length/Data coupling is not enforced by `body_builder` today, and this does not add that
enforcement.** `body_builder` has no dictionary edge (its own header: "no wire->dictionary edge,
data-model §1" — the same reason `group_begin`'s `delimiter_tag` is author-supplied with no INV-5
pre-check). A hand-written caller is already trusted to call `field(len_tag, count)` and
`field(data_tag, value)` as a matched pair; `field_bytes` extends that same trust. Nothing rejects a
lone Data without its Length, or vice versa, at the `body_builder` level — exactly as nothing does
today for the existing string-coupled path.

**Misuse of `field_bytes` on a non-DATA field is likewise not prevented at the `body_builder`
primitive level, at compile time or run time — same trust boundary.** `body_builder` cannot know a
tag's FIX type without the dictionary edge it deliberately lacks. The **codegen** level is where
both of these become structural instead of trusted: `resolve_level`'s pre-scan
(`length_to_data`/`data_tags_to_skip`) only ever builds a coupled Args member when both tags are
declared together in the dictionary at that level, and `emit_level_body` only ever calls
`field_bytes`/`set_bytes` from that one coupled branch — generated code never routes a plain STRING
field there, and never emits a lone Data or lone Length. A hand-authored caller bypassing the
generator could still misuse `field_bytes`, no differently than one misusing `field()` with a wrong
tag today. §5's codegen census witness makes "generated code never does this" checkable, not merely
asserted.

**The two size caps are unaffected; this design proposes changing neither.** `kBodyCap` (3800 B,
serialized-body scratch) and `kArenaCap` (16384 B, accumulator-tree arena, null upstream —
exhaustion throws) already fail closed to `wire_frame_too_large`/caught-`bad_alloc` on overflow, and
`field_bytes` reuses `append_bytes_field`'s rollback path unchanged. Arbitrary-bytes `EncodedText`
is exactly the field likely to carry multi-KB payloads (089's charset arm is the motivating
consumer), so a message previously rejected for being non-ASCII may now be rejected for being
oversized instead — a different, already-correct failure mode, not a new one. Widening either cap
is **out of scope**: it governs which inputs succeed and needs its own review against the real
arena sizing (per this repo's own reserve-from-input-size anti-pattern).

---

## 3. (b) The codegen builders

Two files in `tools/codegen/fixpp-codegen/` change:

- **`gen_util.hpp`** — add `TypeKind::Bytes` (alongside `String`/`Char`/`Bool`/`Int32`/`Decimal`/
  `Skip`) and `BuilderCallKind::Bytes`, with `builder_call_kind` routing `Bytes -> Bytes`. `args_cpp_
  type`/`kind_cpp_type` need no *forced* change (both already `default:`/`case TypeKind::String:` to
  `::std::string_view`), but an explicit `case TypeKind::Bytes:` arm is clearer than a fallthrough
  default and should be added for grep-ability.
- **`emit_builders.cpp`**, two sites in the coupled branches: `resolve_level`'s coupled-item
  construction sets `item.kind = TypeKind::String;` unconditionally today — change to
  `TypeKind::Bytes`. (Inference, not exhaustively verified: every `length_pair_data_tag` target
  in-tree is a `field_data_type::Data`/`::XmlData` field, since that pairing is how FIX's `DATA` type
  is declared; a defensive alternative branches on the Data field's actual `ref.type` at the same
  site.) `emit_level_body`'s coupled branch picks `data_call` as `"bb.field("` (top level) or
  `owner_expr + ".set_string("` (nested) for the Data half — change **both** arms to
  `"bb.field_bytes("`/`owner_expr + ".set_bytes("`, converting the `std::optional<std::string_view>`
  Args member to a byte span at the call site (mirroring `body_builder.cpp`'s own `as_bytes` helper)
  rather than changing the Args struct member's C++ type, so every existing generated `<Msg>Args`
  struct stays source-compatible (`args.encoded_text = "hello world";` still compiles). **Both arms
  must change together** — fixing only the top level (or only nested-group Data members) reproduces
  this repo's own asymmetric-restructure defect; a Data member nested inside a repeating group is
  just as real a case as a top-level one. The stale `gen_util.hpp` comment claiming the Data half's
  `kind_of()` "is already String" needs updating alongside the code.

**Regeneration size — measured at `27c89864`, not guessed.** Generated builder sources are not
checked into `include/`; the tracked reference copies are the golden fixtures under `specs/
078-precompiled-builder-libs/contracts/golden/`. Grepping that tree for `r_data` (the local variable
`emit_level_body` gives the Length half's result — a token appearing only inside a coupled
Length+Data emission) finds the coupled-field builder sources per dictionary version:

| version | golden files with a coupled Length+Data emission |
|---|---|
| v42 | 74 |
| v44 | 154 |
| v50sp2 | 286 |
| vlatest | 0 (open question below) |
| **total** | **514** |

`specs/003-dictionary-codegen/contracts/golden/` and `specs/076-fix-latest-typed-codegen/contracts/
golden/` were also checked (4 and 3 files) — neither holds builder-call output (earlier-feature
message/field-enumeration goldens), zero `r_data` hits, not part of this count.

Two distinct, re-derivable populations: the **build-tree `_codegen` output** regenerates
automatically once the emitter binary is rebuilt (`cmake --build build/<preset> --target
fixpp-codegen && rm -rf build/<preset>/_codegen && cmake --build build/<preset>`, not a plain
incremental build — this repo's codegen-freshness discipline). The **514 checked-in golden files**
need a manual regeneration pass plus a `git diff` review, since they exist to catch generator-output
drift and would otherwise report this fix itself as drift.

**Open question — why does `vlatest` show zero?** The source dictionary declares the pairing:
`dictionaries/orchestra/OrchestraFIXLatest.xml` has `EncodedText` (`id="355"`) with `type="data"`
and `lengthId="354"`. The `vlatest` golden tree simply emits no coupled builder call anywhere — not
a byte-guard question, not investigated further here; flagged for the orchestrator.

---

## 4. (c) The C-ABI and Python surfaces

**The C-ABI needs no new work — it already has the primitive this issue asks `body_builder` for.**
`fixpp_msg_set_bytes` (`src/capi/message_write.cpp`) is a "type-agnostic escape": framing-tag reject
only, no dictionary type check, no content guard. It is not merely un-coupled from `body_builder`
(`body_builder` does not appear anywhere under `src/capi/` or `bindings/` — the C-ABI's
`OutboundAccumulator` is a structurally-parallel but wholly independent implementation, per
`body_builder.hpp`'s own header, "Mirrors the C-ABI OutboundAccumulator shape ... as a standalone
C++ primitive") — it already ships the capability #418 wants. One architectural note, not a defect
to fix here: `fixpp_msg_set_string` carries **no** `is_clean_field_value`-equivalent guard either
(only a null check and `check_outbound_msg`; its own comment says "String → always OK ... no
dict-type restriction"), so the C-ABI's STRING/DATA split does not mirror `body_builder`'s INV-2
guard at all. That asymmetry pre-dates #418 and is out of scope for it.

**The Python binding does not expose `fixpp_msg_set_bytes`.** `bindings/python/fixpp.i` declares
only `fixpp_msg_set_string` from the C-ABI's `set_*` family; grepping all of `bindings/` for
`set_bytes` finds nothing. Python callers have no way to set a raw-bytes `DATA` field today, even
though the C-ABI primitive exists. Exposing it is a small, separable SWIG addition — named here as
**explicitly deferred**: not needed to close #418 (scoped to `body_builder`/the codegen builders),
a separate small follow-up if a Python caller ever needs it.

---

## 5. (d) The witness plan

**Stage two (issue #418 item 3, second half) — flip the four stage-one pins.** Once `field_bytes`/
`set_bytes` land, `DataField_EncodedText_{SOH,ControlByte,0x80,0xFF}_RejectedOutOfRange_418` in
`tests/session/test_067_builder_failclosed.cpp` each invert: `build_NewOrderSingle` must succeed,
`EncodedTextLen(354)` must equal the byte count, `EncodedText(355)` must carry the bytes verbatim —
assert the exact byte sequence including the embedded SOH/control/high-bit byte, not just "no
error". Add a re-parse leg beyond what stage one needed: feed the built frame back through the
read-side parser and assert the round-tripped bytes are identical to the input, proving the
Length-prefixed framing keeps the embedded SOH from being mistaken for a delimiter by a real reader.

**A discriminating mutation-test pass on the fixed code**, mirroring this task's stage-one mutant: a
mutant routing `field_bytes` through `append_string_field` instead of `append_bytes_field`
(accidentally re-applying `is_clean_field_value`) must turn the flipped stage-two tests RED;
reverting must turn them GREEN with a byte-identical diff — the check that the fix actually removed
the guard rather than merely returning a different error.

**Codegen-level witness.** A census over regenerated sources: every `field_bytes(`/`.set_bytes(`
call site in generated output must resolve (by tag) to a field whose dictionary `FieldRef::type` is
`Data`/`XmlData` — never emitted for a plain STRING field. Grep-based, proven non-zero on a
*deliberately* mis-wired tree first (per the `215-dictionary-view.md` census-gate precedent — a
census that cannot fire on a known violation is worse than none), not merely asserted clean once.

**INV-2-preservation regression.** `SohInValue_RejectedBeforeAnyByteReachesOut` (the existing
STRING-field case, `ClOrdID(11)`) must keep passing unmodified through the whole fix — proving
`field()`/`set_string`'s guard was not weakened. No new assertion needed; it must stay GREEN,
unedited.

---

## 6. (e) `L-067-2`'s disposition — two distinct events, not one

**This commit (stage one)** makes `L-067-2`'s existing citation of `test_067_builder_failclosed.cpp`
as its pin **true**. It is **false today**: per issue #418 item 4, no test before this commit
exercised a `DATA` field with a non-clean byte. `L-067-2`'s prose itself (the `0x20-0x7E` range,
already corrected per the issue by fixpp `66655cd8`) needs no further wording change — only its test
citation becomes accurate.

**The fix (stage two, a later commit)** does not reword `L-067-2` in place — it **moves the row** to
`spec/behaviors-and-limitations-closed.md`, per this repo's live/closed convention. The closed-file
entry should carry both stages' evidence: the stage-one pin commit and the stage-two fix
commit/PR, so a future reader sees the limitation was pinned before it was closed.

---

## 7. (f) Out of scope

- **Whether STRING fields should ever admit high-bit bytes is not decided here** — the issue says so
  explicitly. `field()`/`set_string`'s `is_clean_field_value` guard is untouched in every respect.
- **Widening `kBodyCap`/`kArenaCap`** — not proposed (§2).
- **Exposing `fixpp_msg_set_bytes` from the Python binding** — a separate follow-up (§4).
- **The C-ABI's `fixpp_msg_set_string` having no SOH-injection guard at all** — a pre-existing
  architectural difference, noted (§4) but not this issue's job to reconcile.
- **Why `vlatest`'s golden builder output shows zero coupled Length+Data emissions** despite the
  source dictionary declaring the pairing (§3) — flagged, not resolved.
