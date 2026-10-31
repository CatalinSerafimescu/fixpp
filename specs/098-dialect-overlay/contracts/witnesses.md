# Contract: 098 witnesses and their RED arms

Every witness names its file, what it asserts, and how it is shown RED **for its stated reason**. Every
CTest entry a 098 witness runs in carries label `098`. A bucket that already has labels gains it with
`set_property(TEST … APPEND PROPERTY LABELS 098)` after that entry's last LABELS setter, so its existing
labels survive. The standalone census also carries `dictionary` and `census`; the standalone live C-ABI
arm also carries `capi`.

**Test-binary disposition (`[const §VII.8]`).**

| Witness | Bucket | Reason |
|---|---|---|
| W-0 | `dictionary_pure_tests` | Self-test of test-local declared data, with no isolation hazard, like `T020_DeferredSetExactCoverage`. |
| W-1 | `dictionary_pure_tests` | Pure, single-threaded dictionary read/parse/compare tests. |
| W-2 | `dictionary_pure_tests` | Pure, single-threaded vendored-base comparisons. |
| W-3 | `dictionary_pure_tests` | Pure, single-threaded refusal tests. |
| W-4 | `dictionary_pure_tests` | Pure, single-threaded dispatch tests. |
| W-5 | `session_pure_tests` | Pure session test; this bucket supplies `src` and `FIXPP_DICT_DATA_DIR`. |
| W-6 | `session_pure_tests` | Pure session send tests with no isolation hazard. |
| W-7 | dictionary half: standalone `dictionary_098_header_census_test`, labels `dictionary;098;census`; session half: `session_pure_tests` | The dictionary half is an exact census over shipped dictionaries; the session half is a pure test and its bucket supplies `src` and `FIXPP_DICT_DATA_DIR`. |
| W-8 | `config_045_tests` | Pure configuration parsing and resolution tests. |
| W-9 | pure rows: `capi_pure_tests`; status-mapping rows: `dictionary_pure_tests`; live arm: standalone `capi_098_dict_overlay_live_test`, labels `capi;098`, with an explicit `TIMEOUT` | The pure rows start no engine, which is the bucket's admission rule (no `fixpp_engine_start()`). The status-mapping rows are pure, single-threaded calls of a dictionary-library function. The live arm starts two C-ABI engines with their worker threads over loopback, so `[const §VII.8]` keeps it standalone, as `capi_dict066_group_membership_red_test` and `capi_inbound_frame_dispositions_test` are. |
| W-10 | in-tree Python suite (`bindings/python/tests/`); the import-surface row in the wheel suite | Not a gtest executable. The new test has no wheel twin: its overlay fixtures are not in the wheel. |
| W-11 | `fuzz_dict_overlay` and its replay | Fuzz target outside §VII.8, built only under `FIXPP_BUILD_FUZZ`. |
| W-12 | verify measurement | Not a test executable. |
| W-13 | record-check scripts | Not a gtest executable. |

**RED arms.** A witness that calls a new symbol does not compile on the unfixed tree, and a compile
failure is RED for the wrong reason. Such a witness is shown RED by a **named mutant** applied to the
fixed tree, rebuilt, and run unfiltered (`--gtest_filter` narrows the report, never the binary). A row
whose rebuild failed proves nothing and is gated on build rc 0. Where a witness compiles on the unfixed
tree **and its assertion fails there**, its RED is the unfixed tree itself (marked **U**). An arm that
compiles on the unfixed tree and passes there is not a RED arm: it is a **negative control** (it must
hold before and after 098) or a **characterization pin** (it records today's behaviour), and it is never
in the Phase 1 list. The verify record keeps a table: witness | mutant or U | build rc | test rc | the
failing assertion's message, plus every other test the unfiltered run reports failing (collateral is
listed, not assumed).

**Where mutants run (`[const §XVI.6]`).** 098 is built in `~/Work/Programming/fixpp-parallel`, so
mutants run in a **third** worktree, created detached at the implementation head and removed at
close-out; never in the PR's tree, never in the main checkout:

```
H=$(git -C ~/Work/Programming/fixpp-parallel rev-parse HEAD)
df -h /mnt/wsl/fixppbuild                                   # headroom for a Debug tree first
git -C ~/Work/Programming/fixpp-parallel worktree add --detach /mnt/wsl/fixppbuild/fixpp-098-mutants "$H"
export CCACHE_DIR=/mnt/wsl/fixppbuild/ccache
cd /mnt/wsl/fixppbuild/fixpp-098-mutants
conan install . -pr conan/profiles/linux-clang-debug --build=missing -of build/linux-clang-debug
cmake --preset linux-clang-debug -DFIXPP_BUILD_PYTHON=ON     # bindings for W-10's M-11 / M-19 runs
cmake --build --preset linux-clang-debug -j2
# per mutant: apply, rebuild (rc must be 0), run the named binary unfiltered, record, `git checkout -- .`
```

## Arm table (plan Phase 1 and quickstart §1 are derived from it)

One row per arm. "Unfixed outcome" is what the arm's own assertion does on the unfixed tree. Plan
Phase 1 and quickstart §1 list exactly the rows whose unfixed outcome is **fails**; no other row is run
as a RED arm before production code.

| Arm | Compiles on unfixed tree? | Assertion | Unfixed outcome | RED source, or role |
|---|---|---|---|---|
| W-0 seeded rows | no (test-only, but written in plan Phase 2 against Phase 2's code; the header-store row joins in Phase 5) | reported set equals the seed's declared expected set | — | the seeded rows themselves; M-0 |
| W-0 identical pair | no (as above) | zero differences | — | control |
| W-1 rows | no | zero differences vs the complete reference; no counter-origin refusal; step counter = prediction | — | M-1, M-2, M-17 |
| W-2 rows, side against side and against the census | no | zero differences vs the patched base; no counter-origin refusal; step counter = prediction | — | M-3; a zero- or two-match hunk fails the cell; W-0's census row |
| W-2 wrap probe (FIX50SP2) | yes | records `component_fields` of the first component past the wrap point against its raw members | either | probe, recorded in the verify record (research R-10); not a RED arm |
| W-3 rows | no | the named exception, its message naming the key, cap, table or counter origin as applicable | — | M-4, M-5, M-6, M-7, M-12 (by the side-table fit on the doubling rows, by the counter on the empty-chain rows), M-13, M-14, M-16, M-17, M-18 (per row and format, below) |
| W-4 rows | no | dispatch matches the direct loader | — | M-8 |
| W-5 overlay session | no (`load_any_with_overlay`) | agrees with the edited session on every frame; each accepted frame's scenario read holds | — | M-8 |
| W-5 edited session | yes | accepts or refuses each frame as the edited dictionary dictates; each accepted frame's scenario read holds | passes | characterization pin (a complete edited file already works today) |
| W-5 base session | yes | today's outcome on each frame | passes | negative control |
| W-6 edited dictionary, scalar 5020 | yes | 5020 before the first body field | **fails** (5020 sent in the body) | **U**; M-9 after the fix |
| W-6 edited dictionary, custom counted pair in the header | yes | Length and Data both before the first body field, adjacent | **fails** (pair sent in the body) | **U**; M-9 after the fix |
| W-6 replay of the 5020 message | yes | 5020, 43 and 122 all inside the header | **fails** (5020 in the body) | **U** for the 5020 half; the 43/122 half is a pin |
| W-6 overlay dictionary | no | as the edited-dictionary arm | — | M-9 |
| W-6 existing header-order witnesses | yes | unchanged bytes | passes | negative control |
| W-7 `is_header_tag` vs the raw walk | no | equal per vendored dictionary | — | M-9 |
| W-7 union with the production fixed set | no (`send_header_tags.hpp` is new) | union equals the fixed set | — | characterization pin of research R-7; its instrument is proven by the seeded FIX44 copy, which MUST make it fail |
| W-8 AS-1 (no overlay: dictionary filled, `open()` succeeds) | yes | `config.dictionary` set, session opens | **fails** (null; `invalid_session_config`) | **U** |
| W-8 AS-2, shared pointer, AS-5 | yes | merged dictionary per session as stated | **fails** (the key is refused as deferred) | **U**; M-10 for the stated reason |
| W-8 AS-3, AS-4, value checks, two origins, no `[dictionary]` | yes | the stated reason class at the stated key path | **fails** (`recognized_not_yet_supported_step2` instead) | **U** |
| W-8 rewritten T018 / T020 / T028 cells, `T020_DeferredSetExactCoverage` over the remaining keys | yes | the new outcomes | **fails** (the old outcomes) | **U** |
| W-8 `T020_MissingRequired_Dictionary` | yes | unchanged | passes | negative control (N-3) |
| W-9 cap rows (the thunk's cap row; the mapping function fed `overlay_limit_error`) | no | 201 (and NULL from the thunk) | — | M-11 |
| W-9 bad-alloc rows (the mapping function fed `xml_oom_error` and a bare `std::bad_alloc`) | no | 202 | — | M-15 |
| W-9 code-200 rows (the thunk's conflict row; the mapping function fed `xml_parse_error`, `overlay_conflict_error`, a `std::runtime_error` and a non-`std::exception`) | no | 200 (and NULL from the thunk) | — | M-20 |
| W-9 NULL-argument rows | no | `FIXPP_ERR_NULL_HANDLE` | — | control |
| W-9 status-mapping rows (`dictionary_pure_tests`) | no (the shared status-mapping function is new) | `status_out_of_memory` → `xml_oom_error`; another failed status → the format's parse error | — | M-21 |
| W-9 pure handle row (the handle is accepted by `fixpp_session_config_set_dictionary` and `fixpp_session_open` on an unstarted engine) | no | both calls return `FIXPP_ERR_OK` | — | control: a base-only handle passes it too; the live arm is the discriminating row |
| W-9 version pin (`version_test.cpp`) | yes | the version is 1.13 | **fails** (MINOR is 12) | **U** |
| W-9 symbol golden (`fixpp_capi_symbols.txt` gains the symbol) | yes | the library's exported set equals the golden | **fails** (the symbol is absent from the library) | **U** |
| W-9 live arm (raw `fixpp_session_send` payload) | no | the counted 5011 value read intact through `fixpp_msg_get_bytes` | — | **M-19** |
| W-10 import surface | yes | the exact set includes `dict_load_from_xml_with_overlay` | **fails**: the exact-set assertion, with that name as its only difference | **U** |
| W-10 inbound read | no (the function is absent) | `msg_get_string(5011)` equals the full counted value | — | **M-19** |
| W-10 cap row | no | `ValidatorError` with `.code == 201` | — | M-11 |
| W-11 fuzz | no | only `dict::` errors escape (Orchestra by derived type); abort on a counter-origin refusal; counter = prediction on every load | — | M-7 (cycle seeds), M-12 (empty-chain seeds) and M-17 against the harness |

## Comparator self-test (runs first; a zero is trusted only after it)

**W-0** `tests/dictionary/dictionary_equivalence_test.cpp`. The comparator declares its compared stores
as data (research R-10 §3). Each seeded pair of valid small dictionaries declares its expected reported
set. W-0 asserts that the comparator reports exactly that set. The union of all expected sets equals
the seedable part of the declared list; adding the reason-only entries that a version-matched overlay
cannot change yields the full list. Separable entries have separating seeds. Every coupled or derived entry has
a stated reason naming its invariant, the entry it cannot differ independently from, and the query
that observes it; this covers the group and enum bit caches, both pair-map directions and the
`Dictionary`/`table_view` twins. A context-store seed leaves the bare and exact stores equal (for
example the context `required_members` seed has equal bare `group_required_members`). Also seeded, for
the key sources: an unused extra component on one side; a
context record that differs only where the bare value would mask it; and a census mismatch (the census
of a document that differs from the loaded one in one component member). A pair of identical loads MUST
report none. **RED**: the seeded rows are the RED arms; mutant **M-0** (the comparison of one declared
entry, the context `required_members` one, removed while the entry stays listed) turns that entry's row
RED.

## Loading and merging

**W-1 Equivalence, every addition kind (small bases).** `tests/dictionary/overlay_equivalence_test.cpp`,
fixtures `tests/dictionary/fixtures/098/{qf,orch}/{base,overlay,edited}.xml` (hand-written). For each
format: `load_with_overlay_from_string(base, overlay)` vs `load_from_string(edited)` → zero differences.
The overlays cover every FR-003/FR-004 kind: new field, enum value on an existing field (and on an
existing Orchestra code set), new component, new message (with a required field, a new group, a new
component), field added to an existing message, member added to an existing group (inline and inside a
component), field and group added to the header, Required promotion on a message, component and group
child, custom Length+Data pair, an enum value on a base field with no values (N-2: the domain closes;
`enum_valid` refuses a value not listed), a new container the base lacks (R-3), a member appended to a
group whose existing members emit nothing (an empty component), which becomes the delimiter as in the
edited file (spec Edge Cases), and a new DATA after a base LENGTH whose base pair forms earlier in
`detect_length_pairs`'s visit order (loads, R-3).
Every row loads through research R-5's read seam and asserts no counter-origin refusal and that the
step counter equals step 6's prediction. **RED**: **M-1** (the splice prepends a new group member instead of appending) and **M-2**
(the splice skips enum-value additions); each turns its row RED with the comparator naming the store.
**M-17** (one extra `expand_field_list` call in `finalize`) turns every row RED through the distinct
counter origin.

**W-2 Equivalence on the vendored bases.** Same file. FIX44, FIX50SP2 and the Orchestra file, each with an
overlay under `tests/dictionary/fixtures/098/real/` and a hand-written insertion patch (research R-10:
hunks with a multi-line context block that must match exactly once, insertion before the block's last
line), applied by a line-based test helper. `load_with_overlay(base, overlay)` vs
`load_from_string(patched base)` → zero differences, side against side and, for `component_fields` /
`group_fields`, against the raw census under R-10's wrap condition. Kind coverage per FR-017; the FIX44
overlay also extends more than 64 existing messages (N-7: accepted, since the entry caps count new
definitions). Each row also asserts no counter-origin refusal and step counter = prediction (research
R-5). The FIX50SP2 wrap probe
(research R-10) runs here and is recorded, not asserted; where a base side table does not fit, the
overlay's appended definitions past the wrap point are compared side against side only, and the verify
record says so.
**RED**: (a) a deliberately non-applying patch (a context block that matches zero places, and one that
matches two) MUST fail the cell before any comparison; (b) **M-3** (header/trailer additions are
dropped) turns the FIX44 and Orchestra rows RED.

**W-3 Refusals.** `tests/dictionary/overlay_refusal_test.cpp`, table-driven. The table is the
product of the refusal kinds below × {QuickFIX XML, Orchestra}. Every cell is a row, or N/A with the
reason written in the table (for example "`<fields>` adjacency: Orchestra has none"); the verify record
lists the N/A cells. Each row asserts the exception's exact type for its format (an Orchestra schema
error is `orchestra_parse_error`, caught by the derived type, not by `xml_parse_error`) and that its
message names the key, cap or table as applicable. M-17's counter refusal instead names the distinct
counter origin.
- **R-4 conflicts:** identifying attribute changed (type; name; msgtype name; Orchestra message `id`,
  code-set `id` / `type`); field name already used by another number; an Orchestra code set with a new
  `id` and an existing name (N-6; XML N/A, no code sets); required relaxed; redescribed enum value or
  code; `required="y"`, `required="false"` and `presence="require"` (A-013); duplicate within the
  overlay; two root containers of one name; version mismatch; overlay of the other format; a root child
  outside data-model §A.1's list, such as `fixr:datatypes` (N-10); an Orchestra group whose
  `fixr:numInGroup` `id` differs, and a new group with none or two (N-9; XML N/A, the NumInGroup is the
  group's `name`).
- **One tag twice at one level (research R-3):** a child redeclared directly that the parent reaches
  through a component, including through a two-level component chain (N-2); a new component reference
  that reaches a tag the parent already declares (N-7); for XML, a header addition whose tag a message
  body already has.
- **Repeated base key (G098-R2-005):** one row per match-key kind in research R-3's two tables that the
  base loader admits repeated (a repeated MsgType, a repeated `<field>` name, a repeated direct child;
  Orchestra a repeated `msgType` or direct child). A kind the loader itself refuses repeated is N/A
  with that reason (for example a duplicate Orchestra field, component or group `id`, or code-set name);
  the overlay names the key, and the row asserts both base occurrences in the message. A control: the
  same base with an empty overlay loads.
- **Length+Data relation (research R-3, step 8a).** One row per cell; "load" rows assert the load
  succeeds and the relation is unchanged. "Earlier" and "later" are `detect_length_pairs`'s visit order
  (`<fields>`, header, trailer, messages, components, groups):

  | Site | new Data → unpaired base Length | new Data → base Length paired earlier | new Data → base Length paired later | new Length → base Data |
  |---|---|---|---|---|
  | `<fields>` | refuse | N/A: a Length `<fields>` pairs is followed by its Data, so it is never last | refuse (base pair lost) | N/A: a redeclared base Data is a match, not an append |
  | strict container (component, group) | refuse | load | refuse | refuse |
  | non-strict container (message, header, trailer), base group between | refuse | load | refuse | refuse |
  | non-strict container, overlay component between | refuse | load | refuse | refuse |
  | Orchestra `lengthId` | refuse | refuse, as `overlay_conflict_error` (not the loader's `orchestra_parse_error`); Orchestra has no visit order | (same cell) | N/A: a base Data's `lengthId` is fixed by R-3's equal-`lengthId` rule |

- **R-5 guards:** bytes at cap + 1 through both the path and the string form, depth 33, each entry cap +
  1 counting new definitions (new messages = cap + 1, N-7), the reference cap + 1, a DOCTYPE, an unknown
  reference, a cycle that closes through a base component (XML `xml_parse_error`; Orchestra
  `orchestra_parse_error`).
- **Expansion bomb (A-001):** XML, the doubling overlay
  `C_n = { component C_{n-1}; group NoG_n { field F_n; component C_{n-1} } }` over the 63 levels the caps
  allow, rooted at the base's `Instrument`; Orchestra, `C_n = { componentRef C_{n-1}; groupRef G_n }` with
  `G_n = { numInGroup; fieldRef F_n; componentRef C_{n-1} }`. Both referenced by no message. A third row
  per format: two empty-bodied component chains, each level referencing both chains' previous level,
  which emit nothing (research R-5). Each MUST fail with `overlay_limit_error` whose origin, read
  through research R-5's seam, is the pre-walk budget, with the budget's message. That origin shows the
  pre-walk refused before the build started (SC-003). No wall-clock bound is asserted: timing is W-12's
  controlled measurement, not a correctness bound.
- **Header expansion (G098-R2-003; XML only, Orchestra N/A: its header is component 1024, charged
  standalone):** a base with an empty `<messages>`, whose overlay puts nested groups under `<header>`
  that each reference one large component. It MUST load, with no counter-origin refusal, step counter = prediction and the
  declared header store holding the component's tags (the visited-set walk, research R-7).
- **Side-table fit (N-3):** on a small base whose tables fit, an overlay that grows an early-declared
  component until a later base component's start passes 65535; an overlay component whose own entry
  count passes 65535; for groups, a large group with a low NumInGroup tag that pushes base groups past
  65535. Each MUST fail with `overlay_limit_error` naming the table. A control: the same shape kept just
  inside the fit loads.
- **Guard order (N-1):** an overlay that closes a cycle through a base component **and** redeclares a
  child that the parent reaches through that component. It MUST fail with the cycle error of its format,
  not with the redeclare conflict and not by recursion.

FR-008 has no row: it holds by construction (spec FR-008), and no mutant short of global state can make
two fresh base loads differ. **RED**, each mutant run once per format where its rows exist: **M-4**
(conflict checks removed: the conflict rows load), **M-5** (byte cap removed), **M-6** (`parse_doctype`
dropped: the DOCTYPE row loads), **M-7** (cycle check removed: the cycle row no longer fails with the
cycle error; it loops, overflows, or fails differently, for example with a counter-origin refusal where
the walk's prediction stays finite; the RED is the cycle row's cycle-error assertion, the run uses
`linux-clang-asan` so that an overflow is reported, and the verify record states how the run ended),
**M-12** (the step 6 budget comparison removed; step 6's side-table fit check stays). The RED differs
per bomb row, and the verify record's message column confirms which check fired on each:
  - a **doubling** row: on a base whose side tables fit, the chain's emitted entries pass 65535, so
    step 6's side-table fit refuses it before the build; the row goes RED with "side-table origin,
    expected budget origin" and the table's message instead of the budget's;
  - an **empty-chain** row: it emits nothing, so it fits and the build is reached; the counter, armed at
    the smaller of the prediction and the base's total plus the budget (research R-5), refuses once the
    build passes the base's total plus the budget; the row goes RED with "counter origin, expected
    budget origin";
  - both end in bounded time, in the Debug tree.

**M-13** (guard steps 6 and 7 swapped: the guard-order row reports the redeclare
conflict, or crashes, instead of the cycle error), **M-14** (step 5's entry and reference caps removed:
those rows load), **M-16** (step 8a replaced by the previous draft's adjacency rule: the interposed,
paired-later and new-Length refuse rows and the Orchestra rows go RED, and the load rows stay GREEN,
since that rule never refused a paired base Length), **M-17**
(one extra `expand_field_list` call in `finalize`: the header-expansion row is refused with the counter
origin),
**M-18** (the side-table fit check removed: those rows load).

**W-4 Load-any dispatch.** Same file as W-1. `load_any_with_overlay` on each format matches the loader
called directly; a mixed-format pair is refused. **RED**: **M-8** (`load_any_with_overlay` ignores the
overlay) turns the match rows RED.

## Wire behaviour

**W-5 Inbound (US1 AS-1..AS-6, SC-002).** `tests/session/test_098_dialect_overlay_inbound.cpp` (joins
`session_pure_tests`). Three sessions with `validate_inbound_messages = true`: FIX44 base alone, FIX44 base +
the W-2 overlay loaded through `dict::load_any_with_overlay` (so M-8 reaches it), FIX44 patched (the
edited reference). One frame per scenario: custom field on 35=D; custom member inside Parties; private
MsgType `U1` with and without its required field; custom Length+Data value containing SOH;
Optional→Required promotion missing; OrdType `Z`. The enum-closure frame (N-2) uses its own small
overlay fixture and its own pair of sessions (overlay and edited), so that closing a field does not
reach the other frames: that overlay lists a first value on Text(58), which FIX44.xml declares with no
`<value>` (re-check with a grep before relying on it), and a frame carrying a Text value it does not
list is refused there and accepted on the base session. Assert: overlay session and edited session agree on every frame (accept,
or the same Reject 373 and 371); the base session's outcome is the one recorded as today's. On every
accepted frame, the overlay and edited sessions also assert, from the application callback, the read
the scenario names: AS-1, the value of 5001 by tag; AS-2, 5002 read inside the Parties entry through the
group view (the group read is the assertion, not a top-level read); AS-3, `U1` reaches the callback;
AS-4, the 5011 value equals the full counted bytes, SOH included; AS-6, 40 reads `Z`. Accept/reject
alone does not show these: the validator's `table_view` and the layout the callback reads are two
consumers, and a defect in the second would leave both sessions agreeing (FR-018). **RED**:
**M-8** for the overlay arm. The base-session arm is a **negative control** and the edited-session arm a
**characterization pin**: both pass on the unfixed tree and after the fix (arm table).

**W-6 Send (US1 AS-7, FR-022, SC-008).** `tests/session/test_098_header_send.cpp` in
`session_pure_tests`. A session whose
dictionary declares header tag 5020 (a complete edited FIX44, loaded with the existing `load_from_string`,
so the witness compiles on the unfixed tree) sends a message with 5020 after a body field; the frame
carries 5020 before the first body field. A second edited-dictionary arm declares a custom pair
5030 (LENGTH) / 5031 (DATA), both non-standard, in the header and sends it after a body field: both
tags land in the header, adjacent, the Data value following its Length's class (A-008; the first
header-class pair that goes through `session_hooks(inbound_tv_)` rather than the standard table). A
third arm uses base + overlay (new API). A fourth arm replays the 5020 message through ResendRequest
and asserts 5020, 43 and 122 all sit inside the header (`kReplayHeaderTags` unchanged, research R-7).
**Byte identity:** the existing header-order witnesses (`tests/session/test_resend_answer_field_order.cpp`
`SendPayloadTagTest.*`, `ResendAnswer*`; `tests/session/test_091_data_send.cpp` `HeaderPairSend091.*`,
`DataSend091.*`) pass unchanged. **RED**: **U** for both edited-dictionary arms and the replay arm's 5020
half (the unfixed tree sends the custom tags in the body); **M-9** (`as_table_view` skips the header
store) for every arm.

**W-7 Header store per vendored dictionary.** Two halves.
- `tests/dictionary/header_tags_test.cpp`, the standalone `dictionary_098_header_census_test`: enumerate
  the vendored QuickFIX XML files plus the vendored Orchestra file by the existing dictionary-census
  recipe; for each result, `is_header_tag`
  answers true exactly for the tags of an independent raw-XML header walk in the test (QuickFIX
  `<header>` expansion; Orchestra component 1024), and false for 5020. **RED**: **M-9**.
- `tests/session/test_098_header_send.cpp` (`session_pure_tests`): the same raw walk's tags, unioned with
  the production fixed set read from `src/session/send_header_tags.hpp` (research R-7), equal the fixed
  set. It is a characterization pin of R-7's measurement, not a RED arm. Its instrument is proven by a
  seeded FIX44 copy with 5020 in `<header>`, which MUST make the union assertion fail.

## Configuration

**W-8 TOML (US3 AS-1..AS-5, SC-004).** `tests/config/test_098_dialect_overlay.cpp` (joins
`config_045_tests`) and fixtures under `tests/config/fixtures/098/`. A session without an overlay gets
`engine.dictionaries.front()` and opens with no host code (AS-1, SC-004); a session with one gets the
merged dictionary (pointer differs from the base, a custom tag is valid); two sessions naming one path
share one pointer; `[default]` inheritance and override (AS-5); a missing file and a conflicting overlay
each report at the key path where the value was written (AS-3); root scope is refused (AS-4). Value and
origin rows (A-005, N-3): a non-string value (`malformed_value`); an empty string (`empty_required`);
`[default]` and `session[1]` naming one failing file give two diagnostics, one at each key path; a
`[[session]]` with `dialect_overlay` and no `[dictionary]` gives "dialect_overlay requires [dictionary]"
at that key path, and does not crash. `LoadNegativeBattery.T020_MissingRequired_Dictionary` stays green
unchanged. The existing T020 cells (`test_load_deferred_surface.cpp`), `T018_Step2DialectOverlay`
(`test_load_negative_battery.cpp`) and `T028_DialectOverlayDeferred` (`test_load_selectors.cpp`) are
rewritten to the new outcomes, and `T020_DeferredSetExactCoverage` keeps every other deferred key.
**RED**: **U** for every row except `T020_MissingRequired_Dictionary` (arm table: on the unfixed tree
`config.dictionary` is null, so AS-1's `open()` fails with `invalid_session_config`, and every
`dialect_overlay` row gets `recognized_not_yet_supported_step2` instead of its stated outcome);
**M-10** (the resolver assigns the base to every session) for AS-2/AS-5, the reason those rows state.

## C ABI and Python

**W-9 C ABI.** Two executables.
- **Pure rows**, `tests/capi/dict_overlay_capi_test.cpp` (joins `capi_pure_tests` in plan phase 6; it
  starts no engine): cap → 201 and NULL; conflict → 200 and NULL; NULL arguments →
  `FIXPP_ERR_NULL_HANDLE`; a control that the returned handle is accepted by
  `fixpp_session_config_set_dictionary` and by `fixpp_session_open` on an engine that is never started
  (a base-only handle passes it too). The cap row uses an entry-cap overlay (new messages = cap + 1),
  not a byte-cap file. The bucket gains a compile definition, `FIXPP_098_FIXTURE_DIR`
  (= `tests/dictionary/fixtures/098`), because none of its data-directory definitions reaches those
  fixtures. The exception-to-code mapping function (research R-6, declared in
  `src/capi/capi_internal.hpp`) is fed each type directly: `overlay_limit_error` → 201; `xml_oom_error`
  and a bare `std::bad_alloc` → 202; `xml_parse_error`, `overlay_conflict_error` and a
  `std::runtime_error` → 200; a non-`std::exception` → 200. In the `dictionary_pure_tests`, research R-5
  step 2's status mapping, the one function both parses of an overlay call use (N-10), is fed
  `status_out_of_memory` (→ `xml_oom_error`) and one other failed status (→ the format's parse error);
  that both parses call it is structural, since no allocation-failure seam is added.
  `tests/capi/version_test.cpp` pins 1.13; `tests/abi/golden/fixpp_capi_symbols.txt` gains the symbol;
  `tools/capi_freeze.sha256` refreshed; `tools/check_capi_reentrancy.sh` passes.
- **Live arm**, `tests/capi/dict_overlay_capi_live_test.cpp`, the standalone
  `capi_098_dict_overlay_live_test` (labels `capi;098`; compile definitions `FIXPP_DICT_DATA_DIR` and
  `FIXPP_098_FIXTURE_DIR`; a `TIMEOUT` sized by the module's recipe for live tests, the slowest lane's
  time with headroom, as the comment on `capi_inbound_frame_dispositions` states it). Built on
  `capi_dict066_loopback_support.hpp`, as `dict066_group_membership_red_test.cpp` is: two C-ABI engines
  over plaintext loopback. Each session's dictionary is a handle that
  `fixpp_dict_load_from_xml_with_overlay(FIX44.xml, the W-2 FIX44 overlay)` returns, set with
  `fixpp_session_config_set_dictionary`, not the support header's base-dictionary seam. The sender
  passes a raw application payload to `fixpp_session_send` (a byte pointer and a length), as
  `dict066_group_membership_red_test.cpp` does: bytes written in the test that lead with
  `35=<application MsgType><SOH>`, carry no session framing tag, and carry the overlay's custom pair
  5010 (LENGTH, the byte count of the 5011 value) / 5011 (DATA). The payload is not built with
  `fixpp_msg_set_data`: under M-19 the sender's handle is base-only too, so that call returns
  `FIXPP_ERR_TYPE_MISMATCH` for 5011 (`pair_hooks(h).length_tag_for_data` returns 0,
  `src/capi/message_write.cpp`) and the arm would fail at the sender, not with the value mismatch.
  `fixpp_session_send` does no C-ABI-level check of the pair (`src/capi/session.cpp`), and on a
  base-only dictionary `Session::build_send_frame_` scans the payload as well-formed fields, none in its
  `kRefusedTags` (`src/session/session.cpp`), so the raw bytes reach the wire unchanged under M-19.
  The Data value holds an SOH followed by text shaped as a field, for example `AB<SOH>58=CD`, with 58 not
  otherwise in the frame. The registered callback reads 5011 with `fixpp_msg_get_bytes`, the reader
  091's `length_data_send_recv_test.cpp` uses for an inbound Data value (only its reader is taken from
  091, not its builder); the test asserts that it equals
  the full counted value.
  - *Why this observable.* A C-ABI session never validates inbound
    (`SessionConfig::validate_inbound_messages` defaults to `false`, and nothing in `src/capi/` sets it),
    and `fixpp_msg_get_string` / `fixpp_msg_get_bytes` read any top-level tag whatever the dictionary
    declares (both return `view->get(tag)`, `src/capi/message_read.cpp`). So a custom
    scalar tag reads the same on a base-only handle. Pairing follows the session's dictionary
    (`Session::parse_and_dispatch_` parses with a `Parser` built over `inbound_tv_`, fixpp#426), so on a
    base-only handle 5011 reads `AB`. The field-shaped tail keeps that frame well formed, so the RED is a
    value mismatch, not a garbled frame whose callback never fires.

**RED**: **M-11** (the limit branch maps to 200) turns the cap rows RED; **M-15** (`std::exception`
caught before `std::bad_alloc`) turns the bad-alloc rows RED; **U** for the version pin and the symbol
golden, which compile on the unfixed tree and fail there; **M-19** (`fixpp_dict_load_from_xml_with_overlay`
calls `load_any(base)` and drops the overlay) turns the live arm RED with the value mismatch, and the W-10
inbound row too; **M-20** (the mapping function's `std::exception` and `...` branches return
`FIXPP_ERR_CAPI_CONFIG_INVALID`, as `fixpp_dict_load_from_xml` does) turns the code-200 rows RED: the
thunk's conflict row and the mapping function's `xml_parse_error`, `overlay_conflict_error`,
`std::runtime_error` and non-`std::exception` rows; **M-21** (research R-5 step 2's status mapping with
its two branches swapped: `status_out_of_memory` → the format's parse error, any other failed status →
`xml_oom_error`) turns both status-mapping rows RED. The pure handle row and the NULL-argument rows are
controls, not RED arms.

**W-10 Python.** `bindings/python/tests/test_098_dialect_overlay.py`, in-tree only (run with
`PYTHONPATH=<build>/lib`, as `test_093_inbound_frame_dispositions.py` is). It has no wheel twin: the
overlay fixtures are not in the wheel. The wheel README's enforced allowlist names wheel files, and
`ci/assert-wheel-test-parity.py` derives its twins from the wheel side, so an in-tree file without a twin
needs no row (as `test_error_coverage.py` and `test_gil_release_canary.py` have none). Rows:
- **Inbound read:** `fixpp.dict_load_from_xml_with_overlay(FIX44.xml, the W-2 FIX44 overlay)`, set on an
  initiator session config. A raw TCP peer written in the test (the `test_093` pattern: it accepts the
  connection, reads the Logon and answers it) writes W-9's live-arm frame: the 5010/5011 pair, with the
  SOH and the field-shaped tail in the Data value. The callback registered with
  `session_register_callback` copies `msg_get_string(5011)` out, and the test asserts that it equals the
  full counted value. `msg_get_string` reads the same field as the C arm's `fixpp_msg_get_bytes` (both
  return `view->get(tag)`), and the value is ASCII, so the binding's UTF-8 decode keeps it. Python wraps no group reader (`fixpp.i` re-declares only the in-scope read
  functions of `message.h`), so this Length+Data value is the dictionary-only observable both surfaces
  can read. It is the same frame as W-9's live arm, so US4 AS-3 ("matches the C ABI") is witnessed on the
  inbound behaviour, not only on reachability.
- **Cap row:** an entry-cap overlay raises `ValidatorError` with `.code == 201`. The class alone is not
  the assertion: `ValidatorError` covers every code from 200 to 299, so it stays green when the limit
  branch maps to 200.
- **Import surface:** `bindings/python/tests/wheel/test_import_surface.py`'s exact set includes
  `dict_load_from_xml_with_overlay` (a suite-native row, already on the allowlist).

**RED**: **U** for the import-surface row. The recorded failure MUST be the exact-set assertion whose only
difference is the missing `dict_load_from_xml_with_overlay`; an ImportError, a `fixpp` imported from
outside the build's `lib/`, or any other difference is RED for the wrong reason (stop rule). **M-19** for
the inbound read (the value reads `AB`); **M-11** for the cap row (`.code` is 200).

## Fuzz, performance, records

**W-11 Fuzz (`[const §VII.7]`).** `tests/fuzz/fuzz_dict_overlay.cpp` with a seed corpus
(`tests/fuzz/corpus/dict_overlay/`): the W-1 overlays, the W-3 refusal overlays, a DOCTYPE overlay.
Every exception escaping the harness must be a `dict::` error type; the Orchestra half catches
`orchestra_parse_error` by its derived type, as `tests/fuzz/fuzz_orchestra_loader.cpp` does (research
R-11). The harness aborts on a counter-origin refusal even though its public type is
`overlay_limit_error`; on every input that loads, it also aborts if the step counter differs from step
6's prediction. The seeds include the expansion bombs and the empty-component chain of both formats and an
Orchestra cycle. CI replays the seeds (`linux-clang-asan`); the ten-minute run is `/speckit-verify`'s
fuzz step. **RED**: **M-7** re-run against the harness with each format's cycle seed, **M-12** with
each format's empty-chain seed (the harness aborts on the counter origin; under M-12 a doubling seed is
refused by step 6's side-table fit, an accepted `dict::` error, so it never reaches the counter), and
**M-17** with a valid overlay (the
harness aborts on the counter origin).

**W-12 Performance (SC-005, `[const §VIII.2]`).** Measured in the verify record, not a ctest:
`load_with_overlay(FIX50SP2, the named at-cap fixture overlay)` vs `load(FIX50SP2)`, ratio ≤ 1.5, same
machine, median of ≥ 5 runs, with the fixture's expansion count (research R-5 step 6) recorded; the send path's paired bench (base vs candidate) shows no regression past the
provisional band for a session on a vendored dictionary.

**W-13 Records.** The parent repo's `.claude/scripts/check_bl_delta.py --root <tree>` (data-model §D),
the brain freshness checks (`.github/workflows/brain-freshness.yml`'s steps, run locally) and the
parent's `research/G19-fix-fpml-iso20022/tools/brain-external-sweep.sh`, the parent's
`.claude/scripts/check-comment-claims.py --root <tree> --base origin/main`, the line-citation gate over
the bundle, the records inventory recipe (data-model §G) with its seeded positive, and a positive-presence
check for each "Updated (098-dialect-overlay)" note the inventory's dispositions call for (seeded
negative: the check fails on a copy with one note removed).
