# Implementation Plan: Native Orchestra Reader (FIX Latest)

**Branch**: `074-orchestra-native-reader` | **Date**: 2026-07-13 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `specs/074-orchestra-native-reader/spec.md`

## Summary

Add a native pugixml reader (`OrchestraLoader`, sibling to `dict::XmlLoader`) that parses the official Apache-channel `OrchestraFIXLatest.xml` (`fixr:repository` schema, FIX Latest EP303) **directly** into the same internal `detail::dict_metadata_handle` / `Dictionary` the QuickFIX loader produces — so the validator, `as_table_view` group-context machinery, codegen, and C-ABI are unchanged downstream. Give FIX Latest a real distinct identity via `session_version::vlatest` (wire application version stays `v50sp2`/ApplVerID 9 — FIX Latest has no distinct ApplVerID; ApplExtID(1156)=303 is a scheduled follow-on). Vendor + pin + Apache-2.0-attribute the source. Strictly additive: the nine QuickFIX dicts and `XmlLoader` are untouched.

**Technical approach** (from the three Phase-0 exploration passes, consolidated in [research.md](./research.md)): mirror the `XmlLoader` facade→`LoaderState`→`finalize()`→`dict_metadata_handle` shape; reuse verbatim the `field_data_type` enum, `dict_metadata_handle`, `Dictionary`, `as_table_view`, `table_view` context machinery, and the `dict::` error base classes. Supply Orchestra-specific analogues **only** for (a) the root/version gate (`<fixr:repository version="FIX.Latest_EP303">` → `session_version::vlatest`) and (b) the datatype-token table (Orchestra `<fixr:datatype>` names → `field_data_type`). Error strategy: derive `orchestra_parse_error : public dict::xml_parse_error` (reuse `code()`, discriminate by catch type — the `072` `group_delimiter_collision_error` precedent), zero C-ABI churn.

## Technical Context

**Language/Version**: C++23 (matches the library; `field_data_type`/`Dictionary`/pugixml consumers are C++23).

**Primary Dependencies**: pugixml (already pinned `pugixml/1.15`, MIT, in `conanfile.py`; consumed PRIVATE in `fixpp_dictionary` — the new reader `.cpp` joins that TU set so pugixml stays non-transitive). No new third-party dependency. *(Dependency-management rule: `pugixml/1.15` is current Conan-Center-latest; upstream 1.16 exists but has no Conan recipe yet — no bump required, the Orchestra grammar needs nothing beyond 1.15.)*

**Storage**: Vendored data file `dictionaries/orchestra/OrchestraFIXLatest.xml` (~7.5 MB), read from the source tree at test time via a CMake compile-definition path macro (mirror of `FIXPP_DICT_DATA_DIR`). No runtime storage.

**Testing**: GoogleTest (whole-binary grouped bucket per Article VII §8), plus a libFuzzer harness for the new parser (Article VII §7). Reader parses PMR-backed (`std::pmr::memory_resource*`), so PMR/OOM discipline applies.

**Target Platform**: Linux (Clang/GCC) Tier 1 + Windows/MSVC Tier 2 — same matrix as `fixpp_dictionary`.

**Project Type**: C++ library (single project; the `fixpp_dictionary` module).

**Performance Goals**: Config-time load path (not hot-path). No perf budget beyond "loads the 7.5 MB EP303 file in reasonable config time"; no `bench/` baseline change required (Article VIII applies to hot-path modules; dictionary load is config-time per `[const §XV.1]`).

**Constraints**: PMR-allocated build (wrap the body in `trap_throw_or_throw<xml_oom_error>` exactly as `XmlLoader` does); fail-closed on malformed/unknown input; strictly additive (no change to the nine QuickFIX dicts, `XmlLoader`, or the C-ABI frozen at 1.5.0). `-Werror`/`-Wswitch` in CI ⇒ the two exhaustive enum switches over `session_version` must be handled.

**Scale/Scope**: 181 messages, 579 groups / 167 components (2,490 inlined instances), depth-7 max group nesting, 32 datatype tokens, ~27k enum `<value>` entries. Well within `kMaxGroupContextDepth = 16`.

## Constitution Check

*GATE: evaluated pre-Phase-0 and re-checked post-Phase-1 (both PASS with the amendment + controls noted).*

| Article | Gate | Disposition |
|---|---|---|
| **I §1 / XVIII.1** — v1.0 version set locked to FIX 4.0–5.0SP2 + FIXT.1.1 | Adding `session_version::vlatest` widens the supported version set beyond the locked nine | **AMENDMENT REQUIRED** (Article XX) — a MINOR amendment adding FIX Latest (runtime/dictionary tier) to the supported set, **folded at Gate A** per the row-4b v1.0-gating promotion (precedent: 035/043/068/069 Gate-A-folded amendments). Tracked in Complexity Tracking + [research.md](./research.md) Decision D-7. Not a silent violation. |
| **Appendix A** — Codegen layout (dictionary loader, multi-version coexistence) + wire/version | Feature adds a dictionary loader + a new version identity | **All four controls apply:** `/clarify` ✅ done (2 rounds), **Codex Gate A ⏳ NEXT** (pipeline.md step 4, BEFORE `/tasks` step 5 — `[const §XVII.1]`) + user `/plan` sign-off ⏳ mandatory; `/speckit-analyze` runs at step 6 (post-`/tasks`), not next. |
| **V §4** — vendored third-party content carries file-level attribution | Vendoring an Apache-2.0 XML | Ships `dictionaries/orchestra/{LICENSE(Apache-2.0), NOTICE, UPSTREAM.txt}` + README license note. Keeps the Apache surface distinct from the QuickFIX-1.0 surface. The separate pending QuickFIX `NOTICE` (row 15d) is **not** in scope. |
| **VI §4/§5** — coverage-index bidirectional traceability + Normative References | New OFFICIAL rows (absorbs D-011 + A-035..A-065) | Spec now carries a Normative References section; coverage-index `[DocAbbrev §X.Y.Z]` slug assignment + entries are a `/speckit-analyze` / Gate-A reconciliation item (flagged in spec). |
| **VII §3/§4/§7/§8** — TDD, no untested code, fuzz for parser-touching, grouped tests | New parser | TDD red-first; new reader is **parser-touching ⇒ a libFuzzer harness is mandatory** (data-model + tasks include it); tests join a `dictionary`-labelled grouped bucket (new `dictionary_orchestra_tests` executable, `ctest -L orchestra`), no `gtest_discover_tests`. |
| **IX** — coverage 95/85 touched modules, sanitizers, static analysis | Standard | Applies to `src/dictionary/` new files; `/speckit-verify` mirrors Tier 1. |
| **X** — C-ABI frozen 1.5.0 | No C-ABI change | Error strategy derives from `xml_parse_error` (reuses `code()`) — **zero `core::error` enum append, zero C-ABI touch** (FR-001/008). |
| **III** — Conan pinned deps | pugixml already pinned | No change. |

**Result: PASS**, conditioned on the folded constitution amendment (the single justified deviation — see Complexity Tracking) and the four Appendix-A controls, of which two remain (`/analyze`, Gate A) plus user `/plan` sign-off.

## Project Structure

### Documentation (this feature)

```text
specs/074-orchestra-native-reader/
├── plan.md              # This file
├── research.md          # Phase 0 — 8 decisions (reader shape, version gate, datatype table, error strategy, vendoring, ApplVerID, amendment, verification)
├── data-model.md        # Phase 1 — Orchestra→internal entity mapping + version-identity change set + blast-radius table
├── quickstart.md        # Phase 1 — how to build + run the load/group/fail-closed/legacy-regression checks
├── contracts/
│   └── orchestra_loader.md   # Phase 1 — the OrchestraLoader public surface + fail-closed contract + mapping table
└── checklists/
    └── requirements.md  # /speckit-specify quality checklist (16/16)
```

### Source Code (repository root = the library submodule)

```text
include/fixpp/dict/
├── orchestra_loader.hpp        # NEW — OrchestraLoader facade (load/load_from_string → Dictionary, mr); mirrors xml_loader.hpp
├── error.hpp                   # EDIT — add orchestra_parse_error : public xml_parse_error (reuse code(), catch-discriminated)
├── version_profile.hpp         # EDIT — add session_version::vlatest (=10); render_appl_ver_id UNCHANGED (no application_version::vlatest)
└── version_registry.hpp        # (no edit — kTableSize stays 9; no application_version::vlatest added)

src/dictionary/
├── orchestra_loader.cpp        # NEW — OrchestraLoaderState: parse_repository → same FieldRef[]/GroupRef[]/ComponentRef[] tables → dict_metadata_handle; Orchestra datatype-token table (kOrchestraTypeTable) + root/version gate
├── version_registry.cpp        # EDIT (FORCED by -Wswitch) — session_to_application: add `case vlatest: return application_version::v50sp2;`
└── CMakeLists.txt              # EDIT — add orchestra_loader.cpp to fixpp_dictionary source list (keeps pugixml PRIVATE)

dictionaries/orchestra/         # NEW dir
├── OrchestraFIXLatest.xml      # vendored, pinned (Apache-2.0)
├── LICENSE                     # Apache-2.0 text
├── NOTICE                      # Apache-2.0 §4 attribution
└── UPSTREAM.txt                # repo @ SHA tag= date= (mirror dictionaries/UPSTREAM.txt convention)

tests/dictionary/
├── orchestra_loader_test.cpp   # NEW — load(181 msgs) / group depth-7 + reused-tag / version-identity / fail-closed(unknown datatype) / legacy no-regression
├── fuzz/orchestra_loader_fuzz.cpp  # NEW — libFuzzer harness over load_from_string (Article VII §7)
└── CMakeLists.txt              # EDIT — new dictionary_orchestra_tests grouped exe (LABELS "dictionary;orchestra") + ORCHESTRA data-dir macro + fuzz target
```

**Structure Decision**: Single-project, all changes inside the `fixpp_dictionary` module + its tests + a new `dictionaries/orchestra/` vendoring dir. The new reader source joins `fixpp_dictionary`'s existing source list (not a new library) so pugixml stays a PRIVATE link dep. The only edits outside the new reader are the two forced version-identity touches (`version_profile.hpp` enum add + `version_registry.cpp` exhaustive-switch arm) and CMake wiring.

## Complexity Tracking

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|-------------------------------------|
| **Constitution amendment** (Article I §1 / XVIII.1 — widen v1.0 version set to include FIX Latest) | FIX Latest is v1.0-gating per the user's 2026-07-13 decision (`REMAINING-WORK.md` row 4b); the reader cannot land honestly under the locked nine-version set without adding `session_version::vlatest` | Cannot avoid: relabelling FIX Latest as `FIX.5.0SP2` (the spike hack) is explicitly rejected by FR-005; and shipping the reader without a distinct identity would reintroduce the relabel. The amendment is MINOR + additive and folds at Gate A (established precedent), not a standalone Article XX §2 PR. |
| **Touching `version_registry.cpp` from a "read-path" feature** | `session_to_application` is an exhaustive `default`-free `switch` over `session_version`; `-Wswitch`+`-Werror` make a new enumerator a hard CI build break unless the arm is added | Cannot avoid: adding `session_version::vlatest` (required for identity) forces this arm. Mapping `vlatest → application_version::v50sp2` is the standards-correct arm (FIX Latest wire app-version IS 5.0 SP2). This is a 1-line forced edit, not new machinery; the session FSM (begin_string/negotiation) stays untouched. |
