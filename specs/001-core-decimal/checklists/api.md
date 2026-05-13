# C++ API Surface Quality Checklist: 001-core-decimal

**Purpose**: Validate the requirements quality of the public C++ API surface — `fixpp::core::pod_decimal`, `decimal_traits<T>`, `decimal<T>`, the `FIXPP_DECIMAL_T` build-time alias, and the `detail::trap_throw` helper. Tests whether the requirements are written well enough that an implementer cannot misinterpret them and a downstream feature (`2b`, `2c`, `2k`) can inherit without re-asking.

**Created**: 2026-05-12
**Reviewed**: 2026-05-12 — 60 met (initial review found 2 partial + 1 gap; all resolved via artifact edits same day — see Findings at bottom)
**Feature**: [spec.md](../spec.md) | [plan.md](../plan.md) | [data-model.md](../data-model.md) | [contracts/decimal_traits.hpp](../contracts/decimal_traits.hpp)
**Audience**: Gate B reviewer (pre-merge hostile review per `[const §XVII.2]`)
**Depth**: Formal release gate

## Requirement Completeness

- [x] CHK001 Are all 7 normative members of `decimal<T>` (default ctor, value ctor, `value()`, `parse`, `format`, `from<U>`, `to<U>`) enumerated as required? [Completeness, Data-Model §Entity 3]
- [x] CHK002 Are both friend operators (`operator==` and `operator<=>`) specified as required public surface members? [Completeness, Contracts §4.3]
- [x] CHK003 Is the namespace-scope `using decimal_default = decimal<pod_decimal>;` alias specified as required (not optional convenience)? [Completeness, Data-Model §Entity 3] — Research D-13 explicitly preserves it
- [x] CHK004 Are all 8 required `decimal_traits<T>` static functions (`from_chars`, `to_chars`, `from_pod`, `to_pod`, `compare`, `is_finite`, `is_zero`, `is_negative`) enumerated with full signatures? [Completeness, Data-Model §Entity 2]
- [x] CHK005 Are the 3 required member types/constants (`value_type`, `is_lossless_for_fix_float`, `max_serialized_bytes`) of `decimal_traits<T>` specified? [Completeness, Data-Model §Entity 2]
- [x] CHK006 Is `pod_decimal_invalid = {INT64_MIN, 0}` specified as a named `inline constexpr` constant exported from `fixpp::core`? [Completeness, Contracts §4.1]
- [x] CHK007 Is the `detail::trap_throw<F>` helper signature, namespace placement, and `noexcept` guarantee specified? [Completeness, Contracts §decimal_helpers]
- [x] CHK008 Is the `FIXPP_DECIMAL_USER_HEADER` opt-in conditional include mechanism specified? [Completeness, Contracts §4.4]
- [x] CHK009 Is the link-time sentinel mechanism (`decimal_alias_sentinel<T>::tag` + `fixpp_decimal_alias_lock`) specified including which TU emits the definition? [Completeness, Data-Model §Entity 6] — `src/core/decimal.cpp` emits exactly one specialization
- [x] CHK010 Is the contribution of 4 named `fixpp::core::error` variants (`decimal_invalid_input`, `_overflow`, `_precision_loss`, `_buffer_too_small`) by this feature explicitly stated? [Completeness, Data-Model §Entity 5]

## Requirement Clarity

- [x] CHK011 Is the canonical domain (`mantissa ∈ [INT64_MIN+1, INT64_MAX]`, `exponent ∈ [-38, 0]`) defined unambiguously with `INT64_MIN` reserved as the invalid sentinel? [Clarity, Data-Model §Entity 1]
- [x] CHK012 Is `is_lossless_for_fix_float` defined as "value-equal round-trip" (not "byte-equal round-trip") so trait authors cannot mis-promise? [Clarity, Contracts §4.2, Data-Model §Entity 2] — Contracts §4.2 explicit: "Value-lossless, not byte-equivalent"
- [x] CHK013 Is the namespace placement of `decimal_alias_sentinel` (`fixpp::detail`, NOT `fixpp::core`) called out explicitly as a contract requirement? [Clarity, Data-Model §Entity 6]
- [x] CHK014 Is the visibility of `decimal<T>::value_` (private, with `value()` accessor) specified to prevent direct field exposure? [Clarity, Contracts §4.3]
- [x] CHK015 Is the difference between the default-constructed `pod_decimal{}` (value `0`, NOT sentinel) and `pod_decimal_invalid` explicitly documented? [Clarity, Data-Model §Entity 1]
- [x] CHK016 Is "value equality, not field equality" stated unambiguously enough that an implementer will not accidentally default `operator==` on `pod_decimal`? [Clarity, Spec §4.3, Contracts §4.1]
- [x] CHK017 Is the `if constexpr (std::is_same_v<T,U>)` short-circuit in `decimal<T>::to<U>()` specified as an implementation choice (not a return-type carve)? [Clarity, Research §D-11]
- [x] CHK018 Is the `noexcept` requirement on every public function of `decimal<T>` and `decimal_traits<pod_decimal>` stated as a contract requirement (not a recommendation)? [Clarity, Plan §Tech Context] — `[arch §5.3]` cited

## Requirement Consistency

- [x] CHK019 Do `contracts/decimal_traits.hpp`, `data-model.md §Entity 3`, and `spec.md §3` agree byte-for-byte on the 7 `decimal<T>` member signatures? [Consistency, three-way cross-check]
- [x] CHK020 Is `expected_t<T>` referenced consistently as `std::expected<T, fixpp::core::error>` per `[arch §4.1]` with no local `decimal_error` enum redefinition? [Consistency, Research §D-8]
- [x] CHK021 Does the cross-traits `to<U>()` return type stay uniformly `expected_t<decimal<U>>` for both T==U and T≠U branches (no `conditional_t` carve)? [Consistency, Research §D-11, Contracts §4.3]
- [x] CHK022 Does the `decimal_traits<T>::compare` return type (`std::strong_ordering`) match what `decimal<T>::operator<=>` returns and what `operator==` reduces to? [Consistency, Data-Model §Entity 2 vs §Entity 3]
- [x] CHK023 Are PMR semantics (required, non-null, ignored by non-allocating traits) stated consistently across `data-model.md`, `contracts/decimal_traits.hpp`, and `research.md D-6`? [Consistency, four-way cross-check]
- [x] CHK024 Does the `decimal<T>::from<U>` signature (`expected_t<decimal>`) match between `contracts/decimal_traits.hpp §4.3` and `data-model.md §Entity 3` (note `from` returns `expected_t<decimal>`, not `expected_t<decimal<U>>`)? [Consistency, Contracts vs Data-Model] — `from` returns `decimal<T>` (this surrounding type) from a `decimal<U>` source; `to<U>` returns `decimal<U>`; both forms agree across contracts + data-model
- [x] CHK025 Is the round-1 contract-drift root cause reflected in every contract block via `// extract from .specify/2a-decimal.md v0.3 §X` lineage headers? [Consistency, Plan §Round 2]

## Acceptance Criteria Quality

- [x] CHK026 Is each AC-P bullet (P1..P10) tied to a specific input/output pair so an implementer cannot mis-derive the parse rules? [Measurability, Spec §4.1] — all 10 ACs have explicit examples (`.5`/`5.`, `00005`, `5.500` → `{5500, -3}`)
- [x] CHK027 Is each AC-S bullet (S1..S6) tied to byte-level expected output (e.g., `"0"`, `"5500"`, 41-byte worst case)? [Measurability, Spec §4.2]
- [x] CHK028 Is AC-C1 ("value equality, not field equality") backed by a concrete example set (`{1,0}`, `{10,-1}`, `{100,-2}`)? [Clarity, Spec §4.3]
- [x] CHK029 Is AC-C2 (`pod_decimal_invalid` strictly greater than every finite value, equal only to itself) testable without ambiguity about NaN-like semantics? [Measurability, Spec §4.3] — total ordering, no NaN-like properties
- [x] CHK030 Is AC-X1's "funnel through pod_decimal" mechanic specified including error propagation from both `to_pod` and `from_pod`? [Completeness, Spec §4.4] — **RESOLVED 2026-05-12**: spec.md §4.4 AC-X2 now includes "The funnel maps `decimal_overflow` from `decimal_traits<U>::to_pod` to `decimal_precision_loss` on the caller path (per data-model.md §Entity 2 + §Entity 5)."
- [x] CHK031 Is AC-X3's "T==U short-circuit" specified as preserving the uniform `expected_t<decimal<U>>` return shape? [Consistency, Spec §4.4]
- [x] CHK032 Are the four `fixpp::core::error` variants each tied to a specific triggering condition in §4? [Completeness, Spec §4.7]
- [x] CHK033 Is AC-B3 (link-time mismatch) specified with the expected linker-error substring (`decimal_alias_sentinel<...>::tag`)? [Measurability, Spec §4.6] — **RESOLVED 2026-05-12**: spec.md §4.6 AC-B3 now states "The linker error message contains the substring `decimal_alias_sentinel` (for test-assertion stability — rejects spurious failures from unrelated link errors per tasks.md T025)."
- [x] CHK034 Is AC-B4 ("alias swap does not change C-ABI shape") testable independently of the C++ AC-B set? [Measurability, Spec §4.6] — Quickstart §6 step 4 shows the abidiff verification

## Scenario Coverage

- [x] CHK035 Are the public-surface invariants (`noexcept` on every public fn, no arithmetic operators, no runtime polymorphism) stated as requirements with citations, not merely notes? [Coverage, Spec §5, Data-Model]
- [x] CHK036 Is the eager-parse contract ("parse consumes the input span; result does not alias the source") specified as a requirement caller code can rely on? [Coverage, Data-Model §Entity 3]
- [x] CHK037 Is the throwing-3p-traits handling path specified (`trap_throw` for `bad_alloc` → `out_of_memory`, others → `decimal_invalid_input`)? [Coverage, Contracts §decimal_helpers]
- [x] CHK038 Are out-of-scope items (no `+ - * /`, no locale formatting, no direct T→U cross-traits, no `__int128` traits in v1.0) explicitly enumerated to prevent scope creep? [Coverage, Spec §5]
- [x] CHK039 Is `T==U` short-circuit specified to honor round-trip identity unconditionally (including source values outside the PoD domain)? [Coverage, Spec §4.4 AC-X3]

## Edge Case Coverage

- [x] CHK040 Are sentinel-related edge cases (sentinel as operand of compare; sentinel produced by parse overflow; sentinel produced by `to_pod` overflow) enumerated as testable behaviors? [Edge Case, Spec §4] — AC-C2, AC-P8, AC-S1, Entity 2 `to_pod` all cover this
- [x] CHK041 Is the behavior for `pod_decimal{}` (default-init = zero, NOT sentinel) specified to prevent implicit-conversion surprises across `from_pod` / `to_pod` chains? [Edge Case, Data-Model §Entity 1]
- [x] CHK042 Is the forward-compat rule for `decimal_traits<T>` (new required members only behind `FIXPP_DECIMAL_TRAITS_FEATURE_<NAME>` macro) specified to prevent silent ABI-of-the-traits-customization-point breakage? [Edge Case, Data-Model §Entity 2]
- [x] CHK043 Is the `decimal<T>::from<U>` / `to<U>` behavior when `decimal_traits<U>` is undefined (no specialization shipped) specified — does this give a clean compile-error or a confusing template diagnostic? [Edge Case, Contracts §4.3] — **RESOLVED 2026-05-12**: data-model.md §Entity 2 added a new invariant ("Specialization required for cross-traits use") mandating a fixpp-authored compile-error via `static_assert` OR C++20 concept; contracts/decimal_traits.hpp §4.3 comment block extended to reference the data-model invariant. Mechanism is an /implement-time choice; error message must name `decimal_traits<U>`.

## Non-Functional Requirements

- [x] CHK044 Is "no exceptions on hot path" specified as `noexcept` declarations on the C++ surface, not just a goal? [NFR, Spec §6, Data-Model]
- [x] CHK045 Is the "zero allocation between parse and `fromApp`" requirement bound to the C++ API (so a future C++-only consumer can rely on it without the C-ABI layer)? [NFR, Spec §6, [const §VIII.5]]
- [x] CHK046 Is `max_serialized_bytes = 41` for `pod_decimal` specified as part of the public contract (so callers can size buffers without re-deriving)? [NFR, Data-Model §Entity 2]
- [x] CHK047 Are the `constexpr` requirements on `decimal<T>::decimal()`, `decimal<T>::decimal(T)`, and `value()` specified? [NFR, Contracts §4.3]

## Dependencies & Assumptions

- [x] CHK048 Is the dependency on `include/fixpp/core/error.hpp` (owned by **2k**) documented including the four contributed variants? [Dependency, Plan §Project Structure, Data-Model §Entity 5]
- [x] CHK049 Is the dependency on `[arch §4.1]` for the `expected_t<T>` alias documented in every artifact that uses it? [Traceability, Contracts §expected_t alias]
- [x] CHK050 Is the inheritance from `.specify/2a-decimal.md` v0.3 documented at each contract block with section reference? [Traceability, Contracts]
- [x] CHK051 Is the assumption "the wire layer always passes a valid PMR resource" documented as a callsite contract (not silently relied upon)? [Assumption, Data-Model §Entity 2, [arch §5.2]]
- [x] CHK052 Are the C++23 dependencies (`std::expected`, `std::pmr`, `std::span`, `std::strong_ordering`, `if constexpr`, deducing `this`) all listed in plan.md Tech Context? [Dependency, Plan §Tech Context]
- [x] CHK053 Is the assumption "consumers built with a different `FIXPP_DECIMAL_T` than the library see a link error" verifiable independently of the test harness? [Assumption, Data-Model §Entity 6]

## Ambiguities & Conflicts

- [x] CHK054 Is round-1's "contract-from-memory" failure history reflected as a process rule preventing recurrence (literal extracts only)? [Conflict resolution, Plan §Round 1+2]
- [x] CHK055 Is the AC-C6 / 2a §5.2 contradiction resolution (option 1, `_checked` siblings) consistent across spec.md AC-C6, research.md D-12, plan.md Round 2, and contracts? [Consistency, four-way cross-check]
- [x] CHK056 Is AC-X3's "uniform return-type" resolution (D-11) consistent with the rejection of round-1's `conditional_t<is_same_v<T,U>, decimal<U>, expected_t<decimal<U>>>` carve? [Conflict resolution, Research §D-11, Contracts §4.3]
- [x] CHK057 Are all deferred items (`__int128` traits Q2, direct T→U seam Q1, replay coupling Q3, latency revision Q4) tracked with explicit Q-references in spec.md §10? [Traceability, Spec §10]
- [x] CHK058 Is the rejection of round-1's local `decimal_error` enum (research.md D-8) documented as a precedent preventing future error-channel splits? [Conflict resolution, Research §D-8]
- [x] CHK059 Are the cross-doc cites (`[arch §4.1]`, `[arch §4.10]`, `[arch §5.3]`, `[FIX50SP2 §3.3]`, `[SYN §3.1 Q5]`) defined unambiguously enough that a reviewer can resolve each to actual upstream text? [Ambiguity, Plan §Citation verification] — Plan has 26-row citation verification pass
- [x] CHK060 Is the rule that `decimal_alias_sentinel` lives in `fixpp::detail` (not `fixpp::core`) traced to the round-1 finding that flagged this divergence? [Traceability, Data-Model §Entity 6]

## Findings (2026-05-12 review + resolution)

**Initial review tally**: 57 met, 2 partial, 1 gap (60 items total).
**Post-resolution tally**: 60 met, 0 partial, 0 gap (all resolved same day via artifact edits).

**Resolutions applied 2026-05-12:**

- **CHK030 (was partial)** — spec.md §4.4 AC-X2 extended to state the `decimal_overflow` → `decimal_precision_loss` mapping explicitly.
- **CHK033 (was partial)** — spec.md §4.6 AC-B3 extended to state the `decimal_alias_sentinel` substring requirement.
- **CHK043 (was gap)** — data-model.md §Entity 2 gained a new invariant ("Specialization required for cross-traits use"); contracts/decimal_traits.hpp §4.3 comment block extended to reference the invariant. Mechanism (`static_assert` vs C++20 concept) deferred to `/implement` time; the error message must name `decimal_traits<U>` explicitly.

## Notes

- Check items off as completed: `[x]`
- Add comments or findings inline
- Link to relevant resources or documentation
- Items are numbered sequentially for easy reference
- 60 items total — formal release gate depth per `/speckit-checklist` invocation 2026-05-12
- Traceability: ≥95% of items carry an explicit spec/plan/data-model/contracts/research/const reference
