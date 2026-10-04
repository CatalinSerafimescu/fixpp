# Tasks: Inbound frames 092 leaves out

**Feature**: `093-inbound-frame-dispositions` (fixpp#514, #515, #516, #523, #524; #540 reproduce-first;
batch B22) | **Branch**: `093-inbound-frame-dispositions`

**Input**: `specs/093-inbound-frame-dispositions/`:
- spec.md (FR-001…FR-053, SC-001…SC-008, User Stories 1–5);
- plan.md (implementation phases P0–P9, OD-1…OD-19);
- research.md (R-1…R-10);
- data-model.md (E-1…E-13);
- contracts/inbound-frame-dispositions.md (C-1…C-8, W-1…W-4, I-1…I-6, C-7 rows, L-1…L-17);
- quickstart.md (§0 baselines, §1 cells Q-1…Q-37, §2 instruments and mutants, §3 regression and cost,
  §4 traceability).

**Tests**: REQUIRED (`[const §VII.3–4]`). Each story's test tasks come first. Each names the base it
is RED on, or says it is a regression guard green on base and names the mutant that turns it RED
(quickstart's opening rule).

**Gate A**: converged 2026-10-03, round 3 (plan.md `## Gate A`). No owner item is open.

**Depends on**: fixpp PR #539 (B25, fixpp#530, the CI ODR census for `FIXPP_TEST_HOOKS`). No
production edit starts before T002.

## Execution rules (apply to every task)

- **Who executes.** Every task that edits a file other than `*.md` under `specs/`, `spec/`, `brain/`
  or `docs/` goes to `phase-implementer` (`[const §XVI.6]`). That covers code, tests, CMakeLists,
  benches, the fuzz corpus, `capi_freeze.sha256`, the symbol golden, `expected-ctest-093.txt`,
  `CLAUDE-history.md`, and **comment-only edits** in `.hpp`/`.cpp`/`.h`/`.i`/`.cmake` files. The
  orchestrator writes only `tasks.md`, `research.md` records, the B&L text, `brain/`, the
  `spec/feature-catalogue.md` / `spec/coverage-index.md` markdown and the evidence file. It never
  writes library code by any tool, including python or sed through Bash.
- **Scratch artifacts are `phase-implementer`'s too.** Every uncommitted scratch artifact (a
  measurement program, a throwaway test, a seeded fixture, a census `abort()`, a mutant) is authored
  and run by `phase-implementer` in a scratch copy made with `git archive HEAD | tar -x -C <scratch>`
  (a fresh `mktemp -d` path), never in the PR worktree and never by the orchestrator. That covers
  T007, T008, T009, T013's seeding, T014, T051, T061 and every mutant task. The orchestrator writes
  only the evidence-file record of what the implementer reports.
- **Builds need an owner ask (`[const §XVII.7]`).** Ask with `AskUserQuestion` before any configure,
  build, rebuild or `conan install`, including every RED/GREEN step, the fuzz runs, the bench runs,
  the MSVC sandbox and `/speckit-verify`. One approval may cover a named phase. Record each ruling in
  the evidence file. One preset at a time, under the 16 GiB cap.
- **Disk and build trees.** `df -h /mnt/e` before any full rebuild of the main checkout's `-debug`
  tree. Sanitizer, coverage and release trees live on the F: vhdx (`/mnt/wsl/fixppbuild`). Long builds
  run `setsid nohup` with a `.done` marker and one waiter, never a foreground poll loop.
- **Never `git checkout` / `git switch` in the shared main checkout.** The bench base (T005) is its
  own detached worktree under `/mnt/wsl/fixppbuild`.
- **The base.** "RED on base" means RED on the merge-base T002 records after the rebase onto
  `origin/main`, not on spec.md's `00c1f720`. Every RED claim is run on that base and its failing
  output quoted in the commit message or the evidence file.
- **Mutants run in a scratch copy, never in the PR worktree.** Each is shown RED on the cell it names,
  then GREEN after revert; `git diff` of the scratch copy against the PR head proves the revert is
  clean. `strings`-check that the binary is fresh before believing any GREEN or any zero.
- **Test access (FR-053; fixpp#511, B21).** Private state is reached only through a named
  unconditional friend `friend struct <class>_test_access;`, defined once under `tests/support/`.
  Nothing in a class is gated on `FIXPP_TEST_HOOKS`; B25's census enforces it. The engine seam
  `session_engine_access` (E-11) is production code under `src/session/`, not test access.
- **Labels.** Every ctest entry whose cells a task adds or edits carries label `093`. On an existing
  entry use `set_property(TEST <name> APPEND PROPERTY LABELS 093)`, placed **after** the entry's last
  `set_tests_properties(... LABELS ...)`. Never `set_tests_properties(... LABELS ...)` on an existing
  entry. `expected-ctest-093.txt` (T011) is the gate. New isolation-safe `.cpp` files join an existing
  grouped bucket (`[const §VII.8]`); timer, coroutine, engine and global-allocation cells stay
  standalone, with the reason in a CMakeLists comment above the registration.
- **Re-derive, never copy.** Every population the bundle gives by command (Framer callers, late parse
  sites, `logon_arm_superseded` sites, `MessageStore` subclasses, 1.10 bullets, the observer set,
  old-behaviour pins) is re-run at the implementation head (T012). A listed member is a lead; a member
  the command adds is a planned edit.
- **Comments record a procedure, never a result.** No counts, offsets, line numbers, sizes or pasted
  output in a comment. The measured or derived constants (`kBodyLengthDigitCap`, `kAlignPad`,
  `kCallbackReadHeadroom`, `kContainerSlack`, and the counted-work bound's constant, T017a) carry their
  recipe in a comment and their value only in code and in `research.md`. Where a comment supersedes a decision, name the new one in a header
  comment ("093 …").
- **Evidence file.** `.specify/decisions/093-inbound-frame-dispositions-evidence.md` holds owner build
  rulings, populations, baselines, reproductions, mutants, measurements and fuzz results.
  `/speckit-verify` writes its own record (T120) and cites this file.
- **Index.** `codegraph sync` in the tree that owns the branch after every code-changing phase.
- **Commit after each task or logical group**, with the RED output quoted for each TDD step.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: can run in parallel (different files, no dependency on an incomplete task).
- **[Story]**: US1–US5 from spec.md. Setup, Foundational, the public-surface phase and Polish carry no
  story label.

## Phase map: plan P0–P9 → tasks.md phases

| tasks.md phase | Plan phase(s) | Why here |
|---|---|---|
| 1 Setup (T001–T012) | **P0** in full; the #539 rebase; the #540 reproduction | Baselines and the census must precede the first production edit; #540's reproduction must run on base before its catch (FR-015) |
| 2 Foundational (T013–T025, without T024) | **P1** in full (Framer, C-1); **P2a**: `inbound_limit_for` and the stored L, with no refusal; **P3a**: the `session_engine_access` seam with `inbound_limit()` | The pump (P3) consumes the Framer and L. "Framer before pump" holds. The phase changes nothing on the wire: the 383 refusal (T022a) and the L-sized carry (T024) change what a session accepts, so they land in Phase 5's FR-013 group |
| 3 US1 (T026–T040) | **P3** resync half (pump, first-frame read, counter, event, log); **P4** step 1 (35-not-third) | Garbled frames disregarded |
| 4 US2 (T041–T053) | **P3** deadline half (C-4) | Establishment timeout |
| 5 US3 (T054–T070, with T024, T022a, T055a) | **P2b**: B(L), the reserve overloads, the entry cap, the late sites, #540's catch; **P2/P3 FR-013 group**: the 383 refusal (T022a), the carry allocated at `open()` and sized for L (T024), the Framer limit set to L in both Framers, the Active-only 383 check deleted, `test_070` re-based | P2b may follow P3 because no P3 task reads B(L), the reserve or the entry cap. The FR-013 group sits here because setting both Framers' limit to L, refusing a 383 outside [4096, 262144] and sizing the carry for L *are* FR-013/FR-010's behaviour change, and its Q-6 cells need US1's resync |
| 6 US4 (T071–T074) | **P4** step 4 (liveness) | Needs US1's step 1, which the refresh follows |
| 7 US5 (T074a–T089) | **P5** (#523), then **P6** (#524) | FR-030 (T077) lands before FR-041 (T084) |
| 8 Public surface (T090–T104) | **P7** | One C-ABI MINOR bump after every C-7 row 1–6 behaviour has landed; Q-37 needs them all |
| 9 Polish (T105–T126) | **P8** docs, then **P9** `/simplify` → `/speckit-verify`; the two mandatory close-out tasks last | Plan order |

---

## Phase 1: Setup (P0: baselines, census and reproduction before any production edit)

**Purpose**: the measurements that cannot be taken after the first production edit, the #540
reproduction, the population snapshots, the test-builder census and its fixes, and the label manifest.

- [X] T001 Pre-flight in the tree that owns the branch.
  - `git -C research/G19-fix-fpml-iso20022/library worktree list`, and confirm which tree holds
    `093-inbound-frame-dispositions`. Every later path, `projectPath` and `codegraph` command uses that
    tree. `codegraph status --json` there shows a nonzero `fileCount` for that tree's path.
  - `git rev-parse --abbrev-ref HEAD` prints `093-inbound-frame-dispositions`, and `git status
    --short` is empty.
  - `.specify/decisions` is the symlink to the parent's tracked `decisions/speckit/`. If the tree is
    a new worktree, re-create the symlink before writing any record.
  - Create `.specify/decisions/093-inbound-frame-dispositions-evidence.md` with sections `## Owner
    build rulings`, `## Base`, `## Populations`, `## Bench baseline`, `## Ceilings`, `## #540`,
    `## Census`, `## Constants`, `## Mutants`, `## Measurements`, `## Fuzz`, `## MSVC`. Ask the owner
    for build approval (per phase or blanket) and record the answer.
- [X] T002 Rebase onto `origin/main` after fixpp PR #539 (B25, #530) merges.
  - `gh pr view 539 --repo CatalinSerafimescu/fixpp --json state,mergeCommit`. While it is OPEN, stop
    here; nothing below T002 runs.
  - `git fetch --all --prune`, then `git rebase origin/main`. The branch carries only `specs/`
    files, so a conflict outside `specs/` is a stop.
  - Confirm the ODR census step exists in `.github/workflows/tier1.yml`: take the census script or step
    name from #539's diff (`gh pr diff 539 --repo CatalinSerafimescu/fixpp --name-only`), and `grep
    -n` it in `tier1.yml` at `origin/main`. Record both outputs in the evidence file.
  - Record `git merge-base HEAD origin/main` under `## Base`. It is the base for every RED claim.
  - `git diff --stat origin/main...HEAD -- src include tools cmake tests bench bindings` is empty.
  - `codegraph sync` in the owning tree.
- [X] T003 C-ABI preconditions, recorded in the evidence file:
  - `gh release list --repo CatalinSerafimescu/fixpp --exclude-drafts` is empty (`[const §X.7]`);
  - the current `FIXPP_C_ABI_VERSION_MINOR` on `origin/main`, and on every branch:
    `git grep -h "define FIXPP_C_ABI_VERSION_MINOR" $(git for-each-ref --format='%(refname)' refs/heads refs/remotes) -- include/fix/c_api/version.h | sort | uniq -c`.
    The MINOR this feature will take is the `origin/main` value plus one, and no other branch may
    already claim it. T095 re-runs both checks.
  - Positive controls: the grep's output must include `origin/main`'s own MINOR, so a grep that
    matched nothing cannot read as "no branch claims it"; and `gh release list` must exit 0, so an
    auth failure cannot read as "no release".
- [X] T004 Via `phase-implementer`, a **bench-only commit**: add a validation-on variant of
  `BM_Session_OnInboundFrame_InSequence` (`validate_inbound_messages = true`) to
  `bench/session/on_inbound_frame_bench.cpp`. No file outside `bench/` changes, and the source
  compiles against the merge-base API. Record its `git patch-id --stable` in the evidence file.
- [X] T005 The paired timing baseline, **before the first production edit** (quickstart §0.1,
  `[const §VIII.2]`).
  - Base: a detached worktree at the T002 merge-base under `/mnt/wsl/fixppbuild`, its path padded to
    the branch tree's path length, with only T004's commit cherry-picked. `git -C <wt> diff --stat
    <merge-base> -- src include` prints nothing.
  - Candidate: this branch at T004's commit.
  - Both built `linux-clang-release` on one machine. Diff the two `CMakeCache.txt` files' `FIXPP_*`,
    `CMAKE_BUILD_TYPE` and compiler entries; any difference other than the source directory stops the
    run. `cmp` the two `on_inbound_frame_bench` `.text` sections: they must match.
  - A-B-A-B under `taskset`, into a fresh `mktemp -d -p /mnt/wsl/fixppbuild` directory, for
    `bin/on_inbound_frame_bench` (validation off and on) and `bin/framer_bench`'s
    `BM_Framer_Feed_NoCarry` (default `Config`). Record min-per-tree per case, both SHAs and T004's
    patch-id under `## Bench baseline`. The base's `BM_Framer_Feed_NoCarry` is the reference for both
    of T112's Framer pairings: against the same row (the strict path) and against T018a's resync-config
    row (the production path). `on_inbound_frame_bench` bypasses the Framer, so no other bench row covers
    the pump's resync-mode Framer. Keep the base worktree for T089, T102 and T112.
- [X] T006 Take the MSVC sandbox lock: `/mnt/c/temp/fixpp/.sandbox-lock`, per
  `research/G19-fix-fpml-iso20022/msvc-local-build-procedure.md` Step 0, before the first rsync. If
  another owner holds it, stop and ask the owner. Record the acquisition in the evidence file. The
  lock is held until T123.
- [X] T007 Via `phase-implementer`, measure the parse ceilings on base, per lane (quickstart §0.2; RED
  evidence for SC-004). In a scratch program or throwaway test (never committed), in a scratch copy
  made with `git archive HEAD | tar -x -C <scratch>` (Execution rules), using
  `tests/support/pmr_allocation_tracking_resource.hpp`:
  - the field count at which a well-formed frame at the densest layout (`1=<SOH>` fields) fails the
    inbound parse in today's stack arena, on each Linux preset `/speckit-verify` runs;
  - the field count at which `default_max_offset_entries` fails it, on every lane including the MSVC
    debug sandbox (T006's lock);
  - which lanes have a null arena upstream (`fixpp::detail::arena_upstream()` in
    `include/fixpp/core/pmr_arena_upstream.hpp`).
  Record each figure with the SHA in `research.md` R-3 and under `## Ceilings`, never in a comment.
- [X] T008 Via `phase-implementer`, reproduce fixpp#540 on base (quickstart §0.4, Q-32's first half,
  FR-015, SC-008), in a scratch test that is not committed, in a scratch copy made with `git archive
  HEAD | tar -x -C <scratch>`: a `Parser` over a small `std::pmr::monotonic_buffer_resource`
  whose upstream is set explicitly to `std::pmr::null_memory_resource()`, sized so the parse succeeds
  and the unknown-field list does not fit; `unknown_fields()` under `EXPECT_DEATH`. Before believing
  "no terminate", show the resource is exhausted (the tracking resource reports the failed request).
  Record the output under `## #540` and the branch it decides: **terminates** → Q-32 is RED on base
  and the PR settles #540; **does not terminate** → #540 is closed as not a bug with that output (an
  owner-approved comment), and Q-32 becomes a regression guard.
- [X] T009 Via `phase-implementer`, the census of non-canonical test builders (quickstart §0.3,
  research R-8), in a scratch copy only (`git archive HEAD | tar -x -C <scratch>`). Add a temporary `std::abort()` on a fault-free `!msg_type_is_third` in every arm of
  `Session::on_inbound_frame` (`src/session/session.cpp`). Positive control first: one seeded frame
  whose third field is not 35 aborts. Then run the whole suite unfiltered, including the loopback,
  C-ABI and Python round-trips. Record every aborting test and its builder under `## Census`.
  - **Second arm, the tests that rely on a garble closing the session** (no grep finds them). In a
    separate scratch copy, add a temporary `std::abort()` on every Framer error except
    `wire_frame_too_large`, in `run_read_pump` (`src/session/engine.cpp`) and in
    `read_first_frame_bounded` (`src/session/read_first_frame_bounded.hpp`). Positive control first:
    a seeded garble aborts. Run the whole suite unfiltered. Classify each hit as **flips** (its intent
    is the old close: T032 rewrites it RED first) or **keeps its intent with another trigger** (e.g. an
    elapsed-band witness in `tests/session/engine_firstframe_test.cpp` that used junk to force a close:
    T010 gives it a trigger that still closes after US1, such as a CompID mismatch or an over-L frame).
- [X] T010 Via `phase-implementer`, fix each test builder T009 found so it emits 8, 9, 35 first, and give
  each T009 second-arm hit classified "keeps its intent" a trigger that still closes after US1. Tests
  only, no production change; each changed test stays GREEN on base. Re-run both T009 arms until they
  report only the "flips" hits, with each positive control re-shown on that run.
- [X] T011 Via `phase-implementer`, create `specs/093-inbound-frame-dispositions/expected-ctest-093.txt`,
  one ctest name per line, sorted: every existing entry T012's populations say 093 edits, plus the
  entries this file registers (`session_inbound_frame_dispositions` T021,
  `session_engine_establishment_timeout` T041, `capi_inbound_frame_dispositions` T090,
  `alloc_guard_093_pump_active_read` T044, `engine_reset_unit_stop` T082). Append `093` to each existing
  entry as the execution rules say. Where an edited source runs under two entries (a plain alloc guard
  and its `*_mallocnesia` twin, T066), both are listed and both take the APPEND label.
  The gate: `ctest --test-dir build/linux-clang-debug -N -L '^093$' | sed -n 's/.*Test *#[0-9]*: //p' | sort`
  equals the manifest. Positive control: before the registering tasks land, the gate reports exactly
  those names missing.
- [X] T012 Re-derive every population at the implementation head and record each output and its
  classification under `## Populations`. Each later task that consumes one names this section.
  - `Framer::feed` callers: `git grep -n -E "\bFramer\b|\.feed\(" -- src tests bench tools`.
  - Old-behaviour pins (research R-8):
    `git grep -n -E "FramerFailureClosesEstablishedSession|HeaderFieldsOutOfOrder_MsgTypeNotFirst|PreActive_D8Logon|LateSite_|StoreEndsAtTeardownReset|close_from_.*_persist" -- tests`,
    `test_validate_gate_inbound.cpp`'s W1 cells, `coverage_adversarial_test.cpp`'s 35-position cells,
    `tests/session/test_070_max_message_size_test.cpp`, `tests/session/test_066_arena_fit_test.cpp`.
  - Late inbound parse sites (092 contract C-6's command):
    `grep -n "parse_and_dispatch_(\|validate_inbound_(" src/session/session.cpp`, each classified by
    the provenance of its bytes.
  - `kInboundParseArena` and `default_max_offset_entries` users: `git grep -n "kInboundParseArena\|default_max_offset_entries" -- src include tests bench`,
    each classified by T066's three classes.
  - `last_inbound_steady_` writers: `grep -n "last_inbound_steady_" src/session/session.cpp`.
  - `logon_arm_superseded` sites against each Logon arm's `co_await` sites:
    `grep -n "logon_arm_superseded\|co_await" src/session/session.cpp`.
  - `teardown_reset_done_` sites: `grep -n "teardown_reset_done_" src/session/session.cpp`.
  - `MessageStore` subclasses: `git grep -n "public .*MessageStore"`.
  - Tests and fixtures that configure 383: `git grep -n "advertised_max_message_size" -- tests bench bindings`,
    each classified as inside or outside [4096, 262144].
  - `SessionEvent` visits: `git grep -n "std::visit" -- src include tests bindings`.
  - Size pins on changed types: `git grep -n -E "sizeof\((fixpp::)?(session::|wire::)?(Session|SessionConfig|Framer|MessageView|OffsetTable)\b" -- src include tests bench`.
    T017 consumes the `Framer` members of this population.
  - The C-ABI 1.10 bullets and the observer set: `grep -n "C-ABI 1.10" include/fix/c_api/session.h`,
    and the 1.10 history entry in `include/fix/c_api/version.h`.
  - E-4 / E-6 / E-13 placement conditions: what follows each `run_read_pump` and each `publish_entry`
    call in `run_accept_loop` and `run_connect_loop` (`src/session/engine.cpp`), and the lifecycle
    note above `class Engine` (`include/fixpp/session/engine.hpp`).
  - Install rules: `git grep -n "install(" -- CMakeLists.txt cmake` shows no `src/session/` header is
    installed (for E-11).
  - Each member these commands add whose edit lands in a ctest entry missing from T011's manifest
    extends the manifest and gets its APPEND label; T011's positive control is then re-stated.

**Checkpoint**: rebased after #539, baselines and ceilings recorded, #540's branch decided, the
builders canonical, the populations recorded. No production file has changed.

---

## Phase 2: Foundational (P1 Framer, P2a limit and carry, P3a engine seam)

**Purpose**: the opt-in Framer resync with its bounded work (C-1), the one inbound limit L computed
and stored, and the engine seam. Nothing a session accepts or refuses changes here. At the end of this
phase the pump still runs its Framer with resync off, today's limit and its own local carry; US1 turns
resync on. US3's FR-013 group refuses a 383 outside [4096, 262144] (T022a), allocates the carry at
`open()` sized for L (T024) and sets the limit to L (T064).

**⚠️ No user-story task starts until this phase's checkpoint holds.**

### 2a: Derived constants (before the Framer code that uses them)

- [X] T013 Derive `kBodyLengthDigitCap` by research R-2's recipe. Search the repo's FIX-TC fixtures and
  interop goldens for the longest BodyLength digit run, leading zeros included, after each SOH
  spelling the fixtures use (the C escape `\x01`, `^A`, `|`, and a literal 0x01 byte). Positive
  control first: `phase-implementer` seeds one padded run in a scratch copy (`git archive HEAD | tar -x
  -C <scratch>`) and shows the search finds it. Take the larger of that maximum and the
  decimal width of 262144 plus the padding allowance. Check the value against contract C-1 W-2's floor
  and ceiling; if the search finds a run above the ceiling, stop and record it rather than raising the
  ceiling. Record the value, the date, the search output, the allowance's reason, and that both bounds
  hold, in `research.md` R-2.
- [X] T014 Via `phase-implementer`, measure `kContainerSlack` on the MSVC debug sandbox (T006's lock):
  the bytes MSVC's debug STL draws from a `pmr_carry_buffer`'s and a pmr vector's allocator at
  construction (`include/fixpp/core/pmr_arena_upstream.hpp` explains the proxy), using
  `tests/support/pmr_allocation_tracking_resource.hpp` in a scratch test, in a scratch copy made with
  `git archive HEAD | tar -x -C <scratch>` and rsynced to the sandbox. Record the value and its
  measurement in `research.md` R-3 and under `## Constants`.

### 2b: Framer resync (C-1; E-1), tests first

- [X] T015 [P] Via `phase-implementer`, write the Framer RED cells in the new
  `tests/wire/framer_resync_test.cpp`, added to the `wire_pure_tests` bucket in
  `tests/wire/CMakeLists.txt` (APPEND label `093`). Every shape is fed whole, at every split point, and
  one byte per read, with `Config{.max_frame_bytes = L, .resync_on_garble = true}` at L = 65536 and at
  a small L:
  - Q-2: good ‖ garbage ‖ good with garbage not ending in SOH (`XYZ8=FIX…`); a truncated frame then a
    good one; a garbage-only buffer consumed in finite steps;
  - Q-3: a wrong-CheckSum frame with a well-formed frame inside its extent is one garble through its
    own end (L-15); a well-formed frame after a malformed candidate is framed (L-8);
  - Q-4: the counted-work bound of C-1 (through T017's seam) on `8=␁` triples behind a failed large
    candidate; about L bytes of small valid frames behind a failed large candidate, drained one per
    feed; staggered nested candidates sharing one `10=`; `8=FIX` repeated with no SOH; a boundary `8=`
    with no SOH; `8=FIX.4.4␁9=` then a run of zeros with no SOH; the digit-cap pair (zero-padded to
    exactly `kBodyLengthDigitCap` digits framed, cap plus one a garble of kind
    `wire_invalid_body_length`); W-4: the carry's resource, a tracking resource, sees no allocation
    during any resync feed;
  - Q-5 (Framer half): a region split across feeds is counted once (`regions`), a continued region adds
    to `discarded` with `regions == 0`, garbles reported by a call precede every frame it produces;
    each failed candidate is its own region, so two adjacent garbles (two failed candidates back to
    back, the second found by the first's search) count two; and after a wrong-CheckSum frame the next
    byte is a search position, not a frame boundary, so wrong-CheckSum ‖ junk ‖ good counts one region
    and frames the good frame (C-1 Frame start, Extent);
  - Q-6 (Framer half): a frame of L+1 bytes, one of L+1 bytes with a bad CheckSum, and an over-L
    BodyLength at a candidate the resync search found each return `wire_frame_too_large`;
  - Q-10 (Framer half): a BeginString longer than `max_begin_string_bytes` with no SOH is a garble of
    kind `wire_framing_resync`; a configured-long cap frames a frame carrying that BeginString; a
    BeginString not beginning with `FIX` after a garble finds no next start (L-16 pin).
  RED: it does not compile (no `resync_on_garble`, `garble_summary`, `last_garbles()`). Commit as such.
  - **As landed (2026-10-03; `4084623b`, `61245b10`, `8e3f519e`), additions to the list above:**
    - a Q-4 shape, "nested candidates resolved one per byte" (research R-2 names it; without it the
      compact-on-every-non-empty-feed mutant survives every listed shape);
    - a per-outcome work cell (`Q4_EveryOutcomeKindChargesItsWork`);
    - a precondition cell for the K = 6 cap condition;
    - the OD-20 cell.
    Large shapes are fed in 4096-byte reads plus one-byte reads, not whole and at every split: a whole
    feed of an L-sized stream overflows the L + R carry by design.

- [X] T016 [P] Via `phase-implementer`, Q-7 regression guards in `tests/wire/framer_error_path_test.cpp`
  (bucket `wire_pure_tests`): with `resync_on_garble = false`, an over-max frame with a bad CheckSum
  still reports `wire_checksum_mismatch`, and `8=` with no SOH stays partial until the carry fills.
  Green on base by design; mutant in T019.
- [X] T017 Via `phase-implementer`, the counted-work seam (quickstart §2): `friend struct
  framer_test_access;` in `include/fixpp/wire/framer.hpp`, unconditional, with a header comment naming
  fixpp#511/B21, and `tests/support/framer_test_access.hpp` reading the counts of bytes the Framer
  reads, sums and moves. **Before writing**, the implementer reports the counters' shape (members,
  types, where they increment) to the orchestrator: they cannot be gated on `FIXPP_TEST_HOOKS`, so they
  are always compiled, and T005/T112's `framer_bench` pairing covers their cost. The condition they
  must meet: with `resync_on_garble = false`, every `feed` result, error kind and carry state is
  unchanged from the base, so incrementing them changes nothing a strict caller observes (Q-7 and the
  existing Framer cells are the witnesses). The counters grow `sizeof(Framer)`: update every `Framer`
  member of T012's size-pin population, each classified in the evidence file, and build every target
  that holds a `Framer` by value (`-k 0`) before committing. Data-model E-1 and contract C-7 row 1 name
  the friend and the counters.
- [X] T017a Derive the counted-work bound's constant (contract C-1 "The bound") from W-1 to W-3, over
  the counters T017 fixed: one unit per byte read, summed or moved; W-1's compaction term per L ÷ R,
  W-2's per-feed header rescan under the two caps, W-3's single sum per byte. Record the value and its
  derivation, step by step from each W clause, with the date and the head, in `research.md` R-2 and
  under `## Constants`. It is the value T015's Q-4 cells and T020's fuzz arm assert against, so T015
  cannot go GREEN (T018) before it is recorded. The derivation and the R-2 record are the
  orchestrator's; the code half goes to `phase-implementer`: a named test constant in
  `tests/support/framer_test_access.hpp`, which T015 and T020 read, with the recipe in its comment
  and the value nowhere else but `research.md` (Execution rules).
- [X] T018 Via `phase-implementer`, implement C-1 in `include/fixpp/wire/framer.hpp` and
  `src/wire/framer.cpp` (E-1):
  - `bool resync_on_garble = false;` and `std::size_t max_begin_string_bytes` (default: the length of
    the longest supported profile identifier, derived from the profile list in code, not a literal),
    new `Config` members, read only in resync mode;
  - `kBodyLengthDigitCap` with T013's value and its recipe in the comment;
  - `struct garble_summary { std::uint32_t regions; core::error first_kind; std::size_t discarded; };`
    and `garble_summary last_garbles() const noexcept;`, reset by every `feed`;
  - private `bool searching_`; the carry holds at most the trailing bytes that are a proper prefix of
    `8=FIX`;
  - the start rule (at a frame boundary `8=` as today; after a garble the next `8=FIX` anywhere,
    searched from the byte after the garble's first byte, or, after a wrong-CheckSum frame, from the
    byte after its own end, which is a search position and not a frame boundary, so junk after it
    joins its region), the extent rule (a structurally complete frame with a wrong CheckSum value
    consumed through its own end; each failed candidate is its own region, so two adjacent garbles
    count two), and the ordering rule (a call that produced a frame stops before resolving a later
    garble);
  - W-1 (compact only when the incoming bytes would not fit after what the carry holds), W-2 (both
    caps over encoded bytes at every candidate; the BodyLength read digit by digit and refused as over
    L as soon as it exceeds L), W-3 (sum only a structurally complete candidate, then consume it
    whole), W-4 (no allocation);
  - with the flag on and only then, `frame_len > max_frame_bytes` tested as soon as `body_off` is
    known, before the CheckSum (OD-4). With the flag off, `feed` is unchanged byte for byte, the check
    order included.
  T015 and T016 GREEN, against T017a's recorded constant.
- [X] T018a Via `phase-implementer`, a bench-only commit after T018: a candidate-only row
  `BM_Framer_Feed_NoCarry_Resync` in `bench/wire/framer_bench.cpp`, the same ~80-byte frame and carry
  as `BM_Framer_Feed_NoCarry`, over a `Framer` built with `Config{.max_frame_bytes = 65536,
  .resync_on_garble = true}` and `max_begin_string_bytes` set as the pump sets it (OD-16, for the
  frame's FIX.4.4). It is the only bench row on the pump's resync-mode Framer path. It cannot be added
  in T004 (the `Config` members do not exist on the base), so T112 compares it by name against the
  base's `BM_Framer_Feed_NoCarry`.
- [X] T019 The Framer mutants (quickstart §2), each in a scratch copy, RED on the named cell:
  SOH-anchored start rule → Q-2 (`XYZ8=FIX…`, truncation); compact on every feed → Q-4 (small frames);
  compact on every non-empty feed → Q-4 (one-byte reads); sum a wrong-CheckSum frame's nested candidates
  → Q-4 (shared `10=`), Q-3; uncap the BeginString scan → Q-4 (both no-SOH shapes), Q-10; cap the
  BeginString at the longest identifier ignoring the configured length → Q-10 (configured-long);
  uncap the digit run → Q-4 (zero run one byte per read, cap-plus-one); `frame_len > max` after the
  CheckSum in resync mode → Q-6; the reorder with resync off → Q-7. Before trusting Q-4's
  zero-violation result, show the counted-work instrument reports a value over the bound on one of
  these mutants. Record each under `## Mutants`.
- [X] T020 Via `phase-implementer`, the fuzz arm in `tests/fuzz/fuzz_wire_framer.cpp`
  (`[const §VII.7]`): a `resync_on_garble = true` arm that feeds each input whole and one byte per
  read, `__builtin_trap`s on non-termination (a feed that neither consumes, produces nor needs more)
  and on C-1's counted-work bound exceeded (through `framer_test_access`). Add one seed per Q-4 shape
  to its corpus directory. In a scratch copy, plant a violation and show the trap fires. The ≥600 s
  run is T111's.

### 2c: The inbound limit L and the engine seam (E-2, E-11), tests first

The 383 refusal (T022a), the carry allocated at `open()` (T024) and their cells (T055a) land in Phase
5's FR-013 group: each changes what a session accepts, and this phase changes nothing on the wire.

- [X] T021 [P] Via `phase-implementer`, create `tests/session/inbound_frame_dispositions_test.cpp`,
  registered with `add_threading_test(session_inbound_frame_dispositions inbound_frame_dispositions_test.cpp)`
  in `tests/session/CMakeLists.txt`, labels `"093;session"`, no `FIXPP_TEST_HOOKS`; the CMakeLists
  comment says why it is standalone (timers, coroutines, mock clock; `[const §VII.8]`). Later tasks
  (T026, T028, T029, T031, T042, T054–T056, T055a, T060, T076) add their cells to it. Its first cell:
  - Q-13 (the L half): L follows an advertised 383 inside [4096, 262144] when set, and is 65536 when
    unset, read through a new `session_test_access` accessor. RED: does not compile on the base (no
    accessor, no stored L). Commit as such; the refusal half is T055a's.
- [X] T022 Via `phase-implementer`, `std::uint32_t inbound_limit_for(SessionConfig const&) noexcept`
  (E-2): one free function in a private `src/session/` header, returning the advertised 383 if set,
  else 65536. `open()` stores `std::uint32_t inbound_limit_`. No value is refused here: the refusal is
  T022a's, in the FR-013 group. T021's Q-13 L cell GREEN.
- [X] T023 Via `phase-implementer`, the engine seam (E-11, OD-12): `friend struct session_engine_access;`
  in `include/fixpp/session/session.hpp`, defined in `src/session/session_engine_access.hpp`, never
  installed (T012's install population) and not gated on `FIXPP_TEST_HOOKS`. It starts with
  `inbound_limit()`; T024 adds the borrowed carry, and later tasks add the hooks their stories need.
  No underscore-suffixed engine hook joins the installed `Session` surface.
- [X] T025 `ctest -L '^093$'` and the full `wire` and `session` suites on `linux-clang-debug`,
  unfiltered. Every existing cell is GREEN: this phase changes nothing on the wire. (Q-14's MSVC leg
  runs in T069, after T024.)

**Checkpoint**: the Framer resyncs with bounded work behind its flag, strict callers unchanged; L is
derived and stored, not yet enforced or validated; the seam exists. The pump keeps its local carry and
today's limit. The session's behaviour on the wire is unchanged.

---

## Phase 3: User Story 1: a garbled frame is ignored, and the session carries on (Priority: P1) 🎯 MVP

**Goal**: a Framer-detected garble or a 35-not-third frame is disregarded in every state except
Disconnected, counted
exactly, evented and logged rate-bounded, and the next good frame is processed (FR-001…FR-005,
FR-008).

**Independent Test**: good ‖ garbled ‖ good in one write to an Active session: still Active, NextNumIn
reflects only the good frames, a ResendRequest covers the gap, one `session_event_garbled_frame`, and
`garbled_frame_count()` reads 1. The C-ABI half of this test closes in Phase 8 (T090, T104).

### Tests for User Story 1 (write first; RED on base unless marked)

- [X] T026 [P] [US1] Via `phase-implementer`, Q-1 in `tests/session/inbound_frame_dispositions_test.cpp`:
  TC 2020 2d, 2m, 2t, 3b, 3c and 3e in Active. Each: disregard and continue; NextNumIn kept (where a
  numbered frame is lost, the next good frame draws a ResendRequest for it; 2d and 3c lose none, per
  quickstart Q-1); `Session::garbled_frame_count()` reads 1; one `session_event_garbled_frame` with
  the row's `first_kind` from quickstart Q-1 (3e's CheckSum-not-last shape reports
  `wire_invalid_body_length`, the L-2 pin) and bytes; one log record with that kind, captured through a
  test sink set as `SessionConfig::logger_override`. RED: the session closes (2d/2m/3b/3c/3e) or the
  frame is processed (2t).
  - **Base RED method.** The cells name symbols the base lacks (`garbled_frame_count()`,
    `session_event_garbled_frame`), so on the base they are compile-RED, recorded as such. The
    behavioural RED is shown by a base-compilable scratch variant of each cell (the same frames and
    assertions on the base-observable effects only: state, NextNumIn, the ResendRequest, the frame
    reaching a callback), run in a scratch copy of the base (`git -C <T005 base worktree> archive HEAD
    | tar -x -C <scratch>`) built in a debug configuration (owner ask), as T102's base run does. The
    variant is uncommitted (Execution rules, scratch artifacts).
  - **Names and cross-reference (TC-002, TC-003).** Each cell is named for its TC row (e.g.
    `TC002_2d_*`, `TC003_3b_*`). The session TC corpus, `tests/session/conformance/`, holds the
    QuickFIX-oracle scenarios and cannot drive these (they need the pump and the resync Framer), so
    its `CMakeLists.txt` header gains a pointer naming this file and the TC rows it witnesses. T125
    names the cells in the catalogue rows.
- [X] T027 [US1] Via `phase-implementer`, Q-2/Q-3 through the pump in
  `tests/session/engine_readpump_test.cpp` (shared with T032, T055 and T055a, so not [P]): good ‖
  garbage ‖ good in one write with `XYZ8=FIX…`
  garbage; a truncated frame then a good frame; each split at every byte boundary; a garbage-only
  stream consumed with the session up; a wrong-CheckSum frame with a frame in its extent discarded
  whole (L-15); a well-formed frame embedded after a malformed candidate delivered and then guarded
  (L-8). Plus the L-1 pin: in Active, a BodyLength too large but ≤ L stalls, then the frame is
  disregarded and framing resumes once the counted bytes arrive, and the carry overflow closes when
  they do not fit. RED: the session closes.
  - **The L-1 pin's sizes.** Until the FR-013 group the pump runs today's Framer limit and its own
    local carry, not L and T024's carry. The pin takes its BodyLength and its overflow size relative to
    the limit and carry in force at the head it runs on (read from the named constants or the seam,
    never a literal), so it is GREEN at T038 against the local carry. T055a re-asserts it against L
    and the carry allocated at `open()`.
- [X] T028 [US1] Via `phase-implementer`, Q-5 in `tests/session/inbound_frame_dispositions_test.cpp`,
  on the mock clock: a region split across reads counts once; a summary with `regions == 0` emits no
  event; garbles before a frame in one feed are evented before that frame's effects; at most one log
  record per `max(HeartBtInt, 1 s)` carrying the suppressed count, including a session with
  HeartBtInt = 0 under a sustained garbage stream; and the L-9 pin: a garble flood larger than the
  event ring (capacity re-derived from `include/fixpp/session/session_event.hpp`) leaves the counter
  exact. RED: the session closes. Base RED method as T026's: compile-RED on the base (the counter,
  the event, the summary), and the behavioural RED (the close) by a base-compilable scratch variant,
  run as T026's is.
- [X] T029 [US1] Via `phase-implementer`, Q-8: garbled and 35-not-third frames disregarded before
  Active, in NotConnected, LogonSent, LogonReceived and LogoutSent
  (`tests/session/inbound_frame_dispositions_test.cpp`), and on the acceptor's first-frame read
  (`tests/session/engine_firstframe_test.cpp`, APPEND label `093`). In LogoutSent a garble is not taken
  as the Logout reply and the logout timeout ends the session. Each cell ends by its own bounded
  deadline, since the establishment timeout lands in US2. RED: close or refusal. Plus C-2's
  Disconnected rule as a regression guard: in Disconnected a 35-not-third frame is not scanned, not
  counted and not evented (mutant: run step 1 in Disconnected too; observed by the counter). Two more
  arms, from C-2's notes:
  - **a Framer garble in Disconnected** (the transport still open, so the pump still runs) is
    counted, evented and logged. RED: the pump closes on the garble;
  - **D-9 (LogoutSent)**: a frame whose third field is not 35 stays disregarded, and is now counted,
    evented and logged as one garble of kind `wire_header_out_of_order`. RED: compile-RED on the base
    (the counter and the event), and the base-compilable scratch variant shows the disregard with no
    count, run as T026's is.
- [X] T030 [US1] Via `phase-implementer`, Q-34 in `tests/session/engine_firstframe_test.cpp` (shared
  with T029 and T031): k garbled regions before a matching Logon on the acceptor's first-frame read;
  after `open()` the counter reads k and exactly one `session_event_garbled_frame` is recorded. RED:
  the connection closes at the first garbled byte. Base RED method as T026's: compile-RED on the base
  (the counter and the event), and the behavioural RED (the close at the first garbled byte) by a
  base-compilable scratch variant, run as T026's is.
- [X] T031 [US1] Via `phase-implementer`, Q-10 through the session in
  `tests/session/inbound_frame_dispositions_test.cpp` and `tests/session/engine_firstframe_test.cpp`:
  a BeginString mismatch within the cap keeps today's handling (Disconnected with no Logout in Active,
  refusal before Active, transport close on the acceptor's first frame), as regression guards; a value
  longer than the cap is a garble, disregarded and counted (RED: handled as a mismatch); a session
  configured with a BeginString longer than the longest supported identifier frames and processes its
  own frames (regression guard; T039's mutant).
- [X] T032 [US1] Via `phase-implementer`, flip the old-behaviour pins in T012's population, each
  rewritten RED-first, classified in the evidence file:
  - `tests/session/engine_readpump_test.cpp` `FramerFailureClosesEstablishedSession_*` → assert
    disregard and continue (L-004-4 closes). `OverCapacityFrameClosesSession` stays;
  - `tests/interop/parity/fix_tc_coverage_gaps_test.cpp`
    `HeaderFieldsOutOfOrder_MsgTypeNotFirst_Accepted_DivergesFromQuickFix` → asserts the disregard,
    renamed for it (B-005-7 narrows to fields other than the first three);
  - `tests/session/test_validate_gate_inbound.cpp` W1 (373=14 for 35-not-third) → disregard under
    `validate_inbound_messages = true`;
  - `tests/session/unparseable_frame_disposition_test.cpp` `PreActive_D8Logon_*_Refused` and the D-8
    cells → disregard (FR-005; D-8 unreachable);
  - `tests/session/coverage_adversarial_test.cpp`'s 35-position cells;
  - `tests/session/test_validate_gate_default_off.cpp` `T015_HeaderOutOfOrder_Accepted` (W_Off1, found by
    T009 arm 1): pins "accepted and dispatched with validation off", so it flips to disregard (FR-004
    applies in both validation modes);
  - `tests/session/read_first_frame_bounded_test.cpp` `ReadFirstFrameBounded.CovFramerErrorPropagates`
    (found by T009 arm 2): its garble no longer reaches the feed-error arm. `wire_frame_too_large` still
    does, so move the cell onto an over-limit input to keep that arm covered (`[const §IX.1]`). Do not
    delete it;
  - `tests/session/unparseable_frame_disposition_test.cpp`
    `UnparseableFrameDisposition.Anchor_D8_Active_Field3Not35_Disregarded` and
    `UnparseableFrameDisposition/RowByValidation.Disposition/D8_ValOn` / `D8_ValOff` (found by the Phase 3
    census of frames faulty at or before field 3): through `anchor_d8`, their disregard outcome stands, and
    each now also asserts `garbled_frame_count() == 1` (C-2 step 1 takes them).

### Implementation for User Story 1

- [X] T033 [US1] Via `phase-implementer`, append `session_event_garbled_frame { core::error first_kind;
  std::uint32_t frames; std::uint32_t discarded_bytes; }` to `include/fixpp/session/session_event.hpp`
  (`discarded_bytes` saturates at `UINT32_MAX`; `first_kind` is one of E-1's three Framer kinds or
  `wire_header_out_of_order`). Check T012's `std::visit` population.
- [X] T034 [US1] Via `phase-implementer`, the accounting (E-4, E-12) in
  `include/fixpp/session/session.hpp` and `src/session/session.cpp`:
  - `std::atomic<std::uint64_t> garbled_frames_`, incremented with relaxed ordering on the strand;
    public `std::uint64_t garbled_frame_count() const noexcept`;
  - `void note_garbles_(wire::garble_summary const&) noexcept`, reached through
    `session_engine_access`: adds `regions`; when `regions > 0`, emits one event and logs;
  - the logger resolved once at `open()` (`cfg_.logger_override`, else the engine's; may be null);
    the first production session log site, `FIXPP_SLOG` with the session's `trace_context`, its format
    strings registered in `src/log/format_registry.cpp`; at most one record per `max(HeartBtInt, 1 s)`
    carrying the kind, the bytes and the count suppressed since the previous record; the logger's
    `drop_newest` policy never blocks the strand.
  Record in E-4's comment the placement condition and its re-derivation recipe (one `Session` per
  entry per `Engine::start()`), not a count.
- [X] T035 [US1] Via `phase-implementer`, the pump (`run_read_pump`, `src/session/engine.cpp`): its
  Framer runs `resync_on_garble = true` with `max_begin_string_bytes = max(default,
  cfg.begin_string.size())` (OD-16); after every feed whose summary is non-empty it calls
  `note_garbles_`, in the read loop's drain and in the `initial_bytes` drain; `wire_frame_too_large`
  still closes. The `stop_pump()` on a Framer garble is gone; the header comment names 093 superseding
  L-004-4's wontfix.
- [X] T036 [US1] Via `phase-implementer`, the first-frame read (`src/session/read_first_frame_bounded.hpp`,
  `run_accept_loop` in `src/session/engine.cpp`): its Framer runs resync on with the same BeginString
  cap; it returns `{offset, len, garble_summary}`; the engine slices the first frame and the surplus at
  `offset`; after `open()` the accept loop calls `note_garbles_` once through the seam (OD-8). The byte
  budget still counts discarded bytes. The relative-to-absolute deadline line is not touched (088's B6
  mutant is pinned there).
- [X] T037 [US1] Via `phase-implementer`, C-2 step 1 in every arm except Disconnected
  (`src/session/session.cpp`): when the scan's `msg_type_is_third == false`, faulty or not, call
  `note_garbles_({1, core::error::wire_header_out_of_order, frame.size()})` and return success, with no
  Reject, no NextNumIn change and no liveness refresh. It runs before 092's fault branch, so D-8 and
  D-1/D-2 for that shape become unreachable; header comment naming 093 superseding them. The
  validator's Step 0 stays for direct `validate` callers.
- [X] T038 [US1] T026–T032 GREEN, then the full `session`, `wire` and `interop` suites unfiltered on
  `linux-clang-debug`.
- [X] T039 [US1] US1 mutants, each in a scratch copy: the pump passes `resync_on_garble = false` → Q-1,
  Q-2 RED; delete the 35-not-third check → Q-1 (2t), Q-8 RED; delete the 1 s floor of the log rate →
  Q-5 (HeartBtInt = 0) RED; cap the BeginString at the longest identifier ignoring the configured
  length → T031's configured-long cell RED. Record under `## Mutants`.
- [X] T040 [US1] `codegraph sync`; update T011's manifest for any entry this phase touched.

**Checkpoint**: US1 complete in C++. Its C-ABI counter (FR-007) and the C arm of Q-1 close in Phase 8.

---

**Phase 3 as landed (2026-10-03; `6a79a7af` tests RED, `ed16ca08` production, `bb0a477f`, `263e8f8c`):**
- New private header `src/session/read_pump.hpp` holds `kReadPumpCarryCapacity` (moved out of
  `engine.cpp`) and `inbound_framer_config(cfg)`, the one config both inbound Framers use.
  `read_first_frame_bounded` takes a `Framer::Config` (default: resync on) and returns
  `first_frame_read{offset, len, garbles}`.
- `dispose_unparseable_`'s D-8 branch is deleted (step 1 makes it unreachable); its header comment
  names 093.
- **T029: no Framer-garble cell for LogonReceived.** In that state the pump is suspended inside
  `on_inbound_frame`, so no feed reaches the Framer. LogonReceived's step-1 cells are the `Step1_*` and
  D-8 cells in `unparseable_frame_disposition_test.cpp`.
- New harness `tests/session/plain_engine_rig.hpp`: a plaintext Engine on a mock clock with a raw TCP peer.
- Three cells that passed on the base were strengthened until they were RED there: `Q8Pump.LogoutSent_*`,
  W1 and W_Off1.
- OD-21 is pinned by `LogRecordsReconcileWithTheCounterRegionByRegion`.

## Phase 4: User Story 2: establishment cannot hang (Priority: P1)

**Goal**: one absolute deadline per connection until the first Active, in two phases (C-4); expiry is a
loop-head check, the race only wakes a blocked read, and expiry closes the transport (FR-006,
FR-052).

**Independent Test**: initiator after its Logon, and acceptor after a matching first frame left
pre-Active: a garbage-only peer is closed at T and not before; an acceptor with no matching frame closes
at the first-frame bounds. The C, Python and TOML arms close in Phase 8 (T090, T092, T093, T104).

### Tests for User Story 2 (write first)

- [X] T041 [US2] Via `phase-implementer`, the new `tests/session/engine_establishment_timeout_test.cpp`,
  registered `add_threading_test(session_engine_establishment_timeout ...)`, labels `"093;session"`,
  standalone (engine, mock clock, timers). Mock-clock cells drive `engine_cfg.clock`. Not [P]: T045
  writes the same file.
  - Q-16, per role (the initiator after its Logon; the acceptor after a refused Logon and after a
    non-Logon first frame): a garbage-only peer closes **at** T and not before; a silent peer closes
    at T; `session_event_establishment_timeout` is recorded; the transport is closed and the FSM ends
    in Disconnected, including a session already refused into Disconnected. RED: an immediate close
    (garbage), or never (silent).
  - Q-16, readable across T: extend `tests/support/transport_double.hpp` with a read that completes at
    initiation over a finite stream ending in EOF; a peer streaming garbage, and separately valid
    non-Logon frames, past T has no frame delivered after T and is closed at the first loop head after
    T. RED: never closed (the valid-frame stream).
  - Q-35: `SessionConfig{}` reads `logon_timeout_ms == 10000`, and a silent peer in phase (b) is
    closed at 10 s on the mock clock. RED: the field does not exist.
- [X] T042 [P] [US2] Via `phase-implementer`, Q-13 (timeout half) in
  `tests/session/inbound_frame_dispositions_test.cpp`: `logon_timeout_ms == 0` is refused by
  `Engine::register_session` and by `Session::open()` with `invalid_session_config`. RED: the field does
  not exist.
- [X] T043 [US2] Via `phase-implementer`, Q-17 (phase a) in
  `tests/session/engine_firstframe_test.cpp` and `tests/session/first_frame_total_cancel_tls_test.cpp`
  (ctests `engine_firstframe` and `session_first_frame_total_cancel_tls`, APPEND label `093` on each).
  The `min(5 s, T)` and byte-budget cells (the first bullet) run on a **plaintext acceptor**: an entry
  whose `security_profile` is `insecure_plain_tcp`, for which the accept loop in
  `run_accept_loop` (`src/session/engine.cpp`) skips the TLS handshake, so no handshake bound competes
  with T. Re-derive that skip at this head before relying on it. Only the TLS pair (the second and third
  bullets) runs on TLS:
  - a garbage-only acceptor peer closes at the first-frame byte budget or at `min(5 s, T)`,
    whichever comes first; with T < 5 s the first-frame read ends at T; bytes under the budget sent
    slowly are not closed before then; no event is recorded (L-6). RED: close at the first garbled
    byte;
  - TLS with T below the handshake bound: a stalled handshake closes at the handshake bound
    (regression guard);
  - TLS, the late handshake, on real TLS with a **peer-side behavioural observable** (no production
    seam; `run_accept_loop` builds its own `asio_listener`, and the TLS transport's read state is
    private). T is below the handshake bound, and the test peer completes its handshake only after T
    has elapsed, inside the handshake bound, so no establishment time remains when the handshake ends.
    The peer then sends a valid, CompID-matching Logon at once, and asserts that it receives **no**
    Logon reply and that the connection closes within the bound: before the 5 s first-frame deadline
    would have elapsed from the handshake, so a server that read and then timed out also fails. RED
    witness: T053's mutant (delete the post-handshake remaining-time check, so the first-frame read is
    issued on its 5 s bound) turns it RED, because that server reads the Logon and replies. Show the
    mutant's Logon reply before believing the GREEN.
  - **Carried from Phase 3 (T036):** no Phase 3 cell shows that the first-frame budget counts the
    bytes a garble discards. This task's Q-17 cells must show it.

- [X] T044 [P] [US2] Via `phase-implementer`, Q-19 in a new standalone target
  `tests/alloc_guard/test_093_pump_active_read_alloc_guard.cpp` (it replaces global `operator new`,
  `[const §VII.8]`), executable `test_093_pump_active_read_alloc_guard`, registered in
  `tests/alloc_guard/CMakeLists.txt` as ctest `alloc_guard_093_pump_active_read` with labels
  `"093;alloc_guard"`. No `*_mallocnesia` twin: its window wraps `ioc.run()`, which that file's "DELIBERATELY
  NOT GATED" note keeps out of the LD_PRELOAD gate. The counter is
  first shown to count a known allocation in the same binary (the `planted_alloc_witness.cpp`
  pattern); then the real pump is driven past Active and the count per Active read after a warm-up
  read is zero. A regression witness, not claimed to catch a never-disarmed race.
- [X] T045 [US2] Via `phase-implementer`, Q-18 and Q-36 in
  `tests/session/engine_establishment_timeout_test.cpp`:
  - Q-18: a clock-wide `cancel_sleeps()` from another session during phase (b) does not end the wait
    early; with `clock_override` set to a second mock clock, advancing only the override does not
    expire the deadline and advancing `engine_cfg.clock` does (mutants in T053);
  - Q-36, per role, **no application attached**: Active before T, idle past T with T below
    HeartBtInt, still Active with its transport open. Green on base (no deadline); mutants in T053.

### Implementation for User Story 2

- [X] T046 [US2] Via `phase-implementer`, `std::uint32_t logon_timeout_ms{10000};` in
  `include/fixpp/session/session_config.hpp`, beside `logout_disconnect_timeout_ms` (copy neither that
  field's validation claim nor its comment). Zero is refused by `Engine::register_session` and by
  `Session::open()` with `invalid_session_config`. T042 and T041's Q-35 field half GREEN.
- [X] T047 [US2] Via `phase-implementer`, append `session_event_establishment_timeout { }` to
  `include/fixpp/session/session_event.hpp`; add the dedicated `reached_active_` latch, set
  unconditionally in `record_state_transition_` on the first entry to Active, **before** its
  `engine_.application == nullptr` early return (E-6; `onLogon_fired_` cannot serve); add
  `has_reached_active()` and `note_establishment_timeout_()` (the event plus a log record whose format
  string is registered in `src/log/format_registry.cpp`) to `src/session/session_engine_access.hpp`.
  The record is a `FIXPP_SLOG` with the session's `trace_context` (`[const §XIII.3]`), through the
  logger T034 resolves at `open()`, and carries T (data-model E-12). It needs no rate bound: expiry closes the connection, so it fires
  at most once per connection.
- [X] T048 [US2] Via `phase-implementer`, the phase (b) deadline in `run_read_pump`
  (`src/session/engine.cpp`): `run_read_pump(..., std::optional<steady_time_point> establish_deadline)`;
  acceptor: accept time + `logon_timeout`; initiator: the time `drive_reconnect` returns +
  `logon_timeout`. While `!has_reached_active()`, test `steady_now() >= deadline` on
  `engine_cfg.clock` before each read and before each frame's delivery, in both drains; race a blocked
  read against `await_deadline` on `engine_cfg.clock` only so it wakes (the re-arm handles a
  clock-wide sweep, #536). On expiry call `note_establishment_timeout_()`, then `stop_pump()` →
  `close(terminal)`. After the first Active, reads are plain. The deadline ignores
  `cfg.clock_override`.
- [X] T049 [US2] Via `phase-implementer`, phase (a) in `run_accept_loop`: compute `deadline − now` after
  the TLS handshake; when it is not positive, close the transport without calling
  `read_first_frame_bounded`; otherwise pass `min(5 s, deadline − now)` as that function's relative
  deadline. The function's own conversion is unchanged.
- [X] T050 [US2] T041–T045 GREEN.
- [X] T051 [US2] Via `phase-implementer`, in a scratch copy made with `git archive HEAD | tar -x -C
  <scratch>` (the measurement is not committed), measure the pre-Active allocation (L-13, FR-052):
  with T044's global counter around the pump's pre-Active reads, shown first to count a known allocation, record the count per read and
  after warm-up under `## Measurements`. It is disclosed, not asserted.
- [X] T052 [US2] Re-derive E-4/E-6/E-13's placement condition at this head (T012's commands) and
  record that both role loops still `co_return` after one connection.
- [X] T053 [US2] US2 mutants, each in a scratch copy: delete the loop-head test, keeping only the race
  → Q-16 readable-across-T RED; delete the post-handshake remaining-time check, so the first-frame read
  is issued on its 5 s bound → Q-17 late TLS handshake RED (observed by the peer: a Logon reply
  arrives); measure the deadline on `effective_clock_` → Q-18 RED; drop the re-arm → Q-18 RED;
  never disarm the race → Q-36 RED; disarm on `onLogon_fired_` instead of `reached_active_` → Q-36 RED.
  Record under `## Mutants`.

**Checkpoint**: US2 complete in C++. The C setter, Python and TOML arms of Q-16 close in Phase 8.

---

**Phase 4 as landed (2026-10-04; `84e40459` tests RED, `6732d595` production, `103b63db`, `70bf41fc`, `077bb01b`):**
- T044 / Q-19 is a differential (plan OD-22). `kBaseActiveReadAllocs` = 5, measured on the base. The
  pre-existing cost is filed as fixpp#544 (batch B15).
- `run_read_pump` takes a `Clock&`. The first-frame read's deadline is rounded up to whole milliseconds.
  Readable-across-T cells are initiator-only, because the acceptor has no transport seam.
  `EngineLoopbackHarness::build` takes an acceptor-config callback, so the TLS positive control can admit
  its client.
- T048 as landed has no post-race `cancelled()` branch. It was deleted because nothing observable differs:
  both shipped transports wake a pending read on close.
- Two cells added by orchestrator ruling:
  - `EstablishmentTimeoutQ18.AClockWideSweepNeitherCancelsNorReissuesTheBlockedRead` is the witness for
    the re-arm; `no_rearm` is RED on it. `ScriptedStream` gains a hold-open mode and read counters.
  - `EstablishmentTimeoutQ16.InitialBytesDrain_NoCoalescedFrameIsDeliveredOnceTheDeadlineHasPassed`
    covers the `initial_bytes` drain's before-delivery test.

## Phase 5: User Story 3: a large well-formed message is never lost (Priority: P1)

**Goal**: every admitted frame (≤ L, well framed) parses at every inbound site on every lane; a frame
over L closes at framing in every state (FR-010…FR-015).

**Independent Test**: per lane, MSVC debug included: a dense frame of exactly L parses and is
delivered, and a frame of L+1 is refused at framing with no guard or handler acting.

### Tests for User Story 3 (write first)

- [X] T054 [US3] Via `phase-implementer`, Q-11 in `tests/session/inbound_frame_dispositions_test.cpp`
  (shared with T055, T055a, T056 and T060, so not [P]): a dense frame of exactly L at the densest
  layout (`1=<SOH>`), at L = 65536 and at a configured 383 at the floor and at the ceiling, through the
  session: parsed and delivered to `fromApp`, peak ≤ B(L) measured with
  `pmr_allocation_tracking_resource`, no spill. RED: the parse fails (T007's ceiling is the RED
  evidence).
  - **SC-007's measurement.** With tracking resources as `framer_carry_arena` and as the session arena,
    record the bytes `open()` draws from each (T024's carry block, T063's B(L)) at L = 64 KiB and at
    L = 256 KiB, with the SHA, under `## Measurements`, once T024 and T063 have landed (T067). T105's
    B&L totals cite these measured figures beside the formula; a figure that disagrees with the formula
    is a finding, not a rounding.
- [X] T055a [US3] Via `phase-implementer`, the FR-013 group's L and carry cells, deferred from Phase 2
  so that phase changes nothing on the wire. Written RED with T055, before T024, T022a and T064:
  - in `tests/session/inbound_frame_dispositions_test.cpp`, Q-13 (the 383 half): an advertised
    MaxMessageSize below 4096 or above 262144 is refused by `Engine::register_session` and by
    `Session::open()` with `invalid_session_config`; 4096 and 262144 are accepted. RED: accepted today.
  - same file, Q-14 (the carry half): `open()` with a `framer_carry_arena` too small for L + the read
    size + `kContainerSlack` returns an `open()` error. RED: on the base `open()` succeeds, because the
    carry is built in `run_read_pump`, not at `open()`, so the expected error is absent. Run under
    `EXPECT_EXIT`, so that if a later pump start terminates on the undersized arena it is a recorded
    failure, not a crashed binary.
  - in `tests/session/engine_readpump_test.cpp` (ctest `engine_readpump`, APPEND label `093`), Q-12: a
    well-formed frame of exactly 65536 bytes (few fields, so today's parse holds it) fed split at every
    boundary near the carry edge is delivered. RED: today's 64 KiB local carry overflows for some
    splits.
  - same file, T027's L-1 pin re-asserted against L and the carry allocated at `open()`.
  - Base RED method as T026's: `inbound_frame_dispositions_test.cpp` does not compile on the base, so
    its cells are compile-RED there, and each behavioural RED above (the refusal absent, `open()`
    succeeding) is shown by a base-compilable scratch variant, run as T026's is.
  - **Carried from Phase 3:** the L-1 pins read `detail::kReadPumpCarryCapacity`
    (`src/session/read_pump.hpp`). When the carry moves to `open()`, switch them to the seam.

- [X] T055 [US3] Via `phase-implementer`, Q-6 through the pump and the FR-013 reversal:
  - in `tests/session/inbound_frame_dispositions_test.cpp` and `tests/session/engine_readpump_test.cpp`:
    a frame of L+1, one of L+1 with a bad CheckSum, and an over-L BodyLength at a resync candidate each
    close terminally with a log record (no `SessionEvent`; plan OD-24), with no guard or handler reached (no callback, no Reject,
    NextNumIn unchanged), in Active and in every pre-Active state, with a configured 383; RED: before
    Active such a frame is exempt today and in Active it only writes Disconnected with the transport
    open; the bad-CheckSum variant's RED is the reorder mutant (T019);
  - in `tests/session/engine_firstframe_test.cpp`: the acceptor's first frame over L is refused (the
    transport closes; no Session exists, so no event and no log, L-6);
  - `tests/session/test_070_max_message_size_test.cpp` (bucket `session_pure_tests`). Its size cells
    drive `Session::on_inbound_frame` directly, below the Framer, with a 383 near a Heartbeat's size.
    T064 deletes the session-level check they observe and T022a refuses such a 383, so they cannot stay
    in that harness. Each is classified in the evidence file:
    - `InboundAtLimitAccepted` **moves** to `tests/session/engine_readpump_test.cpp`, the pump harness
      (it feeds through the Framer, and its `SessionConfig` sets `advertised_max_message_size`).
      Re-based on a 383 inside [4096, 262144] with a frame padded to exactly that many bytes:
      delivered, session Active;
    - `InboundOverLimitDisconnects_LogonPreEstablishmentExempt` is **deleted**. Its post-Active half is
      the Active over-L cell above, and its pre-establishment exemption is reversed (OD-3) by the
      pre-Active over-L cells above, RED first;
    - `UnsetNoEnforcement` **stays**, re-based: no 383 on the wire, and L == 65536 read through T021's
      accessor. Its "no enforcement" claim becomes false (the Framer enforces L), so the cell and its
      comment are rewritten, not kept;
    - `BuildLogonEmits383` and `PeerAdvertised383Captured` are unchanged.
  - **Orchestrator ruling (Phase 5, 2026-10-04):** the two direct-feed controls
    `UnparseableFrameDisposition.MaxMessageSize_OversizedFaulty_Disconnected_Control_{MalformedTag,LengthDataMismatch}`
    are deleted. A direct feed bypasses the Framer, and T064 deletes the session-level check they observed.
    Their two fault shapes join the pump's Q-6 set: an Active over-L frame carrying a malformed tag or a
    Length/Data mismatch closes at framing, with no Reject, no callback and no `SessionEvent` (OD-24).

- [X] T056 [US3] Via `phase-implementer`, Q-14 (session-arena half) in
  `tests/session/inbound_frame_dispositions_test.cpp`: `open()` with a session arena too small for
  B(L) returns an `open()` error. RED: accepted today, because no B(L) is allocated.
- [X] T057 [P] [US3] Via `phase-implementer`, Q-15 and the re-based late-site cells in
  `tests/session/unparseable_frame_disposition_test.cpp`: a new `session_test_access` accessor shrinks
  the parse buffer after `open()`, so the 092 late close fires on an admitted frame (C-3 I-4); the
  `LateSite_*_Closes` cells move onto that shrink (their old trigger is gone, R-3). Q-15's mutant is in
  T070.
- [X] T058 [P] [US3] Via `phase-implementer`, Q-32 in `tests/wire/unknown_fields_test.cpp` (bucket
  `wire_pure_tests`, one ctest entry, whose `093` label T015 already appended): T008's setup under `EXPECT_EXIT(..., ExitedWithCode(0), ...)`;
  `unknown_fields()` returns an empty view and a second call returns the same empty view. RED on base
  if T008 terminated; otherwise a regression guard (mutant in T070).
- [X] T059 [P] [US3] Via `phase-implementer`, the reserve overload cells in
  `tests/wire/offset_table_test.cpp` (bucket `wire_pure_tests`): the new `Parser::parse(frame, mr,
  OffsetTable::Config, std::size_t reserve_entries)` and `OffsetTable` constructor overloads reserve
  up front (a tracking resource sees one entries allocation), a clone and a reify do not copy the
  reserve, and every existing overload reserves nothing. Two more cells for data-model E-3: a reserve
  above `cfg.max_offset_entries` reserves at most that many entries (the tracking resource sees the
  clamped size); and a reserve the resource cannot serve makes the parse report `out_of_memory`, with
  no exception escaping. Size that cell's bounded resource (over a null upstream) so everything the
  parse draws before the reserve fits and the reserve does not, as T008 sizes its resource; on MSVC
  debug the container proxy draws from the same resource at construction (`kContainerSlack`'s reason),
  so a resource too small for it fails outside `build`'s catch and measures the proxy, not the
  reserve. Show with the tracking resource that the failed request is the reserve's. RED: does not compile.
- [X] T060 [US3] Via `phase-implementer`, Q-33 C++ arms in
  `tests/session/inbound_frame_dispositions_test.cpp`: lazy reads at headroom exhaustion inside a
  callback over a dense frame: `group_slices()` gives an empty span and `unknown_fields()` an empty
  view; the session stays Active and processes the next frame. The assertions branch at runtime on the
  library's own condition, `fixpp::detail::arena_upstream() == std::pmr::null_memory_resource()`, and
  no arm is skipped:
  - **null-upstream lanes**: the reports above;
  - **the forwarding lane** (MSVC debug, where the spill witness forwards and records): each lazy read
    succeeds (a non-empty span and view for the frame's group and unknown fields), the spill witness
    records the spill (read as T054's no-spill assertion reads it), and the session stays Active and
    processes the next frame (quickstart Q-33, FR-011). T069 and T113 run this branch. On the base it
    is a regression guard for the reads (they already succeed on MSVC debug) and compile-RED for the
    spill assertion (no witness exists); its mutant, the witness forwarding without recording, is
    run in T113.
  No C cursor shell arm (L-17, fixpp#541). The C arms are T091's. RED: the `unknown_fields()` arm
  terminates if T008 confirmed #540; the other arms are regression guards pinning today's reports.
  - **Base RED method for the `unknown_fields()` arm.** It runs under `EXPECT_EXIT(...,
    ExitedWithCode(0), ...)`, so a terminate on the base is a recorded failure of that arm, not a
    crashed binary that hides the other arms. On the base the callback parses in the 16 KiB stack
    arena, not B(L), so the frame that exhausts the headroom differs: the base run uses a
    base-compilable scratch variant whose density is chosen to exhaust that arena after the parse (run
    as T026's is). If T008 did not terminate, the arm is a regression guard (mutant in T070).

### Implementation for User Story 3

- [X] T061 [US3] Via `phase-implementer`, derive and record the constants (data-model E-2), in a
  scratch measurement in a scratch copy made with `git archive HEAD | tar -x -C <scratch>`:
  `kAlignPad` (the alignment of the entry and overlay blocks inside one monotonic resource) and
  `kCallbackReadHeadroom`. Check `kCallbackReadHeadroom`'s sizing condition verbatim: "for every frame
  the base delivers, B(L) minus that frame's up-front reserve and its parse leaves at least the room
  the base's stack arena leaves after the same parse", at L = 64 KiB and at the OD-2 floor. Record the
  values, the measurement and the condition's result in `research.md` R-3 and under `## Constants`.
  Where the condition fails, record it for L-17 (T105).
- [X] T062 [US3] Via `phase-implementer`, the reserve threading (E-3) in `include/fixpp/wire/parser.hpp`,
  `include/fixpp/wire/offset_table.hpp` and `src/wire/offset_table.cpp`: a public overload
  `Parser::parse(frame, mr, OffsetTable::Config cfg, std::size_t reserve_entries)`; a private,
  tag-dispatched `MessageView` constructor carrying the reserve; a public `OffsetTable` constructor
  overload `(frame, mr, cfg, hooks, reserve_entries)` passing it to the private `build`. The table does
  not store it; clones and reifies do not copy it. `build` clamps the reserve to
  `cfg.max_offset_entries`, and a reserve the resource cannot serve takes `build`'s existing
  `bad_alloc` catch to `out_of_memory` (data-model E-3). T059 GREEN.
- [X] T063 [US3] Via `phase-implementer`, the per-session parse buffer (E-2) in
  `include/fixpp/session/session.hpp` and `src/session/session.cpp`:
  - "With N(L) = ⌊L/3⌋ + 1": "B(L) = 12·N(L) + 4·`overlay_cap_for`(N(L)) + `kAlignPad` +
    `kCallbackReadHeadroom` + `kContainerSlack`", each constant named with T014/T061's value and its
    recipe in the comment;
  - `std::span<std::byte> inbound_parse_buf_`, `mutable`, allocated once in `open()` from
    `session_arena_`; `bad_alloc` is an `open()` error; released at destruction;
  - `parse_and_dispatch_` and `validate_inbound_` build a fresh `monotonic_buffer_resource` over that
    span per call, upstream the spill witness;
  - "`OffsetTable::Config::max_offset_entries = N(L)` for every inbound parse. Each parse reserves
    `min(N(L), frame.size()/3 + 1)` entries";
  - every late inbound site in T012's population moves to the buffer; the inbound stack arenas go;
    the admin and outbound sites keep theirs (C-3 I-6); 092's late close stays as the defence
    (FR-014);
  - C-3 I-3 (one buffer is safe because no inbound parse nests inside another): before relying on it,
    find the callback-scope assertion against nesting at this head
    (`grep -n "assert\|nest" src/session/session.cpp` around the callback scope) and record it. If it
    is missing, or no cell can trip it, add a death-test cell in
    `tests/session/inbound_frame_dispositions_test.cpp` that re-enters an inbound parse from a
    callback and dies, written RED first.
- [X] T024 [US3] Via `phase-implementer`, the FR-013 group's first part: the carry allocated at `open()`
  (E-2, OD-13), as data-model E-2 states it:
  1. "inside a `try`, allocate one block of L + the read size + `kContainerSlack` from
     `cfg_.framer_carry_arena` (else `new_delete_resource()`). A `bad_alloc` is an `open()` error";
  2. "build a Session-owned `monotonic_buffer_resource` over exactly that block, whose upstream is the
     same spill witness as the parse buffer's (null on every lane except MSVC debug, where it forwards
     and records)";
  3. "build the `pmr_carry_buffer` over that resource. Its `noexcept` constructor reserves L + the read
     size, which the block serves, so the reserve cannot fail."
  The block, resource and carry are released at destruction in reverse order. The seam (T023) gains
  the borrowed carry; `run_read_pump` (`src/session/engine.cpp`) borrows it in place of its local
  carry. Update any size pin from T012. T055a's Q-12 and Q-14 (carry half) GREEN, with T064 in the same
  commit group (the Framer limit becomes L there).
  - **Carried from Phase 3:** the L-1 pins read `detail::kReadPumpCarryCapacity`
    (`src/session/read_pump.hpp`). When the carry moves to `open()`, switch them to the seam.

- [X] T022a [US3] Via `phase-implementer`, the FR-013 group's second part: `Engine::register_session`
  (`src/session/engine.cpp`) and `Session::open()` (`src/session/session.cpp`) refuse an advertised 383
  under 4096 or over 262144 with `invalid_session_config`. Both bounds move here together: they are one
  check, and Q-13's 383 half covers both. Every test T012 found configuring 383 outside
  [4096, 262144] moves to a value inside it, each classified in the evidence file; `test_070`'s size
  cells are T055's. T055a's Q-13 383 cells GREEN.
- [X] T064 [US3] Via `phase-implementer`, the FR-013 group's third part, in one commit group with T024
  and T022a, with T055 and T055a already RED:
  - the pump's Framer and the first-frame Framer run `max_frame_bytes = L` (the pump reads it through
    the seam; the accept loop computes `inbound_limit_for` from the registry entry's `SessionConfig`),
    so the first-frame Framer is `{max = L, resync on}` (OD-11);
  - once a Session exists, a `wire_frame_too_large` closes with `close(terminal)`, the event the
    terminal close records and a log record (no `SessionEvent` alternative beyond E-5's). The record is
    a `FIXPP_SLOG` with the session's `trace_context` (`[const §XIII.3]`), its format string registered
    in `src/log/format_registry.cpp`, and carries the failure kind and L (data-model E-12). It needs no rate bound: the close is terminal, so it fires at most
    once per connection. On the acceptor's first frame no Session exists: the transport closes with no
    event and no log (FR-013, L-6);
  - the Active-only advertised-MaxMessageSize check above the state switch is deleted (C-2); header
    comment naming 093 superseding 070's pre-establishment exemption.
- [X] T065 [US3] Via `phase-implementer`, FR-015 after T008 is recorded: in
  `MessageView::unknown_fields()` (`include/fixpp/wire/parser.hpp`), keep `noexcept`; on `bad_alloc`
  clear `unk_items_`, keep the built flag set so later calls return the same empty view, and return an
  empty view.
- [X] T066 [US3] Via `phase-implementer`, the pinned tests R-3 names, and every member of T012's
  `kInboundParseArena` / `default_max_offset_entries` population, each classified in the evidence file
  into one of three classes:
  - **a mirror of the session's private 16 KiB inbound arena**, which T063 deletes: re-based on the
    derived formula, or the mirror deleted with its "like production" / "matches the dispatch arena"
    claim. Leads, which T012's command re-derives: `tests/session/test_066_arena_fit_test.cpp` (its private copies
    of the arena sizes), `tests/alloc_guard/test_dict066_grouped_read_alloc_guard.cpp` and
    `tests/alloc_guard/test_validate_gate_alloc_guard.cpp`, plus the comments that cite the constant in
    `tests/session/test_validate_gate_inbound.cpp` and `tests/session/CMakeLists.txt`. A member T012's
    command adds is a planned edit. Each edited alloc-guard source runs under its plain entry and its
    `*_mallocnesia` twin: both get the APPEND label and a T011 manifest line, and the twin is re-run
    under the interceptor;
  - **a wire-level user of `default_max_offset_entries`**, a constant 093 does not change (the session
    passes N(L) through `OffsetTable::Config`): no change, recorded as such;
  - **a session cell sized against the default entry cap** (`kLateFillerFields` in
    `tests/session/unparseable_frame_disposition_test.cpp`): re-based by T057 onto the shrink.
  And `engine_readpump_test.cpp`'s oversize body still exceeds L (re-checked, not assumed).
- [X] T067 [US3] T054–T060 and T055a GREEN; `ctest -L '^093$'`, which includes T044's
  `alloc_guard_093_pump_active_read` (the parse-buffer change must keep Q-19 at zero); full `session`,
  `wire`, `capi` and `alloc_guard` suites unfiltered on `linux-clang-debug`. Then T054's `open()` block
  measurement.
- [X] T068 [US3] FR-012's stack bound: build the inbound parse sites with `-fstack-usage` on the T005
  base worktree and on this head, and show no function in T012's late-site population grows its stack
  frame. Record both outputs under `## Measurements`.
- [X] T069 [US3] On the MSVC debug sandbox (T006's lock): Q-11 (dense L, peak ≤ B(L)), Q-14 (both
  halves: the carry half must hold where the container proxy draws on T024's block) and the spill
  witness, which on MSVC debug forwards and records; at the design point it
  records nothing. Before believing "records nothing", show it records a spill on this lane: feed a
  frame denser than B(L)'s design point through T057's `session_test_access` shrink accessor. Also run
  T060's forwarding-lane branch of Q-33 (T091's C branch runs in T113). Record under `## MSVC`.
- [X] T070 [US3] US3 instruments and mutants, each in a scratch copy: the spill witness records a spill
  when a frame denser than B(L)'s design point is fed through the test-access shrink; restore the
  16 KiB arena and the default entry cap → Q-11 RED; delete the capacity derivation (B(L) back to a
  fixed size) → Q-11 RED with the late close firing (FR-014); delete the late close → Q-15 RED; remove
  `unknown_fields()`'s catch → Q-32, Q-33 RED. Record under `## Mutants`.

**Checkpoint**: US3 complete. The C arms of Q-33 and Q-37 rows 3–4 close in Phase 8.

---

**Phase 5 as landed (2026-10-04; `ac0e75b2` tests RED, `cdb55f62` production, `9b70b86f`, `14df17c8`,
`0051b7b6`, `ef0d5ba8`, `500a3c8d`):**
- The constants and the measurements are in research R-3 (plan OD-23, OD-24).
- `src/session/parse_capacity.hpp` holds N(L) and B(L). It is split from `inbound_limit.hpp`, so the
  dict066 alloc guard does not pull asio. Its `overlay_cap_for` copy is pinned by equality (R-3).
- `kInboundParseArena` is renamed `kSendParseArena`; the two outbound toApp sites keep it.
- Q-11 shows the peak through the spill witness plus a buffer-size assertion; Q-15's positive control is
  what makes Q-11's "nothing spilled" able to report a spill.
- I-3 is a regression guard, skipped under `NDEBUG`.
- Q-12 samples every 509 bytes plus every one of the last 64.
- MSVC debug (T069) found two of this phase's own cells wrong; both were fixed in `ef0d5ba8`.

## Phase 6: User Story 4: every good inbound frame proves the peer is alive (Priority: P2)

**Goal**: one liveness writer, right after the fault and 35-not-third checks, before every early return
(FR-020, FR-021, C-5).

**Independent Test**: per early-return class, a peer silent apart from that class for longer than
HeartBtInt draws no TestRequest.

- [ ] T071 [US4] Via `phase-implementer`, Q-20 on the mock clock with `run_liveness_cell` in
  `tests/session/unparseable_frame_disposition_test.cpp` (second home
  `tests/session/heartbeat_testrequest_test.cpp`, APPEND label `093`), one cell per SC-005 class: one
  too-high frame (exactly one: a second non-PossDup, non-Heartbeat too-high frame is fatal, fixpp#537, reproduced in B28); a Reset-mode SequenceReset; a GapFill; the
  validate and PossDup Rejects (#423 sites); a too-low Heartbeat and a too-low PossDup frame; the
  knob-off path; an inbound Reject(35=3). No TestRequest within the interval of that frame. RED: a
  TestRequest is sent.
- [ ] T072 [US4] Via `phase-implementer`, Q-21 in the same file: garbled frames (C-1 and C-2 step 1)
  and faulty frames do not refresh. Regression guards (mutant in T074). Rewrite that file's liveness
  pin comment as a condition (R-8).
- [ ] T073 [US4] Via `phase-implementer`, in the LogonReceived/Active arm of `src/session/session.cpp`:
  one unconditional `last_inbound_steady_ = effective_clock_->steady_now()` after the fault check and
  the 35-not-third check, before the validate gate and every early return; delete the old writer at
  the end of the arm. The seeds at `open()` and at entering Active stay. Re-run T012's writer grep and
  record it. T071 GREEN, T072 green.
- [ ] T074 [US4] The full session suite unfiltered (R-5: no cell is known to pin "too-high does not
  refresh"). Mutants in a scratch copy: restore the old writer → Q-20 RED; refresh garbled or faulty
  frames → Q-21 RED. Record under `## Mutants`.

**Checkpoint**: US4 complete. Q-37 row 6 closes in Phase 8.

---

## Phase 7: User Story 5: a closing session acts on nothing more (Priority: P3)

**Goal**: #523 (P5) then #524 (P6). FR-030 (T077) lands before FR-041 (T084).

**Independent Test**: per role, `Logon ‖ second frame` with a graceful close during the hydrate or
peer-reset yield acts on nothing (#523); a close or `Engine::stop()` inside the 141=Y unit leaves the
durable counters at FR-041's table (#524).

### Shared test store (before T075)

- [ ] T074a [US5] Via `phase-implementer`, tests only, GREEN on the base: move `HookedStore` out of
  `tests/session/test_session_plaintext_roundtrip.cpp`, where it is `final` and file-local, into
  `tests/support/hooked_store.hpp`, so T082's separate executable can use it. The move carries what
  the class needs (`StoreLog`, `kHoldBound`, `HookedStoreFactory`); `flush_thunk_for` is the library's.
  Behaviour unchanged: the file's cells stay GREEN, re-run unfiltered. The `reset_to` modes are added
  by T084, once the operation exists:
  - **forward mode**: its `reset_to` override forwards to the inner `MemoryStore`'s `reset_to` and
    fires its hooks there (Q-23, Q-24 and T080's forwarding cell);
  - **default-body mode**: its override calls the base `MessageStore::reset_to`, so the default body
    runs over `HookedStore`'s own `reset()` and `next_seqnum()` and their hooks. Q-25 (T081) and Q-26
    (T082) use this mode; T088's delete-the-shield mutant is run against it.

### #523 (P5): tests first

- [ ] T075 [US5] Via `phase-implementer`, after T074a, Q-22 per role in
  `tests/session/test_session_plaintext_roundtrip.cpp` (`LogonCloseDuringSuspension`, `HookedStore`,
  `CaseRig`; APPEND label `093`): the Logon and a dictionary-invalid second frame coalesced in one
  write, `validate_inbound_messages = true`, a graceful close during the hydrate or the peer-reset
  yield; no `toAdmin`, Reject or state write after close began (`expect_no_admin_after_close`). The
  flush's post sequence is replaced by a bounded hold. The LogoutSent confirmation cell stays green
  (regression guard). RED: the Reject reaches `toAdmin`.
- [ ] T076 [P] [US5] Via `phase-implementer`, Q-9 in `tests/session/inbound_frame_dispositions_test.cpp`:
  a 35-not-third frame after `close()` began, in NotConnected and in LogonSent, is counted and
  evented, with no other effect. RED: it is processed.
- [ ] T077 [US5] Via `phase-implementer`, C-2 step 2 in the NotConnected and LogonSent arms of
  `src/session/session.cpp`, right after the scan and step 1: `if (state_ == lifecycle::closing &&
  (fsm_state_ == NotConnected || fsm_state_ == LogonSent)) return success;`. LogonReceived, Active
  and LogoutSent are unchanged. T075 and T076 GREEN.
- [ ] T078 [US5] #523 mutants in a scratch copy: delete the guard → Q-22 RED; put it before step 1 →
  Q-9 RED. Record under `## Mutants`.

### #524 (P6): tests first

- [ ] T079 [US5] Via `phase-implementer`, Q-28 and Q-29:
  - Q-28: `reset_to` with a target outside {1, 2} is refused with `session_invalid_argument` and no
    effect, for the default body (a test-local non-overriding store), in
    `tests/session/test_memory_store_round_trip.cpp` for `MemoryStore`, and in
    `tests/session/test_file_store_crash_survival.cpp` for `FileStore` (APPEND label `093` on each);
  - Q-29 in `tests/session/test_file_store_crash_survival.cpp`: fault injection between the temp write
    and the rename, then a restart, sees the old or the new counters, never a partial (1, 1).
  RED: does not compile (no `reset_to`).
- [ ] T080 [US5] Via `phase-implementer`, Q-23 and Q-24 per role in
  `tests/session/test_session_plaintext_roundtrip.cpp`:
  - Q-23, no teardown reset: `close()` drains inside the unit; the durable state equals the unit's
    targets; the peer's next Logon at 34=2 without 141=Y is accepted with 789 tolerance off. RED:
    NextNumIn is stranded at 1;
  - Q-24, with a teardown reset: the final durable state is (1, 1). Green on base (regression guard,
    observed by the durable store counters).
  - `HookedStore` in forward mode (T074a) forwards `reset_to` to its inner store and fires its hooks
    there, with a cell that fails when the forwarding is removed (quickstart §2). Q-23 and Q-24 use
    forward mode.
- [ ] T081 [US5] Via `phase-implementer`, Q-25 and Q-27 in the same file: Q-25, a non-overriding
  (default-body) store, `HookedStore` in default-body mode (T074a), with and without a teardown reset
  meets the table (mutant in T088); Q-27, the
  `close()` wait expiring records `session_event_close_reset_wait_expired` and `close()` completes,
  and for `FileStore` (1, 1) still holds after expiry (the FIFO writer-lock condition measured, not
  assumed); and Q-27's re-arm arm, a clock-wide `cancel_sleeps()` from another session during the
  wait records no expiry event before `effective_clock_` reaches the bound.
- [ ] T082 [US5] Via `phase-implementer`, after T074a, Q-26 in a new
  `tests/session/engine_reset_unit_stop_test.cpp`, executable `engine_reset_unit_stop_test`, registered
  standalone beside `engine_lifecycle_test` in `tests/session/CMakeLists.txt` as ctest
  `engine_reset_unit_stop`, labels `"093;session"`: `Engine::stop()` begins during the unit, for
  `MemoryStore`, `FileStore` and `HookedStore` in default-body mode (`tests/support/hooked_store.hpp`,
  T074a), with and without a teardown reset. The cell holds the store operation,
  so stop's step-1 handler runs first. The table holds, and per role no `toAdmin`, no reset event, no
  `onLogon` and no Active transition is observed after stop's step 1 has run on the session's strand
  (the application double's callback log and the event ring). RED: the unit is interrupted, leaving
  the pre-unit state (default body and `FileStore`).
- [ ] T083 [US5] Via `phase-implementer`, re-derive the `*StoreEndsAtTeardownReset` and
  `close_from_*_persist` cells (T012) for the new unit shape, each classified in the evidence file.

### #524 (P6): implementation

- [ ] T084 [US5] Via `phase-implementer`, `MessageStore::reset_to` (E-9) in
  `include/fixpp/session/message_store.hpp`: "`virtual asio::awaitable<core::expected_t<void>>
  reset_to(seqnum_t next_in, seqnum_t next_out) noexcept;`", non-pure; precondition `next_in, next_out
  ∈ {1, 2}`, else `session_invalid_argument` with no effect; default body `co_await reset()`, then
  `next_seqnum(dir, true)` once per target that is 2, first error returned. The header's pure-virtual
  count comments become conditions (`[const §XIV.2]`, re-derivable), and `reset_to`'s doc comment
  states that the vtable changes, so C++ code built against the previous header must be rebuilt
  (C-7 row 13), and when an implementer should override it (plan.md "What changes for whom"). Overrides: `MemoryStore`
  (`include/fixpp/session/memory_store.hpp`), one critical section clearing and setting both counters,
  keeping `++generation_`; `FileStore` (`include/fixpp/session/file_store.hpp`,
  `src/session/file_store.cpp`), the counters committed by the rename (parametrise `initialise_fresh`
  or append a counter record before the rename), in the POSIX and Windows temp branches, Region 3 and
  the `operation_aborted` catch. `HookedStore` (`tests/support/hooked_store.hpp`) gains T074a's two
  `reset_to` modes, forward and default-body, chosen per store at construction. T079 GREEN.
- [ ] T085 [US5] Via `phase-implementer`, the unit (C-6 steps 1–7), both roles, in
  `src/session/session.cpp`: targets per research R-6; `co_await
  this_coro::reset_cancellation_state(disable_cancellation{})`; the manager set (`reset_to_one()`,
  `set_next_inbound(in)`, `set_next_outbound(out)`), capturing the first error without returning;
  only on success `reset_unit_in_flight_ = true; r = co_await store_->reset_to(in, out);`, then clear
  the flag and signal completion (E-10: no poll; e.g. a `steady_timer` at `time_point::max()`
  cancelled by the unit); restore `enable_total_cancellation()`; the existing dispositions after the
  restore; then `logon_arm_superseded`. True targets on volatile stores too (OD-9). The in-unit
  `teardown_reset_done_` stops are removed, the flag kept as `close()`'s latch, with a header comment
  naming 093 superseding #518's in-unit stops.
  - **C-6's three conditions for "any point of the unit".** Re-derive each at this head and record
    the command, its output and the verdict under `## Populations`: (1) step 2's and step 5's
    `reset_cancellation_state` complete without suspending (read asio's `impl/awaitable.hpp` in the
    Conan-resolved asio the build uses); (2) step 3's lock grants inline on its uncontended fast path
    (L-518-1's condition; read the `async_lock` the manager setters take); (3) steps 5 to 7 contain no
    other `co_await` (`grep -n "logon_arm_superseded\|co_await" src/session/session.cpp`, read against
    the unit's span). A condition that fails stops the task: C-6's one-interleaving-point argument, and
    the cells that hold the store at step 4 (Q-23, Q-26), no longer cover the unit.
- [ ] T086 [US5] Via `phase-implementer`, the engine-stop flag (E-13, OD-15): `bool
  engine_stop_requested_` on the Session, written and read on its strand; `note_engine_stop_()` on
  `src/session/session_engine_access.hpp`; `Engine::stop()`'s step 1 (`src/session/engine.cpp`) reads
  `entry.session` on the control strand and sets the flag inside its existing `co_spawn`, before the
  emit (a null session has nothing to set, on E-13's condition); `logon_arm_superseded` tests
  `state_ == closing`, the flag and the FSM state. Re-derive the predicate's sites against each Logon
  arm's `co_await` sites (T012's command) and record that every suspension is followed by the
  predicate before the next effect.
- [ ] T087 [US5] Via `phase-implementer`, `close()`'s wait (OD-1): only when it is about to issue its
  teardown reset and `reset_unit_in_flight_` is set, await the completion signal raced against
  `await_deadline(*effective_clock_, now + logon_timeout_ms)`; on expiry record the appended
  `session_event_close_reset_wait_expired { }` and proceed. No wait and no teardown reset when neither
  `reset_on_disconnect` nor `reset_on_logout` (after a Logout) holds.
- [ ] T088 [US5] T075–T083 GREEN. FR-042: build every `MessageStore` subclass T012 found under
  `-Werror` unchanged, and record the population. US5 mutants in a scratch copy: delete `close()`'s wait
  → Q-25 RED; delete the shield → Q-26 durable counters RED, shown on the default-body `HookedStore`
  cell and the `FileStore` cell (SC-006 names those two); drop the engine-stop flag from
  `logon_arm_superseded` → Q-26 per-role effect assertions RED; make `reset_to`'s override non-atomic
  (reset, then advance) → Q-29 RED; accept any target → Q-28 RED; expire without recording → Q-27 RED;
  make `close()`'s wait a one-shot sleep without `await_deadline`'s re-arm → Q-27's re-arm arm RED;
  remove `HookedStore`'s forwarding in forward mode → its cell RED. Record under `## Mutants`.
- [ ] T089 [US5] fixpp#538's reproduction (OD-9), on this head and on the T005 base worktree. Record both
  outputs. No closing keyword names #538 whatever the result; report it to the owner.

**Checkpoint**: US5 complete; FR-030 landed before FR-041.

---

## Phase 8: Public surface and versioning (P7; C-7)

**Purpose**: the C setter and getter, Python, TOML, the symbol golden, freeze hashes and reentrancy
tokens, one MINOR bump, and BREAKING per declaration. It closes the C/Python/TOML arms of US1 and US2
and every C-7 row 1–6 C witness.

- [ ] T090 [P] Via `phase-implementer`, the new `tests/capi/inbound_frame_dispositions_capi_test.cpp`,
  registered in `tests/capi/CMakeLists.txt` as ctest `capi_inbound_frame_dispositions`, labels
  `"capi;093"`:
  - Q-30: `fixpp_session_config_set_logon_timeout_ms` refuses a null handle and zero; 
    `fixpp_session_garbled_frame_count` refuses a null handle and a null `out`, writes 0 before the
    session exists, and the count after a garble (Q-1's C arm);
  - Q-16's C arm: a timeout set through the setter is honoured at T. The C ABI has only a real-time
    clock (`fixpp_engine_config_set_realtime_clock`), so the arm uses a timing band on wall time: an
    initiator with T = 500 ms set through the setter, against a loopback peer that accepts the TCP
    connection and never answers the Logon (phase b). The close is asserted at an elapsed time
    ≥ T, measured from a steady-clock stamp taken before the engine starts (it precedes the deadline's
    start, so a correct build cannot fail the lower bound), and < 5 s. The upper bound is derived from
    the competing timeout, not from expected latency: half the 10 s default, so a setter that is
    ignored closes at the default and fails, while a slow lane keeps seconds of headroom. Before
    fixing the band, check at this head that no other close source can fire inside it on a pre-Active
    initiator;
  - the getter's "thread-safe" token: one thread other than the engine's calls
    `fixpp_session_garbled_frame_count` in a loop while the session counts garbles on its strand; the
    reads never decrease and the last equals the count. It runs on `linux-clang-tsan` (T118), where a
    race report fails it.
  RED: the symbols do not exist.
- [ ] T091 Via `phase-implementer`, Q-33's C arms in `tests/capi/inbound_frame_dispositions_capi_test.cpp`:
  inside a C callback over a dense frame at headroom exhaustion, `fixpp_group_get_nested_group` returns
  `FIXPP_ERR_WIRE_LIMIT_EXCEEDED` and `fixpp_msg_get_group` returns `FIXPP_ERR_TYPE_MISMATCH`; the
  session stays Active. These are the null-upstream lanes' assertions; the cell branches on T060's
  runtime condition and skips no arm. On the forwarding lane (MSVC debug) both calls return
  `FIXPP_ERR_OK`, the spill witness records the spill (read as T054's no-spill assertion reads it),
  and the session stays Active (quickstart Q-33). T113 runs that branch. Regression guards pinning
  today's reports (L-5).
- [ ] T092 [P] Via `phase-implementer`, Python RED in the new
  `bindings/python/tests/test_093_inbound_frame_dispositions.py`: the setter (zero raises the typed
  exception), the getter returning an int, and Q-16's Python arm; both wheel lanes (LP64 and Windows
  `uint64_t`). Q-16's Python arm uses T090's band and its derivation: T = 500 ms set through the
  binding, a peer that never answers the Logon, the close at an elapsed time ≥ T (from a stamp taken
  before the engine starts) and < 5 s (half the 10 s default).
- [ ] T093 [P] Via `phase-implementer`, Q-31 and Q-16's TOML arm: `logon_timeout_ms` accepted as a bare
  integer in `tests/config/test_load_happy_path.cpp` with `tests/config/fixtures/happy_full.toml`; zero,
  a negative value, a value above `UINT32_MAX` and a non-integer each refused with a diagnostic in
  `tests/config/test_load_negative_battery.cpp`; a TOML-loaded session honours T. RED: the key is
  unrecognised.
- [ ] T094 [P] Via `phase-implementer`, `tests/capi/version_test.cpp`'s exact-version cell and
  `CompositeMacroValue` set to T003's new MINOR. RED against the current MINOR.
- [ ] T095 Re-run T003's two checks immediately before T099's commit; a release or a branch claiming the
  same MINOR stops the bump.
- [ ] T096 Via `phase-implementer`, the setter in `include/fix/c_api/session.h` and `src/capi/config.cpp`,
  following `fixpp_session_config_set_heartbeat_seconds`: "`fixpp_session_config_set_logon_timeout_ms(fixpp_session_config_t*,
  uint32_t ms)`"; null handle → `FIXPP_ERR_NULL_HANDLE`, zero → `FIXPP_ERR_CAPI_CONFIG_INVALID`;
  reentrancy token "single-thread".
- [ ] T097 Via `phase-implementer`, the getter in `include/fix/c_api/session.h` and `src/capi/session.cpp`:
  "`fixpp_error_t fixpp_session_garbled_frame_count(const fixpp_session_t*, uint64_t* out)`". Its
  refusal order follows `fixpp_session_is_established` (the out-parameter checked for null, `*out`
  written 0, then `check_session`); its read follows `fixpp_session_close`'s scoped
  `engine_->lookup(id)` lease, released before return (`fixpp_session_is_established` reads the
  handle's slot and takes no lookup, so it is not the pattern for the read). A null lookup leaves 0
  written; reentrancy token "thread-safe".
  T090 GREEN.
- [ ] T098 Via `phase-implementer`, Python and TOML:
  - `bindings/python/fixpp.i`: a `%apply … *OUTPUT { uint64_t* … }` for the getter's out-parameter,
    and a row in the "PY-002 GIL-DISCIPLINE AUDIT TABLE" (locate with `git grep -n "GIL-DISCIPLINE
    AUDIT TABLE" -- bindings/python`), and the matching entry in `bindings/python/tests/_gil_staging.py`
    and `bindings/python/tests/wheel/_gil_staging.py` if those lists cover the new wrapper; the setter
    comes through the existing `%include`;
  - TOML: `logon_timeout_ms` in `kRecognized` (`src/config/toml_config_loader.cpp`) and a mapper in
    `src/config/scalar_mappers.cpp`: an integer with 0 < v ≤ `UINT32_MAX`; a non-integer refused with
    `reason_class::malformed_value`; zero, negative or out of range refused with
    `reason_class::out_of_range` (data-model E-7). `logout_disconnect_timeout_ms`'s mapper accepts
    zero and ignores a non-integer without a diagnostic, so neither its range check nor its type
    handling is copied.
  T092 and T093 GREEN.
- [ ] T099 Via `phase-implementer`, the version (C-7 "Version"): `FIXPP_C_ABI_VERSION_MINOR` +1 in
  `include/fix/c_api/version.h`, with a history entry naming 093 as contract C-7 "Version" states it:
  headed BREAKING, rows 1–6 in one summary line each pointing to their declarations, rows 7–8 as
  additions, and an effect detailed only where no declaration carries it. Update every in-repo
  consumer of the old value: `git grep -ln "VERSION_MINOR" -- . ':!specs'` plus the encoded forms of the
  old value that the recipe in `specs/092-garbled-frame-reject/tasks.md` (its version-bump task) greps for, each hit classified in the evidence file. T094 GREEN.
  Mutant in a scratch copy: MINOR back to the old value → `version_test` RED.
- [ ] T100 Via `phase-implementer`, BREAKING per declaration (C-7, `[const §X.7]`). Re-derive the
  observer set from `version.h`'s 1.10 entry and the 1.10 sites with `grep -n "C-ABI 1.10"
  include/fix/c_api/session.h`.
  - Rows 1–6: a BREAKING clause naming the new MINOR and 093 on each observer's doc block in
    `include/fix/c_api/session.h`.
  - Amend in place, marked with the new MINOR, the 1.10 text beginning "a Logon carrying a malformed
    tag" at all its sites (`close`'s paragraph and the bullet lists on `is_established`,
    `fixpp_session_send`, `register_callback` and `fixpp_session_register_send_callback`).
  - Amend the 1.10 bullet beginning "on an established session, a frame the header scan finds
    fault-free but the session cannot parse for dispatch" on `is_established`, `fixpp_session_send`,
    `register_callback` and `fixpp_session_register_send_callback`.
  - Leave the bullet beginning "on an established session, a faulty frame whose fault comes before its
    MsgSeqNum(34)" unchanged (it stays true).
  Never run a formatter on `include/fix/c_api/*.h`.
- [ ] T101 Via `phase-implementer`, the gates: the two symbols in `tests/abi/golden/fixpp_capi_symbols.txt`
  (sorted); `tools/capi_freeze.sha256` re-pinned for `session.h` and `version.h` with
  `tools/check_capi_freeze.sh`; `tools/check_capi_reentrancy.sh` passes with one token per doc block;
  the `nm` audit shows no C++ symbol leaks through the C ABI (`[const §X.2]`).
- [ ] T102 Via `phase-implementer`, Q-37 in its own source file, `tests/capi/inbound_frame_dispositions_c7_witness_test.cpp`,
  linked into `capi_inbound_frame_dispositions` (so it can be copied alone into the base tree): for
  each C-7 row 1–6 trigger (row 1 a garbled frame in Active; row 2 a 35-not-third frame in Active and a
  35-not-third Logon; row 3 a frame of 64 KiB split at the carry edge; row 4 a dense frame of 64 KiB;
  row 5 a pre-Active peer past T; row 6 liveness-only traffic past HeartBtInt with the TestRequest
  unanswered), assert through C: `fixpp_session_is_established`, the result of `fixpp_session_send`,
  whether the callbacks registered with `fixpp_session_register_callback` and
  `fixpp_session_register_send_callback` fire, and the result of `fixpp_session_close`. Written to
  compile against the merge-base API where it can (row 5 uses the default T if the setter is the only
  new symbol it needs), built in a debug configuration in a scratch copy of the base
  (`git -C <T005 base worktree> archive HEAD | tar -x -C <scratch>`, a fresh `mktemp -d` path; owner
  ask), never in the T005 worktree itself, whose release build T112 reuses, to show each row RED for
  its stated reason, then GREEN here.
- [ ] T103 Run every C-ABI and Python cell: T090–T094 and T102 GREEN; `pytest bindings/python/tests/` on
  both `-py` lanes; `capi_*` ctest unfiltered.
- [ ] T104 Confirm the arms this phase closes for US1 (Q-1 C arm) and US2 (Q-16 C, Python, TOML arms)
  are GREEN, and record them against their stories in the evidence file.

**Checkpoint**: the C-ABI MINOR is declared with BREAKING on every observer; C, Python and TOML
surfaces exist and are witnessed.

---

## Phase 9: Polish and cross-cutting (P8 docs, then P9)

### Docs (P8): orchestrator markdown; `phase-implementer` for code comments

- [ ] T105 [P] `spec/behaviors-and-limitations.md` (FR-050; FR-050's list is a lead, not the
  population). First grep the live file and classify every hit:
  `grep -nE "L-004-4|B-005-7|B-004-1|B-041-1|L-092-6|L-518-1|B-092-2|B-092-9|L-092-1\b|D-8|D-9|D-1/D-2|#51[456]\b|#52[34]\b|#540|373=14|kInboundParseArena|Until #514 ships|session-fatal" spec/behaviors-and-limitations.md`.
  Then:
  - move `L-004-4` to `spec/behaviors-and-limitations-closed.md`;
  - narrow `B-005-7` to fields other than the first three, and correct its "QF emits 373=14" (R-1);
  - correct `B-004-1`, `B-041-1`'s 373=14 for this shape, and 092's §4.5.2 note "Until #514 ships…";
  - add B rows for FR-001…FR-041 and for C-7 rows 1–6 BREAKING (`[const §X.7]`), with the 383 and
    zero-timeout refusals as C++ only; FR-040's row states the rebuild requirement (C-7 row 13), and
    the `SessionEvent` row states C-7 row 11's C++ source change;
  - update the 092 rows the grep finds stating D-8's disregard or the pre-Active refusal of a faulty
    frame whose third field is not 35, and the D-9 (LogoutSent) row, whose frames whose third field is
    not 35 stay disregarded and are now counted, evented and logged (C-2; FR-050);
  - add one L row per contract C-8 L-1…L-17 (L-17 names fixpp#541, unconfirmed, B28; it also records
    T061's `kCallbackReadHeadroom` condition result where it failed; L-13 cites T051's measurement);
  - add the B row for the per-session cost: the carry plus B(L) as the formula, with the 64 KiB and
    256 KiB totals re-derived from the measured constants (SC-007), and the admission bound: at most
    one carry plus B(L) per registered session with a live or establishing connection (FR-050,
    research R-3);
  - update `L-092-6` and `L-518-1`, including the "Frames that arrive with the Logon" bullet, whose
    LogonReceived half becomes by design (FR-050).
  Run `python3 /home/catalin/Work/Programming/Antreprenoriat/.claude/scripts/check_bl_delta.py` and
  `check_bl_citations.py`, and record the delta in the evidence file.
- [ ] T106 [P] `spec/coverage-index.md`: rewrite the §4.5.2 row's sentence that says byte-level framing
  failures stay session-fatal (L-004-4, `FramerFailureClosesEstablishedSession_*`), naming 093's
  witnesses; re-derive any other stale row with
  `grep -nE "L-004-4|FramerFailureClosesEstablishedSession|B-005-7|373=14" spec/coverage-index.md`.
- [ ] T107 [P] `brain/`: `brain/components/session.md`, `inbound-message-path.md`, `wire.md` and
  `message-store-quiescence.md` gain the rulings R-1…R-4, the orchestrator decisions and their rejected
  alternatives (research R-2, R-3, R-4, R-6, R-9), and 093's bundle in each page's document list;
  superseded documents are flagged in place. `brain/log.md` gets an entry. Re-derive any other page
  that describes changed behaviour with
  `grep -rlnE "L-004-4|B-005-7|#51[456]\b|#52[34]\b|garbled|kInboundParseArena|logon_timeout|reset_to" brain/components`,
  and rewrite (not append to) each stale passage. `python3 tools/check_brain.py gate` passes.
- [ ] T108 Via `phase-implementer`, header comments naming 093 where a decision is superseded, those not
  already written by T035, T037, T064 and T085: `L-004-4`'s wontfix, 092's D-8, #518's in-unit stops,
  070's exemption. Re-derive the sites with
  `git grep -nE "L-004-4|D-8|teardown_reset_done_|wire_framing_resync|pre-establishment|kInboundParseArena" -- src include tests`.

### `/simplify` (P9, before any final measurement)

- [ ] T109 Run `/simplify` over the branch diff (`[const §XVI.7]`); fixes go through
  `phase-implementer`. Every later check runs on the post-simplify head. Re-check T105–T108's claims
  against the post-simplify code.

### Mutation, fuzz, bench, MSVC

- [ ] T110 Re-run quickstart §2's whole mutant list at the post-simplify head, each in a scratch copy
  (T019, T029, T039, T053, T070, T074, T078, T088, T099; T113's witness mutant on the sandbox), with a
  clean-revert `git diff` each. A mutant whose
  cell stays green is a finding, not a pass. Consolidated table under `## Mutants`.
- [ ] T111 Fuzz (`[const §VII.7]`): build `linux-clang-asan` with `FIXPP_BUILD_FUZZ=ON`; run
  `fuzz_wire_framer` (T020's resync arm, counted-work bound asserted per input) and
  `fuzz_transport_read_path` for ≥ 600 s each, after re-showing T020's planted trap fires. Record the
  commands, the corpus and the result under `## Fuzz`, name both targets to `/speckit-verify`, and
  state the #508 caveat (no coverage feedback from library code).
- [ ] T112 Re-run T005's paired A-B-A-B against the post-simplify head, same base worktree and
  procedure: `on_inbound_frame_bench` validation off and on, and two `framer_bench` pairings, both
  against the base's `BM_Framer_Feed_NoCarry` (default `Config`):
  - the head's `BM_Framer_Feed_NoCarry`: the strict path and the always-compiled counters (T017);
  - the head's `BM_Framer_Feed_NoCarry_Resync` (T018a): the pump's production path, compared across
    row names because the resync row cannot exist on the base. This is the delta the pump pays.
  Budget +5 % per case, min-per-tree (`[const §VIII.2]`, SC-007). Over budget → the owner with the per-leg figures; never
  relax it. Record under `## Bench baseline`, then `git worktree remove --force` the base worktree.
- [ ] T113 The MSVC sandbox (T006's lock): rsync the post-simplify head; on MSVC debug and msvc-asan run
  the `093` label, the dense-L, spill and getter-typemap cells (quickstart §3), the Python wheel
  getter, and Q-33's forwarding-lane branch on MSVC debug (T060's C++ and T091's C cells: each lazy
  read past the headroom succeeds, the spill is recorded, the session stays Active; on a lane where
  `arena_upstream()` is null the same cells take their null-upstream branch). Mutant, in a scratch
  copy rsynced to the sandbox: the spill witness forwards without recording → the forwarding-lane
  branch's spill assertion RED on MSVC debug. Record it under `## Mutants` too. Calibrate each new standalone target's TIMEOUT from the slowest lane with headroom. Record
  under `## MSVC`.

### Static analysis, claims, citations, pins

  - **Carried from Phase 4:** `alloc_guard_093_pump_active_read` (T044) is registered only on Linux
    (`if(CMAKE_SYSTEM_NAME STREQUAL "Linux")`; its base count is a Linux measurement). On MSVC, T011's
    manifest gate must expect it absent, and must not report it missing.

- [ ] T114 Run clang-tidy, clang-format, cppcheck and IWYU (`[const §IX.4]`) on every changed file under
  `src/` and `include/`, `tools/` included where changed. Never format `specs/` or
  `include/fix/c_api/*.h`. Findings on changed lines go to `phase-implementer`.
- [ ] T115 `python3 /home/catalin/Work/Programming/Antreprenoriat/.claude/scripts/check-comment-claims.py
  --root <tree> --base origin/main`, `<tree>` the absolute path of the owning tree. Read every hit, and
  the claims the script cannot see: the format strings in `src/log/format_registry.cpp`, the
  `version.h` history, the BREAKING clauses, the constants' recipe comments. A claim recording a result
  is deleted, not replaced.
- [ ] T116 `python3 tools/check_line_citations.py --shift-audit origin/main..HEAD`; a hit on the
  checker's own fixture strings takes the `# citation-ok` pragma.
- [ ] T117 Run every `ci-script-pins` step locally, driven from `.github/workflows/tier1.yml`'s YAML (read
  the job's steps with a YAML parser and run each `run:` block as Actions would), including B25's ODR
  census step (FR-053: no class member gated on `FIXPP_TEST_HOOKS`; `framer_test_access` and every new
  accessor unconditional). Every step must run and pass; read `.steps[]`-equivalent output, not a
  summary. Before trusting the census's pass, show it can fail: via `phase-implementer`, in a scratch copy, gate one member of
  a 093-touched class on `FIXPP_TEST_HOOKS` and show the step reports it, or re-run the census's own
  self-test if B25 ships one.
- [ ] T118 T011's label gate passes (the labelled set equals the manifest, every entry registered);
  `ctest --test-dir build/linux-clang-debug -L '^093$' --output-on-failure` all GREEN; the whole ctest
  suite unfiltered on `linux-clang-debug`, `-asan`, `-ubsan`, `-tsan` and `-release`, one preset at a
  time under the 16 GiB cap, passing except the tests T012's populations list as intentionally updated.
  On `-tsan`, confirm by name that `engine_reset_unit_stop` (T082) and `capi_inbound_frame_dispositions`
  (T090's cross-thread getter cell) ran: their cross-strand and cross-thread interleavings are what
  the TSan lane is for.
- [ ] T119 Via the `checklist-auditor`, re-disposition any checklist item whose subject changed during
  implementation: derive the population by a complement grep over
  `specs/093-inbound-frame-dispositions/checklists/*.md` for every FR, C-row, OD and invariant id the
  evidence file records a deviation against.

### Verify and records (P9)

- [ ] T120 Run `/speckit-verify` (mandatory after `/speckit-implement`, `[const §XVII.8]`). It writes
  `.specify/decisions/093-inbound-frame-dispositions-verify.md`, citing the evidence file:
  - the full preset matrix and the MSVC leg (T113);
  - coverage on `linux-clang-coverage`, `.profraw` purged first: every changed line of `git diff
    --name-only origin/main...HEAD -- src include` covered or assessed line by line (`[const §IX.1]`);
  - the allocation gate (mallocnesia, `[const §VIII.5]`): `alloc_guard_positive_control_mallocnesia` (its
    registration is in `tests/alloc_guard/CMakeLists.txt`) passing and,
    for each `*_mallocnesia` twin the branch touches, evidence the interceptor took effect; Q-19 GREEN;
    L-13's pre-Active count (T051) restated;
  - the fuzz targets of T111.
  The §7 full build needs an owner ASK.
  - **Carried from Phase 5:** the fork-based cells have not run on `linux-clang-asan` or `linux-clang-tsan`.
    They are Q-14's engine `EXPECT_EXIT`, Q-33's `EXPECT_EXIT`, the I-3 `EXPECT_DEATH` and Q-32's `EXPECT_EXIT`
    in `unknown_fields_test.cpp`. TSan's `die_after_fork` is the open risk; this run must show them on both
    lanes.

- [ ] T121 File the follow-ups spec.md "Out of scope" marks "to file" (the admin and outbound parse
  arenas, contract L-14; a lazy `open()` failure being silent in the role loops), with the owner's
  approval, each labelled from `gh label list`, placed in the parent's `phases/phase-4/issue-batches.md`.
  Record the numbers in the evidence file and in the B&L rows that cite them.
- [ ] T122 Via `phase-implementer`, the `CLAUDE-history.md` entry (Article XIX), newest first: the feature,
  the PR, the closing issues, the owner rulings R-1…R-4, the C-ABI MINOR BREAKING list, #540's outcome
  (T008), #538's (T089), and the follow-ups (#534–#537, #541, T121's issues). `CLAUDE.md`'s "Last merged
  FEATURE" pointer changes only at merge.
- [ ] T123 Release the MSVC sandbox lock taken in T006 (close-out row 18), after T113 and T120 have run
  their MSVC legs. Record the release.
- [ ] T124 Draft the PR description (it is opened in T125a, after the last commit):
  - the `[const §X.7]` BREAKING declaration (C-7 rows 1–6), matching the B&L delta, and why #523/#524 are
    not BREAKING (B-518-1's ruling). Before opening, compare the three carriers row by row: for each
    of C-7 rows 1–6, the body's BREAKING list, the B&L delta (`git diff origin/main --
    spec/behaviors-and-limitations.md`) and `version.h`'s 093 history entry each name it. Record the
    three-column table in the evidence file; a row missing from any carrier stops the opening;
  - the public C++ deltas (C-7 rows 7–15 and 18);
  - the bench (T112), fuzz (T111) and #540 (T008) results;
  - `local build: green on linux-clang-debug @ <git-sha>` with the SHA T120 verified (`[const §XVII.7]`);
  - a `## Gates` section citing the Gate A record and the verify record, and a `## Gate B …` heading;
  - closing keywords for #514, #515, #516, #523 and #524, and for #540 only on T008's terminate branch.
    #534–#538 and #541 appear only in sentences with no closing keyword (a keyword fires inside a
    negation). Before opening, run over the body AND `git log origin/main..HEAD --format=%B`:
    `grep -inE '\b(close[sd]?|fix(e[sd])?|resolve[sd]?)\b[: ]+([a-z0-9_.-]+/[a-z0-9_.-]+)?#[0-9]+' | grep -vE '#(514|515|516|523|524)\b'`
    (add `|540` to the exclusion only on the terminate branch), which must print nothing. Positive
    control first: seed `Closes #538` into a scratch copy of the body and show the grep hits it. After
    opening (T125a), `closingIssuesReferences` is exactly the intended set.

### Mandatory close-out tasks (Gate-B preconditions, Article XVII §8)

- [ ] T125 **Catalogue close-out.** Determine ownership by
  `grep -nE "093-inbound-frame-dispositions|#51[456]\b|#52[34]\b|#540\b" spec/feature-catalogue.md`
  and by reading the TC-002 (2a–2t) and TC-003 (3a–3e) rows. If 093 owns no OFFICIAL row, record that
  disposition with the grep output in the verify record's `## Completeness`. TC-002 and TC-003 are
  unowned backlog rows of which 093 witnesses only some sub-cases (2d, 2m, 2t; 3b, 3c, 3e): do not flip
  them to `done`; name 093's witnesses in their notes, by gtest name: T026's `TC002_2d_*`,
  `TC002_2m_*`, `TC002_2t_*`, `TC003_3b_*`, `TC003_3c_*` and `TC003_3e_*` cells (names re-derived with
  `grep -n "TEST.*TC00[23]_" tests/session/inbound_frame_dispositions_test.cpp`), and the pointer T026
  adds to the session TC corpus (`tests/session/conformance/CMakeLists.txt`). The same names go in
  `spec/coverage-index.md`'s FIX-TC scenario 2 and 3 rows. Any row 093 does own flips to `done` with
  this PR as evidence. The matching `spec/coverage-index.md` entry is T106's §4.5.2 row, plus any row
  the ownership grep adds.
- [ ] T125a **Gate scope, then open the PR.** T121, T122 and T125 add commits after T115–T117 ran, so
  re-run T115, T116 and T117 at the final branch head as the last action before the first push (a gate
  result covers only the commits that existed when it ran). Commit the `tasks.md` ticks through T125
  first, so the head the gates ran on is the head that is pushed. Then push and open the PR with T124's body, and
  check `closingIssuesReferences`. T126 writes only the verify record, through the decisions symlink
  into the parent repo, so it adds no library commit. **Recorded deviation:** this task deliberately
  sits between the two mandatory close-out tasks T125 and T126. T126 stays the FINAL task, so Gate B
  pre-flight 4d (which reads T126's `## Completeness`) is unaffected.
- [ ] T126 **Feature-completeness audit (the FINAL task).** Assert against the final branch head:
  - (i) every `tasks.md` row through T125 is `[X]` or carries an explicit waiver rationale (committed
    before T125a's push); T125a's and T126's own completion is evidenced in the verify record, and their
    ticks land with the next commit the PR takes;
  - (ii) every FR-001…FR-053 and SC-001…SC-008 maps to a landed test AND a landed implementation (the
    coverage tables below), and every quickstart §4 row's cells exist and are GREEN;
  - (iii) every feature-owned OFFICIAL catalogue row is `done` with a matching `coverage-index.md`
    entry, or T125's "owns no row" disposition is recorded.
  Record the verdict (100 % or fully waived) in
  `.specify/decisions/093-inbound-frame-dispositions-verify.md` `## Completeness`. `/gate-b` pre-flight
  4d hard-blocks without it.

---

## Dependencies and execution order

### Phase dependencies

- **Setup (Phase 1):** T001 → T002 (blocks on #539) → everything else. T003 needs T002. T004 → T005,
  and T005 precedes every production edit. T006 → T007 (MSVC leg). T008 and T009 need only T002;
  T009 → T010. T011 needs T002; T012 needs T010, and may extend T011.
- **Foundational (Phase 2):** needs Setup. T013 → T018. T014 (needs T006) → T024 (Phase 5) and T063.
  T015 ‖ T016 → T017 → T017a → T018 → T018a and T019; T020 after T018. T021 → T022 → T023 → T025. 2b and 2c
  touch different files and may run side by side. **Blocks every story.**
- **US1 (Phase 3):** Foundational. Tests T026–T032 before T033–T037; T035 and T036 need T034; T037 needs
  T034.
- **US2 (Phase 4):** Foundational and US1 (a garbage-only peer reaches T only once US1 disregards
  garbage; spec US2's rationale). T041 → T045. T046 → T047 → T048 → T049.
- **US3 (Phase 5):** Foundational and US1 (T055's bad-CheckSum and resync-candidate variants need the
  pump's resync). T061 → T063; T062 → T063. The FR-013 group: T055 and T055a RED → T024, T022a, T064,
  one commit group, after T063. T065 needs T008. T067 after all of them, then T069. No US1/US2 task
  reads its output.
- **US4 (Phase 6):** US1 (the refresh follows step 1).
- **US5 (Phase 7):** US1 (step 2 follows step 1) and US2 (`close()`'s wait is bounded by
  `logon_timeout_ms`). T074a → T075, T080, T081, T082 (the shared `HookedStore`); T084 adds its
  `reset_to` modes. T077 (FR-030) before T084–T087 (FR-041).
- **Public surface (Phase 8):** US1 (the counter), US2 (the field), and every story for T102 (Q-37 rows
  1–6). T095 immediately before T099.
- **Polish (Phase 9):** every phase above. T105–T108 → T109 → T110–T119 → T120 → T121, T122 → T123 →
  T124 → T125 → T125a → T126. T126 is last.

### Story completion order

US1 (MVP) → US2 → US3 → US4 → US5. US3 and US4 may be swapped; both need only US1 beyond Foundational.
`src/session/session.cpp` and `src/session/engine.cpp` are shared by every story, so stories run
sequentially in those files.

### Within each story

Tests first, each shown RED on base for its stated reason (or named as a regression guard with its
mutant), then the implementation, then GREEN, then the story's mutants in a scratch copy.

---

## Parallel execution examples

```text
# Foundational: Framer cells and the L cell together (different files):
phase-implementer: T015 Framer resync cells   → tests/wire/framer_resync_test.cpp
phase-implementer: T016 strict-caller guards  → tests/wire/framer_error_path_test.cpp
phase-implementer: T021 file + L cell          → tests/session/inbound_frame_dispositions_test.cpp

# US1 RED cells. Two chains may run side by side; T029 and T031 also write engine_firstframe_test.cpp,
# so T030 runs between them in the first chain:
phase-implementer: T026 → T028 → T029 → T030 → T031 → inbound_frame_dispositions_test.cpp, engine_firstframe_test.cpp
phase-implementer: T027 → T032 (readpump pins)       → tests/session/engine_readpump_test.cpp

# US2 RED cells:
phase-implementer: T041 → T045                 → tests/session/engine_establishment_timeout_test.cpp
phase-implementer: T043 phase (a)              → engine_firstframe_test.cpp, first_frame_total_cancel_tls_test.cpp
phase-implementer: T042 timeout refusal        → tests/session/inbound_frame_dispositions_test.cpp
phase-implementer: T044 Active-read alloc      → tests/alloc_guard/test_093_pump_active_read_alloc_guard.cpp

# US3 RED cells:
phase-implementer: T058 #540 cell              → tests/wire/unknown_fields_test.cpp
phase-implementer: T059 reserve overloads      → tests/wire/offset_table_test.cpp
phase-implementer: T057 defence + LateSite     → tests/session/unparseable_frame_disposition_test.cpp
phase-implementer: T054 → T055a → T055 → T056 → T060 → inbound_frame_dispositions_test.cpp (sequential)

# US4: T071 and T072 share one file → sequential; T073 after both.

# US5 RED cells (T074a first; everything below uses its HookedStore):
phase-implementer: T074a → T075 → T080 → T081  → tests/session/test_session_plaintext_roundtrip.cpp (sequential)
phase-implementer: T076 closing 35-not-third   → tests/session/inbound_frame_dispositions_test.cpp
phase-implementer: T079 reset_to cells         → test_memory_store_round_trip.cpp, test_file_store_crash_survival.cpp
phase-implementer: T082 stop-during-unit       → tests/session/engine_reset_unit_stop_test.cpp (after T074a)

# Public surface RED cells:
phase-implementer: T090/T091 C-ABI             → tests/capi/inbound_frame_dispositions_capi_test.cpp
phase-implementer: T092 Python                 → bindings/python/tests/test_093_inbound_frame_dispositions.py
phase-implementer: T093 TOML                   → tests/config/test_load_happy_path.cpp, test_load_negative_battery.cpp
phase-implementer: T094 version pin            → tests/capi/version_test.cpp

# Polish docs: T105 ‖ T106 ‖ T107 (orchestrator markdown).
```

---

## Implementation strategy

### MVP first: User Story 1

1. Setup (T001–T012): rebased after #539, baselines, ceilings, #540 decided, builders canonical.
2. Foundational (T013–T025, without T024): Framer resync with bounded work, L computed and stored, the
   seam. The 383 refusal and the carry at `open()` land in US3's FR-013 group.
3. US1 (T026–T040): garbled and 35-not-third frames disregarded, counted, evented, logged.
4. **Stop and validate**: TC 2d/2m/2t/3b/3c/3e conform (SC-001) in C++. US1 alone is not shippable:
   spec US2's rationale says a pre-Active disregard without the establishment timeout is an unbounded
   hang, so US1 and US2 ship together, in one PR with the rest.

### Incremental delivery

US2 bounds establishment; US3 makes every admitted frame parse and closes over-L frames everywhere;
US4 moves liveness; US5 settles #523 then #524. Phase 8 declares the C-ABI MINOR once every C-7 row 1–6
behaviour exists. One PR carries all of it.

### Next pipeline steps (before `/speckit-implement`)

- `/speckit-analyze` (mandatory, `[const §XVI.3–4]`; a `[const §X.6]` control for the C-ABI change).
- `/speckit-checklist`, then `/speckit-checklist-audit` (pipeline step 9), which blocks implement.

---

## Coverage

### FR / SC → tasks

| Requirement | Tests | Implementation |
|---|---|---|
| FR-001 | T015, T026, T027, T029, T032 | T018, T035, T036 |
| FR-002 | T015, T019, T020, T027, T028 | T018 |
| FR-003 | T026, T028, T029, T030, T076, T090 | T033, T034, T035, T036, T037, T097 |
| FR-004 | T026, T029, T032 | T010, T037 |
| FR-005 | T029, T032 | T037 |
| FR-006 | T041, T042, T043, T045 | T046, T047, T048, T049 |
| FR-007 | T090, T092, T093 | T096, T097, T098, T101 |
| FR-008 | T015, T031 | T018, T035, T036 |
| FR-010 | T021, T055a, T054, T055 | T022, T022a, T024, T063, T064 |
| FR-011 | T054, T060, T091, T069 and T113 (forwarding lane) | T061, T063 |
| FR-012 | T054, T066 (arena mirrors re-based), T068 | T063 |
| FR-013 | T015, T016, T055, T055a | T018, T024, T022a, T064 |
| FR-014 | T057, T070 | T063 |
| FR-015 | T008, T058, T060 | T065 |
| FR-020 | T071 | T073 |
| FR-021 | T072 | T073 |
| FR-030 | T075, T076 | T077 |
| FR-040 | T079 | T084 |
| FR-041 | T074a, T080, T081, T082, T083 | T085, T086, T087 |
| FR-042 | T081, T088 | T084 |
| FR-050 | T105 (check_bl_delta) | T105, T106, T107 |
| FR-051 | T094, T101, T102 | T099, T100, T101 |
| FR-052 | T015 (W-4), T044, T045, T051 | T018, T048 |
| FR-053 | T002, T117 | T017, T023, T021/T057 accessors |
| SC-001 | T026, T032 | T035, T037 |
| SC-002 | T015, T019, T027 | T018 |
| SC-003 | T041, T043, T045, T090 (band), T092 (band), T093 | T048, T049 |
| SC-004 | T007, T054, T055, T069 | T063, T064 |
| SC-005 | T071 | T073 |
| SC-006 | T074a, T075, T080, T081, T082, T088 | T077, T085, T086, T087 |
| SC-007 | T005, T018a, T054 (`open()` blocks measured), T112 | T024, T063, T105 (B&L formula and measured totals) |
| SC-008 | T008, T058 | T065 |

### Contract C-1…C-6 clauses → tasks

| Clause | Tasks |
|---|---|
| C-1 configuration (both Framers resync on, max = L) | T035, T036, T024, T064 |
| C-1 outcome table and close rows | T015, T018, T055, T055a, T064 |
| C-1 resync rule (start, extent, state across feeds, ordering, reporting) | T015, T018, T027, T028 |
| C-1 Extent: each failed candidate is its own region (two adjacent garbles count two) | T015 (Q-5), T018 |
| C-1 Frame start: after a wrong-CheckSum frame the next byte is a search position (wrong-CheckSum ‖ junk ‖ good counts one) | T015 (Q-5), T018 |
| C-1 W-1, W-2 (both caps), W-3, W-4; the bound and its condition | T013, T015, T017, T017a (the constant), T018, T018a (cost), T019, T020 |
| C-1 every other Framer caller unchanged | T016, T017 |
| C-2 step 1 (35-not-third, every state but Disconnected) | T026, T029, T032, T037 |
| C-2 step 2 (closing, NotConnected/LogonSent) | T075, T076, T077 |
| C-2 step 3 (092's rows except D-8) | T032, T037 |
| C-2 step 4 (refresh, then the existing arm) | T071, T073 |
| C-2 Disconnected ignores every frame | T029, T110 (its mutant) |
| C-2 Disconnected: a Framer garble is counted, evented and logged | T029, T035 |
| C-2 step 1 takes D-9 (LogoutSent, third field not 35: counted, evented, logged) | T029, T037, T105 |
| C-2 Active-only 383 check deleted | T055, T064 |
| C-3 I-1 (every admitted frame parses) | T054, T063 |
| C-3 I-2 (fresh resource over B(L), spill witness) | T054, T063, T069, T070 |
| C-3 I-3 (no nested inbound parse) | T063 |
| C-3 I-4 (late close stays as defence) | T057, T070 |
| C-3 I-5 (lazy reads at exhaustion) | T058, T060, T061, T065, T091, T113 (forwarding lane) |
| C-3 I-6 (admin/outbound arenas unchanged) | T063, T121 |
| C-4 phase (a) | T043, T049 |
| C-4 phase (b), loop-head check, race, clock, re-arm, disarm, zero refusal | T041, T042, T045, T046, T047, T048 |
| C-4 allocation | T044, T051 |
| C-5 one per-frame writer; garbled and faulty never write | T071, T072, T073 |
| C-6 store operation | T079, T084 |
| C-6 the unit (steps 1–7) and the shield | T074a, T080, T082, T085, T088 |
| C-6 "any point of the unit": its three conditions | T085 (re-derived under `## Populations`) |
| C-6 engine-stop flag | T082, T086 |
| C-6 `close()`'s wait and the outcome table | T080, T081, T087 |

### Quickstart cells → tasks (§1, numerically)

| Cell | Tasks | Cell | Tasks |
|---|---|---|---|
| Q-1 | T026, T039, T090 (C arm) | Q-20 | T071, T074 |
| Q-2 | T015, T027, T019, T039 | Q-21 | T072, T074 |
| Q-3 | T015, T027, T019 | Q-22 | T075, T078 |
| Q-4 | T015, T019, T020 | Q-23 | T080, T084 (forward mode) |
| Q-5 | T015, T028, T039 | Q-24 | T080 |
| Q-6 | T015, T055, T019 | Q-25 | T074a, T081, T088 |
| Q-7 | T016, T019 | Q-26 | T074a, T082, T088 |
| Q-8 | T029, T039, T110 | Q-27 | T081, T088 |
| Q-9 | T076, T078 | Q-28 | T079, T088 |
| Q-10 | T015, T031, T019, T039 | Q-29 | T079, T088 |
| Q-11 | T054, T069, T070 | Q-30 | T090 |
| Q-12 | T055a, T024 | Q-31 | T093 |
| Q-13 | T021 (L), T055a and T022a (383 refusal), T042 (timeout) | Q-32 | T008, T058, T070 |
| Q-14 | T055a and T024 (carry), T056 (session arena), T069 (MSVC, both halves) | Q-33 | T060, T091, T070, T069 and T113 (forwarding lane) |
| Q-15 | T057, T070 | Q-34 | T030 |
| Q-16 | T041, T053, T090 (C, band), T092 (Python, band), T093 (TOML) | Q-35 | T041 |
| Q-17 | T043 (late handshake: peer-side observable), T053 | Q-36 | T045, T053 |
| Q-18 | T045, T053 | Q-37 | T102 |
| Q-19 | T044 | | |

Quickstart §0: T005 (bench), T007 (ceiling), T009–T010 (census), T008 (#540). §2 instruments: T017,
T019 (counted work), T044, T051 (global counter), T070 (spill witness), T074a, T084 and T080
(`HookedStore`'s two modes and its forwarding cell). §3: T018a and T112 (the resync-mode Framer
bench), T111, T112, T113, T114–T118.

### Contract C-7 rows → tasks

| Row | Tasks | Row | Tasks |
|---|---|---|---|
| 1 | T017, T018, T035, T100, T102 | 10 | T022, T022a, T046 |
| 2 | T037, T100, T102 | 11 | T033, T047, T087 |
| 3 | T024, T064, T100, T102 | 12 | T059, T062 |
| 4 | T063, T100, T102 | 13 | T079, T084 |
| 5 | T046–T049, T100, T102 | 14 | T023, T024, T034, T047, T086 |
| 6 | T073, T100, T102 | 15 | T024, T063, T105 |
| 7 | T096, T101 | 16 | T077, T124 |
| 8 | T034, T097, T098, T101 | 17 | T085–T087, T124 |
| 9 | T098 | 18 | T065 |

### Contract C-8 L-rows → tasks (each also a B&L row in T105)

| L-row | Witness or measurement | L-row | Witness or measurement |
|---|---|---|---|
| L-1 | T027 (stall pin), T055a (re-asserted against L) | L-10 | disclosure only (C, Python, TOML cannot set 383) |
| L-2 | T026 (3e kind) | L-11 | T028 |
| L-3 | disclosure (a custom store's crash atomicity); T081 shows its table holds | L-12 | T082 |
| L-4 | T081 (wait expiry) | L-13 | T051, T120 |
| L-5 | T060, T091 | L-14 | T121 (follow-up filed) |
| L-6 | T043 (no event), T055 (first frame over L) | L-15 | T015, T027 |
| L-7 | disclosure only | L-16 | T015 |
| L-8 | T015, T027 | L-17 | T061 (headroom condition); fixpp#541 |
| L-9 | T028 | | |

## Notes

- `[P]` means different files and no incomplete dependency. `[USn]` traces a task to its story.
- This file records no population sizes. Each population is re-derived by its command (T012), and
  coverage of the C-2 steps, C-3 invariants and quickstart cells is re-checked mechanically by
  `/speckit-analyze`.
