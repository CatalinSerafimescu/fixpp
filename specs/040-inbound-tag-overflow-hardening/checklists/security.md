# Checklist: Security & Wire-Correctness Requirements Quality

**Feature**: `040-inbound-tag-overflow-hardening`
**Created**: 2026-06-15
**Audience**: Gate B reviewers
**Purpose**: "Unit tests for the requirements" — validate that the spec's threat model, helper
contract, disposition, completeness, and exclusion requirements are complete, clear, consistent, and
measurable BEFORE implementation. (Tests the requirements, not the code.)

## Threat Model & Scope Completeness

- [x] CHK001 Is the forged-tag-overflow aliasing threat described with a concrete mechanism (uint32
  wrap → small-tag alias) rather than a vague "overflow risk"? [Clarity, Spec §Background]
  — PASS: spec §Background describes the exact mechanism: `tag = tag*10 + digit` into `uint32_t`; without in-loop bound a forged multi-digit token overflows/wraps to a small value; the aliased value is dispatched (not the original). Concrete examples given (34/49/52/56/1137).
- [x] CHK002 Is the attacker class explicitly bounded (TLS-authenticated, CompID-bound per 015; no
  anonymous MITM) so severity (MED) is traceable to the threat model? [Traceability, Spec §Background/Assumptions]
  — PASS: spec §Background explicitly states "TLS-authenticated, CompID-identity-bound counterparty (015) — no anonymous MITM"; §Assumptions repeats it ("threat is a TLS-authenticated, CompID-bound counterparty (015) — MED severity"). Severity directly traceable.
- [x] CHK003 Are ALL live-inbound tag scanners enumerated as an explicit, closed set (the census
  table), and is the basis for "complete" stated (idiom + non-idiom sweep)? [Completeness, Spec §Background]
  — DD-DECIDED §D-3a: spec §Background provides the explicit closed census table (6 rows, 5 in-scope, 1 excluded). The *basis* for the "complete" claim — the secondary non-idiom sweep covering `from_chars`/`strtoul`/`atoi`/`sscanf` — is not stated in spec §Background but is authoritatively recorded in research.md §D-3a ("non-idiom sweep (closed)") and confirmed by the Gate-A Opus adversarial review (which re-verified the census and the non-idiom sweep independently). This is a frozen authority traceability reference; the sweep methodology need not be re-stated in spec.md.
- [x] CHK004 Is each in-scope scanner's aliasable security tags listed (e.g. 34/49/52/56/1137) so a
  reviewer can judge security relevance per site? [Completeness, Spec §Background census]
  — PASS: spec §Background census table column "Aliasable tags" lists per site: (1) Length/Data dispatch, (2) any, (3) 8/35/49/56/108/1137/553/554, (4) 8/49/56, (5) 34/49/52/56/… . Sufficient for security-relevance judgment per site.
- [x] CHK005 Is the `build_replay_frame` exclusion justified by a stated reason (stored own-outbound,
  not inbound) rather than silently omitted? [Completeness, Spec §FR-008/Background site 6]
  — PASS: spec §Background census row 6 explicitly states "NO — stored own-outbound, not inbound"; §FR-008 requires the exclusion be recorded (comment + research/B&L note); §US3 specifies the acceptance scenario. Not silently omitted.
- [x] CHK006 Are non-tag accumulators (value/length/seqnum/checksum) explicitly declared out of scope
  with examples, so a reviewer can confirm the boundary is intentional? [Clarity, Spec §Edge Cases]
  — PASS: spec §Edge Cases explicitly enumerates all non-tag accumulators out of scope with source locations: `framer.cpp:123` BodyLength, `:~173` Checksum, `offset_table.cpp:212` Data length, `parser.hpp:85` `parse_u32`, `validator.hpp:197` group count, `admin_messages.cpp:303` HeartBtInt, `session.cpp:1591` `parse_seqnum`. Intentionality is stated ("MUST NOT be changed").

## Helper Contract Clarity & Measurability

- [x] CHK007 Is the bound value specified as exactly `0xFFFF` (16-bit tag space) with a rationale for
  why no legitimate tag is lost, rather than left as "a sensible limit"? [Clarity, Spec §Clarifications/FR-001]
  — PASS: spec §Clarifications states "Reject any tag whose accumulated value would exceed `0xFFFF` (the 16-bit FIX tag space)"; §FR-001 repeats "exceeds `0xFFFF`"; §Assumptions states "Bounding to `0xFFFF` is correct and loses no legitimate tag (tags `>0xFFFF` are invalid by the 16-bit field width)." Rationale is explicit and complete.
- [x] CHK008 Is "detect the overflow in-loop, before any multiply that could wrap" stated as a
  requirement (not just "add a bound")? [Clarity, Spec §FR-001]
  — PASS: §FR-001 states "detecting the overflow **in-loop** (before any multiply that could wrap a fixed-width accumulator)" as a MUST requirement; §Background §Clarifications both repeat the in-loop pre-multiply distinction. The word "MUST" and the parenthetical make this unambiguous.
- [x] CHK009 Is the helper's return contract (bounded value + ok/overflow signal; MUST NOT embed
  disposition) specified unambiguously? [Clarity, Spec §FR-001, contracts/tag-scan-helper.md]
  — PASS: §FR-001 states "MUST return the bounded tag value plus an overflow/`ok` signal and MUST NOT embed call-site disposition." `contracts/tag-scan-helper.md` §Signature + §Postconditions fully specify the return semantics (true=tag advanced+valid; false=no advance, caller must dispose). Unambiguous.
- [x] CHK010 Is the digit-only precondition stated, AND is the consequence of violating it (a folded
  digit-check accepting a non-numeric tag) documented so it can't be "simplified" away? [Completeness, Spec §FR-007a, research D-3]
  — PASS: §FR-007a explicitly states sites 4/5 MUST keep the explicit non-digit-class check *before* calling the helper; the consequence is spelled out ("if an implementer 'simplifies' by deleting the explicit non-digit line and relying on the helper, a token like `'3a5='` would be accepted and dispatched — a NEW acceptance/aliasing bug" — research D-3). The precondition is also stated in `contracts/tag-scan-helper.md` §Signature. Both the precondition and the "why not fold it in" rationale are documented.
- [x] CHK011 Is boundary correctness made measurable (65535 ok / 65536 reject / wrap-and-continue
  reject / zero-padded ok), and is a compile-time `static_assert` required (not just a runtime test)?
  [Measurability, Spec §FR-001, contracts §Compile-time guarantee]
  — PASS: `contracts/tag-scan-helper.md` §Compile-time guarantee (normative) explicitly requires a `static_assert` block with exactly these four cases (65535 ok, 65536 reject, 429496729649 wrap-reject, zero-padded ok). §FR-007 requires the runtime unit test in addition. The contracts file is cited in CHK011 and the requirement is normative there. T003 in tasks.md implements it.

## Disposition Preservation Consistency

- [x] CHK012 Is each of the 5 sites' on-overflow disposition specified individually (Index
  `entries_.clear()`; Scan `done_`; session scanners `tag_ok=false`/skip), rather than a single
  generic "reject"? [Completeness, Spec §Clarifications, data-model disposition table]
  — PASS: `data-model.md` §Per-site disposition table explicitly lists all 5 sites with their individual dispositions (Index→`status_=err_tag_out_of_range(); entries_.clear(); return;`; Scan→`done_=true; return;`; interpret_logon→`goto next_field;`; scan_first_frame_ids→`tag_ok=false;`; scan_frame_header→`tag_ok=false;`). §Clarifications (2026-06-15) also enumerates them. Not a single generic "reject."
- [x] CHK013 Is the decision to KEEP per-site disposition (not a uniform whole-frame reject) recorded
  with its rationale (minimal blast radius), so the choice is auditable? [Traceability, Spec §Clarifications 2026-06-15]
  — PASS: spec §Clarifications (2026-06-15) explicitly states "NOT a uniform whole-frame reject (that would change all five sites' control flow)" and gives the rationale "Minimal change, lowest blast radius (Opus census rec)." research §D-5 records the same decision with attribution. Fully auditable.
- [x] CHK014 Is it specified that a skipped/forged field can never be surfaced/queryable under the
  aliased tag (the security invariant), distinct from merely "rejected"? [Clarity, Spec §FR-004/FR-005, data-model invariant]
  — PASS: §FR-004 uses "MUST be surfaced/queryable under any aliased small tag" (negative); §SC-001 states "the forged field is **never surfaced/queryable** under any tag `t ≤ 0xFFFF`"; `data-model.md` §Security invariant (SC-001) makes this explicit. Distinct from "rejected" — the un-queryability invariant is explicitly called out.
- [x] CHK015 Are the requirements consistent on how a skipped required header field is handled
  downstream (existing missing-required-field handling provides frame-level rejection)? [Consistency, Spec §Clarifications]
  — PASS: spec §Clarifications (2026-06-15) states "Where the skipped field was a required header field, the session's existing missing-required-field handling provides frame-level rejection." research §D-5 repeats this. There is no conflicting statement in FR/SC/US sections — all disposition text is consistent with per-site skip + downstream missing-field handling.

## 038 Regression Vector & Cross-Feature Consistency

- [x] CHK016 Is the 038 SendingTime-guard regression vector (a forged token aliasing 52 feeding the
  038 MaxLatency guard) called out as an explicit requirement to close, with traceability to the 038
  guard? [Traceability, Spec §US1/SC-002/Normative References]
  — PASS: §US1 description states "The `52`=SendingTime aliasing specifically removes a regression vector against the 038 SendingTime guard"; §SC-002 makes it a measurable outcome ("The `scan_frame_header` defective guard is fixed — `429496729652` no longer aliases SendingTime(52), removing the 038-guard regression vector"); §Normative References cites "038 acceptor SendingTime(52) guard — the regression vector the `scan_frame_header` 52-aliasing defeats (S-019)." Anchor `session.cpp:1493` verified to carry `> 429496729U` (defective guard, `:1493-1496` confirmed in source).
- [x] CHK017 Is the centralization decision (one helper vs N hand-rolled bounds) justified by the
  stated evidence (the one hand-rolled guard shipped wrong), making SC-004 a real requirement?
  [Consistency, Spec §SC-004, research D-1]
  — PASS: §SC-004 explicitly states "Exactly one shared bounded-tag-parse helper exists; the five hand-rolled tag loops no longer each carry their own (divergent) bound. (Invariant-count style assertion / review.)" research §D-1 records the decision with rationale: "the five sites have different loop structures" and the per-site-divergence produced the defect (Opus census rec); alternatives (b) and (c) are explicitly rejected. The evidence (one hand-rolled guard shipped wrong) is the stated justification.

## Acceptance Criteria & Witness Coverage

- [x] CHK018 Does every scanner site have a corresponding wrap-and-continue acceptance scenario /
  witness requirement (not just the central one)? [Coverage, Spec §US1/US2 Acceptance Scenarios, FR-007]
  — PASS: §US1 acceptance scenarios cover scan_frame_header (4 scenarios, including 429496729634/49/52/34 tokens). §US2 acceptance scenarios cover all four remaining sites individually: (1) Index mode, (2) Scan mode, (3) interpret_logon (1137/49/56 aliases), (4) scan_first_frame_ids (49/56 aliases). §FR-007 states "Each of the five sites MUST have a wrap-and-continue negative **unit** witness." Coverage is complete.
- [x] CHK019 Are the canonical verified vectors (`429496729634`→34, `429496729649`→49) pinned as
  required proof tokens across the scanner set, rather than left as "e.g."? [Measurability, Spec §SC-001]
  — PASS: §SC-001 explicitly states "The verified vectors `429496729634` and `429496729649` reject everywhere" as a measurable outcome. §FR-004 names them as required: "in particular the verified vectors `429496729634`→34 and `429496729649`→49." tasks T012 further pins them at Index+Scan sites ("Pin the canonical verified vectors `429496729634`→34 and `429496729649`→49 explicitly at the Index and Scan sites"). Not left as "e.g."
- [x] CHK020 Is the non-digit negative witness (sites 4/5) specified as a required acceptance
  criterion, tied to the FR-007a precondition? [Coverage, Spec §FR-007a]
  — PASS: §FR-007a explicitly requires sites 4/5 MUST have "a **non-digit negative witness** (e.g. a token containing a non-digit) asserting the field is rejected." tasks T007 (site 5) and T012 (site 4 — "non-digit token rejected") both implement it. The precondition rationale is tied directly ("guards against a future 'simplification' that folds the digit check into the helper").
- [x] CHK021 Is the deferral of the live cross-engine witness stated with its rationale and family
  (038 L-038-2), so the absence is a documented decision, not a gap? [Traceability, Spec §FR-007/Clarifications]
  — PASS: §FR-007 explicitly states the live cross-engine witness is "DEFERRED to the Item-1 live-golden workstream (038 L-038-2 family)" with rationale "reference engines do not emit forged overflow tags, so a live witness needs custom hostile-frame injection." §Clarifications (2026-06-15) repeats the deferral with the same family ref. Documented decision, not a gap.
- [x] CHK022 Is "conforming tags unchanged at every site" (no behavioral regression) a measurable
  acceptance criterion (boundary 65535, zero-padded, existing corpora)? [Measurability, Spec §FR-005/SC-003]
  — PASS: §FR-005 states conforming tags "including the maximal `65535` and zero-padded forms" MUST parse exactly as today "at every site." §SC-003 specifies "All conforming tags (boundary `65535`, zero-padded, existing corpora) parse byte-identically to pre-change at every site — zero regressions across wire + session test suites." Boundary and zero-padded forms named explicitly.

## Non-Functional & Boundary Requirements

- [x] CHK023 Is the perf-neutrality requirement (no measurable throughput regression; helper inlines)
  stated measurably enough to witness against a benchmark baseline? [Measurability, Spec §FR-006]
  — PASS: §FR-006 states "no measurable throughput regression" and "inlinable." tasks T016 specifies the witness mechanism: "run `bench/wire/parser_bench` + `bench/wire/offset_table_bench` and confirm no regression vs `bench/baselines/`" — a named baseline exists. The inline mechanism is stated in FR-006 ("no allocation; no measurable throughput regression") and research D-1 ("one compare + multiply-add per digit"). Sufficient for witnessing.
- [x] CHK024 Are the "no new error code / config / codegen / wire / C-ABI change" constraints stated
  as explicit requirements (so the reviewer can confirm scope discipline)? [Completeness, Spec §FR-009]
  — PASS: §FR-009 explicitly lists all: "No new error codes beyond reusing existing out-of-range/invalid dispositions; no new config; no codegen regeneration; no wire-format or C-ABI change." plan.md §Constitution Check repeats each as a PASS item. Reviewer can confirm scope discipline directly against FR-009.
- [x] CHK025 Is the exact-boundary edge case (accumulated value crossing `0xFFFF` mid-scan before any
  wrap; final wrapped value `≤0xFFFF` must still reject) specified, not just the naive case?
  [Edge Case, Spec §Edge Cases]
  — PASS: spec §Edge Cases explicitly states the edge case: "a wrap-and-continue token whose final wrapped value is `≤ 0xFFFF` (e.g. `429496729649`→49) MUST reject — the guard fires on the *accumulated value crossing `0xFFFF` mid-scan*, before any wrap." The boundary is stated in both directions (65535 valid, 65536 rejects) and the wrap-alias case is separately required. This is the exact sub-case the item asks about.

## Notes

- Items are requirements-quality questions for Gate B (do the requirements read complete/clear/
  consistent/measurable), NOT implementation tests. Dispositioned by `/speckit-checklist-audit`.

## Audit Result

| Disposition | Count |
|---|---|
| PASS | 24 |
| SPEC-FIXED | 0 |
| DD-DECIDED | 1 |
| WAIVED | 0 |
| **Total** | **25** |

### SPEC-FIXED items

None.

### DD-DECIDED items

- CHK003 — anchor `research.md §D-3a`; rationale: the non-idiom sweep methodology (from_chars/strtoul/atoi/sscanf) establishing the "complete" claim lives in research.md §D-3a and was independently re-verified by the Gate-A Opus adversarial review (`research/reviews/opus_039-ff-tail-hardening_gate_a_adversarial_review.md`). The closed census set is in spec §Background; the sweep basis is frozen-authority in research. No re-spec needed.

### WAIVED items

None.

Anchors spot-verified:
- `session.cpp:1493` defective guard (`> 429496729U` post-multiply, no `|| (val==429496729U && digit>N)` boundary clause) — confirmed present in source.
- `framer.cpp:120` in-loop pre-multiply bound (`body_length > ((max_frame_bytes - digit) / 10)`) — confirmed.
- `session.cpp:1588` seqnum bound with boundary clause (`val > 429496729U || (val == 429496729U && digit > 5U)`) — confirmed.
- `research/reviews/opus_039-ff-tail-hardening_gate_a_adversarial_review.md` — exists; enumerates "five" live-inbound scanners and confirms non-idiom sweep.
- 040 has no `.specify/2x-*.md` versioned design-doc authority anchor (it is a split-out feature); anchors are code-line citations and the 039 Gate-A census review — all resolved.
