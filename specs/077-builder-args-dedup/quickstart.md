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
#   expect 576 shared group structs (NOT ~26k); file ~78 MB

# Compile-resource CONTRACT (not a manual snippet): a named compile-time bench,
# modelled on 003's T046 compile-time bench, captures peak RSS + wall time for a
# TU that -fsyntax-only includes vlatest/Builders.hpp and asserts a threshold
# (peak RSS low single-digit GB, was >21 GB / OOM):
ctest --preset linux-clang-debug -L compile-budget
#   (labels are set at /implement as e.g. "077;builder-dedup;compile-budget";
#    `ctest -L` is a REGEX over individual labels, so select by one discriminating
#    token — NOT a ';'-joined list, which matches a literal label and selects nothing.)
#   The v50sp2/vlatest builder TUs are expected to exceed the ≤3 s single-version
#   syntax-only ceiling and are recorded as KNOWN_OVERAGE in the feature's
#   .specify/decisions verify record — the SAME decision-record convention 003
#   used for the v50sp2 read tier (NOT an Article VIII mechanism; Art VIII is
#   runtime-only). Runtime hot path is untouched.
```

## V2 — Round-trip a deep-group FIX Latest message (US1 acceptance 3)

Build a message carrying Instrument + Legs + Underlyings via `build_<Msg>`, read
it back field-for-field, assert equality (ctest target labelled at `/implement`):

```bash
ctest --preset linux-clang-debug -L builder-roundtrip
```

## V3 — All application-bearing versions emit builders; vt11 does not; OFF leaves no stale file (US2 / FR-012)

```bash
for v in v42 v44 v50sp2 vlatest; do
  echo "$v: $(ls build/linux-clang-debug/_codegen/include/fixpp/$v/Builders.hpp 2>/dev/null || echo MISSING)"
done
# vt11 must have NO Builders.hpp (admin-only, empty-skip):
test ! -e build/linux-clang-debug/_codegen/include/fixpp/vt11/Builders.hpp && echo "vt11 OK (no builders)"

# OFF-path stale-file check (N4 / FR-012): after a prior ON build, reconfigure
# with the option OFF and confirm the regen-guard CLEANED the vlatest header
# (076's unconditional delete is gone; the OFF path must still leave nothing):
cmake --preset linux-clang-debug -DFIXPP_CODEGEN_FIX_LATEST=OFF
test ! -e build/linux-clang-debug/_codegen/include/fixpp/vlatest/Builders.hpp \
  && echo "OFF OK (no stale vlatest/Builders.hpp)"
```

## V4 — v44 behavior unchanged after dedup (US3 / SC-004)

```bash
# Frozen pre-077 external-byte differential (FR-008): build_<Msg> bytes vs the
# QuickFIX-authored .fix goldens + 061 hand exemplars — extended past the 5
# exemplars of test_067_builder_shape_oracle.cpp to every distinct structural
# plan / multi-plan no_tag (NoLegs/555, NoOrders/73, NoRelatedSym/146), or ≥ all 83.
# (Round-trip alone is NOT a differential — reads scan by tag.)
ctest --preset linux-clang-debug -L v44-byte-invariance
# Regenerated golden + validation suite (both families) and determinism:
ctest --preset linux-clang-debug -L v44-builder
```

## V5 — Legacy read tiers byte-identical (SC-005)

```bash
# Recursive byte-diff of every generated legacy read artifact (Messages/Fields/
# Validator/Reify for v42/v44/v50sp2/vt11) against a pre-077 baseline — NOT
# codegen_determinism_test alone (proves run-to-run stability, not pre-vs-post
# identity) and NOT only the Messages golden (Fields/Validator/Reify are ungoldened).
ctest --preset linux-clang-debug -L read-tier-byte-diff
# If narrowed to the goldened Messages artifact (FR-009), the ungoldened-tier
# residual is recorded in the verify record.
```

## V6 — Completeness census, exact-set + red-provable (US4 / SC-006)

```bash
ctest --preset linux-clang-debug -L builder-completeness
# Prove it can fail via the COMMITTED mutation seam (builder-completeness.md C3b):
ctest --preset linux-clang-debug -R builder_completeness_mutation_witness
#   the committed test-only drop mechanism (FIXPP_CODEGEN_DROP_BUILDER_MSGTYPE) removes
#   one in-scope message → census goes RED. Expected set is derived from a raw-XML
#   / Orchestra walk INDEPENDENT of emit_builders (builder-completeness.md C1).
```

## Full gate before Gate B

```bash
ctest --preset linux-clang-debug -L "codegen|dictionary|integration"
# (label expression; the mutation-witness run above is the only justified -R.)
# plus gcc-release + MSVC are CI-only (local /speckit-verify is clang-only).
```
