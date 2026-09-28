# Tasks: The session never acts on a frame it could not parse

**Feature**: `092-garbled-frame-reject` (fixpp#507, batch B18) | **Branch**: `092-garbled-frame-reject`

**Input**: `specs/092-garbled-frame-reject/`:
- spec.md (FR-001…FR-019, SC-001…SC-010);
- plan.md (phases 0–10);
- research.md (R-1…R-14);
- data-model.md (E-0…E-7);
- contracts/unparseable-frame-disposition.md (C-1…C-6, rows D-1…D-9, invariants I-1…I-7,
  disclosures L-1, L-2, L-4, L-6, L-7);
- quickstart.md (§0–§3).

**Tests**: REQUIRED. The spec names the witnesses (SC-001…SC-010), and Article VII requires TDD
order: each witness is written first and shown RED for the reason its clause states, and only then
made GREEN.

**Gate A**: converged 2026-09-27, loop 2 round 3, and signed off by the user (parent record
`decisions/speckit/092-garbled-frame-reject-gatea.md`). The five P3s L2R3-001…005 were folded into
the bundle when these tasks were generated; the tasks that carry them are named in §Notes.

## Execution rules (apply to every task)

- **Who executes.** Every task that edits a file other than `*.md` under `specs/`, `spec/`, `brain/`
  or `docs/` is delegated to `phase-implementer` (`[const §XVI.6]`; the orchestrator edit guard
  refuses it and has no override). That includes code, tests, CMakeLists, benches, the fuzz corpus,
  `capi_freeze.sha256`, `expected-ctest-092.txt`, and **comment-only edits** in
  `.hpp`/`.cpp`/`.h`/`.cmake` files. The orchestrator authors only `tasks.md`, the B&L text,
  `brain/`, the `spec/feature-catalogue.md` / `spec/coverage-index.md` markdown, and the evidence
  file.
- **Builds need an owner ask (`[const §XVII.7]` resource gate).** Before dispatching any task that
  configures, builds, rebuilds or runs `conan install`, ask the owner with `AskUserQuestion`. The
  tasks that need it include T004, T006, every RED/GREEN step, the fuzz runs, the bench runs and
  `/speckit-verify`. One approval may cover a named phase; record each ruling in the evidence file.
  Never auto-run a build.
- **Disk and build trees.** Run `df -h /mnt/e` before any full rebuild of the main checkout's
  `build/linux-clang-debug`. The sanitizer, coverage and release trees live on the F: vhdx
  (`/mnt/wsl/fixppbuild`). Run long builds with `setsid nohup` and a `.done` marker.
- **Never `git checkout`/`git switch` in this checkout.** The bench base (T004, T069) is its own
  detached worktree under `/mnt/wsl/fixppbuild`.
- **Mutants run in a scratch copy of the tree, never in the PR worktree.** Each mutant is shown RED
  on the test it names, then GREEN after revert. A `git diff` of the scratch copy against the PR
  head proves the revert is clean. Before believing a GREEN, `strings`-check that the binary is
  fresh: a stale binary fails toward clean.
- **Test placement (fixpp#511).** Never add `FIXPP_TEST_HOOKS` to a new target. A cell that reads
  the inbound counter or another private accessor goes into an **existing** target that already
  defines it. The ones 092 uses:
  - `session_validation_compat_toggles` (`test_validation_compat_toggles.cpp`);
  - `session_persistent_seqnum_hydrate` (`test_persistent_seqnum_hydrate.cpp`);
  - `store_fail_reconcile` (`test_store_fail_reconcile.cpp`);
  - `session_store_tests` (`seqnum_manager_test.cpp`);
  - `capi_send_recv_test` (`length_data_logon_refusal_test.cpp`).

  `unparseable_frame_disposition_test.cpp` is a new target and stays hook-free: no cell in it reads
  the counter. In it, "NextNumIn unchanged" or "NextNumIn advanced" is witnessed by the next
  conformant frame: at the old number it is delivered with no ResendRequest (unchanged), or at the
  next number (advanced); a gap draws a ResendRequest. T038's reconnect reads the durable counter by
  reopening the `FileStore` and calling `next_seqnum(inbound, false)` (T052's recipe).
- **Labels.** Every ctest entry whose cells a task adds or edits carries label `092` (an entry
  whose only change is its `--gtest_filter`, such as `capi_send_recv_positive` in T021, does not). On an existing entry, use
  `set_property(TEST <name> APPEND PROPERTY LABELS 092)`. Never use `set_tests_properties(... LABELS
  ...)` on an existing entry: it overwrites the labels the entry has. `expected-ctest-092.txt`
  (T007) is the gate.
- **Re-derive, never copy.** Every population the bundle gives with a command (R-4/C-6 late sites,
  R-7 error pins, R-8 C-ABI effects, R-10 old-behaviour tests, R-12 scan callers, R-13 text sites,
  R-14 `check_inbound` callers) is re-run at the implementation head. A listed member is a lead,
  not an input; a member the command adds is a planned edit, not an omission.
- **Comments record a procedure, never a result** (no counts, offsets, line numbers or pasted
  output). When a comment supersedes a decision, name the new one in a header comment (parent
  CLAUDE.md, fixpp#310).
- **Every Reject cell asserts the exact 373, the exact 371 (or its absence), 45 and 372.**
- **Evidence file.** Mutant, bench, ceiling, population and fuzz evidence goes to
  `.specify/decisions/092-garbled-frame-reject-evidence.md`. `/speckit-verify` writes its own record
  (T078) and cites this file. Never write into the verify record before T078 creates it.
- **Commit after each task or logical group.** The RED form of each TDD step goes in its commit
  message.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: can run in parallel (different files, no dependency on an incomplete task).
- **[Story]**: US1–US4 from spec.md. Setup, Foundational, the two cross-cutting phases (FR-019,
  FR-012) and Polish carry no story label.

---

## Phase 1: Setup (baselines, before any production edit)

**Purpose**: the measurements that cannot be taken after the first production edit, the RED anchor,
the population snapshots, and the label manifest (plan phase 0, quickstart §0).

- [X] T001 Pre-flight in the library root.
  - `git rev-parse --abbrev-ref HEAD` prints `092-garbled-frame-reject`, and `git status --short`
    is empty.
  - Run `git fetch --all --prune` and record `git merge-base HEAD origin/main`.
  - `git diff --stat origin/main...HEAD -- src include tools cmake tests bench` is empty: production
    code is still `main`'s, which T004 and T005 depend on.
  - `df -h /mnt/e` shows room for a `-debug` rebuild.
  - Create `.specify/decisions/092-garbled-frame-reject-evidence.md` with an `## Owner build
    rulings` section, and ask the owner for build approval (per phase, or blanket). Record the
    answer there.
- [X] T002 Via `phase-implementer`, land the two benches in a **bench-only commit** (R-9). No file
  outside `bench/` changes in it.
  - `bench/session/scan_frame_header_bench.cpp`: clean frames (a Heartbeat, a NewOrderSingle, and
    a frame carrying a standard Length+Data pair), each under the session's dictionary hooks and
    under `dict_hooks::none()`.
  - A clean Active `on_inbound_frame` bench, `bench/session/on_inbound_frame_bench.cpp`: an
    in-sequence Heartbeat and NewOrderSingle through an established session, built on the session
    test support that already drives `on_inbound_frame`. It is the only instrument that sees the
    inline branch (R-3) and the FR-019 compare in `check_inbound` (R-14).
  - Register both in `bench/session/CMakeLists.txt`.
  - Add both to `bench/ci-suite.txt` in the file's existing row format, with a tier-3 comparand
    (R-9). A bench missing from that list has no execution gate.
  - The bench sources compile against the merge-base API: no new symbol.
- [X] T003 Record the bench commit's `git patch-id --stable` in the evidence file §*Bench*.
- [X] T004 Run the paired baseline (R-9, quickstart §0.1).
  - **Base:** a detached worktree at `/mnt/wsl/fixppbuild/092-base-wt` at the merge-base from T001,
    with only T002's commit cherry-picked. `git -C <wt> diff --stat <merge-base> -- src include`
    prints nothing.
  - **Candidate:** this branch at T002's commit.
  - Both built with `linux-clang-release` options on one machine. Precheck: diff the two
    `CMakeCache.txt` files' `FIXPP_*`, `CMAKE_BUILD_TYPE` and compiler entries; any difference other
    than the source directory stops the run.
  - A-B-A-B, `taskset -c 3`, into a fresh `mktemp -d -p /mnt/wsl/fixppbuild` output directory.
    Report min-per-tree per case, both SHAs and T003's patch-id, in the evidence file §*Bench
    baseline*. It cannot be reconstructed after the edit.
  - Keep the base worktree for T069.
- [X] T005 Via `phase-implementer`, add #507's reproducer (the table the issue body calls T076;
  `gh issue view 507 --repo CatalinSerafimescu/fixpp`) as a real test, `Issue507Reproducer_*` cells in the new
  `tests/session/unparseable_frame_disposition_test.cpp`, with the new target registered by
  `add_threading_test(session_unparseable_frame_disposition unparseable_frame_disposition_test.cpp)`
  beside `session_validate_gate_inbound` in `tests/session/CMakeLists.txt`, labels `"092;session"`.
  No `FIXPP_TEST_HOOKS`.
  - **Why standalone, not a grouped bucket (`[const §VII.8]`).** Its cells drive `Session`
    coroutines on an io_context with timers (the logout timeout, the liveness interval, the resend
    loop, the scripted peer), which is not the pure, single-threaded, stateless class §VII.8
    groups. Its neighbour `session_validate_gate_inbound` is standalone for the same reason. Write
    this reason in the CMakeLists comment above the registration.
  - The four framed-but-unparseable cells (malformed count or malformed tag, inbound validation on
    or off): a SequenceReset (Reset mode, NewSeqNo=500) at the expected MsgSeqNum 2, then a
    conformant Heartbeat at 500.
  - Assert the **post-092** outcome: a Reject (45=2, 372=4, and 373=0 with no 371, or 373=5 with 371
    = the Length tag), and the probe at 500 draws a ResendRequest (SC-001).
  - Run it: RED today, with every cell showing `probe(34=500): resend=0` (the reset applied). Commit
    with the RED output quoted.
- [X] T006 Measure the parse-arena ceiling (R-4, quickstart §0.3): the smallest field count at which
  a well-formed NewOrderSingle fails `Parser<Index>::parse` in `kInboundParseArena`, on each Linux
  preset the late-site cells (T038) run on (`linux-clang-debug`, and every sanitizer preset
  `/speckit-verify` runs). Use a scratch program or a throwaway test, not a committed one. Record
  the figure per preset with the SHA in the evidence file §*Arena ceiling*. The per-lane cap is
  fixpp#515's.
  - Record which presets and lanes have a null arena upstream (`fixpp::detail::arena_upstream()` in
    `include/fixpp/core/pmr_arena_upstream.hpp`; MSVC debug does not), because T038's guard skips
    the others.
  - Also check whether the offset-table cap (`wire_offset_table_full`) gives a well-formed frame
    that fails the parse on every platform, including MSVC debug. Record the result; T038 uses it if
    so.
- [X] T007 Via `phase-implementer`, create `specs/092-garbled-frame-reject/expected-ctest-092.txt`,
  one ctest name per line, sorted:
  `capi_logon_malformed_tag`, `capi_pure_tests`, `error_017_completeness`,
  `error_019_completeness`, `error_020_completeness`, `error_092_completeness`,
  `session_length_data_scanner`, `session_persistent_seqnum_hydrate`, `session_pure_tests`,
  `session_scan_frame_header_overflow`, `session_store_tests`,
  `session_unparseable_frame_disposition`, `session_validation_compat_toggles`,
  `store_fail_reconcile`, `wire_dict_tests`.
  - Append label `092` to each entry that already exists, with
    `set_property(TEST <name> APPEND PROPERTY LABELS 092)` placed **after** the entry's last
    `set_tests_properties(... LABELS ...)` (an APPEND placed before it is overwritten).
  - `capi_logon_malformed_tag` (T021), `error_092_completeness` (T060) and
    `session_unparseable_frame_disposition` (T005) are registered by their tasks.
  - The gate: `ctest --test-dir build/linux-clang-debug -N -L '^092$' | sed -n 's/.*Test *#[0-9]*: //p' | sort`
    equals the manifest. Positive control: before T021 and T060, the gate reports exactly those two
    names missing.
- [X] T008 Re-derive every command-given population at the implementation head, and record each
  output and its classification in the evidence file §*Populations*. Each later task that consumes
  one names this section.
  - R-10 old-behaviour tests: the four `grep`/`git grep` commands of research R-10.
  - R-12 scan callers: `grep -n "scan_frame_header(" src/session/session.cpp`.
  - R-4 / C-6 late sites: `grep -n "parse_and_dispatch_(\|validate_inbound_(" src/session/session.cpp`,
    each call classified by the provenance of its bytes (peer → late inbound site; built by fixpp →
    not).
  - R-4 paths that never parse: `grep -n 'hdr.msg_type == "' src/session/session.cpp`.
  - R-7 error pins: the three `git grep` commands of research R-7.
  - R-14 callers: `grep -n "check_inbound(" src/session/session.cpp` and
    `grep -n "consume_rejected_seqnum_(" src/session/session.cpp`.
  - R-13 text sites: the `git grep` of research R-13.
  - The C-2 pre-Active refusal source:
    `grep -n "No MsgType discrimination\|out-of-scope admin" src/session/session.cpp`.
  - Via `phase-implementer` (after T007 exists): every member these commands add whose edit lands in a ctest entry missing from
    `expected-ctest-092.txt` extends the manifest (and gets its APPEND label), recorded here. Then
    re-state T007's positive control: the gate reports exactly the not-yet-registered new entries
    missing.
  - `FrameHeader` size pins:
    `git grep -n "sizeof(FrameHeader)\|sizeof(fixpp::session::detail::FrameHeader)\|FrameHeader) ==" -- src tests bench`.

**Checkpoint**: benches baselined, the #507 reproducer (T005) RED, the ceiling measured, the populations recorded.

---

## Phase 2: Foundational (blocking prerequisites)

**Purpose**: the scan's fault record (plan phase 1), the replay guard its stop-first rule needs
(R-12), the Text-carrying Reject builder (plan phase 3), the C-ABI 1.10 declaration (plan phase 0b),
and the disposition mechanism every story routes through (plan phase 2). Every story's cells test a
C-2 row, and the rows are one function, so the mechanism lands here with one anchor cell per row.

**⚠️ No user-story task starts until this phase's checkpoint holds.**

### 2a — The scan fault record (E-0, E-1; R-1, R-2), tests first

- [X] T009 [P] Via `phase-implementer`, write the RED cells for the scan fault record in the new
  `tests/session/scan_frame_header_fault_test.cpp`, added to the `session_pure_tests` bucket in
  `tests/session/CMakeLists.txt` (pure: no thread pool; the bucket is already hooked, so #511 is not engaged). One cell per fault kind and site
  in data-model E-1:
  - `malformed_tag`: a non-digit tag byte; a tag above 0xFFFF; no `=` before SOH; no `=` before the
    end of the buffer; an empty tag (`=` first);
  - `length_data_mismatch`: a count past the frame, a count ending exactly at the frame end, and a
    counted value not followed by SOH, each asserting `fault_length_tag` = the Length tag;
  - `fault_offset` equals the planted field's first byte, in every cell;
  - the scan stops at the first fault: every member after it is empty, every member before it is
    populated;
  - `msg_type_is_third`: true for `8|9|35=…`; false when field 3 is not 35, when field 3 is itself
    the faulting field, and when the scan faults before field 3;
  - `fault_ref_seq_num` holds the **first** well-formed 34 (`34=99|35=D|34=2|9x9=1` → 99);
    `fault_ref_msg_type` holds field 3's value, never a later 35 (`35=D…35=4…9x9=1` → D);
  - **I-3, clean frames stay last-wins:** a fault-free `34=1|…|34=5` scans `msg_seq_num` = 5 and a
    fault-free duplicate 35 scans the last `msg_type`, as today, with `fault == none`.
  Run under the session hooks and under `dict_hooks::none()`. RED: it does not compile (no member),
  which is the expected RED for a new member; commit it as such.
- [X] T010 Via `phase-implementer`, add `enum class field_fault : std::uint8_t { none = 0,
  malformed_tag = 1, length_data_mismatch = 2 }` to `include/fixpp/wire/tag_scan.hpp` in namespace
  `fixpp::wire` (E-0). No include is added to either reader.
- [X] T011 Via `phase-implementer`, implement E-1 in `src/session/scan_frame_header.hpp`.
  - Append the six members of E-1 to `FrameHeader`: `fault` (`fixpp::wire::field_fault`),
    `fault_length_tag` (`std::uint16_t`, "the tag of the field immediately before the faulting Data
    field … else 0"), `fault_offset` (`std::uint32_t`, instrument only: never emitted, never
    logged), `msg_type_is_third` (`bool`), `fault_ref_seq_num` and `fault_ref_msg_type`
    (`std::string_view`).
  - On the first fault the scan returns immediately. The tag-digit loop, `accumulate_tag_digit`'s
    refusal, the missing `=`, and the empty tag set `malformed_tag` (today the first three
    `continue`, and an empty tag scans as tag 0). `length_data_carry::read_value` returning nullopt
    sets `length_data_mismatch` (today it returns without a record).
  - `fault_ref_seq_num` is written once on first sight of a well-formed 34; `fault_ref_msg_type`
    only when field 3 is a well-formed 35. Both are written on fault-free frames too (the
    differential compares them there). `msg_seq_num` and `msg_type` stay last-wins.
  - Before changing the struct's size, check T008's size-pin population and update any pin.
  - T009 GREEN.
- [X] T012 Via `phase-implementer`, update the old-behaviour tests T008's R-10 population lists,
  each classified in the evidence file:
  - `tests/session/scan_frame_header_overflow_test.cpp`: the `ForgedTag*` / `Token*` cells gain a
    fault-kind assertion; `NonDigitToken_Rejected_NotDispatched`'s "conforming pair" half moves to
    its own frame; `ConformingFrame_AllFieldsCorrect` unchanged.
  - `tests/session/length_data_session_scanner_test.cpp`: the `ScanFrameHeaderStopsAt…` cells gain
    fault-kind and Length-tag assertions; the 091 Logon refusal cells unchanged.
  - `tests/session/coverage_adversarial_test.cpp`: rename `InboundFrameMalformedTagCharSkipped` and
    `InboundFrameTagWithoutEqualsSkipped` for the fault-record branch and give each an assertion
    that can go RED (`X35=…` in field 3 → D-8 disregard; the no-`=` field → D-7 or D-8 by its
    position). Their RED comes with T025. **Amended at T008 (2026-09-28):** both cells only call
    `open()` before feeding. The default role is initiator, so the frame arrives in LogonSent and
    hits row D-2, which already disconnects today. That assertion could not go RED. Each cell first
    drives the session to Active with the Logon-ack the same fixture already uses (the Category C
    cell's `35=A`/`34=1` frame). Then the D-8/D-7 assertion is reachable. The frame's MsgSeqNum
    becomes 2 after the ack. Evidence file §*Populations*.
  - Any other hit is classified as "still holds, gains an assertion", "flips (RED first)",
    "renamed" or "mention only".
- [X] T013 Via `phase-implementer`, write the differential corpus (C-3 I-4, R-2 §1) in
  `tests/session/scan_frame_header_fault_test.cpp`.
  - **Seeds:** a well-formed frame for every admin MsgType the session handles and one application
    type, one carrying a standard Length+Data pair and one a dictionary-only pair. Assert each seed
    builds cleanly under the hooks it is used with **before** mutating it.
  - **Mutations** at every field position: a non-digit tag, an empty tag, a missing `=`, a tag above
    0xFFFF, a short count, a long count, a count past the frame, a count ending exactly at the frame
    end.
  - **Oracle per case:** scan `malformed_tag` ⇔ `OffsetTable::build` fails at a tag site
    (`wire_invalid_field_format` or `wire_tag_out_of_range`); scan `length_data_mismatch` ⇔
    `wire_invalid_field_format` at a counted site; `fault_offset` and `fault_length_tag` equal the
    planted mutation's; every member before the planted offset populated, every one after empty.
  - For a fault-free input: `fault_ref_seq_num` equals the first `entries()` element with tag 34;
    `msg_type_is_third` equals "the third `entries()` element has tag 35"; `fault_ref_msg_type`
    equals the third element's value when its tag is 35, else empty. The oracle is `entries()`,
    never `find(34)`.
  - Every case under the dictionary hooks and under `dict_hooks::none()`.
  - **Accepted controls,** each clean on both sides: a leading-zero tag; an empty ordinary value; a
    non-numeric Length value; a Length as the last field before the trailer; an unrelated tag after
    a Length; the standard static pairs under `dict_hooks::none()`; a duplicate 34 and a duplicate
    35; a fault-free frame whose field 3 is not 35.
- [X] T014 In a scratch copy, plant one disagreement **per mutation family** and show that family's
  corpus cells RED (quickstart §2 "Differential"): non-digit, overflow, empty tag, no `=`, non-SOH,
  end-equals-size, the `fault_ref_seq_num` first-34 selection, the `fault_ref_msg_type` third-field
  selection. Revert each; record the eight mutants and their RED cells in the evidence file.
- [X] T015 Via `phase-implementer`, the replay guard (R-12). In `src/session/session.cpp`'s stored-frame
  replay classification (`app_present` in the resend store walk), a stored frame whose scan read no
  35 (`msg_type` empty) is not `app_present`, so it is gap-filled. Write the cell first in
  `tests/session/unparseable_frame_disposition_test.cpp`: an **admin** frame with a fault in field 2,
  stored through a custom `MessageStore`, is gap-filled on resend and not resent. Show it RED on
  T011's head without the guard, then GREEN. Confirm the credential-masking caller (outbound, fields
  1–2 written by fixpp) needs no change, citing T008's R-12 population.

### 2b — The Text-carrying Reject builder (R-5, C-4), tests first

- [X] T016 [P] Via `phase-implementer`, write the RED cells for `build_reject_with_text` in
  `tests/session/session_reject_test.cpp` (bucket `session_pure_tests`, label from T007), beside its
  existing `build_reject` field-shape cells.
  - A non-empty text is emitted as 58.
  - An empty text emits no 58.
  - `build_reject`'s output is byte-identical to today's for the existing goldens (run the existing
    golden cells unchanged).
  - An empty 372 is omitted.
- [X] T017 Via `phase-implementer`, add `build_reject_with_text(…, std::string_view text)` to
  `include/fixpp/session/admin_messages.hpp` and `src/session/admin_messages.cpp`. `build_reject`
  stays a **single declaration**, unchanged in signature, and delegates with an empty text.
  `emit_session_reject_` gains the matching `text` parameter (private). T016 GREEN.
- [X] T018 Via `phase-implementer`, the 372 bound (R-5). Add a named constant for the longest MsgType
  any shipped dictionary defines (no literal count in a comment), and a unit test that recomputes it
  from `dictionaries/*.xml` so it cannot drift, in `tests/session/session_reject_test.cpp`. Add the two fixed
  Text constants, one per fault kind, carrying no offset and no peer bytes.

### 2c — C-ABI 1.10 BREAKING (FR-017, R-8, plan phase 0b), witnesses and pin first

- [X] T019 Preconditions, recorded in the evidence file §*C-ABI 1.10*:
  - `gh release list --repo CatalinSerafimescu/fixpp --exclude-drafts` is empty;
  - after `git fetch --all --prune`,
    `git grep -h "define FIXPP_C_ABI_VERSION_MINOR" $(git for-each-ref --format='%(refname)' refs/heads refs/remotes) -- include/fix/c_api/version.h | sort | uniq -c`
    shows no 10. Positive control: `origin/main` shows 9.
  Re-run the second check right before T023's commit.
- [X] T020 Re-run 091's C-ABI population recipe (`specs/091-data-field-bytes/research.md` R-11,
  steps 1–6) at the implementation head with 092's change as input. Classify every candidate effect
  in R-8's table (D-1/D-2 refusal; D-3 disconnect; D-7 reversal; the LogoutSent confirmation; the
  application Reject; the late-site close at a dispatch site; the FR-019 close; FR-012). Record the
  classification and the observer set in the evidence file.
- [X] T021 [P] Via `phase-implementer`, write the C-ABI witnesses (RED) in
  `tests/capi/length_data_logon_refusal_test.cpp` (target `capi_send_recv_test`, already hooked;
  #511): a `CapiLogonMalformedTag` suite mirroring `CapiLogonMalformedCount`, for a Logon carrying
  `9x9=1`, on every observer T020 names, as acceptor and as initiator, plus a well-formed-tag control
  that establishes.
  - Register `add_test(NAME capi_logon_malformed_tag COMMAND capi_send_recv_test --gtest_filter=CapiLogonMalformedTag.*)`
    with labels `"capi;092"` in `tests/capi/CMakeLists.txt`, and add
    `:CapiLogonMalformedTag.*` to `capi_send_recv_positive`'s negative filter.
  - Run: RED on the unchanged session (the Logon establishes). Commit with the RED output.
- [X] T022 [P] Via `phase-implementer`, set `tests/capi/version_test.cpp`'s exact-version cell and
  `CompositeMacroValue` to 1.10. Run: RED against MINOR 9 (`[const §VII.3]`). Commit with the RED
  output.
- [X] T023 Via `phase-implementer`, bump `FIXPP_C_ABI_VERSION_MINOR` 9 → 10 in
  `include/fix/c_api/version.h`, with a 1.10 history comment naming 092/fixpp#507, every observer,
  and every effect T020 classified that has no carrying declaration. Update every in-repo consumer
  from `git grep -ln "VERSION_MINOR\|0x010900\|1_9_0\|(9U << 8U)" -- . ':!specs'`, each hit
  classified in the evidence file. T022 GREEN.
  - **SC-009 mutant:** after the bump, in a scratch copy, set MINOR back to 9 and show
    `version_test` RED; revert and show GREEN. Record it in the evidence file. T022's pre-bump RED
    is a different proof.
- [X] T024 Via `phase-implementer`, add a **BREAKING (C-ABI 1.10; 092)** clause to each affected
  declaration in `include/fix/c_api/session.h` (T020's set), and rewrite
  `fixpp_session_register_callback`'s 1.9 sentence ("dropped as a parse error, silently … no Reject
  is sent") within the scope research R-8's table row states. Where `include/fix/c_api/error.h` lists the core errors `WIRE_INVALID_FRAME` coalesces,
  that list is updated in T059, not here. Re-pin `tools/capi_freeze.sha256` with
  `tools/check_capi_freeze.sh`. Never run a formatter on `include/fix/c_api/*.h`.

### 2d — The disposition mechanism (C-1, C-2; R-3; E-2), anchor cells first

- [X] T025 Via `phase-implementer`, write one RED **anchor** cell per C-2 row in
  `tests/session/unparseable_frame_disposition_test.cpp`. Each uses an input the pre-feature code
  mishandles (SC-006), and each Reject cell asserts 45, 372, 373 and 371 exactly:
  - D-1: acceptor in NotConnected, a Logon with a malformed **tag** → Disconnected, no outbound.
  - D-2: initiator in LogonSent, a reply Logon with a malformed tag → Disconnected, no outbound.
  - D-9: LogoutSent, a faulty **Logout** → not taken as the reply; the logout timeout ends the
    session.
  - D-8: Active, `8|9|49=…|35=D|34=N|9x9=1|…|10` → no outbound, NextNumIn unchanged, still Active.
  - D-7: Active, a fault before 34 → as D-8.
  - D-3: Active, a faulty Logon (field 3 is 35, 34 read) → silent Disconnected, no Reject, no
    Logout.
  - D-4: Active, a faulty SequenceReset (Reset, NewSeqNo=500) at N → Reject; a conformant message at
    N+1 then draws a ResendRequest (no advance, NewSeqNo not applied).
  - D-5: Active, a faulty NewOrderSingle at N → Reject (45=N, 372=D); `fromApp` not invoked; a
    conformant message at N+1 is delivered with no ResendRequest (advance witnessed). Liveness is
    T031's, not this cell's.
  - D-6: Active, a faulty NewOrderSingle at N+5 → Reject (45=N+5), no ResendRequest, NextNumIn
    unchanged (a conformant message at N is then delivered with no ResendRequest).
  Also the two `coverage_adversarial_test.cpp` cells T012 renamed. Run: RED. Commit with the RED
  output.
- [X] T026 Via `phase-implementer`, implement the disposition in `src/session/session.cpp` and
  `include/fixpp/session/session.hpp` (private declarations only).
  - Hoist the NotConnected arm's scan to the top of the arm and reuse it in the validate gate and
    the post-`interpret_logon` block, so the arm scans once per frame (E-2, R-3).
  - In each of the four state arms (NotConnected, LogonSent, LogonReceived/Active, LogoutSent),
    right after the scan and **before** the validate gate and Guards 2–5, branch **inline** on
    `hdr.fault != fixpp::wire::field_fault::none`; only on that branch `co_await
    dispose_unparseable_(hdr, state)` and return (C-1 step 3). The fault-free path adds one compare
    and no coroutine frame.
  - In Active, the MaxMessageSize(383) guard stays **before** the state switch (C-1 step 1b).
  - `dispose_unparseable_` evaluates C-2 top to bottom, reading only `hdr.fault`,
    `hdr.fault_length_tag`, `hdr.msg_type_is_third`, `hdr.fault_ref_seq_num` and
    `hdr.fault_ref_msg_type` (I-1). "34 read" is `parse_seqnum(hdr.fault_ref_seq_num) > 0`.
    - D-1/D-2/D-3: `record_state_transition_(fsm_state::Disconnected)`, the action the arm takes when
      `interpret_logon` refuses; no Reject, no Logout.
    - D-9, D-8, D-7: disregard (no Reject, no advance, no disconnect).
    - D-5: `consume_rejected_seqnum_(parse_seqnum(hdr.fault_ref_seq_num), hdr.fault_ref_msg_type)`
      (never `hdr.msg_type`), and on its failure `co_return` the failure before any Reject; then
      the Reject.
    - D-4, D-6: the Reject with no advance; no ResendRequest, no too-low Logout, PossDup not
      consulted.
    - The Reject: `emit_session_reject_` → `build_reject_with_text` with 45 =
      `fault_ref_seq_num`; 372 = `fault_ref_msg_type` when within T018's bound, else empty; 373 = 0
      with 371 omitted for `malformed_tag`, 373 = 5 with 371 = `fault_length_tag` for
      `length_data_mismatch`; 58 = T018's fixed Text for the kind.
    - No row writes the inbound-liveness timestamp (FR-018): the disposer adds no
      `last_inbound_steady_` write.
  - Rewrite the #423 ruling references at the validate gate with a pointer to the 2026-09-27
    revision of row 4 (R-13), naming the ruling in the header comment where a decision is
    superseded.
  - T025 and T005 GREEN.

**Checkpoint**: the scan records faults and agrees with `OffsetTable`; replay is fail-safe; the
Reject builder carries Text; C-ABI 1.10 is declared; every C-2 row has a GREEN anchor; the #507 reproducer (T005) is
flipped.

---

## Phase 3: User Story 1 — A malformed admin message is never acted on (Priority: P1) 🎯 MVP

**Goal**: a faulty SequenceReset, Logout, TestRequest, ResendRequest or Heartbeat in LogonReceived or
Active changes nothing but the Reject rows' NextNumIn accounting, and draws a Reject.

**Independent Test**: the #507 reproducer (T005) plus the per-handler I-1 witnesses below; a conformant Heartbeat at
500 after the faulty SequenceReset draws a ResendRequest.

The anchors landed in 2d, so the cells below would be GREEN on arrival. **RED first anyway**
(`[const §VII.3–4]`; plan Constitution Check VII): before committing, each cell-writing task runs its
new cells in a scratch copy with T026 reverted, shows them RED for their stated reason, and quotes
that RED in the commit message. T065's per-arm deletion is the finer SC-006 proof on top of it.
**Baseline for every "RED/green with T026 reverted" claim:** T026 reverted with T011 present (the
stop-first scan), and T039 reverted too if it has landed. **Expected green on that baseline, each with
its own proof:** T031's D-4, single-frame D-6 and faulty-Reject(3) D-5 cells (the forced-hit mutant);
T032 (pins an inherited outcome); T034's MaxMessageSize cell (a control: the size guard runs before
the state switch); T051's well-formed Reset-mode control and its faulty D-4 control (a Reset to the
current NextNumIn is a no-op; controls by design; the D-6 and D-7 controls depend on T026); T033's
`ValidatorLive` cells (controls on fault-free frames: they prove the validation axis is live;
added 2026-09-28). Mark each as a pin or control in its test comment.

- [X] T027 [US1] Via `phase-implementer`, the I-1 witnesses in
  `tests/session/unparseable_frame_disposition_test.cpp`, each in Active at the expected N:
  - a faulty TestRequest: a Reject, **no Heartbeat** in reply, NextNumIn advances (D-5);
  - a faulty ResendRequest: a Reject, **nothing resent**; with 34 = N it advances, with 34 ≠ N it
    does not;
  - a faulty Logout: a Reject, **no Logout reply and no disconnect**;
  - a faulty Heartbeat: a Reject and an advance;
  - a faulty SequenceReset in GapFill mode: a Reject, NewSeqNo not applied (FR-010).
  Each runs in both shapes (a malformed tag → 373=0 with no 371; a malformed Length+Data pair after
  34 → 373=5 with 371 = the Length tag).
- [X] T028 [US1] Via `phase-implementer`, the SC-002 matrix in
  `tests/session/unparseable_frame_disposition_test.cpp`: in LogonReceived and in Active, a faulty
  frame with field 3 = 35 and 34 = the expected number draws **exactly one Reject and no other
  outbound message** for Reject(3), Logout(5), SequenceReset(4), Heartbeat(0), TestRequest(1),
  ResendRequest(2) and one application type. LogonReceived gets its own cells (it skips the
  Active-only block). And the SC-003 pairs: each non-Logon, non-SequenceReset faulty frame followed
  by a conformant message at N+1 delivers it with no ResendRequest; the SequenceReset pair draws a
  ResendRequest.
- [X] T029 [US1] Via `phase-implementer`, the D-6 edge cells in
  `tests/session/unparseable_frame_disposition_test.cpp`, in Active and in LogonReceived: faulty frames at too-low and too-high 34,
  each with and without PossDupFlag(43)=Y. Each draws a Reject, no advance, no ResendRequest and no
  too-low Logout. A faulty frame carrying a wrong CompID before the fault draws a Reject only, no
  disconnect (clarification Q2). A frame with two malformed fields reports the first one's reason.
- [X] T030 [US1] Via `phase-implementer`, the no-reject-loop supersession (FR-003, FR-014, C-2).
  - Rewrite `include/fixpp/session/admin_messages.hpp`'s sentence "a malformed Reject/Logout is
    never itself rejected (I-5)" to the scoped rule (it holds for a well-formed frame that fails
    validation or SendingTime; a faulty frame is Rejected), with a header comment naming the
    2026-09-27 ruling that supersedes it.
  - In `tests/session/session_reject_test.cpp` (bucket `session_pure_tests`): a malformed-Reject cell
    beside `NoRejectLoopOnInboundReject` that asserts the Reject; both existing cells stay.
  - The reject-loop bound cell in `tests/session/unparseable_frame_disposition_test.cpp`: a scripted
    peer answers each fixpp Reject with a malformed Reject; the number of fixpp Rejects equals the
    number of malformed frames the peer sent, and fixpp originates none in reply to a well-formed
    Reject.
- [X] T031 [US1] Via `phase-implementer`, the liveness cells (FR-018) in
  `tests/session/unparseable_frame_disposition_test.cpp`. In Active, within one heartbeat interval,
  send **exactly one** faulty frame of one row and nothing else: one cell each for D-4, D-5 (a faulty
  application frame, and a faulty Reject(3)), D-6 (one too-high frame; a second one while
  AwaitingResend, or a too-low one, disconnects through Arm B and is T029's, not a liveness cell) and
  D-7. In every cell the fault sits **after** 34, 35, 49, 52 and 56, so Guards 2 and 3 cannot decide
  the cell (for D-7 the fault sits before 34 but after 35, 49, 52 and 56). Each asserts the session is
  still Active up to the interval, and then that a TestRequest **is** sent at it. One cell then
  answers with a well-formed Heartbeat and asserts the session stays Active; one sends nothing more
  and asserts the grace-window disconnect.
  - **RED on the baseline (T026 reverted, T011 present):** the D-5 application cell (the frame passes
    Guard 4 and refreshes, so no TestRequest is sent); the D-7 cell (34 is empty, so Guard 4's
    `seq == 0` check disconnects it before the interval).
  - **Green on the baseline:** the D-4 cell, the single-frame D-6 cell and the faulty-Reject(3) D-5 cell
    (each returns before the Active refresh). Their liveness proof, and the D-7 cell's, is the forced-hit
    mutant in a scratch copy: a disposer that writes `last_inbound_steady_` in each row turns each cell
    RED. Record it in the evidence file.
- [X] T032 [US1] Via `phase-implementer`, the D-4 knob-off arm in
  `tests/session/test_validation_compat_toggles.cpp` (target `session_validation_compat_toggles`,
  already hooked; label from T007): with `validate_sequence_numbers` off, a faulty **Reset-mode** SequenceReset at N
  leaves `seqnum_mgr_test_access().next_inbound_unsafe() == N`, and later frames are delivered
  without advancing (the inherited outcome, research R-4).
- [X] T032a [US1] **Added 2026-09-28 (Phase 3 review, I-6/FR-011's `validate_sequence_numbers` axis).**
  Via `phase-implementer`, in the same file and target: with `validate_sequence_numbers` off, the
  same faulty Reset-mode SequenceReset at N draws exactly one Reject (45=N, 372=4, and 373/371/58
  by fault kind). T032 pins only the counter, so no cell asserted the knob-off Reject. It is RED on
  the T026-reverted baseline (no Reject is sent there).
- [X] T033 [US1] Via `phase-implementer`, the FR-011 / I-6 matrix for the admin rows in
  `tests/session/unparseable_frame_disposition_test.cpp`: T005's SequenceReset cell and the D-5
  TestRequest cell, repeated with inbound validation on and off, on FIX.4.2, FIX.4.4 and FIXT.1.1,
  as acceptor and initiator, and with and without an Application registered. The disposition is
  identical in every cell. Also one cell per C-2 row (D-1 … D-9, the application D-5 included) with
  inbound validation on and off, since the fault branch sits in front of the validate gate (C-3
  I-6).

**Checkpoint**: US1 complete: no admin handler acts on a faulty frame; every Reject is exact.

---

## Phase 4: User Story 2 — A malformed application message is rejected, not silently lost (Priority: P1)

**Goal**: a faulty application message draws a Reject that identifies it, is never delivered, and is
accounted under #423's rows; a late parse failure closes the session.

**Independent Test**: a faulty NewOrderSingle at N, then a conformant one at N+1: the first draws a
Reject (45=N) and is not delivered; the second is delivered with no ResendRequest.

- [X] T034 [US2] Via `phase-implementer`, the application cells in
  `tests/session/unparseable_frame_disposition_test.cpp` (both shapes each):
  - at N: `fromApp` not invoked, Reject 45=N, NextNumIn N+1;
  - above N: Reject, NextNumIn stays N, not delivered (FR-005);
  - I-2: the faulty frame is never persisted as received without a Reject;
  - the duplicate cells (I-3, E-1): `34=99|35=D|34=2|9x9=1` at expected 2 is D-6 with 45=99; a
    fault-free `34=1|…|34=5` NewOrderSingle at expected 5 is delivered as today; a faulty
    `…|35=D|34=N|35=4|9x9=1|…` at N is D-5 decided on `fault_ref_msg_type` (D): NextNumIn advances,
    372=D, and N+1 is delivered with no ResendRequest (R3-001);
  - AwaitingResend: a faulty in-sequence application message that fills the gap closes it (D-5
    through `consume_rejected_seqnum_`);
  - MaxMessageSize (C-1 step 1b): an oversized faulty frame in Active ends in Disconnected;
  - the 372 bound (R-5): a faulty frame at N whose MsgType is long enough to overflow the Reject's
    512-byte buffer without the bound draws a Reject **without** 372, and NextNumIn advances only
    with that Reject sent. Mutant in a scratch copy: the disposer passes the unbounded 372 → the
    cell RED (the number consumed, no Reject). Record it in the evidence file.
- [X] T035 [US2] Via `phase-implementer`, the D-5 persistence cases (FR-013, R3-003) in
  `tests/session/test_persistent_seqnum_hydrate.cpp` (target `session_persistent_seqnum_hydrate`,
  already hooked; label from T007). Add a `092 disposer (D-5)` case, labelled by `Case::site` like
  its siblings, to both tables: a faulty application frame at seq 2 (field 3 is 35, a malformed tag
  after 34).
  - `PersistentSeqnumHydrate.RejectedInSequence_AdvanceIsPersisted`: the session stays Active, a
    Reject is sent, and `FaultStore::durable_inbound` is 3.
  - `PersistentSeqnumHydrate.RejectedInSequence_PersistFailure_Fatal`: `fsm_state::Disconnected`,
    `FaultStore::durable_inbound` stays 2, and `fix->capture.frames` does not grow (no Reject).
- [X] T036 [US2] Mutants for T035, each in a scratch copy (quickstart §1 "D-5 persistence"): a
  disposer that skips `consume_rejected_seqnum_` → `RejectedInSequence_AdvanceIsPersisted` RED; one
  that emits the Reject before it → `RejectedInSequence_PersistFailure_Fatal` RED; one that discards
  its error → `RejectedInSequence_PersistFailure_Fatal` RED. Record each in the evidence file.
- [X] T037 [US2] Via `phase-implementer`, the D-5 knob-off counter arm in
  `tests/session/test_validation_compat_toggles.cpp` (target `session_validation_compat_toggles`):
  with `validate_sequence_numbers` off, a faulty application message at N, then a conformant one at
  N+1; `seqnum_mgr_test_access().next_inbound_unsafe()` reads N+1 after the first and N+2 after the
  second (delivery alone cannot witness the advance with the knob off). The same arm for a faulty
  Reject(3) and a faulty Logout(5). Show it RED in a scratch copy when the disposer skips
  `consume_rejected_seqnum_`.
- [X] T038 [US2] Via `phase-implementer`, write one RED cell per late inbound site in T008's C-6
  population, in `tests/session/unparseable_frame_disposition_test.cpp` (hook-free), each with a
  **real well-formed frame above T006's measured ceiling** (no test hook) and a persistent store that
  survives the close (a `FileStore` over a temporary directory):
  - the session closes terminally; no Reject is sent; the parse target's receive callback
    (`fromAdmin` for an admin frame, `fromApp` for an application frame) is not invoked. `onLogout`
    from the close and callbacks already fired at the site (e.g. `toAdmin` for the confirming
    Logout) are not counted (R3-005). No cell asserts that an effect taken before the site's parse
    is absent;
  - it then reconnects over the same store and asserts whether NextNumIn includes the closed-on
    frame (the peer's resend is requested, or the number was consumed), pinning today's per-site
    outcome (R3-004, C-5 L-6).
  RED today: the frame is consumed, or the session continues.
  - The above-ceiling trigger needs a null arena upstream. On MSVC debug `arena_upstream()` returns
    `new_delete_resource()`, the arena spills and the frame parses. Guard each cell at runtime on the
    same condition the library uses, `fixpp::detail::arena_upstream() ==
    std::pmr::null_memory_resource()` (`include/fixpp/core/pmr_arena_upstream.hpp`), with
    `GTEST_SKIP()` and a message naming the reason; never a hand-written platform check. If T006
    found an offset-table-cap trigger (`wire_offset_table_full`) that fails the parse on every
    platform, use it instead and drop the guard.
- [X] T039 [US2] Via `phase-implementer`, implement the late-site close (E-3, C-6) in
  `src/session/session.cpp`.
  - `parse_and_dispatch_` gains a private way to say "the parse failed and the receive callback did
    not run" (a private result enum, or a private sentinel `core::error` that never leaves the
    session). `validate_inbound_` gains a third outcome, "parse failed", beside `nullopt` and a
    `RejectDecision`.
  - Every late inbound call site in T008's population treats "parse failed" as terminal:
    `close(close_mode::terminal)`, no Reject, the receive callback not invoked, return. Where the
    call's result is not bound today, it is bound. One action, no per-site table. The sites that
    parse a frame fixpp built are unchanged.
  - Correct `parse_and_dispatch_`'s "skip, not fatal" and `validate_inbound_`'s "validation passes"
    comments (R-13).
  - T038 GREEN.
- [X] T040 [US2] For each T038 cell, delete that site's close in a scratch copy and show the cell RED
  (SC-008). Record each site and its RED in the evidence file.
- [X] T041 [US2] Via `phase-implementer`, the SC-007 scripted peer in
  `tests/session/unparseable_frame_disposition_test.cpp` (quickstart §2 "Scripted peer"). The
  in-process peer:
  - sends one raw malformed application frame at a too-high MsgSeqNum, so a gap forms;
  - handles fixpp's Reject as QuickFIX J/C++'s `nextReject` does (cite the source file and function
    in a comment, by symbol, not by line);
  - answers each ResendRequest by replaying its stored bytes verbatim with PossDupFlag(43)=Y and
    OrigSendingTime(122) added.
  Assert that the resend converges and every later message is delivered (no B-423-1 stall).
- [X] T042 [US2] File the SC-007 live-QuickFIX interop cell as a follow-up issue placed in B17
  (spec Assumptions), with the owner's approval before filing; record its number in the evidence
  file and in the parent's `phases/phase-4/issue-batches.md` B17 heading.

**Checkpoint**: US2 complete: no application message is lost silently; a late parse failure closes.

---

## Phase 5: User Story 3 — A malformed Logon is refused (Priority: P2)

**Goal**: a Logon with a malformed tag is refused as a malformed count is, in every role and
profile, observable through the C ABI.

**Independent Test**: a Logon with a malformed **tag**, as acceptor and as initiator: the session
ends Disconnected with no Reject and no Logon reply.

- [X] T043 [US3] Via `phase-implementer`, the D-1/D-2 matrix in
  `tests/session/unparseable_frame_disposition_test.cpp`: a Logon with a malformed tag, as acceptor
  and as initiator (reply Logon), on FIX.4.2, FIX.4.4 and FIXT.1.1 (SC-004). Plus a faulty
  non-Logon in NotConnected and in LogonSent, and a D-8-shaped frame (field 3 not 35) in both: each
  refused, the session ends (the pre-Active disregard is fixpp#514).
- [X] T044 [US3] Via `phase-implementer`, the D-3 cells in
  `tests/session/unparseable_frame_disposition_test.cpp`: a faulty Logon (field 3 = 35, 34 read) in
  Active and in LogonReceived ends in a silent Disconnected, with no Reject and no Logout.
- [X] T045 [US3] Run T021's `capi_logon_malformed_tag` and `capi_logon_malformed_count`: both GREEN
  on every observer, both roles. Record the observer outcomes against T020's classification in the
  evidence file.

**Checkpoint**: US3 complete: the C-ABI 1.10 declaration has its witness GREEN.

---

## Phase 6: User Story 4 — A frame whose MsgSeqNum or MsgType position cannot be trusted is disregarded (Priority: P2)

**Goal**: in LogonReceived and Active, a faulty frame whose field 3 is not 35 or whose 34 was not
read draws nothing; the next valid message exposes the gap. LogoutSent disregards any faulty frame.

**Independent Test**: a frame whose malformed field precedes MsgSeqNum, then a conformant message at
N+1: the first draws nothing; the second draws a ResendRequest; the session stays connected.

- [X] T046 [US4] Via `phase-implementer`, the D-7 cells in
  `tests/session/unparseable_frame_disposition_test.cpp`, in Active and in LogonReceived: a malformed
  Length+Data pair before 34 (e.g. SecureDataLen(90)/SecureData(91)); a malformed tag before 34;
  `34=abc` and `34=0` before the fault. Each: no outbound, NextNumIn unchanged, still connected, and
  the next valid message at N+1 draws a ResendRequest (SC-005).
- [X] T047 [US4] Via `phase-implementer`, the D-8 cells in the same file, in Active and in
  LogonReceived: the mixed defect `8|9|49=…|35=D|34=N|9x9=1|…|10`, and a frame whose field 3 is the
  malformed field. Same assertions as T046.
- [X] T048 [US4] Via `phase-implementer`, the D-9 cells in the same file: in LogoutSent a faulty
  Logout is not taken as the Logout reply and the logout timeout ends the session (FR-015); a faulty
  non-Logout draws nothing.
- [X] T049 [US4] Via `phase-implementer`, the disclosed-outcome cells (C-5) in the same file:
  - **L-1:** a replaying peer and a deterministically malformed frame faulty before 34; pin the
    observed resend loop (bounded by the cell's own iteration cap, asserting the loop shape, not a
    count in a comment).
  - **L-2:** a faulty GapFill at N during AwaitingResend; pin the disconnect sequence of C-5 L-2.
  - **L-4:** a malformed SignatureLength(93)/Signature(89) pair draws 373=5 with 371=93.

**Checkpoint**: every C-2 row and every C-5 disclosure has its cells.

---

## Phase 7: Cross-cutting — the inbound seqnum_max bound (FR-019, R-14, C-3 I-7, C-5 L-7)

**Purpose**: NextNumIn never wraps. Not a user story: the owner folded it into 092 (Gate A loop 2,
round 2). The D-5 cell needs Phase 2d.

- [X] T050 [P] Via `phase-implementer`, the unit cells in `tests/session/seqnum_manager_test.cpp`
  (target `session_store_tests`, label from T007): `set_next_inbound(seqnum_max)` succeeds; then
  `check_inbound(seqnum_max)` returns `store_seqnum_overflow` and `next_inbound_unsafe()` stays
  `seqnum_max`. RED today (it wraps to 0).
- [X] T051 [P] Via `phase-implementer`, the memory-store session cells in
  `tests/session/test_validation_compat_toggles.cpp` (target `session_validation_compat_toggles`).
  Each sends a SequenceReset (Reset mode, 36=4294967295), asserts Active and
  `next_inbound_unsafe() == 4294967295` (so no other disconnect cause can satisfy the cell), then
  sends one frame at 34=4294967295 **that would consume NextNumIn** (L2R3-001) and asserts:
  `fsm_state::Disconnected`; `next_inbound_unsafe()` still 4294967295, never 0; no outbound frame
  after it (no Reject); no `fromApp`/`fromAdmin`. The frame is, in turn:
  - an application message;
  - a Heartbeat;
  - a PossDupFlag=Y admin frame with a valid OrigSendingTime(122);
  - a #423 Reject site: a PossDupFlag=Y application frame with no OrigSendingTime(122), which Arm C
    Rejects through `consume_rejected_seqnum_`;
  - a faulty application frame (D-5).
  Plus:
  - the knob-off arm: with `validate_sequence_numbers` off, seed with
    `seqnum_mgr_test_access().set_counters_for_test(4294967295, peek_outbound())`, then an
    application message at 4294967295; same assertions;
  - the pre-Active arm: after `open()` and before the Logon, seed the same way, then a Logon at
    34=4294967295, as acceptor and as the initiator's Logon reply where the fixture drives one;
    assert Disconnected and `next_inbound_unsafe()` still 4294967295;
  - **non-consuming controls** (L2R3-001): at NextNumIn = 4294967295, each of these leaves the
    session Active and NextNumIn at 4294967295, because none consumes it: a Reset-mode
    SequenceReset with NewSeqNo = 4294967295; a faulty Reset-mode SequenceReset at 4294967295 with NewSeqNo =
    4294967295 (D-4); a faulty
    frame at another number (D-6); a D-7 frame. (A faulty Logon is D-3, which disconnects by design,
    so it is not a control.)
  RED today for the Guard 4, #423 and pre-Active cells (the counter wraps and the session stays
  Active or establishes). The D-5 cell's RED is T054's.
- [X] T052 [P] Via `phase-implementer`, the FileStore cells in
  `tests/session/test_store_fail_reconcile.cpp` (target `store_fail_reconcile`, already hooked; label
  from T007), whose fixture drives a Session over a `FileStoreFactory`: the five frames of T051 with
  the same assertions, plus the durable counter, read (L2R3-005) by reopening a `FileStore` over the
  fixture's `dir_` after the session closes and calling `next_seqnum(inbound, false)`: the frame did
  not move it.
- [X] T053 Via `phase-implementer`, implement the bound.
  - `src/session/seqnum_manager.cpp`: in `check_inbound`'s in-sequence branch, before
    `++next_inbound_`, return `core::error::store_seqnum_overflow` when `next_inbound_ ==
    seqnum_max`, leaving the counter unchanged. Rewrite the sentence that counts the callers as the
    re-derivation command, with no count (R-14).
  - `include/fixpp/session/seqnum_manager.hpp`: **replace** the comment "Caller is responsible for
    the session-fatal disposition (emitting Logout-with-text + disconnect) on any unexpected return"
    with per-result text: too-low and too-high keep their context-dependent handling at the caller;
    `store_seqnum_overflow` requires FR-019's silent Disconnected. Name 092/FR-019 in the header
    comment (L2R3-002).
  - `src/session/session.cpp`, `consume_rejected_seqnum_`: on `store_seqnum_overflow`, take
    `record_state_transition_(fsm_state::Disconnected)` and return the error. Its callers already
    `co_return` a failed result before their Reject (confirm against T008's R-14 population).
  - `src/session/session.cpp`, Guard 4 in LogonReceived/Active: in `check_inbound`'s error branch,
    test the overflow **first**, before the Heartbeat, PossDup and knob-off branches; take the
    Disconnected transition and return `store_seqnum_overflow` from `on_inbound_frame`, as
    `consume_rejected_seqnum_`'s branch and the outbound I-8 callers do (L2R3-004).
  - Confirm by reading, at the implementation head, that the acceptor Logon arm and the initiator
    Logon-reply arm end the session on the new error with no edit. Classify every member of T008's
    `check_inbound(` population the same way, in the evidence file.
  - T050, T051 and T052 GREEN.
- [X] T054 The three deletion proofs (quickstart §2 "Inbound bound deletion"), each in a scratch copy:
  - delete the bound in `check_inbound`: every **consuming** FR-019 cell goes RED, the pre-Active
    arm included (the non-consuming controls stay green by design);
  - delete Guard 4's overflow branch: the Heartbeat, PossDup and knob-off cells go RED (the plain
    application cell stays green through the too-low fatal arm, so it is not that branch's witness);
  - revert `consume_rejected_seqnum_`'s overflow branch: the #423 and D-5 cells go RED (a Reject is
    sent and the session stays Active).
  Record each in the evidence file.

---

## Phase 8: Cross-cutting — the validator reports field faults (FR-012, R-7, E-4…E-6)

**Purpose**: `dictionary_driven_validator::validate` never reports "conformant" for a walk that met
an encoding fault. Not a user story: the session cannot reach it (2d decides first); the owner kept
it in 092.

- [X] T055 [P] Via `phase-implementer`, the iterator cells (RED) in a new
  `tests/wire/field_iterator_fault_test.cpp`, added to the `wire_dict_tests` bucket in
  `tests/wire/CMakeLists.txt` (label from T007). One cell per E-4 row S1–S4 and T1–T3 over a bare
  buffer, each asserting `fault()`, `fault_length_tag()` and that the **yielded sequence equals the
  pre-092 sequence**, under the standard, dictionary-only and `none()` hooks. S0 with nothing earlier
  asserts `none`.
- [X] T056 Via `phase-implementer`, implement E-4 in `include/fixpp/wire/parser.hpp`:
  `MessageView<Mode>::field_iterator` gains `[[nodiscard]] field_fault fault() const noexcept`
  ("sticky: the first fault any advance() observed") and
  `[[nodiscard]] std::uint16_t fault_length_tag() const noexcept` ("the Length tag when fault() ==
  length_data_mismatch; else 0"), plus the private members to hold them and the tag of the Length
  that armed the carry (`prev_data_tag_` holds the Data tag, not the Length tag). **What the
  iterator yields does not change** (`scan_slice_for_tag` in `src/capi/message_read.cpp` walks group
  slices with it). Derive S0–S4/T1–T3 by reading every `done_ = true` and early `return` in
  `advance()` at the implementation head. T055 GREEN.
- [X] T057 Via `phase-implementer`, the validator cells (RED) in
  `tests/wire/field_iterator_fault_test.cpp`:
  - malformed tag: construct `MessageView<Index>` directly, assert its failed `build_status()`, then
    `validate` returns `wire_invalid_tag_number` and `*ref_tag_out` is untouched;
  - Length/Data: build under one hook set, validate under a differing set; `validate` returns
    `wire_length_data_mismatch` and `*ref_tag_out` = the Length tag;
  - T1: an empty tag returns `wire_invalid_tag_number`, **not** `wire_unexpected_tag`;
  - clean controls: well-formed messages whose view uses the validator's own hooks, one with a
    standard Length+Data pair and one with a dictionary-only pair; the result is unchanged.
- [X] T058 Via `phase-implementer`, the new errors (E-6) in `include/fixpp/core/error.hpp`:
  `wire_invalid_tag_number = 132` and `wire_length_data_mismatch = 133`, appended at the next
  contiguous slots after `app_payload_malformed = 131` with explicit values (`[const §X.4]`), each
  with an `error_message` entry.
- [X] T059 Via `phase-implementer`, move every pin in T008's R-7 population with them:
  - `src/capi/error.cpp` `translate`: both → `FIXPP_ERR_WIRE_INVALID_FRAME`;
  - `include/fixpp/wire/reject_reason_map.hpp`: 132 → 0, 133 → 5;
  - `tests/capi/error_surface_test.cpp`: `kEnumTable` rows, its size pin, the CSV row-count
    assertion; `tests/capi/expected_error_map.csv`: two rows;
  - the "slot 132 is unknown" boundary pins in `tests/core/test_017_error_completeness.cpp`,
    `test_019_error_completeness.cpp`, `test_020_error_completeness.cpp` and
    `tests/capi/abi_symbol_golden_test.cpp`, each moved to the slot after the 092 block and expressed
    from the last 092 enumerator, not as a literal;
  - comments that state a count (`error.cpp`'s arm count, `reject_reason_map.hpp`'s "five
    validator-emitted slots") rewritten to state the condition, with no number;
  - `include/fix/c_api/error.h`, only if `grep -n "WIRE_INVALID_FRAME" include/fix/c_api/error.h`
    shows it lists the core errors that code coalesces; then re-pin `tools/capi_freeze.sha256`.
  - Any other member the commands add, classified in the evidence file.
- [X] T060 Via `phase-implementer`, add `tests/core/test_092_error_completeness.cpp`, modelled on
  `test_020_error_completeness.cpp`: the 092 block is exactly `{wire_invalid_tag_number,
  wire_length_data_mismatch}` at `{132, 133}`. Register `error_092_completeness` in
  `tests/core/CMakeLists.txt` with label `092`. Add a `reject_reason_map` cell per new enumerator and
  a `translate` cell per new enumerator in the existing sources that pin them.
- [X] T061 Via `phase-implementer`, the over-0xFFFF mapping cell (E-6, R2-008) in
  `tests/capi/error_surface_test.cpp`: `translate` gives `FIXPP_ERR_WIRE_LIMIT_EXCEEDED` for
  `OffsetTable::build`'s `wire_tag_out_of_range` and `FIXPP_ERR_WIRE_INVALID_FRAME` for the
  validator's `wire_invalid_tag_number` on the same bytes.
- [X] T062 Via `phase-implementer`, implement E-5 in the validator
  (`include/fixpp/wire/validator.hpp` and its source): hoist the Step 1 iterator out of the `for`
  init; check `it.fault()` at the **top of each iteration** (before the field checks, so T1's tag 0
  cannot surface as 373=2) and once **after the loop**; `malformed_tag` → return
  `wire_invalid_tag_number`, `*ref_tag_out` untouched; `length_data_mismatch` → write
  `it.fault_length_tag()` to `*ref_tag_out` and return `wire_length_data_mismatch`. Steps 2 onward do
  not run after a fault. T057 GREEN.
- [X] T062a **Added 2026-09-28 (owner ruling, FR-012 on a failed-build view).** Via `phase-implementer`:
  when the `MessageView` passed to `dictionary_driven_validator::validate` has a failed build, `validate`
  first walks the field iterator to its fault and returns FR-012's code (`wire_invalid_tag_number`
  with RefTagID untouched, or `wire_length_data_mismatch` with RefTagID = the Length tag), before
  Step 1. The fault-free path adds one branch. RED first: a cell with an **ordinary** dictionary
  (not a framing-tag builder) and a directly constructed failed-build view with a malformed tag
  after tag 8. Today it returns `wire_unexpected_tag` (RefTagID 8). A second cell does the same for
  a Length+Data mismatch. Deletion proof in a scratch copy: remove the pre-scan and both cells go
  RED. Record it in the evidence file.
- [X] T062b **Added 2026-09-28 (T062a follow-up).** T062a's pre-scan catches every failed-build view
  first, so E-5's in-walk checks lost their witnesses. The top-of-iteration check has none, and
  the after-loop check keeps only S4. Via `phase-implementer`, add validator cells on the path the
  in-walk checks still guard: a **successful** build under hooks that pair a Length/Data (so a
  counted value swallows a malformed field), validated by a dictionary that does not pair them.
  One cell each for T1 (`=x` inside the counted value), S1, S2 and S3 where constructible; say
  why for any that is not. Then re-run T063's deletion table at HEAD, with predictions written
  first. It must show a witness for the top-of-iteration check, the after-loop check, and each
  iterator fault write. That table supersedes the Phase 8 one.
- [X] T063 In a scratch copy: delete the top-of-iteration check (T1 cell RED), then the after-loop
  check (T2/T3 cells RED), then one iterator fault write per row (its E-4 cell RED). Record each in
  the evidence file.
  **Corrected at implementation (2026-09-28):** deleting the after-loop check turns the S1–S4
  validator cells RED, not T2/T3. T2 and T3 yield their faulted field, so the top-of-iteration check
  catches them. The "both checks" arm and the T2/T3 write deletions are those cells' witnesses.
  Evidence file §Phase 8.

---

## Phase 9: Polish & cross-cutting

### Simplify (before any measurement)

- [X] T064 Run `/simplify` over the branch diff (`[const §XVI.7]`); fixes go through
  `phase-implementer`. Every later check runs on the post-simplify head.

### Mechanism deletion (SC-006)

- [ ] T065 In a scratch copy, delete the inline fault branch in **one state arm at a time** (four
  arms) and show that arm's cells RED: the anchors of T025 and the story cells of T027–T034,
  T043–T048. Then repeat each deletion with the late-site close (T039) **also** deleted, so a refusal
  cell kept green by a late-site close is exposed. Also delete D-5's `consume_rejected_seqnum_` call
  (SC-003 advance cells RED) and the NewSeqNo guard of D-4 (T005 RED). Record every deletion and its
  RED cells in the evidence file. A cell that stays GREEN under every deletion that should reach it
  is a finding, not a pass.

### Fuzz (`[const §VII.7]`)

- [ ] T066 Via `phase-implementer`, the R-2 arm in `tests/fuzz/fuzz_session_recovery_admin_parse.cpp`:
  on each input, call `scan_frame_header` and `OffsetTable::build` directly, under both hook sets,
  with a dictionary that declares a dictionary-only pair. `__builtin_trap` on any encoding
  disagreement, and for a fault-free input on a `fault_ref_seq_num`, `msg_type_is_third` or
  `fault_ref_msg_type` disagreement with `entries()`. Skip **only** inputs whose status is
  `wire_offset_table_full` or `out_of_memory`, and count them. Add seeds for both fault shapes and
  both header-position cases to its corpus directory.
- [ ] T067 Via `phase-implementer`, the R-7 arm in `tests/fuzz/fuzz_wire_validator.cpp`: a
  whole-buffer iterator walk under the same hooks as the `OffsetTable` build, asserting both
  directions: an encoding failure implies `fault() != none`; a successful build implies
  `fault() == none`.
- [ ] T068 Build under `linux-clang-asan` with `FIXPP_BUILD_FUZZ=ON`. Plant one disagreement per arm
  (and per direction for T067) in a scratch copy and show each trap fires. Then run each target
  ≥ 600 s. Record the commands, the corpus, the skipped-resource-status count, and the result in the
  evidence file, and name both targets to `/speckit-verify` (T078) so they are not marked N/A. State
  the #508 caveat (no coverage feedback from library code).

### Performance (`[const §VIII.2]`)

- [X] T069 Re-run T004's paired A-B-A-B against the post-simplify candidate head, same base worktree,
  same procedure. Budget +5 % per case, min-per-tree. Over budget → the owner, with the per-leg
  figures; never relax it. Record in the evidence file §*Bench result*, then
  `git worktree remove --force /mnt/wsl/fixppbuild/092-base-wt`.

### Docs (FR-014, R-13) — orchestrator-authored markdown, `phase-implementer` for code comments

- [X] T070 [P] `spec/behaviors-and-limitations.md`: the B-092-* rows for the shipped behaviour (the
  C-2 dispositions, the Reject contents, the validator errors, C-ABI 1.10 BREAKING under
  `[const §X.7]`); the disclosures C-5 L-1, L-2, L-4, L-6 and L-7 as rows, each naming its cell; the
  #423 row 4 revision; and a note that #423 row 1 describes an "Ignore" fixpp does not do, citing
  `L-004-4` and fixpp#514. Run `/home/catalin/Work/Programming/Antreprenoriat/.claude/scripts/check_bl_delta.py` and record
  the delta in the evidence file.
- [X] T071 [P] Using T008's R-13 population for `brain/`: `brain/components/session.md`: rewrite (not append to) the passage that calls #507's
  frames "garbled … contrary to §4.5.2". `brain/components/inbound-message-path.md`, `wire.md`,
  `errors.md` and `c-api.md`: their decision sections gain the ruling, the rejected alternatives
  (ignore-by-default; 373=99; changing the iterator's yield; a per-site late Reject table), the
  1.10 declaration and the FR-019 bound. Each page's document list gains 092's bundle.
- [ ] T072 Via `phase-implementer`, every remaining code-comment site in T008's R-13 population,
  including the #423 ruling references not already rewritten in T026. Header comments name the
  superseding ruling. **Amended at Phase 3 (2026-09-28):** R-13's grep does not reach `tests/`,
  so this task also covers `tests/session/session_reject_test.cpp`'s file-header scenario 2
  ("feeding a malformed Reject does not cause another Reject"). That statement is now false for a
  faulty frame (see `SessionReject.MalformedInboundRejectIsRejected`). Re-run R-13 at HEAD over
  `include src tests` too, and treat each added hit as a planned edit. Leads from Phase 3 (a lead
  is not a population):
  - `session.cpp`: the guard-precedence block above `on_inbound_frame`, the `parse error — skip`
    comments in `parse_and_dispatch_`, and the no-reject-loop comments at the Active-arm guard and
    the inbound-Reject handler;
  - `error.hpp`: the "No reject loop (I-5)" slots;
  - `admin_messages.cpp`: the file header and the `build_reject` notes;
  - `session_fsm.hpp` ("no reject-of-reject").
  - `session.cpp`: the banner above `parse_and_dispatch_` ("extracted from 5 inbound + 1 outbound
    sites"). It is a count claim on the function T039 changed: delete the count and keep the
    re-derivation recipe (C-6's population command).
  - `include/fixpp/session/seqnum_manager.hpp` file header: "I-4: too-high is session-fatal; no
    ResendRequest; caller emits Logout+disconnect" and "I-4: no ResendRequest is emitted by 005; the
    recovery feature is deferred" are dated. T053 rewrote only the "Caller is responsible…" sentence.

### Static analysis, claims and citations

- [ ] T073 Run clang-tidy, clang-format, cppcheck and IWYU (`[const §IX.4]`) on every changed file
  under `src/` and `include/`. Never format `specs/` or `include/fix/c_api/*.h`. Any finding on a
  changed line is fixed by `phase-implementer`.
- [ ] T074 Run `python3 /home/catalin/Work/Programming/Antreprenoriat/.claude/scripts/check-comment-claims.py
  --root <tree> --base origin/main`, with `<tree>` the absolute path of the worktree that owns this
  branch. Read every hit in comments this feature authored (the `version.h` history, the BREAKING
  clauses, the `admin_messages.hpp` supersession, the `seqnum_manager.hpp`/`.cpp` comments, the
  `session.cpp` comments) and the strings the script cannot see. A claim that records a result is
  deleted, not replaced.
- [ ] T075 Run `python3 tools/check_line_citations.py --shift-audit origin/main..HEAD`. For a hit on
  the checker's own fixture strings, apply the `# citation-ok` pragma.

### Close-out checks

- [ ] T076 Via `phase-implementer` for the TIMEOUT edit: measure `session_unparseable_frame_disposition`'s wall time on every sanitizer lane
  `/speckit-verify` runs and on MSVC, and set its TIMEOUT from the slowest with headroom (the
  inherited 120 s is a threading-test default; `wire_dict_tests` needed 1800 s on msvc-asan). Then run T007's label gate; it must pass (the labelled set equals the manifest, every entry
  registered). Then `ctest --test-dir build/linux-clang-debug -L '^092$' --output-on-failure`, all
  GREEN, and the full session suite plus `pytest bindings/python/tests/`, passing except the tests
  R-7 and R-10 list as intentionally updated.
- [ ] T083 Via `phase-implementer`, FR-008's witness (no test pins it today: `grep -rln "L-004-4"
  tests` is empty, and `OverCapacityFrameClosesSession` covers carry overflow only). In
  `tests/session/engine_readpump_test.cpp` (target `engine_readpump_test`, already hooked; ctest
  `engine_readpump`), add cells that feed an **established** session a frame with a bad CheckSum, one
  whose BodyLength is **too small** (so `10=` is misaligned), and one with a malformed BeginString
  prefix (a first byte other than `8`, or no `=`: `wire_framing_resync`), and assert the session
  closes (`L-004-4`). Not a well-formed wrong version (it passes the Framer and Guard 2 closes it
  whatever the pump does) and not a too-large BodyLength (the Framer waits for more bytes).
  These are pins, green today; their proof is a mutant in a scratch copy (a read pump that continues
  after a Framer error) that turns each RED. Add `engine_readpump` to `expected-ctest-092.txt` with an
  APPEND label. Record the mutant in the evidence file.
- [ ] T077 Via the `checklist-auditor`, re-disposition any checklist item whose subject changed
  during implementation (a deviation recorded in the evidence file). Derive the population by a
  complement grep over `specs/092-garbled-frame-reject/checklists/*.md` for every FR, C-2 row and
  invariant id the evidence file records a deviation against.
- [ ] T078 Run `/speckit-verify` (mandatory after `/speckit-implement`, Article XVII §8). It produces
  `.specify/decisions/092-garbled-frame-reject-verify.md`, which cites the evidence file.
  - Its full preset matrix (ASan, UBSan, TSan, …) and the MSVC leg.
  - Coverage on `linux-clang-coverage`, `.profraw` purged first: every changed line in every file of
    `git diff --name-only origin/main...HEAD -- src include` is covered or assessed line by line
    (`[const §IX.1]`).
  - The allocation gate (mallocnesia, `[const §VIII.5]`): follow `/speckit-verify` Step 6, with
    `mallocnesia_positive_control` passing; for each `*_mallocnesia` twin the branch touches, record
    evidence the interceptor **took effect** (a line the override writes, or a planted allocation it
    catches).
  - The fuzz targets named in T068.
  - ⚠️ The §7 full build needs an owner ASK, even as gate evidence.
- [ ] T079 **`CLAUDE-history.md` entry** (Article XIX), via `phase-implementer`: a newest-first 092
  entry naming the feature, the PR, `Closes #507`, the owner ruling (revised #423 row 4), C-ABI 1.10
  BREAKING, FR-012's validator errors, FR-019's inbound bound, and the follow-ups fixpp#514, fixpp#515
  and T042's issue. Update `CLAUDE.md`'s "Last merged FEATURE" pointer only at merge.
- [ ] T080 **PR description.** The body carries:
  - the `[const §X.7]` **C-ABI 1.10 BREAKING** declaration: T020's population and classification,
    pointing at the B&L delta;
  - the public C++ deltas (contract C-4): `build_reject_with_text`, `field_fault`, the iterator
    accessors, errors 132/133, `check_inbound`'s new return;
  - the bench result (T069) and the fuzz result (T068);
  - `local build: green on linux-clang-debug @ <git-sha>` (`[const §XVII.7]`), with the SHA T078
    verified;
  - a `## Gates` section citing `decisions/speckit/092-garbled-frame-reject-gatea.md` and the verify
    record, and a `## Gate B …` heading for the Gate B record;
  - `Closes #507` as the **only** closing keyword. #514 and #515 are named as follow-ups in
    sentences with no closing keyword anywhere near them (a closing keyword fires inside a
    negation). Before opening, run over the body AND `git log origin/main..HEAD --format=%B`:
    `grep -inE '\b(close[sd]?|fix(e[sd])?|resolve[sd]?)\b[: ]+([a-z0-9_.-]+/[a-z0-9_.-]+)?#[0-9]+' | grep -v '#507\b'`,
    which must print nothing. Positive control first: seed `Closes #514` into a scratch copy of the
    body and show the grep hits it. After opening, check
    `closingIssuesReferences` is exactly [#507].

### Mandatory close-out tasks (Gate-B preconditions, Article XVII §8)

- [ ] T081 [P] **Catalogue close-out.** Flip every feature-owned OFFICIAL row in
  `spec/feature-catalogue.md` to `done` with this PR as evidence, including the CA rows matched by
  091's rule for the 1.10 note and any session-recovery row whose behaviour 092 changes; add or
  update the matching `spec/coverage-index.md` entries, naming this feature's witnesses (T005, the
  T025 anchors, the T035 persistence cases, T045, T051/T052).
- [ ] T082 **Feature-completeness audit (the FINAL task).** Assert against the merged tree:
  - (i) every `tasks.md` row is `[X]` or carries an explicit waiver rationale;
  - (ii) every FR-001…FR-019 and SC-001…SC-010 maps to a landed test AND a landed implementation;
  - (iii) every feature-owned OFFICIAL catalogue row is `done`, with a matching `coverage-index.md`
    entry.
  Record the verdict (100 % or fully waived) in `.specify/decisions/092-garbled-frame-reject-verify.md`
  `## Completeness`, or in `.specify/decisions/092-garbled-frame-reject-completeness.md`. `/gate-b`
  pre-flight 4d hard-blocks without it.

---

## Dependencies & execution order

### Phase dependencies

- **Setup (Phase 1):** T001 → T002 → T003 → T004 must precede every production edit. T005 needs
  only T001. T006 needs only T001. T005 → T007 → T008 (T007's positive control assumes T005's
  target is registered; T008 may extend the manifest, so the control is re-stated after it).
- **Foundational (Phase 2):** depends on Setup.
  - 2a: T009 (RED) → T010 → T011 → T012; T013 after T011 → T014; T015 after T011 (the stop-first
    scan is what makes replay unsafe).
  - 2b: T016 (RED) → T017 → T018. Independent of 2a.
  - 2c: T019 → T020 → T021 ‖ T022 (RED) → T023 → T024. T021 goes GREEN at T026. Independent of 2a
    and 2b until T026.
  - 2d: T025 (RED) needs T011 and T017 (it asserts the Text-carrying Reject) → T026 needs 2a, 2b
    and T023.
  - **Blocks all stories and Phase 7's D-5 cell.**
- **US1 (Phase 3), US2 (Phase 4), US3 (Phase 5), US4 (Phase 6):** each needs the Foundational
  checkpoint only. They touch the same test file, so run their cells sequentially within it; T032
  and T037 share `test_validation_compat_toggles.cpp` with T051.
  - US2: T038 (RED) needs T006 → T039 → T040. T035 → T036. T041 after T039.
- **Phase 7 (FR-019):** T050 ‖ T051 ‖ T052 (RED) → T053 → T054. Needs Phase 2 (the D-5 cell).
  Independent of the stories otherwise.
- **Phase 8 (FR-012):** T055 (RED) → T056; T057 (RED) needs T056; T060 and T061 are written
  **before** T058 and committed with their compile-RED; then T058 → T059 → T062 → T063 (T060 and T061's `translate` half GREEN at T059; T061's validator half GREEN at T062). Needs only T010 (the shared enum). It can run beside every story.
- **Polish (Phase 9):** needs every phase above.
  - T064 first; then T065, T066 → T067 → T068, T069;
  - T070–T072 after T064 (they describe the final code);
  - T073–T075 after T072;
  - T083 (FR-008 pin, listed before T077) after T064; T076 after T073–T075 and T083; T077 before T078; T078 after T076;
  - T079, T080 after T078;
  - T081 → T082. T082 is last.

### User story dependencies

- **US1 (P1, MVP):** Foundational only.
- **US2 (P1):** Foundational only. Its late-site close (T039) is its own mechanism.
- **US3 (P2):** Foundational only (the C-ABI witnesses went GREEN at T026).
- **US4 (P2):** Foundational only.

### Within each story

The anchors in 2d are written first and shown RED. Each later story cell is shown RED in a scratch
copy with T026 reverted before it is committed, and T065's per-arm deletion adds the SC-006 proof. A mechanism a story introduces itself (US2's late-site close;
Phase 7's bound; Phase 8's validator check) follows RED → implement → GREEN → mutant.

---

## Parallel opportunities

- **Setup:** after T001, T006 runs beside T005 → T007 → T008 (T005 and T007 both edit
  `tests/session/CMakeLists.txt`; T008 may extend T007's manifest, so T007's positive control is
  re-stated after T008; T006 writes nothing committed).
- **Foundational:** 2a's T009 ‖ 2b's T016 ‖ 2c's T021/T022 (different files). T013 shares T009's
  file, so it runs after it.
- **Phase 7 ‖ Phase 8 ‖ the stories**, after the checkpoint: Phase 8 touches only wire, core and
  capi error files; Phase 7 touches `seqnum_manager.*`, one `session.cpp` region and three hooked
  test files.
- **Polish:** T070 ‖ T071 (markdown, orchestrator).

```text
# Foundational, RED witnesses together:
phase-implementer: T009 scan fault cells   → tests/session/scan_frame_header_fault_test.cpp
phase-implementer: T016 builder cells      → tests/session/session_reject_test.cpp
phase-implementer: T021 C-ABI witnesses    → tests/capi/length_data_logon_refusal_test.cpp
phase-implementer: T022 version pin        → tests/capi/version_test.cpp

# After the checkpoint, the three independent mechanisms together:
phase-implementer: T038 late-site cells    → tests/session/unparseable_frame_disposition_test.cpp
phase-implementer: T050–T052 FR-019 cells  → seqnum_manager_test.cpp, test_validation_compat_toggles.cpp, test_store_fail_reconcile.cpp
phase-implementer: T055 iterator cells     → tests/wire/field_iterator_fault_test.cpp
```

---

## Implementation strategy

### MVP (US1)

1. Setup (T001–T008): benches baselined, the #507 reproducer (T005) RED, the ceiling measured, the populations recorded.
2. Foundational (T009–T026): the scan fault record, the replay guard, the Text-carrying Reject,
   C-ABI 1.10, and the disposition with one GREEN anchor per C-2 row.
3. US1 (T027–T033).
4. **Stop and validate:** the #507 reproducer (T005) is flipped (SC-001), no admin handler acts on a faulty frame, every
   Reject is exact.

### Incremental delivery

US2 (the application rows and the late-site close), US3 (the Logon matrix and the C-ABI observers)
and US4 (the disregard rows and disclosures) follow independently. Phase 7 (FR-019) and Phase 8
(FR-012) can run beside them. One PR carries all of it: C-ABI 1.10 ships with the Logon refusal.

### Next pipeline steps (before `/speckit-implement`)

- `/speckit-analyze`: mandatory (`[const §XVI.3–4]`; a `[const §X.6]` control for the BREAKING
  C-ABI change).
- `/speckit-checklist`, then the checklist audit (`/speckit-checklist-audit`, pipeline step 9), which
  blocks implement.

---

## Notes

- **Gate A P3s folded in (L2R3-001…005).** L2R3-001: spec Key Entity, SC-010 and C-5 L-7 now say "the
  next message that would consume NextNumIn"; T051 adds the non-consuming controls. L2R3-002: R-14
  replaces the `seqnum_manager.hpp` "Logout-with-text" sentence; T053 carries it. L2R3-003: FR-008
  removed from the spec's disregard list. L2R3-004: R-14's Guard 4 branch returns
  `store_seqnum_overflow`; T053 carries it. L2R3-005: R-14 and quickstart name the FileStore read
  recipe; T052 carries it.
- `[P]` means different files and no incomplete dependency.
- `[USn]` traces a task to its story. Setup, Foundational, Phases 7–8 and Polish carry none.
- Coverage of the C-2 rows, the C-3 invariants and the quickstart §1/§2 cell list is re-checked
  mechanically by `/speckit-analyze`; this file records no count.
