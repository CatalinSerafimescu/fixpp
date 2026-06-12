# Interop Checklist: FIXT.1.1 / FIX 5.0 SP2 Session Establishment

**Purpose**: Requirements-quality validation for the live conformance surface — cell enumeration, counterparty configuration, golden conventions, the manifest-flip, and the version-general live demonstration against the reference engines.
**Created**: 2026-06-12
**Feature**: [spec.md](../spec.md) · [contracts/fixt-logon-establishment.md](../contracts/fixt-logon-establishment.md) (C10) · [quickstart.md](../quickstart.md)
**Audience/Depth**: Reviewer (PR) · Standard

## Requirement Completeness

- [x] CHK001 Are the live cells **fully enumerated** as a requirement (the 8 concrete cells = 2 dialect families × 2 roles × {QFcpp, QFJ})? [Completeness, Spec §C10/quickstart] — PASS: C10 enumerates all 8 cells by name: `HP-fixt50sp2-qfcpp-init`, `HP-fixt50sp2-qfcpp-acc`, `HP-fixt50sp2-qfj-init`, `HP-fixt50sp2-qfj-acc`, `HP-fixt44-qfcpp-init`, `HP-fixt44-qfcpp-acc`, `HP-fixt44-qfj-init`, `HP-fixt44-qfj-acc`; quickstart table mirrors all 8; tasks T025 registers all 8. 2 families × 2 roles × 2 engines = 8. Fully enumerated as a requirement.
- [x] CHK002 Are counterparty configuration requirements specified (`TransportDataDictionary=FIXT11.xml`; `AppDataDictionary=FIX50SP2.xml`/`FIX44.xml`; `DefaultApplVerID=9`/`6`)? [Completeness, Spec §C10/quickstart] — PASS: C10 specifies "Counterparty config templates: `TransportDataDictionary=FIXT11.xml`; `AppDataDictionary=FIX50SP2.xml` (50sp2 family) / `FIX44.xml` (4.4 family); `DefaultApplVerID=9` (50sp2) / `6` (4.4)"; quickstart duplicates the same config block; tasks T025 repeats all three config keys. Configuration requirements are fully specified.
- [x] CHK003 Is the manifest-flip requirement specified end-to-end (off `deferred:fixt-routing` → live, plus the `cell_results.yaml` schema-check)? [Completeness, Spec §FR-012/C10] — PASS: FR-012 says "MUST update the interop matrix manifest so the FIXT.1.1/5.0SP2 cell is no longer deferred"; C10 says "the manifest no longer carries `deferred:fixt-routing`"; tasks T028 specifies "Flip the manifest: `tests/interop/cell_results.yaml` off `deferred:fixt-routing` → `status: pass`/`matrix_disposition: live` for all 8 cells; update `cell_results_schema_check_test.py` if a tag retires; bank both golden layers; confirm schema-check passes". End-to-end including schema-check. Complete.
- [x] CHK004 Is the requirement that the shared tag-`554` redactor runs in the interop golden writer before any golden is banked stated (security∩interop)? [Completeness, Spec §C8 site 3] — PASS: C8 enumerates site 3 as "the `tests/interop` golden writer/normalizer (`phase-9-harness/tools/run_interop_cell.py`)"; the C8 requirement also states this must run "before any golden is written"; quickstart says "`Password(554)` is redacted by `run_interop_cell.py`'s shared tag-554 redactor before any golden is written (C8)"; tasks T026 says "wire the shared T010 tag-`554` redactor into `phase-9-harness/tools/run_interop_cell.py`'s golden writer/normalizer before any golden is written (C8 interop class)". Requirement is stated at the security∩interop boundary.

## Requirement Clarity

- [x] CHK005 Is the golden-bank procedure specified clearly enough to execute — the **2-pass** `--update-goldens` capture across **both** golden layers (in-repo `diff_golden_or_skip` + engine-log seam)? [Clarity, quickstart] — PASS: quickstart specifies the 2-pass procedure with exact commands: `python3 tools/run_interop_cell.py HP-fixt50sp2-qfcpp-init --config normal --update-goldens # x2` then re-run without `--update-goldens` expecting "pass, golden match"; tasks T027 says "2-pass `--update-goldens` capture (both golden layers: in-repo `diff_golden_or_skip` + engine-log seam), then re-run flag-free expecting `pass; golden match` for both engines, both roles". Both golden layers named, procedure executable.
- [x] CHK006 Is "passes live against both reference engines, both roles" defined as an objective, banked-golden-match outcome? [Measurability, Spec §SC-004] — PASS: SC-004 states "the previously `deferred:fixt-routing` interop axis passes live against both reference engines (QuickFIX-cpp and QuickFIX-J), in both roles, with banked goldens"; C10 says "each establishes and matches a banked golden"; tasks T027 requires the flag-free re-run to show "pass; golden match" (banked-golden-match is the objective criterion, not just "runs"). Objectively measurable.

## Requirement Consistency

- [x] CHK007 Do the cell names in contract C10 match those in quickstart's table and plan's Project Structure (`HP-fixt50sp2-*` / `HP-fixt44-*`)? [Consistency, Spec §C10 / quickstart / plan] — PASS: C10 names: `HP-fixt50sp2-qfcpp-{init,acc}`, `HP-fixt50sp2-qfj-{init,acc}`, `HP-fixt44-qfcpp-{init,acc}`, `HP-fixt44-qfj-{init,acc}`. Quickstart table: same 8 names in a matching 4-row table. Plan Project Structure: "8 cells (HP-fixt50sp2-{qfcpp,qfj}-{init,acc} + HP-fixt44-{qfcpp,qfj}-{init,acc})". All three sources use the same prefix convention and same 8 cells. Consistent.
- [x] CHK008 Is the `DefaultApplVerID` wire mapping (`9`=5.0SP2, `6`=4.4) consistent between the interop config and the render-helper emit tests in research R3? [Consistency, Spec §C10 / research R3] — PASS: C10/quickstart/T025 all say `DefaultApplVerID=9` for 50sp2 and `DefaultApplVerID=6` for 4.4. R3 pinned emit tests say `v50sp2`→`1137=9` and `v44`→`1137=6`; data-model E3 repeats the same; version_profile.hpp shows `v44 = 5` (C++ enum index) and `v50sp2 = 8` (C++ enum index), with the wire values diverging from the enum indices for lower versions but coinciding here (enum `v44`→5 but wire `"6"`; enum `v50sp2`→8 but wire `"9"` — R3 notes the wire values do coincide for these two but MUST NOT be relied on). The wire values 9 and 6 are consistent across C10, quickstart, tasks, and R3's emit tests.

## Scenario & Coverage

- [x] CHK009 Are requirements defined for **both** dialect families live (FIX.5.0SP2 AND a FIXT-carrying-FIX.4.4) — demonstrating transport/application decoupling, not just 5.0SP2? [Coverage, Spec §SC-004/SC-006] — PASS: SC-004 explicitly requires "for a **FIX.5.0SP2** session AND a representative **FIXT.1.1-carrying-FIX.4.4** session"; SC-006 says "demonstrated by establishing at least two distinct application versions"; C10 has 4 cells in the FIX.4.4 family (`HP-fixt44-*`); tasks T025 registers both families; spec Assumptions §2 confirms "Live conformance validation covers FIX.5.0SP2 plus one representative FIXT.1.1-carrying-FIX.4.4 session". Both families required as live cells.
- [x] CHK010 Is SC-006 ("no version-specific establishment code path") objectively demonstrable via the cells + the `negotiated_version_profile()` accessor (W5/C6), not merely a "both reach Active" proxy? [Measurability, Spec §SC-006/C6] — PASS: C6 specifies the discriminating assertion: `Engine::lookup(sid)->negotiated_version_profile().default_appl == application_version::v44` for the 4.4 cell and `== v50sp2` for the 5.0SP2 cell; R4 says "This is what makes SC-006 demonstrable (New-1 / W5): the witness asserts `Engine::lookup(sid)->negotiated_version_profile().default_appl == v44` for the 4.4 cell"; tasks T015 mirrors this as the W5 witness. The discriminating accessor assertion proves version-general selection, not a "both reach Active" proxy — per the anti-proxy discipline [[feedback_witness_asserts_named_postcondition_not_proxy]].

## Dependencies & Assumptions

- [x] CHK011 Is the assumption that the reference engines are configured as the FIXT/5.0SP2 conformance oracle (transport + app dictionaries) documented and validated against R1? [Assumption, Spec §Assumptions / research R1] — PASS: spec Assumptions §6: "The reference engines (QuickFIX-cpp, QuickFIX-J) are the conformance oracles, configured with a FIXT.1.1 transport dictionary + a FIX.5.0 SP2 application dictionary"; R1 documents the QFcpp oracle evidence (Session.cpp:674/701 dual-advertise, :1210-1212 inbound 1137 read, :1253 missing-1137 RequiredTagMissing); C10 specifies the counterparty dictionaries (`FIXT11.xml` + `FIX50SP2.xml`/`FIX44.xml`). Documented and grounded in R1.
- [x] CHK012 Is the live-cell sandbox caveat captured as a process requirement (sockets allowed / run outside sandbox / self-run) to avoid a silent-SKIP false pass? [Coverage, Gap — tasks T027] — PASS: tasks T027 explicitly says "Run all 8 cells **live with sockets allowed** (outside the sandbox — the 032 close-out pattern; sandbox silently SKIPs sockets → false-pass per `feedback_codex_sandbox_blocks_sockets_false_pass`). … Self-run — do not trust a sandboxed green"; quickstart says "live cells need real TCP — run outside the sandbox / with sockets allowed, the 032 close-out pattern". The caveat is a named process requirement, not merely a note.

## Traceability

- [x] CHK013 Is each of the 8 cells traceable to SC-004/SC-006 and to FR-012 (manifest)? [Traceability, Spec §C10/FR-012] — PASS: C10 closes with "*(FR-012/SC-004/SC-006)*" trace tag covering all 8 cells in the clause; tasks T025 (register cells) and T028 (manifest flip) both trace to `C10`; quickstart "Done when" section cross-references SC-001–SC-006 and FR-012. SC-004 requires both families and both engines; SC-006 requires the version-general demonstration (the 4.4 cells). FR-012 requires the manifest flip. All 8 cells are covered by these three references via C10's bulk clause.

## Notes

- A failing item flags a **requirement-text** weakness in the interop scope, not a live-run failure.
- The live runs themselves are exercised by tasks T025–T028 and validated at `/speckit-verify`, not by this checklist.

## Audit Result

| Disposition | Count |
|---|---|
| PASS | 13 |
| SPEC-FIXED | 0 |
| DD-DECIDED | 0 |
| WAIVED | 0 |
| **Total** | **13** |

### SPEC-FIXED items
_(none)_

### DD-DECIDED items
_(none)_

### WAIVED items
_(none)_

Anchors spot-verified: C10/quickstart/T025 (8 cells enumerated by name), C10/quickstart/T025 (counterparty config: FIXT11.xml/FIX50SP2.xml/FIX44.xml, DefaultApplVerID=9/6), FR-012/C10/T028 (manifest flip + schema-check), C8/quickstart/T026 (tag-554 redactor in golden writer), quickstart/T027 (2-pass golden-bank procedure, both layers), SC-004/C10/T027 (banked-golden-match outcome), C10/quickstart/plan-Project-Structure (HP-fixt50sp2-*/HP-fixt44-* cell names consistent), C10/R3/E3/version_profile.hpp (wire mapping 9=5.0SP2, 6=4.4 consistent), SC-004/SC-006/C10/T025 (both families required live), C6/R4/W5/T015 (discriminating accessor assertion, not proxy), Assumptions/R1 (reference engines as oracle), T027/quickstart (sandbox caveat as process requirement), C10/T025/T028/quickstart (SC-004/SC-006/FR-012 traceability) — all resolve in signed-off bundle.
