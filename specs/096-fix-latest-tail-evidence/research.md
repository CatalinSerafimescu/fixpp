# Research — 096 FIX Latest tail evidence pass

Every reading below was taken at `2de73f3f` (2026-10-28). Where a reading is a measurement, the
command that re-derives it is given; the result itself is not recorded as a fixed number (FR-006),
except where this file says it is the input to a decision and the tests re-derive it on every run.

## R-1 The tail set is in both dictionaries (Context, ruling 1)

**Decision**: close each row on both typed tiers.

**Evidence**: for each of the 31 MsgTypes,
`grep -o "<message name='[^']*' msgtype='<MT>'[^>]*>" dictionaries/FIX50SP2.xml` returns a `msgcat='app'`
message, and `grep -o '<fixr:message [^>]*msgType="<MT>"[^>]*>' dictionaries/orchestra/OrchestraFIXLatest.xml`
returns a message with `added="FIX.5.0SP2"` and a non-`Session` category. Both generated trees carry
`messages/<Name>.hpp` for each (`build/<preset>/_codegen/include/fixpp/{v50sp2,vlatest}/messages/`).
From the library root,
`cmp ../../../reference-engines/quickfix-cpp/spec/FIX50SP2.xml dictionaries/FIX50SP2.xml` exits 0
(quickstart §0). QuickFIX/J 3.0.1's base edition, `FIX50SP2.modified.xml`, holds none of the 31:
quickstart §0's QuickFIX/J loop checks it fail-closed (the file exists, the checkout is at tag
`QFJ_RELEASE_3_0_1`, `BN` is found exactly once, each of the 31 is found zero times). The path is
`quickfixj-messages/quickfixj-messages-fix50sp2/src/main/resources/FIX50SP2.modified.xml`, tracked
in QuickFIX/J's own repository, so a fresh clone at that tag has it; the Maven output copy under
`target/classes/` is not cited, because a fresh clone lacks it. `reference-engines/` is gitignored
in the parent repo: each engine there is its own clone.

**Alternatives**: vlatest only (the plan of record): rejected by the owner, 2026-10-28, because it
ignores the v50sp2 tier that already exists for these MsgTypes.

## R-2 Where the witnesses live, and the shared machinery (FR-001..FR-003, `[const §VII.8]`)

**Decision**:
- Move 095's machinery out of `tests/session/test_095_tail_rows_roundtrip.cpp` into a new header,
  `tests/support/tail_rows_witness.hpp`, namespace `fixpp_test_support::tail_rows`. Moved, in 095's
  declaration order: the `IndexView` alias, then every declaration from `enum class walk_status`
  through `join_tags` (the walk section, the QuickFIX-XML `Walker`, `msgtype_for_generated_name`,
  `split_test_name`, the body enumeration, `value_at`, `parse_count`, `tag_of_path`,
  `required_members_missing`, `join`, `join_tags`), then `drop_hyphen`, `parse_noting_refusal` and
  `Intent`. 095's `using fixpp_test_support::…` declarations stay in its TU, except
  `using fixpp_test_support::parse_dict;`, which goes because only moved code uses it; the header's
  namespace is nested in `fixpp_test_support`, so the moved code sees those names unqualified. What
  may change is fixed byte for byte by two stated transforms (contracts W0):
  - outside the moved region: the header's `#pragma once`, `#include`s, namespace wrapper, comment
    and blank lines; in 095's TU the header's `#include`, `using` lines naming
    `fixpp_test_support::tail_rows`, comment lines naming the header, and the forwarding method;
  - inside it: the region sits between `// clang-format off` / `// clang-format on` lines, so no
    formatter reflows it, and differs from the base only by a leading `inline` on each function head
    (it now lives in a header). No statement, name, type, literal or comment changes, and
    `walk_status` keeps its values;
  - `Tail095Fixture::check_witness` becomes a free function. Its statements are kept in order,
    four columns less indented (it leaves the class body), with three substitutions: the fixture's `tv()` and `walk()` become parameters, the fixture's
    `&read_arena_` becomes a `std::pmr::memory_resource*` parameter, and the statement that computed
    the expected MsgType from the test name with the QuickFIX-only `msgtype_for_generated_name(doc(),
    …)` is dropped in favour of a `std::string_view expected_mt` parameter (each tier maps a test
    name to a MsgType through its own dictionary). 095's fixture method keeps its signature,
    computes that MsgType as before and forwards.
  095's `TailRow` table, `SetUpTestSuite`, fixture classes, census, known positives, selection check
  and witnesses stay in its TU.
- The guards on that refactor: 095's committed tests (deficient frames, 35=i, S1/S2, census and
  witnesses), which run in the same binary after the move; quickstart §1's byte-exact diffs of the
  header region, of `check_witness` and of 095's residual TU, with their controls; and 096's
  comparator mutants D, H, E and value-mismatch (contracts W6), which run through the shared
  `check_witness` in the same binary.
- 096's v50sp2 witnesses: new TU `tests/session/test_096_tail_rows_roundtrip.cpp`, a third source of
  the existing executable `test_077_allversions_builder_roundtrip` (it already links
  `fixpp::builders::v50sp2` and `pugixml::pugixml` and defines `FIXPP_DICT_DATA_DIR` and the golden
  path). Suites `TailRows096` and `TailRows096Census`.
- 096's vlatest witnesses, the Orchestra walk and the differential: new TU
  `tests/session/test_096_tail_rows_roundtrip_vlatest.cpp`, a second source of the existing
  executable `test_077_allversions_builder_roundtrip_vlatest` (under `if(FIXPP_CODEGEN_FIX_LATEST)`).
  It gains `pugixml::pugixml`, `FIXPP_DICT_DATA_DIR` (already defined there) and the golden path.
  Suites `TailRows096Latest`, `TailRows096LatestCensus` and `TailRows096Differential`.

No new ctest entry and no new executable: both binaries are already registered standalone, with
labels; 096 adds `096` to each binary's LABELS.

**Rationale**: 095's FR-006 kept the 077 TU byte-unchanged; 096 likewise leaves both 077 TUs
byte-unchanged, and its spec FR-003 requires the 095 machinery to be reused with no behaviour change.
A copy would be two walkers that can drift; a move keeps one. The census in 095's TU iterates suite
`TailRows095` only, so new suites in the same binary do not disturb it.

**Alternatives**:
- Put the 31 v50sp2 witnesses into 095's TU: one 2 000+ line TU mixing two features' records; the
  census's literal tail table would have to carry two features' rows. Rejected.
- A new executable for 096: `[const §VII.8]` prefers the existing binary, and a second
  vlatest-builder-linking executable doubles the link cost for nothing. Rejected.
- Leave 095 untouched and copy its machinery: rejected above.

## R-3 The v50sp2 leg (FR-003)

**Decision**: 095's walk over `FIX50SP2.xml`, its readback comparator and `required_members_missing`,
used through the shared header. The 096 v50sp2 TU re-runs only what is per-feature: its census
(TailRows096 against its own literal 31-row table), and a field-leg known positive over its own 31
MsgTypes against `golden.csv` (`TailRows096Census.FieldLegMatchesQuickFixCppGolden`). 095's deficient
frames, its 35=i fail-loud and its synthetic dictionaries keep re-running in 095's TU; they are not
duplicated.

**Condition** (spec Edge Cases): every tail MsgType's walk status is `ok` on both legs.
`TailRows096Differential.TailMsgTypesAgree` asserts it on every run: a non-`ok` status on either side
is a `status` finding (data-model §D). The plan's scratch walk applied 095's rules before tasks were
written; if the real walk yields a non-`ok` status for a tail MsgType, the Edge Cases stop rule
applies.

## R-4 The Orchestra required-walk (FR-004)

**Decision**: a second walker in the vlatest TU, `OrchestraWalker`, over a `pugi::xml_document` of
`OrchestraFIXLatest.xml`, producing the shared `WalkMap`. It shares no code with
`src/dictionary/orchestra_loader.cpp` or `tools/codegen/` (076 FR-006's independence rule); it shares
the result type and the readback machinery with the QuickFIX walker (R-2), which is not a derivation.

Rules (spec Terminology), resolved against the file's structure:
- messages: `fixr:messages/fixr:message`, keyed by `msgType`; the walk starts at its `fixr:structure`;
- `fixr:fieldRef id` is the tag (Orchestra field ids are FIX tags); `fixr:componentRef id` resolves
  in `fixr:components/fixr:component`; `fixr:groupRef id` resolves in `fixr:groups/fixr:group`, whose
  count tag is its `fixr:numInGroup id`;
- required ⇔ `presence="required"` on every element of the path; an absent attribute is optional;
- the component references whose component is named `StandardHeader` or `StandardTrailer` are
  skipped;
- the delimiter is the first tag the group emits in declaration order, its `fixr:numInGroup` excluded,
  components expanded, a nested `fixr:groupRef` emitting its count tag. This is the loader's rule:
  `detail::capture_first_emission` is called for a field emission and for a nested group's count
  emission in `OrchestraLoader`'s structure walk (`grep -n capture_first_emission
  src/dictionary/orchestra_loader.cpp`), and the delimiter is the first capture at the open group's
  level;
- statuses: `nested_required_group` and `delimiter_chain` as 095's walk sets them.
  `group_among_siblings` does not arise: Orchestra references the group directly, and the path rule
  decides it. The QuickFIX wrapper shape written in Orchestra (a required `fixr:componentRef` whose
  component holds only a non-required `fixr:groupRef`) gets no status either: the path rule makes
  the group not required, and if 095's walk inherits required-ness through the QuickFIX form of
  the same group, the differential reports it (spec Clarifications, Gate A round 1);
- a `msgType` seen twice fails the walk loudly (fragment S5), unlike 095's `Walker::walk_all`, which
  assigns by key. The channel is an error string beside the map (`OrchestraWalk::error`, data-model
  §B.2), copied into the fixture's `load_error`, so no `walk_status` value is added;
- a reference outside `fixr:structure` is not walked (fragment S6's decoy);
- a group in `element` is named by its count field's name (the `fixr:field@name` of its
  `fixr:numInGroup id`), which is how 095's walk names it, so a status's element reads the same on
  both legs.

**Input to the decision to reuse 095's frames**: the vlatest known positives reuse 095's frames
(R-6) because 35=E's and 35=CC's required shapes, and 35=i's nested required group, were found the
same in both files when this plan was written. Re-derive: on the Orchestra file with data-model
§B.2's `awk` recipe; on `FIX50SP2.xml` with the `sed` lines of the "To re-derive one by hand"
recipe in the header comment of 095's TU (095 data-model §B). No committed test compares the two files' shapes; a frame that the parser refuses or
splits otherwise under the Orchestra dictionary stops the implementer (contracts W6).

**Alternatives**: read required-ness from the `Dictionary` that `OrchestraLoader` builds: circular
(the vlatest witness's parse uses the same loader). Rejected.

## R-5 The independent parse for the vlatest leg (FR-002)

**Decision**: frame with `make_frame` and BeginString `FIX.5.0SP2` (FIX Latest's on-wire application
version is 5.0SP2, L-074-1), parse with `fixpp::wire::Parser<Index>` over the `table_view` of a
`Dictionary` loaded from the Orchestra file by `dict::OrchestraLoader` (the load the 077 vlatest TU
already does, `load_dict`). No builder-side table is consulted. The witness's readback is 095's W4,
over the Orchestra walk's `WalkMap`.

**Rationale**: the parse must be dictionary-driven so that group instances are delimited; the
loader is the production reader of that file and is not the builder.

## R-6 Known positives for the vlatest leg and the differential (FR-009)

**Decision**:
- **Orchestra field leg**: `TailRows096LatestCensus.FieldLegMatchesQuickFixCppGolden` — the Orchestra
  walk's message-level field set equals the `golden.csv` row for each of the 31 tail MsgTypes, with
  095's row-count precondition. `golden.csv` is QuickFIX-cpp's measured output on `FIX50SP2.xml`, a
  file the Orchestra walk does not read: non-circular. Its limit: it is a valid positive only while
  the two files agree on a tail MsgType's message-level set. Where they truly differ (as they do on
  35=W's 779, outside the tail), a correct Orchestra walk would go RED here too, so this check
  cannot tell a walker bug from a dictionary difference. FR-012 stops the row either way, so neither
  case passes silently.
- **Group and per-instance legs**: 095's deficient frames (E-absent, E-one, E-short, E-member,
  E-nodelim, CC-nodelim, CC-nested, CC-count2), parsed under the Orchestra dictionary and checked
  with `required_members_missing` over the Orchestra walk, each against its exact list re-derived from
  the Orchestra file (data-model §B.2 gives the recipe); and 35=i's `nested_required_group`.
- **`delimiter_chain`, the componentRef AND, the duplicate-`msgType` rule and the start node**:
  synthetic Orchestra fragments S2, S4, S5 and S6 (`load_string`) fed to `OrchestraWalker`.
- **Delimiter rule, both emission kinds**: a nested-group emission has frame CC-nodelim; a
  component emission has a measured tail positive, 35=DP, whose required group's first child is a
  `fixr:componentRef` that supplies the delimiter, so its arm runs through `TailMsgTypesAgree`
  (contracts W6).
- **Differential**: `TailRows096Differential.KnownDifference_MarketDataSnapshotFullRefresh` — the
  comparator, run on 35=W, reports exactly one difference: path 779 (LastUpdateTime), value
  `required` on the `FIX50SP2.xml` side and `absent` on the Orchestra side. Re-derive:
  `sed -n "/<message name='MarketDataSnapshotFullRefresh'/,/<\/message>/p" dictionaries/FIX50SP2.xml | grep LastUpdateTime`
  (`required='Y'`) and
  `awk '/<fixr:message [^>]*msgType="W"/,/<\/fixr:message>/' dictionaries/orchestra/OrchestraFIXLatest.xml | grep 'id="779"'`
  (no `presence`). `grep '^FIX50SP2,W,' tools/quickfix_required_golden/golden.csv` carries 779, so
  QuickFIX-cpp agrees with the XML side.
- **Differential non-`ok` path**: `TailRows096Differential.KnownNonOk_MassQuote` — 35=i, where both
  walks return `nested_required_group` (R-4's input above), yields exactly one `status` finding.
- **Differential group, delimiter, instance-member and `delim_group` paths**: synthetic pairs of
  fragments, one pair per path, identical but for that path. They are synthetic because no measured
  source was selected for those paths (FR-009's stated limit); a measured difference may replace a
  pair later.
- **Differential collect-every-difference, sort and one-sided-group rules**: synthetic pair M, two
  MsgTypes passed out of order with several differences across paths (contracts W6); the 35=W and
  35=i positives each carry one finding and cannot show either rule.

**Rationale**: every output is shown able to fire (spec FR-009); a measured source stands where one
exists; a synthetic fragment stands only where none does, and the witness contract says which.

## R-7 Catalogue Spec refs (FR-011)

**Decision**: `[FIX-Latest] <Section words> — <Category words>` from the MsgType's `fixr:message
category` and that category's `fixr:category section`, each written as its identifier's words
(`[const §VI.2]`). Data-model §E has the 31-row table and the recipe. A-037 and A-038 already
conform and stay byte-unchanged.

## R-8 The amendment and its class (FR-010)

**Decision**: v4.1 → v4.2, MINOR (`[const §XX.4]`, no `CHANGELOG.md` entry). The test, as v4.1's
report puts it: does a PR mergeable under v4.1 fail under v4.2? The §XVIII.2 edit removes the rows
from a post-1.0 roadmap bullet because they ship in v1.0; the §XVIII.7 edit records them as closed.
Neither adds a banned pattern, tightens a budget or imposes an obligation. Process: 095 FR-021's
(scoped Codex Gate A on the clause bodies, Gate B, owner sign-off, RATIFIED flip last). Data-model §F
has the before → after text.

`[const §I.1]`'s FIX Latest bullet is read and unchanged: it states what the FIX Latest tiers deliver
and that ApplExtID(1156)=303 and session negotiation remain post-1.0. 096 changes neither statement;
row 4b owns the ApplExtID statement, and the scope decision of 2026-07-15 that pulls 4b into v1.0 is
4b's amendment to make.

## R-9 Local verify and build cost (Assumptions)

**Decision**: 095's precedent — the `linux-clang-debug` preset locally (both binaries, the 077, 095
and 096 suites, full ctest at the end per `[const §XVII.7]`); the sanitizer, coverage, release,
libc++ and MSVC presets to CI, recorded as waivers in the verify record. The vlatest TU includes 31
slim per-message headers and links `fixpp::builders::vlatest` (link mode); its compile RSS is
closure-bounded (L-078-1). The implementer compiles it alone first, `-j1`, under `/usr/bin/time -v`,
and stops if peak RSS exceeds 8 GiB.
