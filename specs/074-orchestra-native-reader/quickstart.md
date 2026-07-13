# Quickstart / Validation Guide: Native Orchestra Reader (FIX Latest)

How to build and prove the feature end-to-end. Details of the interface and mapping live in [contracts/orchestra_loader.md](./contracts/orchestra_loader.md) and [data-model.md](./data-model.md); this is the run guide.

## Prerequisites

- The library submodule build toolchain (Conan + CMake ≥ 3.28 + Ninja), same as `fixpp_dictionary`.
- The vendored source present at `dictionaries/orchestra/OrchestraFIXLatest.xml` (pinned `FIXTradingCommunity/orchestrations @ 236d4a405…`, sha1 `26f60db1…`, EP303). Fetching it is the first implementation task (requires network); verify the sha1 on landing.
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

| # | Scenario | Command | Expected |
|---|---|---|---|
| 1 | **Load 181 messages** (SC-001) | `ctest --preset linux-clang-debug -L orchestra -R OrchestraLoad` | `messages().size() == 181`, `which_session_version() == vlatest` |
| 2 | **Group resolution** (SC-003) | `ctest … -R OrchestraGroups` | depth-7 `MassQuoteAck` (path `296→295→555→40241→41686→41680→41683`) member set + first field non-empty; reused tag 555 resolves non-empty under multiple parents via context key |
| 3 | **Fail-closed on unknown datatype, proven RED** (SC-002) | `ctest … -R OrchestraFailClosed` | synthetic Orchestra XML with an unknown `<fixr:datatype>` throws `orchestra_parse_error`; valid EP303 has zero unknown tokens |
| 4 | **Distinct version identity** (SC-005) | `ctest … -R OrchestraVersionIdentity` | `vlatest` distinct from all nine legacy identities; `session_to_application(vlatest) == v50sp2`; no `FIX.5.0SP2` relabel |
| 5 | **Legacy no-regression** (SC-006) | `ctest --preset linux-clang-debug -L dictionary` | all nine QuickFIX dicts load through `XmlLoader` with unchanged message counts + group queries |
| 6 | **Downstream unchanged** (SC-004) | full `ctest --preset linux-clang-debug` (dictionary + wire + codegen labels) | validator / `as_table_view` / codegen consume the Orchestra `Dictionary` with no code change |
| 7 | **Fuzz harness** (Article VII §7) | `cmake --build … --target orchestra_loader_fuzz && ./orchestra_loader_fuzz -max_total_time=600 corpus/` | no crash / no leak on the parser over ≥10 min |

## Provenance / license check (SC-007)

```bash
cat dictionaries/orchestra/UPSTREAM.txt        # repo @ SHA tag= date=, EP303
sha1sum dictionaries/orchestra/OrchestraFIXLatest.xml   # == 26f60db1c1f52d169d3b6825ac68800abf487fde
ls dictionaries/orchestra/{LICENSE,NOTICE}     # Apache-2.0 text + §4 attribution present
```

## Local Tier-1 mirror

After implementation, run `/speckit-verify` — it derives the sanitizer/coverage/static-analysis/fuzz plan from this feature's diff and the gating `tier1.yml`, and produces `.specify/decisions/074-orchestra-native-reader-verify.md` (required evidence for `/gate-b`). Note `/speckit-verify` is clang-only; the gcc-release + MSVC jobs are CI-only.

## Out of scope (do NOT expect these to pass here)

- Typed `owning_<Message>` codegen for the 181 FIX Latest classes (follow-on).
- Live FIX Latest wire-message validation through `dictionary_driven_validator`.
- ApplExtID(1156)=303 wire differentiation (scheduled follow-on).
- Session-layer `DefaultApplVerID` negotiation.
