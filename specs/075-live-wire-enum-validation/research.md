# Phase 0 Research: Live-Wire Enum-Value Validation (075)

**Date**: 2026-07-14 | **Branch**: `075-live-wire-enum-validation` | **Spec**: [spec.md](./spec.md)

All source anchors below were read on this branch at `ea02eebc` (parent `90f48a3e`). Every "QuickFIX does X" claim cites `reference-engines/quickfix-cpp` (v1.16.0, already built locally — `lib/libquickfix.so`).

---

## R-1 — Where the enum domain lives for the validator: `table_view` OWNS copies

**Decision**: `dict::table_view` stores its **own** copy of the code sets. It does **not** alias the Dictionary's `name_pool_`.

**Rationale**: `table_view` is today **fully self-owning** and aliases nothing external — `valid_`, `required_`, `group_first_`, `group_members_`, `group_ctx_`, `types_` are all `std::unordered_map`s of owned values, and `add_valid_tag` even *copies* the msg_type key (`table_view.hpp:304-305`, `:376-405`). `dictionary.hpp:193-205` states the contract explicitly: *"the returned `table_view` **owns its tables**"*.

Aliasing `string_view`s into the Dictionary's metadata pool would make this the **first** external aliasing in `table_view`, silently coupling validator lifetime to Dictionary lifetime and contradicting a documented contract. `Session` holds `validator_` (which owns the `table_view`) as a `unique_ptr` member (`session.hpp:795`) built from `cfg_.dictionary->as_table_view()` (`session.cpp:1233-1234`) — a *separate* object. Today a `table_view` can legally outlive the Dictionary it was built from; option (a) would quietly make that a use-after-free.

**Cost of owning**: the copy happens once at `as_table_view()` (config time, `[const §XV.1]` — explicitly off the hot path). Worst case is FIX50SP2: 5565 codes, most 1–3 bytes → well under ~64 KB. Negligible.

**Alternatives rejected**:
- *Alias `string_view`s into `name_pool_`* — zero-copy, but breaks the owns-its-tables contract and creates a lifetime coupling that no test would catch until it did. Rejected.
- *Store descriptions too* — the validator never needs them. `EnumValueRef::description` is diagnostics-only (`dictionary.hpp:64-70`). Copy **values only**; halves the storage and narrows the surface.

---

## R-2 — Lookup shape: `unordered_map<tag, enum_domain>` + **sorted** codes, binary-searched

**Decision**:

```
struct enum_domain {
    std::vector<std::string> codes;   // sorted bytewise, deduped
    bool multi_value{false};          // MultiCharValue | MultiStringValue
};
std::unordered_map<std::uint16_t, enum_domain> enums_;   // absent tag == unconstrained
```

`enum_valid(tag, value)`: `enums_.find(tag)` → **absent ⇒ `true`** (FR-003, the anti-reject-everything floor); present ⇒ binary-search `value` (single) or each space-delimited token (multi) against `codes`.

**Rationale**: matches the file's existing idiom (`unordered_map` everywhere, not PMR). O(1) tag lookup + **O(log C)** code lookup. Sorting matters: `MsgType(35)` carries **92 codes in FIX44** and is present on **every single message**, so a linear scan would cost up to 92 `memcmp`s per message on the hot path; binary search makes it ~7. Sorting is free — we build the table once at config time.

**Allocation-free** (FR-007): comparing `std::string` against a `string_view` over the wire bytes allocates nothing; tokenization is done with `string_view::substr`/index arithmetic over the caller's buffer — **no** `std::string` materialization, **no** `vector<string>` of tokens. Note QuickFIX itself *does* allocate a `std::string` per token (`DataDictionary.h:269`); we match its **semantics**, explicitly not its allocation behavior.

---

## R-3 — The multi-value bit must be carried explicitly (FR-005)

**Decision**: `enum_domain::multi_value` is set by `as_table_view()` from the field's `field_data_type` (`MultiCharValue` | `MultiStringValue`).

**Rationale**: `table_view::types_` stores the **collapsed 7-value `field_type`** (`table_view.hpp:404`), into which both multi-value types fold to `String` (`field_type_from_data_type()`). So `field_type_of()` **cannot** distinguish a multi-value field, and FR-004's tokenization is *not satisfiable from the existing table*. The finer `field_data_type` is available on `FieldRef` (`field_ref.hpp:44-45`), which `as_table_view()` already walks to build `types_` — so the bit is free to capture there.

**Trap this defuses**: a plan that assumes `field_type_of()` suffices would tokenize nothing, and **false-reject every conformant `ExecInst(18)`** (8 such tags in FIX44, 10 in FIX50/SP1). This is the feature's highest-risk defect.

---

## R-4 — `XmlLoader` populates the SAME enum store; bind views after the pool is frozen

**Decision**: extend `XmlLoader`'s field parse to read `<value enum= description=>` children into `GlobalFieldInfo`, intern both strings into `name_pool_`, and bind `EnumValueRef`s + `enum_runs_` in the existing **post-`shrink_to_fit()`** binding pass. No new storage type — reuse 074's `enum_values_` / `enum_runs_` (`dictionary_internal.hpp:207-211`).

**Rationale**: the two loaders differ in pool discipline and the difference matters:
- `OrchestraLoader` computes an **exact** byte budget *including every codeset value and description* and `reserve()`s before interning (`orchestra_loader.cpp:603-625`), because it binds `string_view`s as it goes.
- `XmlLoader` interns via **offsets** (`NameSlice`), calls `shrink_to_fit()` to lock the data pointer (`xml_loader.cpp:853-855`), and only **then** binds `string_view`s against `name_pool_.data()` (`:857-865`). Its comment says outright: *"we may have grown it past pool_estimate"*.

So for `XmlLoader` the pool may grow safely — **provided** the new `EnumValueRef`s are bound in that same post-finalize pass and never earlier. Binding an enum view *during* the parse would dangle on the next reallocation. `pool_estimate` (`:646-658`) should still be extended with the code bytes as a perf hint, but correctness rests on the late binding, not on the estimate.

**Alternatives rejected**: a second, enum-only string pool (needless divergence from 074's shape); pointing views at the pugixml document (freed at end of load → guaranteed dangle).

---

## R-5 — Loader strictness matches QuickFIX (FR-017)

**Decision**: duplicate `<value enum="X">` → **dedupe** (union); `<value>` with **no `enum` attribute** → **throw** (`xml_parse_error` family, catch-discriminated per the 072/074 precedent); `<value>` with **no `description`** → **legal**, empty description view.

**Rationale**: QuickFIX stores codes in `std::set<std::string>` (`DataDictionary.h:90`, `addFieldValue` `:248`) — duplicates are silently absorbed; it **throws `ConfigError`** on a missing `enum` attribute (`DataDictionary.cpp:271-273`); `description` is read conditionally (`:276`), i.e. optional.

074's fail-closed-on-duplicate reflex is deliberately **not** applied: 074's guards were on **structural ids**, where a duplicate creates a genuine which-definition-wins ambiguity (silent *loss*). A duplicate **code value** is idempotent for a membership test — the resulting set is identical either way — so fail-closed buys no correctness and would reject third-party dictionaries QuickFIX loads.

**Measured 2026-07-14** — all nine shipped dictionaries: **0** duplicate codes, **0** missing `enum` attributes, **0** missing descriptions. Pinned by SC-011 so a regression in *our own* vendored data is still caught.

---

## R-6 — FR-008 (empty value): **exclude empty values from the enum check** — disposition (a)

**Decision**: an empty field value is **not** submitted to the enum-domain check. Its disposition is left exactly as it is on `main`.

**Rationale**: the spec's original "empty → out-of-domain → reason 5, matching QuickFIX" was **false**. QuickFIX's `iterate` calls `checkHasValue` **before** `checkValue` (`DataDictionary.cpp:168` vs `:172`), and `m_checkFieldsHaveValues` **defaults to `true`** (`DataDictionary.cpp:43`) — so an empty value throws `NoTagValue` → `SessionRejectReason_TAG_SPECIFIED_WITHOUT_A_VALUE` = **4** (`Session.cpp:1267-1268`) and **never reaches the enum check**.

fixpp has **no reason-4 slot at all**: `reject_reason_map.hpp` emits only 14/2/1/5/6. So routing empty values through the new enum check would **manufacture** a divergence (fixpp 5 vs QuickFIX 4), not achieve parity.

Disposition (a) keeps this feature's blast radius to the enum domain. fixpp's missing reason-4 is a **pre-existing** gap that 075 neither creates nor widens; recorded as a known limitation (**L-075-1**) rather than silently absorbed. (b) adding a reason-4 slot is correct but is *session-reject-mapping* work, not enum work, and would grow the diff into `core::error` + `reject_reason_map` + the session reject path — a separate feature. (c) shipping a 5-vs-4 divergence is strictly worse than (a), which has no divergence at all because the value never enters the check.

**Verification**: the FR-018 golden **must** include the empty-value case, so this decision is confirmed against measured QuickFIX behavior rather than against this analysis.

---

## R-7 — Parity proof: golden table generated from the real, already-built QuickFIX (FR-018/FR-019)

**Decision**: a checked-in generator (`tools/quickfix_enum_golden/`) links the **already-built** `reference-engines/quickfix-cpp` (`lib/libquickfix.so` + `include/` are present), runs the boundary corpus through `DataDictionary::validate`, and emits a checked-in golden table. CI asserts fixpp's verdicts against the **golden**, never against the gitignored tree.

**Flag pinning (FR-019)** — the generator MUST set every non-enum switch explicitly and record the settings **inside** the golden artifact:

| QuickFIX flag | Default | Setting for the golden | Why |
|---|---|---|---|
| `m_checkFieldsHaveValues` | `true` | **`false`** | Otherwise `checkHasValue` front-runs `checkValue` and the empty-value case reports reason 4 — conflating "tag has no value" with "value out of domain". Disabling it isolates the **enum** boundary, which is what the golden is for. (fixpp's own disposition for empty values is R-6/(a) and is asserted separately.) |
| `m_checkFieldsOutOfOrder` | `true` | `false` | Field order is not this feature's boundary; fixpp checks header order separately (`validator.hpp:113-136`). |
| `m_checkUserDefinedFields` | `true` | `false` | User-defined tags are out of scope. |
| `AllowUnknownMsgFields` | `false` | `false` | Keep unknown-field rejection on — matches fixpp's `field_valid_for` gate. |

**This is not a detail.** Leaving `m_checkFieldsHaveValues` at its default is *precisely* the mechanism that produced the false FR-008 parity claim: an unrelated switch silently front-ran the check under test. An unpinned generator would report false divergences and we would "fix" fixpp to match an artifact of QuickFIX's config.

---

## R-8 — Call sites, message scope, and what is NOT touched

- **Both sites, no new code at either.** `enum_valid` is already called at `validator.hpp:148` (top-level) and `:325` (group members). Making the check real requires **zero** change at the call sites — the entire behavior change lands inside `table_view::enum_valid` + `as_table_view`. Nested groups therefore inherit it for free (065/072 machinery).
- **Header and trailer fields are in scope for free** (FR-015). Validator Step 1 iterates **every** present field (`validator.hpp:139`, `msg.begin()`→`msg.end()` over the offset table), header included — structurally equivalent to QuickFIX's `iterate` over header+trailer+body (`DataDictionary.cpp:149-156`). FIX44 enum-backed header fields: `MsgType(35)`, `PossDupFlag(43)`, `PossResend(97)`, `MessageEncoding(347)`.
- **Unknown MsgType is unaffected.** `field_valid_for` (`validator.hpp:143`) runs *before* `enum_valid` (`:148`), so a message whose MsgType is absent from the dictionary is still rejected on the unexpected-tag arm. 075 does not change that disposition. *(Checked so it does not resurface at Gate B.)*
- **Admin + Logon are in scope** (FR-013, QuickFIX parity — `Session.cpp:1218-1229` validates before `nextLogon` at `:1231`). Reject(3)/Logout(5) stay exempt via the existing no-reject-loop guard.
- **Not touched**: C-ABI (frozen 1.5.0), Python bindings, `OrchestraLoader`'s existing enum path, codegen/`validate_<Msg>` (L-069-1 stays open), outbound validation.

---

## R-9 — Constitution: Article I §1 amendment (v0.6 → v0.7) rides this branch

**Decision**: amend Article I §1 to narrow the FIX-Latest post-1.0 carve-out from *"typed codegen, live wire validation, ApplExtID(1156)=303 differentiation, and session negotiation"* to *"typed codegen, ApplExtID(1156)=303 differentiation, and session negotiation"*, recording that dictionary-driven wire **validation** ships generically for all supported versions via 075.

**Rationale**: the check is dictionary-generic — a FIX Latest dictionary in a validating session gets enum checking as an automatic consequence of R-1/R-2, with **zero** FIX-Latest-specific code. Shipping it while the ratified baseline says it is post-1.0 would leave a shipped capability contradicting the constitution. Article XVIII §5's no-early-ship bar has no residual conflict once §1 is narrowed, because the scope ceases to be deferred.

MINOR bump per Article XX §4 (additive; no banned-pattern/perf/config change). Rides this feature's branch per the established Gate-A-fold deviation from §2's standalone-PR letter (precedents: 035, 043, 068, 069). The **feature-catalogue D-011 row**, which repeats the carve-out verbatim, MUST be corrected in the same pass.

---

## Open items carried into `/tasks`

| # | Item | Status / where resolved |
|---|---|---|
| O-1 | Does `as_table_view()` have each tag's `field_data_type` in hand (needed for the multi-value bit, R-3)? | ✅ **RESOLVED — yes.** `as_table_view()` (`dictionary.cpp:319`) already iterates `message_fields(mt)` → `FieldRef fr`, and `fr.type` **is** the full `field_data_type` (it is compared against `field_data_type::NumInGroup` at `:358`). So the enum domain is naturally built inside that same `all_fields` loop: for each `fr`, if `enum_values(fr.tag)` is non-empty, register `(fr.tag, codes, is_multi(fr.type))`. **Registration must be idempotent** — a tag recurs across many msg_types, so the same domain is offered repeatedly. *(Consequence: a tag with a code set but present in **no** message gets no domain ⇒ unconstrained ⇒ accept. Harmless and consistent: such a tag is already rejected by `field_valid_for` before `enum_valid` is ever reached.)* |
| O-2 | Confirm the FR-018 golden reproduces R-6's empty-value analysis (QuickFIX yields **5** with `checkFieldsHaveValues=false`, **4** with the default `true`). | Golden-generator task. **If the golden contradicts R-6, R-6 is wrong and the FR-008 decision is revisited — not the golden.** |
| O-3 | Measure `as_table_view()` build time + `table_view` footprint on FIX50SP2 (5565 codes) to *confirm* R-1's "negligible" claim rather than assert it. | Bench/verify task |
