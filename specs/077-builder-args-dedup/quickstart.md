# Quickstart / Validation: builder-args-dedup

**Feature**: 077-builder-args-dedup

Runnable checks that prove the feature end-to-end. Run from the library
submodule root. Preset `linux-clang-debug` unless noted.

## Prerequisites

```bash
cd research/G19-fix-fpml-iso20022/library
cmake --preset linux-clang-debug            # runs codegen (all versions + vlatest builders)
```

## V1 — FIX Latest builders exist, deduped, and compile as one TU (US1 / SC-001/002)

```bash
# Emitted and deduped (each plan once; read-tier order, not 137 MB):
ls -la build/linux-clang-debug/_codegen/include/fixpp/vlatest/Builders.hpp
grep -c '^  struct G_' build/linux-clang-debug/_codegen/include/fixpp/vlatest/Builders.hpp
#   expect ~578 shared group structs (NOT ~26k); file ~10 MB order

# Compiles as a single TU within normal RSS (was >21 GB / OOM):
/usr/bin/time -v clang++ -std=c++23 -fsyntax-only \
  -I build/linux-clang-debug/_codegen/include -I include \
  -include fixpp/vlatest/Builders.hpp -xc++ /dev/null
#   expect exit 0, peak RSS low single-digit GB
```

## V2 — Round-trip a deep-group FIX Latest message (US1 acceptance 3)

Build a message carrying Instrument + Legs + Underlyings via `build_<Msg>`, read
it back field-for-field, assert equality (ctest target added at `/implement`):

```bash
ctest --preset linux-clang-debug -R vlatest_builder_roundtrip
```

## V3 — All application-bearing versions emit builders; vt11 does not (US2)

```bash
for v in v42 v44 v50sp2 vlatest; do
  echo "$v: $(ls build/linux-clang-debug/_codegen/include/fixpp/$v/Builders.hpp 2>/dev/null || echo MISSING)"
done
# vt11 must have NO Builders.hpp (admin-only):
test ! -e build/linux-clang-debug/_codegen/include/fixpp/vt11/Builders.hpp && echo "vt11 OK (no builders)"
```

## V4 — v44 behavior unchanged after dedup (US3 / SC-004)

```bash
# Regenerated golden matches fresh output (both families):
ctest --preset linux-clang-debug -R 'codegen_069|v44_builder'
# Determinism (generate twice, byte-compare):
ctest --preset linux-clang-debug -R codegen_determinism_test
```

## V5 — Legacy read tiers byte-identical (SC-005)

```bash
# Read-tier headers must be unchanged vs pre-feature (this feature touches only the builder emitter):
ctest --preset linux-clang-debug -R 'codegen_determinism_test'
# plus the Messages/Fields/Validator/Reify goldens for v42/v44/v50sp2/vt11 (0 diffs)
```

## V6 — Completeness census, exact-set + red-provable (US4 / SC-006)

```bash
ctest --preset linux-clang-debug -R builder_completeness
# Prove it can fail: drop one in-scope message in the emitter, re-run → RED (documented in the test).
```

## Full gate before Gate B

```bash
ctest --preset linux-clang-debug -L "codegen|dictionary|integration"
# plus gcc-release + MSVC are CI-only (local /speckit-verify is clang-only).
```
