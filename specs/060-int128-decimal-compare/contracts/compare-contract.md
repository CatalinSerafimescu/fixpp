# Contract — `decimal_traits<pod_decimal>::compare` (observable behavior UNCHANGED)

This feature changes the **implementation** of `compare`, not its **contract**. The observable contract
below is the invariant the differential oracle enforces bit-for-bit.

## Signature (frozen)

```cpp
static std::strong_ordering
decimal_traits<pod_decimal>::compare(pod_decimal const& a, pod_decimal const& b) noexcept;
```

- Header `include/fixpp/core/decimal.hpp` — **unchanged**. C-ABI `_checked` wrappers — **unchanged**.
- `noexcept`, zero allocation, O(1), no loops/divisions on the new path.

## Observable behavior (invariant — MUST match the retained digit-string reference for every input)

1. **Total `std::strong_ordering`** over all inputs including the invalid sentinel and out-of-canonical
   `int8` exponents.
2. **Sentinel** (`mantissa == INT64_MIN`) orders **strictly greatest**; two sentinels are `equal`.
3. **Sign**: negative < positive; mixed-sign decided by sign alone.
4. **Magnitude/equality is exact decimal ordering**: `ua·10^ae` vs `ub·10^be` compared exactly, incl.
   canonical equality without canonicalizing (`{100,−2}` == `{1,0}`; `{1000,−3}` == `{10,−1}`).
5. **Zero**: `{0, e}` is value 0 for any `e`.
6. **Antisymmetry**: `compare(a,b) == invert(compare(b,a))`. **Transitivity** holds on triples.
7. **Determinism**: same inputs → same result, on every supported toolchain.

## What MUST NOT change (verified at `/speckit-verify`)

- No public API / C-ABI / wire / error-enum / struct-layout change (abidiff clean; decimal PoD frozen
  per `[const §X.3]`).
- The sentinel filter, sign filter, and merged R3 same-exponent hoist keep their exact current behavior.

## Contract-amendment checklist (FR-009 / SC-005 — the Gate-A-reversal core)

The `no-__int128` decision is reversed; the amendment touches 5 sites split by repository. Sites 1–3
are inside this library submodule and MUST land in the **same (library) PR**. Sites 4–5 are in the
**parent monorepo** (outside this submodule's git tree) and therefore land as a **separate, post-merge
parent-repo commit** — same convention as the 059 `remaining-work` close-out (`790e6b1`). Each site
leads with the overflow bound proof and states semantics are unchanged:

- [ ] `.specify/2a-decimal.md §6.3` — guarded algorithm + `k≥19` dominance + product `< 2^123` proof;
      retire "No multiplication, no wide-int dependency"; keep "no MSVC-vs-Clang **algorithm** split".
- [ ] `specs/001-core-decimal/research.md D-5` — supersession note (v0.1 rejection was of the *unguarded*
      scale; record bound proof + per-compiler-primitive decision).
- [ ] `src/core/decimal.cpp` contract comment (`:240-242`) — drop "No `__int128`", cite amended §6.3.
- [ ] **(parent-repo, post-merge)** `research/G19-fix-fpml-iso20022/phases/phase-9/perf-investigation/02-lowlatency-recommendations.md`
      — dated (2026-07-04) supersession note reclassifying C1 as a **default-path** swap at **all three**
      C1 framings, not just a cross-ref (a pointer does not neutralize the set-level preamble):
  - [ ] the **C1 Tier-C entry** (`:365+`);
  - [ ] the **Tier-C preamble caveat** (`:354-362`) — carve C1 out of the "None of them is a default-path
        change … opt-in low-latency MODE" set-level sentence;
  - [ ] the **"Considered and rejected: `__int128`" bullet** (`:600-611`).
- [ ] **(parent-repo, post-merge)** `remaining-work/perf-and-hardening-findings.md` — reclassify C1 as
      default-path at **all three** sites (post-merge close-out, per the CLEANUP-phase checklist); update
      `[[project_decimal_cluster2_fixes]]` / catalogue:
  - [ ] the Cluster-2 residual line (`:62`);
  - [ ] the **C1 table row** (`:72`) — **rewrite** to state default-path reclassification, do **not**
        merely flip to DONE;
  - [ ] the **"Low-latency MODE" list entry** (`:141`) — remove C1 from the opt-in-mode bucket.

## Verification surface (contract → gate)

| Contract clause | Gate |
|---|---|
| 1–7 above | deterministic differential corpus (Tier-1) + extended Python oracle + differential libFuzzer |
| soundness (no overflow/narrowing/UB) | UBSan Tier-1 + witness row 2 (`hi`-limb) + witness row 3 (k-boundary), mutation-tested |
| MSVC / portable parity | Tier-2 `windows-msvc-*` oracle run + forced-portable `#else` oracle run on Linux |
| no surface change | abidiff (`[const §IX.5]`) + header-untouched check |
| perf (no regression, intended win) | callgrind Ir on `BM_decimal_compare*` + `bench/baselines/decimal_baseline.json` update |
