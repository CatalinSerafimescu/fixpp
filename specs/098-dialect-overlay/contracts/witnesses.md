# Contract: 098 witnesses and their RED arms

Every witness names its file, what it asserts, and how it is shown RED **for its stated reason**. All
gtest witnesses join existing grouped binaries (`[const §VII.8]`) and carry ctest label `098`.

**RED arms.** A witness that calls a new symbol does not compile on the unfixed tree, and a compile
failure is RED for the wrong reason. Such a witness is shown RED by a **named mutant** applied to the
fixed tree, rebuilt, and run unfiltered (`--gtest_filter` narrows the report, never the binary). A row
whose rebuild failed proves nothing and is gated on build rc 0. Where a witness compiles on the unfixed
tree, its RED is the unfixed tree itself (marked **U**). The verify record keeps a table: witness |
mutant or U | build rc | test rc | the failing assertion's message, plus every other test the
unfiltered run reports failing (collateral is listed, not assumed).

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

## Comparator self-test (runs first; a zero is trusted only after it)

**W-0** `tests/dictionary/dictionary_equivalence_test.cpp`. For each store category the comparator
claims (research R-10: a field type, a valid tag, a required tag, an enum value, a component member, a
group member, a group delimiter, a context-store member, a Length+Data pair, a header tag, a message,
a field name), one seeded pair of small dictionaries differing in exactly that store: the comparator
MUST report a difference naming that store. A pair of identical loads MUST report none. **RED**: the
seeded rows are the RED arms; mutant **M-0** (the comparator skips the context stores) turns the
context-store row RED.

## Loading and merging

**W-1 Equivalence, every addition kind (small bases).** `tests/dictionary/overlay_equivalence_test.cpp`,
fixtures `tests/dictionary/fixtures/098/{qf,orch}/{base,overlay,edited}.xml` (hand-written). For each
format: `load_with_overlay_from_string(base, overlay)` vs `load_from_string(edited)` → zero differences.
The overlays cover every FR-003/FR-004 kind: new field, enum value on an existing field (and on an
existing Orchestra code set), new component, new message (with a required field, a new group, a new
component), field added to an existing message, member added to an existing group (inline and inside a
component), field and group added to the header, Required promotion on a message, component and group
child, custom Length+Data pair. **RED**: **M-1** (the splice prepends a new group member instead of
appending) and **M-2** (the splice skips enum-value additions); each turns its row RED with the
comparator naming the store.

**W-2 Equivalence on the vendored bases.** Same file. FIX44, FIX50SP2 and the Orchestra file, each with an
overlay under `tests/dictionary/fixtures/098/real/` and a hand-written insertion patch (anchor line →
lines to insert), applied by a test helper with line-exact matching. `load_with_overlay(base, overlay)`
vs `load_from_string(patched base)` → zero differences. **RED**: (a) a deliberately non-applying patch
(an anchor that matches zero lines, and one that matches two) MUST fail the cell before any comparison;
(b) **M-3** (header/trailer additions are dropped) turns the FIX44 and Orchestra rows RED.

**W-3 Refusals.** `tests/dictionary/overlay_refusal_test.cpp`, table-driven, one row per refusal: every
research R-4 conflict (type, name, msgtype name, required relaxed, redescribed enum value, child
redeclared directly that the parent reaches through a component, duplicate within the overlay, version
mismatch, overlay of the other format), every R-5 cap (bytes at cap + 1 through both the path and the
string form, depth 33, each entry cap + 1), a DOCTYPE, an unknown reference, a cycle that closes through
a base component. Each row asserts the exception type and that its message names the key; after the
table, the base loaded alone compares equal to a fresh load (FR-008). **RED**: **M-4** (conflict checks
removed: the conflict rows load), **M-5** (byte cap removed), **M-6** (`parse_doctype` dropped: the
DOCTYPE row loads), **M-7** (cycle check removed: the cycle row recurses until the process dies; run
under `linux-clang-asan`, whose stack-overflow report is the recorded RED).

**W-4 Load-any dispatch.** Same file as W-1. `load_any_with_overlay` on each format matches the loader
called directly; a mixed-format pair is refused. **RED**: **M-8** (`load_any_with_overlay` ignores the
overlay) turns the match rows RED.

## Wire behaviour

**W-5 Inbound (US1 AS-1..AS-6, SC-002).** `tests/session/test_098_dialect_overlay_inbound.cpp` (joins the
session binary). Three sessions with `validate_inbound_messages = true`: FIX44 base alone, FIX44 base +
the W-2 overlay, FIX44 patched (the edited reference). One frame per scenario: custom field on 35=D;
custom member inside Parties; private MsgType `U1` with and without its required field; custom
Length+Data value containing SOH; Optional→Required promotion missing; OrdType `Z`. Assert: overlay
session and edited session agree on every frame (accept, or the same Reject 373 and 371); the base
session's outcome is the one recorded as today's. **RED**: **U** for the base-session arm (it asserts
today's refusal, so it is a characterization pin), and **M-8** for the overlay arm.

**W-6 Send (US1 AS-7, FR-022, SC-008).** `tests/session/test_098_header_send.cpp`. A session whose
dictionary declares header tag 5020 (a complete edited FIX44, loaded with the existing `load_from_string`,
so the witness compiles on the unfixed tree) sends a message with 5020 after a body field; the frame
carries 5020 before the first body field. A second arm uses base + overlay (new API). A third arm replays
that message through ResendRequest and asserts 43/122 land inside the header, before 5020's body
neighbours (`kReplayHeaderTags` unchanged, research R-7). **Byte identity:** the existing header-order
witnesses (`tests/session/test_resend_answer_field_order.cpp` `SendPayloadTagTest.*`, `ResendAnswer*`;
`tests/session/test_091_data_send.cpp` `HeaderPairSend091.*`, `DataSend091.*`) pass unchanged. **RED**:
**U** for the edited-dictionary arm (the unfixed tree sends 5020 in the body); **M-9** (`as_table_view`
skips the header store) for both arms.

**W-7 Header store per vendored dictionary.** `tests/dictionary/header_tags_test.cpp`. For each of the ten
vendored dictionaries, `is_header_tag` answers true exactly for the tags of an independent raw-XML
header walk in the test (QuickFIX `<header>` expansion; Orchestra component 1024), and false for 5020;
and the union with the session's fixed set equals the fixed set (research R-7's measurement, pinned).
**RED**: **M-9**; and a seeded FIX44 copy with 5020 in `<header>` MUST make the union assertion fail.

## Configuration

**W-8 TOML (US3 AS-1..AS-5, SC-004).** `tests/config/test_098_dialect_overlay.cpp` (joins the config
binary) and fixtures under `tests/config/fixtures/098/`. A session without an overlay gets
`engine.dictionaries.front()` and opens with no host code (AS-1, SC-004); a session with one gets the
merged dictionary (pointer differs from the base, a custom tag is valid); two sessions naming one path
share one pointer; `[default]` inheritance and override (AS-5); a missing file and a conflicting overlay
each report at the key path where the value was written (AS-3); root scope is refused (AS-4). The
existing T020 cells (`test_load_deferred_surface.cpp`), `T018_Step2DialectOverlay`
(`test_load_negative_battery.cpp`) and `T028_DialectOverlayDeferred` (`test_load_selectors.cpp`) are
rewritten to the new outcomes, and `T020_DeferredSetExactCoverage` keeps the other twelve deferred keys.
**RED**: **U** for AS-1 (the unfixed tree leaves `config.dictionary` null, so `open()` fails with
`invalid_session_config`); **M-10** (the resolver assigns the base to every session) for AS-2/AS-5.

## C ABI and Python

**W-9 C ABI.** `tests/capi/dict_overlay_capi_test.cpp` (joins the capi binary): happy path → handle usable
with `fixpp_session_config_set_dictionary`, a session opens and reads a custom tag; cap → 201 and NULL;
conflict → 200 and NULL; NULL arguments → `FIXPP_ERR_NULL_HANDLE`. `tests/capi/version_test.cpp` pins
1.13; `tests/abi/golden/fixpp_capi_symbols.txt` gains the symbol; `tools/capi_freeze.sha256` refreshed;
`tools/check_capi_reentrancy.sh` passes. **RED**: **M-11** (the limit branch maps to 200) turns the cap
row RED.

**W-10 Python.** `bindings/python/tests/test_098_dialect_overlay.py` (and its wheel mirror where the
suite mirrors): load with overlay, open a session config, typed error on a cap overlay;
`test_import_surface.py`'s exact set includes `dict_load_from_xml_with_overlay`. **RED**: **U** for the
import-surface row (the name is absent on the unfixed tree).

## Fuzz, performance, records

**W-11 Fuzz (`[const §VII.7]`).** `tests/fuzz/fuzz_dict_overlay.cpp` with a seed corpus
(`tests/fuzz/corpus/dict_overlay/`): the W-1 overlays, the W-3 refusal overlays, a DOCTYPE overlay.
Every exception escaping the harness must be a `dict::` error type. CI replays the seeds
(`linux-clang-asan`); the ten-minute run is `/speckit-verify`'s fuzz step. **RED**: **M-7** re-run
against the harness with the cycle seed (the harness must crash).

**W-12 Performance (SC-005, `[const §VIII.2]`).** Measured in the verify record, not a ctest:
`load_with_overlay(FIX50SP2, overlay at the entry caps)` vs `load(FIX50SP2)`, ratio ≤ 1.5, same machine,
median of ≥ 5 runs; the send path's paired bench (base vs candidate) shows no regression past the
provisional band for a session on a vendored dictionary.

**W-13 Records.** The parent repo's `.claude/scripts/check_bl_delta.py --root <tree>` (data-model §D),
the brain freshness checks (`.github/workflows/brain-freshness.yml`'s steps, run locally) and the
parent's `research/G19-fix-fpml-iso20022/tools/brain-external-sweep.sh`, the parent's
`.claude/scripts/check-comment-claims.py --root <tree> --base origin/main`, the line-citation gate over
the bundle, and a positive-presence check for each 2c
"Updated (098-dialect-overlay)" note (seeded negative: the check fails on a copy with one note removed).
