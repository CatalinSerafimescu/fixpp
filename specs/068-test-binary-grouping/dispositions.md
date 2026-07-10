# Disposition Ledger — 068-test-binary-grouping

The FR-011 audit trail: every test `.cpp` in every processed module is recorded
here as `grouped:<bucket>` or `standalone:<reason>`. Per-module the sum of rows
MUST equal the module's `.cpp` count.

## Census signal-set (FR-002 / Research §D3) — forces **standalone**

A `.cpp` stays standalone if it matches any of these (classify by **mechanism**,
not filename):

- **allocation-counting** — in-TU global-`operator new` counter, `mallocnesia`
  `LD_PRELOAD` gate, `alloc_guard`. Grep: `operator new` / `set_new_handler` /
  global `alloc_count`. *(A **local** `std::pmr::memory_resource` subclass with a
  per-instance counter passed explicitly is isolation-safe → groupable.)*
- **OOM-injection via global mechanism** — process-wide new-handler / injection
  toggle. *(Local failing-`pmr` passed explicitly → groupable.)*
- **TSan-specific target** / any test carrying a **heterogeneous** per-test
  `ENVIRONMENT` / `TSAN_OPTIONS` / suppression file. *(A homogeneous
  `ENVIRONMENT` shared by the whole bucket may ride the grouped binary — D3.)*
- **top-level `abort()`/`_exit()`** death (NOT gtest fork-based `EXPECT_DEATH`,
  which groups — D3), or link-mode override.
- **genuinely-concurrent / global-singleton-freshness** — spawns
  `std::thread`/`std::jthread`/`std::async`, or mutates a function-local
  `static`/process-global registry read by other `TEST`s. No reliable grep;
  manual-review flag.
- **per-target `target_compile_definitions` variants** of one `.cpp` (e.g.
  `_wide` vs `_portable`).
- **exact-set completeness gate** with a precise `-L` feature label.
- **`ctest -R <target-name>` by name** — see the `-R` policy below.
- **label-heterogeneous** — the sole groupable member of its label class (a
  bucket-of-one yields no disk win → standalone; D4 label-homogeneity).

**Bucket key (D4):** partition the groupable set by `(sorted link-libs, sorted
labels)`; each partition → one `gtest_discover_tests` binary.

## `-R`-by-name policy (SC-004 / Scenario-3 reconciliation)

Grouping renames ctest entries from the target name (`dictionary_lookup_test`)
to per-case `Suite.Case` (`DictionaryLookupFixture.…`). Every documented
`ctest -R <target-name>` / `-R '^<module>_'` / `-R '<module>|…'` idiom in a
**merged-feature** quickstart/tasks doc therefore stops resolving. A literal
reading of SC-004 ("every `-R` resolves to the same set") is **unachievable
under any grouping** — a module-prefix idiom like `-R '^dictionary_'` can only
be satisfied by grouping nothing.

**Policy (user decision 2026-07-10):** preserve every `ctest -L <label>`
selection (labels re-applied at case granularity via `gtest_discover_tests
PROPERTIES LABELS`); for each historical `-R`-by-name idiom that grouping
breaks, record the **equivalent `-L` replacement** here — the "equivalent
selection documented as its replacement" branch of Scenario-3. A test is kept
standalone for `-R` reasons only when a *live* procedure (active tooling, not a
merged-feature snapshot) selects it by exact target name.

---

## Module: `dictionary` (25 `.cpp`) — PILOT (US1)

### Grouped

**Bucket `dictionary_pure_tests`** — 15 `.cpp`, label `dictionary`, link
`fixpp_dictionary` + `pugixml::pugixml` (union for `round_trip` /
`reused_tag_census` raw-XML scans) + gtest:

| `.cpp` | decision | odr_action |
|---|---|---|
| `ref_shape_test` | grouped:pure | none |
| `xml_loader_test` | grouped:pure | none |
| `round_trip_test` | grouped:pure (needs pugixml) | none |
| `determinism_test` | grouped:pure | none |
| `negative_paths_test` | grouped:pure | none |
| `parser_error_test` | grouped:pure | none |
| `lookup_test` | grouped:pure | none |
| `version_profile_test` | grouped:pure | none |
| `field_traits_test` | grouped:pure | none |
| `version_registry_test` | grouped:pure | none |
| `table_view_test` | grouped:pure | none |
| `defect_a_group_context_test` | grouped:pure | none |
| `reused_tag_census_test` | grouped:pure (needs pugixml) | none |
| `collision_membership_guards_test` | grouped:pure | none |
| `oom_injection_test` | grouped:pure (local failing-`pmr`, no global) | none |

**Bucket `dictionary_reify_tests`** — 4 `.cpp`, label `dictionary`, link
`fixpp_dictionary` + gtest, include `_codegen/include`, depends
`fixpp_codegen_generate`:

| `.cpp` | decision | odr_action |
|---|---|---|
| `reify_test` | grouped:reify | none (helpers in anon-ns) |
| `reify_move_test` | grouped:reify | none |
| `reify_dispatch_test` | grouped:reify (local null-`pmr` OOM) | none (helpers in anon-ns) |
| `reify_oom_test` | grouped:reify (local failing/counting-`pmr`) | none (helpers in anon-ns) |

### Standalone (6)

| `.cpp` | reason |
|---|---|
| `pmr_allocation_test` | allocation-counting (in-TU global `operator new`) |
| `concurrent_readers_test` | genuinely concurrent (`std::thread`) |
| `group_context_lookup_alloc_gate_test` | alloc gate + `mallocnesia` add_test keyed by `$<TARGET_FILE:…>` (live name-selection) |
| `reify_cross_strand_test` | TSan target (`LABELS dictionary;tsan`) + threads |
| `reify_membership_identity_test` | label-heterogeneous (`066;dictionary;us1`) — sole groupable member of its label class → standalone (D4) |
| `reify_membership_copy_oom_test` | allocation-counting (global `operator new`) + `066;dictionary;us1` |

**Sum:** 15 + 4 grouped + 6 standalone = **25** ✓ (100% dispositioned).

### `-R` replacements documented (SC-004 / Scenario-3)

| historical idiom (doc) | breaks under grouping? | equivalent replacement |
|---|---|---|
| `-R '^dictionary_'` (002 quickstart:27) | yes (prefix — breaks under any grouping) | `-L dictionary` |
| `-R 'dictionary\|wire\|codegen\|…'` (063 quickstart:20, tasks:26) | yes (lowercase substring vs `Dictionary…` suites) | `-L dictionary` (+ existing `-L`/`-R` for wire/codegen) |
| `-R 'dictionary_(lookup\|negative\|xml_loader)'` (064 quickstart:28, tasks:97) | yes | `-L dictionary` |
| `-R determinism_test` (003 quickstart:78) | yes (`Determinism.*`) | `-L dictionary` (or `-R Determinism`) |
| `-R reify_dispatch` (057 quickstart:42) | yes (`ReifyDispatch*`) | `-L dictionary` (or `-R ReifyDispatch`) |
| `-R 'determinism\|build_graph'` (057 quickstart:50, tasks:97) | determinism yes; `build_graph` is a codegen-module test (unaffected) | `-L dictionary` for determinism |
| `-R '^dictionary_concurrent_readers_test$'` (002 quickstart:78) | **no** — stays standalone, name preserved | — (unchanged) |
