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

**Rationale**: matches the file's existing idiom (`unordered_map` everywhere, not PMR). O(1) tag lookup + **O(log C)** code lookup. Sorting matters: `MsgType(35)` carries **93 codes in FIX44** (*corrected at Gate A round 1 — "92" was wrong in three documents; see R-11*) and is present on **every single message**, so a linear scan would cost up to 93 `memcmp`s per message on the hot path; binary search makes it ~7. Sorting is free — we build the table once at config time.

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

**CORRECTION (Gate A round 1) — "exclude from the enum check" is NOT "accept". The disposition is TYPE-ARM DEPENDENT, and that is the whole finding.**

Rule 2 skips the *enum* check; it does not end the message's journey. The value falls through to `check_field_type` (`validator.hpp:154`), whose empty-value behavior depends on the field's **type arm**:

| Field | `field_type` arm | fixpp (075) | QuickFIX (`checkFieldsHaveValues=false`) | Register row |
|---|---|---|---|---|
| `Side(54)` | `Char` → `value.size() != 1` (`validator.hpp:411-417`) | **reject**, `wire_field_value_out_of_range` → **5** | `set.find("")` → miss → `IncorrectTagValue` → **5** | **DV-1** — parity **by coincidence, via a different arm** |
| `ExecInst(18)`, `SettlLocation(166)` | `String` → **no** constraint (`validator.hpp:419-425`) | **ACCEPT** | **reject** → **5** | **DV-2** — genuine divergence |

So the single "empty-value" golden row the draft mandated would have **passed on `Char` and failed on `String`** — the worst outcome: a gate that looks half-green and encodes nothing. The corpus therefore carries **empty × `Char`** and **empty × `String`** as **two** rows, both **`asserted: false`** (characterization-only). Disposition (a) still stands — it is the narrowest option and it is what avoids manufacturing a 5-vs-4 divergence — but **L-075-1 now records two facts**: fixpp has **no reason-4 mapping** (`reject_reason_map.hpp:15-75` maps only 1/2/5/6/14), **and** fixpp's empty-value disposition is **type-arm dependent**.

**Verification**: the FR-018 golden **must** include both empty-value rows, so this analysis is confirmed against measured QuickFIX behavior rather than trusted. **If the golden contradicts this, the analysis is wrong and the decision is revisited — not the golden.**

---

## R-7 — Parity proof: golden table generated from the real, already-built QuickFIX (FR-018/FR-019)

**Decision**: a checked-in generator (`tools/quickfix_enum_golden/`) links the **already-built** QuickFIX, runs the boundary corpus through `DataDictionary::validate`, and emits a checked-in golden table **plus a manifest**. CI asserts fixpp's verdicts against the **golden**, never against the out-of-repo tree. **The golden is built FIRST (Phase 0.5) and its measured output DEFINES the divergence register** — it is not evidence for a parity claim written ahead of it (see plan.md § Phase ordering; root cause RC#1).

**Path — CORRECTED (Gate A round 1, finding O-4).** `reference-engines/` is **not in the library repo at all**. It sits at the **parent** repo root — `<parent>/reference-engines/quickfix-cpp/lib/libquickfix.so.17.0.0` (1.8 MB, **built** — the "already-built" premise is TRUE) — **outside the submodule's git boundary** (`git check-ignore` from the library root: *"is outside repository"*). So the library's own `.gitignore` says nothing about it, and the claim *"`reference-engines/` is gitignored — CI never sees it"* was **vacuous** — true by accident, not by rule. Replaced with a structural guard (**FR-024**): the generator's root is a **validated CMake cache variable** `FIXPP_QUICKFIX_ROOT` (default `${CMAKE_SOURCE_DIR}/../../../reference-engines/quickfix-cpp`), the target is **OFF by default**, and it **hard-errors** if the variable is set but `lib/libquickfix.so` is absent.

**Staleness — the gap that actually matters (finding C-5).** As originally specified the golden is a checked-in data file produced by a generator **CI never runs**: nothing detects a hand-edited, stale, or wrong-dictionary golden. This repo has been burned by exactly this class twice (`[[feedback_codegen_golden_exists_narrow_verify_misses_it]]`, `[[feedback_sanitizer_canary_must_be_proven_red]]`). **FR-024** closes it with three things: a **manifest inside the golden** (QuickFIX version + soname, every loaded dictionary's SHA-1, generator-source hash, corpus hash, the **dictionary topology**, and all four booleans); a **CI test asserting the manifest against the checked-in corpus/config**, which runs **without** `reference-engines/` and is what makes a hand-edited golden fail; and a **local regenerate-and-diff target** that fails on drift when the tree is present.

**What the CI manifest gate does NOT catch** *(narrowed at Gate A round 2 — finding O2-3)*: **drift against a newer QuickFIX.** `quickfix_version` + soname are **recorded but unverifiable in CI**, because CI has no QuickFIX — that is this design's own premise. Nothing in the tree can falsify the string `1.16.0`. The manifest gate catches **tree drift** (dictionary / corpus-input / generator-source hash) and a **careless hand-edit** (`golden_output_hash`); the *staleness* leg belongs entirely to the **regen-and-diff target**, which as originally specified was OFF by default with **no owner, no cadence and no gate** — i.e. it might never run again after Phase 0.5. It is therefore **bound to the per-release out-of-CI interop gate** (`[[project_release_interop_quickfix_fix8]]`, which already owns a local reference-engine tree): run it on every release-interop pass, and treat a diff as release-blocking. Relatedly, the four booleans **and** the topology MUST be set **in generator source**, not passed on the CLI — otherwise `generator_source_hash` records them without pinning them.

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

- **RE-ANCHORED (Gate A round 1, finding C-3 / root cause RC#2).** The draft said the two `enum_valid` call sites were `:148` (top-level) and `:325` (**group members**), and that *"nested groups inherit it for free (065/072 machinery)"*. **`:325` is NOT the group-member site**, and the 065/072 group machinery is **not** what covers group members. Exhaustive grep: `enum_valid` has exactly **two** production call sites, and neither is inside 072's Step-3 `consume_group` / `validate_group_level`:
  - **`validator.hpp:148`** — inside the **Step-1 walk** (`:139-158`). `msg.begin()`→`msg.end()` on a `MessageView<Index>` constructs a **`field_iterator` over the raw frame bytes** (`parser.hpp:229-233`; `advance()` at `:444` is a byte-level `tag=value` scanner). It is **dict-free and has no group awareness whatsoever** — group members are simply *bytes in the frame*, so the walk yields **header + trailer + body + group members at every depth**. *This is why group coverage exists, and it is a property of one flat loop, not of a second call site.* **(Verified directly this round, not inferred: had `begin()` been an offset-table walk over top-level entries only, group members would never be enum-checked and FR-006's group leg would be unsatisfiable without new code. It is not — it is a raw byte scan.)**
  - **`validator.hpp:325`** — inside **`validate_field(std::uint16_t tag, std::span<const std::byte> value)`** (`:323-330`), the **context-free public pure-virtual** declared at `:65-66`, with **no `msg_type` context**. A **third public surface** that begins enum-validating as a consequence of 075 — now specified and tested (**FR-020**), where the draft left it invisible.

  **Why the wrong anchor was load-bearing, not cosmetic**: (a) it corrupted a normative test seam — AC US1 #4 and quickstart S-4 both aimed the group-member witness at `:325`, so a `tasks.md` generated from the draft would have written a `validate_field()` **unit** test and called the group AC discharged (`[[feedback_witness_asserts_named_postcondition_not_proxy]]`); (b) believing in a dedicated group call site is *why nobody asked whether QuickFIX has one* — **it does not** (R-12).
- **Header and trailer fields are in scope where the dictionary declares them** (FR-015) — same Step-1 walk, same reason. Structurally equivalent to QuickFIX's `iterate` over header+trailer+body (`DataDictionary.cpp:149-156`). FIX44 enum-backed header fields: `MsgType(35)` (**93** codes — *corrected; "92" was wrong in three documents*), `PossDupFlag(43)`, `PossResend(97)`, `MessageEncoding(347)`. FIX50 / FIX50SP1 / FIX50SP2 declare **empty** `<header />` / `<trailer />` blocks (`FIX50.xml:2`/`:2694`, `FIX50SP1.xml:2`/`:2976`, `FIX50SP2.xml:2`/`:4674`) — **not absent ones**. `header_node_` / `trailer_node_` are assigned `root.child("header")` / `root.child("trailer")` (`xml_loader.cpp:621-622`), which return **valid, non-null** nodes for a present-but-empty element, so the null guard at `xml_loader.cpp:739`/`:743` **does not** fire and `xml_loader.cpp:738-745` **does** fold them — it just expands an element with **no children**, contributing **zero** fields. FR-015's header/trailer scope is therefore vacuous on those three dictionaries **by emptiness, not by absence**, and must not be written as if every shipped dictionary contributes dictionary-known header fields. *(Corrected at Gate A round 4 — C4-2. The earlier text asserted the blocks were **absent** and blamed the null guard; both legs were false, though the conclusion survives.)*
- **Unknown MsgType is unaffected.** `field_valid_for` (`validator.hpp:143`) runs *before* `enum_valid` (`:148`), so a message whose MsgType is absent from the dictionary is still rejected on the unexpected-tag arm. 075 does not change that disposition. *(Checked so it does not resurface at Gate B.)*
- **fixpp and QuickFIX check in OPPOSITE ORDER on {tag not valid for this msg_type} × {value out of domain} — recorded, deliberately NOT a register row.** *(Gate A round 4, finding O4-1. R-8 above noticed fixpp's ordering but reasoned only about an unknown **MsgType**, and never compared it to QuickFIX's.)* fixpp's Step-1 walk runs `field_valid_for` **first** (`validator.hpp:143` → `wire_unexpected_tag` → reason **2**) and never reaches `enum_valid` (`:148`). QuickFIX's `iterate` runs **`checkValue`** (`DataDictionary.cpp:172`) **before** `checkIsInMessage` (`:178`) → reason **5**. On a message frame carrying an out-of-domain value in a tag its msg_type does not admit, the two engines therefore return **different reject reasons** while agreeing on *reject*. This is **unobservable on the FR-018 corpus** — every legitimate row uses an **in-message** tag, so both engines reach the enum arm and agree on 5 — which is exactly why it must be written down: a corpus row on a **message-unreachable** tag would walk straight into it and be scored a spurious `asserted: true` divergence, i.e. a self-inflicted feature-blocking "defect" under SC-009. That is what happened to the short-lived `validate_field(1128, "bogus")` corpus row, now removed (FR-018). **Do not add a message-unreachable-tag row to the golden corpus.** No DV-* row is minted: fixpp's ordering is not a behaviour anyone must match, and there is no corpus row that measures it.
- **Admin + Logon are in scope** (FR-013, QuickFIX parity — `Session.cpp:1218-1229` validates before `nextLogon` at `:1231`). Reject(3)/Logout(5) stay exempt via the existing no-reject-loop guard.
- **Not touched**: C-ABI (frozen 1.5.0), Python bindings, `OrchestraLoader`'s existing enum path, codegen/`validate_<Msg>` (L-069-1 stays open), outbound validation.

---

## R-9 — Constitution: Article I §1 amendment (v0.6 → v0.7) rides this branch

**Decision**: amend Article I §1 to narrow the FIX-Latest post-1.0 carve-out from *"typed codegen, live wire validation, ApplExtID(1156)=303 differentiation, and session negotiation"* to *"typed codegen, ApplExtID(1156)=303 differentiation, and session negotiation"*, recording that dictionary-driven wire **validation** ships generically for all supported versions via 075.

**Rationale**: the check is dictionary-generic — a FIX Latest dictionary in a validating session gets enum checking as an automatic consequence of R-1/R-2, with **zero** FIX-Latest-specific code. Shipping it while the ratified baseline says it is post-1.0 would leave a shipped capability contradicting the constitution. Article XVIII §5's no-early-ship bar has no residual conflict once §1 is narrowed, because the scope ceases to be deferred.

MINOR bump per Article XX §4 (additive; no banned-pattern/perf/config change). Rides this feature's branch per the established Gate-A-fold deviation from §2's standalone-PR letter (precedents: 035, 043, 068, 069). The **feature-catalogue D-011 row**, which repeats the carve-out verbatim, MUST be corrected in the same pass.

---

---

## R-10 — Dictionary memory growth: which `memory_resource` actually receives the new data (discharges `checklists/requirements.md:50`)

*(Added Gate A round 1, finding O-6. The checklist explicitly handed `/plan` this task — *"**Confirm at `/plan`** that the arena/pool sizing absorbs this for every dictionary"* — and **neither `plan.md` nor `research.md` discharged it**. R-4 addressed only `name_pool_`; it said nothing about the two vectors that actually receive the enum data.)*

**The store that grows is not just the string pool.** `dict_metadata_handle` (`src/dictionary/dictionary_internal.hpp:90-103`) takes a `std::pmr::memory_resource* mr` and constructs **three** members from it: `name_pool_(mr)`, **`enum_values_(mr)`**, and **`enum_runs_(mr)`** (`:102-103`). For `XmlLoader` the growth in the latter two is **entirely new** — that store is **empty on all nine** QuickFIX dictionaries today.

**Magnitude** (worst case, FIX50SP2): `enum_values_` is a `std::pmr::vector<EnumValueRef>` = 2 × `string_view` = **16 bytes** × **5565** codes ≈ **89 KB**; `enum_runs_` is one `EnumRun{tag,start,count}` per enum-backed field (668) ≈ a few KB; **plus** the code + description bytes now interned into `name_pool_`.

**Findings / requirements:**

1. **`pool_estimate` is now short on every dictionary.** `xml_loader.cpp:646-658` sums message / component / field **names only**. After FR-001 it will under-estimate by the full code+description byte count. R-4 is right that *correctness* rests on the **late bind** (post-`shrink_to_fit()`, `:853-865`), not on the estimate — the pool may safely grow — but the estimate is the reserve hint the pool was designed around, so **FR-001's task MUST extend `pool_estimate`** with the code+description bytes. This is a perf hint, not a correctness gate.
2. **Reserve TIGHT, not loose.** Per `[[feedback_fixed_arena_over_reserve_silent_loss_larger_stl]]`: a loose `reserve()` into a fixed, null-upstream arena over-allocates, MSVC's larger STL exhausts it, and the result is a **silent drop**. `enum_values_` MUST be reserved at the **exact** measured code count, not a padded guess.
3. **Name the upstream.** The handle is `allocate_shared`'d over a `std::pmr::polymorphic_allocator` from the loader's `mr_` (`xml_loader.cpp:634-635`). The `/implement` task MUST **state, per loader, what `mr_` actually is** — `new_delete_resource` (unbounded, benign) or a bounded upstream (in which case the 89 KB is a real budget item and an under-sized arena surfaces as a silent truncation or a load-time alloc failure). This is the confirmation the checklist asked for and is a **named task**, not an open item.
4. **SC-006's footprint measurement is a NAMED task**, not an aspiration (was research O-3).

---

## R-11 — The census counted *shapes*, never *values*: `SettlLocation(166)` declares a prose placeholder as a code

*(Added Gate A round 1, findings O-3 + O-7 / root cause RC#3.)*

The Context census was excellent as far as it went — *how many tags, how many codes, which are multi-value* — and it **never once looked at what a code says**. Two defects came through that gap:

1. **`SettlLocation(166)`** in **FIX41** and **FIX42** declares the literal code **`"ISO Country Code"`** (with `CED, DTC, EUR, FED, PNY, PTC`). Type `STRING`, single-value. It is plainly a *documentation* entry — "any ISO country code here" — and after 075 a strict FIX 4.1/4.2 session **rejects `166=US` with reason 5**: conformant, real-world traffic. This is the *same class* the bundle rates **P1** for multi-value (*"does not merely under-validate — it **actively breaks conformant traffic**"*), and it appeared in **no** risk row, edge case, B-row or corpus row. Held at P2 only because **QuickFIX rejects `166=US` too** (`set.find("US")` → miss → 5), so fixpp is **parity-correct** — it is not a correctness defect, it is an **unenumerated operator-visible regression requiring a spec-level decision the bundle never posed.** **Decided: accept-and-document (FR-022, DV-4).**
2. **`MsgType(35)` has 93 codes in FIX44, not 92** — wrong in three documents (`spec.md` FR-015, `plan.md` Technical Context, `data-model.md` Entity B), all now corrected. **93** is also exactly FIX44's declared `<message>` count (which is why SC-010's census passes). The *conclusion* (sort + binary-search) was right; only the constant was wrong — but SC-002/SC-010 are **exact-count census** criteria, and per `[[feedback_completeness_gate_exact_set_not_subset]]` a wrong constant in three docs is precisely the thing that gets copied into an assertion.

**Measured this round across all ten shipped dictionaries**: `"ISO Country Code"` is the **only** declared code containing a space. Every other census figure re-verified exactly (FIX44 245/1708, FIX50SP2 668/5565, the eight FIX44 multi-value tags, FIXT11's zero-code `MsgType`, 0 duplicates / 0 missing `enum` / 0 missing `description`).

**Requirement**: **SC-011** is extended to assert that the set of declared codes containing a space, across all ten shipped dictionaries, is **EXACTLY** `{FIX41:166:'ISO Country Code', FIX42:166:'ISO Country Code'}` — any addition, removal or changed literal **fails the gate**. It is **GREEN today** (re-measured 2026-07-14 across all ten, Orchestra included: those two entries are the only space-bearing codes in the tree), and any *other* placeholder, present or introduced by a dictionary refresh, **fails the build**.

*(**Reformulated at Gate A round 2 — finding C2-1.** The round-1 wording asserted *"no declared code contains a space"* and then conceded, one sentence later, that the assertion **"fires today"** — a Success Criterion defined to be **RED**, which a correct implementation cannot turn green. An implementer resolves that contradiction one of two bad ways: a permanently-RED test that gets "fixed" under time pressure, or a **log** instead of an assert — `[[feedback_ci_gate_observes_not_asserts_witness_skips_into_green]]`, in the one gate whose whole job is to stop a dictionary refresh from silently converting a codeset into a reject-everything trap. The **decision** was never open; only the predicate was unwritable. The exact-set form is the executable version of the same intent — `[[feedback_completeness_gate_exact_set_not_subset]]`.)*

It also secondarily de-risks the tokenizer: the bundle considered a *wire value* containing a space (AC US2 #3) but never a **declared code** containing one, which is exactly what `166` is and what would be silently shredded if tokenization were ever extended beyond the multi-value bit.

**RC#3 RECURRED inside the round-1 rewrite — finding O2-1** *(Gate A round 2)*. This very section diagnosed *"the census reads shapes, never what the codes **say**"*, added the code-string census — and then the same rewrite authored FR-018 corpus **row 7** (*"`277=A`, where only `AX` is declared"*) **without measuring what tag 277 declares**. `TradeCondition(277)` in the shipped `FIX44.xml` **declares `A`** (codes `A`–`N`, `P`, `Q`, `R`) and does **not** declare `AX`: both engines therefore **accept** `277=A`, so the row — marked `asserted: true`, expecting a reject — was an **accept-accept row that passes green while testing nothing and that no mutation can redden** (every mutation the bundle specifies flips *reject* rows to accept). FR-009 would have shipped with **zero** discriminating witnesses. Re-based on **`MatchType(574)=A`** (FIX44 codeset `A1`–`A5`, `AQ`, `S1`–`S5`, `M1`, `M2`, `MT`, `M3`–`M6`; **no bare `A`**; `STRING`, **single-value**, non-header, top-level in `TradeCaptureReport(AE)`), and **every** asserted literal in the corpus was re-measured against the shipped XML — see FR-018's audit table.

**The generalized rule, stated narrowly** (the broad version — *"expected verdict equals the stub's behaviour"* — would wrongly condemn the legitimate **accept**-asserting rows 1 and 3): **a `reject`-asserting row that BOTH engines ACCEPT is a defect, not a row.** It coincides exactly with the stub (`enum_valid → return true` accepts everything), it does **not** exercise the rule it names, and **none of the mutations this bundle specifies can redden it** — all of them flip *reject* rows to accept, never the reverse. The accept-asserting rows are the paired **positive controls** against **over**-rejection (row 3 reddens under the tokenizer mutation; row 1 is row 2's accept-side partner) and are unaffected.

---

## R-12 — QuickFIX does NOT enum-check repeating-group members, and its FIXT topology is not fixpp's (the two parity misses)

*(Added Gate A round 1, findings O-1 + O-5 / root cause RC#1. Both live **one frame above** `checkValue` — in `iterate` and `validate` — which is the frame nobody read. That is the same mechanism by which `m_checkFieldsHaveValues` front-ran the check unnoticed and produced FR-008's original false parity claim: the bundle's own cautionary tale, repeated twice more.)*

### (a) Group members — **a declared divergence (DV-3), not a "free inheritance"**

**Closed call-site census of QuickFIX v1.16.0** (exhaustive at every step, not a spot read):

1. `isFieldValue` (`DataDictionary.h:255-276`) has **exactly one** caller: `checkValue` (`:493-501`). It is the sole gateway to the enum domain check.
2. `checkValue` has **exactly one call site in the entire engine**: `DataDictionary.cpp:172`, inside `DataDictionary::iterate`'s per-field loop.
3. `iterate` occurs exactly 4× in `DataDictionary.cpp`: its definition (`:159`) and **three** calls (`:150`, `:151`, `:155`) — all inside `DataDictionary::validate`, over `getHeader()`, `getTrailer()`, and the body `FieldMap`. **Zero recursive self-calls.**
4. `iterate`'s body (`:159-184`) has one loop, `for (i = map.begin(); i != map.end(); ++i)` — **no** `g_begin()`/`g_end()` traversal, no recursion, no second `checkValue`.
5. `FieldMap::begin()/end()` return **`m_fields`** iterators (`FieldMap.h:233-236`). Repeating-group instances live in a **separate** member, **`m_groups`**, reachable **only** via `g_begin()/g_end()` (`:237-240`) — which, per (4), `iterate` **never calls**.

⇒ **A group-member field's `FieldBase` lives in `m_groups`, is never passed to `iterate`, and can therefore never reach `checkValue` or `isFieldValue`.** The only group-aware check in the whole path is `checkGroupCount` — a **count** check, not a domain check.

⇒ **fixpp will be strictly STRICTER than the reference engine**, on the very requirement FR-006 declares mandatory (its Step-1 byte scan reaches group members at every depth — R-8).

**Decision: KEEP the check.** Rejected alternative — *suppress it for group members to match QuickFIX* — means writing deliberate, artificial code to make a **correct** check not run, on exactly the fields (party roles, leg sides, MD entry types) where a domain error is most costly; QuickFIX's behaviour here is a known weakness, and fixpp's flat walk makes the check free. But it MUST be **argued, B-rowed and registered** (**FR-023**, **DV-3**), and **removed from SC-009's parity assertion set** — not smuggled in under a *"nested groups inherit it for free"* claim while SC-009 simultaneously declared the empty-value case *"the one known divergence"*. That sentence was **false**, and the FR-018 corpus — which had **no group-member row at all** — was *structurally incapable* of surfacing it. A green golden would have "proved" a parity that does not exist. **Corpus rows 10 (group) and 11 (nested group) exist for exactly this reason.**

### (b) Dictionary topology — pin the golden to the single-DD (non-FIXT) path, per dictionary

`DataDictionary::validate` (`DataDictionary.cpp:145-156`) takes **two** dictionaries and splits the work:

```cpp
if (pSessionDD != 0) { pSessionDD->iterate(message.getHeader(), msgType);
                       pSessionDD->iterate(message.getTrailer(), msgType); }
if (pAppDD != 0)     { pAppDD->iterate(message, msgType); }
```

`Session.cpp:1221-1229` selects that two-DD path whenever `m_sessionID.isFIXT() && message.isApp()`. So on a **FIXT/FIX50SP2** session QuickFIX enum-checks the **header** — including `MsgType(35)` — against **FIXT.1.1**, whose `MsgType` declares **zero** `<value>` children ⇒ under its own no-codeset-⇒-accept rule, `MsgType` is **completely unconstrained on any FIXT session**.

**fixpp has exactly ONE dictionary** — `SessionConfig::dictionary` (`src/session/engine.cpp:210` rejects `validate_inbound_messages && dictionary == nullptr`; the validator is built from a single `cfg_.dictionary->as_table_view()`, `src/session/session.cpp:992`, `:1234`). Loading `FIX50SP2.xml` means `MsgType(35)` is checked against **its** codeset — header *and* body alike.

⇒ A golden generated against a **FIXT-configured** QuickFIX measures a validation topology **fixpp does not have**; one generated against the **single-DD** path (`sessionDataDictionary.validate(message)`, `Session.cpp:1228` — the FIX 4.x path) **does** match fixpp. **FR-019 pins the single-dictionary (non-FIXT) topology — `sessionDD == appDD` — applied per the corpus row's own dictionary**, and **FR-024** records it in the manifest **per dictionary**, alongside each `dictionary_sha1` and the four booleans.

*(**Corrected at Gate A round 2 — finding O2-6.** The round-1 pin read *"single-dictionary **FIX 4.4** topology"*, naming a **version**, while the corpus provably spans more than one dictionary: the `SettlLocation(166)=US` row (**row 12**; numbered 13 until Gate A round 4 dropped the `validate_field` row — O4-1) is on **FIX41/FIX42**, and FR-024's `dictionary_sha1` is defined over **every** dictionary the corpus loads. The property actually being pinned is `sessionDD == appDD` — the non-FIXT path — which holds for **every** 4.x dictionary. The substance was always right; the manifest field was under-specified, which is precisely the class of loose pin FR-019 exists to prevent.)*

**Load-bearing corollary for FR-016/SC-010**: the reasoning *"FIXT11's `MsgType` has zero codes, so its 8 message types pass only via the empty-set arm"* is correct for fixpp **only because fixpp never composes FIXT11 with an app dictionary**. That is an **unstated assumption made explicit here**: a future FIXT/app-DD split would require FR-016 to be revisited.

---

## Open items carried into `/tasks`

| # | Item | Status / where resolved |
|---|---|---|
| O-1 | Does `as_table_view()` have each tag's `field_data_type` in hand (needed for the multi-value bit, R-3)? | ✅ **RESOLVED — yes, but only for MESSAGE-REACHABLE tags.** `as_table_view()` (`dictionary.cpp:319`) already iterates `message_fields(mt)` → `FieldRef fr`, and `fr.type` **is** the full `field_data_type` (it is compared against `field_data_type::NumInGroup` at `:358`). The corrected projection therefore splits in two: **(1)** iterate the dictionary's own enum store (`dict_metadata_handle::enum_runs_`, `dictionary_internal.hpp:207-211`) so **every** enum-backed tag gets a domain, including tags absent from `message_fields()`; **(2)** use the existing `all_fields` walk only to set `multi_value=true` for the reachable tags whose `fr.type` is `MultiCharValue` / `MultiStringValue`. A tag that appears in the enum store but in **no** message expansion therefore still gets a domain, and defaults to **`multi_value=false`** because there is no global tag→`field_data_type` store today. That default is **measured-safe on the shipped dictionaries**: the **35** message-unreachable store-backed tags (**10 / 11 / 14** on FIX50 / FIX50SP1 / FIX50SP2; **zero** on FIX40–FIX44 and FIXT11 — re-measured at Gate A round 4, C4-1) are typed `STRING` / `BOOLEAN` / `INT` / **`CHAR`**, **none** of them `MULTIPLEVALUESTRING` / `MULTIPLESTRINGVALUE` / `MULTIPLECHARVALUE`, and SC-011 is extended to pin that so a future dictionary refresh turns the gate RED instead of silently false-rejecting a conformant multi-value field. The earlier exculpatory clause was also wrong on the public surface that matters here: **`validate()`'s Step-1 path does run `field_valid_for` before `enum_valid`, but `validate_field()` (`validator.hpp:323-329`) calls `enum_valid` first and has no `field_valid_for` precheck.** *(Confirms `FieldRef` stays **byte-identical** — 074's signed-off invariant is not violated.)* |
| O-2 | Confirm the FR-018 golden reproduces R-6's empty-value analysis — **now two rows**: QuickFIX yields **5** for empty × {`Char`, `String`} under `checkFieldsHaveValues=false`; fixpp yields **5** (Char, via its *type* arm) and **accept** (String). | **Phase-0.5 golden task** (blocking). **If the golden contradicts R-6, R-6 is wrong and the FR-008 decision is revisited — not the golden.** |
| O-3 | Measure `as_table_view()` build time + `table_view` footprint on FIX50SP2 (5565 codes) to *confirm* R-1's "negligible" claim rather than assert it. | **Promoted to a named task** (plan.md test matrix, SC-006 row) — was an open item, which finding O-6 correctly called out as insufficient. Bench/verify. |
| O-4 | Does the golden reveal a divergence **not** in the DV-1..DV-4 register? | ✅ **ANSWERED — YES, IT DID. Resolved at T006, 2026-07-14.** The golden's **first real run** revealed exactly such a divergence, and this row is why it was handled rather than shipped. QuickFIX's `checkValidFormat` (`DataDictionary.cpp:171` — the field's **type convertor**, reason **6**) runs **immediately before** `checkValue` (`:172`, the enum arm). **The bundle cited `:172` and `:178` throughout five Gate A rounds; `:171` is one line above the line it quoted.** fixpp has no generic bad-format slot (reason 6 = Float-precision-loss only), so it returns **5** where QuickFIX returns **6**. Outcome: corpus row 6 re-based `PossDupFlag(43)=X` → **`MessageEncoding(347)=ZZZZ`** (`STRING` ⇒ both engines reach the enum arm ⇒ 5); new **DV-5** with `asserted: false` corpus row **13**; **DV-1 corrected** (QuickFIX rejects 6, not 5); **fixpp unchanged**. DV-2/DV-3/DV-4 confirmed by measurement. The register is the golden's **output**, not the spec's input — and that is precisely what saved this feature. |
