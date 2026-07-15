# Checklist: Enum-Domain Correctness, Witness Discrimination & Hot Path — Requirements Quality

**Feature**: `075-live-wire-enum-validation`
**Created**: 2026-07-14 (post-Gate-A, post-`/speckit-analyze`)
**Domain**: FR-001..FR-017, FR-020..FR-023, SC-001..SC-008, SC-010, SC-011 — the domain-check rules, the projection, the tokenizer, witness discrimination, and hot-path/test discipline.
**Purpose**: **Unit tests for the requirements**, not for the implementation. Every item asks whether a rule is *written* completely and unambiguously enough that an implementer cannot get it wrong — not whether the code does it.
**Audience**: implementer (before Phase 2) + reviewer (Gate B).

> **Why this checklist exists.** Every defect this feature has produced so far came from a rule that was *decided* but *under-written*: the projection source (C3-1, which exhausted the first Gate A loop), the two accept-floors (`/speckit-analyze` C1), the non-discriminating witness (O2-1), the double-booked corpus row (O4-1). The design is right; these items test whether the *prose* is strong enough to survive an implementer reading it literally.

## The TWO accept-floors — the rule most likely to be collapsed into one

- [x] CHK033 - Are FR-003's *empty **CODE SET** ⇒ accept* and FR-008's *empty **field VALUE** ⇒ bypass the enum check* specified as **two distinct branches**, in a way an implementer cannot collapse into one? [Clarity, Ambiguity, Spec §FR-003, §FR-008] — PASS: `tasks.md` T017 explicitly labels them "1." (FR-003) and "2." (FR-008) with the sentence "there are TWO distinct accept-floors, and they are NOT the same rule"; `data-model.md` Validation-rules table rows 1 and 2 keep them separate.
- [x] CHK034 - Is the consequence of omitting the **empty-value bypass** stated explicitly — flipping **empty × `String`** from accept to **reject**? [Gap, Completeness, Spec §FR-008] — PASS: `tasks.md` T017 branch 2 states the exact mechanism ("a literal byte-exact whole-token compare with no empty-value guard finds \"\" absent from every codeset and rejects via the enum arm"); the bottom-of-file "Traps this feature has already fallen into once" table has a dedicated row for this (post-`/speckit-analyze` finding C1) naming T017/T022a as the guard.
- [x] CHK035 - Is it stated that the empty-value disposition is **type-arm dependent**, rather than a single uniform "empty ⇒ X" rule? [Clarity, Spec §FR-008] — PASS: FR-008 body, `data-model.md` rule-2 table, and `contracts/enum-domain.md` C-1 all state the `Char`-reject / `String`-accept split explicitly.
- [x] CHK036 - Is the *reason* for choosing disposition (a) recorded — no reason-4 slot, rerouting would manufacture a 5-vs-4 divergence? [Completeness, Spec §FR-008] — PASS: FR-008, `research.md` R-6, `contracts/enum-domain.md` C-5/C-6 DV-2 all state this.
- [x] CHK037 - Is the **absent-tag/empty-codeset ⇒ accept** floor specified as the **FIRST** branch, with its stake named? [Clarity, Spec §FR-003] — PASS: `tasks.md` T017 branch "1." names it explicitly, incl. the FIXT11 stake; `contracts/enum-domain.md` C-3 repeats it as the first bullet.
- [x] CHK038 - Is FIXT11's dependence on the empty-set arm documented as a **load-bearing dependency**, not a coincidence? [Assumption, Spec §FR-016, §SC-010] — PASS: FR-016 measured note states "That is a load-bearing dependency, not a coincidence, and a dictionary refresh could silently violate it" verbatim; `research.md` R-12 corollary repeats it. Verified independently this audit: `dictionaries/FIXT11.xml` tag 35 (`MsgType`) declares zero `<value>` children (Gate A-cited fact, re-confirmed).

## The projection — store-driven, not reachability-driven

- [x] CHK039 - Is the enum-domain table specified to be projected **from the dictionary enum store**, with `message_fields()` used **only** to overlay the multi-value bit? [Clarity, Spec §FR-002, Plan §Summary] — PASS: `plan.md` Summary point 2 ("store-driven — iterate `dict_metadata_handle::enum_runs_`/`enum_values_`... then use `message_fields()` only to set `multi_value=true`"); `tasks.md` T015 repeats verbatim.
- [x] CHK040 - Is the **failure mode of a `message_fields()`-only projection** written down concretely? [Completeness, Spec §FR-002, §FR-020] — PASS: `tasks.md` T015 states the mechanism by name (C3-1) with the tag list. Verified via CodeGraph/Read against production code this audit: `validate_field()` (`validator.hpp:323-330`) calls `dict_.enum_valid(tag, value)` as its first statement with **no** `field_valid_for` precheck (confirmed by direct read) — exactly the silent-accept mechanism claimed. `ApplVerID(1128)` spot-checked in `dictionaries/FIX50SP2.xml`: declares 11 codes (`0`-`10`) and is **not** referenced by name anywhere in `<messages>` or `<components>` (only `RefApplVerID` is referenced) — confirms it as a genuine message-unreachable, store-only tag.
- [x] CHK041 - Is the **`multi_value = false` default for store-only tags** identified as an **assumption**, with its measured basis stated? [Assumption, Spec §FR-005] — PASS: `tasks.md` T016 states it as "This default is an **assumption**"; `data-model.md` Entity B table gives the measured basis (35 tags, types STRING/BOOLEAN/INT/CHAR, none MULTIPLE*). Spot-verified: `MsgDirection(385)` in `dictionaries/FIX50SP2.xml` is `CHAR` with exactly 2 values (`R`,`S`) — not a `MULTIPLE*` type, as claimed.
- [x] CHK042 - Is that assumption **pinned by a gate** that goes RED if a future dictionary violates it? [Measurability, Spec §SC-011] — PASS: `tasks.md` T021/SC-011 ("any store-only tag becoming multi-value — FAILS the build").
- [x] CHK043 - Is the requirement that `table_view` **owns copies** of the code bytes stated **with its rationale**? [Completeness, Plan §Complexity Tracking] — PASS: `plan.md` Complexity Tracking row 2 + `research.md` R-1 both give the use-after-free rationale. Verified via CodeGraph/Read of `table_view.hpp:304-352` this audit: `table_view` is presently fully self-owning (`valid_`, `types_`, `group_members_` etc. hold owned values; `add_valid_tag` copies the msg_type key), consistent with the "first external aliasing" claim.
- [x] CHK044 - Is the string-pool lifetime rule unambiguous? [Clarity, Edge Case, Spec §FR-001, Research R-4] — PASS: `research.md` R-4 + `tasks.md` T012 state the post-`shrink_to_fit()` binding requirement identically. Verified via grep this audit: `xml_loader.cpp:853-855` currently reads "Pool is now finalized... shrink_to_fit to lock the data pointer" — the existing pass the spec requires enum-view binding to join.
- [x] CHK045 - Is `pool_estimate`'s shortfall specified, and is the reserve required to be **TIGHT**? [Gap, Completeness, Research R-10] — PASS: `research.md` R-10 + `tasks.md` T014 state both. Verified via grep this audit: `xml_loader.cpp:646-658` today sums only message/component/field **names** (confirmed no `enum`/`value` reference in the current estimate), consistent with the claimed shortfall.

## The tokenizer — the rule that breaks working traffic if wrong

- [x] CHK046 - Is the multi-value rule specified as **tokenize on a SINGLE space and require EVERY token**, with **single-value fields explicitly NOT tokenized**? [Clarity, Spec §FR-004] — PASS: FR-004 final sentence ("Single-value fields MUST NOT be tokenized"), `tasks.md` T017/T018.
- [x] CHK047 - Is the stake of getting this wrong quantified? [Completeness, Spec §FR-004, Plan §Risks] — PASS: US2 "Why this priority" + `plan.md` Risk 1 both quantify it; independently re-verified this audit via census script over the shipped `dictionaries/FIX44.xml`: exactly **8** enum-backed multi-value fields.
- [x] CHK048 - Is the **no-empty-token-skipping** rule stated as **required for interop parity, not merely permitted**? [Clarity, Spec §FR-014] — PASS: FR-014 states "is required for interop parity, not merely permitted" verbatim.
- [x] CHK049 - Is the case of a **single-value** field whose value *contains* a space specified — checked as **ONE** code, no tokenization? [Edge Case, Spec §FR-004, AC US2 #3] — PASS: spec.md AC US2 #3 (line 85) states this exactly.
- [x] CHK050 - Is the comparison rule stated as **byte-exact and whole-token** — no case folding, no prefix matching — with case-sensitivity noted? [Clarity, Spec §FR-009] — PASS: FR-009 + Edge Cases "Case sensitivity" bullet.
- [x] CHK051 - Is the multi-value census stated **consistently across every site**, including **FIX50SP2's 9**? [Consistency, Spec §SC-003] — **SPEC-FIXED**: this was a genuine (already Gate-A-tracked, `tasks.md` T039) inconsistency — `spec.md:195`, `SC-003` (`spec.md:260`), and `quickstart.md:35` all named "FIX50/SP1 add 1031, 1035" without stating FIX50SP2's count, leaving the census inconsistent with the correct Context table (spec.md:38, FIX50SP2 = 9). Re-derived independently against `dictionaries/FIX50SP2.xml` this audit (tag `1035` = `MULTIPLESTRINGVALUE`, 0 `<value>` children, confirming 9 not 10) and edited all three sites to state "**FIX50SP2 has 9**" explicitly. This discharges `tasks.md` T039.
- [x] CHK052 - Is it specified that `add_enum()` needs a **multi-value companion**, with the reason? [Gap, Spec §FR-021] — PASS: FR-021 body states the reason verbatim; `tasks.md` T019 repeats it.

## Witness discrimination — every witness names its mutation

- [x] CHK053 - Does **every** witness in the bundle name the **mutation that must turn it RED**? [Measurability, Acceptance Criteria, Plan §Test matrix] — PASS: `plan.md` Test matrix's "Mutation witness" column and `tasks.md`'s per-task "Mutation:" clauses are populated for every witness row (re-read in full — no blank cell found).
- [x] CHK054 - Is it specified that the group-member witness MUST be driven through **`validate()` / the Step-1 walk** and **NOT** `validate_field()`? [Clarity, Ambiguity, Spec §FR-006, §FR-020] — PASS: spec.md AC US1 #4, `tasks.md` T023, `quickstart.md` S-4 all state this with the "does NOT discharge" wording.
- [x] CHK055 - Is the `message_fields()`-only mutation identified as the **only** mutation with power against the store-driven projection? [Completeness, Spec §FR-020] — PASS: `tasks.md` T024 states "this is the **only** witness in the bundle with power against it" verbatim.
- [x] CHK056 - Is it stated that `empty × String ⇒ ACCEPT` is the assertion that does the work and **nothing in the tree pins it today**? [Gap, Spec §FR-008] — PASS: `tasks.md` T022a states this explicitly, naming the specific pre-existing test (`validator_type_check_test.cpp:387-396`) that would stay green through the regression.
- [x] CHK057 - Is the accept-side mutation for the tokenizer specified? [Measurability, Spec §SC-003] — PASS: `plan.md` SC-003 test-matrix row + `tasks.md` T027 both name the whole-string-lookup mutation against the accept case.
- [x] CHK058 - Are the **six** FR-021 artifacts enumerated by name with before/after assertions? [Completeness, Spec §FR-021] — PASS: FR-021's named-census table lists all six with before/after columns.
- [x] CHK059 - Is artifact #5 (`w014_validate.csv`) flagged as **DATA, not code**? [Edge Case, Spec §FR-021] — PASS: FR-021 table row 5 + `tasks.md` T026 both state this.
- [x] CHK060 - Is the Logon witness (SC-008) specified with **both** halves? [Coverage, Spec §SC-008] — PASS: SC-008 + `tasks.md` T030 ("Both halves matter — a witness that only pins the reject would stay green under a reject-everything regression").

## Hot-path & non-functional requirements

- [x] CHK061 - Are `enum_valid`'s hot-path constraints stated as **testable** properties? [Measurability, Spec §FR-007] — PASS: FR-007 states `noexcept`, allocation-free, no `std::string` materialization as MUST requirements.
- [x] CHK062 - Is the complexity bound specified, and is the motivating scale stated? [Clarity, Spec §FR-007] — PASS: FR-007 states the O(log T)+O(C) bound; `plan.md` Performance Goals + `research.md` R-2 state the 93-code motivation (actual design is O(1)+O(log C), strictly inside the FR-007 "no worse than" bound — no inconsistency).
- [x] CHK063 - Is the table required to be built **once at config time and never rebuilt per message**, with a reddening mutation named? [Measurability, Spec §FR-002, §SC-006] — PASS: `[const §XV.1]` cited throughout; `tasks.md` T032 names the "rebuild the enum table per message" mutation against the alloc guard.
- [x] CHK064 - Is the SC-006 baseline specified as **measured, not asserted**? [Measurability, Spec §SC-006] — PASS: SC-006, `research.md` O-3, `tasks.md` T011 all say "measured, not asserted".
- [x] CHK065 - Is the `MsgType(35)` code count stated **consistently as 93** everywhere it appears? [Consistency, Spec §FR-015] — PASS: independently re-verified this audit against `dictionaries/FIX44.xml` — tag 35 declares exactly **93** `<value>` elements. Grepped every occurrence of "92" across spec.md/plan.md/tasks.md/data-model.md/research.md/quickstart.md/contracts/enum-domain.md — all are historical "was wrong, corrected to 93" explanatory text; no live/current claim of "92" remains.

## Loader tolerance, censuses & scope boundaries

- [x] CHK066 - Are the **three** loader dispositions specified individually and with rationale? [Completeness, Spec §FR-017] — PASS: FR-017 states all three with rationale; `tasks.md` T013 repeats.
- [x] CHK067 - Is it explained why 074's **fail-closed reflex does not transfer**? [Consistency, Spec §FR-017] — PASS: FR-017, Clarification Q4 answer, and `research.md` R-5 all state the structural-id vs. code-value distinction.
- [x] CHK068 - Is SC-002's **exact-count** leg explicitly scoped to the **nine** XmlLoader dictionaries, with the tenth's actual pin named accurately? [Consistency, Traceability, Spec §SC-002] — PASS: SC-002 (O2-7 correction) + `tasks.md` T028 both state "074 has NO exact-count census — do not claim one" and name the actual `AdvSide(4)` spot-check pin.
- [x] CHK069 - Is SC-011 an **executable GREEN gate**? [Measurability, Ambiguity, Spec §SC-011] — PASS: SC-011 (C2-1 reformulation) states the exact-exception-set predicate. Independently re-derived this audit across all nine XmlLoader dictionaries via census script over the shipped XML: the **only** space-bearing declared codes are `FIX41:166` and `FIX42:166`, both literally `"ISO Country Code"` — confirms the gate is green today as claimed. Also confirmed the Orchestra dictionary's 5708 `fixr:code` values contain zero space-bearing entries.
- [x] CHK070 - Are the censuses required to assert **against the shipped XML**, never a hand-maintained list? [Measurability, Spec §SC-002, §SC-010, §SC-011] — PASS: SC-002/SC-010/SC-011 all state "against the shipped XML" / "not a hand-maintained expectation list".
- [x] CHK071 - Are the **Non-Goals** stated in a way that resists silent absorption? [Completeness, Spec §Non-Goals] — PASS: spec.md Non-Goals section states L-069-1 explicitly ("L-069-1 is NOT retired... must be restated as still-open at close-out"); `tasks.md` T037(g) and T043 both carry the restatement obligation through to close-out.
- [x] CHK072 - Is the FR-013 blast radius stated as an **accepted, bounded** consequence? [Completeness, Spec §FR-013] — PASS: FR-013 states "bounded by the whole path being opt-in behind `validate_inbound_messages`" verbatim.

## Test-binary discipline `[const §VII.8]`

- [x] CHK073 - Does every new test in the plan's matrix name its **binary** and its **`ctest -L` label**? [Completeness, Plan §Test matrix] — PASS: `plan.md` Test matrix has non-empty "Binary (bucket)" and "`ctest -L`" columns for every row (re-read in full).
- [x] CHK074 - Are the **isolation-sensitive** tests correctly identified as **standalone**? [Consistency, Plan §Test matrix] — PASS: SC-009/SC-010/SC-011/FR-024/SC-006(alloc-guard)/SC-007(ABI) rows are all marked standalone with a stated reason (`[const §VII.8]`), consistent with `tasks.md`'s per-task binary annotations.
- [x] CHK075 - Is the **pre-existing** gap (the `tests/wire/` buckets carry no `LABELS` today) explicitly scoped **out** of this feature? [Clarity, Boundary, Plan §Label discipline note] — PASS: `plan.md`'s "Label discipline note" + `tasks.md`'s "Test-binary discipline" header both state this is "pre-existing and NOT 075's to fix", while requiring every *new* test to be `-L`-selectable.

## Audit Result

| Disposition | Count |
|---|---|
| PASS | 42 |
| SPEC-FIXED | 1 |
| DD-DECIDED | 0 |
| WAIVED | 0 |
| **Total** | 43 |

### SPEC-FIXED items

- CHK051 — the multi-value census prose omitted FIX50SP2's count (9) at three sites while the Context census table already had it correct; edited `spec.md:195` (audit note), `spec.md` SC-003 (~line 260), and `quickstart.md:35` to name "FIX50SP2 has 9" explicitly, re-derived against the shipped `dictionaries/FIX50SP2.xml` (tag `1035` = `MULTIPLESTRINGVALUE`, 0 `<value>` children). This discharges the already-tracked Gate A residual P3 `tasks.md` T039 — not a new Gate A reopener.

### DD-DECIDED items

None.

### WAIVED items

None.

Anchors and literals spot-verified this audit (checklist-auditor pass, 2026-07-14) against the shipped `dictionaries/*.xml` and production headers: `table_view.hpp:288-294` (`enum_valid` stub) and `:350-352` (`add_enum` stub) — held; `validator.hpp:65-66,139-158,323-330` (5-virtual cap, Step-1 walk order, `validate_field` no-precheck) — held; `dictionary.hpp:66-67,184-191,195-200` (stale doc claims T038 must correct) — held; `dictionary_internal.hpp:90-103` (`enum_values_`/`enum_runs_` constructed from handle `mr`) — held; `xml_loader.cpp:621-622,646-658,739,743,853-855` (header/trailer node assignment, `pool_estimate` names-only, null-guard, post-`shrink_to_fit()` bind pass) — held, and confirmed `xml_loader.cpp` currently has **zero** references to `<value>`/`enum_values_`/`EnumValueRef` (only `orchestra_loader.cpp` populates the store today); `reject_reason_map.hpp:18,22-23,53,60-61` (stale Phase-1 comments) — held; `dictionaries/{FIX50,FIX50SP1,FIX50SP2}.xml` line 2 = `<header />` (empty, not absent) and matching `<trailer />` lines (2694/2976/4674) — held; FIX44 census (245 enum-backed fields / 1708 codes / 8 multi-value / `MsgType(35)`=93 codes) and the full nine-dictionary Context census table — reproduced exactly via independent census script; `FIXT11.xml` `MsgType(35)` = 0 `<value>` children, 8 message types — held; `MatchType(574)` no bare `A` / `TradeCondition(277)` declares `A` — held; `EncryptMethod(98)` = `0`-`6` — held; `SettlLocation(166)` on FIX41/FIX42 only, declares `"ISO Country Code"` — held; `ApplVerID(1128)` on FIX50SP2 = 11 codes, message-unreachable — held; `MsgDirection(385)` on FIX50SP2 = `CHAR`, 2 values — held. All anchors resolve in the signed-off bundle at Gate A round 5 convergence (submodule `8c8ec699`, user-signed-off 2026-07-14).
