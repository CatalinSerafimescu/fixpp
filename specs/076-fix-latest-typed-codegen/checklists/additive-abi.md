# Additive / Frozen-Surface / Build-Option Requirements Checklist: FIX Latest Typed Codegen (`fixpp::vlatest`)

**Purpose**: Validate the quality of the requirements governing additivity (byte-identical legacy tiers), frozen surfaces (C-ABI/Python/link-ABI), the build option, determinism, and the governance amendment — Gate-B review audience.
**Created**: 2026-07-15
**Feature**: [spec.md](../spec.md) · [contracts/build-and-verification.md](../contracts/build-and-verification.md)

## Requirement Completeness

- [x] CHK001 Is the build-option requirement complete — a dedicated CMake switch, **default ON**, with CI exercising **both** the ON path (full sanitizer/preset matrix) and the OFF path? [Completeness, Spec §FR-003] — PASS: FR-003, contract B-1, and research R4 all state the switch/default/dual-CI-path requirement identically; task T006 implements it.
- [x] CHK002 Are the OFF-path toggle-lifecycle requirements specified — track last-used option value + force regen on change, remove the stale `vlatest` output dir on OFF, export no `fixpp::vlatest` target/include when OFF? [Completeness, Spec §B-1 (P2-5)] — PASS: contract B-1 "Toggle lifecycle (P2-5)" bullet lists the first two sub-requirements verbatim; research R4 "OFF-path stale-output lifecycle" elaborates rationale; task T015 implements them. **SUPERSEDED 2026-07-16:** the third sub-requirement ("export no `fixpp::vlatest` target/include when OFF") is not a separate gate — per T015/B-1 (gate-b/r2 correction) there is no dedicated `fixpp::vlatest` CMake INTERFACE target in the first place (every tier shares the single `_codegen/include` root), so "no vlatest export when OFF" reduces to, and is fully satisfied by, the filesystem-absence removal (sub-requirement 2). See `contracts/build-and-verification.md` V-7.
- [x] CHK003 Is the golden-inventory scope specified so V-4 and V-7 cite one consistent inventory? [Completeness, Spec §V-4 / V-7] — **SUPERSEDED 2026-07-16:** the originally-planned extended inventory (`vlatest` Fields/Messages/Validator/Reify/Builders + Manifest AND legacy + `_dispatch/`) was NOT landed. The as-built, consistent inventory both gates now cite is: golden = `vlatest_Messages.golden.hpp` ONLY (matches the 003/069 `Messages.hpp`-only precedent); everything else (`Fields/Validator/Reify/NormativeReferences.md/Manifest.txt/Builders.hpp` per legacy version, plus `_dispatch/*.hpp`) has no golden and is additivity-proven by the OFF-vs-ON relative byte-diff walk instead; `Builders.hpp` is absent entirely per the builder-tier descope (spec.md Clarifications → Session 2026-07-16). See `contracts/build-and-verification.md` V-4/V-7 and `contracts/golden/README.md` for the full landed accounting.
- [x] CHK004 Are the zero-change frozen-surface requirements (C-ABI symbol golden, Python surface, link-ABI) specified with the verifying gate? [Completeness, Spec §FR-008 / SC-004] — PASS: FR-008, SC-004, and contract V-6/C-4 name the requirement and the verifying gate (existing C-ABI/ABI-golden gate + Python surface check, `ctest -L abi`, task T022).

## Requirement Clarity

- [x] CHK005 Is the byte-diff **baseline** unambiguously defined as the checked-in golden (obtained from the repo), NOT a re-comparison against a live `main` checkout? [Clarity, Spec §V-7] — PASS: contract V-7 states this verbatim ("CI obtains it from the repo (a checked-in golden), NOT by re-comparing to a `main` checkout").
- [x] CHK006 Is the determinism requirement clear that the golden check runs under the **FULL** ctest, not a narrow target (the 072 stale-golden trap)? [Clarity, Spec §FR-011 / V-4] — PASS: contract V-4 and task T017 both explicitly require the full ctest run, citing the narrow-target trap by name.
- [x] CHK007 Is the fail-closed-on-unknown-datatype requirement stated with a defined disposition (thrown/enumerated, non-zero exit, never mis-typed)? [Clarity, Spec §FR-010 / V-5] — PASS: FR-010, contract V-5, and research R9 state the disposition (thrown, non-zero exit, never mis-typed) identically; code-read confirms the reused fail-closed path (`orchestra_parse_error : xml_parse_error`, `include/fixpp/dict/error.hpp:97-98`) exists and is live via `OrchestraLoader` (`include/fixpp/dict/orchestra_loader.hpp:27-33`).

## Requirement Consistency

- [x] CHK008 Is the "byte-identical to `main` for the four legacy tiers" claim expressed consistently as an executable gate (V-7) rather than an observational note across FR-004, SC-003, and contract V-7? [Consistency, Spec §FR-004 / SC-003 / V-7] — PASS: FR-004 ("enforced as an executable gate by the OFF-path byte-diff gate V-7"), SC-003 ("Enforced by gate V-7"), and contract V-7 itself all agree it is executable, not observational.
- [x] CHK009 Is the injective wire-ApplVerID invariant stated consistently (exactly one `application_version::v50sp2` case; no duplicate arm) across FR-009, SC-005, INV-2, and contract V-3? [Consistency, Spec §FR-009 / SC-005] — PASS: all four artifacts state the invariant identically; code-read confirms today's `kAppVersions` (`emit_dispatch.cpp:61-65`) already lists exactly one `v50sp2` entry, consistent with the "would introduce a duplicate" premise if `vlatest` were wired in.

## Acceptance Criteria Quality

- [x] CHK010 Can the OFF-path outcome be objectively verified? [Measurability, Spec §V-7 / B-1] — PASS: contract V-7 requires filesystem absence of `vlatest/`; task T018 implements it. **SUPERSEDED 2026-07-16:** the originally-planned second assertion ("absence of an exported interface target/include path") is not a separate witness — per V-7 (gate-b/r2 correction) there is no separate `fixpp::vlatest` CMake target/include genex to begin with, so filesystem absence alone fully satisfies "no vlatest export when OFF." See `contracts/build-and-verification.md` V-7.
- [x] CHK011 Is the build-cost measurement obligation specified with a measurable output (configure + clean-compile wall-time + binary-size delta ON vs OFF) and an explicit re-raise trigger if the cost is surprising? [Measurability, Spec §Assumptions / R6] — PASS: spec Assumptions, plan Technical Context, research R6, quickstart, and task T020 all specify the same three measured outputs and the same re-raise trigger ("materially worse — CI-timeout risk / disproportionate binary growth").

## Dependencies, Assumptions & Governance

- [x] CHK012 Is the constitution amendment requirement specified across **both** loci — Article I §1 (narrow the FIX-Latest typed-codegen carve-out) AND Article XVIII §2 line 339 (reconcile the v1.2 A-035..A-065 roadmap line) — as one v0.7→v0.8 MINOR bump, with the Sync Impact Report required to list both? [Completeness/Consistency, Spec §plan Constitution Check] — DD-DECIDED §plan.md "Constitution Check" (Amendment required section): both loci are named explicitly — Article I §1 (constitution.md lines 63-65, verified: "FIX Latest (typed-codegen / session-negotiation tiers)... are post-1.0 milestones") and Article XVIII §2 line 339 (verified: "v1.2 — FIX Latest application messages (new MsgTypes A-035..A-065...)") — with the Sync Impact Report requirement stated to list both, in one v0.7→v0.8 MINOR bump, folded into Gate A per the established precedent (035/043/068/069/074/075). Per this audit's brief: the constitution text is intentionally left pre-amendment until merge (the amendment payload rides Gate A / applies at merge), so the current pre-amendment text is NOT a Consistency defect — it is the frozen-authority record of the settled plan, traceability-referenced here rather than re-spec'd into spec.md. Both anchors independently confirmed to resolve in `.specify/constitution.md` (current, pre-076 revision) by direct read.
- [x] CHK013 Is the dependency on 074 (`OrchestraLoader`, `session_version::vlatest`) and 067/069 (emitter, `CoverageMode`/build-option pattern) documented as a validated assumption, not an open question? [Assumption, Spec §Dependencies / Assumptions] — PASS: spec Dependencies section cites both as MERGED (PR #192, PR #185/#187); Assumptions section states "Vehicle = the existing 067/069 codegen emitter... NOT hand-authoring." Code-read confirms both are live: `dict::OrchestraLoader` (`include/fixpp/dict/orchestra_loader.hpp:27`) and `session_version::vlatest` (`include/fixpp/dict/version_profile.hpp:43`) exist; `kCodegenVersions`/`app_version_enum` (`ir.cpp:212-227`, `gen_util.hpp:248-253`) exist in the shape 067/069 shipped. No open question remains.

## Notes

- Audience: Gate B reviewers. Items test whether the additivity / frozen-surface / build-option / governance **requirements** are well-written and executable.
- Disposition each item at `/speckit-checklist-audit`. Governance items (CHK012) may be DD-DECIDED against plan.md's signed-off Constitution Check.

## Audit Result

| Disposition | Count |
|---|---|
| PASS | 12 |
| SPEC-FIXED | 0 |
| DD-DECIDED | 1 |
| WAIVED | 0 |
| **Total** | 13 |

### SPEC-FIXED items

None.

### DD-DECIDED items

- CHK012 — anchors Article I §1 (constitution.md lines 63-65) + Article XVIII §2 (constitution.md line 339); rationale: settled in plan.md's signed-off Constitution Check as a v0.7→v0.8 MINOR bump riding Gate A, applied at merge — the current pre-amendment constitution text is expected/by-design, not a defect, per the Gate-A-fold precedent (035/043/068/069/074/075).

### WAIVED items

None.

Anchors spot-verified: `.specify/constitution.md` Article I §1 (lines 63-65, "FIX Latest (read/dictionary tier only)" bullet + the post-1.0 carve-out sentence) — resolves, matches plan.md's citation verbatim. `.specify/constitution.md` Article XVIII §2 line 339 ("v1.2 — FIX Latest application messages...") — resolves, matches plan.md's citation verbatim. Both confirmed in the current (pre-076) signed-off constitution revision (v0.7, per CLAUDE.md "Constitution v0.7" note on 075).

Additional code-read anchors spot-verified this audit: `include/fixpp/dict/error.hpp:97-98` (`orchestra_parse_error : xml_parse_error`); `include/fixpp/dict/orchestra_loader.hpp:27-33` (`OrchestraLoader`, throws `orchestra_parse_error`); `tools/codegen/fixpp-codegen/emit_dispatch.cpp:61-65` (`kAppVersions` = v42/v44/v50sp2, single `v50sp2` entry); `include/fixpp/dict/version_profile.hpp:43` (`session_version::vlatest`) — all resolve as cited.
