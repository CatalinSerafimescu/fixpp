# Implementation Plan: A Data field can carry any octets — atomic Length+Data emit

**Branch**: `091-data-field-bytes` | **Date**: 2026-09-24 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `specs/091-data-field-bytes/spec.md` (fixpp#418, batch B8)

## Summary

Add one atomic operation to the C++ outbound builder:
- `body_builder::field_data(data_tag, bytes)` and `entry_handle::set_data(data_tag, bytes)`;
- it derives the Length tag from the **standard table only** (Gate A r1 ruling 1, FR-009);
- it emits Length = octet count, then the Data octets verbatim, both or neither.

`commit()` gains the #428 pair rule (`wire::length_data_checker`, built from the builder's
`dict_hooks`, so a hand-written dictionary pair is checked too), folded into the INV-5 tree walk it
already does.

The QuickFIX XML loader's pair detection descends into components and groups (Gate A r1 ruling 2,
FR-017), coupling five FIX 5.0 SP2 standard pairs it missed; a per-dictionary drift arm (FR-018)
keeps each dictionary in step with the standard table. For a user-loaded dictionary that declares a
custom pair only inside a component or group, the loader change turns a C-ABI commit that succeeded
into a refusal, so 091 ships **C-ABI 1.9 BREAKING** (Gate A r3 ruling, FR-019). It adds no C-ABI symbol.

The code generator:
- routes every coupled member through the new operation in both the top-level and nested arms, with a
  per-call-site `static_assert` census;
- adds an optional `message_encoding` (347) Args member to messages that can carry `Encoded*` fields.

Goldens are regenerated (v50sp2 loses the Length members of the five newly coupled pairs, an
owner-ratified source break), two read-tier pins are rebaselined, the stage-one pins flip to success,
and L-067-2 closes.

## Technical Context

**Language/Version**: C++23 (clang 18+ / GCC 14 / MSVC 19.4x per Article II)

**Primary Dependencies**: none new. It reuses `wire::dict_hooks`, `wire::length_data_checker`,
`core/length_data_pairs.hpp`, and Google Benchmark for the bench.

**Storage**: N/A

**Testing**: GoogleTest whole-binary grouped executables (Article VII §8). Mutants run in a scratch
copy (§XVI.6).

**Target Platform**: Linux (clang, GCC) and Windows (MSVC). `body_builder` is header- and
source-portable, with no platform code.

**Project Type**: library (fixpp wire and codegen layers)

**Performance Goals**: SC-005. At most +3 % on `builder_bench`'s NoGroup/WithGroup/Raw cases against
a base built in the same session from the current merge-base plus the final bench source, A-B-A-B,
gated on a noise-floor precondition read from the base legs (R-10, quickstart §6).

**Constraints**:
- Zero global heap in `body_builder`: both new nodes come from the existing null-upstream arena.
- `noexcept` everywhere, as today.
- Refusals use existing `core::error` variants only (FR-004a), so the C-ABI error map is untouched.

**Scale/Scope**:
- ~2 public members plus a constructor parameter.
- One emitter branch plus one Args member; one loader walk extension.
- Every golden file with a coupled emission, plus `message_encoding` on most application messages,
  plus the v50sp2 files that hold the five newly coupled pairs (population re-derived by R-5's
  recipe; no pinned count).

## Constitution Check

*GATE: must pass before Phase 0 research. Re-checked after Phase 1 design.*

| Article | Applies? | Status |
|---|---|---|
| **XVI §3** clarify for wire/codegen | yes | ✅ `/speckit-clarify` ran 2026-09-24 (3 Q); specify-time owner decisions FR-008/FR-009 recorded; Gate A r1 owner rulings recorded (Clarifications, Session 2026-09-24 (Gate A round 1)); Gate A r3 C-ABI ruling recorded by `/speckit-clarify` (Session 2026-09-24 (Gate A round 3), FR-019) |
| **XVII §1** Gate A: public C++ API + codegen layout | **yes, both triggers** | ⏳ `/gate-a 091-data-field-bytes` before `/speckit-tasks` |
| **XVI §6** orchestrator does not implement | yes | Code, tests, bench and generated goldens go through `phase-implementer`. The orchestrator authors only the spec bundle, B&L text and brain |
| **VIII §1–2** bench + paired regression budget | yes | `builder_bench` added (`4749f589`); its WithGroup and Raw prechecks are made exact before the paired run (R-10). This feature's own budget (+3 %) is stricter than the constitutional +5 %. Added to `bench/ci-suite.txt` as a candidate-only row (§2a) with tier-2 value **`no`** — the reversible choice, since §2a makes `paired` irreversible. FR-017 engages the **paired** `bench/dictionary/xml_loader_bench` (FIX50SP2 load): measure it A-B-A-B before pushing, with the quickstart §6 procedure; pass condition the §2 budget (a slowdown ≤ +5 %), and over it → the §2 approval path, never self-declared |
| **VIII / XV** zero-allocation discipline | yes | New nodes use the builder's arena. No `new`/`malloc`. B15 (#497) blind spots (over-aligned allocation, MSan/LSan) are not engaged: no over-aligned type is added |
| **VI** 100 % FIX rule | yes | §VI.5: Normative References list `[FIX50SP2 §3.3] Field data types` (coverage index ↔ W-008); TagValue v1.0 is informative (no coverage-index entry). W-008's evidence is amended in the Ledger phase. The interop residual is disclosed (FR-009a, by extending L-426-3) |
| **VII** testing | yes | TDD order: drift arm RED, then loader; RED stage-one flip, then implementation; C-1 RED, then GREEN. Every C-1/C-2 clause maps to a named test; each mutant in quickstart §3 names an existing test shown RED. §VII.8: every touched ctest entry gets label `091` and the recipe selects with `-L` and gates on the checked-in `expected-ctest-091.txt` manifest (quickstart §2); `wire_body_builder_test` stays standalone (§8 exemption: in-TU global `operator new` counter) |
| **IX** coverage / sanitizers / static analysis | yes | Every new line in `body_builder.cpp` covered. ASan/UBSan presets run in `/speckit-verify`. clang-tidy on the changed `src/`, `include/` **and `tools/codegen/`** files (#265) |
| **X** C-ABI | **yes — §X.7 BREAKING, 1.9** (Gate A r3 owner ruling, FR-019) | No symbol, signature or error code is added or changed (FR-004a). FR-017 changes what an existing call returns: `fixpp_msg_commit` refuses a custom pair that a user dictionary declares only inside a component or group, where before it returned `FIXPP_ERR_OK`. Per §X.7 (pre-release regime):<br>• bump `FIXPP_C_ABI_VERSION_MINOR` 8 → 9;<br>• add BREAKING (1.9) markers on `fixpp_msg_commit` (`message.h`) and `fixpp_dict_load_from_xml` (`dict.h`), in the PR description and in B-091-4;<br>• update every in-repo version consumer in the same PR (Phase 0b).<br>§X.6: all four Appendix A controls apply — `/clarify` ✅ (r3 session), `/analyze` (after `/speckit-tasks`), Gate A, and **user `/plan` sign-off** (⏳ required before `/speckit-tasks`) |
| **XI** concurrency | no | No threading change. `send_impl` is read, not modified (R-9) |
| **XIX** documentation | yes | B&L rows, brain `wire.md` (the Length+Data section's "#418 must reuse" line becomes "did"), `CLAUDE-history.md` at close-out. §5: *dated 2026-09-24*, no Doxygen pipeline existed (re-derive: `find . -maxdepth 3 -name 'Doxyfile*'`; if it finds one, regenerate it); the tracked hand-written `docs/src/*.md` pages are grepped with `git grep -n -e body_builder -e length_pair -e 'Length+Data' -- docs/src` in the Ledger phase and any hit is updated. The API contract (refusals incl. handle checks, atomicity limits, `dict_hooks` lifetime) lives in the `body_builder.hpp` comments, C-1 |

**Result:** no violations. Gate A is **required** (not waivable by triviality: a public API plus a
codegen layout change). The declared C-ABI break is sanctioned by §X.7's pre-release clause. It is not a
violation, but it adds the §X.6 user `/plan` sign-off.

## Project Structure

### Documentation (this feature)

```text
specs/091-data-field-bytes/
├── spec.md
├── plan.md              # this file
├── research.md          # R-1..R-11, measured
├── data-model.md        # API delta, invariants INV-6..INV-8, refusal table, ledger rows
├── quickstart.md        # validation recipe incl. mutants and the paired bench
├── contracts/
│   ├── body-builder-data.md   # C-1: public C++ API
│   └── codegen-builders.md    # C-2: emitter output, census, goldens
├── checklists/requirements.md
├── expected-ctest-091.txt   # /speckit-tasks: one ctest name per line (quickstart §2 gate)
└── tasks.md             # /speckit-tasks (after Gate A)
```

### Source Code (repository root)

```text
include/fixpp/wire/body_builder.hpp     # field_data, set_data, ctor(dict_hooks) + lifetime comment, hooks_
src/wire/body_builder.cpp               # R-3 append+rollback, constexpr is_framing_tag + static_assert; R-4 combined walk
src/dictionary/xml_loader.cpp           # R-11 secondary walk into <component>/<group> (stated visit order); delete false comment; header names the decision
include/fix/c_api/version.h             # FR-019: FIXPP_C_ABI_VERSION_MINOR 8 → 9, re-authored trailing comment naming 091/fixpp#418
include/fix/c_api/message.h             # FR-019: BREAKING (1.9) note on fixpp_msg_commit
include/fix/c_api/dict.h                # FR-019: BREAKING (1.9) note on fixpp_dict_load_from_xml
tools/capi_freeze.sha256                # re-pinned for message.h, dict.h, version.h (gate tools/check_capi_freeze.sh)
tests/capi/version_test.cpp (+ every hit of the Phase 0b grep)   # exact-version pins → 1.9
tests/capi/...                          # FR-019 C-ABI before/after test: synthetic component-only custom pair
tools/codegen/fixpp-codegen/
├── emit_builders.cpp                   # R-7 coupled arms + static_assert; R-8 message_encoding
└── gen_util.hpp                        # stale "already String" comment
specs/078-precompiled-builder-libs/contracts/golden/{v42,v44,v50sp2,vlatest}/   # regenerated
bench/wire/builder_bench.cpp            # added (4749f589); WithGroup/Raw prechecks made exact (kWithGroupBody, kRawBody) before the paired run (R-10)
bench/ci-suite.txt                      # + builder_bench row, tier-2 `no`
tests/session/test_067_builder_failclosed.cpp   # stage-one pins brought over (68c8c769), then flipped
tests/wire/test_body_builder.cpp                # C-1 clauses except C-1.9
tests/wire/dict_hooks_custom_pair_test.cpp      # C-1.9 (wire_dict_tests), outbound twin of the inbound cases
tests/wire/length_data_pairs_drift_test.cpp     # FR-018 per-dictionary drift arm incl. Orchestra, non-empty assert per leg (wire_dict_tests)
tests/dictionary/...                            # C-2.5a synthetic-XML loader test, arms (i)-(viii), label 091
tests/wire/CMakeLists.txt, tests/session/CMakeLists.txt, tests/codegen/CMakeLists.txt   # label 091 on touched entries; delete tests/wire's false "none … carry LABELS" banner claim
tests/capi/CMakeLists.txt, tests/dictionary/CMakeLists.txt   # label 091 on the C-ABI exact-version bucket, the FR-019 test's bucket and the C-2.5a loader test's entry (quickstart §2)
tests/codegen/read_tier_byte_diff_test.cmake    # rebaseline v50sp2 Fields/Validator + recipe banner; #427 recipe chained; stale 082/header result text deleted; _baseline_desc + summary text (C-2.5)
tests/codegen/...                               # C-2.2 IR-vs-standard-table census + orphan-half check; C-2.3 message_encoding presence
tests/session/... (or test_067_builder_roundtrip) # C-2.6 generated 256-value witnesses (top + nested)
tests/session/...                               # R-9 send_impl header-pair witness; R-6 v50sp2 Length-delimited group
tests/interop/conversation/support/conv_wire.hpp, conv_cell_test.cpp   # #418 comments reworded (FR-016)
include/fixpp/wire/length_data_check.hpp        # "#418 is meant to be its second caller" → past tense (FR-016)
spec/behaviors-and-limitations{,-closed}.md     # L-067-2 move; delete the "L-067-2 is unchanged" bullet; B-091-1..4; extend L-426-3
spec/feature-catalogue.md                       # W-008 evidence; CA-011 C-ABI 1.9 note
brain/components/wire.md                        # Length+Data section update
brain/components/c-api.md                       # C-ABI 1.9 entry (FR-019)
```

**Structure Decision**: existing single-library layout. No new target except `builder_bench`.

## Phasing (input to /speckit-tasks)

0. **Loader pairing (FR-017/FR-018), TDD.**
   - Write the per-dictionary drift arm first; show it **RED** on the unfixed loader with exactly the
     five FIX50SP2 pairs named. Every leg (Orchestra included) asserts a non-empty, printed probe set.
   - Write C-2.5a (synthetic XML) first too: arms (i), (ii), (iv), (v), (vi), (vii) (all placements), (viii) RED on
     the unfixed loader; arm (iii) is GREEN there by construction and is made live by the skip-instead-of-break
     mutant, (v)/(vi) by the components-and-direct-groups-only mutant and (vii) by the depth-first
     order mutant, and (viii) by the messages-and-components-only entry mutant (quickstart §3).
   - Extend the loader walk (R-11); the arm turns GREEN and the union test stays GREEN (no
     over-pairing).
   - Regenerate; rebaseline the two v50sp2 read-tier pins with their recipes run (C-2.5). Any other
     pin moving stops the phase.
   - The loader change alone also changes the **v50sp2 builder** output: the five pairs become
     coupled in the old two-call form and their Length members disappear. Regenerate the 078 v50sp2
     goldens in this phase as an intermediate, reviewed diff (only those pairs change); phase 3 then
     reroutes all coupled sites. C-2.2's control (a) is **not** these goldens: it is the output of
     the new emitter over the unfixed loader (the "loader walk removed" mutant, phase 4).
   - **Blast radius of the deleted members:** `git grep -nE
     'encoded_leg_documentation_text_len|encoded_post_trade_payment_desc_len|(leg_|underlying_)?payment_stream_formula_length'
     -- src tests bench bindings docs examples include tools` (the accessor names come from the
     pre-change v50sp2 goldens' `set_int`/`field` calls on 2494/2815/43109/43110/43111; re-derive
     them). *Dated, 2026-09-24:* no in-tree hit. Any hit becomes a planned edit in this phase.
0b. **C-ABI 1.9 BREAKING (FR-019)**, in the same PR as phase 0. Pattern: `.specify/495-493-486-dict-reify-copy.md`'s 1.8 procedure, re-derived for 1.9.
   - **Pre-checks:**
     - `gh release list --exclude-drafts` prints nothing, so the §X.7 pre-release regime applies;
     - no open PR touches `include/fix/c_api/version.h`;
     - after `git fetch --all --prune`, `git grep -n "define FIXPP_C_ABI_VERSION_MINOR" $(git for-each-ref --format='%(refname)' refs/heads refs/remotes) -- include/fix/c_api/version.h` shows no branch already at 9. Positive control: `origin/main` shows 8.
   - **C-ABI test, written first:** a synthetic XML dictionary declares a custom pair adjacently only inside a `<component>`. The test loads it with `fixpp_dict_load_from_xml`, writes a malformed instance through the C-ABI (Data with no Length before it), and calls `fixpp_msg_commit`.
     - **Construction** (tags as in C-2.5a, e.g. 5001/5002):
       - **malformed instance:** only `fixpp_msg_set_string(5002, …)`, with a value that holds no SOH (else the unfixed loader refuses at `set_string` and RED lands on the wrong call); no Length is written;
       - **well-formed control:** `fixpp_msg_set_int(5001, 3)` + `fixpp_msg_set_string(5002, "abc")`;
       - **additive-widening instance** (post-change only): `fixpp_msg_set_int(5001, 3)` + `fixpp_msg_set_string(5002, "a\x01b")`; after FR-017 the set call returns `FIXPP_ERR_OK` and the commit returns `FIXPP_ERR_OK`. On the unfixed loader `set_string` refuses the SOH value; that is expected and is not the pinned RED.
     - **Preconditions (else it is not RED on the unfixed loader).** The two tags meet C-2.5a's non-adjacency conditions: not adjacent in `<fields>` order and not consecutive among the `<field>` children of any message, header or trailer, non-field children skipped. Do not copy `kLengthDataFix42Xml` (`tests/capi/length_data_capi_support.hpp`): it pairs its custom tags through the primary walk, so it refuses on the unfixed loader too.
     - **Load path:** `fixpp_dict_load_from_xml` takes a filesystem path, so the test writes the XML to a temp file (not `XmlLoader::load_from_string`); this is the entry point `dict.h`'s BREAKING note marks.
     - **Setters:** hand-written, as the construction above names. Not `fixpp_msg_set_data`: on the unfixed loader the tag is unpaired and it refuses with `FIXPP_ERR_TYPE_MISMATCH`.
     - On the unfixed loader the result is `FIXPP_ERR_OK`, recorded in the commit as the pre-change form.
     - After FR-017 it is `FIXPP_ERR_WIRE_CONFORMANCE`. The test asserts only that post-change value (FR-019), so it goes RED → GREEN across phase 0.
     - A well-formed instance commits `FIXPP_ERR_OK` both before and after (no over-refusal).
   - **Bump:** `FIXPP_C_ABI_VERSION_MINOR` 8 → 9 with a re-authored trailing comment. Add the BREAKING (1.9) notes in `message.h` and `dict.h`.
   - **Consumers:** run `git grep -ln "VERSION_MINOR\|0x010800\|1_8_0\|(8U << 8U)" -- . ':!specs'` and classify each hit; most compare against the macro and move automatically.
     - Hard pins it must surface: `tests/capi/version_test.cpp`'s exact-version cell and `CompositeMacroValue`, plus the `version.h` narrative.
     - `version_test.cpp`'s header comment enumerates each post-freeze minor by ordinal. Delete that ordinal enumeration rather than extending it (a comment records a procedure, not a result); keep the rule it explains.
     - The Python binding exports the name only (`bindings/python/fixpp.i`); confirm it has no value assertion.
     - No error code is minted, so `introducing_minor()` and `tools/abi_history/error_codes_v1.txt` do not change.
     - The library track `FIXPP_VERSION_*` is not bumped.
   - **Freeze manifest:** `tools/check_capi_freeze.sh` must fail on exactly `message.h`, `dict.h` and `version.h` (positive control), then pass after the re-pin.
   - **Mutant:** `FIXPP_C_ABI_VERSION_MINOR` back to 8 must turn the version test RED.
1. **Stage-one carry-over, then flip (RED).** Bring `68c8c769`'s test hunk onto the branch, **tests
   only**, without the superseded design note.
   - Re-run the four rejection pins on the current base: they must be GREEN, confirming the
     limitation still behaves as pinned.
   - **Flip them in the same phase** to assert success, verbatim bytes and a re-parse. They are now
     **RED** against today's builder, which is the TDD red step.
   - Phase 3's codegen change is what turns them GREEN.
   - The pre-flip rejection form is recorded in the commit, so the limitation's pinned-then-fixed
     history stays visible.
2. **`body_builder` API + commit check** (C-1, including C-1.4b handle checks). The C-1 unit tests are written RED first, then
   implemented to GREEN. The flipped pins stay RED: the generated builder still routes through
   `field()`.
3. **Codegen** (C-2.1, C-2.3). Regenerate goldens; run the C-2.2 census (positive controls first) and
   the C-2.4 structural residual diff; confirm the read-tier pins (only the two C-2.5 moves). The
   flipped pins and the C-2.6 witnesses turn **GREEN** here.
4. **Mutants** (quickstart §3), each in a scratch copy, each proven RED on its named test.
5. **Session witnesses** (R-9 header pair, R-6 Length-delimited group, now buildable on v50sp2 after
   phase 0) and the v50sp2/vlatest builder round trips. The **`message_encoding` witness lives here,
   on the `send_impl` path**:
   - 347 is a header tag, so a body-only build-and-validate round trip may refuse it as undeclared
     in the message;
   - through `send_impl` it must land in the header and re-parse.
   - The compile-time surface is re-measured with `vlatest_builders_compile_bench` (R-10).
6. **Paired bench** (quickstart §6); `ci-suite.txt` row (tier-2 `no`). Before the run: the
   `builder_bench` WithGroup/Raw prechecks are made exact, each proven by a one-line mutant (delete
   one scalar field; delete one `kParties` entry) that produces `SkipWithError`. Also the paired
   `xml_loader_bench` A-B-A-B (Constitution Check VIII row).
7. **Ledger + brain** (FR-016, W-008, data-model ledger table). B-091-4 is marked **BREAKING (C-ABI 1.9)**, and the PR description carries the §X.7 declaration. move L-067-2; delete the live
   "L-067-2 is unchanged … (fixpp#418)" bullet; reword the `conv_wire.hpp` / `conv_cell_test.cpp`
   comments; B-091-1..4; extend L-426-3. Record that `.specify/426-428-length-data-pairs.md`'s
   "the drift test keeps the two in step" was stale per dictionary and is made true by FR-018 (the
   426-428 note itself is not edited). The follow-up *verifiable session binding for dictionary
   pairs, option (b)* is fixpp#505, cited in B-091-3. Article XIX §5 docs grep (Constitution Check).
   C-ABI 1.9 doc surfaces, as the 1.8 precedent updated them: a C-ABI 1.9 entry in
   `brain/components/c-api.md` beside its C-ABI 1.8 section, and a C-ABI 1.9 BREAKING note on the
   CA-011 row of `spec/feature-catalogue.md` (data-model ledger table).
   Close-out check: the R-4 follow-up is fixpp#506 (filed 2026-09-24; research.md R-4 and the
   Parked list in `issue-batches.md` both cite it). Confirm it is still open, or record its fix, before
   091 closes.
   In `tests/codegen/read_tier_byte_diff_test.cmake` (C-2.5): the #427 banner's recipe is re-stated
   as chained (apply 091's Validator recipe first, then #427's); the 082 banner's and the header's
   "byte-identical" / "every other artifact" results are deleted and point at the 091 banner rather
   than restating a list; `_baseline_desc` and the pass/fail summary text name 091's baseline for
   both v50sp2 artifacts.

## Complexity Tracking

None. No constitution violation to justify.

## Gate A

### Round 1 — disagreements

None. No Opus-judged finding was marked Disagree.

- Round 1 applied 2026-09-24: Codex P1=3 P2=6 P3=1; Opus post-judging P1=2 P2=6 P3=10; rewrite addresses root causes #1–#5 and owner rulings (FR-009 standard-only set-time; FIX50SP2 loader walk fix inside 091, 7 v50sp2 Length members deleted, 2 read-tier pins rebaselined). Reviews: research/reviews/codex_091-data-field-bytes_gate_a_review.md, research/reviews/opus_091-data-field-bytes_gate_a_adversarial_review.md.

### Round 2 — disagreements

None. No Opus-judged finding was marked Disagree. Two narrowings, recorded so round 3 does not
re-raise them:
- **Noise-floor threshold 1 %, not Codex's 0.6 %** (Opus judging of Codex r2 P2-1): 1 % is budget/3,
  so a 3 % verdict stays resolvable; 0.6 % would sit at the recorded A/A maximum (R-10, dated).
- **ctest names stay `/speckit-tasks` work** (Codex r2 P2-3 downgraded to P3): tasks decides file,
  executable and registration. Gate A's obligation is the checked-in manifest gate in quickstart §2,
  which refuses an absent, empty, unregistered or mis-labelled entry.

- Round 2 applied 2026-09-24: Codex P1=0 P2=4 P3=2; Opus post-judging P1=0 P2=3 P3=10; rewrite addresses root causes RC-A..RC-D. Reviews: research/reviews/codex_091-data-field-bytes_gate_a_2_review.md, research/reviews/opus_091-data-field-bytes_gate_a_2_adversarial_review.md.

### Round 3 — exhausted; owner path

- Round 3 reviewed 2026-09-24: Codex P1=1 P2=1 P3=0; Opus post-judging P1=1 P2=1 P3=2. The loop
  exhausted after 2 rewrites. The owner chose "re-run `/speckit-clarify` then `/speckit-plan`" and
  ruled P1 as **C-ABI 1.9 BREAKING** (FR-019). `/speckit-plan` refresh applied 2026-09-24:
  - Constitution Check Article X engaged (§X.7, §X.6 controls);
  - Phase 0b (C-ABI bump, consumers, freeze manifest, before/after test);
  - B-091-4 BREAKING;
  - the P2 fix: R-4's group-node feed made normative, C-1.7/C-1.9 group-tag cases, and the
    count-digits mutant.
  Reviews: research/reviews/codex_091-data-field-bytes_gate_a_3_review.md,
  research/reviews/opus_091-data-field-bytes_gate_a_3_adversarial_review.md. A fresh `/gate-a` loop
  follows.

### Loop 2 round 1 — disagreements

None. No Opus-judged finding was marked Disagree. Narrowings and choices, recorded so later rounds do
not re-raise them:
- **Codex P2-2 downgraded to P3** (Opus judging): both C-ABI tests keep failable criteria (the
  quickstart §3 version and loader-walk mutants, and the full suite); only the feature-scoped
  selection was missing. Fixed by the quickstart §2 roles and the plan's CMakeLists list; Codex's
  per-bucket "remove label 091, show the population gate RED" proof is not added, since the manifest
  gate already refuses a missing or mis-labelled entry and names stay `/speckit-tasks` work (round 2).
- **Codex P3 (R-3 arena bound)**: the wrong bound is deleted and no corrected bound is stated; the
  condition (a failed call's arena consumption is not reclaimed) is what FR-006 and C-1 carry.
- **Codex P2-1 / Opus P2 (`set_data` parity)**: the two-surface arms are adopted for C-1.4, C-1.5
  (nested twin) and C-1.9. A shared `append_data_field` helper is the preferred implementation (R-3),
  not the witness.
- **N-4 (C-ABI count-digits group feed)**: recorded as follow-up fixpp#506 (R-4); folding its fix
  into C-ABI 1.9 stays an owner option, not taken here.

- Loop 2 round 1 applied 2026-09-24: Codex P1=0 P2=2 P3=1; Opus post-judging P1=0 P2=1 P3=7; rewrite addresses set_data parity + all P3s. Reviews: research/reviews/codex_091-data-field-bytes_gate_a_L2_review.md, research/reviews/opus_091-data-field-bytes_gate_a_L2_adversarial_review.md.

### Loop 2 round 2 — disagreements

None. No Opus-judged finding was marked Disagree. Choices, recorded so round 3 does not re-raise
them:
- **N-1 (R-11 visit order)**: the discriminator arm C-2.5a (vii) is added, in two placements (group
  under a message; group in an earlier component), with the depth-first-order mutant in quickstart
  §3; R-11's stated order is kept, not relaxed.
- **Codex P2 mutants narrowed** (Opus judging): one mutant, "the new walk visits only components and
  their direct `<group>` children", turns both (v) and (vi) RED; (v)'s precondition is stated in the
  loader's own terms (groups skipped) so it is RED on the unfixed loader.
- **Codex P3-1 widened** (Opus judging): beside the two string setters, the commit-time SOH check
  (same `length_tag_for_data(tag) == 0` predicate) is listed as an additive widening, and every
  copy of the setter list (FR-017, FR-019, B-091-4, R-11) is updated.

- Loop 2 round 2 applied 2026-09-24: Codex P1=0 P2=1 P3=3; Opus post-judging P1=0 P2=1 P3=4; rewrite addresses loader depth arms (v)/(vi) + all P3s. Reviews: research/reviews/codex_091-data-field-bytes_gate_a_L2_2_review.md, research/reviews/opus_091-data-field-bytes_gate_a_L2_2_adversarial_review.md.

### Loop 2 round 3 — exhausted; owner path

- Loop 2 round 3 was reviewed on 2026-09-24:
  - Codex P1=0 P2=1 P3=2; Opus post-judging P1=0 P2=1 P3=2.
  - The open P2 is that C-2.5a had no witness for groups owned by `<header>` or `<trailer>`.
- The owner chose "re-run `/speckit-clarify` then `/speckit-plan`". `/speckit-clarify` found no
  critical ambiguity.
- The `/speckit-plan` refresh, applied 2026-09-24:
  - C-2.5, R-11 and FR-017 now cover every `<group>` whatever its parent.
  - C-2.5a gains arm (viii) (header and trailer groups) and placement (vii)(c).
  - Quickstart §3 gains the mutant "group walk entered only from messages and components".
  - The spec Input block gets a note saying the Gate A rulings supersede it in part.
  - Phase 7 gets a close-out check for fixpp#506.
- The witness set is now derived from the complete set of group parents: the parents measured
  across `dictionaries/*.xml`, plus the trailer, which the schema allows. It is no longer listed by
  example.
- Reviews: research/reviews/codex_091-data-field-bytes_gate_a_L2_3_review.md,
  research/reviews/opus_091-data-field-bytes_gate_a_L2_3_adversarial_review.md.
- A fresh `/gate-a` loop (loop 3) follows.
