# Contract: Enum-Domain Validation (075)

**Date**: 2026-07-14 | **Branch**: `075-live-wire-enum-validation`

fixpp's externally-observable interfaces here are (1) the **wire behavior** of a validating session, (2) the **C++ dictionary API**, and (3) the **C ABI**. This document pins what each promises after 075.

---

## C-1 — Wire contract (the one that matters to an operator)

**Applies when**: `SessionConfig::validate_inbound_messages == true` (unchanged gate — FR-010, no new flag). A session with validation **off** is bit-for-bit unaffected (SC-005).

| Inbound condition | Disposition | Reject reason | RefTagID |
|---|---|---|---|
| Field's tag has **no** code set in the session dictionary | **accept** (unconstrained) | — | — |
| Codeset-backed field, value **is** a declared code | accept | — | — |
| Codeset-backed field, value is **not** a declared code | **reject** | **5** (value is incorrect / out of range) | the offending tag |
| Multi-value field, **every** space-separated token declared | accept | — | — |
| Multi-value field, **any** token undeclared (incl. an empty token from a double/trailing space) | **reject** | **5** | the offending tag |
| Same, but the field sits **inside a repeating group** (any depth) | **reject**, identically | **5** | the offending **member** tag — *and this is a **declared divergence**: QuickFIX does **not** check it (**DV-3**)* |
| Same, but the field is a **header** field (`PossDupFlag(43)`, `PossResend(97)`, `MessageEncoding(347)`, `MsgType(35)`) | **reject**, identically | **5** | the header tag. *fixpp rejects/5 on **all four** header enum fields. QuickFIX agrees at **5** on the two **`STRING`** ones (`MsgType(35)`, `MessageEncoding(347)` — corpus row 6), but returns **6** on the two **`BOOLEAN`** ones (`PossDupFlag(43)`, `PossResend(97)`) because a value outside `{Y,N}` fails its type convertor before the enum arm: **DV-5** (corpus row 13). Verdict matches; only the reason differs.* |
| Field value is **empty**, field is typed **`Char`** (e.g. `Side(54)`) | **reject** — *not* via the enum check (it is skipped) but via `check_field_type`'s `size() != 1` arm | **5** | the tag — **DV-1** *(QuickFIX rejects with **6** here, not 5 — measured; DV-1 is an instance of the DV-5 class)* |
| Field value is **empty**, field is typed **`String`** (e.g. `ExecInst(18)`) | **ACCEPT** — the enum check is skipped and the `String` type arm imposes no constraint | — | — — **DV-2** |
| `SettlLocation(166)=US` on **FIX41/FIX42** (its codeset declares the placeholder `"ISO Country Code"`) | **reject** | **5** | 166 — **DV-4**; QuickFIX rejects identically |
| Message is **Reject(3)** or **Logout(5)** | not validated (pre-existing no-reject-loop guard) | — | — |
| Message is **admin, including Logon(A)** | **validated** — an out-of-domain admin enum rejects and **the session does not establish** | **5** | the offending tag |

*(The two empty-value rows replace a single row that read "**unchanged from `main`** — not submitted to the enum check". That was **not wrong so much as not an answer**: "not submitted to the enum check" does not determine the disposition, because `check_field_type` then does — and it decides **differently by type arm**. Corrected at Gate A round 1, finding C-1.)*

**Behavior change (B-row).** A strict-validating session that today **accepts** an out-of-domain enum will now **reject** it. This is the point of the feature, it rides the existing flag by decision (FR-010), and it matches QuickFIX — which likewise has no separate enum switch. It must ship as an operator-facing **B-row** in `spec/behaviors-and-limitations.md`, not as a surprise. *(Corrected at Gate A round 2 — finding C2-2: this read "behavior row **+ release note**". Per **FR-010** there is **no** release-note artifact in this repo — no `CHANGELOG.md` — and **the B-row IS it**. Naming a second, nonexistent deliverable is exactly the undischargeable clause FR-010 retired.)*

**Reference-engine parity (SC-009).** Verdicts match QuickFIX's across the FR-018 corpus on **every `asserted: true` row**, measured against a golden generated from the real engine — **not** against a reading of its source. Deliberate divergences are enumerated in **C-6** below and are `asserted: false`. *(The prior sentence — "the single known, deliberate divergence is the empty-value case" — was **factually false**; see C-6.)*

---

## C-2 — `dict::Dictionary` (C++ API) — additive, no signature change

| Member | Before 075 | After 075 |
|---|---|---|
| `enum_values(tag) -> span<EnumValueRef const>` | Populated **only** by `OrchestraLoader`; empty for all nine QuickFIX dictionaries | Populated by **both** loaders — all ten dictionaries expose their code sets |
| `as_table_view() -> table_view` | Builds valid/required/group/type tables; enum table absent | **Also** builds the owned enum-domain table (values + multi-value bit), projecting from the dictionary's own `enum_runs_` / `enum_values_` store so tags absent from `message_fields()` still get a domain |
| Everything else | — | **unchanged** |

**No signature changes. No new public types.** `EnumValueRef` keeps its shape; `FieldRef` stays byte-identical.

**Doc invariant to correct**: `dictionary.hpp:66-67` says *"Populated only by OrchestraLoader; XmlLoader-produced dictionaries carry an empty enum store."* This becomes **false** in 075 and must be updated in the same commit that makes it so.

**Lifetime contract preserved**: `as_table_view()` continues to return a `table_view` that **owns its tables** (`dictionary.hpp:193-205`). The enum table is an owned copy — a `table_view` may still legally outlive the `Dictionary` it was built from. *(Aliasing the dictionary's string pool would have silently broken this; see research R-1.)*

---

## C-3 — `dict::table_view::enum_valid` (the behavioral core)

```
[[nodiscard]] bool enum_valid(std::uint16_t tag,
                              std::span<const std::byte> value) const noexcept;
```

**Signature unchanged** — only the body and the backing store change.

Guarantees:

- **`noexcept`** and **allocation-free** on every path (FR-007). No `std::string` materialization of the wire value; no token vector. Tokenization walks `string_view` slices of the caller's buffer.
- **O(1)** tag lookup + **O(log C)** code lookup (codes sorted at build time).
- **Absent tag ⇒ `true`.** The floor (FR-003). A dictionary that carries no enum data behaves exactly as it does today. This is not a convenience — it is the sole guard against a reject-everything regression, and it is what keeps **FIXT11** functional (its `MsgType` declares zero codes).
- The enum-domain table is built from the dictionary's **enum store**, not from `message_fields()` reachability. A codeset-backed tag that is absent from message expansion therefore remains constrained on **all** public surfaces, including `validate_field()`.
- A codeset-backed tag that is present in the enum store but absent from `message_fields()` defaults to **`multi_value=false`**. That is measured-safe on the shipped dictionaries, and SC-011 pins the assumption so a future message-unreachable multi-value tag fails the gate instead of silently becoming a false reject.
- **Byte-exact, whole-token** comparison — no case folding, no prefix match (FR-009).
- Table is **immutable after construction**; no mutex (`[const §XV.9]`), safe for concurrent reads.

**Loader strictness** (FR-017, QuickFIX parity): duplicate `<value enum=X>` → deduped; `<value>` with no `enum` attribute → **load failure** (`xml_parse_error` family); `<value>` with no `description` → legal, empty description.

---

## C-4 — C ABI: **frozen, zero diff**

No change to `include/fix/c_api*`, no change to the exported symbol set, no change to `core/error.hpp`. C ABI stays at **`1.5.0`** (FR-011, SC-007), asserted by the existing ABI-golden gate.

The enum store is **not** exposed through the C ABI — 074 established that precedent and 075 does not revisit it.

---

## C-5 — Explicit scope limits (what this contract does NOT promise)

- **Typed-codegen validators are unaffected.** `validate_<Msg>` for the 83 v44 builders still enforces required-presence + type-conformance **only**, not enum domain — it rides typed codegen, not `table_view`. **L-069-1 stays open** and must be restated as open at close-out.
- **Outbound messages are not enum-validated.** Inbound only.
- **No reason-4 slot, and a type-arm-dependent empty-value disposition (L-075-1).** fixpp emits reject reasons 14/2/1/5/6 (`reject_reason_map.hpp:15-75`) and has no "tag specified without a value" (4). QuickFIX returns 4 for an empty value **at its default settings**; fixpp leaves empty-value disposition unchanged rather than routing it through the enum check and manufacturing a 5-vs-4 divergence. **L-075-1 records BOTH halves**: the missing reason-4 slot **and** the fact that fixpp's empty-value disposition is decided by `check_field_type`'s **type arm** (`Char` rejects/5; `String` accepts) — so "fixpp leaves empty alone" is *not* the same as "fixpp accepts empty". Both are **pre-existing** gaps that 075 neither creates nor widens — recorded, not fixed.
- **ApplExtID(1156)=303 / registry re-keying (L-074-1)** and **typed `fixpp::vlatest` codegen** remain separate scheduled follow-ons.

---

## C-6 — Divergence register (**derived from the FR-018 golden, not asserted ahead of it**)

*(New at Gate A round 1, root causes RC#1/RC#3. It replaces SC-009's claim that the empty-value case was *"the one known divergence"* — which was **false in at least three directions**, and which the FR-018 corpus, having **no group-member row**, was structurally incapable of disproving.)*

**The rule.** The golden measures QuickFIX's verdict + reason for **every** corpus row. fixpp is **asserted** to match only on **`asserted: true`** rows; on those, divergence is a **defect**. **`asserted: false`** rows are the register below: each is a deliberate difference carrying an argued disposition, a corpus row that **measures** it, and a **B-row**. **A divergence the golden reveals that is not in this register is a defect** and blocks the feature until it is fixed or promoted to an argued row. *The register is the golden's output, not this contract's input.*

| ID | Divergence | fixpp | QuickFIX | Disposition (argued) | Corpus row | B-row |
|---|---|---|---|---|---|---|
| **DV-1** | **empty × `Char`** (`54=`) | reject **5** — via `check_field_type`'s `size() != 1` arm (`validator.hpp:411-417`) | reject **6** — via `checkValidFormat`'s `CharConvertor::convert("")` failure (`DataDictionary.cpp:171`, `DataDictionary.h:386-388`) ⇒ `IncorrectDataFormat` | ⚠️ **CORRECTED at T006 from MEASURED golden output (2026-07-14). The prior text — "reject **5** via `isFieldValue`'s `set.find("")` miss", "parity by COINCIDENCE" — was FALSE IN BOTH LEGS.** QuickFIX never reaches `isFieldValue` here: `checkValidFormat` (`:171`) front-runs `checkValue` (`:172`) and the `Char` convertor rejects the empty string outright. So this is **not parity at all** — the engines return **different reasons** (fixpp 5 / QuickFIX 6). It is an instance of the **DV-5** class. Both engines still **reject**. `asserted: false`. (FR-008) | 8 | ✅ |
| **DV-2** | **empty × `String`** (`18=`) | **ACCEPT** — enum check skipped (rule 2), `String` type arm imposes no constraint (`validator.hpp:419-425`) | reject **5** | **Accepted.** fixpp has **no reason-4 slot** (L-075-1); routing empty values through the enum check would emit 5 where QuickFIX (at its *defaults*) emits **4** — *manufacturing* a divergence rather than removing one. Adding reason 4 is session-reject-mapping work, not enum work: a separate feature. (FR-008, disposition (a)) | 9 | ✅ |
| **DV-3** | **repeating-group member enums** (any depth) | **CHECKS them** — the Step-1 walk is a raw-frame byte scan with no group awareness (`validator.hpp:139-158`, `parser.hpp:229-233`), so it yields members at every depth | **NEVER checks them.** Closed call-site census: `isFieldValue` ← `checkValue` ← *only* `DataDictionary.cpp:172` inside `iterate`; `iterate` never recurses and loops only over `FieldMap::m_fields` — group instances live in `m_groups`, reachable only via `g_begin()/g_end()`, which `iterate` never calls | **KEEP IT — fixpp is deliberately STRICTER and more correct.** QuickFIX's behaviour here is a known weakness: an out-of-domain enum inside a repeating group passes its validator entirely. Suppressing fixpp's check to match would mean writing deliberate code to make a **correct** check not run, on exactly the fields (party roles, leg sides, MD entry types) where a domain error is most costly — and fixpp's flat walk makes the check free. (FR-023) | 10, 11 | ✅ |
| **DV-5** | **Reason divergence on a value that fails QuickFIX's TYPE CONVERTOR** — canonically an enum-backed **`BOOLEAN`** or **`CHAR`** field carrying a format-invalid value (`PossDupFlag(43)=X`; also empty × `Char`, i.e. DV-1) | reject **5** (`wire_field_value_out_of_range`) | reject **6** (`IncorrectDataFormat`) | ⚠️ **DERIVED FROM THE GOLDEN AT T006 — the register's only measurement-discovered row.** QuickFIX's `iterate()` runs `checkValidFormat` (**`DataDictionary.cpp:171`** — the field's **type convertor**) **immediately BEFORE** `checkValue` (`:172`, the enum arm). A value that fails its convertor throws `IncorrectDataFormat` ⇒ reason **6** and **never reaches the enum arm**. fixpp has **no generic bad-format slot** — its reason **6** (`wire_field_value_truncated`) is reserved for **Float/decimal precision-loss only** (`reject_reason_map.hpp:24-26, 63-65`) — so the same value maps to `wire_field_value_out_of_range` ⇒ reason **5** (`:59-62`). **Both engines REJECT; only the REASON differs.** **KEEP IT — fixpp's verdict is correct and its behaviour is unchanged.** Matching QuickFIX's 6 would mean adding a generic bad-format reject slot: that is **session-reject-mapping work, not enum work** (the same argument C-5 already makes for reason **4**), and it would flip the **pre-existing** empty × `Char` ⇒ reject/**5** pin at `tests/wire/validator_type_check_test.cpp:387-396` — a silent re-baseline **FR-012 forbids**. Recorded, not fixed. **The divergence class is "the wire value FAILS QuickFIX's type convertor at `:171`" — NOT "the field is not a `STRING`".** It diverges for a **`BOOLEAN`** value outside `{Y,N}` (row 13) and for an **empty or multi-character `CHAR`** value (row 8) — cases fixpp's type arm does not replicate the same way, so it falls through to reason **5**. **Every other out-of-domain value — including a single-character `CHAR` like `Side(54)=Z` (row 2, the headline) — PASSES the convertor, so both engines reach the enum arm and reject/5 identically.** ⚠️ *An earlier draft of this note said "reason-5 parity holds exactly for `STRING`/multi-string enum fields". **That was FALSE and row 2 falsifies it**: `Side(54)` is a `CHAR` field in the parity set. Corrected before close-out — the same over-generalising-from-a-partial-reading failure this phase exists to catch.* *(This is the **root cause that five Gate A rounds missed**: every parity claim in the bundle cited `DataDictionary.cpp:172` and `:178` — and **`:171` is one line above the line they quoted**. It is the **third** instance of RC#1: a parity claim written from a partial reading of QuickFIX's call graph. The **golden** caught it; review did not. This is what Phase 0.5 is for.)* (FR-015) | **13** (`asserted: false`), and **8** | ✅ |
| **DV-4** | **`SettlLocation(166)=US`** on FIX41/FIX42 — the codeset declares the prose placeholder `"ISO Country Code"` | reject **5** | reject **5** (identical) | **Not a divergence FROM QuickFIX — an operator-visible REGRESSION both engines share.** Registered here because it is the same *class* of harm (breaks conformant traffic) and it required a decision the bundle never posed. **Accept-and-document**: a carve-out would manufacture a divergence from the reference engine to paper over bad vendored data, and any heuristic ("ignore codes with a space") is an invented rule that would silently weaken a real codeset the day a legitimate space-bearing code appears. **SC-011** asserts that the space-bearing declared codes across the ten shipped dictionaries are **EXACTLY** `{FIX41:166:'ISO Country Code', FIX42:166:'ISO Country Code'}` — an **executable, green** exact-set gate *(reformulated at Gate A round 2, C2-1: it previously asserted "no code contains a space" and simultaneously conceded that it "fires today", i.e. a gate defined to be RED)*. Any addition, removal or edit — including a *new* placeholder from a dictionary refresh — **fails the build**. (FR-022) | 12 (`asserted: true` — both reject; **re-numbered from 13** at Gate A round 4 when the `validate_field(1128, …)` row was dropped — O4-1) | ✅ |

**Deliberately NOT a register row — the check-ordering difference.** *(Gate A round 4, finding O4-1.)* On the overlap {tag not valid for this msg_type} × {value out of domain}, fixpp runs `field_valid_for` **first** (`validator.hpp:143` → reason **2**, never reaching `enum_valid` at `:148`) while QuickFIX runs `checkValue` (`DataDictionary.cpp:172`) **before** `checkIsInMessage` (`:178`) → reason **5**. Both engines *reject*; only the **reason** differs. It gets **no DV-\* row**: it is **unobservable on every legitimate corpus row** (all of them use **in-message** tags, so both engines reach the enum arm and agree on 5), there is therefore no corpus row that **measures** it — and a register row whose corpus row does not exist is exactly the prose-not-data failure C-6 was created to end. It is recorded in **FR-018** and `research.md` R-8 as a **fact and a warning**: a corpus row on a **message-unreachable** tag would walk straight into it and be scored a spurious `asserted: true` divergence — a self-inflicted, feature-blocking "defect" under SC-009. That is what the short-lived `validate_field(1128, "bogus")` row did, and why it was dropped. **Do not add a message-unreachable-tag row to the corpus, and do not promote this to DV-5.**

**Note on DV-4's `asserted` value.** It is `true`, not `false`: fixpp and QuickFIX **agree**, so it belongs in the asserted set. It carries a register row anyway because the register is not only *"where we differ from QuickFIX"* — it is *"every operator-visible behaviour change that required an argued decision."* Conflating those two is what let this case go unenumerated in the first place.
