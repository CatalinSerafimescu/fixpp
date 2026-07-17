# Contract: Completeness census + golden set + determinism

**Feature**: 078-precompiled-builder-libs · Phase 1

## Completeness census (VI / FR-009 / R7)

- **Mechanism preserved:** the address-of-everything TUs
  (`&fixpp::<ns>::build_<Msg>` / `validate_<Msg>`,
  `tests/codegen/builder_completeness_common.hpp:32-37`) over the checked-in
  `tests/codegen/generated/<ver>_builder_completeness_entries.def`; exact-set
  equality (no message missing, none extra); mutation witness stays red under a
  dropped entry (`builder_completeness_mutation_witness_test.cpp`).
- **Semantic shift (state it — R7):** with bodies out-of-line, taking the address
  of a **declaration** proves the symbol **links** (resolved from the lib), not
  that its body **compiles**. Compile-proof moves to the `fixpp_builders_<ver>` /
  `fixpp_validators_<ver>` **build target** — if a body failed to compile the lib
  would not build and the census would not link.
- **Required wiring:** each census TU `add_dependencies(...
  fixpp_builders_<ver> fixpp_validators_<ver>)` and links them. This is *why* the
  heavy tests get cheap (SC-006) — declarations compile fast; addresses resolve at
  link.
- **Coverage is not weakened:** VI is `PASS+` — the census plus the new file
  name-set/count determinism (below) together strengthen the no-silent-omission
  guarantee across the split.

## Golden set (FR-010 / R6)

- **From single file → set.** Replace the 077 single-file goldens
  (`specs/077-.../contracts/golden/{v44_Builders_all,v50sp2_Builders,vlatest_Builders}.golden.hpp`
  + `specs/069-.../v44_Builders_official.golden.hpp`) with a **golden set** per
  version under `specs/078-.../contracts/golden/`: `groups.hpp` +
  `messages/<Msg>.{hpp,inl,cpp}` + `all.hpp` (+ validator surface).
- **Regeneration is an explicit task**, not a side effect
  ([[feedback_codegen_golden_exists_narrow_verify_misses_it]]).

## Determinism test (rewritten `tests/codegen/determinism_test.cpp`)

Three assertions over the emitted **set**:
1. **Byte-stable content** — run the tool twice; every file in the set is
   byte-identical run-to-run.
2. **Stable file NAME set + COUNT** — the set of emitted relative paths and their
   count is invariant; a dropped/renamed message is a distinct failure (a
   content-only diff would miss it — the split multiplies files).
3. **Set == golden** — the generated set matches the checked-in golden set.

Backstops (unchanged): `codegen-build-graph-check` (`git status --porcelain`
clean, `RESOURCE_LOCK codegen_source_tree`) and `codegen-source-staleness-check`
(`RUN_SERIAL`).

## Byte-identity in both modes (SC-004 / FR-009)

- Round-trip tests assert the wire bytes for every message equal the 077 baseline,
  exercised **both** via the linked lib and via header-only inline mode (the `.inl`
  body and the `.cpp` body are the same generation at different linkage).

## `nm` symbol witnesses (SC-002 / SC-003 — new tests)

- **SC-002 (link-only):** a binary that calls a subset of `build_<Msg>` and links
  `fixpp_builders_<ver>` → `nm` shows only the called `build_` symbols (per-message
  `.o` granularity), not the whole version's set.
- **SC-003 (zero validator):** a builder-only binary (links `fixpp_builders_<ver>`
  only) → `nm` shows **zero** `validate_<Msg>` / `writer_traits` symbols.
- Both seed from the **R2a ODR/SC-005 probe** (a Phase-0 blocking prerequisite),
  which also proves force-inlined + linked validators sharing a plan don't collide
  (FR-007).
