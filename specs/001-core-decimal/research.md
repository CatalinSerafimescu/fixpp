---
id: 001-core-decimal
title: Research — Decimal type design decisions
spec_kit_step: /plan Phase 0
last_updated: 2026-05-12
status: drafted (round-2 redraft)
inherits_design: .specify/2a-decimal.md v0.3 (signed off 2026-05-07; Gate A round 2 converged)
---

# Phase 0 Research — 001-core-decimal

All major decisions are **inherited from `.specify/2a-decimal.md` v0.3** (already signed off after its own two-round Gate A pass). This document does not re-litigate them; it records each decision with rationale + alternatives in the canonical `/plan` Phase 0 format, so `/tasks` and reviewers have a single point of reference. No NEEDS CLARIFICATION items remain — the 2026-05-10 `/clarify` session (5 questions, all answered in `spec.md Clarifications`) closed every Phase-4-specific ambiguity.

## D-1: Default representation = `pod_decimal { int64_t mantissa, int8_t exponent ∈ [-38, 0] }`

- **Decision:** the engine's default decimal representation is a 9-byte payload of `(int64 mantissa, int8 exponent)`, with `mantissa ∈ [INT64_MIN+1, INT64_MAX]` (≈ ±9.22 × 10^18) and `exponent ∈ [-38, 0]` (canonical domain). `INT64_MIN` is the invalid sentinel.
- **Rationale:** chosen for the FX/equities mainstream — with 8 fractional digits, max absolute value is ≈ ±9.22 × 10^10, sufficient for retail and most institutional flow `[const §X.3]`. Restricting `exponent ≤ 0` matches the FIX FLOAT grammar `[FIX50SP2 §3.3]` (no positive exponent form). Restricting `exponent ≥ -38` caps the serializer worst case at 41 bytes (`max_serialized_bytes`).
- **Alternatives considered:** (a) `__int128` mantissa — rejected, no `__int128` on MSVC; complicates the C-ABI shape. (b) IEEE-754 `_Decimal64` — rejected, neither MSVC nor stock Clang ships it portably. (c) Full `decimal128` as default — rejected, doubles ABI size and forces a 16-byte unsigned representation that doesn't compose with the typed-message store layout in **2e**. Consumers who need wider mantissa swap the alias via `FIXPP_DECIMAL_T` (see D-7).

## D-2: `decimal_traits<T>` is the compile-time customization point

- **Decision:** users plug in alternative representations (`decimal128`, `boost::multiprecision::cpp_dec_float`, custom) by specializing `decimal_traits<T>`. There is no runtime polymorphism (no virtual interface, no `std::variant`) per `[SYN §3.1 Q5]`.
- **Rationale:** zero indirection on the hot path; matches the broader fixpp philosophy of compile-time customization (codegen + constexpr metadata + typed accessors per `[const §XV.6]`). Templated-symbol amplification is bounded by the "one alias per build" rule (D-7).
- **Alternatives considered:** (a) runtime-polymorphic `IDecimal` — rejected, adds a vcall per parse/format on the hot path, blowing the latency budget (50 / 30 / 20 ns per D-9). (b) `std::variant<pod_decimal, decimal128, …>` — rejected, requires the engine to know the closed type set at compile time anyway and adds visit overhead.

## D-3: C-ABI boundary form is the same PoD layout — frozen 16-byte struct

- **Decision:** `fixpp_decimal_t` is `struct { int64_t mantissa; int8_t exponent; int8_t _reserved[7]; }`, with `sizeof == 16`, `alignof == 8`, `is_standard_layout` true, and `_reserved` at offset 9. Frozen for `FIXPP_C_ABI_VERSION_MAJOR == 1` `[const §X.1]`.
- **Rationale:** `[const §X.3]` mandates a PoD shape at the C-ABI; choosing the same shape as the C++ default eliminates a per-call conversion at the binding layer for the default-traits case. `_reserved[7]` reserves 7 bytes for future minor-version semantics under the `FIXPP_C_ABI_DECIMAL_RESERVED_USED` feature macro `[2a-decimal §5.1]`.
- **Alternatives considered:** (a) 9-byte struct (no `_reserved`) — rejected, blocks any future flag-byte / NaN-indicator extension without a major-version bump. (b) Pack `exponent` into the high byte of `mantissa` — rejected, breaks `is_standard_layout` and prevents direct `static_cast<pod_decimal>(fixpp_decimal_t)`.

## D-4: C-ABI boundary functions are the **2a §5.2 verbatim signatures** — by-value, `int`-direct-return for compare/equal

- **Decision:** the five C-ABI entry points are (exactly per 2a-decimal.md:253–273):
  - `fixpp_error_t fixpp_decimal_parse(const char* src, size_t src_len, fixpp_decimal_t* out);`
  - `fixpp_error_t fixpp_decimal_format(fixpp_decimal_t d, char* dst, size_t dst_cap, size_t* written);`
  - `int fixpp_decimal_compare(fixpp_decimal_t a, fixpp_decimal_t b);`
  - `int fixpp_decimal_equal(fixpp_decimal_t a, fixpp_decimal_t b);`
  - `void fixpp_decimal_init(fixpp_decimal_t* out);`
- **Rationale:** these are the shapes signed off in 2a v0.3 after two Gate A rounds. `_compare` returns the comparison value directly (`-1` / `0` / `+1`) because the comparison is total and never overflows (digit-string algorithm per §6.3); `-1` is unambiguously "less", not "error". `_equal` returns `0` / `1` for the same reason. `_parse` / `_format` return `fixpp_error_t` because they can fail on data-validation grounds (parse rejection, buffer too small).
- **AC-C6 carve:** the **C-ABI entry points** validate `exponent ∈ [-38, 0]` on inputs (symmetric with AC-S3) and report `FIXPP_ERR_DECIMAL_INVALID` on out-of-domain. The validation path is realized via `_checked` overloads (see D-12) OR a wrapping layer; it does **not** change the 2a §5.2 signature shapes above. C++ `compare` remains `noexcept` and assumes canonical domain (callers from inside the engine produce canonical values by construction).
- **Round-1 divergence (rejected):** round 1's draft used `int fixpp_decimal_format(const fixpp_decimal_t* src, char* dst, size_t dst_len, size_t* out_written)` — by-pointer source, `int` return, `dst_len`/`out_written` param names. **All three diverge from 2a v0.3.** Reverted to verbatim in round 2.
- **Alternatives considered:** (a) by-pointer source params to avoid Windows-x64 stack-spill of 16-byte structs — rejected as Phase-4 silent ABI redesign; if pursued, it must be a 2a v0.4 amendment with rationale. (b) `fixpp_error_t`-returning `_compare` / `_equal` with out-param — rejected as round-1 redesign; the only signal a `_compare`/`_equal` call needs is the value itself plus the canonical-domain validation, which D-12 owns separately.

## D-5: Comparison algorithm = digit-string compare (no `__int128`)

- **Decision:** `compare(a, b)` for two `pod_decimal`s runs the digit-string algorithm of 2a §6.3 — invalid-sentinel check, sign compare, canonicalize (strip trailing-zero base-10 digits), magnitude-bucket compare (`digit_count + exponent`), then lex-compare digit strings on tie. O(digits) ≤ 19 iterations, no allocation, `noexcept`, no `__int128`.
- **Rationale:** `__int128` is unavailable on MSVC and the previous wide-compare draft (v0.1) overflowed for `INT64_MAX × 10^0` vs `INT64_MIN × 10^-38` pairs (Codex 2a P2 #3 + Opus 2a P1 #3). Digit-string compare never overflows and works portably.
- **Alternatives considered:** (a) `__int128` widen — rejected (see above). (b) Decimal-conversion-to-string then `strcmp` — rejected, allocates and ignores `compare` total-ordering contract for invalid sentinel.

## D-6: PMR is **required** in `from_chars`

- **Decision:** `decimal_traits<T>::from_chars(span src, std::pmr::memory_resource* mr) noexcept` — `mr` is a required, non-null parameter. Non-allocating traits (`pod_decimal`) ignore it; allocating traits (`boost::multiprecision::cpp_dec_float`) use it.
- **Rationale:** previous designs allowed an optional PMR overload, which left a contract gap for allocating traits (Codex 2a P2 #4 + Opus 2a P1 #4). Making `mr` required closes the gap; the wire layer always passes the per-message arena `[arch §5.2]`.
- **Alternatives considered:** (a) two overloads (with/without `mr`) — rejected as incoherent contract. (b) Global `pmr::get_default_resource()` default — rejected, allows accidental heap use on the hot path, contradicts `[const §VIII.5]`.

## D-7: Build-time alias via `FIXPP_DECIMAL_T` + template-specialization sentinel

- **Decision:** the engine-wide alias `fixpp::decimal_t = core::decimal<FIXPP_DECIMAL_T>` is selected at library build time via the `FIXPP_DECIMAL_T` preprocessor macro. The library defines exactly one specialization of `fixpp::detail::decimal_alias_sentinel<FIXPP_DECIMAL_T>::tag` in `src/core/decimal.cpp`; consumers built with a different alias reference a specialization the library never defined → link error.
- **Rationale:** every TU that includes `decimal_alias.hpp` emits a reference to the sentinel; the symbol's mangled name encodes `T`, so a mismatched alias fails to link. Avoids the v0.2 bug where the alias-pasting trick was a no-op (macros don't expand inside identifiers) — caught by Codex 2a 2nd-pass P1.
- **Alternatives considered:** (a) header-only `#error` if the macros don't match — rejected, doesn't catch a library built with one alias and a consumer built with another. (b) Runtime check via global constructor — rejected, too late; link-time is the right boundary.

## D-8: Error model = `fixpp::core::error` (single error enum across `core/`)

- **Decision:** decimal-layer errors are members of `fixpp::core::error` (the engine-wide error enum declared at `core/error.hpp`, owned by **2k**). The four named variants are `decimal_invalid_input`, `decimal_overflow`, `decimal_precision_loss`, `decimal_buffer_too_small`. `expected_t<T>` is `std::expected<T, fixpp::core::error>` per `[arch §4.1]`.
- **Rationale:** every other `core/` site (parser, validator, store I/O) carries `fixpp::core::error` in its `expected_t<T>`. A decimal-specific error enum would not compose — the wire layer at **2b** translates `decimal_precision_loss` to `wire_field_value_truncated`, which it cannot do if the source uses a different alias-wrapped error type. Round 1 introduced a local `enum class decimal_error` + a re-bound `expected_t` and was flagged as Codex P1 + Opus P1 (cross-document contradiction).
- **Alternatives considered:** (a) Local `decimal_error` enum — rejected (round-1 divergence). (b) `std::variant<core::error, decimal_error>` — rejected, doubles the failure-channel width without solving the composition problem.

## D-9: Latency targets = 50 ns parse / 30 ns format / 20 ns compare (Linux/Clang/x86_64, warm cache, 5-digit mantissa)

- **Decision:** Tier 1 regression bars are 50 / 30 / 20 ns median per 2a §6.5; bench harness fails CI on breach.
- **Rationale:** these are bench-spike targets, not constitutional minimums. 2a §10 Q4 explicitly allows revision after the **2b** wire integration if the targets prove infeasible. Tier 1 enforcement from day one (rather than "we'll tune later") prevents perf drift.
- **Alternatives considered:** (a) No latency bar in v1.0 — rejected, perf is part of the FIX-engine value proposition `[const §VIII.4]`. (b) Stricter (30 / 20 / 15 ns) — premature; revisit at **2b**.

## D-10: Allocation guard = `mallocnesia` (Linux) + Tier 2 Windows gap documented

- **Decision:** the allocation discipline is enforced on Linux via `mallocnesia` LD_PRELOAD interceptor, wrapped by `tools/check_alloc.py` and exercised by `tests/alloc_guard/decimal_alloc_guard_test.cpp` running a parse-format loop. Windows lacks an equivalent shim of comparable quality; documented as a Tier 2 v1.x gap per `[const §IX.6]`.
- **Rationale:** 2a §9 seam #6 + `[const §VIII.5]`; the Linux Tier 1 coverage is sufficient because the discipline is platform-independent at the source level (no `new`/`delete` on the hot path) — Windows-specific allocation bugs would also surface on Linux Tier 1.
- **Alternatives considered:** (a) `_CrtSetAllocHook` on MSVC — deferred to Tier 2 v1.x because `mallocnesia`-equivalent fidelity is not available. (b) No allocation guard, rely on `noexcept` + code review — rejected, `noexcept` doesn't prove zero-alloc (PMR allocations don't throw on success).

## D-11: AC-X3 short-circuit = `if constexpr` at `/implement` time, **not** a return-type shape change

- **Decision:** for `decimal<T>::to<U>()` with `T == U`, the implementation uses `if constexpr (std::is_same_v<T, U>)` to short-circuit (return the source unchanged, no funnel, no error). The **contract signature** stays uniform `expected_t<decimal<U>>` per 2a §4.3. The short-circuit is an implementation choice (no codegen overhead, no funnel cost), not an API shape change.
- **Rationale:** spec.md `Clarifications` 2026-05-10 line 63 (carried from `/clarify`) chose this resolution. Round 1's `conditional_t<std::is_same_v<T,U>, decimal<U>, expected_t<decimal<U>>>` return-type carve diverged from 2a §4.3 and was flagged as Opus P1 NEW. Keeping the uniform `expected_t<decimal<U>>` return preserves the API shape and lets the implementation choose the optimization.
- **Alternatives considered:** (a) `conditional_t` return-type carve — rejected (round-1 divergence; would require 2a v0.4 amendment). (b) Forward `T == U` through the funnel — rejected per `/clarify` answer (wastes a `to_pod` / `from_pod` round-trip for the no-op case).

## D-12: AC-C6 owned by `_checked` siblings (RESOLVED 2026-05-12 — `/clarify` session 2026-05-12, option 1)

**Decision (ratified by user `/clarify` 2026-05-12):** add `fixpp_decimal_compare_checked` / `fixpp_decimal_equal_checked` C-ABI overloads that own AC-C6's defensive validation. The bare `_compare` / `_equal` stay `.specify/2a-decimal.md` v0.3 §5.2 verbatim (`int` return, by-value, no validation, canonical-domain precondition). C++ engine code uses the bare path; SWIG bindings + future C consumers from untrusted contexts use `_checked`. See `spec.md Clarifications` Session 2026-05-12 and `contracts/c_api_decimal.h` (the `_checked` siblings carry the "ratified 2026-05-12" comment, no longer "PENDING").

**Origin (round-2 redraft context):**

`spec.md §4.3 AC-C6` (from `/clarify` 2026-05-10) states:

> AC-C6. C-ABI entry points `fixpp_decimal_compare` / `fixpp_decimal_equal` validate `exponent ∈ [-38, 0]` on **every** input (symmetric with AC-S3); out-of-domain returns `FIXPP_ERR_DECIMAL_INVALID`.

But `.specify/2a-decimal.md` v0.3 §5.2 freezes these signatures as:

```c
int fixpp_decimal_compare(fixpp_decimal_t a, fixpp_decimal_t b);
int fixpp_decimal_equal  (fixpp_decimal_t a, fixpp_decimal_t b);
```

— `int` return, no error channel. Returning `FIXPP_ERR_DECIMAL_INVALID` from an `int` requires either reusing some `int` value to mean "invalid" (overloading `-1`, which already means "less"), OR reshaping the signatures (round-1's `int fixpp_decimal_compare(const fixpp_decimal_t* a, const fixpp_decimal_t* b, int* out_ordering)` — rejected at round 1 as a silent ABI redesign), OR amending 2a v0.4.

**Three resolutions, must pick one explicitly via `/clarify` or a 2a amendment:**

1. **Add `_checked` overloads, keep 2a §5.2 verbatim.** Add new C-ABI entry points `fixpp_decimal_compare_checked` / `_equal_checked` returning `fixpp_error_t` with an out-param for the ordering. The bare `_compare` / `_equal` stay 2a §5.2 verbatim (`int` return, no validation). Bindings (SWIG → Python, future C consumers from untrusted contexts) use `_checked`; C++ engine code uses the bare entry points. **AC-C6 wording is updated to name both pairs.** Recommended — it preserves 2a §5.2 and gives AC-C6 a real implementation home.
2. **Move AC-C6 validation into C++ only.** The bare `_compare` / `_equal` C-ABI fns stay 2a §5.2 verbatim and **do not validate**. The C-ABI thunks call into C++ helpers that assume canonical domain. AC-C6's "validate on every C entry" is dropped from the spec; the parallel `_parse` / `_format` checks (AC-P*, AC-S3) carry the defense-in-depth load. **Spec.md AC-C6 needs to be deleted or weakened.** Acceptable if the team judges the defense-in-depth on `_compare`/`_equal` is not load-bearing.
3. **Amend `2a-decimal.md` to v0.4.** Change §5.2 to error-code+out-param signatures for `_compare` / `_equal`. Re-Gate-A the amendment. Then 001-core-decimal inherits the new shape. **Heaviest path** — reopens a signed-off design doc.

**`/plan` posture (final, post-resolution 2026-05-12):** the `contracts/c_api_decimal.h` ships bare 2a §5.2 verbatim signatures PLUS the ratified `_checked` siblings. Lineage comments distinguish the two: the bare entry points cite "extract from .specify/2a-decimal.md v0.3 §5.2"; the `_checked` siblings cite "ratified per /clarify 2026-05-12 (option 1, research.md D-12)".

**Provisional numeric range:** `FIXPP_ERR_DECIMAL_INVALID` and `FIXPP_ERR_DECIMAL_PRECISION_LOSS` are allocated in this PR's `include/fix/c_api/decimal.h` with a dated `// allocated 2026-05-12, owned by 2i` comment, independent of D-12's resolution — these codes are needed regardless (they also carry `_parse` / `_format` failures per AC-P*, AC-S*). **2i** ratifies by re-using the same numeric range. Per spec.md §4.7 `Clarifications` 2026-05-10 line 60.

**Why this was flagged rather than picked silently (round-2 redraft note).** Round 1 silently picked option 3's effect (reshaped `_compare`/`_equal` to error-return) without amending 2a — Codex P1 and Opus P1 both flagged it as inheritance defect. The round-2 redraft surfaced the spec.md-vs-2a contradiction as a NEEDS CLARIFICATION and let the user pick. The user picked option 1 on 2026-05-12; this section is preserved as audit trail.

## D-13: `decimal<T>` member surface = 2a §4.3 verbatim, all 7 members + `decimal_default` alias preserved

- **Decision:** the `decimal<T>` class template ships with exactly these public members per 2a §4.3:
  1. `constexpr decimal() noexcept = default;` (default ctor)
  2. `constexpr explicit decimal(T v) noexcept;`
  3. `constexpr T const& value() const noexcept;`
  4. `static expected_t<decimal> parse(std::span<const std::byte> src, std::pmr::memory_resource* mr) noexcept;`
  5. `expected_t<std::size_t> format(std::span<std::byte> dst) const noexcept;`
  6. `template <class U> static expected_t<decimal> from(decimal<U> const&) noexcept;`
  7. `template <class U> expected_t<decimal<U>> to() const noexcept;`
  - Plus friend `operator==` and `operator<=>` (both `noexcept`).

  And the type alias `using decimal_default = decimal<pod_decimal>;` at namespace scope.

- **Rationale:** round 1 dropped the default ctor, `parse`, `format`, friend operators, and the `decimal_default` alias from the contract extract — five normative members + one alias gone. All are required by downstream code (codegen at **2c** uses `parse`/`format`/`operator==`; tests use `decimal_default`).
- **Alternatives considered:** none — the member set is fixed by 2a §4.3.

## D-14: Citation form = `[const §Roman.arabic]` canonical only

- **Decision:** every constitution reference in this bundle uses canonical `[const §Roman.arabic]` form (e.g., `[const §VIII.5]`, `[const §X.3]`). The two-symbol form (Roman numeral and arabic numeral separated by a literal space-and-second-`§` instead of a dot) is forbidden.
- **Rationale:** `constitution.md:5` defines the canonical form; CI linting rules at `[const §VI.2]` reject vague refs. Round 1 mass-rewrote citations to the broken two-symbol form in four of the five bundle files (Codex P2 → Opus P1 escalation).
- **Verification:** `plan.md §Gate A` includes a 26-row table verifying every cite resolves to actual constitution text. The same form is used across `research.md`, `data-model.md`, `quickstart.md`, `contracts/c_api_decimal.h`, and `contracts/decimal_traits.hpp`.

## Open questions resolved

All questions in `spec.md §10` (Q1..Q6) have a disposition. Re-stated here for the redraft audit trail:

| # | Question | Disposition |
|---|---|---|
| Q1 | Direct `T → U` cross-traits seam (skipping PoD) | DEFERRED to first user pull; not in v1.0. |
| Q2 | Built-in `decimal_traits<__int128>` | DEFERRED; not in v1.0. |
| Q3 | Confirm with **2e** raw-frame replay + typed-payload `static_assert` placement | DEFERRED to **2e**; reopen 2a §7.1 only if (a) inverts. |
| Q4 | Latency ceilings 50 / 30 / 20 ns right? | Bench spike during `/implement`; revise if **2b** suggests headroom. |
| Q5 | C-ABI numeric error-code values | RESOLVED 2026-05-10 — provisional values in this PR; **2i** ratifies. |
| Q6 | All 10 seams in this PR vs follow-up | RESOLVED 2026-05-10 — **same PR**. |

No new questions arise from the round-2 redraft.
