# Logging-Config Requirements-Quality Checklist: 045-observability-config

**Purpose**: "Unit tests for English" — validate that the REQUIREMENTS for the logging-leg TOML loader are complete, clear, consistent, measurable, and cover the highest-risk surfaces, BEFORE implementation. Tests the spec/plan/research wording, not the (not-yet-written) code.
**Created**: 2026-06-20
**Feature**: [spec.md](../spec.md) · [plan.md](../plan.md) · [research.md](../research.md) · [data-model.md](../data-model.md)
**Focus areas**: fail-closed + zero-side-effects-on-failure; side-effect-free load-time preflight; build-conditional sink symmetry; per-key collect-ALL diagnostics; credential redaction; exactly-one-key deferral flip; no-step-1-regression.

## Fail-Closed & Zero-Side-Effects (US2 / FR-014 / FR-015 / SC-003)

- [ ] CHK001 Is the ordering requirement "validate + preflight, then construct the live Logger only on a clean whole-file accumulator" stated unambiguously, with the construction step named as the SOLE side-effectful step? [Clarity, Spec §FR-015 / research §D-7]
- [ ] CHK002 Is "nothing is left opened on a failed load" decomposed into objectively-checkable post-conditions (no sink file opened, no directory created, no drain thread started)? [Measurability, Spec §FR-015 / §SC-003]
- [ ] CHK003 Are the requirements explicit that a per-session logger error suppresses construction of an EARLIER session's (and the engine's) logger (whole-file scope, not per-session)? [Completeness, research §D-7 N-2 / data-model §E-5]
- [ ] CHK004 Is the prohibition on silent no-op-logger substitution stated as a requirement (not merely implied), and is "no-op logger" defined? [Clarity, Spec §US2 / §FR-001]
- [ ] CHK005 Is the named inherited-017 TOCTOU limitation (ctor silently disables a sink whose open() fails post-preflight) documented as an explicit, bounded limitation rather than left as an unstated gap? [Completeness, research §D-7 / Spec §FR-015]

## Side-Effect-Free Load-Time Resource Preflight (FR-014)

- [ ] CHK006 Is "side-effect-free" preflight quantified by the exact operations allowed (stat/access only; no mkdir; no probe file write)? [Measurability, Spec §FR-014 / data-model §validation-summary]
- [ ] CHK007 Are the three preflight checks each specified with their pass/fail condition: file-sink directory already-exists-and-writable, OTLP cert readable+PEM-magic, OTLP endpoint present/non-empty? [Completeness, Spec §FR-014 / data-model §E-4]
- [ ] CHK008 Is the boundary between load-time validation (PEM-magic only) and deferred sink-open() work (full CA-bundle parse) stated unambiguously, with the rationale for not doing the full parse at load? [Clarity, research §D-4]
- [ ] CHK009 Is each preflight failure mapped to a specific reason_class (dir/cert → invalid_or_contradictory_selector; endpoint → missing/empty_required) so the requirement is testable per-key? [Measurability, data-model §validation-summary]
- [ ] CHK010 Is it specified that FileSink::open() opens the log FILE (not the directory), so the "directory must pre-exist" requirement is internally consistent across spec/research/quickstart? [Consistency, Spec §FR-014 / quickstart §note]

## Build-Conditional Sink Symmetry (FR-013)

- [ ] CHK011 Are BOTH build-conditional cases (syslog without FIXPP_HAS_SYSLOG; otlp without FIXPP_CONFIG_HAS_OTLP) required to resolve to the SAME reason_class (invalid_or_contradictory_selector)? [Consistency, Spec §FR-013 / research §D-3]
- [ ] CHK012 Is the distinction between "build/platform-unavailable kind" (invalid_or_contradictory_selector) and "recognized-but-deferred" (recognized_not_yet_supported_step2) stated as a requirement so the two are never conflated? [Clarity, Spec §FR-013 / Clarifications 2026-06-20]
- [ ] CHK013 Is the syslog `facility` closed set fully enumerated (no ellipsis) AND is the build-conditional `#ifdef LOG_*` sub-rule (in-set name whose macro is undefined → invalid_or_contradictory_selector) specified distinctly from the not-in-set rule (→ unknown_enum)? [Completeness, data-model §E-4 / research §D-3]
- [ ] CHK014 Is it required that a build-unavailable sink is NEVER silently skipped or dropped from the chain (loud, not silent)? [Coverage, Spec §FR-013]

## Per-Key Collect-ALL Diagnostics (FR-020 / FR-021 / SC-007)

- [ ] CHK015 Does the spec require every diagnostic to carry all of {offending key/selector path, reason_class, file location}, and is the reason_class taxonomy enumerated (reusing step-1's 9 values, no new value)? [Completeness, Spec §FR-020 / research §verified-surface]
- [ ] CHK016 Is "collect-ALL in one pass" stated as a hard requirement (N independent errors → exactly N diagnostics; no fix-one/re-run loop), including logger errors interleaved with establishment errors? [Measurability, Spec §FR-021 / §SC-007]
- [ ] CHK017 Are the key_path conventions for nested/array selectors specified (e.g., `logger.sinks[1].endpoint`) so diagnostics are unambiguous per sink? [Clarity, quickstart §failure-table / data-model §E-4]
- [ ] CHK018 Is it specified that a still-deferred key reports the deferred reason DISTINCT from an unknown-key typo (so the two reason classes are never merged)? [Consistency, Spec §SC-007 / §FR-022]

## Credential Redaction (FR-023)

- [ ] CHK019 Is the set of sensitive values reachable through a logger selector defined (e.g., credentials embedded in a collector endpoint/header) so redaction scope is unambiguous? [Clarity, Spec §FR-023]
- [ ] CHK020 Is the redaction requirement stated to cover BOTH diagnostics AND logs, consistent with step-1's redaction behavior (no drift in the redaction token/placeholder)? [Consistency, Spec §FR-023]
- [ ] CHK021 Is redaction made objectively verifiable (the diagnostic message must never contain the cleartext secret)? [Measurability, Spec §FR-023]

## Exactly-One-Key Deferral Flip (FR-022)

- [ ] CHK022 Is it specified that EXACTLY `logger` flips from deferred to supported, and is the authoritative remaining-deferred key list enumerated in one place (research §D-6) and consistent across spec/data-model/quickstart? [Consistency, Spec §FR-022 / research §D-6 / data-model §E-5]
- [ ] CHK023 Is the "no previously-supported key may regress" clause stated as a requirement alongside the flip? [Completeness, Spec §FR-022]
- [ ] CHK024 Is the decision to KEEP the `recognized_not_yet_supported_step2` enum symbol (and only generalize the message text) documented with rationale (renaming churns 044 tests)? [Clarity, research §D-6]
- [ ] CHK025 Are the reasons each key stays deferred specified at the right granularity (tracer/meter = export unimplemented; otlp/log_sink/exporter = not standalone blocks; prometheus = no channel; arenas/dialect = next step; tap = unshipped 2l)? [Completeness, data-model §E-5 / research §D-6]

## No Step-1 Regression (FR-025 / SC-004)

- [ ] CHK026 Is "no step-1 regression" made testable: every step-1 file that loaded still loads identically, and every step-1 diagnostic is unchanged except the `logger` flip? [Measurability, Spec §FR-025]
- [ ] CHK027 Is the absent-`[logger]` case required to be byte-identical to the step-1 result (null logger, not an error), and is this consistent between FR-003 and SC-004? [Consistency, Spec §FR-003 / §SC-004]
- [ ] CHK028 Is the additive-only nature of the public surface change (one `EngineEstablishment` field; existing `SessionConfig::logger_override`; no new public type/reason_class/fixpp_error_t) stated as a constraint? [Completeness, Spec §FR-024 / research §D-1]

## Cross-Cutting Clarity / Consistency / Edge Cases

- [ ] CHK029 Is the composite-selector requirement (logger scalars PLUS an ordered, non-empty `[[logger.sinks]]` array) unambiguous, including the zero-sinks-is-an-error rule and the duplicate-sink-kind-is-valid edge case? [Coverage, Spec §FR-005 / §Edge-Cases]
- [ ] CHK030 Is relative-path resolution specified consistently for ALL path-bearing selectors (file-sink directory, OTLP cert) — against the config-file dir, not the process CWD (matching step-1 FR-016a)? [Consistency, Spec §FR-018]
- [ ] CHK031 Is the empty-OTLP-endpoint decision (loader is stricter than the sink's silent-drop default → reject as empty_required) documented with rationale, so the requirement is not mistaken for sink default behavior? [Clarity, Spec §Edge-Cases / data-model §E-4]
- [ ] CHK032 Is the `ring_resource` exclusion (logger buffer allocator stays default; selecting it → deferred arena, never file-set) stated as a requirement so it is not silently mappable? [Completeness, Spec §FR-010 / data-model §E-3]
- [ ] CHK033 Is "frozen at load/open" specified (later edits to the file do not affect a running engine until reload-and-reopen), consistent with step-1? [Consistency, Spec §FR-019]
- [ ] CHK034 Are the `capacity` power-of-2 and `on_overflow` closed-enum requirements quantified with their failure reason_class (out_of_range / unknown_enum), and is "no implicit defaults" stated (absent optional → documented default; absent required → error)? [Measurability, Spec §FR-016 / §FR-017 / data-model §E-3]

## Notes

- Check items off as the spec/plan/research wording is confirmed (or amended): `[x]`.
- This checklist tests REQUIREMENTS quality, not implementation — it is consumed by `/checklist-audit` (pipeline step 9) before `/speckit-implement`.
- The `/checklist-audit` gate dispositions each item PASS / SPEC-FIXED / DD-DECIDED §X / WAIVED:<reason>.
