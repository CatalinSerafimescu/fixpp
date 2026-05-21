# Adversarial / Edge-Coverage Requirements Quality Checklist: Wire Codec

**Purpose**: Formal release-gate validation that edge-case and hostile-input requirements are complete, consistent, and measurable. Tests whether the spec *defines* the edge behaviour — not whether the parser handles it.
**Created**: 2026-05-16
**Audited**: 2026-05-21 (pipeline step 9 — `/speckit-checklist-audit`, retroactive post-merge per POLICY OVERRIDE)
**Feature**: [spec.md](../spec.md) · **Plan**: [plan.md](../plan.md)
**Audience**: Reviewer at Gate A/B

## Partial-Read Coverage

- [x] CHK001 - Is the set of split boundaries (mid-tag, mid-`=`, mid-value, mid-SOH, mid-`BodyLength`, between messages, one byte at a time) enumerated exhaustively rather than "etc."? [Completeness, Spec Edge Cases] — **PASS**: spec Edge Cases "Partial reads at every boundary: bytes split mid-tag, mid-`=`, mid-value, mid-SOH, mid-`BodyLength`, between messages, and a single byte at a time — all must reassemble correctly". Enumerated exhaustively, no "etc." escape.
- [x] CHK002 - Is the carry-over requirement (partial trailing bytes survive to the next feed) stated with a defined storage owner and lifetime? [Clarity, Spec FR-009 / Key Entities] — **PASS**: spec FR-009 ("carrying partial trailing bytes over to the next feed"); Key Entities "Framer carry buffer: caller-managed storage holding partial trailing bytes between feeds"; data-model E2 + `[2b §8]` bind owner ("session-lifetime `SessionConfig::framer_carry_arena`") and lifetime ("session"). Owner + lifetime named.
- [x] CHK003 - Is "reassembles correctly" expressed as a measurable outcome (exactly the original frame sequence, in order, no loss/dup)? [Measurability, Spec SC-004] — **PASS**: spec SC-004 "A byte stream arbitrarily fragmented (down to one byte per feed) at all message and field boundaries reassembles into exactly the original sequence of frames in order, with no lost or duplicated frames". Measurable predicate.

## Repeating Groups

- [x] CHK004 - Are nested-group requirements (groups within groups, W-007) specified distinctly from flat-group requirements? [Completeness, Spec FR-004/Edge Cases] — **PASS**: spec FR-004 ("repeating groups including nested groups (W-006, W-007)"); Edge Cases "Nested repeating groups: groups within groups (W-007) index and round-trip correctly". W-006 vs W-007 enumerated distinctly.
- [x] CHK005 - Is the per-group-instance cap explicitly per-instance, not aggregate, with the exceed error code named? [Ambiguity, Spec Edge Cases/FR-015] — **PASS**: spec FR-015 + Edge Cases explicit "maximum group entries per instance (default 4096 → `wire_group_too_large`)"; Edge Cases "the per-group-instance entry cap (`default_max_group_entries_per_instance`, 4096) is enforced per instance, not aggregate"; `[2b §1.2]` repeats "Per-instance, not aggregate". Exceed error named (`wire_group_too_large`).
- [x] CHK006 - Is the `iter()` vs `operator[]` equivalence stated as a requirement (identical entries/order), making it verifiable? [Measurability, Spec FR-004 / Plan seam #8] — **DD-DECIDED `[2b §9]` seam #8 + data-model E8 + plan Test-seam #8**: data-model E8 explicit "`iter()` and `operator[]` MUST enumerate identical entries/order (seam #8)"; design-doc §9 seam #8 "Repeating-group equivalence — for any group-bearing message, `group_view::iter()` and `group_view::operator[]` enumerate the same entries in the same order. Catches divergence between the two paths"; plan Test-seam #8 binds `tests/wire/repeating_group_equivalence_test.cpp`. Verifiable. Belongs in data-model + design-doc per Phase-1 split (FR-004 carries the surface requirement; equivalence invariant lives in the type-level oracle).

## Length+Data Fields

- [x] CHK007 - Is the W-008 requirement that `Data` is read by length (never misparsed on `=`/SOH) stated unambiguously? [Clarity, Spec FR-005] — **PASS**: spec FR-005 "handle `Length`+`Data` raw-byte field pairs (W-008) such that `Data` content (which may contain `=`/SOH) is read by length and never misinterpreted as delimiters"; Edge Cases "`Length`+`Data` fields (W-008): the raw `Data` field is read using the preceding `Length` field and may legally contain `=` and SOH bytes; it must not be misparsed as field delimiters". Unambiguous.
- [x] CHK008 - Is the Index-mode vs Iter-mode scope boundary for dialect-introduced BLOB pairs explicitly bounded (Iter = static FIX-5.0-SP2 table only; new dialect pairs out of v1.0 Iter scope)? [Coverage, Spec FR-005 / research D-11] — **PASS**: spec FR-005 explicit "Scope boundary (`[2b §4.3]`, design §10 Q5): Index mode handles all dialect-introduced `Length`+`Data` pairs via the runtime dictionary; Iter mode uses a static `constexpr` table of FIX-5.0-SP2-standard pairs and does **not** cover dialect-introduced *new* BLOB pairs for v1.0"; research D-11 + `[2b §10 Q5]` confirm. Boundary bounded.
- [x] CHK009 - Is the deferred case (dialect-introduced new BLOB pair in Iter mode) documented as an intentional exclusion, not an undefined gap? [Gap, Spec FR-005 / research D-11] — **PASS**: spec FR-005 "(those need Index mode or a 2c static-table specialization — deferred per research D-11)"; research D-11 "Dialect-introduced new BLOB pairs are out of Iter-mode scope for v1.0 (tap/logger don't read BLOBs); revisit at 2c if a real dialect needs it". Intentional, documented exclusion.

## Unknown / Custom Fields

- [x] CHK010 - Is the split between dictionary-missing (→ `unknown_fields()`) and dictionary-known-but-invalid-for-MsgType (→ `wire_unexpected_tag`) specified unambiguously? [Conflict, Spec Edge Cases / data-model E9] — **PASS**: data-model E9 explicit "Filtered view over the offset table yielding `(tag,value)` for **dictionary-missing** tags only. Dictionary-known-but-invalid-for-MsgType tags are NOT here — they produce `wire_unexpected_tag` (validator rule 5)"; Edge Cases "Custom / unknown fields: tags absent from the dictionary are preserved opaquely for byte-exact round-trip and exposed via a filtered `unknown_fields()` view"; data-model Error mapping row `wire_unexpected_tag (42)` "dict-known tag invalid for MsgType — Session-Reject `SessionRejectReason=2`". Split unambiguous (this was Root cause #2 in Gate A r1).
- [x] CHK011 - Is opaque byte-exact round-trip preservation of unknown fields stated as a measurable requirement (original byte order, no vector materialization)? [Measurability, Spec FR-008/Edge Cases] — **PASS**: spec FR-008 "including opaque preservation of unknown/custom fields"; data-model E9 "No vector materialization; round-trip writes them back in original byte order via in-place two-pointer merge (zero alloc)"; `[2b §4.8]` + plan Test-seam #9 ("Unknown-fields preservation (2-case split)"). Measurable: byte-identical round-trip + zero-alloc.

## Corruption / Hostile Input

- [x] CHK012 - Is mandatory CheckSum/BodyLength rejection stated with an explicit "no production bypass" requirement (tests-only hook permitted)? [Clarity, Spec FR-017] — **PASS**: spec FR-017 "`CheckSum` verification MUST be mandatory with no production bypass switch (a tests-only hook is permitted)"; data-model E2 "CheckSum verification mandatory (no production switch)"; `[2b §2]` non-goal "**No checksum-bypass mode**"; `[2b §4.2]` Config comment "CheckSum verification is mandatory and not configurable. Per §2 non-goal ... and `[FIX50SP2 §3]` / W-005, the only way to skip CheckSum is via `detail::framer_test_hooks` in a tests/-only header — never the production API"; tasks.md T039 confirms tests-only hook. Explicit.
- [x] CHK013 - Is each hostile case (oversized frame, offset overflow, out-of-range tag, oversized group) bound to one defined error code with bounded memory? [Completeness, Spec Edge Cases/SC-003] — **PASS**: spec FR-015 enumerates the four caps + their errors: "maximum frame size (default 256 KiB → `wire_frame_too_large`), maximum offset-table occurrences (default 4096 → `wire_offset_table_full`), maximum group entries per instance (default 4096 → `wire_group_too_large`), and tag numeric range `uint16_t` 0..65535 (→ `wire_tag_out_of_range`) — with bounded memory and no crash when exceeded"; Edge Cases mirrors; data-model Error mapping rows 30/35/36/37 confirm. One-error-per-case mapping.
- [x] CHK014 - Is the over-4096 corpus distinction specified clearly — SC-003 measures the reject path at the default cap vs SC-008 measures footprint with the cap raised — so the two are not contradictory? [Consistency, Spec SC-003/SC-008 / research D-7] — **PASS**: spec SC-003 "The offset-table-overflow case here is measured at the **default** cap (the `wire_offset_table_full` reject path); the footprint spike's **raised-cap** measurement is SC-008. The two are distinct and mutually consistent — same cap, two different measurement targets (reject path vs footprint) — and never apply to the same corpus run (see SC-008)"; SC-008 "footprint corpora that exceed `default_max_offset_entries` (4096) ... MUST be measured with the cap **raised** ... distinct from and consistent with SC-003"; research D-7 binds the same. The contradiction is anticipated and resolved inline (Gate A r1 reconciliation).
- [x] CHK015 - Is "clean under sanitizers and fuzzing" specified as a measurable acceptance condition (zero crash / zero OOB / zero unbounded alloc)? [Measurability, Spec SC-003] — **PASS**: spec SC-003 explicit "zero crashes, zero unbounded allocation, zero out-of-bounds access (clean under sanitizers and fuzzing)"; plan Test-seam #11 (fuzzers, ASan+UBSan); tasks.md T050 binds ≥10-min Tier-1 ASan+UBSan campaign. Measurable triple-zero predicate.

## Structural Edge Cases

- [x] CHK016 - Are empty/zero-length values, missing trailer, fields after `CheckSum(10)`, duplicated standard header fields, and out-of-order mandatory header fields each given a defined expected outcome? [Coverage, Spec Edge Cases] — **DD-DECIDED `[2b §6.5]` + data-model Error mapping + spec Edge Cases**: spec Edge Cases enumerates "Empty / zero-length values, missing trailer, fields after `CheckSum(10)`, duplicated standard header fields, and out-of-order mandatory header fields"; out-of-order header → `wire_header_out_of_order` (slot 39); missing trailer is covered by `[2b §6.1]` framing (Framer requires `10=CheckSum|` as the final field; absence → `wire_invalid_field_format` slot 34 via §6.2 mid-field malformation, or rejected pre-parse if BodyLength count is wrong → `wire_invalid_body_length`); fields after `CheckSum(10)` are rejected by `[2b §6.5 rule 2]` ("Trailer: `10=CheckSum` is the last field; nothing follows"); duplicated standard header fields are caught by header-order rule `[2b §6.5 rule 1]`; empty/zero-length values are valid FIX and handled per `[2b §6.2]` (length=0 in `(tag, offset, length)` entry). Each case has a defined outcome at the design-doc level.
- [x] CHK017 - Is the digit-only BodyLength requirement (space-padded `9=   123|` rejected) stated explicitly so the conformance boundary is unambiguous? [Clarity, Spec / research D-5 / Plan §4.5] — **PASS**: research D-5 "BodyLength rendered digit-only with a `memmove` backpatch at `commit()` (no space padding)"; plan §4.5 row + research D-5 "Digit-strict counterparties reject `9=   123|`"; `[2b §4.5]` algorithm step 3 + `[2b §App-C]` Codex P1 #3 binds the rejection; `[2b §9]` seam #12 "explicit space-padded BodyLength (`9=   123|`) frames (verifying the v0.2 `Framer` rejects them per `[FIX50SP2 §3.3]` `Length` data-type rule)"; tasks.md T039 wires the test. Explicit.

## Traceability

- [x] CHK018 - Does every edge case in the spec's Edge Cases section map to at least one FR- or SC-, with no enumerated edge case lacking a defined requirement? [Traceability, Spec Edge Cases ↔ FR/SC] — **PASS**: edge-case → FR/SC matrix: Partial reads → FR-009 + SC-004; Nested repeating groups → FR-004 + SC-001; Length+Data → FR-005 + SC-001; Hostile peer / DoS bounds → FR-015 + SC-003; Custom / unknown fields → FR-008 + SC-001 (round-trip); Corruption signals → FR-017 + SC-003 + Spec US2 acceptance scenario 3 (too-small caller buffer → defined wire error); Empty/missing-trailer/post-CheckSum/dup-header/out-of-order-header → FR-001 (header/trailer ordering surface) + `[2b §6.5]` validator rules + data-model Error mapping. Every enumerated edge case has an FR/SC anchor.
- [x] CHK019 - Are intentionally-excluded scenarios (FIXP/SOFH/SBE, session FSM semantics) stated as explicit exclusions rather than silent omissions? [Gap, Spec Assumptions] — **PASS**: spec Assumptions "v1.0 wire bytes are exclusively Tag=Value SOH (`[FIX50SP2 §3]`). FIXP / SOFH / SBE binary encodings (W-015, W-016) are out of scope (post-v1, P3)"; "Session-FSM semantics (sequence numbers, gap fill, ResendRequest/TestRequest, PossDup) are out of scope — owned by the session-module Phase-4 spec"; "The `dict::reify` bridge itself is out of scope here — it is owned on the dictionary side (003)". Explicit exclusions enumerated.

---

## Audit Result

**Feature**: 004-wire-codec
**Checklist**: `edge.md` (19 items, CHK001–CHK019)
**Design-doc anchor verified**: `.specify/2b-wire.md` Draft v0.2 (Gate A round 1 converged)
**Audit mode**: retroactive post-merge per POLICY OVERRIDE

### Per-domain tally

| Domain | PASS | SPEC-FIX-CANDIDATE | DD-DECIDED | WAIVED | Total |
|--------|------|--------------------|------------|--------|-------|
| Partial-Read Coverage (CHK001–003) | 3 | 0 | 0 | 0 | 3 |
| Repeating Groups (CHK004–006) | 2 | 0 | 1 | 0 | 3 |
| Length+Data Fields (CHK007–009) | 3 | 0 | 0 | 0 | 3 |
| Unknown / Custom Fields (CHK010–011) | 2 | 0 | 0 | 0 | 2 |
| Corruption / Hostile Input (CHK012–015) | 4 | 0 | 0 | 0 | 4 |
| Structural Edge Cases (CHK016–017) | 1 | 0 | 1 | 0 | 2 |
| Traceability (CHK018–019) | 2 | 0 | 0 | 0 | 2 |
| **Total** | **17** | **0** | **2** | **0** | **19** |

### Findings + resolutions

All 19 items dispositioned. Zero SPEC-FIX-CANDIDATEs. Two DD-DECIDED items — both anchored in the design doc per the Phase-1 ownership split (CHK006 equivalence invariant lives in data-model E8 / `[2b §9]` seam #8; CHK016 per-case structural outcomes live in `[2b §6.5]` rules + data-model Error mapping).

The adversarial-edge surface (FR-015 DoS caps + SC-003 corpus + SC-004 partial-read reassembly) is the explicit hostile-peer defense surface; spec + design-doc + data-model + plan are mutually consistent and the over-4096 corpus contradiction (CHK014) is anticipated and resolved inline in SC-003/SC-008 (the Gate A r1 reconciliation).

### Pipeline disposition

- **`/speckit-analyze` re-run required?** NO — zero SPEC-FIX-CANDIDATEs; spec/design-doc unchanged.
- **Verdict**: GREEN.
