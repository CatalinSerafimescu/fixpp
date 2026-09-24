# Tasks: A Data field can carry any octets — atomic Length+Data emit in the C++ builders

**Feature**: `091-data-field-bytes` (fixpp#418, batch B8) | **Branch**: `091-data-field-bytes`

**Input**: `specs/091-data-field-bytes/` — spec.md (FR-001…FR-020, SC-001…SC-006), plan.md (phases 0–7),
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
### 2d — `interpret_logon` refuses a malformed count (FR-020, owner ruling 2026-09-24), tests first

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
  - The C-ABI observer was not written: `capi_loopback_support.hpp` pairs two engines and cannot
    inject a hand-built Logon; the arms above are the handshake witnesses.
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
- [ ] T072 Via `phase-implementer`, update the `include/fix/c_api/version.h` 1.9 history comment
  (FR-019 as amended): the Logon effect is now FR-020's — a Logon carrying a malformed count, of a
  component/group-only pair or of a standard pair such as 95/96, is refused — replacing the
  "a required field follows the count" wording; the observers stay, on either role (the C-ABI
  latches are set by `onLogon` whatever the role). The `close` observer holds once the refused
  session has drained; `version.h` says so (a follow-up edit after `03bc2d4a`, with the freeze
  re-pin). Re-run
  `tools/check_capi_freeze.sh` (it must fail on exactly `version.h`, then re-pin
  `tools/capi_freeze.sha256` and pass).

- [ ] T026 Via `phase-implementer`, run the Foundational mutants from quickstart §3 in a scratch copy.
  Each one must be RED on the named test, then GREEN after revert:
  - "the new walk skips non-field children instead of breaking" → C-2.5a arm (iii) (T007);
  - "the new walk visits only components and their direct `<group>` children" → arms (v) and (vi);
  - "groups are walked depth-first, right after their container" → arm (vii), all three placements;
  - "the group walk is entered only from messages and component definitions" → arm (viii) (a) and
    (b), and arm (vii)(c);
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
    cell, on the function and on both arms.
  Record each mutant, command and RED line in `.specify/decisions/091-data-field-bytes-evidence.md`
  §*Mutants*.
- [ ] T027 Run the **full** `ctest --test-dir build/linux-clang-debug --output-on-failure`, after
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
- `interpret_logon` refuses a malformed count (FR-020): the T070 cells are GREEN and their twins hold.
- The four flipped `_418` pins are still RED: the generated builder still routes through `field()`.

---

## Phase 3: User Story 1 — Send non-ASCII text in an encoded Data field through a generated builder (Priority: P1) 🎯 MVP

**Goal**: every generated builder routes each coupled Length+Data member through
`field_data`/`set_data` (FR-010), and messages that can carry an `Encoded*` field gain
`message_encoding` (FR-011a). SOH, C0 controls and `0x80–0xFF` then round-trip.

**Independent test**: build a v44 `NewOrderSingle` with `encoded_text` holding SOH, a C0 byte, `0x80`
and `0xFF` in turn: success, exact bytes for 354/355, and a re-parse yields identical octets. These
are the four flipped `_418` pins, plus C-2.6.

### Tests for User Story 1 (write first; RED until T036)

- [ ] T028 [US1] Via `phase-implementer`, create the **exact completeness census** (C-2.2, FR-010,
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
- [ ] T029 [P] [US1] Via `phase-implementer`, add the C-2.6 exhaustive witnesses to
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
- [ ] T030 [US1] Via `phase-implementer`, add the C-2.3 compile-time presence checks.
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

- [ ] T031 [P] [US1] Via `phase-implementer`, add the v50sp2 Length-delimited group witness (R-6) to
  `tests/session/test_077_allversions_builder_roundtrip.cpp`.
  - Build one `NoPaymentStreamFormulas` entry (delimiter `PaymentStreamFormulaLength` 43109) whose
    Data holds SOH. Assert commit success, `43109=<octet count>` first in the entry, and a re-parse
    that recovers the Data.
  - Written after T014 and before T033: on T014's intermediate goldens the pair is coupled in the old two-call
    form, and the Data half still takes the string path (`set_string`), which refuses SOH. Run it and quote that RED; it turns GREEN at T036.
- [ ] T032 [P] [US1] Via `phase-implementer`, add a vlatest coupled-pair round trip to
  `tests/session/test_077_allversions_builder_roundtrip_vlatest.cpp`.
  - Use one vlatest message with a coupled Data member whose value holds SOH and `0xFF`. Assert
    verbatim emit, Length = octet count, and a re-parse (spec Edge Cases, "FIX Latest").
  - Written before T033 and run RED (the old two-call form routes the Data through `set_string`);
    quote it. It turns GREEN at T036.
  - The test is gated on `FIXPP_CODEGEN_FIX_LATEST`, which is ON by default and in
    `linux-clang-debug`.

### Implementation for User Story 1

- [ ] T033 [US1] Via `phase-implementer`, update the emitter's coupled arms in
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
- [ ] T034 [US1] Via `phase-implementer`, add `message_encoding` to
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
- [ ] T035 [US1] Via `phase-implementer`, run codegen freshness (quickstart §1) and regenerate the 078
  builder goldens for **v42, v44, v50sp2 and vlatest** under
  `specs/078-precompiled-builder-libs/contracts/golden/{v42,v44,v50sp2,vlatest}/`. The v50sp2 goldens
  start from T014's intermediate state.
  - Size the diff with R-5's recipe (re-run it; do not reuse a figure).
  - Every other read-tier pin in `fixpp::dict::read-tier-byte-diff` is unchanged. Only T013's two
    have moved; anything else is a stop (spec Assumptions).
- [ ] T036 [US1] Build, then run `test_067_builder_failclosed`, `test_067_builder_roundtrip`,
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

- [ ] T037 [US1] Show that the census can fire (C-2.2 positive controls) **before** relying on it.
  Each control runs in a scratch copy, output quoted:
  - **(a)** Run the census over the output of the "loader walk removed" mutant: the new emitter over
    the unfixed loader.
    - It must report missing **exactly** the tuples of the five R-11 pairs, and nothing else.
    - The orphan-half check must report exactly their ten tags on v50sp2.
    - The pre-change goldens are **not** this control: they hold no coupled call at all.
  - **(b)** Delete one coupled emission from a regenerated golden: reported missing.
  - **(c)** Move one call site from the nested arm to the top arm: reported as a wrong arm.
  - Record the three controls' output in `.specify/decisions/091-data-field-bytes-evidence.md`.
- [ ] T038 [US1] Via `phase-implementer`, validate the goldens **structurally** (C-2.4, FR-013)
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
- [ ] T039 [US1] Via `phase-implementer`, add a new executable `session_091_data_send` from a new file
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
    witness 1 RED. For witness 2, reproduce the pre-T034 build failure (the member does not exist) in a scratch
    copy at the pre-T034 commit, and quote it. Record both liveness results in `.specify/decisions/091-data-field-bytes-evidence.md`.
  - T003's manifest entry `session_091_data_send` now resolves. This is plan phase 5 (R-9, quickstart §5), and it closes US1's `args.message_encoding` scenario.
- [ ] T040 [US1] Via `phase-implementer`, run the US1 mutants from quickstart §3 in a scratch copy:
  - "the emitter passes `item.tag` instead of `item.data_tag`" → the v44 builder build fails on the
    C-2.2 `static_assert`;
  - "the emitter changes only the top-level arm" → the C-2.6 **nested** 256-value witness (T029),
    and T028's census (wrong arm);
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

- [ ] T041 [US2] Prove `SohInValue_RejectedBeforeAnyByteReachesOut` is byte-identical (FR-007,
  SC-002). `git diff origin/main -- tests/session/test_067_builder_failclosed.cpp` shows no hunk
  inside that `TEST` body, and every pre-existing injection-guard test in
  `tests/wire/test_body_builder.cpp` and `tests/session/test_067_builder_failclosed.cpp` is unedited
  (`git diff origin/main` hunks touch only new tests and the four flipped pins). Record both diffs in
  `.specify/decisions/091-data-field-bytes-evidence.md`.
- [ ] T042 [US2] Via `phase-implementer`, run the guard mutants from quickstart §3 in a scratch copy.
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
- [ ] T043 [US2] Confirm FR-012 / SC-002 from T028/T037: the census reports zero `field_data`/
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

- [ ] T044 [US3] Via `phase-implementer`, run the contract mutants from quickstart §3 in a scratch
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
- [ ] T045 [US3] Run C-1.5 on **MSVC** (`windows-msvc-debug` in the MSVC sandbox, per
  `phases/phase-4/parallel-worktrees.md`). The arrangement witness must hold there too, because
  outer-vector regrowth differs by STL. If the MSVC leg cannot run, record a waiver with its cost
  stated plainly ("CI's MSVC matrix runs only once both gate labels land"), not as "CI covers it".
- [ ] T046 [US3] Blast radius of FR-008 for hand-written callers (R-4).
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

- [ ] T047 [US4] Via `phase-implementer`, reword the comments FR-016 names:
  - `tests/interop/conversation/support/conv_wire.hpp` (the hand-built-frame route);
  - `tests/interop/conversation/conv_cell_test.cpp` (B-05).
  Both stay on their hook. The comment says why: moving B-05 needs a counterparty republish (spec
  Assumptions). It no longer says that #418 is open. No behaviour change.
- [ ] T048 [US4] Ledger edits in `spec/behaviors-and-limitations.md` and
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
  - **B-091-2:** `message_encoding` is available but not enforced when an `Encoded*` field is set.
  - **B-091-3:** the set-time vs C-ABI divergences. A dictionary-only Data tag is refused by
    `field_data`/`set_data` but accepted by `fixpp_msg_set_data` (follow-up fixpp#505). A repeated
    call appends where the C-ABI upserts.
  - **B-426-2:** its "every later field stays absent" no longer holds for a Logon: under FR-020
    `interpret_logon` refuses a Logon carrying a malformed count. Amend the row, citing FR-020.
  - **B-091-4, marked BREAKING (C-ABI 1.9):** data-model.md's row text. It covers the v50sp2 source
    break in both shapes (a compile error for designated or member access; a silent shift for a
    positional aggregate), the user-loaded-dictionary effects, the recipe-derived BREAKING
    population, and the additive widenings.
  - No citation of a line number; cite files and symbols.
- [ ] T049 [P] [US4] `spec/feature-catalogue.md` edits.
  - W-008's evidence column gains this feature.
  - A C-ABI 1.9 BREAKING note, in the style of CA-011's 1.8 note, on every CA row that lists a
    declaration FR-019 marks, reader-paragraph members included.
  - Re-derive the rows: match each Appendix A class-B row whose carrier is a declaration note or the
    reader paragraph against each CA row's listed functions. The root cause is the
    `fixpp_dict_load_from_xml` row, CA-011.
- [ ] T050 [P] [US4] Brain updates, orchestrator-authored:
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
- [ ] T051 [US4] Article XIX §5 docs check:
  - run `find . -maxdepth 3 -name 'Doxyfile*'`, and if one exists, regenerate it;
  - run `git grep -n -e body_builder -e length_pair -e 'Length+Data' -- docs/src`, and update any
    hit (`.md` is orchestrator-authored).
- [ ] T052 [US4] FR-016 sweep.
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

- [ ] T053 Run `/simplify` over the branch diff (`[const §XVI.7]`, pipeline step 11); fixes go through
  `phase-implementer`. Every later check (T060 onward) runs on the post-simplify head.
- [ ] T054 After `/simplify` (T053) and codegen freshness, re-measure the compile-time surface "after" figure with
  `bench/codegen/vlatest_builders_compile_bench/compile_bench.sh`, same command and toolchain as
  T002. Report the delta in `.specify/decisions/091-data-field-bytes-evidence.md`; it has no budget (R-10).

### Performance (SC-005, Article VIII; plan phase 6)

- [ ] T055 Via `phase-implementer`, make `bench/wire/builder_bench.cpp`'s WithGroup and Raw prechecks
  exact.
  - Pin complete `kWithGroupBody` and `kRawBody` (as `kNoGroupBody` is) and compare exactly.
  - Show each case's one-line mutant producing `SkipWithError`: drop one scalar field (both cases);
    drop one of the three `kParties` entries (WithGroup). Record each mutant's `SkipWithError` in
    `.specify/decisions/091-data-field-bytes-evidence.md`.
  - The final bench source must compile against the merge-base API; it may not call
    `field_data`/`set_data`.
- [ ] T056 Via `phase-implementer`, add the `builder_bench` row to `bench/ci-suite.txt` with tier-2
  value **`no`** (Article VIII §2a candidate-only; `paired` is irreversible), in the file's existing
  row format.
- [ ] T057 Run the paired SC-005 run, following quickstart §6 exactly.
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
- [ ] T058 Run the paired `bench/dictionary/xml_loader_bench` A-B-A-B (FIX50SP2 load) against the same
  base worktree, same procedure.
  - Pass condition: a slowdown ≤ +5 % (Article VIII §2). Over it → the §2 approval path, never
    self-declared.
  - Record the per-leg figures and the verdict in `.specify/decisions/091-data-field-bytes-evidence.md` §*xml_loader_bench*.
  - Then `git worktree remove --force /mnt/wsl/fixppbuild/091-base-wt`.

### Fuzz

- [ ] T059 Fuzz the changed loader (`[const §VII.7]`; owner ruling 2026-09-24: the dictionary XML
  loader is in scope for §VII.7, and this run is required): build `fuzz_dict_xml_loader`
  (`tests/fuzz/fuzz_dict_xml_loader.cpp`) under `linux-clang-asan` (`FIXPP_BUILD_FUZZ=ON`), add seeds
  with nested `<group>`s, `<group>`s under `<header>`/`<trailer>` and component-only Length/Data
  pairs, and run it for ≥ 600 s. Seeds are committed to `tests/fuzz/corpus/dict_xml_loader/`; its
  replay ctest exists only with `FIXPP_BUILD_FUZZ=ON`, so it is exempt from the `091` label rule and
  the `linux-clang-debug` manifest. Record the command, corpus and result in the evidence file; name
  the target to `/speckit-verify` (T065, `--fuzz-duration=600`) so it is not marked N/A.

- [ ] T073 Via `phase-implementer`, extend the seeds of `fuzz_session_recovery_admin_parse`
  (`tests/fuzz/fuzz_session_recovery_admin_parse.cpp`, which reaches `interpret_logon` through
  `Session::on_inbound_frame`; precedent 027 T026) for FR-020: Logons with a 95/96 count running
  past the frame, a count ending on a non-SOH byte, and `98=2` after a malformed count. Build under
  `linux-clang-asan` (`FIXPP_BUILD_FUZZ=ON`), run ≥ 600 s, commit the seeds to its corpus directory,
  record the command, corpus and result in the evidence file, and name the target to
  `/speckit-verify` (T065) beside `fuzz_dict_xml_loader`.

### Static analysis, claims and citations

- [ ] T060 Run clang-tidy, clang-format, cppcheck and IWYU (`[const §IX.4]`) on every changed file under `src/`, `include/` **and
  `tools/codegen/`** (#265). Never format `specs/` or `include/fix/c_api/*.h`. Any finding on a
  changed line is fixed by `phase-implementer`.
- [ ] T061 Run `python3 /home/catalin/Work/Programming/Antreprenoriat/.claude/scripts/check-comment-claims.py
  --root <tree> --base origin/main`, with `<tree>` the worktree that owns this branch (absolute
  path, so it also works from a parallel worktree).
  - Read every hit in the comments this feature authored: the `version.h` history, the BREAKING
    notes, the read-tier banner, the `body_builder.hpp` contract comments and the CMakeLists
    comments. Also read the strings the script cannot see.
  - A claim that records a result is deleted, not replaced.
- [ ] T062 Run `python3 tools/check_line_citations.py --shift-audit origin/main..HEAD`. For a hit on
  the checker's own fixture strings, apply the `# citation-ok` pragma.

### Close-out checks

- [ ] T063 Confirm fixpp#506 is still open (`gh issue view 506 --json state`), or record its fix,
  before 091 closes (plan phase 7). Confirm fixpp#505 is open and cited in B-091-3.
- [ ] T064 Run the quickstart §2 label gate. It must pass: the label set equals the manifest, and
  every manifest entry is a registered test. Then run `ctest --test-dir build/linux-clang-debug -L '^091$' --output-on-failure`, all
  GREEN.
- [ ] T065 Run `/speckit-verify` (mandatory after `/speckit-implement`, Article XVII §8). It produces
  `.specify/decisions/091-data-field-bytes-verify.md`. The record cites
  `.specify/decisions/091-data-field-bytes-evidence.md`, which every task whose body says "record … in" that file wrote; its
  discriminating-witness rows point there rather than restating it.
  - It covers /speckit-verify's full preset matrix (ASan, UBSan, TSan, …) and the MSVC leg,
    coverage (every new line in `src/wire/body_builder.cpp`,
    the new loader walk in `src/dictionary/xml_loader.cpp` and `interpret_logon`'s changed lines in
    `src/session/admin_messages.cpp`), clang-tidy and ABI hygiene.
  - It includes the full `ctest --test-dir build/linux-clang-debug` run.
  - ⚠️ The §7 full build needs an owner ASK, even as gate evidence.

- [ ] T066 **`CLAUDE-history.md` entry** (Article XIX, plan Constitution Check): via `phase-implementer`
  (the edit guard decides the file class), add a newest-first 091 entry to the library's
  `CLAUDE-history.md` naming the feature, the PR, `Closes #418`, C-ABI 1.9 BREAKING, the FR-020
  `[const §XII.7]` fail-open fix (it affects shipped dictionaries through 95/96) and the
  follow-ups #505/#506. Update `CLAUDE.md`'s "Last merged FEATURE" pointer only at merge.
- [ ] T067 **PR description** (FR-019, plan phase 7). The body carries:
  - the `[const §X.7]` **C-ABI 1.9 BREAKING** declaration: FR-019's population, pointing at B-091-4
    and data-model.md Appendix A;
  - the v50sp2 source break (FR-011 carve-out);
  - the FR-020 fix: `interpret_logon` refuses a Logon carrying a malformed count, closing a
    `[const §XII.7]` fail-open reachable on `main` through 95/96;
  - `local build: green on linux-clang-debug @ <git-sha>` (`[const §XVII.7]`), with the SHA T065
    verified;
  - a `## Gates` section, and a `## Gate B …` heading for the Gate B record;
  - under `## Gates`, the record backing `gate-a-done`: `.specify/decisions/091-data-field-bytes-gatea.md`
    (loop 3 round 3 converged Opus-only, owner-accepted without the Codex pass). If Gate B rules
    that `[const §XVII.8]`'s "Codex convergence record" is not met, ask the owner before labelling;
    never choose between `gate-a-done` and `gate-a-waived` unilaterally;
  - `Closes #418` as the ONLY closing keyword.
  Before opening, grep the body AND every commit message on the branch
  (`git log origin/main..HEAD --format=%B`) for `close[sd]?|fix(e[sd])?|resolve[sd]?` next to
  `#50[56]` or any number other than 418. A negated keyword still links. After opening, check
  `closingIssuesReferences` lists only #418.

### Mandatory close-out tasks (Gate-B preconditions, Article XVII §8)

- [ ] T068 [P] **Catalogue close-out.**
  - Flip every feature-owned OFFICIAL row in `spec/feature-catalogue.md` to `done` with this PR as
    evidence. That covers W-008, which gains 091's evidence (T049), and any row 091 owns; the CA
    rows carry the 1.9 note from T049.
  - Add or update the matching `spec/coverage-index.md` entry: `[FIX50SP2 §3.3] Field data types` ↔
    W-008, naming this feature's witnesses (the `_418` pins, C-2.6, C-1.2).
- [ ] T069 **Feature-completeness audit (the FINAL task).** Assert against the merged tree:
  - (i) every `tasks.md` row is `[X]` or carries an explicit waiver rationale;
  - (ii) every FR-001…FR-020 (including FR-004a, FR-009a and FR-011a) and SC-001…SC-006 maps to a
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
  - Then T026 (needs T006–T012, T020 and T071), then T027 (needs everything above).
  - **Blocks all stories.**
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
  - T053 (simplify, first, so every measurement is of the final candidate) → T054 (compile-surface
    "after") → T055 → T056 → T057 → T058 → T059 → T073 (fuzz);
  - T060–T063 after T053;
  - T065 after T060–T063;
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
2. Foundational (T006–T027, T070–T072): the loader, C-ABI 1.9 and the `body_builder` API are all GREEN.
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
