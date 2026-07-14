# Checklist: QuickFIX Golden-Parity Leg — Requirements Quality

**Feature**: `075-live-wire-enum-validation`
**Created**: 2026-07-14 (post-Gate-A, post-`/speckit-analyze`)
**Domain**: FR-018 / FR-019 / FR-024 / SC-009 — the golden, its manifest, and the divergence register.
**Purpose**: **Unit tests for the requirements**, not for the implementation. Every item asks whether the parity requirements are *written* completely, unambiguously and consistently enough to build and audit from. An item that asks "does the code do X?" does not belong here.
**Audience**: implementer (before Phase 0.5) + reviewer (Gate B).

> **Why this checklist exists.** Gate A root cause **RC#1**: the bundle originally *asserted* QuickFIX parity from a partial read of the reference engine's call graph, then designed the corpus that was supposed to police that claim **from the same partial read** — so the corpus was structurally incapable of detecting the read's errors. Two errors were found by *review*, not by the corpus (**DV-3** group-member divergence; **DV-1/DV-2** type-arm-dependent empty value). The parity leg is now *derived*, and these items test whether it stayed that way.

## Derivation discipline — parity is DERIVED, never ASSERTED

- [x] CHK001 - Is the requirement that the golden is a **blocking Phase-0.5 deliverable** stated as a hard ordering constraint, rather than left as a suggestion a task graph could silently reorder? [Clarity, Spec §FR-018] — PASS: `plan.md` § Phase ordering table marks Phase 0.5 "BLOCKS the parity leg"; `tasks.md:21-23` header "⚠️ GATE ... MUST NOT be reordered after the FRs it validates" + explicit Dependencies diagram gating Phase 1 on the Phase 0.5 checkpoint.
- [x] CHK002 - Does every parity sentence in the bundle satisfy the stated test — *"can it be falsified ONLY by the golden?"* — or does some FR/SC still assert a QuickFIX behaviour from a source reading? [Consistency, Spec §FR-018, §SC-009] — PASS: FR-018/SC-009 state the test explicitly; no remaining "fixpp matches QuickFIX except X" sentence found outside argued/B-rowed register rows (spec.md:148-150, 266-268 re-read in full).
- [x] CHK003 - Is it unambiguous that the golden's **measured output DEFINES** the divergence register, rather than serving as evidence for a register written in advance? [Clarity, Spec §SC-009] — PASS: FR-018 ("its measured output IS the definition"), SC-009, `contracts/enum-domain.md` C-6 header, `tasks.md` T006 all state this identically.
- [x] CHK004 - Are the **consequences of an unregistered divergence** specified — i.e. is it stated that the golden surfacing a divergence not already an argued register row **blocks the feature**, and is the required response (fix, or promote with an argued disposition + B-row + corpus row) spelled out? [Completeness, Spec §SC-009] — PASS: spec.md:284 ("blocks the feature until it is either fixed or promoted"); `tasks.md` T006 adds a STOP-and-report obligation.
- [x] CHK005 - Is the distinction between the register's **two legs** stated explicitly — (1) every deliberate divergence (necessarily `asserted: false`) and (2) every operator-visible behaviour change requiring an argued decision (which may be `asserted: **true**`, e.g. DV-4) — so that "`asserted: false` ⇒ register row, but NOT conversely" cannot be misread? [Clarity, Spec §SC-009] — PASS: spec.md:270-274 states both legs verbatim; DV-4 row explicitly marked `asserted: true` with a dedicated "Note on DV-4's asserted value" paragraph in `contracts/enum-domain.md` C-6.

## Corpus completeness & row discrimination

- [x] CHK006 - Is the corpus specified to cover **every fixpp Step-1 surface** (top-level, header, trailer, group member, nested group member, multi-value, degenerate whitespace, empty × each type arm, strict prefix), rather than only the surfaces the original theory said mattered? [Coverage, Spec §FR-018] — PASS: FR-018's 12-row table (spec.md:154-167) enumerates exactly these surfaces.
- [x] CHK007 - Does **every** corpus row carry an explicit `asserted: true|false` discriminator, and is the meaning of `asserted: false` (characterization-only; feeds a register row; asserts **nothing** about fixpp) unambiguous? [Clarity, Spec §FR-018] — PASS: FR-018 table + `data-model.md` row schema both carry the discriminator with the meaning spelled out.
- [x] CHK008 - Is the **non-discriminating-row rule** stated **narrowly and correctly** — *"a `reject`-asserting row that BOTH engines ACCEPT is a defect, not a row"* — rather than in the broad, **false** form (*"expected verdict equals the stub's behaviour"*), which would condemn the legitimate **accept**-asserting positive controls (rows 1 and 3)? [Clarity, Ambiguity, Spec §FR-018] — PASS: spec.md:180 states the rule narrowly verbatim and explicitly exempts rows 1/3 in the same paragraph.
- [x] CHK009 - Is the requirement to **re-measure every `asserted: true` literal against the shipped XML *before* writing the generator** stated as a precondition, not an afterthought? [Completeness, Spec §FR-018] — PASS: `tasks.md` T001 states this as the first Phase 0.5 task, explicitly gating T002 (generator creation). Re-verified independently this audit against the shipped XML: `MatchType(574)` (FIX44) has no bare `A` (18 codes `A1..M6`); `TradeCondition(277)` **does** declare `A`.
- [x] CHK010 - Are the **positive controls** (rows 1 and 3) documented as guarding against **over**-rejection, with the specific mutation each one reddens under, so a future author does not delete them as "redundant accept rows"? [Completeness, Spec §FR-018] — PASS: spec.md:180 parenthetical + `plan.md` SC-009 test-matrix row name row 1/3's specific reddening mutations.
- [x] CHK011 - Is it stated **why there is no `validate_field()` corpus row** — QuickFIX has no context-free analogue, and the row schema carries **no column naming which fixpp surface drives a row** — and is the **check-ordering fact** recorded where a future corpus author will actually encounter it? [Completeness, Spec §FR-018] — PASS: spec.md:169-176 ("Why there is NO validate_field() row..."); check-ordering fact independently confirmed against `validator.hpp:143/148` (field_valid_for before enum_valid) and recorded in FR-018, `research.md` R-8, and `contracts/enum-domain.md` C-6's closing paragraph.
- [x] CHK012 - Is it clear that the check-ordering difference is deliberately **recorded but NOT registered** as a DV row, with the reason given (unobservable on every legitimate corpus row), so nobody mints a spurious DV-5? [Clarity, Spec §FR-018] — PASS: `contracts/enum-domain.md` C-6 closing paragraph, header "Deliberately NOT a register row", states the reason and forbids DV-5.

## Generator configuration — what is pinned, and where

- [x] CHK013 - Are **all four** non-enum QuickFIX booleans named individually (`m_checkFieldsHaveValues`, `m_checkFieldsOutOfOrder`, `m_checkUserDefinedFields`, `AllowUnknownMsgFields`), rather than referred to collectively as "the flags"? [Completeness, Clarity, Spec §FR-019] — PASS: FR-019 names all four with individual settings; `research.md` R-7 tabulates each with default/setting/reason; `tasks.md` T003 repeats the census.
- [x] CHK014 - Is the requirement that the flags and topology be pinned **in the generator's own source, never as CLI arguments** stated *with its reason*? [Clarity, Spec §FR-019, §FR-024] — PASS: FR-019/FR-024 item1 state this with the reason (`generator_source_hash` pins source, not CLI-configurable values).
- [x] CHK015 - Is `m_checkFieldsHaveValues`'s **default of `true`** called out as the specific switch that front-ran the enum check and produced this bundle's original false parity claim? [Completeness, Spec §FR-008, §FR-019] — PASS: FR-019 ("This is precisely how FR-008's false parity claim arose"), `research.md` R-7 table row 1.
- [x] CHK016 - Is the **dictionary topology** pinned as a *property* (`sessionDD == appDD`, single-DD / non-FIXT, applied **per the corpus row's own dictionary**) rather than as a **version name** ("FIX 4.4")? [Clarity, Consistency, Spec §FR-019] — PASS: FR-019 second paragraph states the property form explicitly and documents the O2-6 correction away from the version-named form.
- [x] CHK017 - Is the *reason* topology matters stated — QuickFIX's two-DD FIXT path enum-checks `MsgType(35)` against **FIXT11** (zero codes ⇒ unconstrained), a validation topology **fixpp does not possess**? [Completeness, Spec §FR-019] — PASS: FR-019 + `research.md` R-12(b) give the full mechanism with source anchors (`DataDictionary.cpp:145-156`, `Session.cpp:1221-1229`, `engine.cpp:210`).

## The manifest gate — the anti-false-green requirement

- [x] CHK018 - Are **all six** manifest fields enumerated individually (`quickfix_version` + soname, `dictionary_sha1` for **every** dictionary the corpus loads, `generator_source_hash`, `corpus_input_hash`, `golden_output_hash`, and the **per-dictionary** topology + four flag settings)? [Completeness, Spec §FR-024] — PASS: FR-024 item1 lists all six explicitly.
- [x] CHK019 - Is it stated **why `golden_output_hash` is load-bearing and not implied by the others**? [Clarity, Spec §FR-024] — PASS: FR-024 item1 bullet states this exactly ("an input hash, a generator hash and a dictionary hash all stay valid when someone hand-edits an accept to a reject").
- [x] CHK020 - Is the requirement that `golden_output_hash` be computed **excluding the manifest block it lives in** stated, so the definition is not self-referential and uncomputable? [Clarity, Edge Case, Spec §FR-024] — PASS: FR-024 item1, final clause of the `golden_output_hash` bullet.
- [x] CHK021 - Is the manifest gate's scope stated **honestly and narrowly** — catches tree drift + hand-edit, explicitly CANNOT catch drift against a newer QuickFIX? [Clarity, Spec §FR-024] — PASS: FR-024 item2 "What it catches, exactly" paragraph, narrowed at Gate A round 2 (O2-3).
- [x] CHK022 - Is there a requirement that the manifest gate be **PROVEN RED**, rather than merely written? [Acceptance Criteria, Measurability, Spec §FR-024] — PASS: FR-024 item2 discriminating-witness clause + `quickstart.md` S-7 step 5 + `tasks.md` T008 (dedicated prove-RED task).
- [x] CHK023 - Is the gate specified to run **without** `reference-engines/` present, so it is genuinely a CI gate and not a local-only one? [Completeness, Spec §FR-024] — PASS: FR-024 item2 ("This runs without reference-engines/"), `tasks.md` T007.

## The out-of-repo generator — structural protection, owner, cadence

- [x] CHK024 - Is the location of `reference-engines/` stated **correctly** — at the **parent** repo root, **outside** the submodule's git boundary — rather than by the earlier (accidentally-true) "gitignored" claim? [Consistency, Spec §FR-024, Plan §Technical Context] — PASS: `plan.md` Technical Context ("finding O-4") and FR-024 both state the corrected parent-root location.
- [x] CHK025 - Is the protection specified as **structural** (validated `FIXPP_QUICKFIX_ROOT` cache variable, target guarded OFF by default, hard-error if set but `lib/libquickfix.so` absent) rather than resting on a gitignore claim? [Clarity, Spec §FR-024] — PASS: FR-024 item3 states exactly this; `tasks.md` T002 repeats it as an implementation task.
- [x] CHK026 - Does the regeneration-and-diff target have a **named owner and a cadence**? [Gap, Completeness, Spec §FR-024] — PASS: FR-024 item3 last paragraph binds it to `[[project_release_interop_quickfix_fix8]]`'s per-release interop run, an owner+cadence; `tasks.md` T009.
- [x] CHK027 - Is the binding to the per-release out-of-CI interop gate required to land as **a line in the release-interop checklist**, not merely as a CMake target — and is a diff specified to be **release-blocking**? [Measurability, Spec §FR-024] — PASS: FR-024 item3 final sentence + `tasks.md` T009 both state this.

## Divergence register — each row argued and measured

- [x] CHK028 - Does **each** register row (DV-1..DV-4) carry all three of: an **argued disposition**, a **B-row**, and a **corpus row that measures it**? [Completeness, Spec §SC-009] — PASS: `contracts/enum-domain.md` C-6 table has non-empty Disposition, Corpus row and B-row columns for all four rows.
- [x] CHK029 - Is **DV-1** stated precisely enough to be non-misleading — parity **by coincidence**, documented and *not relied upon*? [Clarity, Spec §FR-008] — PASS: C-6 DV-1 row + contract C-1 table both state "parity by COINCIDENCE, via a different arm... documented, not relied upon".
- [x] CHK030 - Is **DV-4** correctly classified — engines agree, not a divergence *from* QuickFIX, still a register row under leg (2), `asserted: **true**`? [Consistency, Ambiguity, Spec §SC-009, §FR-022] — PASS: C-6 has a dedicated "Note on DV-4's asserted value" paragraph stating this precisely.
- [x] CHK031 - Is **DV-3**'s evidence a **closed call-site census** (not a spot read)? [Traceability, Measurability, Spec §FR-023] — PASS: `research.md` R-12(a) gives a 5-step closed census (`isFieldValue`→`checkValue`→`DataDictionary.cpp:172`→`iterate`→`FieldMap::begin/end` vs `g_begin/g_end`); repeated in FR-023 and C-6 DV-3.
- [x] CHK032 - Is the requirement that the register be **reconciled against the golden's measured output**, including a **STOP-and-report** obligation, explicit? [Completeness, Spec §SC-009] — PASS: `tasks.md` T006 states both the reconciliation requirement and the STOP-and-report obligation verbatim.

## Audit Result

| Disposition | Count |
|---|---|
| PASS | 32 |
| SPEC-FIXED | 0 |
| DD-DECIDED | 0 |
| WAIVED | 0 |
| **Total** | 32 |

### SPEC-FIXED items

None.

### DD-DECIDED items

None.

### WAIVED items

None.

Anchors spot-verified this audit (checklist-auditor pass, 2026-07-14): `validator.hpp:143,148,325` (field_valid_for → enum_valid ordering; validate_field no-precheck) — held; `dictionaries/FIX44.xml` `MatchType(574)` (no bare `A`, 18 codes) and `TradeCondition(277)` (declares `A`) — held; `dictionaries/{FIX41,FIX42}.xml` tag 166 = `{CED,DTC,EUR,FED,ISO Country Code,PNY,PTC}` — held; ten-dictionary space-bearing-code census (incl. Orchestra's 5708 `fixr:code` values, 0 with a space) reproduced exactly as `{FIX41:166, FIX42:166}` — held. All anchors resolve in the signed-off bundle at Gate A round 5 convergence (submodule `8c8ec699`, user-signed-off 2026-07-14).
