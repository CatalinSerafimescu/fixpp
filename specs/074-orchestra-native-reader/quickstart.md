# Quickstart / Validation Guide: Native Orchestra Reader (FIX Latest)

How to build and prove the feature end-to-end. Details of the interface and mapping live in [contracts/orchestra_loader.md](./contracts/orchestra_loader.md) and [data-model.md](./data-model.md); this is the run guide.

## Prerequisites

- The library submodule build toolchain (Conan + CMake ≥ 3.28 + Ninja), same as `fixpp_dictionary`.
- The vendored source present at `dictionaries/orchestra/OrchestraFIXLatest.xml` (pinned `FIXTradingCommunity/orchestrations @ 236d4a405…`, EP303). The sha1 `26f60db1c1f52d169d3b6825ac68800abf487fde` is the spike's grade-1 recorded sha1 of the **OFFICIAL** file (spike-and-plan doc line 36, NOT the relabelled `OrchestraFIXLatest_relabeled.xml`) — a supply-chain integrity pin. Fetching the file is the gating first implementation task (requires network); **compute** its sha1 from the fetched bytes and **assert it equals this pinned value** (mismatch → STOP and investigate, do not proceed) — do not silently drop the check.
- pugixml already resolved (`pugixml/1.15`, PRIVATE dep of `fixpp_dictionary`).

## Build

```bash
cd research/G19-fix-fpml-iso20022/library
# configure + build the dictionary module for the local clang-debug preset
cmake --preset linux-clang-debug
cmake --build --preset linux-clang-debug --target fixpp_dictionary dictionary_orchestra_tests
```

## Validation scenarios

Each maps to a Success Criterion; all live in `tests/dictionary/orchestra_loader_test.cpp` (grouped bucket, label `orchestra`).

The whole bucket runs via its label; per-scenario narrowing is a **binary gtest filter argument** (Article VII §8 — buckets are selected by `-L <label>`, never `ctest -R <exe/case>`). Bucket run: `ctest --preset linux-clang-debug -L orchestra`. Locate the binary once: ``ORCH_BIN=$(find build -name dictionary_orchestra_tests -type f)``.

| # | Scenario | Command | Expected |
|---|---|---|---|
| 1 | **Load 181 messages** (SC-001) | `$ORCH_BIN --gtest_filter=OrchestraLoader.Load181` | `messages().size() == 181`, `which_session_version() == vlatest` |
| 2 | **Group resolution** (SC-003) | `$ORCH_BIN --gtest_filter=OrchestraLoader.Groups` | depth-7 `MassQuoteAck` resolves the **full parent path** `296→295→555→40241→41686→41680→41683` (asserted, not just non-empty); reused tag 555 resolves non-empty under multiple parents via context key |
| 3 | **Codeset values + descriptions** (FR-002) | `$ORCH_BIN --gtest_filter=OrchestraCodesets.PreservesValuesAndDescriptions` | a known EP303 codeset field: **both** enum value bytes AND description text survive |
| 4 | **Fail-closed on unknown datatype, proven RED** (SC-002) | `$ORCH_BIN --gtest_filter='OrchestraFailClosed.*'` | field that **uses** `type="<unknown>"` throws `orchestra_parse_error`; **unused** unknown `<fixr:datatype>` decl does NOT fail; `unionDataType` with an unknown **primary/base** arm still throws; valid EP303 (zero unknown tokens) does NOT throw |
| 5 | **Distinct version identity** (SC-005) | `$ORCH_BIN --gtest_filter=OrchestraVersionIdentity.Distinct` | `vlatest` distinct at the `session_version` layer; `session_to_application(vlatest) == v50sp2`; no `FIX.5.0SP2` relabel |
| 6 | **Registry fail-loud guard** (FR-010) | `ctest --preset linux-clang-debug -L orchestra` (runs the guard test) | co-registering a FIX50SP2 + a FIX Latest dict fires the fail-loud guard (abort/error), never silent last-writer-wins |
| 7 | **Legacy no-regression** (SC-006) | `ctest --preset linux-clang-debug -L dictionary` | all nine QuickFIX dicts load through `XmlLoader` with unchanged message counts + group queries |
| 8 | **Downstream read-path unchanged** (SC-004) | full `ctest --preset linux-clang-debug` (dictionary + wire + codegen labels) | validator / `as_table_view` / C-ABI read path consume the Orchestra `Dictionary` with no source change; **codegen tests do not regress** (codegen does not consume a `vlatest` dict — `build_ir` throws on it) |
| 9 | **Fuzz harness** (Article VII §7) | `cmake --build … --target orchestra_loader_fuzz && ./orchestra_loader_fuzz -max_total_time=600 corpus/` | no crash / no leak on the parser over ≥10 min |

## Provenance / license check (SC-007)

```bash
cat dictionaries/orchestra/UPSTREAM.txt        # repo @ SHA tag= date=, EP303, + the recorded sha1
# The fetched official file MUST match the spike's grade-1 recorded sha1 (integrity pin):
sha1sum dictionaries/orchestra/OrchestraFIXLatest.xml   # == 26f60db1c1f52d169d3b6825ac68800abf487fde (also recorded in UPSTREAM.txt)
ls dictionaries/orchestra/{LICENSE,NOTICE}     # Apache-2.0 text + §4 attribution present
```

## Local Tier-1 mirror

After implementation, run `/speckit-verify` — it derives the sanitizer/coverage/static-analysis/fuzz plan from this feature's diff and the gating `tier1.yml`, and produces `.specify/decisions/074-orchestra-native-reader-verify.md` (required evidence for `/gate-b`). Note `/speckit-verify` is clang-only; the gcc-release + MSVC jobs are CI-only.

## Out of scope (do NOT expect these to pass here)

- Typed `owning_<Message>` codegen for the 181 FIX Latest classes (follow-on).
- Live FIX Latest wire-message validation through `dictionary_driven_validator`.
- ApplExtID(1156)=303 wire differentiation (scheduled follow-on).
- Session-layer `DefaultApplVerID` negotiation.
