# Quickstart / Validation Guide: Live-Wire Enum-Value Validation (075)

**Date**: 2026-07-14 | **Branch**: `075-live-wire-enum-validation`

How to prove 075 actually works. Every scenario below maps to a Success Criterion and is written so that a **wrong** implementation fails it — several are mutation-discriminating by construction.

Prerequisites: the standard fixpp build (see repo `README`), plus — for the golden generator only — the already-built local QuickFIX at `reference-engines/quickfix-cpp/lib/libquickfix.so`.

---

## S-1 — The headline: an out-of-domain enum is rejected (SC-001)

The behavior that does not exist on `main`.

1. Load the shipped `dictionaries/FIX44.xml`; open a session with `validate_inbound_messages = true`.
2. Feed an inbound `NewOrderSingle` with `Side(54)=Z` (structurally a valid `char`; **not** a declared `Side` code).
3. **Expect**: rejected, `SessionRejectReason = 5`, `RefTagID = 54`; the application never sees the message.
4. Feed the same message with `54=1`. **Expect**: accepted, exactly as on `main`.

**Mutation check (required, not optional)**: revert `enum_valid` to `return true`. Step 3 **must** turn RED. If it stays green, the test is asserting something else and proves nothing.

---

## S-2 — Multi-value fields must not be false-rejected (SC-003) ⚠️ highest-risk

The scenario that breaks **conformant, shipped** traffic if FR-004/FR-005 are botched.

1. Same FIX44 validating session.
2. Feed `ExecInst(18)=1 G 6` — three space-separated, individually-declared codes. **Expect: ACCEPT.**
3. Feed `18=1 ZZ 6` (middle token undeclared). **Expect: reject, reason 5, RefTagID 18.**
4. Feed `18=1` (single token). **Expect: accept.**
5. Degenerate forms (FR-014, QuickFIX parity): `18=1  G` (double space) and `18=1 ` (trailing space) → **reject** (the empty token is never a declared code).
6. Repeat across the full multi-value census — FIX44: `18, 276, 277, 286, 291, 292, 529, 546`; FIX50/SP1 add `1031, 1035`.

**Mutation check**: replace the tokenizer with a whole-string lookup. Step 2 **must** flip to reject. A green step 2 under that mutation means tokenization is not being exercised.

---

## S-3 — The floor: no dictionary starts rejecting everything (SC-004)

The single scenario standing between this feature and a catastrophic regression.

1. Feed a message with a value for a field that has **no** code set (e.g. `ClOrdID(11)=anything`). **Expect: accept.**
2. Load **FIXT11** (whose `MsgType` field declares **zero** `<value>` children) and validate each of its 8 message types. **Expect: all accept** — they survive *only* via the absent-tag ⇒ accept rule.
3. Run the full pre-existing conformance / validator / session suites. **Expect: green, with zero new rejections.**

Assert the empty-store-accepts rule **directly**. A green suite is corroboration, not proof.

---

## S-4 — Group members and header fields (SC-001, SC-009 / FR-015)

1. Out-of-domain enum on a field **inside a repeating group** (and inside a *nested* group) → reject, reason 5, `RefTagID` = the member tag. *(Same `enum_valid`, second call site — `validator.hpp:325`.)*
2. `PossDupFlag(43)=X` — a **header** field with a `Y`/`N` code set → **reject, reason 5, RefTagID 43**.

Step 2 is the one a body-only implementation silently passes. Without it, SC-009's parity claim is hollow — QuickFIX's `iterate` walks header, trailer **and** body.

---

## S-5 — Admin and Logon (SC-008)

1. Inbound **Logon** with an out-of-domain admin enum (e.g. an undeclared `EncryptMethod(98)`). **Expect**: reject, reason 5, and **the session does NOT establish**.
2. Inbound Logon with all admin enums in-domain. **Expect**: establishes exactly as today (no handshake false-reject).
3. `Reject(3)` / `Logout(5)` remain unvalidated (no reject loop).

Deliberate QuickFIX parity (`Session.cpp:1218-1229` validates before `nextLogon` at `:1231`). Step 2 is what proves we did not brick the handshake.

---

## S-6 — Ten dictionaries carry their code sets (SC-002, SC-010, SC-011)

1. Load each of the ten dictionaries; assert `enum_values(54)` on FIX44 returns the declared `Side` codes **with** descriptions.
2. Assert per-dictionary counts **against the shipped XML** (not a hand-maintained list): FIX44 = **245** enum-backed fields / **1708** codes.
3. **SC-010**: every declared `<message msgtype=X>` is present in that dictionary's `MsgType(35)` code set **or** the code set is empty. *(All nine pass; FIXT11 passes via the empty arm — state that explicitly, do not special-case it.)*
4. **SC-011**: zero duplicate codes, zero `<value>` missing an `enum` attribute, zero missing descriptions across the ten.

Asserting against the XML (not a frozen number list) is what makes a future dictionary refresh fail the build instead of silently bricking a message type.

---

## S-7 — QuickFIX parity, measured (SC-009 / FR-018/FR-019)

1. Build + run the golden generator (`tools/quickfix_enum_golden/`) against the local QuickFIX, with all non-enum switches **pinned**: `checkFieldsHaveValues=false`, `checkFieldsOutOfOrder=false`, `checkUserDefinedFields=false`, `AllowUnknownMsgFields=false`. The settings are recorded **inside** the golden.
2. Check the golden in. **CI asserts fixpp against the golden** — never against the gitignored `reference-engines/` tree.
3. Confirm the golden reproduces research **R-6**: with `checkFieldsHaveValues=false` an empty value reaches `checkValue` and yields **5**; with the default `true` it yields **4** (`NoTagValue`). This is the empirical check on the analysis that reopened FR-008 — if the golden disagrees with R-6, **R-6 is wrong and the empty-value decision must be revisited**, not the golden.

Do not hand-write the expected verdicts. A test encoding our *reading* of QuickFIX would enshrine any misreading of `isFieldValue`'s tokenizer.

---

## S-8 — Non-regression (SC-005, SC-006, SC-007)

1. **Validation off** → byte-identical behavior to `main`.
2. **Hot path**: zero additional allocations per message (existing alloc-guard suite); no measurable throughput regression (bench gate). Measure `as_table_view()` build time + `table_view` footprint on FIX50SP2 (5565 codes) — research O-3 says *measure*, not assert.
3. **C ABI**: zero diff, symbol set unchanged (ABI-golden gate).

---

## Definition of done (all must hold)

- [ ] S-1 and S-2 pass **and** fail under their stated mutations.
- [ ] S-3 asserts the floor directly; full suite green with zero new rejections.
- [ ] S-4 pins **both** call sites and a header field.
- [ ] S-5 pins Logon reject **and** Logon success.
- [ ] S-6 asserts against the XML, not a frozen list.
- [ ] S-7 golden generated with pinned flags; R-6 confirmed empirically.
- [ ] S-8 shows no regression; C ABI unchanged.
- [ ] B-row (behavior change) + **L-075-1** (no reason-4 slot) recorded; **L-069-1 restated as still open**.
- [ ] Constitution **v0.7** (Article I §1) + Sync Impact Report + feature-catalogue **D-011** correction landed together.
