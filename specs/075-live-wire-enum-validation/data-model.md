# Phase 1 Data Model: Live-Wire Enum-Value Validation (075)

**Date**: 2026-07-14 | **Branch**: `075-live-wire-enum-validation` | **Plan**: [plan.md](./plan.md)

Two entities, one new. The dictionary-side store already exists (074); the validator-side projection is new.

---

## Entity A — Code set (dictionary side) — **EXISTS, extended in coverage only**

074 shipped this; 075 changes **who populates it**, not its shape.

| Element | Type | Source | Notes |
|---|---|---|---|
| `EnumValueRef{value, description}` | `struct` of 2 × `string_view` | `include/fixpp/dict/dictionary.hpp:68-71` | Views alias the metadata handle's `name_pool_`. `description` is **diagnostics-only** — the validator never reads it. |
| `dict_metadata_handle::enum_values_` | `std::pmr::vector<EnumValueRef>` | `src/dictionary/dictionary_internal.hpp:210` | Flat store, all tags concatenated. |
| `dict_metadata_handle::enum_runs_` | `std::pmr::vector<EnumRun{tag,start,count}>` | `dictionary_internal.hpp:74-78`, `:211` | **Sorted by tag**; binary-searched by `enum_values_impl`. |
| `Dictionary::enum_values(tag)` | `std::span<EnumValueRef const>` | `dictionary.hpp:190` | Read-only accessor. Empty span ⇒ tag has no code set. |

**Change in 075**: `XmlLoader` populates these for the nine QuickFIX dictionaries (today it leaves them **empty** — it never reads `<value>` at all). `OrchestraLoader`'s path is untouched.

**Invariant (load)**: every `string_view` in `enum_values_` is bound **after** `name_pool_.shrink_to_fit()` (`xml_loader.cpp:853-865`), using stable `NameSlice` offsets. Binding earlier dangles on the next pool reallocation. *(This is the one lifetime rule of the whole feature.)*

**Doc corrections required — SEVEN blocks** *(census completed at Gate A round 1 — finding O-8; a **seventh** block added at Gate A round 2 — finding O2-4. The round-1 census presented itself as exhaustive ("all six") while missing the doc block on `enum_values()` **itself** — the very accessor the whole feature is built on, sitting 18 lines above a block the census did catch, in the same file.)*:

| # | File:line | Says today | After 075 |
|---|---|---|---|
| 1 | `include/fixpp/dict/dictionary.hpp:66-67` | *"Populated **only by OrchestraLoader**; XmlLoader-produced dictionaries carry an empty enum store."* | **FALSE** — both loaders populate it |
| 2 | **`include/fixpp/dict/dictionary.hpp:184-189`** (the `enum_values()` doc block) — **added at Gate A round 2, O2-4** | *"Empty span if `tag` has no codeset, or for any dictionary that does not populate the enum store **(all XmlLoader/QuickFIX dictionaries)**. Additive, read-only, **orthogonal to `table_view::enum_valid` (unchanged Phase-1 stub)**."* | **TWO statements go FALSE**: (i) the nine XmlLoader dictionaries **do** populate the store (FR-001); (ii) `enum_valid` is no longer a stub and reads **exactly this data** — the opposite of orthogonal (FR-002/FR-003) |
| 3 | `include/fixpp/dict/dictionary.hpp:195-200` | *"all validator method calls on it are **O(1)** and alloc-free"* | **O(log C)** for `enum_valid` (alloc-free still holds) |
| 4 | `include/fixpp/dict/dictionary.hpp:195-200` | *"`enum_valid` is **stubbed to true** (FR-005, enum tables deferred to 2c work)"* | **FALSE** — it is the real check |
| 5 | `include/fixpp/wire/reject_reason_map.hpp:22-23` | *"type-arm only in Phase-1 since **the enum arm is dead** — `enum_valid`→true"* | **FALSE** — the enum arm is live. *(This is the comment the spec's Context quotes as fact #2.)* |
| 6 | `include/fixpp/wire/reject_reason_map.hpp:60-61` | *"Reason 5: value is incorrect (not type-conformant — type arm; **enum arm is dead Phase-1**)."* | **FALSE** — same |
| 7 | `include/fixpp/wire/reject_reason_map.hpp:1-19` (doc block) | the same Phase-1 framing | **FALSE** — same |

All seven must be corrected in the same commit that makes them false. `reject_reason_map.hpp` takes **no code change** — comments only — which is exactly why it is easy to miss and why it is now in `plan.md`'s file inventory.

---

## Entity B — Enum-domain table (validator side) — **NEW**

Lives inside `dict::table_view`. **Owned** (research R-1) — copies the code bytes; aliases nothing.

```
struct enum_domain {
    std::vector<std::string> codes;   // sorted bytewise ascending, deduped
    bool multi_value{false};          // field_data_type ∈ {MultiCharValue, MultiStringValue}
};

std::unordered_map<std::uint16_t, enum_domain> enums_;   // tag → domain
```

| Field | Rule |
|---|---|
| `codes` | **Values only** — descriptions are not copied (the validator never needs them; halves the footprint). **Sorted** so lookup is O(log C): `MsgType(35)` has **93** codes in FIX44 (*corrected at Gate A round 1 — "92" was wrong here, in `spec.md` and in `plan.md`*) and appears on *every* message, so a linear scan would be up to 93 `memcmp`s per message on the hot path. **Deduped** (FR-017 — QuickFIX's `std::set` semantics). |
| `multi_value` | For a **message-reachable** tag, set from the field's `field_data_type`, **not** from `table_view::types_` — the collapsed 7-value `field_type` folds both multi-value types into `String` (`table_view.hpp:404`), so the bit is unrecoverable from the existing table (FR-005). For a tag present in the enum store but absent from `message_fields()` (the FIX50/FIX50SP1/FIX50SP2 headerless gap), `multi_value` is pinned to **`false`**: there is no global tag→`field_data_type` store today, and the shipped dictionaries' store-only tags are measured-safe — **35** of them (**10 / 11 / 14** on FIX50 / FIX50SP1 / FIX50SP2, zero elsewhere; re-measured at Gate A round 4, C4-1), typed `STRING` / `BOOLEAN` / `INT` / **`CHAR`** (the `CHAR` is `MsgDirection(385)`), **none** multi-value. SC-011 asserts that this remains true so a future dictionary refresh fails the gate instead of silently turning a conformant multi-value field into a false reject. |
| absent tag | **Unconstrained ⇒ accept.** The `enums_.find(tag) == end()` branch is the anti-reject-everything floor (FR-003) and is what keeps FIXT11 working at all (its `MsgType` declares zero codes). |

**Populated by**: `Dictionary::as_table_view()` (`src/dictionary/dictionary.cpp`) — once, at config time (`[const §XV.1]`). The corrected projection is **store-driven**: iterate `dict_metadata_handle::enum_runs_` / `enum_values_` first so every enum-backed tag in the dictionary gets a domain, then overlay the `multi_value` bit from the existing `message_fields(mt)` walk where `FieldRef` data exists. Immutable thereafter; no mutex (`[const §XV.9]`).

**Footprint**: worst case FIX50SP2 — 5565 codes, most 1–3 bytes ⇒ well under ~64 KB (to be *measured*, not asserted — research O-3).

---

## State & lifecycle

```
XmlLoader::load / OrchestraLoader::load        [load time, once per dictionary]
   └─► dict_metadata_handle{ enum_values_, enum_runs_ }        (Entity A)
          │
          │  Dictionary::as_table_view()        [config time, once per session — session.cpp:1233]
          ▼
       table_view{ enums_ }                                     (Entity B, OWNED copy)
          │
          │  dictionary_driven_validator::validate()            [hot path, per message]
          ▼
       table_view::enum_valid(tag, value)  ──► accept / reject
                                                     │
                                                     └─► core::error::wire_field_value_out_of_range
                                                            └─► SessionRejectReason 5, RefTagID = tag
```

No state transitions: both entities are **build-once, read-only**.

---

## Validation rules (the decision table `enum_valid` implements)

Given `tag` and the raw wire `value` (a byte span into the frame — never copied):

| # | Condition | Result | Requirement |
|---|---|---|---|
| 1 | `tag` not in `enums_` | **accept** | FR-003 (floor) |
| 2 | `value` is empty | **not submitted to the enum check** — `enum_valid` returns `true`, but the message's fate is then decided by `check_field_type`, and **that is type-arm dependent** (see below). **NOT the same as "the message is accepted."** | FR-008 / research R-6 |
| 3 | `!multi_value` | accept iff `value` ∈ `codes` (whole-token, byte-exact) | FR-003, FR-009 |
| 4 | `multi_value` | split on a **single** space, **no empty-token skipping**; accept iff **every** token ∈ `codes` | FR-004, FR-014 |
| 5 | any token ∉ `codes` (incl. an empty token from `1  G` or a trailing space) | **reject** | FR-014 |

**Rule 2 is the subtle one, and it is subtler than the first draft said.** It is not "empty is in-domain" — it is "empty is *someone else's* check". But *someone else* has an opinion, and it **depends on the field's type arm** *(corrected at Gate A round 1 — the draft's bare "**accept**" was wrong for half the fields)*:

| Field | `field_type` arm | fixpp (075) | QuickFIX (`checkFieldsHaveValues=false`, FR-019) | Register |
|---|---|---|---|---|
| `Side(54)` | `Char` → `value.size() != 1` (`validator.hpp:411-417`) | **reject** → reason **5** | `set.find("")` → miss → reason **5** | **DV-1** — parity **by coincidence, via a different arm** |
| `ExecInst(18)`, `SettlLocation(166)` | `String` → no constraint (`validator.hpp:419-425`) | **ACCEPT** | reject → reason **5** | **DV-2** — genuine divergence |

Consequences: the FR-018 corpus carries **empty × `Char`** and **empty × `String`** as **two** rows, both **`asserted: false`** — a single undifferentiated empty-value row would **pass on Char and fail on String**, i.e. encode nothing. And **L-075-1** records **two** facts: fixpp has **no reason-4 slot** (`reject_reason_map.hpp:15-75` maps only 1/2/5/6/14, so submitting empty values to the enum check would produce reason 5 and *manufacture* a 5-vs-4 divergence), **and** fixpp's empty-value disposition is **type-arm dependent**. Empty-value disposition is left exactly as it is on `main` — a **pre-existing** gap, neither created nor widened here.

**Comparison is byte-exact**: no case folding (`Side=1` ≠ `Side=l`), no prefix matching — `MatchType(574)=A` **rejects** on FIX44, whose codeset is `A1, A2, A3, A4, A5, AQ, S1…S5, M1, M2, MT, M3…M6` and declares no bare `A` (FR-009). *(Corrected at Gate A round 2 — finding O2-1. The example was `277=A` "when only `AX` is declared"; `TradeCondition(277)` in the shipped `FIX44.xml` **declares `A`** and does **not** declare `AX`, so both engines accept it — a non-discriminating witness for the one rule it was written to pin.)*

---

## Golden parity artifact (FR-018/FR-019/FR-024) — **NEW, data not code. Built FIRST (Phase 0.5).**

**Its measured output DEFINES the divergence register — it is not evidence for a parity claim written ahead of it.** *(Restructured at Gate A round 1, root cause RC#1: the draft's corpus was designed from the same partial reading of QuickFIX's call graph it was supposed to check, and had **no group-member row at all** — so it was structurally incapable of surfacing the largest divergence in the feature.)*

### Row schema — every row carries an `asserted` discriminator

```
{ case_id, dictionary, msg_type, tag, wire_value,
  quickfix_verdict,        # accept | reject
  quickfix_reason,         # SessionRejectReason, when reject
  asserted }               # true  → fixpp MUST match QuickFIX on this row (SC-009)
                           # false → characterization-only; records the delta instead
```

**`asserted: false` ⇒ a divergence-register row, but NOT conversely.** The register (`contracts/enum-domain.md` C-6) is the union of *(1)* every deliberate divergence from QuickFIX (necessarily `asserted: false`) **and** *(2)* every **operator-visible behaviour change that required an argued decision** — which may be **`asserted: true`**, because the two engines can *agree* and still both break conformant traffic. **DV-4** (`166=US`) is exactly that case: `asserted: true`, and a register row. Conflating the two legs is what let DV-4 go unenumerated in the first draft.

| Element | Rule |
|---|---|
| Corpus | **13 rows** — see FR-018 (**8** `asserted: true` + **5** `asserted: false`). *(**12 → 13 at T006**, from MEASURED golden output.)* `asserted: true`: in-domain; out-of-domain; multi-value all-declared; multi-value one-undeclared; degenerate whitespace; **header** field (**`MessageEncoding(347)=ZZZZ`** — ⚠️ **re-based at T006** from `PossDupFlag(43)=X`: `43` is `BOOLEAN`, so QuickFIX's `BoolConvertor` fails in `checkValidFormat` at `DataDictionary.cpp:171` ⇒ reason **6**, **before** the enum arm at `:172`, making an `asserted: true` reason-5 parity claim impossible; `347` is `STRING`, so both engines reach the enum arm ⇒ **5**); **strict prefix (`MatchType(574)=A`** — re-based at Gate A round 2, O2-1); `SettlLocation(166)=US`. **Every row is a message frame driven through `validate()`** — the corpus is a **QuickFIX-parity** artifact and QuickFIX has **no context-free `validate_field` analogue**, so **no `validate_field()` row belongs here** (the FIX50SP2 store-only witness `validate_field(1128, "bogus")` lives in the **FR-020 unit test**, not the corpus — dropped at Gate A round 4, O4-1; see FR-018 for the check-ordering trap that made it go spuriously red). `asserted: false`: **empty × `Char`** (DV-1 — ⚠️ **corrected at T006**: QuickFIX rejects **6**, not 5, via `CharConvertor` at `:171`; it never reaches `isFieldValue`, so the old "parity by coincidence" reading was false in both legs); **empty × `String`** (DV-2); **repeating-group member** (DV-3); **nested-group member** (DV-3, at depth); **`PossDupFlag(43)=X`** (**DV-5**, new at T006 — fixpp rejects **5**, QuickFIX rejects **6**; both REJECT, only the reason differs). **Every `asserted: true` literal is re-measured against the shipped XML** (FR-018's audit table): **a `reject`-asserting row that BOTH engines ACCEPT is a defect, not a row** — it coincides with the stub (`enum_valid → true`), never exercises the rule it names, and **no specified mutation can redden it** (they all flip *reject* rows to accept). The **accept**-asserting rows (1, 3) are the paired positive controls against **over**-rejection and are not caught by this rule. |
| Verdict per case | QuickFIX's accept/reject **and** its reject reason, **for every row** — including `asserted: false` rows, whose value is precisely that they record the delta. |
| **Dictionary topology** | **Pinned: single-dictionary / non-FIXT** — `sessionDD == appDD`, i.e. `sessionDataDictionary.validate(message)` (the FIX 4.x path) — **applied per the corpus row's own dictionary**. QuickFIX's *two*-DD FIXT path (`DataDictionary.cpp:145-156`, selected at `Session.cpp:1221-1229`) checks the header against **FIXT11**, whose `MsgType` has **zero** codes ⇒ unconstrained — a topology **fixpp does not have** (one `cfg_.dictionary`). A two-DD golden would measure the wrong engine. *(FR-019 / research R-12. **Corrected at Gate A round 2 — O2-6**: the pin read "single-dictionary **FIX 4.4**", naming a **version**, while the corpus spans FIX44 **and** FIX41/FIX42 (the `SettlLocation(166)=US` row — **row 12**, numbered 13 until Gate A round 4 dropped the `validate_field` row, O4-1). The invariant is `sessionDD == appDD`, which holds for every 4.x dictionary — the version was never the property being pinned.)* |
| QuickFIX flags | **Pinned** (FR-019): `checkFieldsHaveValues=false`, `checkFieldsOutOfOrder=false`, `checkUserDefinedFields=false`, `AllowUnknownMsgFields=false` |
| **Manifest** (FR-024) | Embedded **inside** the artifact, **six** fields: QuickFIX **version + soname** (`1.16.0` / `libquickfix.so.17.0.0`); **SHA-1 of every dictionary** the corpus loads; **`generator_source_hash`**; **`corpus_input_hash`** (the frames); **`golden_output_hash`** (the **verdicts + reasons + `asserted`**, computed over the rows **excluding the manifest block itself**); the **topology, recorded PER DICTIONARY** alongside each `dictionary_sha1` (O2-6); **all four** flag settings — **set in generator source, not on the CLI**, so `generator_source_hash` actually pins them. **The output hash is not redundant**: the dictionary / generator / corpus-input hashes **all remain valid** when someone hand-edits an `accept` into a `reject`. Without it the manifest gate is decorative. |
| Generated by | a checked-in tool linking the **already-built** local QuickFIX, rooted at the validated CMake cache var `FIXPP_QUICKFIX_ROOT` (default `${CMAKE_SOURCE_DIR}/../../../reference-engines/quickfix-cpp` — **parent-relative**, outside the submodule; **OFF by default**; hard-errors if set but `lib/libquickfix.so` is absent). |
| Consumed by | CI, from the **checked-in golden** — CI never touches the out-of-repo `reference-engines/` tree. |
| **Guarded by** (FR-024) | **(1)** a CI test asserting the **manifest** against the checked-in corpus/config (recomputes the dictionary SHA-1s, corpus hash, generator hash, **output hash**) — runs **without** `reference-engines/`, and catches **tree drift** and a **careless hand-edit**. It does **NOT** catch drift against a **newer QuickFIX**: `quickfix_version` is recorded but **unverifiable in CI**, which has no QuickFIX by design *(narrowed at Gate A round 2 — O2-3; the earlier "hand-edited **or stale**" claim was overstated)*. **(2)** a local **regenerate-and-diff** target that fails on drift when the tree is present — **the only** mechanism that can catch a QuickFIX-version drift, so it is **bound to the per-release out-of-CI interop gate** (`[[project_release_interop_quickfix_fix8]]`) with that owner and cadence, not left as an unscheduled OFF-by-default target. |

Recording the flags *and the topology* **inside** the artifact is not bookkeeping: an unpinned `checkFieldsHaveValues` is precisely what front-ran the enum check and produced this spec's original false parity claim — and an unpinned *topology* would have silently measured a two-dictionary engine fixpp has never been. And a golden with no manifest, produced by a generator CI never runs, is a false-green surface of exactly the class that has burned this repo twice (`[[feedback_codegen_golden_exists_narrow_verify_misses_it]]`).
