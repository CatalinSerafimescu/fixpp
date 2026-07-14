# Feature Specification: Live-Wire Enum-Value Validation (Dictionary-Driven Enum Domain Checking)

**Feature Branch**: `075-live-wire-enum-validation`

**Created**: 2026-07-14

**Status**: Draft

**Input**: User description: "live-wire enum-value validation (dictionary-driven enum domain checking) — turn the dead `enum_valid` stub into a real dictionary-backed domain check at both validator call sites, for every loaded dictionary; make the pre-built SessionRejectReason-5 arm fire."

---

## Context (why this feature exists)

Three facts, all source-verified on `main` at `90f48a3e`:

1. **The check is a stub.** `dict::table_view::enum_valid(tag, value)` is `return true` with no storage (`include/fixpp/dict/table_view.hpp:288-294`); its builder `add_enum(tag, value)` discards both arguments (`:350-352`). The runtime validator calls it at **both** sites — top-level fields (`include/fixpp/wire/validator.hpp:148`) and group-member fields (`:325`). A field carrying a **structurally-valid type but an out-of-domain enum constant** (e.g. `Side(54)=Z`) is therefore ACCEPTED on the wire today.
2. **The reject path already exists and is dead.** `include/fixpp/wire/reject_reason_map.hpp:20-23` maps `wire_field_value_out_of_range(40)` → SessionRejectReason **5** ("value is incorrect / out of range for this tag") and documents in-source that this is *"type-arm only in Phase-1 since the enum arm is dead — `enum_valid`→true"*. The rejection machinery is built; nothing ever triggers it.
3. **074 landed the data, unused.** Feature 074 added an additive tag-keyed enum side-table on `dict_metadata_handle` (flat `std::pmr::vector<EnumValueRef>` + sorted-by-tag run index, binary-searched — `src/dictionary/dictionary.cpp:178-191`), exposed read-only as `Dictionary::enum_values(tag) → std::span<EnumValueRef const>` (`dictionary.hpp:184-191`). **`as_table_view()` never copies it in and `enum_valid` never reads it.** The data sits inert.

**The scoping fork (source-verified, resolved in this spec).** `Dictionary::enum_values` is populated **only by `OrchestraLoader`**. `XmlLoader` — which produces all **nine** legacy QuickFIX dictionaries — **never reads the `<value enum="…" description="…"/>` elements at all**; those dictionaries carry an empty enum store (documented at `dictionary.hpp:66-67`). Wiring `enum_valid` to the existing store alone would deliver enum validation for **FIX Latest only** and leave the real-world FIX 4.4 `Side(54)`/`OrdType(40)` case untouched. This spec therefore scopes to **all ten dictionaries** (FR-001).

**Census of what is actually at stake** (measured from the shipped `dictionaries/*.xml`, 2026-07-14):

| Dictionary | Fields | Enum-backed fields | Total codes | Multi-value enum fields |
|---|---|---|---|---|
| FIX40 | 138 | 39 | 235 | 0 |
| FIX41 | 206 | 53 | 342 | 0 |
| FIX42 | 405 | 104 | 629 | 3 |
| FIX43 | 635 | 159 | 1198 | 8 |
| **FIX44** | 912 | **245** | **1708** | **8** |
| FIX50 | 1090 | 290 | 2326 | 10 |
| FIX50SP1 | 1373 | 327 | 2640 | 10 |
| FIX50SP2 | 6028 | 668 | 5565 | 9 |
| FIXT11 | 71 | 9 | 56 | 0 |

The **multi-value** fields are shipped and real — in FIX44: `ExecInst(18)` (40 codes), `QuoteCondition(276)`, `TradeCondition(277)`, `OpenCloseSettlFlag(286)`, `FinancialStatus(291)`, `CorporateAction(292)`, `OrderRestrictions(529)`, `Scope(546)`. These carry a **space-separated list of codes** on the wire, not a single code. Validating the raw field value as one code would false-reject every conformant `ExecInst` — this is the single highest-risk trap in the feature (FR-004).

---

## User Scenarios & Testing *(mandatory)*

### User Story 1 — A strict-validating session rejects an out-of-domain enum value (Priority: P1)

An operator runs a session with inbound validation enabled against a FIX 4.4 dictionary. A counterparty sends a `NewOrderSingle` whose `Side(54)` is `Z` — a value that is not one of the dictionary's declared `Side` codes, but which *is* a structurally valid `char`. Today fixpp accepts it and hands it to the application, which must re-check the domain itself or silently mis-book. After this feature, fixpp rejects the message with SessionRejectReason **5** and `RefTagID=54`, and the application never sees it.

**Why this priority**: This is the entire point of the feature and the only story that turns the dead reject arm live. Everything else is a refinement of it.

**Independent Test**: Load the shipped `FIX44.xml`, build the validator, feed a frame with `54=Z`; assert reject with reason 5 and `RefTagID=54`. Feed the same frame with `54=1` (a declared code); assert accept. Mutation-check: revert `enum_valid` to `return true` and confirm the first assertion fails.

**Acceptance Scenarios**:

1. **Given** a session validating against FIX44, **When** an inbound message carries `Side(54)=Z` (undeclared code), **Then** the message is rejected with SessionRejectReason 5, `RefTagID=54`, and is not delivered to the application.
2. **Given** the same session, **When** an inbound message carries `Side(54)=1` (declared code), **Then** the message is accepted exactly as it is today.
3. **Given** the same session, **When** an inbound message carries a value for a field with **no** codeset (e.g. `ClOrdID(11)`), **Then** the value is accepted regardless of its content — no enum constraint is imposed.
4. **Given** a message whose out-of-domain enum field sits **inside a repeating group**, **When** it is validated, **Then** it is rejected identically (reason 5, `RefTagID` = the offending member tag) — the group-member call site behaves the same as the top-level one.

---

### User Story 2 — Multi-value enum fields validate per token (Priority: P1)

A counterparty sends `ExecInst(18)=1 G 6` — three space-separated, individually-declared `ExecInst` codes, which is exactly what the FIX spec prescribes for a `MULTIPLEVALUESTRING`/`MULTIPLECHARVALUE` field. The session must accept it. If any single token is undeclared (`18=1 ZZ 6`), the message must be rejected.

**Why this priority**: P1 and **co-equal with Story 1** — not a refinement. Getting this wrong does not merely under-validate, it **actively breaks conformant traffic**: a naive whole-string domain check false-rejects every well-formed `ExecInst`, `QuoteCondition`, `TradeCondition`, `OrderRestrictions`, … on eight FIX44 tags. Story 1 cannot ship without Story 2.

**Independent Test**: Feed `18=1 G 6` (all declared) → accept. Feed `18=1 ZZ 6` → reject, reason 5, `RefTagID=18`. Feed `18=1` (single token) → accept. Mutation-check: replace the tokenizer with a whole-string lookup and confirm the accept case flips to reject.

**Acceptance Scenarios**:

1. **Given** a multi-value enum field whose every space-separated token is a declared code, **When** it is validated, **Then** it is accepted.
2. **Given** a multi-value enum field with at least one undeclared token, **When** it is validated, **Then** the message is rejected with reason 5 and `RefTagID` = that field's tag.
3. **Given** a single-value enum field whose value happens to contain a space, **When** it is validated, **Then** the value is checked as **one** code (no tokenization) and rejected if undeclared — tokenization applies **only** to fields the dictionary types as multi-value.

---

### User Story 3 — Legacy dictionaries gain enum data without changing anything else (Priority: P2)

An operator loading any of the nine QuickFIX dictionaries gets a dictionary that now carries its declared code sets, queryable via the same read-only accessor 074 introduced for FIX Latest. Everything else about those dictionaries — field metadata, group structure, message sets, the C-ABI surface — is byte-for-byte unchanged.

**Why this priority**: P2 — it is the enabling substrate for Stories 1 and 2 on the nine legacy versions (without it they cover FIX Latest only), but it delivers no wire behavior by itself.

**Independent Test**: Load each of the ten dictionaries; assert `enum_values(54)` on the FIX44 dictionary returns the declared `Side` codes with their descriptions; assert the per-dictionary enum-backed-field and total-code counts match the census table above; assert every pre-existing dictionary test still passes unchanged.

**Acceptance Scenarios**:

1. **Given** the shipped `FIX44.xml`, **When** it is loaded, **Then** `enum_values(54)` returns the declared `Side` codes, each with its `description`, and the dictionary's totals match the census (245 enum-backed fields / 1708 codes).
2. **Given** any of the ten dictionaries, **When** it is loaded, **Then** every existing dictionary/validator/codegen test passes with no change in behavior other than enum-domain rejection.

---

### Edge Cases

- **Empty enum store for a tag** → the tag is **unconstrained**; accept. This is the fail-safe default and the sole thing standing between this feature and a catastrophic reject-everything regression. It must hold for a tag with no codeset, for a dictionary that carries no enum data at all, and for a dictionary built by a future loader that does not populate the store.
- **Empty field value** (`54=` with nothing before the SOH) — is an empty value in-domain? Only if the codeset declares an empty code (none do). Decide and pin: empty value on a codeset-backed field is out-of-domain → reject.
- **Multi-value tokenization degenerate forms** — leading/trailing/repeated spaces (`18= 1  G `). **Resolved (FR-014): reject.** QuickFIX splits on a single space and does not skip empty tokens, so an empty token is looked up, is never a declared code, and rejects. We match byte-for-byte rather than inventing a more forgiving tokenizer.
- **Value that is a strict prefix of a declared code** (`277=A` where only `AX` is declared) — must reject; the comparison is whole-token equality, never a prefix match.
- **Inbound admin messages, especially Logon.** The validator runs **before** `interpret_logon` on the `NotConnected` arm (`src/session/session.cpp:2060-2070`); only Reject(3) and Logout(5) are exempt. **Resolved (FR-013): admin and Logon ARE enum-checked** — QuickFIX does exactly this (`Session.cpp:1218-1229`, before `nextLogon` at `:1231`). The consequence is accepted and must be pinned + documented: a peer sending an out-of-domain admin enum (e.g. an undeclared `EncryptMethod(98)`) is rejected with reason 5 and **cannot establish a session**. Bounded by the whole path being opt-in behind `validate_inbound_messages`.
- **Case sensitivity** — FIX enum codes are case-sensitive (`Side=1`, `SecurityIDSource=4`, `OrdStatus=A`). Comparison is byte-exact; no case folding.
- **A field that is enum-backed in one dictionary and free-form in another** — the constraint is always per-loaded-dictionary; there is no global tag→codeset table.
- **Hot path** — `enum_valid` is called per-field, per-message. A per-call allocation, a `std::string` construction, or a linear scan over all tags would be a performance regression on the message hot path.

---

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The dictionary loader for the nine QuickFIX-sourced dictionaries MUST parse each field's declared code set (`<value enum="…" description="…"/>`) into the **same** additive tag-keyed enum store that feature 074 introduced, so that all ten dictionaries expose their code sets through the existing read-only accessor. No new parallel storage mechanism.
- **FR-002**: The config-time validator table MUST carry an enum-domain table derived from the dictionary's enum store, built **once** at table-construction time and never rebuilt per message.
- **FR-003**: The enum-domain check MUST return **not-valid** for a value that is absent from a **non-empty** code set for that tag, and MUST return **valid** whenever the tag's code set is **empty** — a tag with no codeset, and any dictionary carrying no enum data, are unconstrained and behave exactly as today. *(Fail-safe default; no dictionary may be made stricter by the mere absence of data.)*
- **FR-004**: For a field the dictionary types as **multi-value** (`MULTIPLEVALUESTRING` / `MULTIPLESTRINGVALUE` → `MultiStringValue`; `MULTIPLECHARVALUE` → `MultiCharValue`; Orchestra `MultipleCharValue`), the wire value MUST be tokenized on the space separator and **each token** checked independently against the code set. The field is valid only if **every** token is a declared code. Single-value fields MUST NOT be tokenized.
- **FR-005**: The validator table MUST retain enough per-tag type information to know that a tag is multi-value. *(The existing 7-value `field_type` category collapses the multi-value types into `String` — `field_type_of()` alone cannot distinguish them, so FR-004 is not satisfiable from the current table without carrying additional per-tag data.)*
- **FR-006**: A detected out-of-domain value MUST surface as the existing out-of-range slot so it maps to **SessionRejectReason 5**, with `RefTagID` set to the offending tag — at **both** validator call sites: top-level fields **and** repeating-group member fields. A fix at one site only is a half-restructure and MUST be treated as incomplete.
- **FR-007**: The enum-domain check MUST be `noexcept`, allocation-free, and no worse than O(log T) tag lookup + O(C) scan of that tag's codes (T = enum-backed tags, C = codes for the tag). No per-message or per-field heap allocation; no `std::string` materialization of the wire value.
- **FR-008**: An empty field value on a codeset-backed field MUST be treated as out-of-domain (rejected), since no shipped code set declares an empty code.
- **FR-009**: Comparison MUST be byte-exact and whole-token — no case folding, no prefix matching.
- **FR-010**: Enum-domain validation MUST ride the **existing** `validate_inbound_messages` flag — **no new config surface, no sub-flag** (Clarification Q2, matching QuickFIX, which has no separate enum switch). A session with inbound validation off MUST be bit-for-bit unaffected. The resulting behavior change for existing strict-validating sessions MUST be recorded as a `behaviors-and-limitations.md` **B-row** and in the release notes.
- **FR-011**: Zero C-ABI change. The C ABI is GA-frozen at `1.5.0`; this feature MUST NOT alter `include/fix/c_api*` or the exported symbol set.
- **FR-012**: Every pre-existing dictionary, validator, session, and codegen test MUST pass unchanged except where a test asserted the *old* accept-an-invalid-enum behavior; any such test MUST be updated with its change explicitly called out (never silently re-baselined).
- **FR-013**: Enum-domain checking MUST apply to **all** validated inbound messages — application **and** admin, **including Logon**, which fixpp validates before `interpret_logon` (QuickFIX parity, Clarification Q3). Reject(3) and Logout(5) remain exempt via the pre-existing no-reject-loop guard. A Logon carrying an out-of-domain admin enum MUST be pinned by a direct test showing it is rejected with reason 5 and that the session does not establish; this operator-visible consequence MUST be recorded as a B-row.
- **FR-014**: The multi-value tokenizer MUST split on a **single space** and MUST NOT skip empty tokens — a double space or a trailing space yields an empty token, which is never a declared code and therefore rejects. This is byte-for-byte QuickFIX behavior (`DataDictionary.h:265-275`) and is required for interop parity, not merely permitted.

### Key Entities

- **Code set** — the set of declared `{value, description}` pairs for one tag, per dictionary. Already modelled by 074's `EnumValueRef` and the tag-keyed store; this feature populates it for nine more dictionaries and *reads* it from the validator.
- **Enum-domain table** — the config-time, validator-facing projection of the code sets: tag → (codes, is-multi-value). Built once from the dictionary; immutable; consulted per field on the hot path.

---

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: A message carrying an out-of-domain value for a codeset-backed field is **rejected** with SessionRejectReason 5 and the offending tag as `RefTagID`, at both the top-level and repeating-group-member positions. Proven by a mutation-discriminating witness: reverting the domain check to always-accept must turn the test RED.
- **SC-002**: All **ten** dictionaries expose their declared code sets, with per-dictionary enum-backed-field and total-code counts matching the census table in Context (FIX44 = 245 fields / 1708 codes). Proven by a direct assertion against the shipped XML, not against a hand-maintained expectation list.
- **SC-003**: Every one of the multi-value enum fields shipped in the dictionaries (8 in FIX44, 10 in FIX50/SP1) accepts a conformant multi-token value and rejects a value with any undeclared token. Proven per-field for `ExecInst(18)` and by a table-driven witness across the full multi-value census.
- **SC-004**: A field with no code set, and a dictionary with no enum data, are accepted exactly as before — **zero** new rejections. Proven by the full existing conformance/validator/session suite passing green, plus a direct empty-store-accepts pin.
- **SC-005**: A session with inbound validation **disabled** exhibits byte-identical behavior to `main`.
- **SC-006**: No measurable throughput regression on the validated-message hot path relative to `main` (the existing bench gate is the arbiter); zero additional allocations per message.
- **SC-007**: Zero C-ABI diff; exported symbol set unchanged.
- **SC-008**: An inbound **Logon** carrying an out-of-domain admin enum is rejected with reason 5 and the session does **not** establish — pinned by a direct test, matching QuickFIX. Conversely, a Logon whose admin enums are all in-domain establishes exactly as today (no false-reject of the handshake).
- **SC-009**: **QuickFIX interop parity on the enum-domain boundary.** For the accept/reject cases exercised by this feature, fixpp's disposition matches QuickFIX's (`checkValue`/`isFieldValue`), including the degenerate multi-value whitespace forms (FR-014). Divergence from the reference engine is a defect, not a preference.

---

## Non-Goals *(explicit — do not silently absorb)*

- **Codegen enum-domain validation (L-069-1 is NOT retired).** The generated `validate_<Msg>` functions for the 83 v44 typed builders enforce required-field presence and type conformance only, not enum domain. They ride a **different** path (typed codegen), not `table_view`, and this feature does **not** touch them. L-069-1 remains open and MUST be restated as still-open at close-out.
- **ApplExtID(1156)=303 differentiation and `version_registry` re-keying** (retires L-074-1) — separate scheduled follow-on.
- **Typed `fixpp::vlatest` 181-class codegen** — separate scheduled follow-on.
- **Outbound validation.** This feature validates **inbound** messages only; it does not add an outbound enum gate.
- **Any C-ABI or Python-binding surface change.**

---

## Constitution Check *(must be resolved at `/plan` / Gate A)*

Constitution **v0.6**, Article I §1, records the FIX Latest read/dictionary tier as delivered by 074 and explicitly scopes it so that **"typed codegen, live wire validation, ApplExtID(1156)=303 differentiation, and session negotiation remain post-1.0."** Feature-catalogue row D-011 repeats this.

This feature delivers **live wire validation**. For the nine legacy dictionaries that is squarely in-scope v1.0 work (it discharges the long-standing Phase-1 `enum_valid` stub / FR-005 deferral, which predates and is independent of FIX Latest). But the check is **dictionary-generic** — a FIX Latest dictionary loaded into a validating session will get enum-domain checking as an automatic consequence of FR-002/FR-003, with no FIX-Latest-specific code. That touches the letter of the Article I §1 post-1.0 carve-out and of Article XVIII §5 (no early-ship of deferred post-1.0 scope).

**Disposition — DECIDED (Clarification Q1, 2026-07-14): (a) amend.** Article I §1 is amended (MINOR, **v0.6 → v0.7**) to narrow the FIX-Latest post-1.0 carve-out to *typed codegen + ApplExtID(1156)=303 + session negotiation*, recording that dictionary-driven wire **validation** ships generically for **all** supported versions via this feature. Article XVIII §5's no-early-ship bar has no residual conflict once §1 is narrowed, because the scope ceases to be deferred.

The rejected alternative — leave the constitution unchanged and argue the carve-out only ever meant FIX-Latest-*specific* wire work — leans on a reading the text does not clearly support, and would leave a shipped capability contradicting the ratified baseline.

The amendment rides **this feature's branch** rather than a standalone `Constitution: amend …` PR, per the established Gate-A-fold deviation from Article XX §2's letter (precedents: 035, 043, 068, 069). It MUST be drafted with a Sync Impact Report and ratified at Gate A / `/plan`, and the feature-catalogue **D-011** row (which repeats the post-1.0 carve-out verbatim) MUST be corrected in the same pass.

---

## Assumptions

- The nine QuickFIX dictionaries' `<value>` elements are trustworthy as the authoritative code sets for those versions — they are the same vendored files already used for every other dictionary fact, so no new supply-chain surface is introduced.
- The enum store introduced by 074 (flat vector + sorted-by-tag run index) is the right storage shape and scales to the largest case (FIX50SP2: 668 enum-backed fields / 5565 codes); no new storage design is needed, only population and a validator-side projection. *(To be confirmed at `/plan` — if it does not, the store is 074's and changing it is in scope.)*
- Reject(3) and Logout(5) remain exempt from validation (the existing no-reject-loop guard), so enum checking cannot induce a reject loop.
- FIX enum codes are case-sensitive and are compared as raw wire bytes.
- The census in Context is a faithful count of the shipped XML as of 2026-07-14; SC-002 asserts against the XML itself, not against these numbers, so a dictionary refresh cannot silently invalidate the gate.

*(Three former assumptions — empty-store-accepts, multi-value tokenization, and reject-reason-5 mapping — are no longer assumptions. They are source-verified against QuickFIX and recorded under Clarifications → Reference-engine confirmations, and pinned as FR-003 / FR-004+FR-014 / FR-006.)*

---

## Clarifications

### Session 2026-07-14

- **Q: Which dictionaries get enum-domain validation — all ten, FIX-Latest-only, or the nine legacy only?**
  → **A: All ten.** `XmlLoader` is taught to parse `<value enum="…" description="…"/>` into the **same** additive enum store 074 introduced, so the nine QuickFIX dictionaries and FIX Latest all expose their code sets and all get live enum checking (**FR-001**). FIX-Latest-only would deliver nothing operationally (no deployment runs FIX Latest yet) *and* would not dodge the constitutional question — it is precisely the leg Article I §1 defers. Consequence: the **Constitution Check** disposition **(a)** applies — a MINOR amendment (→ **v0.7**) narrowing the FIX-Latest post-1.0 carve-out to *typed codegen + ApplExtID + session negotiation*, recording that dictionary-driven wire validation ships generically for all supported versions. To be folded into this feature's branch at Gate A (precedent: 035/043/068/069).

- **Q: Does enum checking ride the existing `validate_inbound_messages` flag, or need its own opt-in sub-flag?**
  → **A: Rides the existing flag.** No new config surface. A session that opted into strict inbound validation gets enum-domain checking; a session with validation off is bit-for-bit unaffected (**FR-010**, **SC-005**). This **is** a real behavior change for existing strict-validating deployments — they will begin rejecting out-of-domain enum values they accept today — and it MUST be recorded as a `behaviors-and-limitations.md` **B-row** and called out in the release notes. This matches QuickFIX, which has no separate enum-validation switch: `checkValue` is unconditional inside `DataDictionary::validate`.

- **Q: Does enum-domain checking apply to inbound admin messages, including Logon (which fixpp validates *before* `interpret_logon`)?**
  → **A: Yes — all inbound validated messages, admin and Logon included. QuickFIX parity, source-verified.** `Session::next` validates **every** inbound message at `Session.cpp:1218-1229` — **before** `nextLogon` (`:1231`), the same validate-first ordering fixpp has. `DataDictionary::validate` → `iterate` → `checkValue` runs the enum check over header, trailer **and** body, so `Logon`'s `EncryptMethod(98)` *is* enum-checked; an out-of-domain value throws `IncorrectTagValue`, caught at `Session.cpp:1283-1284` → `generateReject(…, SessionRejectReason_VALUE_IS_INCORRECT)` = reason **5** — exactly the arm this feature makes live. A message-category carve-out would be a deliberate divergence from the engine our interop gate compares against; we take the parity. The Logon blast radius is accepted (a peer sending an undeclared admin enum cannot establish), bounded by the fact that the whole path is opt-in behind `validate_inbound_messages`, and MUST be pinned by a direct Logon test (**FR-013**) and recorded as an operator-facing B-row.

### Reference-engine confirmations (QuickFIX, source-verified — these are no longer assumptions)

- **Empty code set ⇒ unconstrained ⇒ accept.** `checkValue` returns early when the tag has no declared values (`DataDictionary.h:493-496`). This is exactly **FR-003**, and it is the reference engine's own anti-reject-everything guard.
- **Multi-value tokenization.** `isFieldValue` (`DataDictionary.h:255-276`) splits a multi-value field on a **single space** and requires **every** token to be a declared code. This is exactly **FR-004**. Note it does **not** skip empty tokens: `18=1  G` (double space) and any trailing space therefore produce an empty token, which is never a declared code, and **reject**. fixpp matches this (**FR-014**).
- **Out-of-domain ⇒ SessionRejectReason 5** with the offending tag as `RefTagID` (`Session.cpp:1283-1284`). This is exactly **FR-006**.
