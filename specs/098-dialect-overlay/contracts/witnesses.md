# Contract: 098 witnesses and their RED arms

Every witness names its file, what it asserts, and how it is shown RED **for its stated reason**. All
gtest witnesses join existing grouped binaries (`[const §VII.8]`) and carry ctest label `098`.

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
cmake --preset linux-clang-debug && cmake --build --preset linux-clang-debug -j2
# per mutant: apply, rebuild (rc must be 0), run the named binary unfiltered, record, `git checkout -- .`
```

## Arm table (plan Phase 1 and quickstart §1 are derived from it)

One row per arm. "Unfixed outcome" is what the arm's own assertion does on the unfixed tree. Plan
Phase 1 and quickstart §1 list exactly the rows whose unfixed outcome is **fails**; no other row is run
as a RED arm before production code.

| Arm | Compiles on unfixed tree? | Assertion | Unfixed outcome | RED source, or role |
|---|---|---|---|---|
| W-0 seeded rows | no (test-only, but written in plan Phase 2 against Phase 2's code; the header-store row joins in Phase 5) | a difference naming the store | — | the seeded rows themselves; M-0 |
| W-0 identical pair | no (as above) | zero differences | — | control |
| W-1 rows | no | zero differences vs the complete reference | — | M-1, M-2 |
| W-2 rows, side against side and against the census | no | zero differences vs the patched base | — | M-3; a zero- or two-match hunk fails the cell; W-0's census row |
| W-2 wrap probe (FIX50SP2) | yes | records `component_fields` of the first component past the wrap point against its raw members | either | probe, recorded in the verify record (research R-10); not a RED arm |
| W-3 rows | no | the named exception, its message naming the key | — | M-4, M-5, M-6, M-7, M-12, M-13, M-14 (per row, below) |
| W-4 rows | no | dispatch matches the direct loader | — | M-8 |
| W-5 overlay session | no (`load_any_with_overlay`) | agrees with the edited session on every frame | — | M-8 |
| W-5 edited session | yes | accepts or refuses each frame as the edited dictionary dictates | passes | characterization pin (a complete edited file already works today) |
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
| W-9 rows | no | codes per research R-6 | — | M-11, M-15 |
| W-10 import surface | yes | the exact set includes `dict_load_from_xml_with_overlay` | **fails** (name absent) | **U** |
| W-10 other rows | no (the function is absent) | as W-9 | — | M-11 for the cap row |
| W-11 fuzz | no | only `dict::` errors escape | — | M-7 and M-12 against the harness |

## Comparator self-test (runs first; a zero is trusted only after it)

**W-0** `tests/dictionary/dictionary_equivalence_test.cpp`. For each store category the comparator
claims (research R-10, "Seeded rows": a field type, a valid tag, a required tag, an enum value, an enum order, a
component member, a component id, a group member, a group delimiter, an exact-store entry, a
context-store member, a Length+Data pair, a header tag, a message, a field name), one seeded pair of
small dictionaries differing in exactly that store: the comparator MUST report a difference naming that
store. Also seeded: an unused extra component on one side; a context record that differs only where the
bare value would mask it; a bare-only `group_required_members` difference; and a census mismatch (the
census of a document that differs from the loaded one in one component member). A pair of identical
loads MUST report none. **RED**: the seeded rows are the RED arms; mutant **M-0** (the comparator skips
the context stores) turns the context-store row RED.

## Loading and merging

**W-1 Equivalence, every addition kind (small bases).** `tests/dictionary/overlay_equivalence_test.cpp`,
fixtures `tests/dictionary/fixtures/098/{qf,orch}/{base,overlay,edited}.xml` (hand-written). For each
format: `load_with_overlay_from_string(base, overlay)` vs `load_from_string(edited)` → zero differences.
The overlays cover every FR-003/FR-004 kind: new field, enum value on an existing field (and on an
existing Orchestra code set), new component, new message (with a required field, a new group, a new
component), field added to an existing message, member added to an existing group (inline and inside a
component), field and group added to the header, Required promotion on a message, component and group
child, custom Length+Data pair, an enum value on a base field with no values (N-2: the domain closes;
`enum_valid` refuses a value not listed), a new container the base lacks (R-3). **RED**: **M-1** (the
splice prepends a new group member instead of appending) and **M-2** (the splice skips enum-value
additions); each turns its row RED with the comparator naming the store.

**W-2 Equivalence on the vendored bases.** Same file. FIX44, FIX50SP2 and the Orchestra file, each with an
overlay under `tests/dictionary/fixtures/098/real/` and a hand-written insertion patch (research R-10:
hunks with a multi-line context block that must match exactly once, insertion before the block's last
line), applied by a line-based test helper. `load_with_overlay(base, overlay)` vs
`load_from_string(patched base)` → zero differences, side against side and, for `component_fields` /
`group_fields`, against the raw census under R-10's wrap condition. Kind coverage per FR-017; the FIX44
overlay also extends more than 64 existing messages (N-7: accepted, since the entry caps count new
definitions). The FIX50SP2 wrap probe (research R-10) runs here and is recorded, not asserted.
**RED**: (a) a deliberately non-applying patch (a context block that matches zero places, and one that
matches two) MUST fail the cell before any comparison; (b) **M-3** (header/trailer additions are
dropped) turns the FIX44 and Orchestra rows RED.

**W-3 Refusals.** `tests/dictionary/overlay_refusal_test.cpp`, table-driven, one row per refusal. Each row
asserts the exception type and that its message names the key or cap.
- **R-4 conflicts:** type, name, msgtype name, required relaxed, redescribed enum value, child redeclared
  directly that the parent reaches through a component, duplicate within the overlay, two root containers
  of one name, version mismatch, overlay of the other format; `required="y"` and `required="false"` on a
  base-Required child, and `presence="require"` (A-013); an Orchestra code set with a new `id` and an
  existing name (N-6); an overlay DATA field right after a base LENGTH field (N-4); a root child outside
  data-model §A.1's list, such as `fixr:datatypes` (N-10).
- **R-5 guards:** bytes at cap + 1 through both the path and the string form, depth 33, each entry cap + 1
  counting new definitions (new messages = cap + 1, N-7), the reference cap + 1, a DOCTYPE, an unknown
  reference, a cycle that closes through a base component.
- **Expansion bomb (A-001):** the doubling overlay
  `C_n = { component C_{n-1}; group NoG_n { field F_n; component C_{n-1} } }` over the 63 levels the caps
  allow, rooted at the base's `Instrument`, referenced by no message. It MUST fail with
  `overlay_limit_error` naming the budget, and the call MUST return within ten times the same binary's
  load of the base alone, timed in the same test (the bound is derived from the competing work, not
  fixed).
- **Guard order (N-1):** an overlay that closes a cycle through a base component **and** redeclares a
  child that the parent reaches through that component. It MUST fail with the cycle error, not with the
  redeclare conflict and not by recursion.

FR-008 has no row: it holds by construction (spec FR-008), and no mutant short of global state can make
two fresh base loads differ. **RED**: **M-4** (conflict checks removed: the conflict rows load), **M-5**
(byte cap removed), **M-6** (`parse_doctype` dropped: the DOCTYPE row loads), **M-7** (cycle check
removed: the cycle row no longer fails with the cycle error; it loops, overflows, or fails differently;
run under `linux-clang-asan`, whose report is the recorded RED), **M-12** (the expansion budget removed:
the bomb row exceeds its time bound or exhausts memory; run under `linux-clang-asan`), **M-13** (guard
steps 6 and 7 swapped: the guard-order row reports the redeclare conflict, or crashes, instead of the
cycle error), **M-14** (step 5's entry and reference caps removed: those rows load).

**W-4 Load-any dispatch.** Same file as W-1. `load_any_with_overlay` on each format matches the loader
called directly; a mixed-format pair is refused. **RED**: **M-8** (`load_any_with_overlay` ignores the
overlay) turns the match rows RED.

## Wire behaviour

**W-5 Inbound (US1 AS-1..AS-6, SC-002).** `tests/session/test_098_dialect_overlay_inbound.cpp` (joins the
session binary). Three sessions with `validate_inbound_messages = true`: FIX44 base alone, FIX44 base +
the W-2 overlay loaded through `dict::load_any_with_overlay` (so M-8 reaches it), FIX44 patched (the
edited reference). One frame per scenario: custom field on 35=D; custom member inside Parties; private
MsgType `U1` with and without its required field; custom Length+Data value containing SOH;
Optional→Required promotion missing; OrdType `Z`. The enum-closure frame (N-2) uses its own small
overlay fixture and its own pair of sessions (overlay and edited), so that closing a field does not
reach the other frames: that overlay lists a first value on Text(58), which FIX44.xml declares with no
`<value>` (re-check with a grep before relying on it), and a frame carrying a Text value it does not
list is refused there and accepted on the base session. Assert: overlay session and edited session agree on every frame (accept,
or the same Reject 373 and 371); the base session's outcome is the one recorded as today's. **RED**:
**M-8** for the overlay arm. The base-session arm is a **negative control** and the edited-session arm a
**characterization pin**: both pass on the unfixed tree and after the fix (arm table).

**W-6 Send (US1 AS-7, FR-022, SC-008).** `tests/session/test_098_header_send.cpp`. A session whose
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
- `tests/dictionary/header_tags_test.cpp`: for each of the ten vendored dictionaries, `is_header_tag`
  answers true exactly for the tags of an independent raw-XML header walk in the test (QuickFIX
  `<header>` expansion; Orchestra component 1024), and false for 5020. **RED**: **M-9**.
- `tests/session/test_098_header_send.cpp` (the session binary): the same raw walk's tags, unioned with
  the production fixed set read from `src/session/send_header_tags.hpp` (research R-7), equal the fixed
  set. It is a characterization pin of R-7's measurement, not a RED arm. Its instrument is proven by a
  seeded FIX44 copy with 5020 in `<header>`, which MUST make the union assertion fail.

## Configuration

**W-8 TOML (US3 AS-1..AS-5, SC-004).** `tests/config/test_098_dialect_overlay.cpp` (joins the config
binary) and fixtures under `tests/config/fixtures/098/`. A session without an overlay gets
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

**W-9 C ABI.** `tests/capi/dict_overlay_capi_test.cpp` (joins the capi binary): happy path → handle usable
with `fixpp_session_config_set_dictionary`, a session opens and reads a custom tag; cap → 201 and NULL;
conflict → 200 and NULL; NULL arguments → `FIXPP_ERR_NULL_HANDLE`. The exception-to-code mapping
function (research R-6, declared in `src/capi/capi_internal.hpp`) is fed each type directly:
`overlay_limit_error` → 201; `xml_oom_error` and a bare `std::bad_alloc` → 202; `xml_parse_error`,
`overlay_conflict_error` and a `std::runtime_error` → 200; a non-`std::exception` → 200.
`tests/capi/version_test.cpp` pins 1.13; `tests/abi/golden/fixpp_capi_symbols.txt` gains the symbol;
`tools/capi_freeze.sha256` refreshed; `tools/check_capi_reentrancy.sh` passes. **RED**: **M-11** (the
limit branch maps to 200) turns the cap rows RED; **M-15** (`std::exception` caught before
`std::bad_alloc`) turns the bad-alloc rows RED.

**W-10 Python.** `bindings/python/tests/test_098_dialect_overlay.py` (and its wheel mirror where the
suite mirrors): load with overlay, open a session config, typed error on a cap overlay;
`test_import_surface.py`'s exact set includes `dict_load_from_xml_with_overlay`. **RED**: **U** for the
import-surface row (the name is absent on the unfixed tree).

## Fuzz, performance, records

**W-11 Fuzz (`[const §VII.7]`).** `tests/fuzz/fuzz_dict_overlay.cpp` with a seed corpus
(`tests/fuzz/corpus/dict_overlay/`): the W-1 overlays, the W-3 refusal overlays, a DOCTYPE overlay.
Every exception escaping the harness must be a `dict::` error type. The seeds include the expansion
bomb (A-001). CI replays the seeds (`linux-clang-asan`); the ten-minute run is `/speckit-verify`'s fuzz
step. **RED**: **M-7** re-run against the harness with the cycle seed, and **M-12** with the bomb seed
(the harness must crash or exhaust its limits).

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
