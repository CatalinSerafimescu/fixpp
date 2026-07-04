# Phase 0 Research — C1 / int128 decimal compare

All technical unknowns are pre-resolved by the vetted design note
`research/G19-fix-fpml-iso20022/phases/phase-9/perf-investigation/findings/msvc-int128-decimal.md`
(bound proof §2, algorithm §3, MSVC options §4, insertion §5, witness matrix §6, scope §7, amendment
§8, risks §9). This file records the **decisions** and the residual confirm-at-implement obligations.
No NEEDS CLARIFICATION remain.

**Normative references (per `[const §VI.5]`):** this feature introduces **no new OFFICIAL FIX spec
rows**. The governing internal contract is the decimal-compare specification `.specify/2a-decimal.md
§6.3` and `specs/001-core-decimal/research.md D-5` (both amended by this feature, see R6). No FIX
wire/session semantics are touched, so Article VI's `[DocAbbrev §X.Y.Z]` coverage-index obligation is
N/A-justified.

---

## R1 — Replacement algorithm + overflow soundness

- **Decision**: After sentinel + sign filters + the merged R3 same-exponent hoist, decide the
  different-exponent case by: (1) raw-mantissa zero filters; (2) `k = |int{ae} − int{be}|`;
  (3) **`k ≥ 19` → magnitude dominance**, higher-exponent operand ordered by its sign, **no multiply**;
  (4) **`k ≤ 18` → one `mul_u64_wide(mag_hi, kPow10[k], &hi)`** vs the other magnitude via `(hi,lo)`
  two-limb compare; flip for negative sign exactly as the current `:371-374`. Deletes `strip_zeros`,
  `digit_count`/bucket, and the digit-extraction/lexicographic walk → zero loops, zero divisions.
- **Rationale**: Exact integer identity `ua·10^ae = ub·10^be ⟺ ua·10^(ae−emin) = ub·10^(be−emin)`
  reproduces canonical equality (`{100,−2}` = `{1,0}`) without canonicalizing. Soundness by
  construction: `k ≥ 19 ⇒ |m|·10^19 ≥ 10^19 > INT64_MAX ≥ |other|` (dominance, no arithmetic);
  `k ≤ 18 ⇒ |m|·10^k ≤ (2^63−1)·10^18 < 2^122.8 < 2^128` (fits unsigned-128 with >5 bits headroom).
  Comparison runs on magnitudes; sign applied by flip → no signed-128 needed.
- **Guard constant**: keep `k ≥ 19` (simplest dominance proof: `10^19 > INT64_MAX` directly). A
  `19 → 20` mutant is **result-equivalent** (at k=19 the dominance arm and a hypothetical
  `kPow10[19]=10^19`, which fits uint64 since `10^19 < 2^64`, both yield the same "greater"), BUT it is
  **NOT a benign no-kill**: `kPow10` is sized exactly to the guard (19 entries, max valid index 18), so
  under `k ≥ 20` the k=19 case reaches the multiply arm and reads `kPow10[19]` **out of bounds** — an
  ASan-confirmed `global-buffer-overflow`. The guard constant `19` and `kPow10`'s size are therefore
  **jointly load-bearing for memory safety**, and the mutant is **killed via the ASan lane** (T009 kill
  table; measurement superseded this note's original ex-ante "semantically-equivalent no-kill"
  prediction). The other real breaking points are the `hi`-limb consultation and `k = 20` near
  `INT64_MAX` — pinned by directed witnesses (R5, incl. the added k=19 correctness cell).
- **Alternatives considered**: (a) *unguarded common-exponent scale* — the v0.1 design; overflows even
  int128 at full delta (≈189 bits) — **rejected** (this is exactly what Gate A killed). (b) *countl_zero
  + table to cheapen `digit_count` only* — keeps `strip_zeros` + lexicographic loops — **rejected** as
  the target (wide-mul subsumes all three). (c) *smaller slice — replace only the `:329-369`
  lexicographic tail under the Step-3 same-bucket invariant, plain `uint64` suffices, MSVC question
  vanishes* — **retained as a documented fallback** if the contract amendment stalls, not the goal.

## R2 — MSVC portability primitive + `#else` coverage

- **Decision**: one TU-local `static inline std::uint64_t mul_u64_wide(uint64_t a, uint64_t b,
  uint64_t* hi) noexcept`, three-way selection: `#if defined(__SIZEOF_INT128__)` →
  `unsigned __int128` (GCC/Clang/clang-cl); `#elif defined(_MSC_VER) && defined(_M_X64)` → `_umul128`;
  `#elif defined(_MSC_VER) && defined(_M_ARM64)` → `__umulh` + `a*b`; `#else` → portable 32-bit-limb
  schoolbook (4 muls). Detection order puts clang-cl (`_MSC_VER` **and** `__SIZEOF_INT128__`) on the
  builtin path. **No int128 class, no internal-STL header (`__msvc_int128.hpp` rejected), no abseil.**
- **Coverage mechanism (`[const §IX.1]`)**: on Linux/Clang only the `__int128` branch compiles; the
  MSVC `#elif` and portable `#else` are preprocessed out → they neither count against nor are exercised
  by Linux lcov. To *cover* the portable fallback, the new test TU force-compiles it via a build-time
  override macro (e.g. `FIXPP_DECIMAL_FORCE_PORTABLE_MUL`) guarding the `#if`, and runs the differential
  oracle against that build — so the `#else` limb path is proven equivalent on Linux. The MSVC intrinsic
  branches are covered by the Tier-2 `windows-msvc-*` presets running the same oracle.
- **Residual obligation — CONFIRMED at T012 (2026-07-04) against MS Learn `<intrin.h>` docs (msvc-170)**:
  `_umul128(unsigned __int64 Multiplier, unsigned __int64 Multiplicand, unsigned __int64 *HighProduct)`
  → returns low 64, writes high 64 to `*HighProduct`; **Architecture: x64 only**; header `<intrin.h>`.
  `__umulh(unsigned __int64 a, unsigned __int64 b)` → returns high 64; **Architecture: x64, ARM64**;
  header `<intrin.h>`. The shipped `src/core/decimal.cpp` matches EXACTLY: `_umul128` guarded by
  `_M_X64` (`return _umul128(a,b,hi)`), `__umulh` guarded by `_M_ARM64` (`*hi = __umulh(a,b); return a*b;`
  — x64 is already taken by the earlier `_umul128` `#elif`, so ARM64 correctly resolves to `__umulh`).
  **No code adjustment needed.** A wrong `#elif` guess would degrade to the `#else` fallback = a perf
  bug, not a correctness bug, and the conditions are conservative. (Original from-memory note preserved
  below for provenance; historical concern "ARM64 status varies by VS version" resolved: `__umulh` has
  been x64+ARM64 since msvc-140.)
  _Original (pre-confirmation) note:_ the `_umul128` / `__umulh` signatures and their arch availability
  were **from memory** in the design note and had to be confirmed against current Microsoft `<intrin.h>`
  docs before the MSVC branch could be trusted.
- **Rationale/Alternatives**: table in design-note §4 — (a) primitives-behind-one-helper **chosen**;
  (b) MSVC-STL `_Unsigned128` rejected (internal header, breaks on toolset bumps, needs a split anyway);
  (c) hand-rolled `struct int128` rejected (untested surface — compare needs no add/sub/negate);
  (d) abseil rejected (a whole dependency for ~10 instructions, contra minimal-deps posture).

## R3 — Benchmark + baseline protocol

- **Decision**: reuse the existing `BM_decimal_compare` (same-exp, no-regress), `BM_decimal_compare_diff_exp`
  (primary — expected to drop), `BM_decimal_compare_diff_bucket` (no-regress) in `bench/core/decimal_bench.cpp`.
  Gate on **deterministic callgrind instruction count** (single-filter, per the R3 protocol), not WSL2
  wall-clock. Update `bench/baselines/decimal_baseline.json` **in this PR** with a rationale line per
  `[const §VIII.2/§VIII.3]`; same-exp/diff-bucket must stay within ±5%.
- **Rationale**: callgrind Ir is deterministic and CI-stable; WSL2 ns is noisy (`shape-only`, per spec
  NFR-001). The intentional change is the diff-exp win; §VIII.3 forbids merging a perf change without a
  bench in-PR and §VIII.2 requires the in-PR baseline bump.
- **Alternatives**: asserting an absolute ns threshold — **rejected** (WSL2 non-deterministic, would be
  a benchmark hack per §VIII.6).

### §VIII.2 pre-change regression basis (captured at T001, 2026-07-04)

The whole-process `summary:` line at `--benchmark_min_time=1x` is **not** attributable: at 1 iteration
~99.7% of the ~2.49M Ir is dynamic-linker symbol resolution + `--benchmark_filter` regex compilation +
process init, not `compare` (confirmed via `callgrind_annotate` — `compare` does not even appear above
the 99.9% threshold at 1x). The gate must be on the **self-Ir of `compare` per call**, isolated by running
at a high fixed iteration count (`--benchmark_min_time=1000000x`, i.e. exactly 1,000,000 iterations) so
the loop dominates the one-time startup cost, then reading `compare`'s self-Ir from `callgrind_annotate`'s
per-function breakdown (not the whole-process summary):

| Benchmark                        | Ir (`compare`, 1,000,000 calls) | Ir per call |
|-----------------------------------|----------------------------------|-------------|
| `BM_decimal_compare`              | 38,000,000                       | 38          |
| `BM_decimal_compare_diff_exp`     | 205,000,000                      | 205         |
| `BM_decimal_compare_diff_bucket`  | 103,000,000                      | 103         |

Commands (per benchmark, `<name>` = the filter):
```
valgrind --tool=callgrind --trace-children=yes \
  --callgrind-out-file=<out>.%p \
  build/linux-clang-release/bench/core/decimal_bench \
  --benchmark_filter='^<name>$' --benchmark_min_time=1000000x
callgrind_annotate --threshold=99.9 <out>.<pid>   # read the
  # `fixpp::core::decimal_traits<pod_decimal>::compare(...)` row's Ir
```
A repeat run of `BM_decimal_compare` at 1,000,000x measured 46,489,709 total vs 46,489,743 initially
(Δ34, ~0.00007% of the total, all attributable to the fixed ~2.49M one-time startup component diluted
across 1M iterations — negligible relative to the ±5% no-regress bar) — reproducible. Post-change (T005)
comparison against this basis gates `BM_decimal_compare` (no-regress) and `BM_decimal_compare_diff_bucket`
(no-regress) at ±5% on the **per-call self-Ir of `compare`**, and expects `BM_decimal_compare_diff_exp`
(205 Ir/call pre-change, the digit-string slow path) to drop (primary win).

An initial attempt used the whole-process summary at `--benchmark_min_time=1x` (2,490,446 / 2,497,290 /
2,498,490 Ir) — **superseded**, not usable as a regression basis: at 1 call, `compare`'s few dozen
instructions are swamped 65,000:1 by process-init noise, so a ±5% gate against those numbers could never
trip on a real `compare`-path regression. Retained here only as a documented false start, not as the basis.

### §VIII.2 post-change measurement (T018, 2026-07-04)

Rebuilt `bench/core/decimal_bench` from the T005-swapped `src/core/decimal.cpp` on
`build/linux-clang-release` (release preset, `-j2`), then re-ran the identical single-filter
callgrind protocol above (`--trace-children=yes`, `--benchmark_min_time=1000000x`, self-Ir of
`compare` read from `callgrind_annotate --threshold=99.9`):

| Benchmark                        | Pre-change Ir/call (T001) | Post-change Ir/call (T018) | Δ Ir/call | Δ % | Gate | Verdict |
|-----------------------------------|---------------------------|------------------------------|-----------|--------|----------------------------------|---------|
| `BM_decimal_compare`              | 38                        | 29                            | −9        | −23.7% | no regression beyond ±5%         | PASS (improved) |
| `BM_decimal_compare_diff_exp`     | 205                       | 69                            | −136      | −66.3% | **must decrease** (primary win)  | PASS |
| `BM_decimal_compare_diff_bucket`  | 103                       | 66                            | −37       | −35.9% | no regression beyond ±5%         | PASS (improved) |

Commands (identical to the T001 protocol, `<name>` = the filter):
```
valgrind --tool=callgrind --trace-children=yes \
  --callgrind-out-file=<out>.%p \
  build/linux-clang-release/bench/core/decimal_bench \
  --benchmark_filter='^<name>$' --benchmark_min_time=1000000x
callgrind_annotate --threshold=99.9 <out>.<pid>
```
Reproducibility: `BM_decimal_compare` re-run twice gave identical `29,000,000` total Ir at
1,000,000 iterations both times (deterministic, no drift).

**Verdict**: `BM_decimal_compare_diff_exp` Ir decreased as required (the wide-multiply path
replaces the digit-extraction + lexicographic walk). Neither `BM_decimal_compare` nor
`BM_decimal_compare_diff_bucket` regressed — both also decreased. This is explained by the T005
diff (`git diff src/core/decimal.cpp`): the old separate "Step 3: magnitude bucket via
`digit_count`" early-exit (which `BM_decimal_compare_diff_bucket` exercised) was removed along
with the digit-string slow path, so that regime now also flows through the single k-dominance /
`mul_u64_wide` comparator instead of a divide-loop `digit_count` early-exit — cheaper on both
axes. `BM_decimal_compare`'s hot same-exponent branch (`a.exponent == b.exponent` raw check,
unchanged by the T005 diff — confirmed byte-identical via `git diff` context) improved too,
consistent with a whole-TU codegen shift from adding `mul_u64_wide`/`kPow10` to the same
translation unit (not a source change on that path). WSL2 wall-clock ns (`bench/baselines/
decimal_baseline.json`, regenerated in-PR) moved in the same direction (shape-only per NFR-001,
not the gate). Baseline updated in this PR with a `_note` rationale field (precedent:
`bench/baselines/sync/async_mutex_baselines.json`'s `_note`).

## R4 — Differential-oracle test vehicle (from /clarify)

- **Decision (per /clarify 2026-07-04)**: **both** — (a) a **fixed-seed deterministic** C++ differential
  corpus (new `tests/core/decimal_compare_diff_oracle_test.cpp`) holding a verbatim copy of today's
  digit-string comparator as the reference and asserting bit-identical `strong_ordering` over a
  boundary-biased full-domain corpus (mantissa ∈ [INT64_MIN+1, INT64_MAX], exponent ∈ [−38,0] **plus**
  out-of-domain int8) + antisymmetry + transitivity — this is the **hard always-on Tier-1 gate**;
  (b) a **differential libFuzzer target** (new `tests/fuzz/fuzz_decimal_compare.cpp`, `fuzzer,address,undefined`)
  decoding two `pod_decimal`s and asserting `new == reference` (mismatch → `__builtin_trap`), with a
  committed seed corpus — continuous coverage.
- **Plus**: **extend** the existing `tests/oracle/decimal_compare_oracle_test.py` (Python-`Decimal`,
  seed=42, currently canonical-domain pairs) with **cross-exponent pairs** — it already is the seam-#8
  external oracle; augment `_canonical_pairs` rather than build a new one.
- **Rationale**: the deterministic corpus makes SC-001 reproducible on a normal CI run; the fuzzer gives
  randomized depth AND remediates the module's known "fuzz asserts nothing" anti-pattern
  (`fuzz_decimal_parse.cpp:24-28`) for the compare path. This function's history (Gate-B P1 #1
  same-bucket-negatives) earns the full discriminating-witness treatment, not presence-tests.
- **Alternatives**: deterministic-only (loses continuous randomized coverage) and fuzzer-only (loses an
  always-on deterministic gate — SC-001 unprovable on a normal run) — both **rejected** at /clarify.

## R5 — Directed witness matrix + mutation testing

- **Decision**: encode the design-note §6 seven-row matrix as directed GoogleTest cases, each chosen to
  kill a specific mutant, all **mutation-tested** (introduce the mutant, prove the witness fails):
  1. canonicalization-equality (`{100,−2}`={1,0}, negatives, `{1000,−3}`={10,−1}).
  2. **scaled product crosses 2^64** — the *unsigned* hi-limb threshold above which `mul_u64_wide`
     writes a non-zero `hi` — (`{99,0}` vs `{9223372036854775807,−18}`: k=18, scaled
     `99×10^18 = 9.9e19 ≥ 2^64` → `hi = 5 ≠ 0`; both orders/signs) — kills silent int64 narrowing /
     dropped `hi != 0` test. **The soundness-critical witness.** *Correction note:* design-note §6 row 2 /
     §2 / Risk #4 stated `2^63` (the *signed*-int64 overflow point) and the mantissa-`…−17` value, whose
     product `9.9e18 < 2^64` leaves `hi = 0` — so the drop-`hi` mutant **survives** it. Because the
     shipped helper is UNSIGNED, the `hi`-limb consequence only begins at `2^64`; this bundle **supersedes**
     that witness value with the `…−18` pair (product ≥ 2^64) and must not be "restored" to the weaker
     design-note value.
  3. **k-boundary** (k=18 multiply arm, **k=19 dominance-boundary cell** (added post-T009 to make the
     directed matrix discriminate the guard boundary directly rather than relying on the seed-42 corpus
     happening to draw a k=19 pair), k=20 near-INT64_MAX guard arm, k=38 full domain, both scale
     directions/signs) — kills `>=19→>=21/>=40`, `a_scales` swap, table off-by-one. `19→20` is
     **result-equivalent but killed via the ASan lane** (OOB read of `kPow10[19]`; guard + table size
     jointly load-bearing for memory safety — see the Guard-constant note above), NOT a benign no-kill.
  4. zero-filter ordering/sign (`{0,−38}`={0,0}; `{0,−5}`>{−1,0}; `{0,3}`<{1,−38}).
  5. extremes (`±INT64_MAX`, `±(INT64_MIN+1)` at exp 0 and −38, cross-paired) — kills `mag()` negation /
     end-flip omission (the P1 #1 shape).
  6. sentinel pairs (regression, path unchanged).
  7. out-of-domain exponents (`{5,+7}` vs `{5,0}`; delta>38 via int8 extremes) — kills int8-promotion /
     totality regressions.
- **Rationale**: `[feedback_coverage_push_enshrines_bugs]` / `[feedback_witness_asserts_named_postcondition_not_proxy]`
  — positive fixtures can enshrine bugs; each witness must assert the named post-condition and be proven
  to kill its mutant.

## R6 — Contract amendment (the Gate-A-reversal core)

- **Decision**: amend the documents below, **leading with the §2 bound proof** and stating observable
  semantics are UNCHANGED (total `strong_ordering`, canonical equality, sentinel-strictly-greatest). The
  amendment is not just algorithm/contract anchors: because C1 is **reclassified from opt-in low-latency
  MODE to a default-path speed swap**, every text that still frames C1 as opt-in-mode must also carry a
  **dated (2026-07-04) supersession note**, or the bundle self-violates SC-005 after merge. Sites 1–3
  below are inside this library submodule (land in the same library PR); sites 4–5 are in the **parent
  monorepo**, outside this submodule's git tree, so they land as a **separate post-merge parent-repo
  commit** (059 `remaining-work` close-out precedent, `790e6b1` after PR #163's merge bump `f0d4876`) —
  covered by SC-005's own "after merge" verification wording:
  1. `.specify/2a-decimal.md §6.3` — replace "not implemented by scaling to a common exponent in a wider
     integer (the previous `__int128` fallback was unsound…)" with the guarded algorithm + `k≥19`
     dominance + product `< 2^123` proof; retire "No multiplication, no wide-int dependency"; **keep**
     "no MSVC-vs-Clang *algorithm* split" (primitive-level `#if` only).
  2. `specs/001-core-decimal/research.md D-5` — supersession note: v0.1 rejection was of the *unguarded*
     scale; record the bound proof + per-compiler-primitive decision.
  3. `src/core/decimal.cpp` contract comment (`:240-242`) — drop "No `__int128`", cite amended §6.3.
  4. `research/.../perf-investigation/02-lowlatency-recommendations.md` — at **all three C1 framings**:
     the **C1 Tier-C entry** (`:365+`), the **Tier-C preamble caveat** (`:354-362`, the set-level "None
     of them is a default-path change … opt-in low-latency MODE" sentence — carve C1 out), and the
     **"Considered and rejected: `__int128`" bullet** (`:600-611`). A pointer alone is insufficient (it
     does not neutralize the set-level preamble); land a dated supersession note reclassifying C1 as a
     default-path swap.
  5. `remaining-work/perf-and-hardening-findings.md` — at **all three C1 sites**: the Cluster-2 residual
     line (`:62`), the **C1 table row** (`:72`), and the **"Low-latency MODE" list entry** (`:141`).
     **Rewrite** the C1 row to state the default-path reclassification (semantics-preserving speed swap)
     — do **not** merely flip it to DONE, which would leave `:141` still classifying C1 as opt-in-mode.
- **Rationale**: `[const §XVII]` — re-introducing a Gate-A-rejected approach is a contract change; the
  Gate-A bundle must lead with why it is now sound. `[feedback_clarify_reconciled_away_standing_user_decision]`:
  this reversal is a *standing-decision* change — done here with explicit user sign-off (2026-07-04) +
  decision archaeology (the v0.1 rejection reason vs the new guard), not silently.
- **Alternatives**: ship the code without amending — **rejected** (`[const §XVII]` violation; the code
  comment + spec would contradict the implementation).

---

## Open confirm-at-implement obligations (carried to tasks)

1. **MSVC intrinsic signatures** (R2) — confirm `_umul128` / `__umulh` vs current MS `<intrin.h>` docs
   before trusting the MSVC branch; `windows-msvc-*` Tier-2 must run the oracle.
2. **`#else` fallback coverage** (R2) — force-portable build define + oracle run on Linux.
3. **Bench baseline update** (R3) — regenerate `decimal_baseline.json` in-PR with rationale.
