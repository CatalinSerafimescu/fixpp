# 076 codegen golden — inventory contract

This directory holds the checked-in codegen golden that the feature's
**determinism** gate (V-4, T017) and **additive OFF-path / ON-path byte-diff**
gate (V-7, T018) diff the emitter's output against. Goldens live under
`specs/<id>/contracts/golden/` (cf. `specs/003-dictionary-codegen/contracts/golden/`
= `*_Messages.golden.hpp` per tier; there is **no** `tools/codegen/golden/`).

## Inventory (this feature's `fixpp::vlatest` read tier)

Following the merged determinism-golden precedent, the **anchor is `Messages.hpp`
only** per tier (003 goldens `Fields`/`Validator`/`Reify` for *no* tier). This feature
adds:

- `vlatest_Messages.golden.hpp` — the shipped read/reify class surface for all 181.

The census **`Manifest.txt` is intentionally NOT goldened here**: its *correctness* is
pinned by V-1 (`manifest ≡ raw-XML`) and V-1b (`manifest ≡ shipped class`) — strictly
stronger than a byte snapshot. An 11.5 MB frozen copy would be pure redundancy.

**Correction (gate-b/r2):** `Manifest.txt`'s (and the other non-`Messages.hpp` vlatest
artifacts') run-to-run *determinism* is **not** independently pinned by `AC-T1`
(`DeterminismTest.ByteIdenticalAcrossRuns`) — that test drives two runs via
`run_codegen()`, which invokes the tool over the 4 legacy XMLs only and never the
Orchestra XML, so it does not exercise `vlatest/` at all. `Manifest.txt` and its
siblings (`Fields.hpp`/`Validator.hpp`/`Reify.hpp`/`NormativeReferences.md`) share the
same deterministic emitter code path as the legacy tiers (no randomness/timestamps)
but have no dedicated two-run byte-identity test today — a known, low-risk asymmetry
vs. the legacy tiers, not a correctness gap. See
`specs/076-fix-latest-typed-codegen/contracts/build-and-verification.md` V-4 for the
full accounting.

**No `Builders.hpp` golden** — the `fixpp::vlatest` typed builder tier was **descoped
2026-07-16** (137 MB uncompilable header; see spec.md Clarifications → Session
2026-07-16). `emit_builders` stays v44-only, so the v44 golden
(`specs/069-v44-all-families/contracts/golden/v44_Builders_official.golden.hpp`) and the
`specs/003-dictionary-codegen/contracts/golden/` legacy Messages goldens are the
**unchanged** V-7 OFF-path baseline for the legacy tiers.

## CRLF safety
All `*.golden.hpp` / `*.golden.txt` here are byte-compared (LF-generated). A
`.gitattributes` in this directory pins them `-text` so a Windows checkout cannot
CRLF-rewrite them and false-break the byte-exact gate (the 074/075 precedent:
`OrchestraFIXLatest.xml`/`FIX44.xml` both broke this way — a local rsync-MSVC
sandbox false-greens it; only real CI Windows catches it).

## Related gates
- **V-4 determinism** (T017): `tests/codegen/determinism_test.cpp` — the freshly
  generated `vlatest/Messages.hpp` is byte-identical to `vlatest_Messages.golden.hpp`,
  under the FULL ctest run (a stale/non-deterministic emit fails CI). Run-to-run
  byte-stability (`AC-T1`) as currently implemented (`ByteIdenticalAcrossRuns`)
  covers the **legacy** tiers only, not `vlatest/` — see the Correction note above.
- **V-7 additive** (T018): OFF job (`FIXPP_CODEGEN_FIX_LATEST=OFF`) → legacy
  `v42/v44/v50sp2/vt11` `Messages.hpp` byte-identical to the existing 003/069
  goldens, `vlatest/` **absent** (there is no separate `fixpp::vlatest` CMake
  target to begin with — see `build-and-verification.md` V-7); ON job → same
  legacy artifacts byte-identical while `vlatest/Messages.hpp` matches the golden
  here. Additivity of every other legacy/`_dispatch/` file (no golden of its own)
  is proven by the OFF-vs-ON relative byte-diff walk in `AdditiveOffOnByteDiff`.
