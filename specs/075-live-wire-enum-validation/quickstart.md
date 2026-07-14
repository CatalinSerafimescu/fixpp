# Quickstart / Validation Guide: Live-Wire Enum-Value Validation (075)

**Date**: 2026-07-14 | **Branch**: `075-live-wire-enum-validation`

How to prove 075 actually works. Every scenario below maps to a Success Criterion and is written so that a **wrong** implementation fails it — several are mutation-discriminating by construction.

Prerequisites: the standard fixpp build (see repo `README`), plus — **for the golden generator only** — the already-built local QuickFIX.

> **Path corrected at Gate A round 1 (finding O-4).** `reference-engines/` is **not in the library repo**; it lives at the **parent** repo root, *outside the submodule's git boundary* — `<parent>/reference-engines/quickfix-cpp/lib/libquickfix.so.17.0.0` (built). The instruction *"`reference-engines/quickfix-cpp/lib/libquickfix.so`"* was **unfollowable as written** from anywhere in the library. Point the generator at it via the validated CMake cache variable **`FIXPP_QUICKFIX_ROOT`** (default `${CMAKE_SOURCE_DIR}/../../../reference-engines/quickfix-cpp`). The target is **OFF by default** and **hard-errors** if the variable is set but `lib/libquickfix.so` is absent (FR-024) — that, not a gitignore claim, is what keeps CI from ever reaching the tree.

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
6. Repeat across the full multi-value census — FIX44: `18, 276, 277, 286, 291, 292, 529, 546`; FIX50/SP1 add `1031, 1035`; **FIX50SP2 has 9** (tag `1035` is `MULTIPLESTRINGVALUE` but declares zero `<value>` children, so it is not enum-backed).

**Mutation check**: replace the tokenizer with a whole-string lookup. Step 2 **must** flip to reject. A green step 2 under that mutation means tokenization is not being exercised.

---

## S-3 — The floor: no dictionary starts rejecting everything (SC-004)

The single scenario standing between this feature and a catastrophic regression.

1. Feed a message with a value for a field that has **no** code set (e.g. `ClOrdID(11)=anything`). **Expect: accept.**
2. Load **FIXT11** (whose `MsgType` field declares **zero** `<value>` children) and validate each of its 8 message types. **Expect: all accept** — they survive *only* via the absent-tag ⇒ accept rule.
3. Run the full pre-existing conformance / validator / session suites. **Expect: green, with zero new rejections.**

Assert the empty-store-accepts rule **directly**. A green suite is corroboration, not proof.

---

## S-4 — Group members and header fields (SC-001, FR-006 / FR-015 / FR-020 / FR-023)

**Mechanism — re-anchored at Gate A round 1 (finding C-3).** There is **no second call site for group members.** Step 1 (`validator.hpp:139-158`) walks a **dict-free `field_iterator` over the raw frame bytes** (`parser.hpp:229-233`) — a linear `tag=value` scan with **no group awareness** — so **one** loop yields **header + trailer + body + group members at every depth**. `validator.hpp:325` is **`validate_field(tag, value)`**, a *context-free public virtual* and a **separate surface** (FR-020).

1. Out-of-domain enum on a field **inside a repeating group**, and separately **inside a nested group** (depth ≥ 2) → reject, reason 5, `RefTagID` = the member tag. **Drive this through `validate()` — i.e. the Step-1 walk — NOT through `validate_field()`.** *(A `validate_field()` unit test would be a **false-green witness** for this AC: it exercises a different surface with no message context. That substitution is exactly what the old `:325` anchor invited — `[[feedback_witness_asserts_named_postcondition_not_proxy]]`.)*
   **This is a declared divergence (DV-3): QuickFIX does NOT enum-check group members at all** — so its golden rows (corpus 10, 11) are `asserted: false`. fixpp is deliberately stricter (FR-023).
2. `PossDupFlag(43)=X` — a **header** field with a `Y`/`N` code set → **reject, reason 5, RefTagID 43**.
3. **`validate_field()` on its own** (FR-020): in-domain accept / out-of-domain reject / multi-value / empty. It is a third public surface and gets its own witnesses; the existing test asserting the **opposite** (`tests/wire/validator_type_check_test.cpp:257-275`) flips. The discriminating witness for C3-1 is **FIX50SP2 `validate_field(1128, "bogus")` → reject / `validate_field(1128, "9")` → accept** (`ApplVerID`, declared in the dictionary but absent from message expansion): the stub and a `message_fields()`-only projection both silently accept it, while the store-driven projection rejects it.

Step 2 is the one a body-only implementation silently passes. **Mutation check for step 1**: restrict the Step-1 walk to top-level fields — the group **and** nested-group assertions must turn RED. If they stay green, the witness is not exercising the flat walk.

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
4. **SC-011**: zero duplicate codes, zero `<value>` missing an `enum` attribute, zero missing descriptions across the ten — **and** the set of declared codes containing a **space** is **EXACTLY** `{FIX41:166:"ISO Country Code", FIX42:166:"ISO Country Code"}`. **This gate is GREEN today** with that exception set pinned; **any addition, removal, or changed literal fails it.** *(Reformulated at Gate A round 2, finding C2-1 — it previously said "**NO** declared code containing a space … this assertion **fires today**, deliberately", i.e. a gate defined to be RED, which an implementer would either "fix" under pressure or downgrade to a log.)* The two pinned entries are `SettlLocation(166)`'s prose placeholder on FIX 4.1/4.2 — the single **argued exception** (FR-022 / **DV-4**: accept-and-document; QuickFIX rejects `166=US` identically). Any *other* placeholder — present or introduced by a future refresh — **fails the build** instead of silently turning a codeset into a reject-everything trap.

Asserting against the XML (not a frozen number list) is what makes a future dictionary refresh fail the build instead of silently bricking a message type. *(The **shape** census alone would never have caught `166` — it counts tags and codes, never what a code **says**. That gap is root cause RC#3.)*

---

## S-7 — QuickFIX parity, measured (SC-009 / FR-018 / FR-019 / FR-024) — **DO THIS FIRST (Phase 0.5)**

**This is a BLOCKING first deliverable, not a verification afterthought.** The golden's *measured output* **defines** the divergence register; it is not evidence for a parity claim written ahead of it. *(Root cause RC#1: the first draft wrote the parity FRs/SCs/corpus first and deferred the golden, so the corpus was designed from the same partial reading of QuickFIX's call graph it was supposed to check — and had **no group-member row**, making it structurally blind to the largest divergence in the feature.)*

1. Build + run the golden generator against the local QuickFIX (root: `FIXPP_QUICKFIX_ROOT`), with:
   - all four non-enum switches **pinned**: `checkFieldsHaveValues=false`, `checkFieldsOutOfOrder=false`, `checkUserDefinedFields=false`, `AllowUnknownMsgFields=false`; **and**
   - the **dictionary topology pinned to single-DD / non-FIXT** — `sessionDD == appDD` — **applied per the corpus row's own dictionary** (FR-019; *corrected at Gate A round 2, O2-6 — it read "single-DD **FIX 4.4**", but the corpus spans FIX44 **and** FIX41/FIX42*). QuickFIX's *two*-DD FIXT path checks the header against **FIXT11** (whose `MsgType` has **zero** codes ⇒ unconstrained) — a topology **fixpp does not have** (it has one `cfg_.dictionary`). A two-DD golden measures the wrong engine. Set the topology **and** the four booleans **in generator source**, not on the CLI, so `generator_source_hash` pins them.
2. Run the **full 13-row corpus** *(12 → 13 at T006)* — including the rows the first draft did not have: **group member**, **nested group member**, **empty × `Char`**, **empty × `String`**, **`SettlLocation(166)=US`**. Each row carries `asserted: true|false`. **Every row is a message frame driven through `validate()`** — QuickFIX has **no context-free `validate_field` analogue**, so a `validate_field()` row cannot be measured here and does not belong (the FIX50SP2 `validate_field(1128, "bogus")` store-only witness is an **FR-020 unit test**, not a corpus row — dropped at Gate A round 4, O4-1). **Never add a message-unreachable-tag row**: fixpp's `field_valid_for` fires before `enum_valid` (reason 2) while QuickFIX's `checkValue` runs before `checkIsInMessage` (reason 5), so such a row goes spuriously red — see FR-018. **Re-measure every `asserted: true` literal against the shipped XML before running** (FR-018's audit table): **a `reject`-asserting row that BOTH engines ACCEPT is a defect, not a row** — it coincides with the stub (`enum_valid → true`), never exercises the rule it names, and no specified mutation can redden it. That is how round-1's `277=A` prefix row got in (`TradeCondition(277)` **declares `A`**; the row is now **`MatchType(574)=A`**). *(The accept-asserting rows 1 and 3 are the paired positive controls against **over**-rejection — this rule does not touch them.)*
3. **Derive** the divergence register (`contracts/enum-domain.md` C-6) from what it measures. Expected: **DV-1** (empty×Char — parity *by coincidence*, different arms), **DV-2** (empty×String — fixpp accepts, QuickFIX rejects/5), **DV-3** (group members — fixpp checks, QuickFIX **never** does), **DV-4** (`166=US` — both reject). **A divergence the golden reveals that is NOT in the register is a DEFECT** — fix it or promote it to an argued register row. Do not quietly widen the register.
4. Check the golden in **with its manifest** (FR-024): QuickFIX version + soname, every loaded dictionary's SHA-1, generator-source hash, corpus hash, topology, all four flags. **CI asserts fixpp against the golden** — never against the out-of-repo `reference-engines/` tree.
5. **Prove the manifest gate is not decorative**: hand-edit one verdict in the checked-in golden and confirm the manifest test turns **RED**. A golden nothing can falsify is a false-green surface (`[[feedback_codegen_golden_exists_narrow_verify_misses_it]]`, `[[feedback_sanitizer_canary_must_be_proven_red]]`).

Do not hand-write the expected verdicts. A test encoding our *reading* of QuickFIX would enshrine any misreading — and this bundle has now been caught doing exactly that **three** times (`m_checkFieldsHaveValues` front-running the check; the group-member miss; the type-arm-dependent empty value).

---

## S-8 — Non-regression (SC-005, SC-006, SC-007)

1. **Validation off** → byte-identical behavior to `main`.
2. **Hot path**: zero additional allocations per message (existing alloc-guard suite); no measurable throughput regression (bench gate). Measure `as_table_view()` build time + `table_view` footprint on FIX50SP2 (5565 codes) — research O-3 says *measure*, not assert.
3. **C ABI**: zero diff, symbol set unchanged (ABI-golden gate).

---

## Definition of done (all must hold)

- [ ] **S-7 FIRST** (Phase 0.5): golden generated with the four flags **and the single-DD / non-FIXT topology (`sessionDD == appDD`, per the row's own dictionary)** pinned **in generator source**; **13-row** corpus incl. group / nested-group / empty×{Char,String} / `166=US` / the re-based **`MatchType(574)=A`** prefix row — **all message frames through `validate()`; no `validate_field()` row** (O4-1); **every `asserted: true` literal re-measured against the shipped XML** (no accept-accept rows); manifest embedded, recording the topology **per dictionary**; **manifest gate proven RED under a hand-edit**; regen-and-diff target **bound to the per-release interop gate** (owner + cadence — it is the only thing that can catch a QuickFIX-version drift); divergence register **DERIVED** from its output (**DV-1..DV-5**), with any unexpected divergence treated as a **defect** — ⚠️ **this FIRED at T006**: the golden revealed the `checkValidFormat`(`:171`)-before-`checkValue`(`:172`) reason-6 divergence that five Gate A rounds missed ⇒ row 6 re-based to `MessageEncoding(347)`, new **DV-5** + `asserted: false` row 13, **DV-1 corrected**.
- [ ] S-1 and S-2 pass **and** fail under their stated mutations.
- [ ] S-3 asserts the floor directly; full suite green with zero new rejections.
- [ ] S-4 pins the **Step-1 flat walk** (group **and nested-group** members, through `validate()` — **not** `validate_field()`), a **header** field, **and** `validate_field()` as its own surface (FR-020), including the FIX50SP2 `ApplVerID(1128)` store-only witness.
- [ ] S-5 pins Logon reject **and** Logon success.
- [ ] S-6 asserts against the XML, not a frozen list — incl. SC-011's **exact-exception-set** assertion over space-bearing codes (`{FIX41:166, FIX42:166}` — **green today**; any addition/removal/edit fails). SC-002's **exact-count** leg covers the **nine** XmlLoader dictionaries (the Context census table); the tenth's code sets are 074's.
- [ ] S-8 shows no regression; C ABI unchanged.
- [ ] **`add_enum()` is REAL** (FR-021) + its multi-value companion; **all six** named artifacts flipped with each change explicitly called out (FR-012) — incl. the **CSV** row, which fails at runtime, not at compile time.
- [ ] **All seven doc corrections** landed (`dictionary.hpp:66-67`, **`:184-189`** — the `enum_values()` block itself, *added at Gate A round 2, O2-4* — `:195-200` ×2, `reject_reason_map.hpp:22-23`, `:60-61`, `:1-19`) — `reject_reason_map.hpp` takes **no code change**, comments only, which is why it is easy to miss.
- [ ] B-rows recorded: behavior change (FR-010 — **the B-row IS the release note**; there is no `CHANGELOG.md`), **DV-3** (group-member strictness), **DV-4** (tag 166 on FIX 4.1/4.2). **L-075-1** records **both** halves (no reason-4 slot **and** type-arm-dependent empty-value disposition). **L-041-1 RETIRED**; **L-069-1 restated as still open**.
- [ ] Coverage-index + catalogue edits landed **before the row lands** (`[const §VI.4]`): `coverage-index.md:581`, `:189`, `:68`, `:704`; `feature-catalogue.md:130`, `:111`.
- [ ] Constitution **v0.7** (Article I §1) + Sync Impact Report + feature-catalogue **D-011** correction landed together.
