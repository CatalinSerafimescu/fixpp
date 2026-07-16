# 076 codegen golden — inventory contract

This directory holds the checked-in codegen golden that the feature's
**determinism** gate (V-4, T017) and **additive OFF-path / ON-path byte-diff**
gate (V-7, T018) diff the emitter's output against. Goldens live under
`specs/<id>/contracts/golden/` (cf. `specs/003-dictionary-codegen/contracts/golden/`
= `*_Messages.golden.hpp` per tier; there is **no** `tools/codegen/golden/`).

## Inventory (this feature's `fixpp::vlatest` read tier)

Following the merged determinism-golden precedent, the **anchor is `Messages.hpp`
only** per tier (003 goldens `Fields`/`Validator`/`Reify` for *no* tier — run-to-run
determinism `AC-T1` already self-compares every generated file). This feature adds:

- `vlatest_Messages.golden.hpp` — the shipped read/reify class surface for all 181.

The census **`Manifest.txt` is intentionally NOT goldened here**: its *correctness* is
pinned by V-1 (`manifest ≡ raw-XML`) and V-1b (`manifest ≡ shipped class`) — strictly
stronger than a byte snapshot — and its *determinism* by `AC-T1` (which walks every
generated file, `Manifest.txt` included). An 11.5 MB frozen copy would be pure
redundancy.

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
  under the FULL ctest run (a stale/non-deterministic emit fails CI); run-to-run
  byte-stability of the whole `vlatest/` tier (incl. `Manifest.txt`) is `AC-T1`.
- **V-7 additive** (T018): OFF job (`FIXPP_CODEGEN_FIX_LATEST=OFF`) → legacy
  `v42/v44/v50sp2/vt11` + `_dispatch/` byte-identical to the existing 003/069
  goldens, `vlatest/` **absent** + no `fixpp::vlatest` target/include; ON job →
  same legacy artifacts byte-identical while `vlatest/` is present and matches the
  goldens here.
