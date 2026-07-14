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

**Doc correction required**: `dictionary.hpp:66-67` currently states *"Populated only by OrchestraLoader; XmlLoader-produced dictionaries carry an empty enum store."* That becomes false and must be updated in the same commit.

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
| `codes` | **Values only** — descriptions are not copied (the validator never needs them; halves the footprint). **Sorted** so lookup is O(log C): `MsgType(35)` has 92 codes in FIX44 and appears on *every* message, so a linear scan would be up to 92 `memcmp`s per message on the hot path. **Deduped** (FR-017 — QuickFIX's `std::set` semantics). |
| `multi_value` | Set from the field's `field_data_type`, **not** from `table_view::types_` — the collapsed 7-value `field_type` folds both multi-value types into `String` (`table_view.hpp:404`), so the bit is unrecoverable from the existing table (FR-005). |
| absent tag | **Unconstrained ⇒ accept.** The `enums_.find(tag) == end()` branch is the anti-reject-everything floor (FR-003) and is what keeps FIXT11 working at all (its `MsgType` declares zero codes). |

**Populated by**: `Dictionary::as_table_view()` (`src/dictionary/dictionary.cpp`) — once, at config time (`[const §XV.1]`). Immutable thereafter; no mutex (`[const §XV.9]`).

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
| 2 | `value` is empty | **accept** — empty values are *not* submitted to the enum check | FR-008 / research R-6 |
| 3 | `!multi_value` | accept iff `value` ∈ `codes` (whole-token, byte-exact) | FR-003, FR-009 |
| 4 | `multi_value` | split on a **single** space, **no empty-token skipping**; accept iff **every** token ∈ `codes` | FR-004, FR-014 |
| 5 | any token ∉ `codes` (incl. an empty token from `1  G` or a trailing space) | **reject** | FR-014 |

**Rule 2 is the subtle one.** It is not "empty is in-domain" — it is "empty is *someone else's* check". QuickFIX rejects an empty value with reason **4** (`NoTagValue`, via `checkHasValue` which front-runs `checkValue`), and fixpp has **no reason-4 slot**. Submitting empty values to the enum check would produce reason 5 and *manufacture* a divergence. Empty-value disposition is therefore left exactly as it is on `main` — a **pre-existing** gap (recorded as **L-075-1**), neither created nor widened here.

**Comparison is byte-exact**: no case folding (`Side=1` ≠ `Side=l`), no prefix matching (`277=A` rejects when only `AX` is declared) — FR-009.

---

## Golden parity artifact (FR-018/FR-019) — **NEW, data not code**

| Element | Rule |
|---|---|
| Corpus | in-domain; out-of-domain; multi-value all-declared; multi-value one-undeclared; degenerate whitespace (double / trailing space); enum-backed **header** field (`PossDupFlag(43)`); empty value; strict prefix of a declared code |
| Verdict per case | QuickFIX's accept/reject **and** its reject reason |
| QuickFIX flags | **Pinned and recorded inside the artifact** (FR-019): `checkFieldsHaveValues=false`, `checkFieldsOutOfOrder=false`, `checkUserDefinedFields=false`, `AllowUnknownMsgFields=false` |
| Generated by | a checked-in tool linking the **already-built** local QuickFIX (`reference-engines/quickfix-cpp/lib/libquickfix.so`) |
| Consumed by | CI, from the **checked-in golden** — CI never touches the gitignored `reference-engines/` tree |

Recording the flags *inside* the artifact is not bookkeeping: an unpinned `checkFieldsHaveValues` is precisely what front-ran the enum check and produced this spec's one false parity claim.
