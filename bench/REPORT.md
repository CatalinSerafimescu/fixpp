# Wire Bench Report — 004-wire-codec T048/T052

## Latency measurements (linux-clang-debug, NDEBUG off, 2026-05-16)

Debug timings are ~10–25x release; the [2b §6.6] ceilings apply to linux-clang-release+warm-cache.
The ±5% regression gate (CI) fires on linux-clang-release.

| Bench | Ceiling [2b §6.6] | Debug measured | Ceiling sourced? |
|---|---|---|---|
| BM_Framer_Feed_NoCarry | ≤ 30 ns | ~749 ns | YES — from [2b §6.6] table row 1 |
| BM_Parser_Iter_20tag | ≤ 80 ns | ~27 ns | YES — from [2b §6.6] table row 2 |
| BM_Parser_Index_20tag | ≤ 400 ns | ~5877 ns | YES — from [2b §6.6] table row 3 |
| BM_Parser_Index_200tag | ≤ 4 µs | ~43295 ns | YES — from [2b §6.6] table row 4 |
| BM_OffsetTable_Find_32slot | ≤ 15 ns | ~34 ns | YES — from [2b §6.6] table row 5 |
| BM_Writer_Commit_20tag | ≤ 80 ns | ~1856 ns | YES — from [2b §6.6] table row 7 |
| BM_Writer_Commit_200tag | ≤ 800 ns | ~17500 ns | YES — from [2b §6.6] table row 8 |
| Validator::validate | ≤ 200 ns | — | YES — ceiling exists; bench deferred with US4 |

## D-8 — check_alive() cost (T052, linux-clang-debug)

| Bench | Total 200 calls (ns) | Per-access (ns) |
|---|---|---|
| BM_CheckAlive_200tag (debug, real slot lookup) | 1148.9 | ~5.74 |
| BM_Bytes_200tag (baseline, no generation check) | 852.5 | ~4.26 |
| Ratio | **1.35×** | |

Verdict: 1.35× < 2× threshold → per-N-access sampling fallback NOT triggered.
Release cost: analytically zero (constexpr empty body).

## D-14 — hffix comparison

hffix comparison NOT wired — D-14 says measured-not-blocker.
hffix is absent from the Conan package graph. Wire in hffix before v1.0 RC
per [const §VIII.4] and add BM_HffixParser_20tag to parser_bench.cpp.

## US4 PAUSED — validator_bench omitted

validator_bench.cpp intentionally absent. US4 PAUSED (dict::table_view undefined).
Recorded as "deferred with US4" in all deliverables.

## PR68-08 — Release baseline follow-up (gate-b/r1, 2026-05-17)

Triage finding `opus_pr68_1_triage.md` PR68-08 (P2, waived): the [2b §6.6] ±5%
regression gate is explicitly CI/linux-clang-release-scoped; this REPORT is honest
that all timings above are debug-only and do NOT claim release compliance.

**Tracked follow-up:** when `linux-clang-release` CI lands (currently blocked by
`CMAKE_MAKE_PROGRAM`/Ninja preset gap in the GCC-release profile), re-run the
bench suite under `linux-clang-release`, record the release baselines here, and
commit the baseline JSON `_note` as "release-baseline committed". The ±5%
regression gate fires on linux-clang-release CI; this follow-up closes the
PR68-08 waiver.

The `Validator::validate` ceiling (≤ 200 ns, [2b §6.6] table row for validator)
is still debug-only deferred; it must be wired into `validator_bench.cpp` alongside
the release-baseline commit. US4 is now landed (`dictionary_driven_validator`
complete) so the only remaining blocker is the release-CI infra gap.
