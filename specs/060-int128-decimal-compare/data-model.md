# Phase 1 Data Model — C1 / int128 decimal compare

No persistent data, no new public type. The "entities" are the value domain, the one internal
primitive, the constant table, and the test-oracle artifacts.

## Value domain

### `pod_decimal` (existing, UNCHANGED)

- Layout: `{ std::int64_t mantissa; std::int8_t exponent; }` (`include/fixpp/core/decimal.hpp:22-28`).
- Value: `mantissa × 10^exponent`.
- **Sentinel**: `mantissa == INT64_MIN` = invalid; orders **strictly greatest** (AC-C2). Filtered before
  any magnitude code → `INT64_MIN` never reaches negation.
- Canonical exponent domain: `[−38, 0]`. The C++ comparator is **total** beyond it (explicit ctor does
  not validate); the new path preserves totality (`k` computed in `int`).
- Frozen at the C ABI (`[const §X.3]`) — not touched.

## Internal primitive (new, TU-local to `src/core/decimal.cpp`)

### `mul_u64_wide`

```
std::uint64_t mul_u64_wide(std::uint64_t a, std::uint64_t b, std::uint64_t* hi) noexcept
```

| Aspect | Contract |
|---|---|
| Semantics | Unsigned 64×64→128 widening multiply. Returns low 64 bits; writes high 64 bits to `*hi`. |
| Domain | Any `a, b`. Callers pass `a = magnitude` (∈ [1, INT64_MAX]), `b = kPow10[k]` (k ≤ 18). |
| Range guarantee | For the call sites, product `< 2^123` (proved) → `*hi < 2^59`. Full 128-bit range supported regardless. |
| Errors | None. Total, `noexcept`, no allocation, no throw on any `#if` branch. |
| Selection | `__SIZEOF_INT128__` → `unsigned __int128`; `_MSC_VER && _M_X64` → `_umul128`; `_MSC_VER && _M_ARM64` → `__umulh`+`a*b`; else 32-bit-limb schoolbook. |
| Invariant under test | `hi` MUST be consulted by callers — a caller that ignores `hi` silently narrows to 64 bits (corrected witness: `{99,0}` vs `{9223372036854775807,−18}`, product ≥ 2^64 → `hi ≠ 0`; supersedes the weak `2^63` framing of design-note §6 row 2). |

### `kPow10` (new, TU-local `constexpr`)

```
constexpr std::uint64_t kPow10[19] = { 1, 10, 100, …, 10^18 };   // 152 bytes
```

- `10^18 < 2^63` → fits `uint64_t`. Indexed by `k ∈ [0, 18]` (the `k ≥ 19` guard caps the index before
  any access). Single indexed load; no `10^19..10^38` entries needed (design-note §3 Q3).

## Compare control flow (target state)

```
compare(a, b):
  sentinel filter        →  a/b == INT64_MIN handling            (UNCHANGED :242-254)
  sign filter            →  a_neg != b_neg                        (UNCHANGED :257-262)
  R3 same-exp hoist      →  a.exponent == b.exponent: a.mantissa <=> b.mantissa   (UNCHANGED, merged)
  ── replaces :264..:376 ──
  zero filter            →  {0,e} handling (both/one zero)
  k = |int{ae} - int{be}|;  a_scales = ae > be
  if k >= 19:  mag_cmp = dominance by scaled side              (NO multiply)
  else:        lo = mul_u64_wide(mag_scaled, kPow10[k], &hi)
               scaled_vs_other = (hi!=0 || lo>other) ? greater : (lo==other ? equal : less)
               mag_cmp = a_scales ? scaled_vs_other : invert(...)
  return a_neg ? invert(mag_cmp) : mag_cmp                      (same flip as :371-374)
```

Zero loops, zero divisions, `noexcept`, O(1).

## Test-oracle entities

| Entity | Location | Role |
|---|---|---|
| **Reference comparator** | `tests/core/decimal_compare_diff_oracle_test.cpp` | Verbatim copy of today's digit-string `compare` body; the differential oracle's source of truth. Never changes with the impl. |
| **Deterministic corpus** | same file | Fixed-seed (documented seed) boundary-biased pairs over full domain + out-of-domain int8 exponents. Hard Tier-1 gate. |
| **Witness matrix** | same file | 7 directed rows (corrected `research.md` R5 matrix), each mutation-tested. **Design-note §6 row 2 is SUPERSEDED**: its weak `{…,−17}` / `2^63` witness is replaced by `{99,0}` vs `{9223372036854775807,−18}` (product `99×10^18 = 9.9e19 ≥ 2^64`, `hi = 5 ≠ 0`), which actually kills the drop-`hi` mutant. |
| **Property checks** | same file | Antisymmetry `cmp(a,b) == invert(cmp(b,a))`; transitivity on sampled triples. |
| **Python oracle (extended)** | `tests/oracle/decimal_compare_oracle_test.py` | Existing seam-#8 Python-`Decimal` oracle (seed=42) + new cross-exponent pairs. |
| **Fuzz corpus** | `tests/fuzz/corpus/decimal_compare/` | Committed seed inputs for the differential libFuzzer target. |
| **Forced-portable build** | CMake option/define | Compiles the `#else` limb path on Linux so the oracle covers it. |
