# Tasks: A Data field can carry any octets — atomic Length+Data emit in the C++ builders

**Feature**: `091-data-field-bytes` (fixpp#418, batch B8) | **Branch**: `091-data-field-bytes`

**Input**: `specs/091-data-field-bytes/` — spec.md (FR-001…FR-021, SC-001…SC-006), plan.md (phases 0–7),
research.md (R-1…R-11), data-model.md (INV-2…INV-8, refusal table, ledger, Appendix A),
contracts/body-builder-data.md (C-1), contracts/codegen-builders.md (C-2), quickstart.md (§1–§6).

**Tests**: REQUIRED. The spec names the witnesses (C-1.x, C-2.x, FR-014/015/018/019) and Article VII
requires TDD order: each witness is written first and shown RED for the reason its clause states, and
only then made GREEN.

## Execution rules (apply to every task)

- **Who executes.** Every task that edits a file other than `*.md` under `specs/`, `spec/`, `brain/`
  or `docs/` is delegated to `phase-implementer` (`[const §XVI.6]`; the orchestrator edit guard
  refuses it, and has no override). That covers code, tests, CMakeLists, generated goldens, the bench,
  `capi_freeze.sha256`, **comment-only edits** in `.hpp`/`.cpp`/`.h`/`.cmake` files, and the
  `expected-ctest-091.txt` manifest. The orchestrator authors only `tasks.md`, the B&L text,
  `brain/`, and the `spec/feature-catalogue.md` / `spec/coverage-index.md` markdown.
- **Mutants run in a scratch copy of the tree, never in the PR worktree** (quickstart §3). Every
  mutant is shown RED on the test it names, then GREEN after revert, and a `git diff` of the scratch
  copy against the PR head proves the revert is clean.
- **Codegen freshness** (quickstart §1) after ANY emitter or loader change:
  `cmake --build build/<preset> --target fixpp-codegen && rm -rf build/<preset>/_codegen && cmake --build build/<preset>`.
  Codegen runs at configure time and does not track the emitter binary.
- **Disk and build trees.** Run `df -h /mnt/e` before any full rebuild of the main checkout's
  `build/linux-clang-debug` (it sits on E:). `build/linux-clang-release` is a symlink to
  `/mnt/wsl/fixppbuild/build/main-clang-release`. Run long builds with `setsid nohup`, plus a
  `.done` marker.
- **Builds need an owner ask (`[const §XVII.7]` resource gate).** Before dispatching any task that
  configures, builds, rebuilds or runs `conan install` (codegen freshness, T027, the bench base in
  T057/T058, fuzz, `/speckit-verify`), the orchestrator asks the owner with `AskUserQuestion`. One
  approval may cover a named phase; record it in the evidence file. Never auto-run a build.
  **Owner ruling 2026-09-24: blanket approval for every build 091 needs** (every phase, the bench
  base, fuzz and `/speckit-verify`), so no per-phase ask is needed. Copy this ruling into the
  evidence file when T002 creates it. The disk and placement rules below still apply.
- **Never `git checkout`/`git switch` in this checkout.** The bench base (T057) is its own
  detached worktree under `/mnt/wsl/fixppbuild`.
- **Comments record a procedure, never a result** (no counts, offsets or pasted output). Every
  re-derivation recipe the spec cites is re-run at the implementation head; a recorded figure is a
  lead, not an input.
- **Labels.** Every ctest entry a task adds to or edits gets label `091` in the CMakeLists that
  registers it. `expected-ctest-091.txt` (T003) is the gate.
- **Evidence file.** Mutant, bench, compile-surface and golden-residual evidence goes to
  `.specify/decisions/091-data-field-bytes-evidence.md` (a sibling of the verify record; `/speckit-verify` produces its own record in T065 and
  cites this file). Never write into the verify record before T065 creates it.
- **Commit after each task or logical group**; the RED form of each TDD step goes in its commit
  message (FR-019, plan phase 1).

## Format: `[ID] [P?] [Story] Description`

- **[P]**: can run in parallel (different files, no dependency on an incomplete task).
- **[Story]**: US1–US4 from spec.md. Setup, Foundational and Polish carry no story label.

---

## Phase 1: Setup

**Purpose**: pre-flight, before-measurements, the ctest manifest, and the stage-one TDD anchors.

- [X] T001 Pre-flight in the library root. `git rev-parse --abbrev-ref HEAD` prints
  `091-data-field-bytes`, and `git status --short` is empty. Run `git fetch --all --prune` and
  record `git merge-base HEAD origin/main`. `df -h /mnt/e` shows room for a `-debug` rebuild. Confirm
  `git diff --stat origin/main...HEAD -- src include tools cmake` is empty; production code is still
  `main`'s, which is what T002 depends on.
- [X] T002 Before any edit under `src/`, `include/` or `tools/`, measure the compile-time surface
  "before" figure with `bench/codegen/vlatest_builders_compile_bench/compile_bench.sh`
  (R-10). Record the command, toolchain and figures in
  `.specify/decisions/091-data-field-bytes-evidence.md` §*Compile-time surface*. The figure is
  unrecoverable after the first production edit. If T001 finds a production diff, measure from
  the merge-base worktree instead and say so.
- [X] T003 Via `phase-implementer`: create `specs/091-data-field-bytes/expected-ctest-091.txt`, one
  ctest name per line, sorted:
  `capi_length_data`, `capi_pure_tests`, `codegen_091_data_census_test`, `dictionary_pure_tests`,
  `fixpp::dict::read-tier-byte-diff`, `session_091_data_send`, `test_067_builder_failclosed`,
  `test_067_builder_roundtrip`, `test_077_allversions_builder_roundtrip`,
  `test_077_allversions_builder_roundtrip_vlatest`, `wire_body_builder_test`, `wire_dict_tests`.
  - Add label `091` (appended to the existing LABELS, none removed) on the entries that already exist:
    - `wire_body_builder_test` in `tests/wire/CMakeLists.txt`: add a `set_tests_properties` after
      its `add_wire_test`;
    - `wire_dict_tests` in `tests/wire/CMakeLists.txt`;
    - `dictionary_pure_tests` in `tests/dictionary/CMakeLists.txt`;
    - `fixpp::dict::read-tier-byte-diff` in `tests/codegen/CMakeLists.txt`;
    - `test_067_builder_failclosed`, `test_067_builder_roundtrip`,
      `test_077_allversions_builder_roundtrip` and `test_077_allversions_builder_roundtrip_vlatest`
      in `tests/session/CMakeLists.txt`;
    - `capi_pure_tests` and `capi_length_data` in `tests/capi/CMakeLists.txt`.
  - In the same edit, **delete** the false banner claim in `tests/wire/CMakeLists.txt`'s header
    comment (the clause beginning "none of
    the 34 carry LABELS"); it was already false (`wire_pure_tests` and
    `wire_dict_tests` carry labels). Delete it; do not replace it with a count.
  - `codegen_091_data_census_test` and `session_091_data_send` are registered by T028 and T039. Until
    then, the quickstart §2 gate is expected to report them missing.
  - Positive control: before T028, the gate's `diff "$R/expected.txt" "$R/labelled.txt"` line must
    report exactly those two names missing from the labelled side (the gate exits there, before
    `comm -23`).
- [X] T004 Via `phase-implementer`, carry over the stage-one pins, **tests only**. Apply the
  `tests/session/test_067_builder_failclosed.cpp` hunk of `68c8c769`
  (`git show 68c8c769 -- tests/session/test_067_builder_failclosed.cpp`) onto this branch; do NOT
  bring `.specify/418-data-field-bytes.md`.
  - The four `DataField_EncodedText_{SOH,ControlByte,0x80,0xFF}_…_418` cases must pass **as rejection
    pins** (`wire_field_value_out_of_range`, `out` untouched) on the current production code.
  - Show each one RED under a mutant that widens `is_printable`'s range, in a scratch copy.
  - Commit this form on its own ("stage-one carry-over, rejection form, GREEN"), so the
    pinned-then-fixed history stays visible (FR-014, plan phase 1).
- [X] T005 Via `phase-implementer`, flip the four pins in
  `tests/session/test_067_builder_failclosed.cpp` to assert:
  - success;
  - `EncodedTextLen(354)` equals the octet count;
  - `EncodedText(355)` equals the input octets verbatim;
  - a re-parse through fixpp's inbound parser (`Parser<Index>`) recovers the same octets.
  If the re-parse needs `fixpp_dictionary`, add it to `test_067_builder_failclosed`'s
  `target_link_libraries`.
  - `SohInValue_RejectedBeforeAnyByteReachesOut` stays byte-identical: `git diff origin/main --
    tests/session/test_067_builder_failclosed.cpp` shows no edit inside that `TEST` body (FR-007).
  - All four are now **RED** against today's builder. Commit with the RED output quoted. They turn
    GREEN in US1 (T036).

---

## Phase 2: Foundational (blocking prerequisites)

**Purpose**: the loader fix and its C-ABI 1.9 declaration (plan phases 0 + 0b), then the
`body_builder` operation and its commit check (plan phase 2). Every user story routes through these:
- US1's generated builders call `field_data`/`set_data`, and their v50sp2 census needs the loader fix;
- US2 is about the operation's guard;
- US3 is the operation's contract.

**⚠️ No user-story task starts until this phase's checkpoint holds.**

### 2a — Loader pairing (FR-017, FR-018, C-2.5, C-2.5a), tests first

- [X] T006 [P] Via `phase-implementer`, add the **per-dictionary drift arm** to
  `tests/wire/length_data_pairs_drift_test.cpp` (`wire_dict_tests`), beside
  `HeaderEqualsShippedDictionaryUnion`, which stays unchanged.
  - One arm per shipped dictionary, Orchestra FIX Latest included.
  - Each arm probes every Length-typed `FieldRef` in every message expansion (the `message_fields()`
    probe `add_pairs` uses). For every standard pair (`core::detail::standard_length_data_pairs`)
    whose Length **and** Data tags both occur in that dictionary's expansions, it requires
    `dict.length_pair_data_tag(length) == data`.
  - Each arm asserts its probed standard-pair set is **non-empty** and prints it (FR-018).
  - Run it on the unfixed loader. It must be **RED for FIX50SP2 with exactly the five pairs**
    2494→2493, 2815→2814, 43109→42684, 43110→42486 and 43111→42982, and GREEN on every other leg.
    Quote the RED output in the commit.
- [X] T007 [P] Via `phase-implementer`, add the C-2.5a synthetic-XML loader test as a new file
  `tests/dictionary/xml_loader_component_group_pairs_test.cpp`, joined to `dictionary_pure_tests`
  in `tests/dictionary/CMakeLists.txt` (`[const §VII.8]`, isolation-safe; T003 labels that bucket).
  - It uses `XmlLoader::load_from_string`, with one custom LENGTH/DATA pair per arm and **distinct
    tags per arm**, so the first-writer rule cannot leak between arms.
  - **Global preconditions, every arm.** The two fields are not adjacent in `<fields>` order. They
    are not consecutive among the `<field>` children of any message, header or trailer, reading with
    non-field children skipped.
  - The arms, with assertions exactly as contracts/codegen-builders.md C-2.5a states:
    - **(i)** adjacent only inside a `<component>` → paired, and
      `table_view::has_nonstandard_pair()` is true;
    - **(ii)** adjacent only inside a `<group>` nested in a component → paired;
    - **(iii)** a `<component>` reference between the two fields, in a component → **not** paired;
    - **(iv)** a Length adjacent to Data A in one component and to Data B in a later one → paired
      with A;
    - **(v)** adjacent only inside a `<group>` that is a direct child of a `<message>`, both tags
      only inside that group → paired;
    - **(vi)** adjacent only inside a `<group>` directly in another `<group>`, the outer group inside
      a component → paired;
    - **(vii)** a Length adjacent to Data A inside a group and to Data B as direct fields of a
      component → paired with B, in three placements:
      - (a) the group is a direct child of a `<message>`;
      - (b) the group is in an earlier component C1, Data B in a later component C2;
      - (c) the group is a direct child of `<header>`;
    - **(viii)** adjacent only inside a `<group>` that is a direct child of (a) `<header>` and
      (b) `<trailer>` → paired.
  - Run on the unfixed loader. (i), (ii), (iv), (v), (vi), every (vii) placement and both (viii)
    placements must be **RED**. (iii) is GREEN by construction; T026 proves it can fail.
- [X] T008 [P] Via `phase-implementer`, add the FR-019 C-ABI before/after test as a new file
  `tests/capi/length_data_component_pair_test.cpp`. Join it to `capi_length_data_test` in
  `tests/capi/CMakeLists.txt` (the Length+Data bucket; T003 labels it).
  - **Dictionary and load path.** A synthetic XML declares a custom pair (e.g. 5001/5002)
    adjacently **only inside a `<component>`**, under C-2.5a's global preconditions. It is written to
    a temp file and loaded with `fixpp_dict_load_from_xml`, the entry point `dict.h`'s BREAKING note
    marks. Do **not** copy `kLengthDataFix42Xml` from `tests/capi/length_data_capi_support.hpp`: its
    primary walk pairs the tags, so it refuses on the unfixed loader too.
  - **Malformed instance:** only `fixpp_msg_set_string(5002, …)`, with a value holding no SOH and
    no Length written. `fixpp_msg_commit` must return `FIXPP_ERR_WIRE_CONFORMANCE`. The test asserts
    only that post-change value.
  - **Well-formed control:** `fixpp_msg_set_int(5001, 3)` + `fixpp_msg_set_string(5002, "abc")`
    commits `FIXPP_ERR_OK`, before and after (no over-refusal).
  - **Additive widening** (post-change only): `fixpp_msg_set_int(5001, 3)` +
    `fixpp_msg_set_string(5002, "a\x01b")`. The set call and the commit both return `FIXPP_ERR_OK`.
  - **`set_data` failure → different failure:** `fixpp_msg_set_data(5002, bytes, 0)`, with
    `bytes` a **non-null** pointer (a null pointer returns `FIXPP_ERR_NULL_HANDLE` first and would
    be RED for the wrong reason), asserts
    `FIXPP_ERR_WIRE_CONFORMANCE`. First re-derive in `src/capi/message_write.cpp` that
    `fixpp_msg_set_data`'s `len == 0` refusal precedes its `FIXPP_ERR_DICT_CONFIG` declaration check;
    if it does not, the GREEN value is that check's answer, and this goes back to the orchestrator
    before writing the assertion.
  - Run on the unfixed loader. The malformed commit returns `FIXPP_ERR_OK`, `set_string` refuses the
    SOH value (expected, not the pinned RED), and `set_data(len = 0)` returns
    `FIXPP_ERR_TYPE_MISMATCH`. The commit message records these as the pre-change form.
- [X] T009 Via `phase-implementer`, add the FR-019 `fixpp_session_send` witness in the same file as
  T008, `tests/capi/length_data_component_pair_test.cpp`.
  - Harness: `tests/capi/capi_loopback_support.hpp`.
  - Session dictionary: a full shipped dictionary (e.g. the FIX 4.4 XML) with one injected
    `<component>` declaring 5001/5002 adjacently, referenced from message `D`, under C-2.5a's
    non-adjacency preconditions, including `<fields>` order in the base XML.
  - `fixpp_session_send` of `35=D␁5001=2␁5002=abc␁` asserts `FIXPP_ERR_APP_PAYLOAD_MALFORMED`.
  - Precondition: on the unfixed loader the same send returns `FIXPP_ERR_OK`, not some other
    refusal. Record it in the commit as the pre-change form.
  - If the loopback harness cannot link into `capi_length_data_test`, register the assertion in the
    bucket that holds `capi_loopback_support.hpp` users, give it label `091`, and add its name to
    `expected-ctest-091.txt`.
- [X] T010 [P] Via `phase-implementer`, add the FR-019 inbound-drop and reader witnesses to
  `tests/wire/dict_hooks_custom_pair_test.cpp` (`wire_dict_tests`). Use `Parser<Index>` over the
  `table_view` of the T008-style synthetic dictionary, loaded with `load_from_string`.
  - **Inbound drop.** The malformed frame (Data 5002 whose count does not end on SOH) is refused. On
    the unfixed loader it parses as two plain fields (RED).
  - **Reader, 1137.** Frame `…5001=8␁5002=a␁1137=9␁…`, where `a␁1137=9` is the 8-byte Data. Assert
    that `parse()` returns a value AND that tag 1137 is absent. On the unfixed loader it reads `"9"`
    (RED).
  - **Reader, msg_type.** Frame `8=…␁9=…␁5001=6␁5002=a␁35=D␁34=…␁49=…␁56=…␁52=…␁…`, where
    `a␁35=D` is the 6-byte Data. Assert that `parse()` returns a value AND that `msg_type()` is
    empty. On the unfixed loader it is `"D"` (RED).
  - Each reader witness asserts both halves, so a parse refusal cannot pass as an absorbed field.
- [X] T011 Blast radius of the deleted v50sp2 Length members (FR-011 carve-out).
  - Re-derive the accessor names from the pre-change goldens under
    `specs/078-precompiled-builder-libs/contracts/golden/v50sp2/`: the members emitted as
    `field`/`set_int` on Length tags 2494, 2815, 43109, 43110 and 43111.
  - Run `git grep -nE '<those names>' -- src tests bench bindings docs examples include tools`.
  - Every hit becomes a planned edit in T014. Record the grep and its result in the commit.
- [X] T012 Via `phase-implementer`, extend `LoaderState::detect_length_pairs` in
  `src/dictionary/xml_loader.cpp` (R-11).
  - **The new walk** visits every `<component>` definition and every `<group>` at any depth,
    whatever its parent (`<header>`, `<trailer>`, `<message>`, `<component>` or another
    `<group>`). In those containers, **any non-`<field>` child breaks adjacency**.
  - The existing direct-`<field>` walk of the header, trailer and messages stays unchanged.
  - **Visit order:** `<fields>`, then the header, trailer and messages (as today), then
    `<component>` definitions in document order, then `<group>` elements in document order.
    `mark_pair` stays first-writer-wins.
  - **Delete** the false comment "In practice the global-fields path already captures all standard
    pairs". Then re-read the function's whole header comment and delete every other sentence the new
    walk falsifies (e.g. "the secondary walk retains the original coverage", "the old per-container
    walk never descended into `<component>` nodes"). Do not replace any of them with a new claim.
  - The function's header comment names the superseding decision: "091 (fixpp#418) Gate A r1 —
    secondary walk descends into components and groups".
  - GREEN on T006–T010. `HeaderEqualsShippedDictionaryUnion` stays GREEN, so there is no
    over-pairing: no non-standard row on any shipped dictionary.
- [X] T013 Via `phase-implementer`, run codegen freshness and rebaseline the read-tier pins in
  `tests/codegen/read_tier_byte_diff_test.cmake` (C-2.5).
  - **Rebaseline exactly** `_expected_v50sp2_Fields.hpp` and `_expected_v50sp2_Validator.hpp`, with
    a banner paragraph in the fixpp#427 style giving both re-derivation recipes relative to the
    current pins:
    - `Validator.hpp`: delete the `length_data_pairs` rows for the five pairs, restore the array
      extent, sha256;
    - `Fields.hpp`: zero the `length_pair_data_tag` column of the `FieldRef` rows for 2494, 2815,
      43109, 43110 and 43111, sha256.
    - **Run both recipes.** Each must reproduce the pre-091 pin exactly.
  - **Consequential edits in the same file:**
    - the fixpp#427 banner's recipe is re-stated as **chained** (091's Validator recipe first, then
      #427's);
    - the 082 banner's and the header's "byte-identical" / "every other artifact …" result
      statements are **deleted**, and point at the 091 banner;
    - `_baseline_desc`, and the FATAL_ERROR and PASSED summary texts, name 091's baseline for both
      v50sp2 artifacts.
  - **Stop condition:** if any other pin moves — in particular `v50sp2/Messages.hpp`, which also
    corroborates `specs/003-dictionary-codegen/contracts/golden/v50sp2_Messages.golden.hpp` — stop
    and report to the owner (spec Assumptions).
- [X] T014 Via `phase-implementer`, regenerate the 078 **v50sp2** builder goldens under
  `specs/078-precompiled-builder-libs/contracts/golden/v50sp2/` as an **intermediate** diff (old
  emitter, fixed loader). Only the five newly coupled pairs may change:
  - their Length members are deleted;
  - they become coupled in the old two-call form (`r_len` + `r_data`).
  Apply every T011 hit as an edit. The diff is reviewed and committed on its own. It is **not**
  C-2.2's control (a) (that control is T037).

### 2b — C-ABI 1.9 BREAKING (FR-019, `[const §X.7]`), same PR

- [X] T015 C-ABI pre-checks (plan phase 0b):
  - `gh release list --exclude-drafts` prints nothing, so the §X.7 pre-release regime applies;
  - no open PR touches `include/fix/c_api/version.h` (`gh pr list --state open --json number,files`,
    filtered);
  - after `git fetch --all --prune`, `git grep -n "define FIXPP_C_ABI_VERSION_MINOR" $(git
    for-each-ref --format='%(refname)' refs/heads refs/remotes) -- include/fix/c_api/version.h`
    shows no branch already at 9. Positive control: `origin/main` shows 8.
  Any failure goes to the owner before T018.
- [X] T016 Appendix A row-set gate and class re-verification (data-model.md Appendix A, R-11).
  - Run Appendix A's `diff` recipe from the library root; it must print nothing.
  - Positive controls, each shown to print the symbol:
    - delete the `fixpp_msg_get_string` row from a scratch copy of `data-model.md`;
    - append a fake symbol to a scratch copy of `tests/abi/golden/fixpp_capi_symbols.txt`.
  - Re-run R-11's *C-ABI 1.9 population recipe* (steps 1–5) at the implementation head. A class
    that changed, or a declaration the recipe adds that FR-019 does not name, is a planned edit of
    FR-019 and Appendix A: the orchestrator edits the `.md`, and the implementer edits the header in
    T019. It is never a silent omission.
- [X] T017 Via `phase-implementer`, write the 1.9 expectation first (`[const §VII.3]`). In
  `tests/capi/version_test.cpp`, set the exact-version cell and `CompositeMacroValue` to 1.9, and
  **delete** the header comment's enumeration of post-freeze minors by ordinal (keep the rule it
  explains). Run `capi_pure_tests` against the unbumped `version.h` (minor 8): it must be **RED** on
  exactly those cells. Quote the RED in the commit. T018 turns it GREEN.
- [X] T018 Via `phase-implementer`, set `FIXPP_C_ABI_VERSION_MINOR` 8 → 9 in
  `include/fix/c_api/version.h`, with a re-authored trailing history comment.
  - It names 091/fixpp#418 and the §X.7 BREAKING declaration.
  - It carries FR-019's effects that have no carrying declaration:
    - a stored pre-1.9 frame with a malformed component/group-only custom pair fails replay
      (`build_replay_frame`) and is gap-filled;
    - the header and Logon scans (`scan_frame_header`, `interpret_logon`, the store's
      `frame_has_genuine_tag554` masking) read such a Data by count. So a Logon in which a required
      field (re-derive the list from `interpret_logon`'s validation steps) follows a malformed count
      is refused. On the initiator path `fixpp_session_is_established` stays `false`, and
      `fixpp_session_close` returns `FIXPP_ERR_THREAD_SESSION_LIFECYCLE`, not `FIXPP_ERR_OK`. Name
      both as observers.
  - No error code is minted. `introducing_minor()`, `tools/abi_history/error_codes_v1.txt` and the
    library track `FIXPP_VERSION_*` do not change.
- [X] T019 Via `phase-implementer`, add the BREAKING (1.9) notes FR-019 classifies. Each note says
  that a Length+Data pair a loaded dictionary declares only inside a component or group is now a
  dictionary pair, then names that declaration's own effect, in the words of FR-019 / Appendix A.
  - `include/fix/c_api/dict.h`: `fixpp_dict_load_from_xml` (the root cause).
  - `include/fix/c_api/message.h`:
    - `fixpp_msg_commit` (`FIXPP_ERR_OK` → `FIXPP_ERR_WIRE_CONFORMANCE`);
    - `fixpp_msg_set_data` and `fixpp_entry_set_data` (failure → different failure:
      `FIXPP_ERR_TYPE_MISMATCH` no longer fires, and later refusals are reached, e.g.
      `FIXPP_ERR_WIRE_CONFORMANCE` for `len == 0`; `FIXPP_ERR_DICT_CONFIG` for
      `fixpp_msg_set_data`);
    - **one** reader-family paragraph in the accessor preamble ("Return codes common to all
      accessors"). It names `fixpp_msg_get_{string,bytes,int,double,decimal}`, `fixpp_msg_has_tag`,
      `fixpp_msg_version`, `fixpp_msg_get_msg_type`, `fixpp_msg_field_count`, `fixpp_msg_field_at`,
      `fixpp_msg_get_group`, `fixpp_group_get_field_{string,int,double,decimal}` and
      `fixpp_group_get_nested_group`. It covers the inbound, clone and toApp views and states each
      effect as FR-019 does. For the 35-reachability condition, keep the re-derivation recipe
      ("grep `validate_inbound_messages` in `src/capi`"), not a result.
  - `include/fix/c_api/session.h`:
    - `fixpp_session_send` (`FIXPP_ERR_OK` → `FIXPP_ERR_APP_PAYLOAD_MALFORMED`; a count covering a
      following field is transmitted verbatim; for a refused send the toApp callback is not
      invoked);
    - `fixpp_session_register_callback` (a malformed-pair frame is dropped silently, with no
      Reject).
  - No note goes on `fixpp_msg_set_string`/`fixpp_entry_set_string`: their change is additive only,
    so B-091-4 carries it.
- [X] T020 Via `phase-implementer`, update the version consumers.
  - Run `git grep -ln "VERSION_MINOR\|0x010800\|1_8_0\|(8U << 8U)" -- . ':!specs'` and classify
    each hit.
  - Update the remaining hard pins (the `version.h` narrative, any other exact-value hit). The
    `version_test.cpp` pins were written first in T017.
  - **Behaviour consumers (`[const §X.7]` "every in-repository consumer"):** grep the runtime
    dictionary-load users (`fixpp_dict_load_from_xml`, `XmlLoader::load`) in `bindings/`, `examples/`
    and `tests/interop/`. Record "none affected" or a planned edit for each hit; a hit under
    `bindings/python/` also runs `pytest bindings/python/tests/` (after the owner build ask).
  - Confirm `bindings/python/fixpp.i` exports the name only, with no value assertion.
- [X] T021 Via `phase-implementer`, update the freeze manifest.
  - `tools/check_capi_freeze.sh` must first **fail on exactly** `message.h`, `dict.h`, `session.h`
    and `version.h` (positive control, output quoted).
  - Then re-pin `tools/capi_freeze.sha256`, and the script passes.

### 2c — `body_builder` operation and commit check (C-1, FR-001…FR-009), tests first

- [X] T022 Via `phase-implementer`, write the C-1 unit tests in `tests/wire/test_body_builder.cpp`
  (`wire_body_builder_test`, standalone, `[const §VII.8]`-exempt because of its in-TU global
  `operator new` counter). They are **RED** at first (the members do not exist).
  - **Oracle for every refusal and rollback case:** commit, and require the body to be
    byte-identical to one committed by an identical builder that never made the call.
  - **C-1.1** `field_data(355, {0x41,0x01,0x42})` + commit yields `354=3␁355=A␁B␁`, in order, at the
    position of the call.
  - **C-1.2** every octet `0x00–0xFF` through `field_data(355, {b})`, and the same through
    `set_data` in a group entry. A re-parse through the inbound parser recovers exactly `{b}` as tag
    355. If `wire_body_builder_test` cannot parse without `fixpp_dictionary`, use the
    dictionary-free `Parser` path.
  - **C-1.3** N ≥ 10 octets, and the largest N that still commits, with N + 1 refused
    `wire_frame_too_large` at commit. Find the boundary in the test (never a literal; `kBodyCap` is
    TU-local in `src/wire/body_builder.cpp` and cannot be named). `354=<N>` with no leading zeros.
  - **C-1.1 variants (spec Edge Cases):** a pair whose Data tag is below its Length tag
    (`field_data(89, …)` → `93=<n>␁89=…`) and a non-adjacent pair (`field_data(1527, …)` →
    `1525=<n>␁1527=…`, per the standard table) commit with the Length first.
  - **C-1.4, on both surfaces:** once through `field_data`, and once through `set_data` on a **live
    innermost entry** (group opened, entry added, delimiter already set; the no-call twin commits
    OK). Codes are FR-004a's, verbatim:
    - tag `11` with value `"A\x01" "1=EVIL"` → `wire_unexpected_tag`;
    - tag `354` (a Length half) → `wire_unexpected_tag`;
    - tag `8` (framing) → `wire_field_value_out_of_range`;
    - empty value → `wire_field_value_out_of_range`.
  - **C-1.4b `set_data` handle checks**, each → `wire_invalid_field_format` with the oracle, called
    with a valid standard Data tag and a non-empty value: a default-constructed `entry_handle`; the
    handle of an entry whose group was already closed by `group_end`; an outer entry's handle while a
    nested group's entry is innermost.
  - **C-1.5 rollback.**
    - Pre-fill the arena until the remaining capacity is below the Data size, then call
      `field_data` with a value larger than what remains but below `kBodyCap` →
      `wire_frame_too_large`, oracle holds.
    - **Arrangement witness, on every platform:** a twin builder with the identical pre-fill
      accepts `field(354, std::int64_t{N})`.
    - **The no-call twin commits OK** (assert it): choose the pre-fill so the body stays under the
      body cap, else the oracle compares two `wire_frame_too_large` commits and the
      rollback-removed mutant survives. If no pre-fill both exhausts the arena and stays under the
      cap, stop and report to the orchestrator (a contract change, not a workaround).
    - **Nested twin:** the same inside a live innermost entry via `set_data`; its arrangement witness
      is the twin's `set_int(354, N)`.
    - An over-cap total at commit (arena not exhausted) → `wire_frame_too_large`, `out` untouched,
      and a second commit returns the same error.
  - **C-1.6** `field(355, "A\x01B")` is still refused by the content guard.
  - **C-1.7 commit pair check**, each → `wire_invalid_field_format`, `out` untouched:
    - `field(354, int 3)` alone;
    - `field(355, "abc")` alone;
    - `field(354, int 4)` + `field(355, "abc")`;
    - a group node between 354 and 355;
    - `field(354, "0")` + `field(355, "x")`;
    - a group with `no_tag` 354 and one instance, then a sibling one-byte `field(355, "x")`;
    - a group with `no_tag` 355.
    - A hand-written `field(354, int 3)` + `field(355, "abc")` commits.
    - Every group-tag case has instances that are non-empty and delimiter-first, and a **committing
      twin** exactly as C-1.7 specifies, so the refusal is proven to be INV-6's, not INV-5's.
  - **C-1.8** per-container: a Length ending a group instance does not pair with a Data that
    follows the group in the enclosing container (top level and nested), with a committing twin
    of identical bytes (contract C-1.8).
  - **C-1.10** an entry of a group whose `delimiter_tag` is 43109, populated by
    `set_data(42684, …)`, commits.
  - **C-1.11** `field_data(355, …)` twice appends two well-formed pairs; commit accepts.
  - Record the RED build output in the commit.
- [X] T023 Via `phase-implementer`, add C-1.9 (dictionary pairs, FR-009) to
  `tests/wire/dict_hooks_custom_pair_test.cpp` (`wire_dict_tests`), as the outbound twin of its
  inbound cases. With a builder constructed from `dict_hooks::for_table_view(tv)` of a dictionary
  that declares (5001, 5002):
  - `field_data(5002, "abc\x01" "1=EVIL")` → `wire_unexpected_tag`, nothing appended; the same
    through `set_data` on a live innermost entry, under the oracle;
  - hand-written `field(5001, int 3)` + `field(5002, "abc")` commits;
  - `field(5001, int 4)` + `field(5002, "abc")`, and 5002 with no preceding 5001 →
    `wire_invalid_field_format` at commit, `out` untouched;
  - the same malformed pair on a default (`none()`) builder commits;
  - a group whose `no_tag` is 5001, with one instance → `wire_invalid_field_format`, with its
    committing twin (the `no_tag` changed to a non-pair tag);
  - a dictionary re-pairing a standard tag (95 → 5002) is ignored in favour of the standard pair
    (L-426-2).
  The table view outlives the builder. RED at first.
- [X] T024 Via `phase-implementer`, implement in `include/fixpp/wire/body_builder.hpp` and
  `src/wire/body_builder.cpp` (R-1…R-4, data-model).
  - **Header.**
    - Add `#include "dict_hooks.hpp"`; no dictionary include.
    - The constructor becomes `explicit body_builder(std::string_view msg_type, dict_hooks hooks =
      dict_hooks::none()) noexcept`. It is already `explicit` with one argument, so existing callers
      compile unchanged.
    - Add a private `dict_hooks hooks_` stored by value.
    - Add `field_data(std::uint16_t, std::span<const std::byte>) noexcept` and
      `entry_handle::set_data(std::uint16_t, std::span<const std::byte>) noexcept`.
  - **API comments, the contract (C-1):**
    - set time uses the standard table only, whatever hooks the builder holds;
    - `value` is copied verbatim, and any octet `0x00–0xFF` is accepted;
    - on failure the container is back at its pre-call size;
    - **arena capacity consumed by a failed call is not reclaimed** (FR-006);
    - `hooks_` is read by `commit()` only;
    - the `dict_hooks` lifetime precondition: the `table_view` must outlive the builder.
  - **Refusal order** (R-3), in one static `append_data_field(into, data_tag, value)` (preferred) that
    both members call:
    1. `set_data` only: the owner and innermost-open-entry checks → `wire_invalid_field_format`,
       before any group or instance resolution;
    2. `is_framing_tag(data_tag)` → `wire_field_value_out_of_range`;
    3. `dict_hooks::none().length_tag_for_data(data_tag) == 0` → `wire_unexpected_tag` (FR-003: the
       authoritative table through the wire layer's lookup, never a builder-private list);
    4. `value.empty()` → `wire_field_value_out_of_range`. The octets are NOT subject to the
       printable-content guard (FR-005);
    5. append the Length node (the decimal octet count), then the Data node through the arena. If the
       second append throws `bad_alloc`, pop the Length too; return `wire_frame_too_large`.
  - **Compile-time property.** Make `is_framing_tag` `constexpr`, and add a `static_assert` that no
    row of `core::detail::standard_length_data_pairs` names a framing tag (INV-7).
  - **Commit (R-4, INV-6).** `validate_group_grammar` becomes the combined INV-5 + pair walk; it
    gains a `dict_hooks const&` parameter. For each container (the top-level `entries_` and each
    `group_instance::fields`) it feeds each node in order to a `wire::length_data_checker` built from
    `hooks_`:
    - a scalar as `observe(tag, value_bytes)`;
    - a group node as `observe(no_tag, {})`. The **empty value is normative**; never the count
      digits.
    Then it calls `finish()`. Any failure → `wire_invalid_field_format`, `out` untouched.
  - No new allocation: both nodes come from the existing arena, and there is no `new`/`malloc`
    (Article VIII/XV).
  - GREEN on T022 and T023, and on the full `wire_body_builder_test`.
  - **Zero-allocation witness for the new paths:** in `tests/wire/test_body_builder.cpp`, add a
    counted window (the existing global `operator new` counter) spanning construction with a
    `dict_hooks` argument (hooks built outside the window), `field_data`, a group entry's
    `set_data`, and commit. Assert a delta of 0. Show it RED with a mutant that heap-allocates in
    `append_data_field`.
- [X] T025 Via `phase-implementer`, update `include/fixpp/wire/length_data_check.hpp`'s header
  comment: "#418 is meant to be its second caller" becomes past tense (the builder is the second
  caller), with no count added (FR-016).
### 2d — `interpret_logon` refuses a malformed paired count (FR-020, owner ruling 2026-09-24), tests first

- [X] T070 Via `phase-implementer`, the FR-020 RED witnesses in
  `tests/session/length_data_session_scanner_test.cpp` (`session_length_data_scanner`, labelled
  `091` and added to `expected-ctest-091.txt` here).
  - `InterpretLogonMalformedCount.*`: the function directly, over the standard pair 95/96 (count
    ending on a non-SOH byte then `98=2`; count running past the frame then `98=2`; a malformed count
    followed only by `98=0`, so after the fix the refusal can come only from the count) and over a
    component-only custom pair through `dict_hooks::for_table_view` (then `98=2`). Each is accepted
    today: RED.
  - `LogonArmMalformedCount.*`: the acceptor (NotConnected) and initiator (LogonSent) arms reach
    Active today on the malformed-count Logon: RED.
  - Twins, GREEN before and after: well-formed count + `98=2` refused; + `98=0` accepted; no count +
    `98=2` refused; the custom-pair frame without the dictionary refused; and each arm's two
    well-formed controls.
  - C-ABI observers: `tests/capi/length_data_logon_refusal_test.cpp` `CapiLogonMalformedCount.*` (a
    C-ABI acceptor, raw TCP peer); the `onLogon` latch writer, both roles: `LogonArmMalformedCount.*`.
  - Pre-registered table and observations: evidence file §*interpret_logon fail-open — RED (T070)*.
- [X] T071 Via `phase-implementer`, implement FR-020 in `src/session/admin_messages.cpp`'s
  `interpret_logon`: a malformed count returns `core::error::session_invalid_logon` instead of
  breaking out of the scan. No new error code.
  - Replace the `break`'s comment with the condition and a pointer to FR-020; delete any sentence
    of the function's comments the change falsifies.
  - Invert `LengthDataSessionScanner.InterpretLogonStopsAtAMalformedCount` (renamed
    `InterpretLogonRefusesAMalformedCount`) to assert the refusal
    (FR-020 is its authority); edit no other pre-existing test without an orchestrator ruling.
  - GREEN: the unfiltered `session_length_data_scanner` binary passes, and the T070 twins still
    hold. Then run the full session suite (`ctest --test-dir build/linux-clang-debug -R '^session_'`
    plus every test that `codegraph_callers interpret_logon` reaches); a Logon built through a
    helper is invisible to a lexical grep, so the full run is the blast-radius check. Any other
    failure goes to the orchestrator.
- [X] T072 Via `phase-implementer`, update the `include/fix/c_api/version.h` 1.9 history comment
  (FR-019 as amended): the Logon effect is now FR-020's — a Logon carrying a malformed count, of a
  component/group-only pair or of a standard pair such as 95/96, is refused — replacing the
  "a required field follows the count" wording; the observers stay, on either role (the C-ABI
  latches are set by `onLogon` whatever the role). The `close` observer holds once the refused
  session has drained; `version.h` says so (a follow-up edit after `03bc2d4a`, with the freeze
  re-pin). Re-run
  `tools/check_capi_freeze.sh` (it must fail on exactly `version.h`, then re-pin
  `tools/capi_freeze.sha256` and pass).
- [X] T074 Via `phase-implementer`, add the FR-020 witnesses the scoped Gate A round 1 found missing
  to `tests/session/length_data_session_scanner_test.cpp` (`session_length_data_scanner`, already
  labelled `091` and in `expected-ctest-091.txt`; a manifest change is needed only if a new ctest
  entry is registered).
  - **Orphan-Length pins, function level, over the standard table (no hooks argument).** The Logon
    body is T070's `logon_body_with_count` shape with the `96=x␁` field removed, so `95=999␁` is
    followed directly by `98=…␁` and the count is never applied:
    - `InterpretLogonMalformedCount.OrphanOverrunningLengthDoesNotHideEncryptMethod`: `…108=30␁95=999␁98=2␁`
      is refused with `session_invalid_logon`;
    - `InterpretLogonMalformedCount.TwinOrphanOverrunningLengthWithZeroEncryptMethodIsAccepted`:
      `…108=30␁95=999␁98=0␁` is accepted.
    - Both are GREEN on arrival (not RED witnesses). Each is proven by its own mutant in T026
      (quickstart §3 rows "the carry applies a pending count whatever the next field's tag", alone
      and combined with the `break` mutant).
  - **Arm cells, both roles**, in the `LogonArmMalformedCount` fixture (`state_after_peer_logon`),
    asserting the session is not established exactly as the existing
    `AcceptorMalformedCountDoesNotHideEncryptMethod` / `InitiatorMalformedCountDoesNotHideEncryptMethod`
    cells do:
    - `AcceptorCountRunningPastTheFrameIsNotEstablished` and
      `InitiatorCountRunningPastTheFrameIsNotEstablished`: count `999`, then `98=2`;
    - `AcceptorMalformedCountWithZeroEncryptMethodIsNotEstablished` and
      `InitiatorMalformedCountWithZeroEncryptMethodIsNotEstablished`: count `2` (ends on a non-SOH
      byte), then `98=0`, the shape a conforming peer's Logon has.
    - The fix has landed, so their RED on the unfixed code is shown by T026's `break` mutant, which
      must turn each of them RED; record it with T026.
  - **Equality-boundary pair, function level, over the standard table (no hooks argument)** (scoped
    Gate A round 2, P3-1). The body is `35=A␁34=1␁49=TW␁52=…␁56=ISLD␁108=30␁98=0␁95=<N>␁96=x␁`: every
    field `interpret_logon` validates comes before `95`, and RawData(96) is the last body field, so
    its counted extent runs into the trailer. Frame it with `make_frame` after N is chosen (N is
    inside the body, so BodyLength is computed from the final body). Let R be the number of bytes
    from the first byte of 96's value to the end of the framed message, trailer included, measured
    on the framed bytes, not hard-coded:
    - `InterpretLogonMalformedCount.CountReachingTheFrameEndIsRefused`: N = R, so the counted extent
      reaches the end of the frame; refused with `session_invalid_logon`;
    - `InterpretLogonMalformedCount.TwinCountEndingOnTheFinalSohIsAccepted`: N = R − 1, so the byte
      after the counted extent is the trailer's final SOH; accepted.
    - Each cell asserts, on its final framed bytes, that R equals its N (first cell) or N + 1
      (second), so a change to the framing helper cannot move the boundary silently.
    - The first pins FR-020 item 3's "reaches" through the session function; the second pins "the
      whole framed message, trailer included" (a scan over the body alone would refuse it). The
      second is a `Twin*` cell, GREEN before and after the fix. The first must turn RED under
      T026's `break` mutant, since every field `interpret_logon` validates precedes the count, so a
      scan that stops there accepts the Logon; record it with T026.
    - A `>=` → `>` mutant in `counted_value_end` does not reach these cells: the static assertion
      in `include/fixpp/wire/length_data_carry.hpp` that a count reaching the end of the buffer is
      malformed (`counted_value_end(detail::counted_value_end_probe, 0, 4)`) rejects it at compile
      time (T026 records that build failure). This pair is the runtime witness at `interpret_logon`.
  - GREEN: the unfiltered `session_length_data_scanner` binary passes. Record the cells, the command
    and the result in `.specify/decisions/091-data-field-bytes-evidence.md` §*FR-020 GREEN
    (T071–T072)*, as a dated addendum.
- [X] T075 Via `phase-implementer`, align the header and source comments with FR-020 as rewritten by
  the scoped Gate A rounds 1 and 2, with the freeze re-pin. The short form is FR-020's, verbatim: *a
  Length immediately followed by its paired Data whose counted extent reaches or passes the end of
  the whole framed message, or whose following byte is not SOH*.
  - `include/fix/c_api/version.h` 1.9 history: replace "interpret_logon refuses a Logon carrying a
    malformed count" with the short form, keeping the spelling
    `RawDataLength(95) and RawData(96)` (a slash-joined number pair trips the comment lint's ratio
    rule, `03bc2d4a`). Replace the two-observer sentence with the class (every call whose result
    depends on the session having logged on) and its members: `fixpp_session_is_established` stays
    false; `fixpp_session_close`, once the refused session has drained, returns
    `FIXPP_ERR_THREAD_SESSION_LIFECYCLE`, not `FIXPP_ERR_OK`; `fixpp_session_send` on that session
    returns `FIXPP_ERR_SESSION_INVALID_STATE`, not `FIXPP_ERR_OK`; neither the receive callback nor
    the toApp callback (`fixpp_session_register_send_callback`) is ever invoked for it. The
    history keeps the consolidated list; it is no longer the only carrier of any of these five.
  - `include/fix/c_api/session.h`: each of the five observer declarations' documentation carries a
    BREAKING (C-ABI 1.9) FR-020 clause (`[const §X.7]`: "each affected declaration"). Each clause
    states the refused-Logon shape by the short form, not "a malformed count", and says it holds on
    either role:
    - `fixpp_session_close`: once a session whose Logon was refused under FR-020 has drained, close
      returns `FIXPP_ERR_THREAD_SESSION_LIFECYCLE` where it returned `FIXPP_ERR_OK` (the existing
      reaped-session paragraph stays; the clause names the drained precondition);
    - `fixpp_session_is_established`: for that session it stays false where it became true;
    - `fixpp_session_send`: its existing BREAKING note gains the clause (a send on that session,
      issued after that Logon, which returned `FIXPP_ERR_OK`, now returns
      `FIXPP_ERR_SESSION_INVALID_STATE`); its existing sentence "For a send this refuses, the toApp
      callback … is not invoked" is extended to cover this refusal too, since `Engine::send` refuses
      at its Active check on the session strand before it calls `Session::send`, so the send never
      reaches `send_impl`, which builds the toApp view;
    - `fixpp_session_register_callback`: its existing note gains the clause (inbound application
      messages on that session, delivered before, are never delivered);
    - `fixpp_session_register_send_callback`: it has no BREAKING note today; add one naming both of
      its effects, since a BREAKING note names every effect its declaration shows (research.md R-11
      Classification): FR-019's (for a send `fixpp_session_send` refuses because of a malformed
      pair, the callback is not invoked) and FR-020's (on that session the callback, invoked before
      for each send, is never invoked). The view's content effect stays in the reader paragraph.
  - `include/fixpp/session/admin_messages.hpp` (`interpret_logon`'s comment) and the refusal comment
    in `src/session/admin_messages.cpp`'s `interpret_logon`: state the refused shape by the short
    form (the refusal comment's "it runs past the frame" omits the equality boundary) and the armed
    condition (the count applies only when the next field is the Length's paired Data), so neither
    reads as "any Length count". Delete any sentence the change falsifies; add no count or list of
    sites.
  - Before writing a clause, re-read the source its mechanism names (`Engine::send` in
    `src/session/engine.cpp`, `counted_value_end` in `include/fixpp/wire/length_data_carry.hpp`);
    a clause that disagrees with the source goes to the orchestrator, not into the header.
  - Run `tools/check_capi_freeze.sh`: it must fail on exactly `version.h` and `session.h`, then
    re-pin `tools/capi_freeze.sha256` and pass. Run
    `.claude/scripts/check-comment-claims.py --root <tree> --base origin/main` on the result.
- [X] T076 Via `phase-implementer`, record in `.specify/decisions/091-data-field-bytes-evidence.md`
  §*Malformed-count scan sites* (dated addendum) the complement recipe for the scan-site population:
  `grep -rn 'data_tag_for_length\|counted_value_end' src include` beside the existing
  `grep -rn 'read_value(' src include`. Classify each site it adds, from source, as a refusal gate
  or a reader, and state what it does at a malformed count (candidates to check: `OffsetTable::build`,
  `MessageView`'s `field_iterator::advance` and its `src/` users, `length_data_checker`, and
  `Session::validate_inbound_`'s parse-failure path, which falls through to `interpret_logon`). A
  site that is a refusal gate and fails open at a malformed count goes to the orchestrator; FR-020's
  scope rests on this population.

- [X] T026 Via `phase-implementer`, run the Foundational mutants from quickstart §3 in a scratch copy.
  Each one must be RED on the named test (or, for a compile-time kill, fail to build), then GREEN
  after revert:
  - "the new walk skips non-field children instead of breaking" → C-2.5a arm (iii) (T007);
  - "the new walk visits only components and their direct `<group>` children" → arms (v) and (vi);
  - "groups are walked depth-first, right after their container" → arm (vii), all three placements;
  - "the group walk is entered only from messages and component definitions" → arm (viii) (a) and
    (b) (arm (vii)(c) cannot die here: a walk that skips header groups never records group A's
    pair, so component B wins as the arm expects; the depth-first row above kills it);
  - "one non-FIX50SP2 drift leg's probe returns nothing" (e.g. FIX44's `message_fields()` result
    emptied) → that leg's non-empty assertion (T006);
  - "the loader's component/group walk is removed" (C-ABI view) → all of these:
    - T008's malformed commit (`FIXPP_ERR_OK` again), its additive-widening assertion and its
      `set_data(len = 0)` assertion (`FIXPP_ERR_TYPE_MISMATCH` again);
    - T009 (`FIXPP_ERR_OK` again);
    - T010's inbound-drop witness and both reader witnesses; plus the T006 FIX50SP2 leg and C-2.5a
      arms (i), (ii), (iv), (v), (vi), (vii) and (viii);
  - "`FIXPP_C_ABI_VERSION_MINOR` set back to 8" → `tests/capi/version_test.cpp`'s exact-version cell;
  - "`interpret_logon` breaks at a malformed count instead of refusing" (FR-020) → every T070 RED
    cell, on the function and on both arms, the inverted pin
    `LengthDataSessionScanner.InterpretLogonRefusesAMalformedCount`, every T074 arm cell, and
    T074's `InterpretLogonMalformedCount.CountReachingTheFrameEndIsRefused`; every twin stays GREEN
    (the exact list is quickstart §3's row);
  - "the carry applies a pending count whatever the next field's tag" (`length_data_carry::read_value`
    drops the `data_tag_ == tag` test) (FR-020) → T074's
    `TwinOrphanOverrunningLengthWithZeroEncryptMethodIsAccepted`;
  - the same mutant combined with the `break` mutant above → T074's
    `OrphanOverrunningLengthDoesNotHideEncryptMethod`;
  - "`counted_value_end` accepts a count that reaches the end of the buffer" (`count >=` → `count >`
    in its bound) (FR-020) → the build: the static assertion in
    `include/fixpp/wire/length_data_carry.hpp` that a count reaching the end of the buffer is
    malformed (`counted_value_end(detail::counted_value_end_probe, 0, 4)`) must fail to compile.
    Apply it in the scratch copy, build `session_length_data_scanner`, and record the diagnostic's
    first line; this is a compile-time kill, and T074's equality-boundary pair is the runtime
    witness.
  Record each mutant, command and RED line in `.specify/decisions/091-data-field-bytes-evidence.md`
  §*Mutants*.
- [X] T027 Run the **full** `ctest --test-dir build/linux-clang-debug --output-on-failure`, after
  codegen freshness.
  - The loader change moves FIX 5.0 SP2 answers that unlabelled tests read: the `table_view`
    differential, `length_data_table_test`, and the codegen determinism golden.
  - Expected failures, by name: the four flipped `DataField_EncodedText_*_418` pins (T005), which
    stay RED until T036. Any other failure outside the intended moves (T013, T014) goes back to the
    orchestrator; do not "fix" a test to match without a ruling.

**Checkpoint**:
- The loader pairs the five v50sp2 pairs, and every drift leg is GREEN and non-empty.
- C-ABI 1.9 is declared, re-pinned and witnessed RED → GREEN.
- `field_data`/`set_data` and the commit check are GREEN on C-1.
- `interpret_logon` refuses a malformed paired count (FR-020): the T070 and T074 cells are GREEN and
  their twins hold.
- The four flipped `_418` pins are still RED: the generated builder still routes through `field()`.

### 2e — The C-ABI commit feeds a group node an empty value (FR-021, owner ruling 2026-09-25), tests first

Added during `/speckit-implement`, after `/simplify` (T053) measured the defect (evidence file
§*/simplify (T053)*, M1). It is Foundational in kind but runs **after T053 and before T054**, so every
later measurement is of the final candidate. **Entry condition (met 2026-09-25):** the scoped Gate A round on
the FR-021 delta has converged (round 3), `/speckit-analyze` has been re-run, and the owner has
re-signed the plan (plan.md Constitution Check, X row; §Gate A, *Scoped FR-021 round*;
`.specify/decisions/091-data-field-bytes-gatea.md` §"Addendum — scoped Gate A round on FR-021 (2026-09-25)"). T078 may start. No fuzz harness reaches `fixpp_msg_commit` and FR-021 changes no parser, so no
fuzz task is added (re-derive: `git grep -ln -e fixpp_msg_commit -e fixpp_msg_group_begin -- '*fuzz*'` (empty; a hit reopens this), positive control `git grep -ln -e on_inbound_frame -- '*fuzz*'`, which must list `tests/fuzz/fuzz_session_recovery_admin_parse.cpp`).

- [X] T078 Via `phase-implementer`, the FR-021 RED witnesses in `tests/capi/length_data_setters_test.cpp`
  (`capi_length_data`, already labelled `091` and in `expected-ctest-091.txt`; no manifest change
  unless a new ctest entry is registered). Every cell except the dictionary cell below uses `DictFreeFixture` (a session with no
  dictionary, so the pairs are the standard table alone and a group opens on any non-framing tag),
  `ASSERT`s `FIXPP_ERR_OK` on every setup call so a RED cannot land on the wrong call, and gives each
  group instance a non-pair first field (e.g. `79=A1`), so no cell can pass through INV-4
  (`FIXPP_ERR_TYPE_MISMATCH`, a different code).
  - **RED cells** (each asserts `fixpp_msg_commit` returns `FIXPP_ERR_WIRE_CONFORMANCE` and yields no
    payload; on the unfixed code each returns `FIXPP_ERR_OK`, and the emitted bytes go into the
    commit message as the pre-change form):
    - `CapiCommitGroupNode.GroupTaggedAsALengthDoesNotCompleteThePair` (the Length shape, M1):
      `fixpp_msg_group_begin(354)`, one instance, `fixpp_msg_group_end`, then
      `fixpp_msg_set_bytes(355, "x", 1)`; today it emits `35=D␁354=1␁79=A1␁355=x␁`;
    - `CapiCommitGroupNode.GroupTaggedAsTheDataDoesNotCompleteThePair` (the Data shape):
      `fixpp_msg_set_int(354, 1)`, then `fixpp_msg_group_begin(355)` with one instance; today
      `observe(355, "1")` satisfies the awaited one-byte Data;
    - `CapiCommitGroupNode.EmptyGroupTaggedAsTheDataDoesNotCompleteThePair` (analyze E2): the Data
      shape with **zero** instances: `fixpp_msg_set_int(354, 1)`, then `fixpp_msg_group_begin(355)`
      and `fixpp_msg_group_end` with no entry; today `observe(355, "0")` satisfies the awaited
      one-byte Data. Confirm the unfixed code returns `FIXPP_ERR_OK` by running it before writing it
      as RED (a zero-instance group may be refused or serialised differently elsewhere); a
      non-`OK` result goes to the orchestrator;
    - `CapiCommitGroupNode.EmptyGroupTaggedAsADictionaryDataDoesNotCompleteThePair` (Gate A FR-021
      r1, Codex P2): on a session whose dictionary is a **new** fixture XML constant (e.g.
      `kGroupOnPairHalfFix42Xml` in `tests/capi/length_data_capi_support.hpp`; **do not modify**
      `kLengthDataFix42Xml`, whose other cells would then meet a group collision on 5002). It
      declares 5001 LENGTH immediately followed by 5002 DATA in `<fields>` (so the loader pairs them
      by adjacency), and a NewOrderSingle carrying the 5001 field and a `<group>` named after 5002
      with a non-pair first member. Preconditions, each asserted two-sided before the RED, none of them on the RED message:
      - **Load.** `make_length_data_session_cfg(…, kGroupOnPairHalfFix42Xml)` returns without
        throwing (the loader is C++ `XmlLoader::load_from_string`, which throws on a rejected
        document and returns no C error code). Its `fixpp_session_config_set_dictionary` returns
        `FIXPP_ERR_OK` (the helper already `EXPECT`s it), and the session opens with `FIXPP_ERR_OK`.
      - **Pair.** In the test body, load the same constant a second time through
        `fixpp::dict::XmlLoader{}.load_from_string` and `ASSERT` that `length_pair_data_tag(5001) ==
        5002` on the `Dictionary`, and that `as_table_view().data_pair_length_tag(5002) == 5001`.
        Negative arm: `length_pair_data_tag` of a declared non-LENGTH field of the same XML is `0`.
        Do **not** observe the pair through `fixpp_msg_set_data(…, 5002, …)`: on this XML it returns
        `FIXPP_ERR_TYPE_MISMATCH` through `is_group_collision`, because 5002 is a group count,
        whatever the pair.
      - **Group.** On a throwaway control message `ctl` of the same session, which is never
        committed: `fixpp_msg_group_begin(ctl, 5002, &b)` returns `FIXPP_ERR_OK`, then
        `fixpp_msg_group_end(ctl, b)`; and `fixpp_msg_group_begin(ctl, 5001, &b2)` returns
        `FIXPP_ERR_TYPE_MISMATCH`, since the dictionary declares no group on 5001. That shows the
        acceptance is the dictionary's answer and not an absent gate.

      **RED**, on a fresh message `msg` whose only calls are these:
      `fixpp_msg_set_bytes(msg, 5001, "1", 1)` (`ASSERT` `FIXPP_ERR_OK`),
      `fixpp_msg_group_begin(msg, 5002, &g)` (`ASSERT` `FIXPP_ERR_OK`), `fixpp_msg_group_end(msg, g)`
      with no entry (a zero-instance group needs no delimiter or context setup), then
      `fixpp_msg_commit`. On the unfixed code commit returns `FIXPP_ERR_OK` (the digit `0` satisfies
      the awaited one-byte 5002); after the fix it returns `FIXPP_ERR_WIRE_CONFORMANCE`. Any
      precondition that fails, or a commit that is not `FIXPP_ERR_OK` on the unfixed code, goes to
      the orchestrator with the observed codes. The cell does not go into the file, and this task
      draws no conclusion about FR-021's dictionary branch;
    - `CapiCommitGroupNode.NestedGroupTaggedAsALengthDoesNotCompleteThePair`: inside one instance of
      a group tagged 78, `fixpp_entry_group_begin(354)` with one instance, then
      `fixpp_entry_set_string(355, "x", 1)` on the outer entry (there is no `fixpp_entry_set_bytes`),
      so the shape sits in a group instance, which `check_length_data` checks as its own container.
  - **Twins, GREEN before and after** (same fixture, same instance contents):
    - `CapiCommitGroupNode.TwinNonPairGroupWithABareSiblingDataIsRefused`: the Length-shape cell
      with the group tagged 78; refused `FIXPP_ERR_WIRE_CONFORMANCE` (an orphan Data), before and
      after;
    - `CapiCommitGroupNode.TwinNonPairGroupAloneCommits`: that group tagged 78 with no sibling;
      commits `FIXPP_ERR_OK`;
    - `CapiCommitGroupNode.TwinWellFormedPairNextToAGroupCommits`: the group tagged 78, then
      `fixpp_msg_set_data(355, "x", 1)`; commits `FIXPP_ERR_OK` with the bytes
      `…78=1␁79=A1␁354=1␁355=x␁`;
    - `CapiCommitGroupNode.TwinLonePairLengthGroupIsRefused` (analyze E1): a group tagged 354 with
      one instance `79=A1` and nothing else; refused `FIXPP_ERR_WIRE_CONFORMANCE` before (a Length
      whose Data never comes) and after (an empty count);
    - `CapiCommitGroupNode.TwinLonePairDataGroupIsRefused` (analyze E1): a group tagged 355 with
      one instance `79=A1` and nothing else; refused `FIXPP_ERR_WIRE_CONFORMANCE` before and after
      (a Data not preceded by its Length);
    - `CapiCommitGroupNode.TwinLengthBeforeAnEmptyNonPairGroupIsRefused` (Gate A FR-021 r1, P2-1):
      `fixpp_msg_set_int(354, 1)`, then `fixpp_msg_group_begin(78)` and `fixpp_msg_group_end` with
      no entry, then `fixpp_msg_set_bytes(355, "x", 1)`; refused `FIXPP_ERR_WIRE_CONFORMANCE` before
      (`observe(78, "0")` while 355 is awaited) and after (`observe(78, {})`).
  - Before writing, re-read `check_length_data` and `length_data_checker::observe`
    (`include/fixpp/wire/length_data_check.hpp`) and confirm each RED cell's pre-change `FIXPP_ERR_OK`
    by running it; a cell that is not RED on the unfixed code goes to the orchestrator, not into the
    file.
  - Record the pre-registered table (cell, expected before, expected after) and the observed RED in
    `.specify/decisions/091-data-field-bytes-evidence.md` §*FR-021 RED (T078)*.
- [X] T079 Via `phase-implementer`, implement FR-021 in `src/capi/message_write.cpp`'s
  `check_length_data`: a group node is fed as `checker.observe(e.tag, {})`, with an empty span, as
  `body_builder::commit` does (R-4), and the instance recursion is unchanged. Delete the count-digit
  buffer and the `std::to_chars` call, and any include only they used.
  - Rewrite the function's header comment: state the rule as a condition (a group node is fed an
    empty value, so a group whose count tag is a pair half fails the check, and a Length right
    before any group is a Length not followed by its Data), and name the superseding decision in
    it ("091 FR-021 (fixpp#506)"). Add no count, list of shapes or line citation.
  - GREEN: the unfiltered `capi_length_data` binary passes, every T078 RED cell is GREEN and
    the twins still hold. Then the full `ctest --test-dir build/linux-clang-debug --output-on-failure`
    (a group built through a helper is invisible to a lexical grep; the full run is the blast-radius
    check). Any other failure goes to the orchestrator; edit no pre-existing test without a ruling.
  - Record the cells, commands and results in the evidence file §*FR-021 GREEN (T079–T080)*.
- [X] T080 Via `phase-implementer`, the FR-021 declaration text and the freeze re-pin
  (`[const §X.7]`: "each affected declaration"):
  - `include/fix/c_api/message.h`, `fixpp_msg_commit`: a BREAKING (C-ABI 1.9) FR-021 clause beside
    the existing FR-019 one (whose cause, the loader, is a different one): a group whose count tag is
    the Length or the Data half of a pair (the FIX standard's, or the session dictionary's) is not
    read as that half; such a group, which returned `FIXPP_ERR_OK` when its instance-count digits
    completed the pair, now returns `FIXPP_ERR_WIRE_CONFORMANCE`. Extend its
    `FIXPP_ERR_WIRE_CONFORMANCE` return-code line with that case, tagged in line
    `(1.9, BREAKING)`, as `fixpp_msg_remove_tag`'s `(1.7, BREAKING)` clause is, so the line's older
    `(1.6, BREAKING)` label does not date it.
  - `include/fix/c_api/version.h` 1.9 history: one FR-021 sentence naming `fixpp_msg_commit` and
    the same condition, saying that it is independent of the loader change (the history's opening
    cause), applies to standard pairs, and is reachable on every session with no dictionary, and on
    a dictionary session only where that dictionary declares a group on a pair-half tag.
  - No other declaration changes (FR-021's classification: `fixpp_msg_group_begin`,
    `fixpp_entry_group_begin`, the setters and `fixpp_session_send` return what they did). Before
    writing, re-read the source T079 left; a clause that disagrees with it goes to the orchestrator.
  - Run `tools/check_capi_freeze.sh`: it must fail on exactly `message.h` and `version.h`, then
    re-pin `tools/capi_freeze.sha256` and pass. Run
    `.claude/scripts/check-comment-claims.py --root <tree> --base origin/main` on the result.
- [X] T081 Via `phase-implementer`, run the FR-021 mutants from quickstart §3 in a scratch copy (the
  T026 procedure: RED on the named cells, GREEN after revert, a clean `git diff` of the scratch
  copy against the PR head):
  - "`check_length_data` feeds a group node its instance-count digits again" → the T078 RED
    cells (`CapiCommitGroupNode.GroupTaggedAsALengthDoesNotCompleteThePair`,
    `.GroupTaggedAsTheDataDoesNotCompleteThePair`,
    `.EmptyGroupTaggedAsTheDataDoesNotCompleteThePair`,
    `.EmptyGroupTaggedAsADictionaryDataDoesNotCompleteThePair`,
    `.NestedGroupTaggedAsALengthDoesNotCompleteThePair`); every `Twin*` cell stays GREEN;
  - "`check_length_data` does not observe a group node at all" (the node skipped, not fed) →
    `CapiCommitPairs.RefusesALengthSeparatedFromItsDataByAGroup`, since a skipped group lets a
    Length pair with a Data after it; this shows the fix feeds the node rather than dropping it.
    That cell's group has one instance, so it kills only the unconditional skip;
  - "`check_length_data` skips a zero-instance group node (`if (e.instances.empty()) continue;`)"
    (Gate A FR-021 r1, P2-1) → `CapiCommitGroupNode.TwinLengthBeforeAnEmptyNonPairGroupIsRefused`,
    which commits `35=D␁354=1␁78=0␁355=x␁` under it;
  - "`check_length_data` feeds `{}` only for a group tag that `dict_hooks::none()` pairs, and the
    count digits for a dictionary-only pair half" (Gate A FR-021 r1, Codex P2) →
    `CapiCommitGroupNode.EmptyGroupTaggedAsADictionaryDataDoesNotCompleteThePair`. Not covered,
    recorded (Gate A FR-021 r2, P3-2): a mutant that feeds count digits only for a **dictionary
    Length** half survives (the dictionary cell is Data-side and zero-instance; a Length-side kill
    needs a one-instance group with delimiter setup, since a zero-instance node's `"0"` is refused
    as a zero count either way). No cell is added: killing it needs a condition on both the pair's
    side and its source, which no plausible implementation of FR-021 has. Recorded as a residual in
    the evidence file §*Mutants*, not run;
  - "`check_length_data` feeds a group node under a non-pair tag (`observe(0, {})`) instead of its
    own tag" (analyze E1) → `CapiCommitGroupNode.TwinLonePairLengthGroupIsRefused` and
    `.TwinLonePairDataGroupIsRefused`, which commit under it; this shows the group's own tag is
    what the check reads.
  Record each mutant, command and RED line in the evidence file §*Mutants*, as a dated addendum.
- [X] T082 Orchestrator-authored text (T079 landed, so the ledger describes shipped behaviour):
  - `spec/behaviors-and-limitations.md` B-091-4 (the live text): add FR-021's effect as data-model.md's
    B-091-4 row states it: the condition (a group whose count tag is a pair half is refused at
    `fixpp_msg_commit`, where it committed when its count digits completed the pair), the
    shapes as witnesses, the reachability condition (every session with no dictionary; a dictionary
    session only where that dictionary's `group_first_field` is nonzero for a pair-half tag), and
    that `fixpp_session_send` of such a payload is unchanged;
  - `spec/feature-catalogue.md`: every CA row naming `fixpp_msg_commit`, derived by **reading**
    each row's function list (CA-009 abbreviates it as `set_*/remove_tag/commit/…`, so a grep for
    `fixpp_msg_commit` misses it), gains FR-021's effect in its C-ABI 1.9 note;
  - `brain/components/wire.md`: the group-node paragraph's "That C-ABI defect is fixpp#506" →
    FR-021 fixed it, and the C-ABI commit now feeds a group node an empty value too;
    `brain/components/c-api.md`: the C-ABI 1.9 entry gains `fixpp_msg_commit`'s FR-021 effect;
  - the parent's `phases/phase-4/issue-batches.md` Parked entry for #506 is updated at close-out
    (T063), not here.

---

## Phase 3: User Story 1 — Send non-ASCII text in an encoded Data field through a generated builder (Priority: P1) 🎯 MVP

**Goal**: every generated builder routes each coupled Length+Data member through
`field_data`/`set_data` (FR-010), and messages that can carry an `Encoded*` field gain
`message_encoding` (FR-011a). SOH, C0 controls and `0x80–0xFF` then round-trip.

**Independent test**: build a v44 `NewOrderSingle` with `encoded_text` holding SOH, a C0 byte, `0x80`
and `0xFF` in turn: success, exact bytes for 354/355, and a re-parse yields identical octets. These
are the four flipped `_418` pins, plus C-2.6.

### Tests for User Story 1 (write first; RED until T036)

- [X] T028 [US1] Via `phase-implementer`, create the **exact completeness census** (C-2.2, FR-010,
  FR-012, SC-003) as a new standalone executable `codegen_091_data_census_test` in
  `tests/codegen/CMakeLists.txt`.
  - **Standalone, as the `[const §VII.8]` exact-set completeness-gate exemption.** It carries label
    `091` and links `fixpp_dictionary`. Say why in the CMakeLists comment.
  - **Expected** multiset, per shipped dictionary version (v42, v44, v50sp2, vlatest), from the
    dictionary sources (walker below):
    every (message, structural path, Length tag, Data tag, arm ∈ {top, nested}) where
    Length/Data is a row of `core::detail::standard_length_data_pairs` and both appear at that
    level. Pair-ness comes **never** from `FieldRef::length_pair_data_tag`.
  - **Actual** multiset, parsed from each message's `.builder.cpp`: every `field_data(D, …)` /
    `set_data(D, …)`, with its message, its enclosing group chain, and the Length tag of the
    preceding `static_assert`.
  - Separately, assert that each message's `.builder.cpp` and `.builder.inl` call-site multisets are
    identical.
  - **Pass:** actual == expected, and no `field_data`/`set_data` names a `D` outside the expected
    set. No pinned count.
  - **Orphan-half check:** in every regenerated version, zero calls to `field(T, …)` or
    `set_<kind>(T, …)` other than `field_data`/`set_data` for any tag T of the standard table. Keyed
    on the call name and the tag, never on a local's name.
  - **Message-encoding set:** the messages whose Args carry `message_encoding` equal the set that
    T034's rule selects, recomputed on the census's own walk. The member is last, and its emit is the first body statement.
  - **Expected-set source (non-circular):** an independent walker over `dictionaries/*.xml` (pugixml,
    as `codegen_vlatest_census_test` does) and the Orchestra FIX Latest XML for vlatest, never
    `tools/codegen/fixpp-codegen/ir.cpp` or the emitter's own `resolve_level`, so the census cannot
    inherit the emitter's coupling decision. Its structural path must follow the same component
    expansion the generated Args use.
  - **Written before the emitter change (TDD):** on the old emitter every expected tuple is missing,
    so it is RED; quote that output. It turns GREEN at T035. Control (a) (T037) needs the new emitter.
  - It takes the generated-source root as an argument (default `${FIXPP_CODEGEN_OUT}`), so the T037
    controls can point it at a mutant's output.
- [X] T029 [P] [US1] Via `phase-implementer`, add the C-2.6 exhaustive witnesses to
  `tests/session/test_067_builder_roundtrip.cpp` (`test_067_builder_roundtrip`: wire + dictionary +
  v44 builders).
  - Two parameterized tests over every octet `0x00–0xFF`. Each asserts the raw frame boundaries
    (Length value `1`, the Data octet verbatim) and that a re-parse through the inbound parser
    recovers the octet.
    - **top level:** v44 `NewOrderSingle` with `encoded_text = {b}`;
    - **nested:** a coupled member inside a v44 repeating-group entry (e.g.
      `EncodedLegIssuer(618/619)` in a leg group).
  - Name the message and group from the C-2.2 census (T028, run on the old emitter, which lists the
    expected tuples), and cite that census, not a remembered name.
  - RED at first: the Data goes through `set_string`.
- [X] T030 [US1] Via `phase-implementer`, add the C-2.3 compile-time presence checks.
  - Each check is a `constexpr bool` computed from a `requires` expression and asserted with
    `EXPECT_TRUE`/`EXPECT_FALSE`, never a `static_assert`: a compile error would stop the whole
    binary and hide T029's runtime RED.
  - In `tests/session/test_067_builder_roundtrip.cpp`:
    - `fixpp::v44::NewOrderSingleArgs` has `message_encoding` (its position, last, is T028's
      parse check; a `requires` expression cannot see member order);
    - one v44 message with no `Encoded*` field has **no** `message_encoding` member. The census
      (T028) names it, e.g. an app message; `Heartbeat` only if it is in the generated set.
  - In `tests/session/test_077_allversions_builder_roundtrip.cpp`, the same checks for v50sp2:
    - a message whose **only** encoded field is `DerivativeEncodedIssuer(1278)`,
      `DerivativeEncodedSecurityDesc(1281)` or `InstrumentScopeEncodedSecurityDesc(1621)` **has**
      the member (named by census);
    - one v50sp2 message with no `Encoded*` field has none.
  - v42 and vlatest have no per-version negative here; T028's set equality discharges C-2.3's
    "one message per version" for them (recorded in the evidence file).
  - RED (a test failure, not a build failure) at first.

- [X] T031 [P] [US1] Via `phase-implementer`, add the v50sp2 Length-delimited group witness (R-6) to
  `tests/session/test_077_allversions_builder_roundtrip.cpp`.
  - Build one `NoPaymentStreamFormulas` entry (delimiter `PaymentStreamFormulaLength` 43109) whose
    Data holds SOH. Assert commit success, `43109=<octet count>` first in the entry, and a re-parse
    that recovers the Data.
  - Written after T014 and before T033: on T014's intermediate goldens the pair is coupled in the old two-call
    form, and the Data half still takes the string path (`set_string`), which refuses SOH. Run it and quote that RED; it turns GREEN at T036.
- [X] T032 [P] [US1] Via `phase-implementer`, add a vlatest coupled-pair round trip to
  `tests/session/test_077_allversions_builder_roundtrip_vlatest.cpp`.
  - Use one vlatest message with a coupled Data member whose value holds SOH and `0xFF`. Assert
    verbatim emit, Length = octet count, and a re-parse (spec Edge Cases, "FIX Latest").
  - Written before T033 and run RED (the old two-call form routes the Data through `set_string`);
    quote it. It turns GREEN at T036.
  - The test is gated on `FIXPP_CODEGEN_FIX_LATEST`, which is ON by default and in
    `linux-clang-debug`.

### Implementation for User Story 1

- [X] T033 [US1] Via `phase-implementer`, update the emitter's coupled arms in
  `tools/codegen/fixpp-codegen/emit_builders.cpp` (C-2.1, R-7).
  - In `emit_level_body`'s coupled branch, **both** arms emit exactly the C-2.1 shape:
    - `static_assert(::fixpp::wire::dict_hooks::none().length_tag_for_data(D) == L);`
    - then `auto r_pair = bb.field_data(D, ::std::as_bytes(::std::span{*args.m}));` at the top level,
      or `<ehN>.set_data(D, …)` nested;
    - `if (!r_pair) return ::std::unexpected(r_pair.error());`.
  - No `field(L, …)`/`set_int(L, …)` is emitted for a coupled item, and the token `r_data` disappears
    from the output.
  - The Args member keeps `std::optional<std::string_view>` (FR-011, SC-004: existing ASCII callers
    compile and behave identically, except the owner-ratified v50sp2 carve-out of T014).
  - `tools/codegen/fixpp-codegen/gen_util.hpp`: update the stale "is already String" comment on the
    Data half's kind. No new `TypeKind`.
- [X] T034 [US1] Via `phase-implementer`, add `message_encoding` to
  `tools/codegen/fixpp-codegen/emit_builders.cpp` (C-2.3, R-8).
  - **Selection:** per message, true when any member at any depth (the `group_order` tree
    `resolve_level` walks) is the Data half of a **standard** pair
    (`core::detail::standard_length_data_pairs`) whose `FieldIR` name **contains** `Encoded`.
  - **Member:** top-level Args gain `std::optional<std::string_view> message_encoding;`, appended
    **last**.
  - **Emit:** `bb.field(347, *args.message_encoding)`, when set, as the **first** body statement,
    before any other member. Top level only.
  - Accessor collisions go through the existing `uniquify_accessor`.
  - Not enforced: FIX 4.4's "required if any Encoded fields" stays the caller's decision.
- [X] T035 [US1] Via `phase-implementer`, run codegen freshness (quickstart §1) and regenerate the 078
  builder goldens for **v42, v44, v50sp2 and vlatest** under
  `specs/078-precompiled-builder-libs/contracts/golden/{v42,v44,v50sp2,vlatest}/`. The v50sp2 goldens
  start from T014's intermediate state.
  - Size the diff with R-5's recipe (re-run it; do not reuse a figure).
  - Every other read-tier pin in `fixpp::dict::read-tier-byte-diff` is unchanged. Only T013's two
    have moved; anything else is a stop (spec Assumptions).
- [X] T036 [US1] Build, then run `test_067_builder_failclosed`, `test_067_builder_roundtrip`,
  `test_077_allversions_builder_roundtrip`, `test_077_allversions_builder_roundtrip_vlatest` and
  `codegen_091_data_census_test`.
  - The four flipped `_418` pins (T005), the census (T028), the C-2.6 witnesses (T029), the C-2.3
    checks (T030), the R-6 witness (T031) and the vlatest round trip (T032) turn **GREEN**.
  - US1 AS-4 (coupled member unset → neither half emitted): name the existing round-trip or golden
    case that builds a message with a coupled member unset, and cite it; if none exists, add one to
    `test_067_builder_roundtrip.cpp`.
  - `SohInValue_RejectedBeforeAnyByteReachesOut` still passes, unedited.
  - SC-004's witnesses are the existing ASCII round trips, unedited and GREEN: the seeds in
    `tests/session/test_067_seeds.hpp` driven by `test_067_builder_roundtrip.cpp`, and
    `test_077_allversions_builder_roundtrip.cpp`'s pre-existing cases.
  - Quote the before/after in the commit (FR-014, SC-001, SC-004).

### Census, goldens and FIX Latest for User Story 1

- [X] T037 [US1] Show that the census can fire (C-2.2 positive controls) **before** relying on it.
  Each control runs in a scratch copy, output quoted:
  - **(a)** Run the census over the output of the "loader walk removed" mutant: the new emitter over
    the unfixed loader.
    - It must report missing **exactly** the tuples of the five R-11 pairs, and nothing else.
    - The orphan-half check must report exactly their ten tags on v50sp2.
    - The pre-change goldens are **not** this control: they hold no coupled call at all.
  - **(b)** Delete one coupled emission from a regenerated golden: reported missing.
  - **(c)** Move one call site from the nested arm to the top arm: reported as a wrong arm.
  - Record the three controls' output in `.specify/decisions/091-data-field-bytes-evidence.md`.
- [X] T038 [US1] Via `phase-implementer`, validate the goldens **structurally** (C-2.4, FR-013)
  with a script `tests/codegen/golden_091_residual_diff.py`. It is not registered in ctest; it runs
  against `origin/main`'s goldens and the regenerated ones.
  - **(a) and (c):** T028's census, plus a check that every removed two-call site in the old golden
    has exactly one corresponding coupled call in the new one.
  - **(b):** the `message_encoding` set, member position and emit position match (T028).
  - **Residual:** normalise (a)–(c) out of both sides (rewrite each old two-call site to its coupled
    form; delete the `message_encoding` member and emit; delete the (c) Length members). Old and new
    must then be **byte-identical**, and that empty residual is the check.
  - **Positive control:** inject a stray `bb.field(347, …)` inside a group body of one regenerated
    golden; the residual must be non-empty.
  - Record the commands and output in `.specify/decisions/091-data-field-bytes-evidence.md`.
- [X] T039 [US1] Via `phase-implementer`, add a new executable `session_091_data_send` from a new file
  `tests/session/test_091_data_send.cpp`, registered in `tests/session/CMakeLists.txt` with label
  `091`.
  - It links `fixpp_session`, `fixpp_mock_clock`, `fixpp::builders::v44` and `session_test_support`,
    with `FIXPP_TEST_HOOKS`.
  - **Why a new executable (`[const §VII.8]`):** it needs `FIXPP_TEST_HOOKS` + `fixpp_mock_clock` +
    `fixpp::builders::v44` together. The CMakeLists comment states that condition only, never a list
    of what other targets link (a result that goes stale).
  - **Witness 1, XmlData:** send a message carrying XmlData(212/213), with SOH in the value, through
    `send_impl`, then re-parse the emitted frame. 212 and 213 are adjacent, Length first, in the
    header, and the 213 value round-trips. Add the same case for SecureData(90/91).
  - **Witness 2, message_encoding:** a generated `NewOrderSingle` with `message_encoding` and a
    non-ASCII `encoded_text` through `send_impl`. `347` sits in the header, and `354`/`355` are in
    the body with their verbatim bytes.
  - **Liveness (no production change backs these):** in a scratch copy, the mutant "`send_impl`
    classifies a counted field by its own tag instead of inheriting `prev_header`" must turn
    witness 1 RED. The standard header pairs cannot do this, since both halves of 90/91 and
    212/213 are header-class. Witness 1 therefore includes a session-dictionary pair whose Length
    is body-class and whose Data is a header-set, non-pair tag: the input on which the two
    classifications differ (added at T039, 2026-09-25). For witness 2, reproduce the pre-T034 build failure (the member does not exist) in a scratch
    copy at the pre-T034 commit, and quote it. Record both liveness results in `.specify/decisions/091-data-field-bytes-evidence.md`.
  - T003's manifest entry `session_091_data_send` now resolves. This is plan phase 5 (R-9, quickstart §5), and it closes US1's `args.message_encoding` scenario.
- [X] T040 [US1] Via `phase-implementer`, run the US1 mutants from quickstart §3 in a scratch copy:
  - "the emitter passes `item.tag` instead of `item.data_tag`" → the v44 builder build fails on the
    C-2.2 `static_assert`;
  - "the emitter changes only the top-level arm" → the C-2.6 **nested** 256-value witness (T029),
    and T028's census (missing sites plus orphan halves: the nested arm keeps the two-call form;
    control (c) of T037 is the wrong-arm report);
  - "the `message_encoding` selection uses 'begins with `Encoded`'" → T030's v50sp2 witness, and
    T028's message-encoding set;
  - "`field_data` routes through `append_string_field`" (re-applies the content guard, FR-015) → the
    four `_418` success cases, and C-1.2.
  Record them in `.specify/decisions/091-data-field-bytes-evidence.md` §*Mutants*.

**Checkpoint**:
- US1 works independently: the four `_418` pins, C-2.6 top and nested, and the R-6 and vlatest round
  trips are GREEN.
- The census is exact and proven able to fire, and the golden residual is empty.

---

## Phase 4: User Story 2 — The injection guard on every non-Data field stays exactly as strong (Priority: P1)

**Goal**: opening the byte path opens no injection path.
- INV-2 holds on `field()`/`set_string`, unchanged (FR-007).
- The Data operation refuses every non-Data, framing and dictionary-only tag on both surfaces (FR-004,
  FR-009).
- The generator never routes a non-Data field through it (FR-012).

**Independent test**: `SohInValue_RejectedBeforeAnyByteReachesOut` passes unedited. The C-1.4 and
C-1.9 refusal arms pass, and each is shown able to fail.

- [X] T041 [US2] Prove `SohInValue_RejectedBeforeAnyByteReachesOut` is byte-identical (FR-007,
  SC-002). `git diff origin/main -- tests/session/test_067_builder_failclosed.cpp` shows no hunk
  inside that `TEST` body, and every pre-existing injection-guard test in
  `tests/wire/test_body_builder.cpp` and `tests/session/test_067_builder_failclosed.cpp` is unedited
  (`git diff origin/main` hunks touch only new tests and the four flipped pins). Record both diffs in
  `.specify/decisions/091-data-field-bytes-evidence.md`.
- [X] T042 [US2] Via `phase-implementer`, run the guard mutants from quickstart §3 in a scratch copy.
  Each is RED on the named arm, then GREEN after revert:
  - "`set_data` skips pair resolution (forwards straight to `append_bytes_field`)" → C-1.4's
    `set_data` arms for tag 11 (the SOH-bearing payload) and tag 354; C-1.9's `set_data(5002, …)`
    arm;
  - "`field_data` resolves through `hooks_` instead of the standard table" → C-1.9's
    `field_data(5002, …)` refused arm;
  - "`set_data` resolves through `hooks_` instead of the standard table" → C-1.9's
    `set_data(5002, …)` arm;
  - "`set_data` omits the owner check" → C-1.4b default handle. Run it under ASan: it must fail, not
    pass through UB;
  - "`set_data` omits `is_innermost_open`" → C-1.4b outer handle and closed-group handle.
  Record them in `.specify/decisions/091-data-field-bytes-evidence.md` §*Mutants*.
- [X] T043 [US2] Confirm FR-012 / SC-002 from T028/T037: the census reports zero `field_data`/
  `set_data` on a tag outside the standard Data set, on every version. Cite control (b)/(c) as the
  proof that it can fire, and the `static_assert` positive control (T040, first bullet) as the
  per-call-site guard.

**Checkpoint**: US2 holds independently. Every guard arm is live, and INV-2 is unchanged.

---

## Phase 5: User Story 3 — Hand-written `body_builder` callers get the same atomic operation (Priority: P2)

**Goal**: a hand-written caller names the Data tag once. The Length is derived, both nodes or
neither, and malformed hand-written pairs are refused at commit (FR-001, FR-002, FR-006, FR-008).

**Independent test**: `wire_body_builder_test` alone: success, each refusal, and all-or-nothing
rollback on arena exhaustion and on the body cap (C-1.1–C-1.11 except C-1.9; C-1.9 is in
`wire_dict_tests`).

- [X] T044 [US3] Via `phase-implementer`, run the contract mutants from quickstart §3 in a scratch
  copy:
  - "`field_data` appends Data before Length" → C-1.1 and C-1.10;
  - "the second-append rollback is removed" → C-1.5 (its commit-and-byte-compare oracle);
  - "`set_data` skips the second-append rollback" → C-1.5's nested `set_data` twin;
  - "the commit pair check is deleted" → C-1.7, and C-1.9's "malformed hand-written custom pair
    refused" arm;
  - "a group node is fed as `observe(no_tag, <count digits>)` instead of `observe(no_tag, {})`" →
    C-1.7's "group `no_tag` 354 + sibling one-byte 355" case;
  - "commit's checker is built from `none()` instead of `hooks_`" → C-1.9's "malformed custom pair
    refused at commit" arm.
  Record them in `.specify/decisions/091-data-field-bytes-evidence.md` §*Mutants*.
- [X] T045 [US3] Run C-1.5 on **MSVC** (`windows-msvc-debug` in the MSVC sandbox, per
  `phases/phase-4/parallel-worktrees.md`). The arrangement witness must hold there too, because
  outer-vector regrowth differs by STL. If the MSVC leg cannot run, record a waiver with its cost
  stated plainly ("CI's MSVC matrix runs only once both gate labels land"), not as "CI covers it".
- [X] T046 [US3] Blast radius of FR-008 for hand-written callers (R-4).
  - Re-run `git grep -n "field(\(90\|91\|95\|96\|212\|213\|354\|355\)\b" -- src tests bench bindings`.
  - Confirm that the full ctest run of T027 raised no `wire_invalid_field_format` from a
    pre-existing caller (T065 repeats the check at the verify head).
  - Record the grep and its stated blind spot (named constants, computed tags) in `.specify/decisions/091-data-field-bytes-evidence.md`. The
    blind spot is covered structurally by T028, and disclosed as B-091-1.

**Checkpoint**: US3 holds independently, and every contract clause has a live test.

---

## Phase 6: User Story 4 — The limitation ledger and the witnesses tell the truth (Priority: P3)

**Goal**: L-067-2 is closed with its evidence, no live text calls the gap open, the new behaviours
are disclosed, and C-ABI 1.9 is recorded on every doc surface (FR-009a, FR-011, FR-011a, FR-016,
FR-019, SC-006).

**Independent test**:
- L-067-2 is absent from `spec/behaviors-and-limitations.md`, and present in
  `spec/behaviors-and-limitations-closed.md` with `68c8c769` and this PR.
- `git grep -nE '#418|fixpp ?#418' -- . ':!specs/091-data-field-bytes'` finds no text describing the
  gap as open.

- [X] T047 [US4] Via `phase-implementer`, reword the comments FR-016 names:
  - `tests/interop/conversation/support/conv_wire.hpp` (the hand-built-frame route);
  - `tests/interop/conversation/conv_cell_test.cpp` (B-05).
  Both stay on their hook. The comment says why: moving B-05 needs a counterparty republish (spec
  Assumptions). It no longer says that #418 is open. No behaviour change.
- [X] T048 [US4] Ledger edits in `spec/behaviors-and-limitations.md` and
  `spec/behaviors-and-limitations-closed.md` (orchestrator-authored `.md`). Assign the final row IDs
  against the live file.
  - **Move** L-067-2 to the closed file, citing stage-one `68c8c769` and this PR.
  - **Delete** the live "L-067-2 is unchanged … (fixpp#418)" bullet in the #426/#428 limitations
    block.
  - **Extend L-426-3** with the send-side consequence (FR-009a): fixpp can now send a Data value
    holding SOH, which QuickFIX/C++ and QuickFIX/J split unless the Length tag is Data − 1, and
    QuickFIX/n splits unless the tag is XmlData(213). No near-copy row.
  - **B-091-1:** `body_builder::commit` refuses a malformed hand-written pair
    (`wire_invalid_field_format`), a behaviour change for hand-written C++ callers.
  - **Carried from T036 (2026-09-25): an empty coupled member on a generated builder.** Before 091,
    a coupled member set to `""` emitted `<Length>=0` and an empty Data. Now `field_data`/`set_data`
    refuse it with `wire_field_value_out_of_range` (spec Edge Cases, "An empty Data value is
    refused"; data-model set-time table). This is a behaviour change for generated-builder callers
    that SC-004's "ASCII callers behave identically" does not cover. Give it its own row, or fold it
    into B-091-1's text, and cite the Edge Case.
  - **B-091-2:** `message_encoding` is available but not enforced when an `Encoded*` field is set.
  - **B-091-3:** the set-time vs C-ABI divergences. A dictionary-only Data tag is refused by
    `field_data`/`set_data` but accepted by `fixpp_msg_set_data` (follow-up fixpp#505). A repeated
    call appends where the C-ABI upserts.
  - **B-426-2:** its "every later field stays absent" no longer holds for a Logon: under FR-020
    `interpret_logon` refuses a Logon in which a Length is immediately followed by its paired Data
    whose counted extent reaches or passes the end of the whole framed message, or whose following
    byte is not SOH (an orphan Length is read as a plain value), and the refusal code is always
    `session_invalid_logon` (a Logon whose 49 or 56 follows such a count returned
    `session_compid_mismatch` before where that expected CompID was non-empty, and could be accepted
    where it was empty). Amend the row, citing FR-020.
  - **B-091-4, marked BREAKING (C-ABI 1.9):** data-model.md's row text. It covers the v50sp2 source
    break in both shapes (a compile error for designated or member access; a silent shift for a
    positional aggregate), the user-loaded-dictionary effects, the recipe-derived BREAKING
    population, and the additive widenings.
  - No citation of a line number; cite files and symbols.
- [X] T049 [P] [US4] `spec/feature-catalogue.md` edits.
  - W-008's evidence column gains this feature.
  - A C-ABI 1.9 BREAKING note, in the style of CA-011's 1.8 note, on every CA row that lists a
    declaration FR-019 marks, reader-paragraph members included.
  - Re-derive the rows: match each Appendix A class-B row whose carrier is a declaration note or the
    reader paragraph against each CA row's listed functions. The root cause is the
    `fixpp_dict_load_from_xml` row, CA-011.
- [X] T050 [P] [US4] Brain updates, orchestrator-authored:
  - `brain/components/wire.md` §*Length+Data pairs*: "#418's `body_builder` must reuse" becomes past
    tense, and the loader/drift-arm change is noted.
  - `brain/components/dictionary.md`: the loader walk's present-tense "never entered `<component>` or
    `<group>`" and any "shipped as C-ABI 1.9" wording are re-read against the merged code; and
    `wire.md`'s "Until that merges, treat the claim as unproven" is reworded.
  - `brain/components/c-api.md`: a C-ABI 1.9 entry beside the 1.8 section, naming FR-019's
    population and its recipe (R-11, Appendix A).
  - `brain/components/wire.md`'s "one malformed-count policy for every scanner … must stop" line:
    name FR-020 as superseding `.specify/426-428-length-data-pairs.md` §4's policy for
    `interpret_logon` (the design doc is not edited; the brain page flags it, #334).
  - Record that `.specify/426-428-length-data-pairs.md`'s "the drift test keeps the two in step" was
    stale per dictionary and is made true by FR-018. The 426-428 note itself is not edited.
- [X] T051 [US4] Article XIX §5 docs check:
  - run `find . -maxdepth 3 -name 'Doxyfile*'`, and if one exists, regenerate it;
  - run `git grep -n -e body_builder -e length_pair -e 'Length+Data' -- docs/src`, and update any
    hit (`.md` is orchestrator-authored).
- [X] T052 [US4] FR-016 sweep.
  - Run `git grep -nE '#418|fixpp ?#418' -- . ':!specs/091-data-field-bytes'` (not a bare `418`,
    which matches FIX tag 418).
  - Every hit is past tense or a history reference, and none describes the gap as open.
  - Positive control: the same grep against `origin/main` (`git grep -nE '#418|fixpp ?#418' origin/main
    -- . ':!specs/091-data-field-bytes'`) lists the `conv_wire.hpp`, `conv_cell_test.cpp`,
    `length_data_check.hpp` and B&L sites.

**Checkpoint**: SC-006 holds. The ledger, catalogue and brain match the code.

---

## Phase 7: Polish & cross-cutting

### Simplify (before any measurement)

- [X] T053 Run `/simplify` over the branch diff (`[const §XVI.7]`, pipeline step 11); fixes go through
  `phase-implementer`. Every later check (T060 onward) runs on the post-simplify head.
- [X] T054 After `/simplify` (T053) and codegen freshness, re-measure the compile-time surface "after" figure with
  `bench/codegen/vlatest_builders_compile_bench/compile_bench.sh`, same command and toolchain as
  T002. Report the delta in `.specify/decisions/091-data-field-bytes-evidence.md`; it has no budget (R-10).

### Performance (SC-005, Article VIII; plan phase 6)

- [X] T055 Via `phase-implementer`, make `bench/wire/builder_bench.cpp`'s WithGroup and Raw prechecks
  exact.
  - Pin complete `kWithGroupBody` and `kRawBody` (as `kNoGroupBody` is) and compare exactly.
  - Show each case's one-line mutant producing `SkipWithError`: drop one scalar field (both cases);
    drop one of the three `kParties` entries (WithGroup). Record each mutant's `SkipWithError` in
    `.specify/decisions/091-data-field-bytes-evidence.md`.
  - The final bench source must compile against the merge-base API; it may not call
    `field_data`/`set_data`.
- [X] T056 Via `phase-implementer`, add the `builder_bench` row to `bench/ci-suite.txt` with tier-2
  value **`no`** (Article VIII §2a candidate-only; `paired` is irreversible), in the file's existing
  row format.
- [X] T057 Run the paired SC-005 run, following quickstart §6 exactly.
  - **Base:** a detached worktree at `/mnt/wsl/fixppbuild/091-base-wt`, at the current
    `git merge-base HEAD origin/main` (after `git fetch`), with only the final
    `bench/wire/builder_bench.cpp` and `bench/wire/CMakeLists.txt` copied in.
    - `git -C "$W" diff --stat "$MB" -- src include tools cmake` prints nothing;
    - `status --porcelain` lists exactly those two files.
  - **Base configure:** the same `-D` options as the candidate's `CMakeCache.txt` (including
    `-DFIXPP_BUILD_BENCH=ON`, which no preset sets), the same Conan toolchain (run its own
    `conan install`), building both `builder_bench` and `xml_loader_bench`. Precheck: diff the two
    caches' `FIXPP_*` and `CMAKE_BUILD_TYPE`/compiler entries; any difference other than the source
    dir stops the run.
  - **Candidate:** `linux-clang-release`, after codegen freshness.
  - **Run:** A-B-A-B × 4, `taskset -c 3`, into a fresh `mktemp -d -p /mnt/wsl/fixppbuild` output
    directory. Every leg must exist and hold all four cases.
  - **Noise floor, from the base legs:** ≤ 1 %, else the verdict is **inconclusive** and goes to
    the owner.
  - **Verdict:** NoGroup, WithGroup and Raw each ≤ +3 % (minimum of medians). AsciiEncodedText is
    reported only.
  - Over budget → the owner, with the floor and the per-leg figures. Never relax it.
  - Record everything in `.specify/decisions/091-data-field-bytes-evidence.md` §*SC-005*.
  - `/mnt/wsl/fixppbuild/091-baseline/builder_bench.base` is a drift cross-check only.
- [X] T058 Run the paired `bench/dictionary/xml_loader_bench` A-B-A-B (FIX50SP2 load) against the same
  base worktree, same procedure.
  - Pass condition: a slowdown ≤ +5 % (Article VIII §2). Over it → the §2 approval path, never
    self-declared.
  - Record the per-leg figures and the verdict in `.specify/decisions/091-data-field-bytes-evidence.md` §*xml_loader_bench*.
  - Then `git worktree remove --force /mnt/wsl/fixppbuild/091-base-wt`.

### Fuzz

- [X] T059 Fuzz the changed loader (`[const §VII.7]`; owner ruling 2026-09-24: the dictionary XML
  loader is in scope for §VII.7, and this run is required): build `fuzz_dict_xml_loader`
  (`tests/fuzz/fuzz_dict_xml_loader.cpp`) under `linux-clang-asan` (`FIXPP_BUILD_FUZZ=ON`), add seeds
  with nested `<group>`s, `<group>`s under `<header>`/`<trailer>` and component-only Length/Data
  pairs, and run it for ≥ 600 s. Seeds are committed to `tests/fuzz/corpus/dict_xml_loader/`; its
  replay ctest exists only with `FIXPP_BUILD_FUZZ=ON`, so it is exempt from the `091` label rule and
  the `linux-clang-debug` manifest. Record the command, corpus and result in the evidence file; name
  the target to `/speckit-verify` (T065, `--fuzz-duration=600`) so it is not marked N/A.

- [X] T073 Via `phase-implementer`, extend the seeds of `fuzz_session_recovery_admin_parse`
  (`tests/fuzz/fuzz_session_recovery_admin_parse.cpp`, which reaches `interpret_logon` through
  `Session::on_inbound_frame`; precedent 027 T026) for FR-020: Logons in which RawDataLength(95) is
  immediately followed by RawData(96) with a count running past the frame, with a count ending on a
  non-SOH byte, and with `98=2` after such a count; and an orphan `95=999` followed directly by
  `98=2` (no 96). Build under
  `linux-clang-asan` (`FIXPP_BUILD_FUZZ=ON`), run ≥ 600 s, commit the seeds to its corpus directory,
  record the command, corpus and result in the evidence file, and name the target to
  `/speckit-verify` (T065) beside `fuzz_dict_xml_loader`.

### Static analysis, claims and citations

- [X] T060 Run clang-tidy, clang-format, cppcheck and IWYU (`[const §IX.4]`) on every changed file under `src/`, `include/` **and
  `tools/codegen/`** (#265). Never format `specs/` or `include/fix/c_api/*.h`. Any finding on a
  changed line is fixed by `phase-implementer`.
- [X] T061 Run `python3 /home/catalin/Work/Programming/Antreprenoriat/.claude/scripts/check-comment-claims.py
  --root <tree> --base origin/main`, with `<tree>` the worktree that owns this branch (absolute
  path, so it also works from a parallel worktree).
  - Read every hit in the comments this feature authored: the `version.h` history, the BREAKING
    notes, the read-tier banner, the `body_builder.hpp` contract comments, the CMakeLists
    comments, the `check_length_data` header comment (FR-021) and the `interpret_logon` comments
    (FR-020). Also read the strings the script cannot see.
  - A claim that records a result is deleted, not replaced.
- [X] T062 Run `python3 tools/check_line_citations.py --shift-audit origin/main..HEAD`. For a hit on
  the checker's own fixture strings, apply the `# citation-ok` pragma.

### Close-out checks

- [X] T063 Record fixpp#506's fix before 091 closes (plan phase 7): FR-021 fixes it (owner ruling
  2026-09-25); name the §2e commits and the T078 cells in the evidence file, and confirm the parent's
  `phases/phase-4/issue-batches.md` Parked entry for #506 (updated early at T082, parent `f126dde`). **Owner ruling 2026-09-25: #506 closes through this PR**
  (FR-021 delivers its whole Expected and Test sections). T067 carries its closing keyword. Confirm fixpp#505 is open and cited in B-091-3.
- [X] T064 Run the quickstart §2 label gate. It must pass: the label set equals the manifest, and
  every manifest entry is a registered test. Then run `ctest --test-dir build/linux-clang-debug -L '^091$' --output-on-failure`, all
  GREEN.
- [X] T077 Via the `checklist-auditor` (the checklists are reviewer-owned), audit the checklists
  against the FR-020 and FR-021 deltas **by complement** (scoped Gate A round 3, P3-3; FR-021 added
  2026-09-25): grep every domain checklist under `checklists/` for items whose subject FR-020,
  FR-021, FR-019, B-091-4, research.md R-4, contract C-1.7 or data-model.md Appendix A changed, and
  re-disposition each hit (SPEC-FIXED / DD-DECIDED / WAIVED, with the FR-020 or FR-021 note).
  - Derive the population by a complement grep, not by example:
    `grep -nE 'interpret_logon|\b98\b|EncryptMethod|malformed|fixpp_session_|is_established|register_|session\.h|version\.h|§X\.7|§XII\.7|FR-019|FR-020|FR-021|B-091-4|Appendix A|carrying declaration|covered|shipped dictionar|observer|check_length_data|count.digits|group node|no_tag|#?506|R-4|C-1\.7' specs/091-data-field-bytes/checklists/*.md`,
    then read each hit's PASS condition against the current spec, plan, data-model and tasks.
  - Items known at Gate A: `abi.md` CHK001, CHK006, CHK014, CHK015 and `codegen-loader.md` CHK023;
    for FR-021, `abi.md` CHK017 (it passes on the #506 fix being scoped **out** of 1.9, which
    FR-021 reverses) and `api.md` CHK010 (the group-node empty-value rule, now on both writers).
    Before trusting the grep, confirm it hits every one of them; if one is missed, widen the terms
    until it is hit. The known list is the positive control, not the population.
- [X] T065 Run `/speckit-verify` (mandatory after `/speckit-implement`, Article XVII §8). It produces
  `.specify/decisions/091-data-field-bytes-verify.md`. The record cites
  `.specify/decisions/091-data-field-bytes-evidence.md`, which every task whose body says "record … in" that file wrote; its
  discriminating-witness rows point there rather than restating it.
  - It covers /speckit-verify's full preset matrix (ASan, UBSan, TSan, …) and the MSVC leg,
    coverage (every new line in `src/wire/body_builder.cpp`,
    the new loader walk in `src/dictionary/xml_loader.cpp`, `interpret_logon`'s changed lines in
    `src/session/admin_messages.cpp` and `check_length_data`'s changed lines in
    `src/capi/message_write.cpp` (FR-021)), clang-tidy and ABI hygiene.
  - It includes the full `ctest --test-dir build/linux-clang-debug` run.
  - **Coverage** runs on `linux-clang-coverage` (its build tree lives on the F: vhdx through the
    `build/` symlink, not on E:). The `.profraw` files are purged first, so no stale profile can
    merge in. Every changed line named above must be covered, or assessed line by line
    (`[const §IX.1]`).
  - **Allocation gate (mallocnesia, `[const §VIII.5]`)** (owner request 2026-09-25):
    - The interceptor is the CMake target `mallocnesia`, built into
      `build/<preset>/lib/libmallocnesia.so` (fixpp#448). Follow `/speckit-verify` Step 6, which
      mirrors tier1: `tools/check_mallocnesia_population.py --min-gates <tier1's floor>`, then
      `ctest --preset linux-clang-release -L mallocnesia --no-tests=error`, with
      `mallocnesia_positive_control` passing. "SKIPPED (interceptor missing)" is not accepted.
    - Run every `*_mallocnesia` ctest twin the branch touches, including
      `session_length_data_scanner_mallocnesia` and the `capi_*_mallocnesia` twins, and
      `tools/check_alloc.py` where a guard target exists.
    - For each, record evidence that the interceptor **took effect** (LOADED is not INTERPOSED): a
      line the override writes, or a planted allocation that it catches.
    - Also record the `NoGlobalHeap_FieldDataSetDataCommit` result on the Linux lanes, and the MSVC
      debug skip of that test (the `_ITERATOR_DEBUG_LEVEL` guard; evidence §US3 on MSVC).
  - ⚠️ The §7 full build needs an owner ASK, even as gate evidence.

- [X] T066 **`CLAUDE-history.md` entry** (Article XIX, plan Constitution Check): via `phase-implementer`
  (the edit guard decides the file class), add a newest-first 091 entry to the library's
  `CLAUDE-history.md` naming the feature, the PR, `Closes #418`, C-ABI 1.9 BREAKING, the FR-020
  `[const §XII.7]` fail-open fix (a Length immediately followed by its paired Data whose counted
  extent reaches or passes the end of the whole framed message, or whose following byte is not SOH,
  now refuses the Logon; it affects shipped dictionaries
  through RawDataLength(95) and RawData(96)), the FR-021 C-ABI commit fix (a group whose count tag
  is a pair half is refused; FR-021 repairs the defect issue 506 tracks) and the follow-up #505. Update `CLAUDE.md`'s "Last merged FEATURE" pointer only at merge.
- [X] T067 **PR description** (FR-019, plan phase 7). The body carries:
  - the `[const §X.7]` **C-ABI 1.9 BREAKING** declaration: FR-019's population, pointing at B-091-4
    and data-model.md Appendix A;
  - the v50sp2 source break (FR-011 carve-out);
  - the FR-020 fix: `interpret_logon` refuses a Logon in which a Length is immediately followed by
    its paired Data whose counted extent reaches or passes the end of the whole framed message, or
    whose following byte is not SOH, closing a
    `[const §XII.7]` fail-open reachable on `main` through RawDataLength(95) and RawData(96); its
    C-ABI observers are every call whose result depends on the session having logged on
    (`fixpp_session_is_established`, `fixpp_session_close`, `fixpp_session_send`, the receive
    callback and the toApp callback);
  - the FR-021 fix: `fixpp_msg_commit` feeds a group node an empty value to the Length+Data check, so
    a group whose count tag is a pair half, which committed `FIXPP_ERR_OK` when its instance-count
    digits completed the pair, now returns `FIXPP_ERR_WIRE_CONFORMANCE` (C-ABI 1.9 BREAKING, reachable
    on every session with no dictionary); FR-021 repairs the defect issue 506 tracks, and the body
    closes it (owner ruling 2026-09-25, T063): one affirmative `Closes #506` line beside
    `Closes #418`, and no negated sentence anywhere that mentions either number (a closing keyword
    fires inside a negation);
  - the SC-005 and loader-bench results (noise-floor precondition not met on either run; passed by
    owner ruling 2026-09-26), including the owner rulings (T057 passes on the fix `1fe2a063`; T058
    passes on FIX50SP2 +3.05 %), the AsciiEncodedText build delta (+13 %), and the FIX44/FIX42 load
    deltas (+6.04 % and +11.35 %, about 0.1–0.3 ms per load; evidence §xml_loader_bench), which
    exceed +5 % and take `[const §VIII.2]`'s approval path (next bullet);
  - **`[const §VIII.2]` ratification, before labelling.** A slowdown over +5 % on a paired bench
    needs the paired measurement, a rationale, and an owner statement in the PR THREAD; a claim in
    the PR body does not count. The PR thread must carry the owner's ratification covering the
    FIX44 and FIX42 `xml_loader_bench` loads and `builder_bench`'s AsciiEncodedText case. The
    orchestrator hands the owner a ready line to post themselves, e.g.
    `! gh pr comment <PR> --repo CatalinSerafimescu/fixpp --body "Owner ratification [const §VIII.2]: I accept the xml_loader_bench FIX44 (+6.04 %) and FIX42 (+11.35 %) load slowdowns and the builder_bench AsciiEncodedText (+13 %) slowdown, on the paired measurements and rationale in .specify/decisions/091-data-field-bytes-evidence.md §xml_loader_bench and §SC-005."`,
    and **never posts it on the owner's behalf**. Then it verifies with
    `gh api --paginate repos/CatalinSerafimescu/fixpp/issues/<PR>/comments` that a comment exists whose
    `.user.login` is `CatalinSerafimescu` and whose body names FIX44, FIX42 and AsciiEncodedText. The session's `gh` is
    authenticated as the owner, so the login check proves the account, not who typed it; the
    never-post-on-their-behalf rule is what that check rests on. No such comment → do not label;
  - `local build: green on linux-clang-debug @ <git-sha>` (`[const §XVII.7]`), with the SHA T065
    verified;
  - a `## Gates` section, and a `## Gate B …` heading for the Gate B record;
  - under `## Gates`, the records backing `gate-a-done`: `.specify/decisions/091-data-field-bytes-gatea.md`
    (loop 3 round 3 converged Opus-only, owner-accepted without the Codex pass), and the FR-020
    scoped Gate A: plan.md §Gate A's *Scoped FR-020 round …* entries and their review files
    (`research/reviews/codex_091-data-field-bytes_gate_a_FR020_review.md`,
    `research/reviews/opus_091-data-field-bytes_gate_a_FR020_adversarial_review.md`, and any later
    round's), plus the owner's FR-020 plan re-sign-off (`[const §X.6]`). The FR-020 Gate A record
    is `.specify/decisions/091-data-field-bytes-gatea.md` §"Addendum — scoped Gate A round on
    FR-020 (2026-09-25)" (converged round 3; owner re-sign-off 2026-09-25). Likewise the FR-021
    scoped Gate A: plan.md §Gate A's *Scoped FR-021 round …* entries, their review files, and the
    owner's FR-021 plan re-sign-off; the FR-021 Gate A record is
    `.specify/decisions/091-data-field-bytes-gatea.md` §"Addendum — scoped Gate A round on FR-021 (2026-09-25)" (converged round 3; owner re-sign-off 2026-09-25). If either scoped round has not
    converged or either re-sign-off is still pending (plan.md Constitution Check, X row), ask the owner
    before labelling. If Gate B rules
    that `[const §XVII.8]`'s "Codex convergence record" is not met, ask the owner before labelling;
    never choose between `gate-a-done` and `gate-a-waived` unilaterally;
  - `Closes #418` and `Closes #506` as the ONLY closing keywords (owner ruling 2026-09-25, T063).
  Before opening, grep the body AND every commit message on the branch
  (`git log origin/main..HEAD --format=%B`) for `close[sd]?|fix(e[sd])?|resolve[sd]?` next to
  #505 or any number other than 418 and 506; #506 is exempt from this grep. #505 stays open (B-091-3
  cites it), so a hit on it is a defect. A negated keyword still links. After opening, check
  `closingIssuesReferences` lists exactly #418 and #506, and nothing else.

### Mandatory close-out tasks (Gate-B preconditions, Article XVII §8)

- [X] T068 [P] **Catalogue close-out.**
  - Flip every feature-owned OFFICIAL row in `spec/feature-catalogue.md` to `done` with this PR as
    evidence. That covers W-008, which gains 091's evidence (T049), and any row 091 owns; the CA
    rows carry the 1.9 note from T049.
  - Add or update the matching `spec/coverage-index.md` entry: `[FIX50SP2 §3.3] Field data types` ↔
    W-008, naming this feature's witnesses (the `_418` pins, C-2.6, C-1.2).
- [X] T069 **Feature-completeness audit (the FINAL task).** Assert against the merged tree:
  - (i) every `tasks.md` row is `[X]` or carries an explicit waiver rationale;
  - (ii) every FR-001…FR-021 (including FR-004a, FR-009a and FR-011a) and SC-001…SC-006 maps to a
    landed test AND a landed implementation;
  - (iii) every feature-owned OFFICIAL catalogue row is `done`, with a matching `coverage-index.md`
    entry.
  Record the verdict (100 % or fully waived) in `.specify/decisions/091-data-field-bytes-verify.md`
  `## Completeness`, or in `.specify/decisions/091-data-field-bytes-completeness.md`. `/gate-b`
  pre-flight 4d hard-blocks without it.

---

## Dependencies & execution order

### Phase dependencies

- **Setup (Phase 1):**
  - T002 must precede every production edit;
  - T004 → T005, as two commits;
  - T003 needs no other task.
- **Foundational (Phase 2):** depends on Setup.
  - 2a: T006–T010 are written RED first → T011 → T012 → T013 → T014.
  - 2b: T015 → T016 → T017 (RED) → T018 (GREEN) → T019 → T020 → T021. It can run beside 2a, but T008–T010 are its RED
    witnesses, and their GREEN needs T012.
  - 2c: T022 and T023 (RED) → T024 → T025. It needs no 2a/2b task and can run beside them, except
    T010 → T023 (same file).
  - 2d: T070 (RED) → T071 (GREEN) → T072. It needs T018 (the `version.h` comment T072 edits).
    Then T074, T075 (after T072: the same `version.h` comment and freeze pin) and T076, from the
    scoped Gate A round 1; they can run beside each other, except T074 → T076 (both append to the
    evidence file).
  - Then T026 (needs T006–T012, T020, T071 and T074), then T027 (needs everything above, T075 and
    T076 included).
  - **Blocks all stories.**
  - 2e runs in Polish after T053 (see Polish order) and blocks no story.
- **US1 (Phase 3):** needs the Foundational checkpoint.
  - T028 (census, RED) → T029, T030, T031, T032 (RED) → T033 → T034 → T035 → T036 (all GREEN);
  - T035 → T037 → T038;
  - T040 needs T035 and T036 GREEN;
  - T039 needs T033–T035 (a generated `message_encoding`);
- **US2 (Phase 4):** needs Foundational (C-1 exists) and T028/T037 (for T043). Independent of US3.
- **US3 (Phase 5):** needs Foundational only. It can run beside US1 and US2.
- **US4 (Phase 6):** needs US1 through US3 landed (the ledger describes shipped behaviour). T047 and
  T048 must precede T052's final sweep.
- **Polish (Phase 7):** T064 needs every
  manifest-listed entry registered.
- **Polish (Phase 7), order:**
  - T053 (simplify, first, so every measurement is of the final candidate) → §2e (FR-021: its entry
    condition, then T078 (RED) → T079 (GREEN) → T080 → T081, and T082 after T079) → T054
    (compile-surface "after") → T055 → T056 → T057 → T058 → T059 → T073 (fuzz);
  - T060–T063 after T081 and T082 (so after §2e);
  - T077 (checklist audit of the FR-020 and FR-021 deltas) after T082 and before T065;
  - T065 after T060–T063 and T077;
  - T068 → T069. T069 is last.

### User story dependencies

- **US1 (P1, MVP):** the Foundational `body_builder` API and loader fix. No other story.
- **US2 (P1):** Foundational; T043 reads US1's census.
- **US3 (P2):** Foundational only. Its operation is built in Foundational, because US1 calls it.
  Its priority reflects how many callers use it, not its place in the order.
- **US4 (P3):** documents US1–US3, so it lands after them.

### Within each story

The witness is written first and shown RED for its stated reason. Then comes the implementation,
GREEN, and then the mutants in a scratch copy. There is no GREEN claim without its RED.

---

## Parallel opportunities

- **Foundational 2a:** T006 (`length_data_pairs_drift_test.cpp`), T007 (new
  `xml_loader_component_group_pairs_test.cpp`), T008 (new `length_data_component_pair_test.cpp`) and
  T010 (`dict_hooks_custom_pair_test.cpp`) are separate files and can be written together.
- **Foundational 2c ‖ 2a:** T022 (`test_body_builder.cpp`) and T023 (`dict_hooks_custom_pair_test.cpp`,
  after T010, same file) can be written alongside 2a.
- **US1:** after T028, T029 ‖ T031 ‖ T032 (different files); T030 shares a file with T029 (v44 half)
  and T031 (v50sp2 half), so run it after them.
- **US2 ‖ US3:** their mutant tasks, T042 and T044, touch disjoint scratch copies.
- **US4:** T049 ‖ T050 (different files).

```text
# Foundational, write the RED witnesses together:
phase-implementer: T006 drift arm      → tests/wire/length_data_pairs_drift_test.cpp
phase-implementer: T007 C-2.5a arms    → tests/dictionary/xml_loader_component_group_pairs_test.cpp
phase-implementer: T008/T009 C-ABI 1.9 → tests/capi/length_data_component_pair_test.cpp
phase-implementer: T022 C-1 clauses    → tests/wire/test_body_builder.cpp

# US1 witnesses together:
phase-implementer: T029 C-2.6          → tests/session/test_067_builder_roundtrip.cpp
phase-implementer: T031 R-6 v50sp2     → tests/session/test_077_allversions_builder_roundtrip.cpp
phase-implementer: T032 vlatest        → tests/session/test_077_allversions_builder_roundtrip_vlatest.cpp
```

---

## Implementation strategy

### MVP (US1)

1. Setup (T001–T005): the four `_418` pins are RED anchors.
2. Foundational (T006–T027, T070–T072, T074–T076): the loader, C-ABI 1.9 and the `body_builder` API are all GREEN.
3. US1 (T028–T040): the generated builders send any octet, including through `send_impl` (T039).
4. **Stop and validate:** the four `_418` pins and C-2.6 are GREEN, the census is exact, and the
   residual is empty.

### Incremental delivery

After the MVP, US2 and US3 add their mutation evidence, independently of each other. Then US4
writes the ledger, and Polish runs the bench, analysis, verify and the two close-out tasks. One PR
carries all of it: C-ABI 1.9 must ship in the same PR as the loader change (plan 0b).

### Next pipeline steps (before `/speckit-implement`)

- `/speckit-analyze`: a mandatory `[const §X.6]` control for this BREAKING C-ABI change.
- The checklist audit (`/speckit-checklist-audit`, pipeline step 9), which blocks implement.

---

## Notes

- `[P]` means different files and no incomplete dependency.
- `[USn]` traces a task to its story. Setup, Foundational and Polish carry none.
- Coverage of the C-1/C-2 clauses, the C-2.5a arms and the quickstart §3 mutant rows is
  re-checked mechanically by `/speckit-analyze`; this file records no count.
