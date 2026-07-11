# Quickstart: v44 all-families typed codegen coverage

## Build with full-family coverage (default)

```bash
cd research/G19-fix-fpml-iso20022/library
# default is FIXPP_CODEGEN_V44_FAMILIES=all — no flag needed
cmake --preset linux-clang-debug
cmake --build build/linux-clang-debug -j2
```

The generated `build/linux-clang-debug/_codegen/include/fixpp/v44/Builders.hpp` now contains `build_<Msg>`/`validate_<Msg>` for all 81 in-scope FIX44 application messages (33 OFFICIAL + ~48 families).

## Opt down to OFFICIAL-only (bound compile cost)

```bash
cmake --preset linux-clang-debug -DFIXPP_CODEGEN_V44_FAMILIES=official
cmake --build build/linux-clang-debug -j2
```

`Builders.hpp` regenerates to exactly the 33 OFFICIAL builders — byte-identical to pre-069 — restoring today's compile cost (~-19 s per Builders.hpp-consuming TU).

## Use a newly-covered typed builder (developer view)

```cpp
#include <fixpp/v44/Builders.hpp>

std::byte buf[4096];
fixpp::v44::TradeCaptureReportArgs args{};
args.trade_report_id = "TRADE-42";
args.exec_type = 'F';
// … populate typed fields / nested groups …
auto bytes = fixpp::v44::build_TradeCaptureReport(buf, args);   // expected_t<span<byte>>
auto rc    = fixpp::v44::validate_TradeCaptureReport(args);     // required-presence + type
```

No runtime, C-ABI, or Python surface changes — this is a compile-time typed convenience over the same wire the runtime path already produces.

## Run the verification

```bash
# Differential round-trip over ALL emitted application builders
ctest --test-dir build/linux-clang-debug -L codegen -R 069 --output-on-failure

# The exemplar-per-family external-golden anchor
ctest --test-dir build/linux-clang-debug -R family_exemplar_golden --output-on-failure

# 33-OFFICIAL byte-identical regression (official mode) + completeness pin
ctest --test-dir build/linux-clang-debug -R 067_emit_builders_unit --output-on-failure
```

## Acceptance quick-checks (map to Success Criteria)

| Check | Command / observation | SC |
|---|---|---|
| All in-scope families have a builder | `grep -c 'build_' …/v44/Builders.hpp` == 81 under `all` | SC-001 |
| Every builder round-trips | `069_all_families_roundtrip` green (0 skips) | SC-002 |
| 33 OFFICIAL unchanged | `official`-mode `Builders.hpp` diff vs pre-069 == empty | SC-003 |
| Mode selection works | `all` → 81 builders, `official` → 33 builders | SC-004 |
| Typed API reachable w/o runtime path | the developer snippet above compiles & runs | SC-005 |
| External oracle green | `family_exemplar_golden` bytes match QuickFIX goldens | SC-006 |
| Enum limitation recorded | `L-069-*` present in `spec/behaviors-and-limitations.md` | SC-007 |

## Verify (Tier-1, before PR)

```bash
# per Article XVII §7/§8 — /speckit-verify runs the serial sanitizer matrix on the
# codegen consumers + differential harness and writes .specify/decisions/069-…-verify.md
```
