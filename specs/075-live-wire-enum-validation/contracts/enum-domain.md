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
| Same, but the field sits **inside a repeating group** (any depth) | **reject**, identically | **5** | the offending **member** tag |
| Same, but the field is a **header** field (`PossDupFlag(43)`, `PossResend(97)`, `MessageEncoding(347)`, `MsgType(35)`) | **reject**, identically | **5** | the header tag |
| Field value is **empty** | **unchanged from `main`** — not submitted to the enum check | *(see L-075-1)* | — |
| Message is **Reject(3)** or **Logout(5)** | not validated (pre-existing no-reject-loop guard) | — | — |
| Message is **admin, including Logon(A)** | **validated** — an out-of-domain admin enum rejects and **the session does not establish** | **5** | the offending tag |

**Behavior change (B-row).** A strict-validating session that today **accepts** an out-of-domain enum will now **reject** it. This is the point of the feature, it rides the existing flag by decision (FR-010), and it matches QuickFIX — which likewise has no separate enum switch. It must ship as an operator-facing behavior row + release note, not as a surprise.

**Reference-engine parity (SC-009).** Verdicts match QuickFIX's `DataDictionary::checkValue`/`isFieldValue` across the FR-018 corpus, measured against a golden generated from the real engine — **not** against a reading of its source. The single known, deliberate divergence is the empty-value case (below).

---

## C-2 — `dict::Dictionary` (C++ API) — additive, no signature change

| Member | Before 075 | After 075 |
|---|---|---|
| `enum_values(tag) -> span<EnumValueRef const>` | Populated **only** by `OrchestraLoader`; empty for all nine QuickFIX dictionaries | Populated by **both** loaders — all ten dictionaries expose their code sets |
| `as_table_view() -> table_view` | Builds valid/required/group/type tables; enum table absent | **Also** builds the owned enum-domain table (values + multi-value bit) |
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
- **No reason-4 slot (L-075-1).** fixpp emits reject reasons 14/2/1/5/6 and has no "tag specified without a value" (4). QuickFIX returns 4 for an empty value; fixpp leaves empty-value disposition unchanged rather than routing it through the enum check and manufacturing a 5-vs-4 divergence. This is a **pre-existing** gap that 075 neither creates nor widens — recorded, not fixed.
- **ApplExtID(1156)=303 / registry re-keying (L-074-1)** and **typed `fixpp::vlatest` codegen** remain separate scheduled follow-ons.
