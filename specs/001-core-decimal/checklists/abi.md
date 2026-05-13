# C-ABI Surface Quality Checklist: 001-core-decimal

**Purpose**: Validate the requirements quality of the C-ABI surface — `fixpp_decimal_t` struct, 5 bare boundary functions (parse/format/compare/equal/init), 2 `_checked` siblings, macros, provisional error codes, and the `FIXPP_C_ABI_VERSION_MAJOR == 1` freeze. Round-1 root cause #1 was "contract authored from memory" — this checklist exists specifically to catch any drift from `.specify/2a-decimal.md` v0.3 §5.1–§5.2 and ratified 2026-05-12 additions before merge.

**Created**: 2026-05-12
**Reviewed**: 2026-05-12 — 60 met (initial review found 2 gaps; all resolved via artifact edits same day — see Findings at bottom)
**Feature**: [spec.md](../spec.md) | [plan.md](../plan.md) | [data-model.md](../data-model.md) | [contracts/c_api_decimal.h](../contracts/c_api_decimal.h)
**Audience**: Gate B reviewer (pre-merge hostile review per `[const §XVII.2]`)
**Depth**: Formal release gate

## Layout Requirements — Completeness

- [x] CHK001 Are `sizeof(fixpp_decimal_t) == 16`, `alignof(fixpp_decimal_t) == 8`, and explicit field offsets (mantissa@0, exponent@8, _reserved@9) all specified as static-assertable requirements? [Completeness, Spec §4.5 AC-A1..A2, Data-Model §Entity 4]
- [x] CHK002 Is `is_standard_layout_v<fixpp_decimal_t>` declared as a hard requirement (AC-A3)? [Completeness, Spec §4.5]
- [x] CHK003 Is `_reserved` specified as `int8_t[7]` (not `int8_t _reserved` × 7 separate fields, not a bit-packed field)? [Completeness, Contracts §5.1, Data-Model §Entity 4]
- [x] CHK004 Is `FIXPP_DECIMAL_INITIALIZER` macro expansion specified byte-by-byte as `{ 0, 0, {0,0,0,0,0,0,0} }`? [Completeness, Contracts §5.1]
- [x] CHK005 Is `FIXPP_DECIMAL_INVALID` macro expansion specified byte-by-byte as `{ INT64_MIN, 0, {0,0,0,0,0,0,0} }`? [Completeness, Contracts §5.1]
- [x] CHK006 Is the seam #4 layout-assertion anchor (`src/capi/decimal_assert.cpp` with six `static_assert`s) specified independently of the runtime AC-A tests? [Completeness, Plan §Seam 4, Data-Model §Entity 4]

## Layout Requirements — Clarity

- [x] CHK007 Is the layout-freeze scope (`FIXPP_C_ABI_VERSION_MAJOR == 1`) named explicitly in the contract header? [Clarity, Data-Model §Entity 4, [const §X.1]] — Contracts §5.1 trailing comment explicit
- [x] CHK008 Is the semantic difference between `FIXPP_DECIMAL_INITIALIZER` (canonical zero value) and `FIXPP_DECIMAL_INVALID` (sentinel) called out so callers cannot mix them up? [Clarity, Contracts §5.1] — distinct macro names + Data-Model Entity 4 explicit
- [x] CHK009 Is the canonical-domain precondition (`exponent ∈ [-38, 0]`) stated for every C-ABI ingress point that consumes a `fixpp_decimal_t`? [Clarity, Spec §4.5 AC-S3, AC-C6] — bare path = "ASSUMES canonical domain" (Contracts §5.2 comment), `_checked` path = validated (AC-C6); both paths explicit
- [x] CHK010 Is `_reserved` specified as ignored on read in v1.0 (AC-A4), with the future-semantic-change rule documented? [Clarity, Spec §4.5 AC-A4, Data-Model §Entity 4]

## Boundary Function Signatures — Completeness

- [x] CHK011 Are all 5 bare boundary fn signatures (`fixpp_decimal_parse`, `_format`, `_compare`, `_equal`, `_init`) specified byte-for-byte per `.specify/2a-decimal.md` v0.3 §5.2? [Completeness, Contracts §5.2]
- [x] CHK012 Are both `_checked` siblings (`fixpp_decimal_compare_checked`, `fixpp_decimal_equal_checked`) signatures specified including out-param semantics and return-code shape? [Completeness, Contracts §_checked, Spec §4.3 AC-C6]
- [x] CHK013 Is the by-value-vs-by-pointer choice specified per function (parse: out-pointer, format: by-value src + out-pointer for written count, compare/equal: by-value both args)? [Completeness, Contracts §5.2, Research §D-4]
- [x] CHK014 Is the `int`-direct-return shape for bare `_compare`/`_equal` (not `fixpp_error_t`-wrapped) preserved per 2a §5.2 verbatim? [Completeness, Research §D-4]
- [x] CHK015 Are `extern "C"` linkage requirements specified for every declaration in the contract block? [Completeness, Contracts §C linkage]
- [x] CHK016 Is the self-sufficiency of `c_api/decimal.h` for pure-C consumers (forward typedef for `fixpp_error_t`) specified? [Completeness, Contracts §forward typedef]
- [x] CHK017 Is the `<stdint.h>` + `<stddef.h>` include set specified to give a pure-C consumer enough types? [Completeness, Contracts §includes] — `<stdint.h>` for `int64_t`/`int8_t`/`INT64_MIN`, `<stddef.h>` for `size_t`

## Boundary Function Signatures — Clarity & Consistency

- [x] CHK018 Is the signature divergence rejection (round-1's `int fixpp_decimal_format(const fixpp_decimal_t* src, ...)` by-pointer source — REJECTED) documented as a no-go rule? [Conflict resolution, Research §D-4]
- [x] CHK019 Is the parameter-naming consistency (e.g., `dst_cap` vs `dst_len`) preserved per 2a §5.2 (round-1 deviated from the verbatim names)? [Consistency, Research §D-4]
- [x] CHK020 Does every signature in `contracts/c_api_decimal.h` carry an `extract from .specify/2a-decimal.md v0.3 §X` lineage comment? [Traceability, Contracts]
- [x] CHK021 Is the call-site discipline ("C++ engine code uses bare path; SWIG bindings / untrusted-context C consumers use `_checked`") specified in the contract block? [Clarity, Contracts §_checked]

## Error Model

- [x] CHK022 Is the provisional numeric value for `FIXPP_ERR_DECIMAL_INVALID = 10` specified with the dated `// allocated 2026-05-12, owned by 2i` comment? [Completeness, Contracts §error codes]
- [x] CHK023 Is the provisional numeric value for `FIXPP_ERR_DECIMAL_PRECISION_LOSS = 11` specified with the same dated comment? [Completeness, Contracts §error codes]
- [x] CHK024 Is `FIXPP_ERR_BUFFER_TOO_SMALL = 3` documented as a generic reused code (not decimal-specific)? [Clarity, Contracts §error codes, Data-Model §Entity 5]
- [x] CHK025 Is the `fixpp::core::error` → `fixpp_error_t` mapping (`_invalid_input` + `_overflow` → `_DECIMAL_INVALID`; `_precision_loss` → `_DECIMAL_PRECISION_LOSS`; `_buffer_too_small` → `_BUFFER_TOO_SMALL`) specified? [Completeness, Data-Model §Entity 5]
- [x] CHK026 Is the "2i ratification by numeric-range re-use" rule specified including the post-ratification Tier-2-breakage clause? [Completeness, Spec §4.7]
- [x] CHK027 Is `FIXPP_ERR_UNKNOWN = 2` (reserved by `[const §X.4]` / 2i §4.5) preserved and documented in the contract block? [Consistency, Contracts §error codes, [const §X.4]]

## Versioning

- [x] CHK028 Is the "once published in tagged C-ABI, numeric values never change meaning" rule per `[const §X.4]` documented for the provisional codes? [Completeness, [const §X.4]] — Spec §4.7 "any change after ratification is a Tier 2 ABI breakage"
- [x] CHK029 Is the version-downgrade behavior (engine maps codes outside the consumer's published minor version to `FIXPP_ERR_UNKNOWN`) referenced or scoped to 2i? [Coverage, [const §X.4], 2i §4.5] — inherited via `[const §X.4]` → 2i §4.5 reference; "owned by 2i" comment in contracts confirms scoping
- [x] CHK030 Is the abidiff Tier-2 hard-fail policy specified including the "from first tagged ABI release onward" boundary? [Completeness, [const §IX.5]] — Quickstart §3 + `[const §IX.5]` together
- [x] CHK031 Is the rule "every breaking C-ABI change pairs with a MAJOR bump on `FIXPP_C_ABI_VERSION_MAJOR`" specified? [Completeness, Quickstart §3, [const §X.1]]

## Reentrancy

- [x] CHK032 Is the reentrancy contract per C-ABI symbol (thread-safe / single-thread / requires-session-lock) documented? [Coverage, [const §X.5]] — Contracts §5.2 reentrancy block + `_checked` reentrancy block
- [x] CHK033 Are all 5 bare + 2 `_checked` fns annotated with their threading posture in the contract block (the current contract states "all five boundary functions are thread-safe")? [Completeness, Contracts §5.2 reentrancy block] — bare block covers all 5; `_checked` block covers the 2 siblings; total 7 covered
- [x] CHK034 Is the "no engine-global state, no allocation" property of every boundary fn stated as a thread-safety justification? [Clarity, Contracts §5.2 reentrancy]

## Forward-Compatibility (_reserved)

- [x] CHK035 Is the writer contract for `_reserved` (zero-init recommended via `FIXPP_DECIMAL_INITIALIZER` / `fixpp_decimal_init()`, not required) explicitly stated? [Completeness, Spec §4.5 AC-A5b]
- [x] CHK036 Is the reader contract ("v1.0 engine ignores `_reserved` on read") consistent with the writer-recommended-not-required clause (no contradiction with AC-A4)? [Consistency, Spec §4.5 AC-A4 vs AC-A5b]
- [x] CHK037 Is the future-semantic-change rule ("any future v1.x meaning for `_reserved` is opt-in via a NEW API, never silent meaning change") specified? [Completeness, Spec §Clarifications 2026-05-10]
- [x] CHK038 Is the `FIXPP_C_ABI_DECIMAL_RESERVED_USED` feature macro mentioned as the future opt-in vehicle? [Completeness, Data-Model §Entity 4]
- [x] CHK039 Is seam #10 (`_reserved` byte tolerance test) tied to AC-A4 + AC-A5b as a regression guard? [Traceability, Plan §Seam 10]

## Cross-Language Consumer

- [x] CHK040 Are SWIG/Python binding expectations on the bare-vs-`_checked` path documented (Python binding from untrusted contexts uses `_checked`)? [Coverage, Spec §3.3, Contracts §_checked]
- [x] CHK041 Is the ctypes layout expectation (`struct { int64, int8, int8[7] }`) specified in a form that a non-C++ binding generator can pick up directly from the header? [Measurability, Spec §4.5 AC-A1..A3] — Contracts §5.1 bare C struct
- [x] CHK042 Is the worst-case 41-byte output buffer for `fixpp_decimal_format` documented in the C-ABI contract for caller allocation? [Completeness, Spec §4.2 AC-S5, Contracts §5.2 format comment] — "Worst-case bound: 41 bytes (sign + '0.' + 38 leading zeros + 19 mantissa digits)"
- [x] CHK043 Are the error-code numeric values exposed as `#define` symbols (not just enum) so a non-C++ binding generator can resolve them? [Coverage, Contracts §error codes]

## Inheritance from 2a-decimal.md v0.3

- [x] CHK044 Does every contract block in `contracts/c_api_decimal.h` carry the literal `// extract from .specify/2a-decimal.md v0.3 §X` lineage header? [Traceability, Contracts]
- [x] CHK045 Are the `_checked` siblings flagged as "NOT in 2a v0.3" with the ratification source (2026-05-12 `/clarify`, option 1, research.md D-12)? [Traceability, Contracts §_checked]
- [x] CHK046 Is round-1's "from-memory contract authoring" root cause (six independent shape divergences) reflected as a "literal extracts only" process rule? [Conflict resolution, Plan §Round 1+2]

## ABI-Affecting Mandatory Controls

- [x] CHK047 Is `gate_a_required: yes` set in spec.md front-matter to trigger `[const §X.6]`'s four mandatory controls? [Completeness, Spec front-matter]
- [x] CHK048 Are all four `[const §X.6]` mandatory controls (`/clarify`, `/analyze`, Codex Gate A, user `/plan` sign-off) referenced as having run for this feature? [Coverage, Plan §Gate A]
- [x] CHK049 Is the abidiff Tier 2 golden initialization documented in quickstart.md §3 with a working `abidiff` command? [Completeness, Quickstart §3]

## C++ Symbol Leakage Discipline

- [x] CHK050 Is the "no C++ symbol leakage through C-ABI" rule per `[const §X.2]` specified as a verification step for this feature? [Completeness, [const §X.2]] — Plan §Constitution Check + Tasks T049b (post-/analyze remediation)
- [x] CHK051 Is the verification mechanism (`nm` Linux / `dumpbin` Windows) specified? [Measurability, [const §X.2]] — Tasks T049b explicit `nm` command
- [x] CHK052 Is the verification scope (`libfixpp_capi.so` exported symbol set must contain only `fixpp_*` `extern "C"` entries) specified unambiguously? [Clarity, Tasks T049b] — `grep -v '^fixpp_'` filter explicit

## Ambiguities & Conflicts

- [x] CHK053 Is the AC-C6 resolution (option 1, `_checked` siblings) consistent across spec.md AC-C6, research.md D-12, plan.md Round 2 NEEDS CLARIFICATION resolution, and contracts/c_api_decimal.h? [Consistency, four-way cross-check]
- [x] CHK054 Is the rejection of round-1's reshaped `_compare`/`_equal` (silent ABI redesign with error-channel out-param) documented as a precedent preventing future "by-pointer" drift? [Conflict resolution, Research §D-4]
- [x] CHK055 Is the seam #4 split (compile-time `decimal_assert.cpp` runtime-build + Tier 2 abidiff golden) consistent with the no-runtime-asserts framing (decimal_assert.cpp is not a test file)? [Consistency, Plan §Seam 4, Tasks T032 (post-remediation)]
- [x] CHK056 Is the `FIXPP_DECIMAL_T` alias-swap C-ABI invariance (AC-B4) explicitly stated to prevent future "optimize C-ABI for the wider type" drift? [Completeness, Spec §4.6 AC-B4]
- [x] CHK057 Are the AC-A1..A6 layout requirements specified independently of byte-order (no endianness-specific clauses, no `__attribute__((packed))`)? [Coverage, Spec §4.5] — **RESOLVED 2026-05-12**: data-model.md §Entity 4 gained an "Endianness" sub-section explicitly scoping v1.0 to little-endian platforms (x86_64 Linux + Windows), naming `FIXPP_C_ABI_BIG_ENDIAN` as the future opt-in feature macro, and noting that `sizeof`/`offsetof`/`alignof` invariants are byte-order-agnostic at the C level but cross-endian binary interchange is out of scope for v1.0.
- [x] CHK058 Is the empty-input vs invalid-byte distinction at the C-ABI specified (parse takes `(src, src_len)` — `src_len == 0` is empty-input AC-P1, not invalid-byte AC-P4)? [Edge Case, Contracts §5.2 parse, Spec §4.1] — `(src, src_len)` signature + AC-P1 ("empty") vs AC-P4 ("non-digit") distinction
- [x] CHK059 Is the master-header include path (`include/fix/c_api.h` includes `fix/c_api/decimal.h`) documented and tracked as a required PR change? [Coverage, Plan §Project Structure, Tasks T033b] — Tasks T033b (post-/analyze remediation) added this
- [x] CHK060 Is the future-opt-in API contract for `_reserved` (when `FIXPP_C_ABI_DECIMAL_RESERVED_USED` is defined) sketched so consumers know what shape to expect? [Coverage, Data-Model §Entity 4] — **RESOLVED 2026-05-12**: data-model.md §Entity 4 gained a "Future use sketch (non-binding, post-v1.0)" sub-section enumerating candidate semantics (byte 9 NaN/Inf indicator; byte 10 alternate-mantissa-encoding flag; bytes 11–15 further extension) and re-affirming the "new function alongside, never silent re-interpretation" rule (`fixpp_decimal_parse_v2` example given).

## Findings (2026-05-12 review + resolution)

**Initial review tally**: 58 met, 0 partial, 2 gaps (60 items total).
**Post-resolution tally**: 60 met, 0 partial, 0 gap (all resolved same day via artifact edits).

**Resolutions applied 2026-05-12:**

- **CHK057 (was gap)** — data-model.md §Entity 4 gained an "Endianness" sub-section: v1.0 supports little-endian platforms only (x86_64 Linux + Windows); big-endian portability is post-v1.0 with a future `FIXPP_C_ABI_BIG_ENDIAN` feature macro; `sizeof`/`offsetof`/`alignof` invariants remain byte-order-agnostic at the C level.
- **CHK060 (was gap)** — data-model.md §Entity 4 gained a "Future use sketch" sub-section enumerating non-binding candidate semantics for `_reserved[7]` (NaN/Inf indicator, alternate-mantissa-encoding flags, further extension) with the re-affirmation that the eventual semantic ships as a NEW function (e.g., `fixpp_decimal_parse_v2`) — never as silent re-interpretation.

## Notes

- Check items off as completed: `[x]`
- Add comments or findings inline
- Link to relevant resources or documentation
- Items are numbered sequentially for easy reference
- 60 items total — formal release gate depth per `/speckit-checklist` invocation 2026-05-12
- Traceability: ≥95% of items carry an explicit spec/plan/data-model/contracts/research/const reference
- Round-1 root cause #1 (contract drift) was on the C-ABI surface; this checklist exists as the recurrence guard
