# Implementation Plan: Native Orchestra Reader (FIX Latest)

**Branch**: `074-orchestra-native-reader` | **Date**: 2026-07-13 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `specs/074-orchestra-native-reader/spec.md`

## Summary

Add a native pugixml reader (`OrchestraLoader`, sibling to `dict::XmlLoader`) that parses the official Apache-channel `OrchestraFIXLatest.xml` (`fixr:repository` schema, FIX Latest EP303) **directly** into the same internal `detail::dict_metadata_handle` / `Dictionary` the QuickFIX loader produces — so the validator, `as_table_view` group-context machinery, and C-ABI read path are unchanged downstream. Codegen source remains untouched and its existing nine-dictionary tests must not regress, but codegen does **not** consume a `vlatest` dictionary in this feature (`build_ir` throws on the unmapped session — typed FIX Latest codegen is a scheduled follow-on). Give FIX Latest a real distinct identity via `session_version::vlatest` (wire application version stays `v50sp2`/ApplVerID 9 — FIX Latest has no distinct ApplVerID; ApplExtID(1156)=303 is a scheduled follow-on). Vendor + pin + Apache-2.0-attribute the source. Strictly additive: the nine QuickFIX dicts and `XmlLoader` are untouched.

**Technical approach** (from the three Phase-0 exploration passes, consolidated in [research.md](./research.md)): mirror the `XmlLoader` facade→`LoaderState`→`finalize()`→`dict_metadata_handle` shape; reuse verbatim the `field_data_type` enum, `dict_metadata_handle`, `Dictionary`, `as_table_view`, `table_view` context machinery, and the `dict::` error base classes. Supply Orchestra-specific analogues **only** for (a) the root/version gate (`<fixr:repository version="FIX.Latest_EP303">` → `session_version::vlatest`) and (b) the datatype-token table (Orchestra `<fixr:datatype>` names → `field_data_type`). Error strategy: derive `orchestra_parse_error : public dict::xml_parse_error` (reuse `code()`, discriminate by catch type — the `072` `group_delimiter_collision_error` precedent), zero C-ABI churn.

## Technical Context

**Language/Version**: C++23 (matches the library; `field_data_type`/`Dictionary`/pugixml consumers are C++23).

**Primary Dependencies**: pugixml (already pinned `pugixml/1.15`, MIT, in `conanfile.py`; consumed PRIVATE in `fixpp_dictionary` — the new reader `.cpp` joins that TU set so pugixml stays non-transitive). No new third-party dependency. *(Dependency-management rule: `pugixml/1.15` is current Conan-Center-latest; upstream 1.16 exists but has no Conan recipe yet — no bump required, the Orchestra grammar needs nothing beyond 1.15.)*

**Storage**: Vendored data file `dictionaries/orchestra/OrchestraFIXLatest.xml` (~7.5 MB), read from the source tree at test time via a CMake compile-definition path macro (mirror of `FIXPP_DICT_DATA_DIR`). No runtime storage.

**Testing**: GoogleTest (whole-binary grouped bucket per Article VII §8), plus a libFuzzer harness for the new parser (Article VII §7). Reader parses PMR-backed (`std::pmr::memory_resource*`), so PMR/OOM discipline applies.

**Target Platform**: Linux (Clang/GCC) Tier 1 + Windows/MSVC Tier 2 — same matrix as `fixpp_dictionary`.

**Project Type**: C++ library (single project; the `fixpp_dictionary` module).

**Performance Goals**: Config-time load path (not hot-path). No perf budget beyond "loads the 7.5 MB EP303 file in reasonable config time"; no `bench/` baseline change required (Article VIII applies to hot-path modules; dictionary load is config-time per `[const §XV.1]`).

**Constraints**: PMR-allocated build (wrap the body in `trap_throw_or_throw<xml_oom_error>` exactly as `XmlLoader` does); fail-closed on malformed/unknown input; strictly additive (no change to the nine QuickFIX dicts, `XmlLoader`, or the C-ABI frozen at 1.5.0). `-Werror`/`-Wswitch` in CI ⇒ the **one** exhaustive `default`-free switch over `session_version` (`session_to_application`, `version_registry.cpp:33-55`) must gain a `vlatest` arm. (The other exhaustive render switch, `render_appl_ver_id`, is over `application_version` — **not** touched, since no `application_version::vlatest` is added.)

**Scale/Scope**: 181 messages, 579 groups / 167 components (2,490 inlined instances), depth-7 max group nesting, 32 datatype tokens, ~27k enum `<value>` entries. Well within `kMaxGroupContextDepth = 16`.

## Constitution Check

*GATE: evaluated pre-Phase-0 and re-checked post-Phase-1 (both PASS with the amendment + controls noted).*

| Article | Gate | Disposition |
|---|---|---|
| **I §1 / XVIII.1** — v1.0 version set locked to FIX 4.0–5.0SP2 + FIXT.1.1 (and Article I §1 line 42 explicitly lists **FIX Latest** as a **post-1.0 milestone**) | Adding `session_version::vlatest` widens the supported version set beyond the locked nine | **AMENDMENT REQUIRED — draft provided (D-7), folded at Gate A** (Article XX). Precedent corrected (Gate A r1): this is the **first amendment to ADD a new FIX version** to the locked set — categorically larger than the cited Gate-A-folded precedents (035 FileStore exemption / 043 SecurityProfile reopen / 068 test-infra / 069 v44-codegen **reclassification**), **none of which widened the version set**; do not equate them. The concrete draft in [research.md](./research.md) D-7 (i) adds FIX Latest at the **read/dictionary tier only** (so it does NOT trigger Article I §1's "100% of the official spec" session+application obligation), (ii) moves FIX Latest off the Article I §1 line-42 post-1.0 milestone list, and (iii) states it is a version-set widening distinct from the 069-style reclassification. **Remaining:** explicit user `/plan` sign-off + ratification at the Gate-A fold (a control already listed as pending). Tracked in Complexity Tracking. Not a silent violation. |
| **Appendix A** — Codegen layout (dictionary loader, multi-version coexistence) + wire/version | Feature adds a dictionary loader + a new version identity | **All four controls apply:** `/clarify` ✅ done (2 rounds), **Codex Gate A ⏳ NEXT** (pipeline.md step 4, BEFORE `/tasks` step 5 — `[const §XVII.1]`) + user `/plan` sign-off ⏳ mandatory; `/speckit-analyze` runs at step 6 (post-`/tasks`), not next. |
| **V §4** — vendored third-party content carries file-level attribution | Vendoring an Apache-2.0 XML | Ships `dictionaries/orchestra/{LICENSE(Apache-2.0), NOTICE, UPSTREAM.txt}` + README license note. Keeps the Apache surface distinct from the QuickFIX-1.0 surface. The separate pending QuickFIX `NOTICE` (row 15d) is **not** in scope. |
| **VI §4/§5** — coverage-index bidirectional traceability + Normative References | New OFFICIAL rows (absorbs D-011 + A-035..A-065) | **CONDITIONAL** (not an unbacked PASS). Spec Normative References cite the real registered anchors (`FIX-Latest` DocAbbrev + the D-011 row + the A-035..A-065 MsgType rows); the Orchestra `fixr:repository` schema has no `§X.Y.Z` slugs so none are manufactured. **Blocking before-land obligation (Article VI §4):** register an Orchestra-schema DocAbbrev + add the bidirectional `coverage-index.md` entries (promote D-011, link A-035..A-065) — a named `/speckit-analyze` (step 6) task, done before merge, not before Gate A. |
| **VII §3/§4/§7/§8** — TDD, no untested code, fuzz for parser-touching, grouped tests | New parser | TDD red-first; new reader is **parser-touching ⇒ a libFuzzer harness is mandatory** (data-model + tasks include it); tests join a `dictionary`-labelled grouped bucket (new `dictionary_orchestra_tests` executable, `ctest -L orchestra`), no `gtest_discover_tests`. |
| **IX** — coverage 95/85 touched modules, sanitizers, static analysis | Standard | Applies to `src/dictionary/` new files; `/speckit-verify` mirrors Tier 1. |
| **X** — C-ABI frozen 1.5.0 | No C-ABI change | Error strategy derives from `xml_parse_error` (reuses `code()`) — **zero `core::error` enum append, zero C-ABI touch** (FR-001/008). |
| **III** — Conan pinned deps | pugixml already pinned | No change. |

**Result: PASS**, conditioned on the folded constitution amendment (draft provided in D-7, precedent corrected, scoped to the read/dictionary tier — the single justified deviation, see Complexity Tracking) and the four Appendix-A controls, of which two remain (`/analyze`, Gate A) plus mandatory user `/plan` sign-off. One row carries a named pre-land obligation: the **Article VI** row is **CONDITIONAL** (coverage-index DocAbbrev + bidirectional entries are a before-land `/speckit-analyze` obligation, not folded into the green). The Article I §1 amendment is drafted and awaits only user sign-off + Gate-A ratification.

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
├── version_registry.hpp        # (kTableSize stays 9; no application_version::vlatest) — MAY carry the FR-010 guard if it lands in an error-returning build_version_registry
└── dictionary.hpp              # EDIT — add `friend class OrchestraLoader;` (dictionary.hpp:188 currently friends ONLY XmlLoader); required so OrchestraLoader can construct `Dictionary{handle}` via the private handle-ctor (dictionary.hpp:192). Third edit site outside the reader.

src/dictionary/
├── orchestra_loader.cpp        # NEW — OrchestraLoaderState: parse_repository → same FieldRef[]/GroupRef[]/ComponentRef[] tables → dict_metadata_handle; Orchestra datatype-token table (kOrchestraTypeTable) + root/version gate
├── version_registry.cpp        # EDIT (two logical edits) — (1) FORCED by -Wswitch: session_to_application add `case vlatest: return application_version::v50sp2;`; (2) FR-010 interim fail-loud guard against silent same-slot double-registration (ctor at :60-73 is noexcept ⇒ abort/fatal-in-ctor, OR route the guard through an error-returning build_version_registry — mechanism at /tasks)
└── CMakeLists.txt              # EDIT — add orchestra_loader.cpp to fixpp_dictionary source list (keeps pugixml PRIVATE)

dictionaries/orchestra/         # NEW dir
├── OrchestraFIXLatest.xml      # vendored, pinned (Apache-2.0)
├── LICENSE                     # Apache-2.0 text
├── NOTICE                      # Apache-2.0 §4 attribution
└── UPSTREAM.txt                # repo @ SHA tag= date= (mirror dictionaries/UPSTREAM.txt convention)

tests/dictionary/
├── orchestra_loader_test.cpp   # NEW — test seams:
│   #   OrchestraLoader.Load181            — 181 msgs, which_session_version()==vlatest (SC-001)
│   #   OrchestraLoader.Groups             — depth-7 MassQuoteAck asserts the FULL parent path 296→295→555→40241→41686→41680→41683 (not just non-empty) + reused-tag 555 non-empty under each distinct parent (SC-003, FR-004)
│   #   OrchestraCodesets.PreservesValuesAndDescriptions — a known EP303 codeset field: assert BOTH enum value bytes AND description text survive (FR-002)
│   #   OrchestraFailClosed.UnusedUnknownDatatypeDecl — an unknown <fixr:datatype> that no field references does NOT fail (if unreachable)
│   #   OrchestraFailClosed.FieldUsesUnknownDatatype  — a field with type="<DefinitelyUnknownType>" DOES throw orchestra_parse_error (SC-002, discriminating RED + valid-EP303 negative arm)
│   #   OrchestraFailClosed.UnionArmUnknownBase       — a unionDataType whose PRIMARY/base arm is unknown still fails closed (drop-second-arm must not mask it)
│   #   OrchestraVersionIdentity.Distinct  — vlatest distinct at session_version layer; session_to_application(vlatest)==v50sp2 (SC-005)
│   #   OrchestraLegacy.NoRegression       — nine QuickFIX dicts unchanged (SC-006)
├── orchestra_registry_guard_test.cpp  # NEW (FR-010) — build a version_registry with BOTH a real FIX50SP2 dict and a FIX Latest dict; assert the fail-loud guard fires (death/abort or error return), NOT a silent last-writer-wins overwrite. May be a standalone (death/abort) binary per the alloc/oom-style standalone convention.
├── fuzz/orchestra_loader_fuzz.cpp  # NEW — libFuzzer harness over load_from_string (Article VII §7)
└── CMakeLists.txt              # EDIT — new dictionary_orchestra_tests grouped exe (LABELS "dictionary;orchestra") + ORCHESTRA data-dir macro + fuzz target + the FR-010 guard test
```

**Structure Decision**: Single-project, all changes inside the `fixpp_dictionary` module + its tests + a new `dictionaries/orchestra/` vendoring dir. The new reader source joins `fixpp_dictionary`'s existing source list (not a new library) so pugixml stays a PRIVATE link dep. The edits outside the new reader are at **three** header/source sites (plus CMake wiring): (1) `version_profile.hpp` — `session_version::vlatest` enum add; (2) `version_registry.cpp` — the forced exhaustive-switch arm **and** the FR-010 interim fail-loud double-registration guard (two logical edits, same TU); (3) `dictionary.hpp` — a one-line `friend class OrchestraLoader;` so the reader can build `Dictionary{handle}` via the private handle-ctor (symmetric with the existing `friend class XmlLoader;`, `dictionary.hpp:188`). The FR-010 guard is the one edit that could grow blast radius **if** implemented as an error-returning `build_version_registry` (ripples into `engine_config.hpp:211-214` + `engine.cpp:108`); the fatal-in-ctor variant stays contained in `version_registry.cpp` — the mechanism (and thus the exact blast radius) is chosen at `/speckit-tasks`. The session FSM (`begin_string`/negotiation) stays untouched.

## Complexity Tracking

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|-------------------------------------|
| **Constitution amendment** (Article I §1 / XVIII.1 — widen v1.0 version set to include FIX Latest) | FIX Latest is v1.0-gating per the user's 2026-07-13 decision (`REMAINING-WORK.md` row 4b); the reader cannot land honestly under the locked nine-version set without adding `session_version::vlatest` | Cannot avoid: relabelling FIX Latest as `FIX.5.0SP2` (the spike hack) is explicitly rejected by FR-005; and shipping the reader without a distinct identity would reintroduce the relabel. **This is the FIRST amendment to ADD a new FIX version to the locked set** — categorically larger than the Gate-A-folded *reclassification* precedents (035/043/068/069 widened no version set); it also contradicts Article I §1 line 42 (FIX Latest listed as a post-1.0 milestone). Scoped to the **read/dictionary tier only** (no session+application "100% of the spec" obligation triggered) and MINOR + additive; concrete revision text is now drafted (D-7). Remaining: explicit user `/plan` sign-off + ratification at the Gate-A fold. Not a silent violation. |
| **Touching `version_registry.cpp` from a "read-path" feature** | `session_to_application` is an exhaustive `default`-free `switch` over `session_version`; `-Wswitch`+`-Werror` make a new enumerator a hard CI build break unless the arm is added | Cannot avoid: adding `session_version::vlatest` (required for identity) forces this arm. Mapping `vlatest → application_version::v50sp2` is the standards-correct arm (FIX Latest wire app-version IS 5.0 SP2). This is a 1-line forced edit, not new machinery; the session FSM (begin_string/negotiation) stays untouched. |
| **FR-010 interim registry double-registration guard** (`version_registry.cpp`) | `session_to_application(vlatest)→v50sp2` collapses FIX Latest and real 5.0SP2 onto the one `v50sp2` slot; the ctor is silent last-writer-wins (`version_registry.cpp:71-72`), so co-registering both silently drops one — the project's cardinal silent-loss sin | Cannot ship silently. Full ApplExtID-aware re-keying is legitimately deferred by the spike RECONCILE (L145-146), but shipping the collapse *silently* is not sanctioned. The lighter interim fix is a release-effective fail-loud guard (abort/fatal-in-ctor or error-returning builder), not the heavy re-keying. |

## Gate A

- Round 1 applied 2026-07-13: Codex P1=1 P2=6 P3=3; Opus post-judging P1=1 P2=8 P3=4; rewrite addresses root causes [registry-collision guard, cross-doc overclaims (SC-004/codegen, build_ir), test-seam strengthening, Article VI slug/BLOCKED, friend-edit correction]. Reviews: research/reviews/codex_074-orchestra-native-reader_gate_a_review.md, research/reviews/opus_074-orchestra-native-reader_gate_a_adversarial_review.md.
  - Coverage of the full P2=8 tally: the mandated root-cause bracket above names 7 threads; the **8th P2 (NEW — undrafted amendment + inapposite 035/043/068/069 precedent, Opus root cause #4)** is also addressed — precedent corrected (first version-set widening, not a reclassification; verified against constitution Article I §1 line 42 which lists FIX Latest as a post-1.0 milestone), concrete amendment draft supplied in research.md D-7, scoped to the read/dictionary tier, Article I / Complexity-Tracking rows re-marked.
- Round 2 converged 2026-07-13: Codex P1=0 P2=1 P3=0; Opus post-judging P1=0 P2=0 P3=2 (both P3 applied post-convergence: plan.md Summary codegen residual + FR-010 fatal-in-ctor preference). Reviews: research/reviews/codex_074-orchestra-native-reader_gate_a_2_review.md, research/reviews/opus_074-orchestra-native-reader_gate_a_2_adversarial_review.md.
