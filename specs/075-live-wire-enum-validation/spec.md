# Feature Specification: Live-Wire Enum-Value Validation (Dictionary-Driven Enum Domain Checking)

**Feature Branch**: `075-live-wire-enum-validation`

**Created**: 2026-07-14

**Status**: Draft

**Input**: User description: "live-wire enum-value validation (dictionary-driven enum domain checking) — turn the dead `enum_valid` stub into a real dictionary-backed domain check at both validator call sites, for every loaded dictionary; make the pre-built SessionRejectReason-5 arm fire."

---

## Context (why this feature exists)

Three facts, all source-verified on `main` at `90f48a3e`:

1. **The check is a stub.** `dict::table_view::enum_valid(tag, value)` is `return true` with no storage (`include/fixpp/dict/table_view.hpp:288-294`); its builder `add_enum(tag, value)` discards both arguments (`:350-352`). A field carrying a **structurally-valid type but an out-of-domain enum constant** (e.g. `Side(54)=Z`) is therefore ACCEPTED on the wire today.

   **`enum_valid` has exactly two production call sites** (exhaustive — no third):
   - **`validator.hpp:148`**, inside the **Step-1 flat walk** (`:139-158`). `msg.begin()`→`msg.end()` on a `MessageView<Index>` constructs a **dict-free `field_iterator` over the raw frame bytes** (`parser.hpp:229-233`, `:186-225`, `advance()` at `:444`) — a linear `tag=value` scan of the whole frame with **no group awareness at all**. It therefore yields **every present field**: header, body, **repeating-group members at every depth**, and trailer. Group-member coverage is a *consequence of this one flat walk*, not of a second call site.
   - **`validator.hpp:325`**, inside **`validate_field(tag, value)`** (`:323-330`) — the context-free public pure-virtual declared at `:65-66`, with **no `msg_type` context**. This is a *third public surface*, not the group-member path (FR-020).
2. **The reject path already exists and is dead.** `include/fixpp/wire/reject_reason_map.hpp:20-23` maps `wire_field_value_out_of_range(40)` → SessionRejectReason **5** ("value is incorrect / out of range for this tag") and documents in-source that this is *"type-arm only in Phase-1 since the enum arm is dead — `enum_valid`→true"*. The rejection machinery is built; nothing ever triggers it.
3. **074 landed the data, unused.** Feature 074 added an additive tag-keyed enum side-table on `dict_metadata_handle` (flat `std::pmr::vector<EnumValueRef>` + sorted-by-tag run index, binary-searched — `src/dictionary/dictionary.cpp:178-191`), exposed read-only as `Dictionary::enum_values(tag) → std::span<EnumValueRef const>` (`dictionary.hpp:184-191`). **`as_table_view()` never copies it in and `enum_valid` never reads it.** The data sits inert.

**The scoping fork (source-verified, resolved in this spec).** `Dictionary::enum_values` is populated **only by `OrchestraLoader`**. `XmlLoader` — which produces all **nine** legacy QuickFIX dictionaries — **never reads the `<value enum="…" description="…"/>` elements at all**; those dictionaries carry an empty enum store (documented at `dictionary.hpp:66-67`). Wiring `enum_valid` to the existing store alone would deliver enum validation for **FIX Latest only** and leave the real-world FIX 4.4 `Side(54)`/`OrdType(40)` case untouched. This spec therefore scopes to **all ten dictionaries** (FR-001).

**Census of what is actually at stake** — the **nine XmlLoader dictionaries**, i.e. the ones 075 newly populates (measured from the shipped `dictionaries/*.xml`, 2026-07-14; re-verified at Gate A round 2). The **tenth** dictionary, `dictionaries/orchestra/OrchestraFIXLatest.xml`, is deliberately **not** a row here: its code sets are **074's**, already populated by `OrchestraLoader` and untouched by 075. *(Its shape is not commensurable with these columns anyway — Orchestra declares codes in shared `fixr:codeSet` elements referenced by field `type`, not inline per field, so "enum-backed fields" is not a count the file yields directly. File-level, it carries 5708 `fixr:code` values.)* **SC-002's exact-count leg is scoped to these nine** — see SC-002 (finding O2-7):

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

**Census of the code *strings* themselves** (measured 2026-07-14 — the census above counts *shapes*; this one reads what the codes actually **say**, which is a different and independently load-bearing question):

| Property | Result across all ten shipped dictionaries |
|---|---|
| Declared codes containing a **space** | **1** — `SettlLocation(166)` in **FIX41** and **FIX42** declares the literal code **`"ISO Country Code"`** (alongside `CED, DTC, EUR, FED, PNY, PTC`). Tag 166 is typed `STRING`, **single-value**. |
| Duplicate codes / `<value>` missing `enum` / missing `description` | **0 / 0 / 0** |

`"ISO Country Code"` is a **prose documentation placeholder**, not a wire literal — it means *"any ISO country code goes here"*. Under FR-003/FR-009 (non-empty codeset ⇒ byte-exact whole-token membership) a strict-validating **FIX 4.1 / 4.2** session will, after 075, **reject `166=US` / `166=GB` / `166=DE` with reason 5** — conformant, real-world traffic. QuickFIX rejects it too (`isFieldValue` → `set.find("US")` → miss → reason 5), so this is **parity-correct**, but it is an operator-visible regression and it is **decided, not discovered**: see **FR-022** and the divergence register (`contracts/enum-domain.md` C-6). SC-011 pins the space-bearing codes to an **exact two-entry exception set**, so this is a *gate* rather than a surprise — and any *other* placeholder, present or added by a future refresh, fails the build.

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
4. **Given** a message whose out-of-domain enum field sits **inside a repeating group** (and, separately, inside a **nested** group), **When** it is validated, **Then** it is rejected identically (reason 5, `RefTagID` = the offending member tag) — because the **Step-1 flat walk** (`validator.hpp:139-158`) scans the raw frame bytes and yields group members at every depth. *(This is a **declared divergence** from QuickFIX, which does **not** enum-check group members at all — FR-023, divergence register row **DV-3**. The witness MUST exercise the group path through `validate()` / the Step-1 walk, **not** through `validate_field()`, which has no message context and is a separate surface — FR-020.)*

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
- **Empty field value** (`54=` with nothing before the SOH) — **RESOLVED (FR-008, disposition (a)): excluded from the enum check — but the resulting disposition is TYPE-ARM-DEPENDENT, and that is the trap.** Skipping the enum check does not end the message's journey: it falls through to `check_field_type`, so an empty **`Char`** field (`Side(54)`) **rejects** with reason 5 (`value.size() != 1`) while an empty **`String`** field (`ExecInst(18)`) is **ACCEPTED** (the `ft::String` arm imposes no constraint). QuickFIX rejects/5 for **both** under the FR-019 pinning. So fixpp matches on Char *by coincidence, via a different arm* (**DV-1**) and diverges on String (**DV-2**). The corpus carries empty × {Char, String} as **two** rows, both `asserted: false` — a single undifferentiated empty-value row would pass on Char and fail on String, encoding nothing.
- **Out-of-domain enum inside a repeating group** — fixpp **rejects** it, at every depth (the Step-1 flat walk over the raw frame bytes reaches group members). **QuickFIX does not check group members at all** (`iterate` never descends into `m_groups`). This is a **declared divergence** — fixpp is deliberately stricter (**FR-023**, register row **DV-3**), not a "free inheritance" as an earlier draft claimed.
- **A declared code that is not a wire literal** — `SettlLocation(166)` in FIX41/FIX42 declares the literal code `"ISO Country Code"`, a prose placeholder. After 075 a strict FIX 4.1/4.2 session **rejects `166=US`** (QuickFIX does too). **Accept-and-document** (**FR-022**, register row **DV-4**); SC-011's **exact-exception-set** assertion over space-bearing codes makes it a gate rather than a surprise.
- **Multi-value tokenization degenerate forms** — leading/trailing/repeated spaces (`18= 1  G `). **Resolved (FR-014): reject.** QuickFIX splits on a single space and does not skip empty tokens, so an empty token is looked up, is never a declared code, and rejects. We match byte-for-byte rather than inventing a more forgiving tokenizer.
- **Value that is a strict prefix of a declared code** — `MatchType(574)=A` on FIX44, whose codeset is `A1, A2, A3, A4, A5, AQ, S1…S5, M1, M2, MT, M3…M6` and declares **no bare `A`**. Must reject; the comparison is whole-token equality, never a prefix match. *(Corrected at Gate A round 2 — finding O2-1. The example was `277=A` "where only `AX` is declared", which is **false against the shipped `dictionaries/FIX44.xml`**: `TradeCondition(277)` **declares `A`** (codes `A`–`N`, `P`, `Q`, `R`) and does **not** declare `AX`. Both engines would have **accepted** it, so the corpus row asserting a reject was an accept-accept row no mutation could redden — FR-009 would have shipped with **zero** discriminating witnesses. `574` is `STRING`, **single-value** (no tokenizer interaction), non-header, and a top-level field of `TradeCaptureReport(35=AE)`.)*
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
- **FR-006**: A detected out-of-domain value MUST surface as the existing out-of-range slot so it maps to **SessionRejectReason 5**, with `RefTagID` set to the offending tag, for **every field the validator's Step-1 walk yields**. *Mechanism (source-anchored, corrected at Gate A round 1):* Step 1 (`validator.hpp:139-158`) iterates a **dict-free `field_iterator` over the raw frame bytes** (`parser.hpp:229-233`, `:444`) — a linear `tag=value` scan with no group awareness — so it yields **header, body, repeating-group members at every depth, and trailer** from a single loop. Group-member and header coverage is therefore a property of **that one walk**, *not* of a second call site. The earlier claim that `validator.hpp:325` is "the group-member call site" was **wrong**: `:325` is `validate_field(tag, value)`, a context-free public virtual (FR-020). A witness aimed at `validate_field()` does **not** discharge the group leg of this FR.
- **FR-007**: The enum-domain check MUST be `noexcept`, allocation-free, and no worse than O(log T) tag lookup + O(C) scan of that tag's codes (T = enum-backed tags, C = codes for the tag). No per-message or per-field heap allocation; no `std::string` materialization of the wire value.
- **FR-008**: **Empty field value — DECIDED (a): excluded from the enum check; the resulting disposition is TYPE-ARM DEPENDENT (DV-1 / DV-2).** *(Headline corrected at Gate A round 2 — finding O2-5. It still read "**OPEN, resolve empirically at `/plan`**" while its own body, eight lines down, records the decision — an invitation for `/tasks` to emit a NEEDS-CLARIFICATION task for a call that is made. The decision history below is retained because it is the bundle's cautionary tale.)* **Do NOT assume reason-5 parity.** An earlier draft asserted "empty value → out-of-domain → reason 5, matching QuickFIX". **That is wrong under QuickFIX's defaults**: `iterate` calls `checkHasValue` *before* `checkValue` (`DataDictionary.cpp:168` vs `:172`), and `m_checkFieldsHaveValues` **defaults to `true`** (`DataDictionary.cpp:43`) — so an empty value throws `NoTagValue` → `SessionRejectReason_TAG_SPECIFIED_WITHOUT_A_VALUE` = **4** (`Session.cpp:1267-1268`) and **never reaches the enum check**. Only with that flag disabled would empty reach `checkValue` and yield 5. Compounding this: **fixpp has no reason-4 slot at all** — `reject_reason_map.hpp` emits only 14/2/1/5/6. So routing empty values through the new enum check would *manufacture* a divergence (fixpp 5 vs QuickFIX 4), not achieve parity. `/plan` MUST choose and record one of: **(a)** exclude empty values from the enum check and leave today's disposition untouched (narrowest, no new reject slot, accepts that fixpp still lacks reason 4 — a *pre-existing* gap this feature does not widen); **(b)** add the reason-4 slot for true parity (correct, but scope growth beyond enum validation); **(c)** accept the 5-vs-4 divergence and document it as a B-row. **(a) is the choice — DECIDED at Gate A round 1** (Clarifications, Session 2026-07-14 (Gate A round 1)). It keeps this feature's blast radius to enum domain only.

  **What (a) actually means is NOT "fixpp accepts empty" — it is TYPE-ARM-DEPENDENT, and that is the real finding.** Skipping the enum check (rule 2) does not end the message's journey: the disposition falls through to `check_field_type` (`validator.hpp:154`), whose behavior on an **empty** value depends on the field's **type arm**:
  - **`Char`** (e.g. `Side(54)`) → `value.size() != 1` → **reject**, `wire_field_value_out_of_range` → reason **5** (`validator.hpp:411-417`).
  - **`String`** (e.g. `ExecInst(18)`, `SettlLocation(166)`) → the `ft::String` arm imposes **no** constraint (`validator.hpp:419-425`) → **ACCEPT**.

  QuickFIX under the FR-019 pinning (`m_checkFieldsHaveValues=false`) does `set.find("")` → miss → `IncorrectTagValue` → reason **5** for **both**. So fixpp matches QuickFIX on **empty × Char** *by coincidence, via a different arm* (its type check, not its enum check), and **diverges on empty × String**.

  Consequences, all normative: **(i)** the FR-018 corpus MUST carry **empty × Char** *and* **empty × String** as **two separate rows**, both marked **`asserted: false`** (characterization-only) — a single undifferentiated "empty-value" row would pass on Char and fail on String, i.e. a gate that looks half-green and encodes nothing. **(ii)** Both rows enter the divergence register (`contracts/enum-domain.md` C-6) as **DV-1** / **DV-2**. **(iii)** **L-075-1** records *two* facts, not one: fixpp has **no reason-4 mapping** (`reject_reason_map.hpp:15-75` maps only 1/2/5/6/14) **and** fixpp's empty-value disposition is **type-arm dependent**.
- **FR-009**: Comparison MUST be byte-exact and whole-token — no case folding, no prefix matching.
- **FR-010**: Enum-domain validation MUST ride the **existing** `validate_inbound_messages` flag — **no new config surface, no sub-flag** (Clarification Q2, matching QuickFIX, which has no separate enum switch). A session with inbound validation off MUST be bit-for-bit unaffected. The resulting behavior change for existing strict-validating sessions MUST be recorded as a `spec/behaviors-and-limitations.md` **B-row**. *(Gate A round 1 — the earlier clause said "and in the release notes", which named **no artifact**: there is **no `CHANGELOG.md`** in this repo, so that clause was undischargeable and would have been silently dropped at `/tasks`. **Resolved: the B-row IS the release-note artifact.** `spec/behaviors-and-limitations.md` is this repo's operator-facing behavior-change record and the only one that exists; the B-row MUST therefore be written for an **operator** audience — naming the flag, the new reject reason, and the affected tags/dictionaries (incl. FR-022's tag 166 on FIX 4.1/4.2) — not as an internal note. No second artifact is invented.)*
- **FR-011**: Zero C-ABI change. The C ABI is GA-frozen at `1.5.0`; this feature MUST NOT alter `include/fix/c_api*` or the exported symbol set.
- **FR-012**: Every pre-existing dictionary, validator, session, and codegen test MUST pass unchanged except where a test asserted the *old* accept-an-invalid-enum behavior; any such test MUST be updated with its change explicitly called out (never silently re-baselined).
- **FR-013**: Enum-domain checking MUST apply to **all** validated inbound messages — application **and** admin, **including Logon**, which fixpp validates before `interpret_logon` (QuickFIX parity, Clarification Q3). Reject(3) and Logout(5) remain exempt via the pre-existing no-reject-loop guard. A Logon carrying an out-of-domain admin enum MUST be pinned by a direct test showing it is rejected with reason 5 and that the session does not establish; this operator-visible consequence MUST be recorded as a B-row.
- **FR-014**: The multi-value tokenizer MUST split on a **single space** and MUST NOT skip empty tokens — a double space or a trailing space yields an empty token, which is never a declared code and therefore rejects. This is byte-for-byte QuickFIX behavior (`DataDictionary.h:265-275`) and is required for interop parity, not merely permitted.
- **FR-015**: **Header and trailer fields are in scope where the dictionary declares them.** fixpp's validator Step 1 iterates **every present field** of the message (`validator.hpp:139-158` — `msg.begin()`→`msg.end()`, a dict-free linear `tag=value` scan of the raw frame bytes, `parser.hpp:229-233`), which includes standard-header fields; QuickFIX likewise runs `checkValue` over header, trailer **and** body (`DataDictionary.cpp:149-156`). Enum-backed **header** fields therefore MUST be domain-checked on dictionaries that actually declare `<header>` / `<trailer>` sections. In FIX44 these are `MsgType(35)` (**93** codes — *corrected at Gate A round 1; the earlier "92" was wrong in three documents*), `PossDupFlag(43)` (`Y`/`N`), `PossResend(97)` (`Y`/`N`), and `MessageEncoding(347)`. FIX50 / FIX50SP1 / FIX50SP2 declare **empty** `<header />` / `<trailer />` blocks (`FIX50SP2.xml:2`, `:4674`; same shape in `FIX50.xml:2`/`:2694` and `FIX50SP1.xml:2`/`:2976`) — so `header_node_` / `trailer_node_` (`xml_loader.cpp:621-622`, `root.child("header")`) are **non-null**, the null guard at `xml_loader.cpp:739`/`:743` **does not** fire, and `xml_loader.cpp:738-745` **does** fold them; the fold simply expands an element with **no children** and contributes **zero** fields. Those three dictionaries therefore contribute no dictionary-known header/trailer fields to Step 1 — by *emptiness*, not by absence. *(Corrected at Gate A round 4 — C4-2: the earlier wording said they "declare **no** `<header>` / `<trailer>` blocks" and blamed the null guard; both legs were false, though the conclusion was not.)* A test MUST pin at least `PossDupFlag(43)=X` → reject with reason 5, `RefTagID=43`, since a body-only implementation would silently pass it and break SC-009 parity.
- **FR-016**: **MsgType(35) self-consistency guard.** `MsgType(35)` is itself an enum-backed header field, so enabling the domain check makes every message type's *own* MsgType value subject to it. A dictionary that declares a `<message msgtype="X">` while omitting `X` from tag 35's code set would have that message type rejected outright — a self-inflicted, total loss of a message type. A **load-time or test-time census** MUST assert, for every shipped dictionary, that each declared message type is either present in the `MsgType` code set **or** that the code set is empty (unconstrained). *(Measured 2026-07-14: all nine QuickFIX dictionaries satisfy this — but **FIXT11's `MsgType` field declares ZERO `<value>` children**, so its 8 message types pass only by virtue of FR-003's empty-set-accepts rule. That is a load-bearing dependency, not a coincidence, and a dictionary refresh could silently violate it.)*
- **FR-017**: **Loader tolerance/strictness on code-set XML, matching QuickFIX** (Clarification Q4): a **duplicate** `<value enum="X">` on one field is **deduped** (union semantics, no error); a `<value>` **missing its `enum` attribute** is a **load failure** (fail-closed, mirroring QuickFIX's `ConfigError`, and using this codebase's existing `xml_parse_error` family per the 072/074 catch-discriminated precedent); a `<value>` **missing its `description`** is **legal** and yields an empty description view — it MUST NOT fail the load, since `EnumValueRef`'s description is diagnostics-only.
- **FR-018**: **Parity is DERIVED from the golden, never ASSERTED ahead of it — and the golden is a BLOCKING FIRST DELIVERABLE (Phase 0.5).** *(Restructured at Gate A round 1. The prior wording wrote the parity claims first and deferred the golden to a later task, so the corpus was designed from the same partial reading of QuickFIX's call graph it was supposed to check — and was therefore structurally incapable of detecting that reading's errors. Two such errors are now known: the group-member divergence (FR-023) and the type-arm-dependent empty-value case (FR-008).)*

  The golden — a checked-in expectation table produced by a checked-in generator that runs a bounded corpus of boundary frames through **real QuickFIX `DataDictionary::validate`** — MUST be **built and its output recorded before `/tasks` closes the parity FRs**. It is not evidence *for* a parity claim written elsewhere; its **measured output IS the definition** of the divergence register (`contracts/enum-domain.md` C-6). No FR or SC in this bundle may contain a sentence of the form *"fixpp matches QuickFIX except X"* unless X is either (a) a claim the golden is **required to prove**, or (b) an explicitly argued, B-rowed **declared divergence** carrying a register row.

  **Corpus — every row carries an `asserted: true|false` discriminator.** The golden records QuickFIX's verdict + reject reason for **every** row; fixpp is asserted to match **only** on `asserted: true` rows. An `asserted: false` row is **characterization-only**: it records what each engine does and feeds a divergence-register row. Required rows, one per fixpp Step-1 surface:

  | # | Corpus row | `asserted` | Notes |
  |---|---|---|---|
  | 1 | in-domain single value | true | |
  | 2 | out-of-domain single value | true | the headline |
  | 3 | multi-value, all tokens declared | true | `ExecInst(18)=1 G 6` |
  | 4 | multi-value, one token undeclared | true | `18=1 ZZ 6` |
  | 5 | degenerate whitespace (double / trailing space) | true | FR-014 |
  | 6 | enum-backed **header** field (`PossDupFlag(43)=X`) | true | FR-015 |
  | 7 | strict prefix of a declared code (**`MatchType(574)=A`** on FIX44, msg `AE` — the codeset is `A1, A2, …`; **no bare `A`**) | true | *Re-based at Gate A round 2 — O2-1. Was `277=A`, which **`TradeCondition(277)` actually declares** ⇒ accept-accept ⇒ non-discriminating.* |
  | 8 | **empty value × `Char`** (`54=`) | **false** | fixpp reason 5 via its **type** arm; QuickFIX reason 5 via its **enum** arm. Parity **by coincidence** → **DV-1** |
  | 9 | **empty value × `String`** (`18=`) | **false** | fixpp **accepts**; QuickFIX rejects/5 → **DV-2** |
  | 10 | **out-of-domain enum on a repeating-group member** (a member of `NoPartyIDs(453)` in a `NewOrderSingle`) | **false** | fixpp **rejects**; QuickFIX **never checks it** → **DV-3** (FR-023) |
  | 11 | **out-of-domain enum on a NESTED-group member** (depth ≥ 2) | **false** | same divergence, at depth — pins that fixpp's flat walk reaches every depth |
  | 12 | **`SettlLocation(166)=US`** on FIX41/FIX42 (the `"ISO Country Code"` placeholder codeset) | true | **both** engines reject/5 — parity-correct, but an operator-visible regression → **DV-4** (FR-022) |

  **Why there is NO `validate_field()` row in this corpus — and why that is not a gap.** *(Gate A round 4, finding O4-1.)* The FIX50SP2 store-only witness `validate_field(1128, "bogus")` was briefly carried here as a 13th row with `asserted: true`. It does **not** belong: this corpus is a **QuickFIX-parity** artifact whose contract is *"QuickFIX's measured verdict + reason, per row"*, and **QuickFIX has no context-free `validate_field` analogue** — every row it can measure is a *message frame* driven through `DataDictionary::validate`. Worse, the row schema has **no column naming which fixpp surface drives a row**, so a uniform harness would drive it through `validate()`, where the two engines **check in opposite order** on the overlap {tag not valid for this msg_type} × {value out of domain}:

  - **fixpp** runs `field_valid_for` **first** (`validator.hpp:143`) → `wire_unexpected_tag` → reason **2**, and never reaches `enum_valid` (`:148`);
  - **QuickFIX** runs `checkValue` (`DataDictionary.cpp:172`) **before** `checkIsInMessage` (`:178`) → reason **5**.

  `ApplVerID(1128)` is *precisely* a message-unreachable tag, so that row — and only that row — walks straight into the difference: golden **5** vs fixpp **2**, on an `asserted: true` row, which SC-009 defines as a **defect that blocks the feature**. A spurious divergence manufactured by the corpus itself. **The witness is retained where it is genuinely discriminating** — the **FR-020 unit test** (`plan.md`'s test matrix; mutation cell = *"build the enum-domain table only from `message_fields()`"*), whose power is against **fixpp's own projection**, not against QuickFIX; it never needed a QuickFIX verdict.

  **This check-ordering difference is a recorded FACT, not a register row.** It is deliberately **not** promoted to a DV-* divergence: it is unobservable on every row this corpus can legitimately contain (every other row uses an **in-message** tag, so both engines reach the enum arm and agree on 5), and fixpp's order is not a behaviour anyone must match. It is stated here so a future corpus author does not re-introduce the trap by adding a message-unreachable-tag row. *(`research.md` R-8 carries the same fact.)*

  **Every asserted literal in this corpus was RE-MEASURED against the shipped `dictionaries/*.xml` at Gate A round 2** *(root cause RC#3, which recurred: the round-1 rewrite added the code-**string** census and then authored row 7 without checking what tag 277 actually declares — see O2-1)*.

  **The rule this enforces — stated narrowly, because the broad version is false**: **a `reject`-asserting row that BOTH engines ACCEPT is a defect, not a row.** Such a row coincides exactly with the **stub's** behaviour (`enum_valid → return true` accepts everything), so it passes green forever, it does **not** exercise the rule it names, and **none of the mutations this bundle specifies can redden it** — every one of them (`enum_valid → true`; whole-string lookup instead of the tokenizer; body-only walk) flips **reject** rows to accept, never the reverse. That is exactly how row 7 got in. *(The **accept**-asserting rows — 1 and 3 — are the paired positive controls that guard against **over**-rejection, and they are **not** caught by this rule: row 3 reddens under the tokenizer mutation, and row 1 is row 2's accept-side partner on the same tag. Do not read this rule as condemning them.)*

  Measured 2026-07-14, all in FIX44 unless stated:

  | Row | Literal | Declared codes (measured) | Expected verdict | Discriminating? |
  |---|---|---|---|---|
  | 1 | `Side(54)=1` | `1` **is** declared | accept | ✅ *positive control* — pairs with row 2 on the same tag; guards against over-rejection (an absent-tag⇒reject or reject-everything regression reddens it) |
  | 2 | `Side(54)=Z` | `1`–`9`, `A`–`G` (16, `CHAR`) — no `Z` | reject/5 | ✅ `Z` is a valid `char`, so only the enum arm catches it |
  | 3 | `ExecInst(18)=1 G 6` | 40 codes incl. `1`, `G`, `6` (`MULTIPLEVALUESTRING`) | accept | ✅ *positive control* — reddens under the whole-string-lookup mutation |
  | 4 | `18=1 ZZ 6` | — no `ZZ` | reject/5 | ✅ |
  | 5 | `18=1  G` / `18=1 ` | empty token is never declared | reject/5 | ✅ (FR-014) |
  | 6 | `PossDupFlag(43)=X` | `Y`, `N` (`BOOLEAN`) — no `X` | reject/5 | ✅ the `Boolean` type arm imposes **no** constraint (`validator.hpp:419-425`), so only the enum arm catches it |
  | **7** | **`MatchType(574)=A`** | `A1`–`A5`, `AQ`, `S1`–`S5`, `M1`, `M2`, `MT`, `M3`–`M6` (18, `STRING`, single-value) — **no bare `A`** | reject/5 | ✅ **re-based this round** |
  | 12 | `SettlLocation(166)=US` **FIX41/FIX42** | `CED`, `DTC`, `EUR`, `FED`, `ISO Country Code`, `PNY`, `PTC` — no `US` | reject/5 | ✅ |

  Also re-measured for the witnesses that live outside this corpus: `EncryptMethod(98)` = `0`–`6` (so `98=9` — SC-008's Logon witness and FR-021 artifact #3 — is genuinely undeclared ✅); `ClOrdID(11)` carries **zero** codes (SC-004's unconstrained-tag pin ✅); `MsgType(35)` = **93** codes ✅; FIXT11's `MsgType` = **zero** `<value>` children ✅; the multi-value census (FIX44 `18, 276, 277, 286, 291, 292, 529, 546`; FIX50/SP1 add `1031, 1035`; **FIX50SP2 has 9** — tag `1035` is `MULTIPLESTRINGVALUE` but declares zero `<value>` children, so it is not enum-backed) matches the Context table on every dictionary ✅.

  The golden is data, not prose. CI asserts fixpp's verdicts against the **checked-in golden**, and CI MUST NOT depend on the **out-of-repo** `reference-engines/` tree (it sits **outside the submodule's git boundary**, so the library's own `.gitignore` says nothing about it — the protection is **structural**, per FR-024, not a gitignore claim).

- **FR-019**: **The golden generator's QuickFIX configuration MUST be pinned explicitly — booleans AND dictionary topology — and recorded inside the golden.** `DataDictionary` carries several independent switches that are *not* about enum domain — `m_checkFieldsHaveValues` (default **true**), `m_checkFieldsOutOfOrder`, `m_checkUserDefinedFields`, `AllowUnknownMsgFields`. If the generator leaves the wrong ones set, the differential conflates enum-domain divergence with unrelated validation differences and reports **false divergences**. Every non-enum flag MUST be pinned to match fixpp's own validation choices so the golden isolates the enum boundary. *(This is precisely how FR-008's false parity claim arose — `m_checkFieldsHaveValues` silently front-runs the enum check.)*

  **The dictionary topology MUST be pinned too, and it is the bigger lever.** `DataDictionary::validate` takes **two** dictionaries and splits the work (`DataDictionary.cpp:145-156`): a **session** DD for header+trailer and an **app** DD for the body — a path `Session.cpp:1221-1229` selects whenever the session `isFIXT()` and the message `isApp()`. On a FIXT/FIX50SP2 session QuickFIX therefore enum-checks `MsgType(35)` against **FIXT11**, whose `MsgType` declares **zero** `<value>` children — leaving `MsgType` completely unconstrained. **fixpp has exactly one dictionary** (`SessionConfig::dictionary`; `src/session/engine.cpp:210`, `src/session/session.cpp:992`, `:1234` — no session-DD/app-DD split). The golden MUST therefore be generated on the **single-dictionary (non-FIXT) topology** — `sessionDD == appDD`, i.e. `sessionDataDictionary.validate(message)`, the FIX 4.x path — **applied per the corpus row's own dictionary**, which is the topology fixpp actually has; a two-DD golden would measure a validation topology fixpp does not possess and FR-015's header-parity claim would be false against it.

  *(Corrected at Gate A round 2 — finding O2-6. The pin read "single-dictionary **FIX 4.4** topology", naming one **version**, while the corpus demonstrably spans more than one dictionary: the `SettlLocation(166)=US` row (**row 12**; numbered 13 until Gate A round 4 dropped the `validate_field` row — O4-1) is on **FIX41/FIX42** — FIX 4.4 does not declare the placeholder code at all — and FR-024's `dictionary_sha1` is defined over **every** dictionary the corpus loads. The property actually being pinned is `sessionDD == appDD`, which holds for **every** 4.x dictionary; the version was never the invariant. The manifest therefore records the topology **per dictionary**, alongside each `dictionary_sha1` — FR-024.)*

- **FR-020**: **`validate_field(tag, value)` is a THIRD public surface and is IN SCOPE.** *(Decided at Gate A round 1.)* It is a frozen pure-virtual on `Validator` (`validator.hpp:65-66`, `[const §XIV.2]` 5-virtual cap) whose body calls `enum_valid` at `:325` **with no `msg_type` context**. It will begin enum-validating as a direct consequence of 075. It is **in scope with tests**, not silently along for the ride: the enum domain is **per-tag, not per-message-type** (FR-003's table is keyed by tag alone), so the absence of message context costs `validate_field()` nothing and its enum behavior is well-defined and identical to Step 1's. Rejected alternative — *keep it type-only* — would require a deliberate flag or a second `enum_valid` overload to suppress a **correct** check on one surface, and would leave two public surfaces with divergent domain semantics. It MUST carry its own named witnesses (in-domain accept / out-of-domain reject / multi-value / empty), **including a store-only-tag witness on FIX50SP2 (`validate_field(1128, "bogus")` → reject; `validate_field(1128, "9")` → accept)** so the bundle can distinguish the required enum-store projection from both the stub and a `message_fields()`-only projection. The existing test that asserts the **opposite** (`tests/wire/validator_type_check_test.cpp:257-275`, *"Phase-1: enum_valid always true; validate_field must accept"*) MUST be flipped under FR-012's called-out-not-silent rule.

- **FR-021**: **`table_view::add_enum()` MUST become REAL, and the six existing test artifacts it invalidates are enumerated here.** *(Decided at Gate A round 1. Its fate was previously never decided anywhere in the bundle — yet it is the **only** way test fixtures populate enum data today, and **both** outcomes were broken as specified.)*

  `add_enum(tag, value)` (`table_view.hpp:350-352`) currently discards both arguments. **If it stayed a no-op**, every builder-built `table_view` would have an empty enum table, FR-003's floor would make `enum_valid` return `true` for all of them, and four suites would stay **green while proving nothing** — `[[feedback_coverage_push_enshrines_bugs]]` verbatim. It therefore **becomes real**: it populates the same owned enum-domain table `as_table_view()` builds. It **cannot carry the multi-value bit by itself** (FR-005), so it MUST gain a companion — an `add_enum` overload or a `set_multi_value(tag)` builder — otherwise the tokenizer (FR-004) has no unit-level witness that does not require a full XML load. `tests/support/mock_dict_table.hpp:16` exists precisely to keep builder-surface parity with the production type and MUST track the change.

  **Named census of the artifacts this invalidates** (FR-012 requires each change be explicitly called out, never silently re-baselined; an open-ended carve-out is not a list, so here is the list):

  | # | Artifact | Asserts today | After 075 |
  |---|---|---|---|
  | 1 | `tests/wire/validator_domain_test.cpp:559-592` | *"Phase-1: enum_valid always true; `Side=X` must be accepted (FR-005)"* | flips to **reject/5** |
| 2 | `tests/wire/validator_type_check_test.cpp:257-275` | `validate_field()` accepts an enum violation (Phase-1) | flips to **reject/5** (FR-020), including the FIX50SP2 store-only witness `validate_field(1128, "bogus")` |
  | 3 | `tests/wire/validator_per_version_test.cpp:212-230` | `EncryptMethod(98)=9` must be **accepted** | flips to **reject/5** |
  | 4 | `tests/dictionary/table_view_test.cpp:256-269` | *"`enum_valid` must always return true (Phase-1)"* | rewritten to assert the **real** domain check (incl. the absent-tag ⇒ accept floor) |
  | 5 | `tests/wire/conformance/w014_validate.csv` (the enum row) | oracle row encoding Phase-1 pass-through | **data**, not code — it will not fail to compile, it fails at runtime with an unhelpful diff. MUST be updated deliberately. |
  | 6 | `tests/support/mock_dict_table.hpp:16` | builder-surface parity incl. the `add_enum` no-op | tracks the real builder + its multi-value companion |

- **FR-022**: **Declared-code hygiene, and the `SettlLocation(166)` decision.** A declared code that is not a plausible wire literal silently converts a codeset into a reject-everything trap. `SettlLocation(166)` in **FIX41/FIX42** declares the literal code `"ISO Country Code"` — a prose placeholder (Context census). **Decision (Gate A round 1): accept-and-document, do NOT carve out.** Rationale: QuickFIX rejects `166=US` identically, so a carve-out would *manufacture* a divergence from the reference engine to paper over bad vendored data; and any carve-out heuristic (*"ignore codes containing a space"*) is an invented rule neither the FIX spec nor the engine has, and it would silently weaken a real codeset the day a legitimate space-bearing code appears. Recorded as divergence-register row **DV-4** and as an operator-facing **B-row** in `spec/behaviors-and-limitations.md` that **names tag 166 on FIX 4.1 / 4.2 explicitly** — per FR-010, **the B-row IS the release-note artifact**; no second artifact exists or is invented. *(Corrected at Gate A round 2 — finding C2-2 / root cause RC#4. This FR, written in the round-1 rewrite, re-invented the "release-note line" that **FR-010 — one FR earlier, in the same pass** — had just established does not exist.)* SC-011 makes the property a **gate**: it asserts that the space-bearing codes across the ten shipped dictionaries are **exactly** `{FIX41:166:'ISO Country Code', FIX42:166:'ISO Country Code'}` — green today with that exception set pinned, and RED the moment a dictionary refresh adds, removes, or alters a space-bearing code.

- **FR-023**: **Declared divergence — fixpp enum-checks repeating-group members; QuickFIX does NOT.** *(Decided at Gate A round 1. This was previously an *undeclared* divergence smuggled in under a "nested groups inherit it for free" claim, while SC-009 simultaneously declared the empty-value case "the one known divergence" — a factually false sentence, now removed.)*

  **Evidence — a closed call-site census of QuickFIX v1.16.0**, not a spot read: `isFieldValue` (`DataDictionary.h:255-276`) has exactly one caller, `checkValue` (`:493-501`); `checkValue` has exactly one call site in the entire engine — `DataDictionary.cpp:172`, inside `DataDictionary::iterate`'s per-field loop; `iterate` is called exactly three times (`:150`, `:151`, `:155`, all inside `validate`, over header / trailer / body) and **never recurses**; `iterate`'s only loop is over `FieldMap::begin()..end()`, which returns **`m_fields`** iterators (`FieldMap.h:233-236`) — repeating-group instances live in a **separate** member, `m_groups`, reachable only via `g_begin()/g_end()` (`:237-240`), which `iterate` never calls. ⇒ **A group-member field's value can never reach `checkValue`.** The only group-aware check in the path is `checkGroupCount` — a **count** check, not a domain check.

  **fixpp's Step-1 flat walk over the raw frame bytes DOES cover group members at every depth** (FR-006). **Disposition: KEEP IT — fixpp is deliberately stricter and more correct here.** QuickFIX's behavior is a known weakness (an out-of-domain enum inside a repeating group passes its validator entirely), and fixpp's flat walk makes the check free. Rejected alternative — *suppress the check for group members to match QuickFIX* — would mean writing deliberate code to make a **correct** check not run, on the exact class of field (party roles, leg sides, MD entry types) where domain errors are most costly.

  This divergence MUST be: recorded as register row **DV-3** (`contracts/enum-domain.md` C-6); carried as a **B-row**; **removed from SC-009's parity assertion set** (corpus rows 10 and 11 are `asserted: false`); and pinned by the group **and nested-group** witnesses of AC US1 #4.

- **FR-024**: **The golden MUST embed a MANIFEST, and CI MUST gate on it.** *(Gate A round 1.)* `reference-engines/` is **not in the library repo at all** — it lives at the **parent** repo root (`../../../reference-engines/quickfix-cpp`, outside the submodule's git boundary), so the library's own `.gitignore` says nothing about it and CI never sees it. A checked-in golden produced by a generator CI never runs is therefore a **false-green surface of exactly the class this repo has been burned by twice** (`[[feedback_sanitizer_canary_must_be_proven_red]]`, `[[feedback_codegen_golden_exists_narrow_verify_misses_it]]`): it can go stale, be hand-edited, or be generated against the wrong dictionary, with **nothing** catching it.

  Required, all three:
  1. **Manifest inside the golden artifact**, carrying **six** fields:
     - **`quickfix_version` + soname** (`1.16.0` / `libquickfix.so.17.0.0`);
     - **`dictionary_sha1`** — SHA-1 of **every** dictionary the corpus loads;
     - **`generator_source_hash`**;
     - **`corpus_input_hash`** — over the corpus **inputs** (frames: dictionary, msg_type, tag, wire value);
     - **`golden_output_hash`** — over the golden's **output rows** (`quickfix_verdict` + `quickfix_reason` + `asserted`, per row). **This field is load-bearing and is NOT implied by the others**: an input hash, a generator hash and a dictionary hash **all stay valid** when someone hand-edits an `accept` to a `reject` in the checked-in table. Without a hash over the *verdicts*, the manifest gate would be decorative — exactly the false-green it exists to prevent. It MUST be computed over the row content **excluding the manifest block it lives in** (else it is self-referential and uncomputable);
     - the **dictionary topology, recorded PER DICTIONARY** alongside each `dictionary_sha1` — single-DD / non-FIXT, `sessionDD == appDD` (FR-019, corrected at Gate A round 2 — O2-6; the corpus spans FIX44 **and** FIX41/FIX42, so one version-named topology string cannot describe it) — and **all four boolean flag settings** (FR-019). The four booleans **and** the topology MUST be set **in the generator's own source**, not passed as CLI arguments, so that `generator_source_hash` genuinely pins them; a runtime-configurable flag would leave them recorded but unpinned.
  2. **A CI test asserting the manifest matches the checked-in artifact** — it recomputes the dictionary SHA-1s, the generator-source hash, the corpus **input** hash **and the golden output hash** from the tree, and fails on any mismatch. This runs **without** `reference-engines/`. **What it catches, exactly** *(narrowed at Gate A round 2 — finding O2-3; the earlier claim that it makes "a **stale** … golden fail" was overstated)*: **(i) tree drift** — a dictionary, corpus input or generator-source change that the golden was not regenerated against; and **(ii) a careless hand-edit** of the checked-in verdicts (`golden_output_hash`). What it **cannot** catch is drift against a **newer QuickFIX**: `quickfix_version` + soname are **recorded but unverifiable in CI**, because CI has no QuickFIX by this design's own premise — nothing in the tree can falsify the string `1.16.0`. That leg is item 3's job, not this one's. **Its discriminating witness (quickstart S-7 step 5): hand-edit one verdict in the checked-in golden ⇒ the test MUST turn RED.** A gate never proven RED proves nothing (`[[feedback_sanitizer_canary_must_be_proven_red]]`).
  3. **A local regeneration-and-diff target** that, **when `reference-engines/` is present**, rebuilds the golden and **fails on drift**. The generator's QuickFIX root MUST be a **validated CMake cache variable** (`FIXPP_QUICKFIX_ROOT`, default `${CMAKE_SOURCE_DIR}/../../../reference-engines/quickfix-cpp`), the target **guarded off by default**, and it MUST **hard-error** if the variable is set but `lib/libquickfix.so` is absent. *(That, not a gitignore claim, is what keeps CI from ever reaching the tree.)* Feasibility is confirmed: `libquickfix.so.17.0.0` **is built** on the development machine.

     **This target MUST have an owner and a cadence, or "stale" has no one to catch it** *(Gate A round 2 — finding O2-3)*. Guarded off by default with no schedule, it may never run again after Phase 0.5 — and it is the **only** mechanism that can detect a QuickFIX-version drift (see item 2). It is therefore **bound to the existing out-of-CI release/interop gate** (`[[project_release_interop_quickfix_fix8]]` — the per-release QuickFIX/Fix8 interop run, which already owns a local reference-engine tree): **run the regen-and-diff on every release-interop pass**, and treat a diff as a release-blocking finding. `/implement` MUST land that binding as a line in the release-interop checklist, not merely a CMake target.

### Key Entities

- **Code set** — the set of declared `{value, description}` pairs for one tag, per dictionary. Already modelled by 074's `EnumValueRef` and the tag-keyed store; this feature populates it for nine more dictionaries and *reads* it from the validator.
- **Enum-domain table** — the config-time, validator-facing projection of the code sets: tag → (codes, is-multi-value). Built once from the dictionary; immutable; consulted per field on the hot path.

---

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: A message carrying an out-of-domain value for a codeset-backed field is **rejected** with SessionRejectReason 5 and the offending tag as `RefTagID`, at both the top-level and repeating-group-member positions. Proven by a mutation-discriminating witness: reverting the domain check to always-accept must turn the test RED.
- **SC-002**: All **ten** dictionaries expose their declared code sets through `Dictionary::enum_values(tag)`. The **exact-count** leg is asserted over the **nine XmlLoader dictionaries** — per-dictionary enum-backed-field and total-code counts matching the Context census table (FIX44 = 245 fields / 1708 codes) — proven by a direct assertion against the shipped XML, not against a hand-maintained expectation list. *(Scoped at Gate A round 2 — finding O2-7. The criterion said "all **ten** … matching the census table", but that table has **nine** rows: the tenth dictionary, `dictionaries/orchestra/OrchestraFIXLatest.xml`, is not in it. Per `[[feedback_completeness_gate_exact_set_not_subset]]` an exact-count gate must not carry a silent carve-out, so the scope is now **stated**: the nine are what **075** newly populates and what the census table covers; the tenth's code sets are **074's** — populated by `OrchestraLoader`, untouched by 075, and pinned today by `tests/dictionary/orchestra_loader_test.cpp`'s `OrchestraCodesets.PreservesValuesAndDescriptions`. 075 adds no new exact-count gate over it.)*
- **SC-003**: Every one of the multi-value enum fields shipped in the dictionaries (8 in FIX44, 10 in FIX50/SP1, **9 in FIX50SP2**) accepts a conformant multi-token value and rejects a value with any undeclared token. Proven per-field for `ExecInst(18)` and by a table-driven witness across the full multi-value census.
- **SC-004**: A field with no code set, and a dictionary with no enum data, are accepted exactly as before — **zero** new rejections. Proven by the full existing conformance/validator/session suite passing green, plus a direct empty-store-accepts pin.
- **SC-005**: A session with inbound validation **disabled** exhibits byte-identical behavior to `main`.
- **SC-006**: No measurable throughput regression on the validated-message hot path relative to `main` (the existing bench gate is the arbiter); zero additional allocations per message.
- **SC-007**: Zero C-ABI diff; exported symbol set unchanged.
- **SC-008**: An inbound **Logon** carrying an out-of-domain admin enum is rejected with reason 5 and the session does **not** establish — pinned by a direct test, matching QuickFIX. Conversely, a Logon whose admin enums are all in-domain establishes exactly as today (no false-reject of the handshake).
- **SC-009**: **QuickFIX interop parity on the enum-domain boundary — DERIVED from the golden, not asserted.** *(Rewritten at Gate A round 1. The prior wording — "the **one known** divergence is the empty-value case" — was **factually false**: there are at least three, and the corpus that was supposed to police the claim had no row capable of surfacing any of them.)*

  fixpp's disposition matches QuickFIX's on **every `asserted: true` row** of the FR-018 corpus, measured against the FR-018 golden under the FR-019 pinning (four booleans **and** the single-dictionary / non-FIXT topology, `sessionDD == appDD`, applied per the row's own dictionary) with the FR-024 manifest gate green — **not** against a reading of the QuickFIX source. On an `asserted: true` row, divergence from the reference engine is a **defect**.

  **The register is not simply "the `asserted: false` rows".** It is the union of two things, and conflating them is what let DV-4 go unenumerated in the first place:

  1. **every deliberate divergence from QuickFIX** (necessarily `asserted: false` — we do not assert a difference), **and**
  2. **every operator-visible behaviour change that required an argued decision** — which may be **`asserted: true`**, because fixpp and QuickFIX can *agree* and still both break conformant traffic (that is exactly **DV-4**).

  So `asserted: false` ⇒ a register row, but **not** conversely. Each register row MUST carry an argued disposition, a B-row, and a corpus row that *measures* it:

  | Row | `asserted` | Divergence / behaviour change | Disposition |
  |---|---|---|---|
  | **DV-1** | **false** | empty × `Char`: both engines reject/5 — but fixpp via its **type** arm, QuickFIX via its **enum** arm | parity **by coincidence**; documented, not relied upon (FR-008) |
  | **DV-2** | **false** | empty × `String`: fixpp **accepts**; QuickFIX rejects/5 | accepted — fixpp has **no reason-4 slot** (L-075-1); routing empty through the enum check would manufacture a 5-vs-4 divergence (FR-008) |
  | **DV-3** | **false** | **repeating-group member enums**: fixpp **checks** them at every depth; QuickFIX **never** does | accepted — fixpp is deliberately **stricter and more correct** (FR-023) |
  | **DV-4** | **true** ⚠️ | `SettlLocation(166)="ISO Country Code"` placeholder codeset on FIX41/42: `166=US` rejects. **Not a divergence FROM QuickFIX** — the engines **agree** — but an operator-visible regression that required an argued decision, so it is a register row under leg **(2)** above | accept-and-document; a carve-out would manufacture a divergence to paper over bad vendored data (FR-022). Gated by SC-011's **exact-exception-set** assertion (these two entries pinned; any other space-bearing code fails the build) |

  A divergence discovered by the golden that is **not** in this register is a **defect** and blocks the feature until it is either fixed or promoted to an argued register row. The register is the golden's output, not this spec's input.
- **SC-010**: **No message type is bricked by its own MsgType code.** For every shipped dictionary, each declared message type is present in the `MsgType(35)` code set, or that code set is empty. Asserted by a census against the shipped XML (not a hand-maintained list), so a future dictionary refresh that violates it fails the build rather than silently disabling a message type. *(FIXT11 passes via the empty-set arm — the assertion must state this explicitly rather than special-casing it.)*
- **SC-011**: **Our own vendored dictionaries stay clean — in shape, code-string content, and the store-only-type assumption that closes C3-1.** A test asserts the ten shipped dictionaries have zero duplicate codes, zero `<value>` elements missing an `enum` attribute, and zero missing descriptions (all true as of 2026-07-14). It also asserts that the set of declared codes containing a space is **exactly** `{FIX41:166:'ISO Country Code', FIX42:166:'ISO Country Code'}` and that every enum-backed tag present in the dictionary store but absent from message expansion carries a **non-multi-value** declared type today. Third-party dictionaries remain tolerated per FR-017 (dedupe), but a regression in *our* data is caught.

  **Extended at Gate A round 1 to the code *strings***, because a census of enum *shapes* cannot see a code that is not a wire literal. The test additionally asserts that the set of declared codes containing a **space**, across all ten shipped dictionaries, is **EXACTLY**:

  ```
  { FIX41 : 166 : "ISO Country Code",
    FIX42 : 166 : "ISO Country Code" }
  ```

  **Any addition, removal, or changed literal fails the gate.** It is an **exact-set** predicate per `[[feedback_completeness_gate_exact_set_not_subset]]`, and it is **GREEN today** — re-measured 2026-07-14 across all ten dictionaries **including** the Orchestra one (5708 `fixr:code` values, zero with a space): those two entries are the **only** space-bearing declared codes in the tree. So a future dictionary refresh that introduces *another* placeholder — or that quietly drops or edits these — **fails the build** instead of silently converting a codeset into a reject-everything trap. The two pinned entries carry the argued disposition of FR-022 / register row **DV-4**.

  *(Reformulated at Gate A round 2 — finding C2-1. It previously asserted "**no** declared code … contains a space" and then, one sentence later, that the assertion "**fires TODAY**" on `SettlLocation(166)` — i.e. a predicate that is **false of the tree** and that 075 does not change. A Success Criterion a correct implementation cannot turn green is not a Success Criterion: an implementer either writes a permanently-RED test that gets "fixed" under time pressure, or downgrades the assertion to a **log** — `[[feedback_ci_gate_observes_not_asserts_witness_skips_into_green]]` — in the one gate whose entire job is to stop a dictionary refresh from bricking a codeset. The **decision** was never open; only the predicate was unwritable. It is now an executable green gate.)*

---

## Non-Goals *(explicit — do not silently absorb)*

- **Codegen enum-domain validation (L-069-1 is NOT retired).** The generated `validate_<Msg>` functions for the 83 v44 typed builders enforce required-field presence and type conformance only, not enum domain. They ride a **different** path (typed codegen), not `table_view`, and this feature does **not** touch them. L-069-1 remains open and MUST be restated as still-open at close-out.
- **ApplExtID(1156)=303 differentiation and `version_registry` re-keying** (retires L-074-1) — separate scheduled follow-on.
- **Typed `fixpp::vlatest` 181-class codegen** — separate scheduled follow-on.
- **Outbound validation.** This feature validates **inbound** messages only; it does not add an outbound enum gate.
- **Any C-ABI or Python-binding surface change.**

---

## Normative References

*(Article VI §5 — `.specify/constitution.md:117`: every `/specify` artifact must list the exact `[DocAbbrev §X.Y.Z] Title` entries from the coverage index that inform the spec. **Added at Gate A round 1** — the bundle previously had none, and `plan.md`'s Constitution Check had no Article VI row, so the gate could not fail.)*

**FIX-spec anchors (registered in `spec/coverage-index.md`):**

- **`[FIX50SP2 §3] Message validator — required fields, type conformance, enum values, group structure`** (`spec/coverage-index.md:189`) → catalogue row **W-014**. **This is the primary anchor**: the coverage index has *listed enum values inside W-014's scope since 004-wire-codec* — 075 is the delivery of the `enum values` clause of a row that is already `OFFICIAL`. It is not a new row.
- **`[FIX50SP2 §3.2] Repeating groups (NoXxx delimiter, ordered field list, nested groups)`** (`coverage-index.md:184`) → **W-006 / W-007 / D-010**. Anchors FR-006's group-member leg and the FR-023 divergence.
- **`[FIX50SP2 §3.3] Field data types (28 types …)`** (`coverage-index.md:185`) → **W-009**. Anchors FR-004/FR-005 (`MULTIPLEVALUESTRING` / `MULTIPLECHARVALUE` are *data types*, which is exactly why the collapsed 7-value `field_type` cannot express them).
- **`[FIX50SP2 §4.5.4] Rejecting invalid messages (Reject 35=3)`** (`coverage-index.md:68`) → **S-007 / S-033 / S-034**. Anchors FR-006's reason-5 mapping and FR-013 (admin/Logon). The **041** note on this row already records that *"enum-value checks [are] deferred (L-041-1)"* — 075 discharges that deferral.
- **`[FIX-Orchestra] fixr:repository schema`** / **`[FIX-Latest] EP303`** → **D-011** (`spec/feature-catalogue.md:130`). The tenth dictionary's code sets; the constitutional carve-out this feature narrows.

**Constitution anchors** (`.specify/constitution.md`):

- `[const §I.1]` Identity & version set (`:46-52`) — the *"live wire validation … post-1.0"* carve-out this feature **amends** (v0.6 → v0.7).
- `[const §II.1]` Language standard C++23 (`:63`).
- `[const §VI.4]` Bidirectional traceability — every new/changed OFFICIAL row needs its coverage-index entry **before** it lands (`:116`).
- `[const §VI.5]` Normative References (`:117`) — this section.
- `[const §VII.8]` Grouped test binaries, selected by `ctest -L` (`:131`).
- `[const §VIII.5]` / `[const §XV.1]` Zero per-message heap; config-time tables.
- `[const §XIV.2]` ≤5 pure-virtuals on `Validator` — the cap that makes `validate_field()` (FR-020) a *frozen* surface.
- `[const §XV.9]` No `std::mutex` / `std::shared_mutex` on these paths.
- `[const §XVIII.5]` No early-ship of deferred post-1.0 **protocols**.
- `[const §XX.2]` / `[const §XX.4]` Amendment process + MINOR classification.

**Coverage-index / catalogue edits this feature MUST land** *(Article VI §4 — "**before** it lands"; these are `/implement` deliverables of this bundle, not edits performed at `/specify`)*:

| Artifact | Line | Current text (stale after 075) | Required edit |
|---|---|---|---|
| `spec/coverage-index.md` | **:581** | *"typed `owning_<Message>` codegen, **live-wire validation**, and ApplExtID(1156)=303 differentiation for these MsgTypes remain **backlog** (post-1.0)"* | strike **live-wire validation** from the backlog list; record it as delivered generically for **all ten** dictionaries by 075 |
| `spec/coverage-index.md` | **:189** (W-014 row) | *"Message validator — required fields, type conformance, enum values, group structure"* | add the 075 delivery note discharging the `enum values` clause + **L-041-1** |
| `spec/coverage-index.md` | **:68** (§4.5.4 row) | 041 note: *"enum-value checks deferred (L-041-1)"* | amend — **L-041-1 retired by 075**; reason 5's **enum arm** now live |
| `spec/coverage-index.md` | **:704** (Post-1.0 Gap Registry, D-011) | *"only the typed-codegen / **live-wire** / ApplExtID … tiers remain deferred"* | strike **live-wire** |
| `spec/feature-catalogue.md` | **:130** (D-011) | *"Typed-codegen / **live-wire** / ApplExtID(1156)=303 / session-negotiation tiers remain post-1.0"* | strike **live-wire**; rides the Article I §1 amendment |
| `spec/feature-catalogue.md` | **:111** (W-014) | validator row | add 075 as a delivering feature for the `enum values` clause |

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
  → **A: Rides the existing flag.** No new config surface. A session that opted into strict inbound validation gets enum-domain checking; a session with validation off is bit-for-bit unaffected (**FR-010**, **SC-005**). This **is** a real behavior change for existing strict-validating deployments — they will begin rejecting out-of-domain enum values they accept today — and it MUST be recorded as an operator-facing **B-row** in `spec/behaviors-and-limitations.md`. *(Amended at Gate A round 2 — finding C2-2. This answer originally said "and called out in the release notes"; **FR-010** establishes that no such artifact exists — there is no `CHANGELOG.md` in this repo — and that **the B-row IS the release-note artifact**. No second deliverable.)* This matches QuickFIX, which has no separate enum-validation switch: `checkValue` is unconditional inside `DataDictionary::validate`.

- **Q: Does enum-domain checking apply to inbound admin messages, including Logon (which fixpp validates *before* `interpret_logon`)?**
  → **A: Yes — all inbound validated messages, admin and Logon included. QuickFIX parity, source-verified.** `Session::next` validates **every** inbound message at `Session.cpp:1218-1229` — **before** `nextLogon` (`:1231`), the same validate-first ordering fixpp has. `DataDictionary::validate` → `iterate` → `checkValue` runs the enum check over header, trailer **and** body, so `Logon`'s `EncryptMethod(98)` *is* enum-checked; an out-of-domain value throws `IncorrectTagValue`, caught at `Session.cpp:1283-1284` → `generateReject(…, SessionRejectReason_VALUE_IS_INCORRECT)` = reason **5** — exactly the arm this feature makes live. A message-category carve-out would be a deliberate divergence from the engine our interop gate compares against; we take the parity. The Logon blast radius is accepted (a peer sending an undeclared admin enum cannot establish), bounded by the fact that the whole path is opt-in behind `validate_inbound_messages`, and MUST be pinned by a direct Logon test (**FR-013**) and recorded as an operator-facing B-row.

- **Q: A dictionary declares the same code twice for one field (duplicate `<value enum="1">` on tag 54) — fail closed, or dedupe?**
  → **A: Dedupe silently (QuickFIX parity); fail closed on a missing `enum` attribute.** QuickFIX stores codes in a `std::set<std::string>` (`DataDictionary.h:90`, `addFieldValue` at `:248`) so a duplicate is absorbed with no error — but it **throws `ConfigError`** when a `<value>` has no `enum` attribute (`DataDictionary.cpp:271-273`), and treats `description` as **optional** (`:276`). fixpp matches all three (**FR-017**). *Rationale for not applying 074's fail-closed reflex:* 074's duplicate guards were on **structural ids**, where a duplicate creates a genuine which-definition-wins ambiguity — i.e. silent **loss**. A duplicate **code value** is idempotent for a membership test: the resulting code set is identical either way, so fail-closed buys no correctness and would reject third-party dictionaries QuickFIX loads. Census 2026-07-14: all nine shipped dictionaries have **zero** duplicate codes, **zero** missing `enum` attributes, **zero** missing descriptions — pinned by a test (**SC-011**) so a regression in *our own* vendored data is still caught.

- **Q: How is SC-009 (QuickFIX enum-domain parity) actually proven, given `reference-engines/` is out-of-repo (outside the submodule's git boundary) and cannot be a CI gate?**
  → **A: Differential run locally → checked-in golden table.** A bounded corpus of boundary frames (in-domain; out-of-domain; multi-value all-declared; multi-value one-undeclared; degenerate whitespace per FR-014; header `PossDupFlag(43)`; empty value; strict-prefix-of-a-declared-code) is run through **real QuickFIX `DataDictionary::validate`** locally; its accept/reject verdicts and reject reasons are captured and **checked in as a golden expectation table** that CI asserts fixpp against (**FR-018**). The generator script is checked in so the golden is reproducible rather than hand-typed. This yields genuine reference-engine evidence while remaining CI-runnable without the out-of-repo tree. *Rejected:* unit tests written from a reading of the QuickFIX source — self-referential, and would enshrine any misreading of `isFieldValue`'s tokenizer (cf. `[[feedback_coverage_push_enshrines_bugs]]`). The live-interop suite remains a **complement**, not the primary proof (it is an out-of-CI release gate and exercises the session path, not the validator boundary).

### Session 2026-07-14 (Gate A round 1)

*(Five ambiguities the Gate A round-1 review forced to a **decision** — as distinct from the factual corrections it also forced. Reviews: `research/reviews/codex_075-live-wire-enum-validation_gate_a_review.md`, `research/reviews/opus_075-live-wire-enum-validation_gate_a_adversarial_review.md`.)*

- **Q: QuickFIX does NOT enum-check repeating-group member fields (closed call-site census: `isFieldValue` ← `checkValue` ← only `DataDictionary.cpp:172` inside `iterate`, which never descends into `m_groups`), while fixpp's Step-1 flat walk over the raw frame bytes checks them at every depth. Suppress fixpp's check to match, or keep it and declare the divergence?**
  → **A: KEEP IT — fixpp is stricter and more correct; declare it.** Suppressing would mean writing deliberate code to make a **correct** check not run, on exactly the fields (party roles, leg sides, MD entry types) where a domain error is most costly. QuickFIX's behaviour here is a known weakness, and fixpp's flat walk makes the check free. Recorded as **FR-023**, divergence-register row **DV-3**, a B-row, and corpus rows 10 + 11 (`asserted: false`). SC-009's *"the one known divergence is the empty-value case"* sentence was **factually false** and is deleted.

- **Q: `table_view::add_enum(tag, value)` is a no-op that discards both arguments, and it is the ONLY way the six existing enum test fixtures populate enum data. Its fate was never decided. No-op or real?**
  → **A: REAL.** If it stayed a no-op, every builder-built `table_view` would have an empty enum table, FR-003's floor would return `true` for all of them, and four suites would stay **green while proving nothing** (`[[feedback_coverage_push_enshrines_bugs]]`). It also needs a **multi-value companion** (`set_multi_value(tag)` or an `add_enum` overload) — without it FR-004's tokenizer has no unit-level witness. **FR-021** carries the named census of the **six** artifacts this invalidates (`validator_domain_test.cpp:559-592`, `validator_type_check_test.cpp:257-275`, `validator_per_version_test.cpp:212-230`, `table_view_test.cpp:256-269`, `tests/wire/conformance/w014_validate.csv`, `tests/support/mock_dict_table.hpp:16`), each with its before/after assertion — FR-012's carve-out is now **bounded**, not open-ended.

- **Q: `validator.hpp:325` is `validate_field(tag, value)` — a context-free public pure-virtual, NOT the group-member call site the bundle claimed. It will begin enum-validating as a side effect. In scope, or explicitly type-only?**
  → **A: IN SCOPE, with its own tests (FR-020).** The enum domain is keyed by **tag alone**, so the absent `msg_type` context costs `validate_field()` nothing and its enum semantics are well-defined and identical to Step 1's. Keeping it type-only would require deliberate suppression machinery and would leave two public surfaces with divergent domain semantics. Consequently **FR-006's mechanism is re-anchored** to the Step-1 flat walk (`validator.hpp:139-158`): group-member and header coverage come from **one** loop over the raw frame bytes, not from a second call site — and a witness aimed at `validate_field()` does **not** discharge the group leg.

- **Q: With `m_checkFieldsHaveValues=false` pinned, QuickFIX rejects an empty value with reason 5; fixpp's rule-2 enum skip makes its empty-value disposition depend on the field's TYPE ARM (`Char` → reject/5; `String` → accept). What does the golden's empty-value row assert?**
  → **A: NOTHING — split it and mark both rows `asserted: false`.** A single "empty-value" row would **pass on `Char` and fail on `String`** — a gate that looks half-green and encodes nothing. The corpus carries **empty × Char** (**DV-1**, parity *by coincidence via a different arm*) and **empty × String** (**DV-2**, genuine divergence) as two characterization-only rows. **L-075-1** therefore records **two** facts: fixpp has no reason-4 mapping, **and** its empty-value disposition is type-arm dependent.

- **Q: `SettlLocation(166)` declares the literal code `"ISO Country Code"` — a prose placeholder — in FIX41/FIX42, so `166=US` starts rejecting. Carve it out, or accept it?**
  → **A: ACCEPT-AND-DOCUMENT (FR-022).** QuickFIX rejects `166=US` identically, so a carve-out would *manufacture* a divergence from the reference engine to paper over bad vendored data; and any carve-out heuristic ("ignore codes containing a space") is an invented rule neither the spec nor the engine has, which would silently weaken a real codeset the day a legitimate space-bearing code appears. Register row **DV-4** + an operator-facing **B-row** that names **tag 166 on FIX 4.1/4.2** explicitly — per FR-010 the B-row **is** the release-note artifact; there is no second one *(amended at Gate A round 2 — finding C2-2)*. **SC-011** is extended to assert that the space-bearing declared codes across the ten dictionaries are **exactly** `{FIX41:166:'ISO Country Code', FIX42:166:'ISO Country Code'}` — an executable **green** gate (measured: those are the **only** such codes across all ten), so any *other* placeholder, and any edit to these two, fails the build *(exact-set reformulation at Gate A round 2 — finding C2-1)*.

### Session 2026-07-14 (Gate A round 4)

*(One disposition the Gate A round-4 review forced to a **decision** — as distinct from the three factual corrections it also forced (census 35 / 10-11-14; empty `<header />` blocks; the residual "gitignored" fragments). Reviews: `research/reviews/codex_075-live-wire-enum-validation_gate_a_4_review.md`, `research/reviews/opus_075-live-wire-enum-validation_gate_a_4_adversarial_review.md`.)*

- **Q: The FIX50SP2 store-only witness `validate_field(1128, "bogus")` was booked BOTH as the FR-020 unit test and as FR-018 golden-corpus row 12 (`asserted: true`). QuickFIX has no context-free `validate_field`, and the corpus row schema has no column naming which fixpp surface drives a row. Drop the corpus row, or keep it and add a `surface` column plus a pre-registered DV-5 for the check-ordering divergence?**
  → **A: DROP THE CORPUS ROW (13 → 12 rows); keep the witness solely as the FR-020 unit test.** The golden corpus is a **QuickFIX-parity** artifact — *"QuickFIX's measured verdict + reason, per row"* — and QuickFIX has no surface to measure here. A uniform harness would drive the row through `validate()`, where fixpp's `field_valid_for` fires first (`validator.hpp:143`) → reason **2**, while QuickFIX's `checkValue` (`DataDictionary.cpp:172`) runs **before** `checkIsInMessage` (`:178`) → reason **5**: an `asserted: true` row that goes red by construction, which SC-009 declares a feature-blocking defect. The witness's discriminating power is against **fixpp's own projection** (the `message_fields()`-only mutation cell), not against QuickFIX; it never needed a QuickFIX verdict, so it loses nothing by moving. *Rejected:* the `surface` column + **DV-5** route — strictly more machinery (a schema change plus a register row, a B-row and a disposition) to keep a row that measures two different surfaces, for a divergence **unobservable on every legitimate corpus row** (all the others use in-message tags, so both engines reach the enum arm and agree). The check-ordering fact is instead **recorded, not registered** — in FR-018 and in `research.md` R-8 — so a future corpus author cannot re-introduce the trap.

### Reference-engine confirmations (QuickFIX, source-verified — these are no longer assumptions)

- **Empty code set ⇒ unconstrained ⇒ accept.** `checkValue` returns early when the tag has no declared values (`DataDictionary.h:493-496`). This is exactly **FR-003**, and it is the reference engine's own anti-reject-everything guard.
- **Multi-value tokenization.** `isFieldValue` (`DataDictionary.h:255-276`) splits a multi-value field on a **single space** and requires **every** token to be a declared code. This is exactly **FR-004**. Note it does **not** skip empty tokens: `18=1  G` (double space) and any trailing space therefore produce an empty token, which is never a declared code, and **reject**. fixpp matches this (**FR-014**).
- **Out-of-domain ⇒ SessionRejectReason 5** with the offending tag as `RefTagID` (`Session.cpp:1283-1284`). This is exactly **FR-006**.
