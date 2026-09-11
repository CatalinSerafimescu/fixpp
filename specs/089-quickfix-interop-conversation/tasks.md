---
description: "Task list for 089 — comprehensive live QuickFIX interop conversation"
---

# Tasks: Comprehensive live QuickFIX interop conversation

**Input**: Design documents from `specs/089-quickfix-interop-conversation/`
**Prerequisites**: `plan.md`, `spec.md`, `research.md`, `data-model.md`, `contracts/`, `quickstart.md`

**Tests**: REQUIRED and non-negotiable for this feature. FR-017 mandates a forced-failure
demonstration for every new assertion and FR-018 mandates a **spurious-hit** arm in addition. The
arm inventory is `quickstart.md` Step 4, which is itself derived from the contracts' witness lists.

**Organization**: Tasks are grouped by user story. Two axes cut across every story and are stated
once here rather than repeated per task:

- **TWO REPOSITORIES.** `phase-9-harness/…` paths are the **parent** repo
  (`/home/catalin/Work/Programming/Antreprenoriat/research/G19-fix-fpml-iso20022/`). All other paths
  are the **library** submodule. The counterparties link QuickFIX, which is deliberately outside
  fixpp's dependency graph; that boundary is not negotiable.
- **⛔ EVERY configure/build task is gated by `ci/disk-preflight.sh`.** Phase 1 builds the gate
  first for that reason. `df` inside WSL reports the VHD's internal free space and is blind to the
  host drive that actually binds — see `contracts/disk-preflight.md`.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (US1–US5)
- Include exact file paths in descriptions

---

## ⚠️ Inherited work — read before starting Phase 2

A parallel session implemented part of the counterparty side ahead of this task list. It is
**committed and pushed but deliberately unmerged**:

| Branch | Tip | Carries |
|---|---|---|
| parent `counterparty-rebuild` | `3a42ca4` *(was `0aa5cf0` when this table was written; the extra commit is report-only; 3 commits were unpushed as of 2026-09-11 — re-derive with `git branch -vv`)* | readback/sent/hello/terminal emitters in both counterparties, the six `INTEROP_CP_*` consumers with `script_digest` recomputed, the QuickFIX-J `ISO-8859-1` assert, the typed-accessor compile arm in `CMakeLists.txt` + `pom.xml`, Dockerfile capability markers, the **two-way** cross-language fixture |
| parent `089-harness` (worktree `~/Work/Programming/antreprenoriat-089`) | *cut from `counterparty-rebuild`* | **089's own parent-side work lands here** (from T008 on). The worktree reaches the library checkout and `reference-engines/` through local symlinks, since `run_interop_cell.py` derives both from its own location. ⛔ A commit to parent `main` touching the harness paths **is** the GHCR publish (T003) |
| fixpp `089-pin-counterparty-digest` | `5ef6e88b` | ⛔ **OBSOLETE — do not land.** It pins `interop-smoke.yml` to the pre-089 digest, which FR-026 no longer asks for and which would make FR-020 undischargeable. Kept only as a record |

Tasks below marked **[INHERITED]** are substantially implemented there. They are still tasks: each
must be **verified against the current bundle** and landed, not assumed. Two corrections have already
been applied on top (`bf5ce32e`, `0aa5cf0`) after the inherited work was found to spell `direction`
against the bundle and to report two bundle defects that did not exist at the bundle's tip.

⚠️ **Merging the parent branch IS the publish**, and under FR-026 that is the correct first step — not
something to defer. What must not happen is anything *depending* on the new image before FR-020 has
run against it, and no pull request touching the consumer paths being opened in between. See T003/T004.
⛔ **`research.md` R-11 is SUPERSEDED on this point.** It reasoned that existing consumers must be
pinned to the pre-089 digest before republishing; that remedy obstructs FR-020, whose whole question is
whether those cells still pass **against the new counterparty**. R-11's underlying concern — that
nothing should silently depend on an unverified image — survives; its prescription does not.

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: The disk gate that every later build depends on, the republish-ordering prerequisites,
and the configuration-vocabulary correction that four artifacts currently get wrong.

- [ ] T001 Measure the per-configuration disk budget (R-1): build each of the four targeted interop-driver configurations — ⚠️ **amended 2026-09-11 (user decision): incrementally in the existing per-configuration trees, as the matrix performs it, not from clean; the four binaries the new cells name do not exist until T052, so measure the interop driver targets the existing cells name, and T095 re-derives once T052's targets exist; `build/linux-clang-debug` is never deleted** — and record the **internal free delta** and the **host `E:` growth delta** separately, with the measurement date, into `ci/disk-preflight.sh`'s threshold block — per `contracts/disk-preflight.md` D-7 (values come from R-1's measurement, **per configuration**, against the **targeted** interop-driver build, not `all`) and D-7a (`required_host_growth` is the measured host delta and may **not** be derived from or offset against the internal figure)
- [X] T002 Implement `ci/disk-preflight.sh` per `contracts/disk-preflight.md`: D-1 two named predicates evaluated **independently** (`required_internal_free`, `required_host_growth`), D-1a output names the failing predicate (`internal` / `host` / `both`), D-2 WSL detection via `microsoft` in `/proc/version` with the host mount located from `/proc/mounts`, D-3 unreadable or absent host mount on WSL ⇒ **FAIL, never `proceed`**, D-4 off-WSL skips the host predicate, D-5 **exit non-zero and refuse the build — no warn-and-continue, no exit 0 on the failing path**, D-6 print both readings and both required values with their measurement dates, D-8 reserve ballast not counted as available, D-9 unset/unparseable threshold is a **hard error, never 0**, D-10 host mount resolved independently with no fallback to the build mount, D-11 neither reading may include `/mnt/wsl/fixppbuild/ccache` (different device `/dev/sde`)
- [ ] T002a **Refill the reserve ballast — the half of `plan.md` § *Reserve ballast* that had no producer.** T002/D-8 implements *"the preflight must not count it as available"*; this task implements *"**if spent, it must be refilled** as an explicit task step"*, which nothing did. Before **every** configure/build gated by `ci/disk-preflight.sh` (T095's sequencing): (a) check that `/mnt/e/_wsl-reserve-1.bin` and `_wsl-reserve-2.bin` are present and **5 GiB each**; (b) if a preceding preflight consumed either, **refill it to 5 GiB before the next configure or build starts** — not at the end of the matrix, since the valve must be intact for the run that needs it; (c) ⛔ **a spent-and-unrefillable valve is a `stop`**, per `plan.md` § *When the preflight cannot be satisfied* — never a warn-and-continue, on the same rule D-5 states for the gate itself. ⚠️ **A spent valve nobody refills is worse than no valve**, because the next person believes they still have it — which is why *"we did not need it"* does not discharge this task; record the check's result either way. ⚠️ **Id note**: suffixed rather than renumbered, on this bundle's own `a`-suffix idiom (FR-021a, SC-009a, E-7a) — renumbering T003…T110 would rot every task citation in the bundle to fix an ordering nobody reads
- [ ] T003 **Publish the rebuilt counterparty image, then VERIFY it — in that order (FR-026).** ⛔ **The pin work this task used to carry is DELETED**: no consumer is pinned to the pre-089 digest, because pinning the existing cells away from the new image is exactly what makes FR-020 undischargeable. (a) Land the parent counterparty change so `publish-counterparties.yml` fires on `push: main`; the typed-accessor compile arm gates it, so a non-conforming counterparty is never published. (b) Read the new digest from the publish run's **step log**, never `.conclusion`. (c) ⛔ **Before anything depends on it**, run FR-020's regression — the existing cells, now pulling the new image — per T099. ⚠️ Both consumers keep naming `:latest` deliberately; that is what lets them exercise the new counterparty at all
- [ ] T004 **The sequencing rule and the rollback — the two things that make T003 safe (FR-026).** ⛔ **The FR-016b override mechanism this task used to 'decide' is DISSOLVED, not deferred**: with no workflow-level pin there is nothing for an 089 cell to override. (a) ⛔ **Open no pull request touching the counterparty-image consumer paths between the publish and FR-020 reporting green** — re-derive the open set at publish time with `gh pr list --state open` in **both** repositories; it measured zero on 2026-09-10, which is a fact about that moment and not a property of the design. ⚠️ The hazard is real when work IS in flight: both consumers trigger on `pull_request` with `branches: ["**"]`, so such a run resolves the image in its own base's context. (b) ⛔ **If FR-020 goes RED, re-tag `:latest` to the pre-089 digest `be149447…` before attempting anything else** — GHCR retains superseded versions, so the old digest stays addressable. Write the rollback down rather than improvising it: recovering under pressure is when the wrong version gets deleted
- [X] T005 [P] Implement `ci/test-disk-preflight.sh` with the RED arms `contracts/disk-preflight.md` § *Required RED arms* enumerates — A-1, A-1a, A-3, A-5, A-5a, A-6 — plus the spurious-hit arms A-7 (threshold unset/unparseable with both mounts nearly full ⇒ `proceed` true while the predicates measure nothing) and A-8 (host mount resolves to the build mount ⇒ the host predicate satisfied by the build reading), and the GREEN controls A-2 (both comfortable) and A-4 (not WSL, no host mount — proving A-3 is not merely *"no host mount ⇒ fail"*)
- [X] T006 [P] Register `ci/disk-preflight.sh` and `ci/test-disk-preflight.sh` in the `ci-script-pins` fixture so the gate's own scripts are pinned like every other CI script
- [X] T007 Resolve D-9a's bootstrap: R-1's measurement cannot run under its own gate, since D-7 sources both thresholds from it. Implement the documented bootstrap path in `ci/disk-preflight.sh` and prove it cannot be used to bypass the gate in ordinary operation
- [X] T008 [P] Correct `CONFIG_TO_PRESET` in `phase-9-harness/tools/run_interop_cell.py` to `{normal→linux-clang-debug, asan→linux-clang-asan, ubsan→linux-clang-ubsan, tsan→linux-clang-tsan}`, retiring the `asan-ubsan` key (FR-021 · FR-021a)
- [ ] T008a [P] In `phase-9-harness/tools/run_interop_cell.py`, launch each cell's gtest with the `environment` of the test preset its configuration maps to, **read from `library/CMakePresets.json` at run time, never copied** (FR-021a). ⚠️ UBSan recovers by default and exits 0; #268 put `UBSAN_OPTIONS=halt_on_error=1` in the `linux-clang-ubsan` test preset, which only ctest reads, and this runner bypasses ctest. A missing preset or an unreadable `CMakePresets.json` is a **hard error**, never a silent empty environment. ⚠️ **Id note**: `a`-suffixed on this bundle's own idiom (see T002a)
- [X] T009 [P] Extend `CONFIGS` in `tests/interop/cell_results_schema_check_test.py` to gain `asan` and `ubsan` and lose `asan-ubsan` (FR-021a)
- [X] T010 [P] Correct the config vocabulary `normal|asan-ubsan|tsan` to the four-config set in `phase-9-harness/INTEROP-016-DESIGN.md` (FR-021a)
- [X] T011 [P] Correct the claim that the charter's ASan+UBSan requirement is met by `asan-ubsan` in `phase-9-harness/INTEROP-COVERAGE-REPORT.md` (FR-021a)

**Checkpoint**: The disk gate exists and is proven able to refuse a build; the republish ordering is
decided rather than assumed; the four-config vocabulary is consistent across all four artifacts.

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: The artifacts every user story reads or writes. Nothing in Phase 3+ can run until these
exist.

**⚠️ CRITICAL**: No user story work can begin until this phase is complete.

- [ ] T012 Author `tests/interop/conversation/conversation_script.yaml` — **the executable conversation** (FR-008a): ordered steps carrying `step_id`, `msg_type`, originator, direction, **intent field values declared down to the value**, typed-read declarations (FR-003b) and `depends_on`. Read by **both** sides — the gtest and the counterparty via `INTEROP_CP_SCRIPT_PATH`. Must cover Logon, the admin repertoire (TestRequest/Heartbeat, ResendRequest, SequenceReset-GapFill, Reject) and the business sequence in a **single session per cell** (FR-008), and must include **at least one multi-instance nested repeating group** (FR-008b)
- [ ] T013 Author `tests/interop/conversation/census.yaml` — **W-2a's operand** (FR-015d): a **manual, mechanical transcription** of `spec.md` § *Conversation census* (the B-01..B-12 table, the applicability table, the declared-inapplicability table). ⛔ **It MUST NOT be generated from `conversation_script.yaml`, nor from any traversal of it.** Generating one from the other is the cheapest way to make SC-009a pass and it converts the equality into a tautology that passes forever — at which point the census is exactly the rot class it claimed exemption from. A task that generates one from the other has not implemented SC-009a; it has deleted it
- [ ] T014 [P] Create `phase-9-harness/tools/interop_capability_minimums.yaml` — **the capability-minimums artifact**, the required-version operand that C-2, C-12, FR-016a and the hello gate all point at. ⛔ No minimum is written into the spec bundle; this file is where they live. It must declare, per cell, the minimum `readback_protocol` and the minimum `typed_accessor_arm`
- [ ] T015 **[INHERITED]** Verify and land the `sent` + `readback` + `hello` + `terminal` emitters in `phase-9-harness/quickfix-cpp/counterparty/interop_counterparty_main.cpp` and `phase-9-harness/quickfixj/src/main/java/io/fixpp/phase9harness/quickfixj/InteropCounterparty.java` (FR-003, FR-003a, FR-013b), including the hello's **`typed_accessor_arm`** field (C-12) whose value must originate in the arm's own build step as a compile definition / generated constant — ⛔ **not a hand-written literal**, and the source must fail to compile when that definition is absent
- [ ] T016 **[INHERITED]** Verify and land the typed-accessor **compile arm** in `phase-9-harness/quickfix-cpp/counterparty/CMakeLists.txt` and `phase-9-harness/quickfixj/pom.xml`: a typed accessor compiles only if the field belongs to that message, with a negative-compilation arm and a **control** proving the mutant field type exists on some FIX 4.4 message. ⚠️ The control is load-bearing — without it, setting the mutant field to a **nonexistent** type left the C++ arm GREEN via a cascading diagnostic that satisfied both conditions
- [ ] T017 **[INHERITED]** Verify and land the QuickFIX-J startup assertion that `org.quickfixj.CharsetSupport.getDefaultCharset()` is **`ISO-8859-1`**, failing loudly otherwise — `value_b64` is only reconstructible because of it
- [ ] T018 Implement fixpp's own `sent` + `readback` emitter **and the shared comparator** in `tests/interop/support/` — **the witness producer** (data-model §4). fixpp is the **third** emitter; the inherited fixture covers only two of the three
- [ ] T019 Add the six metadata keys to the existing `cp_env` block in `launch_counterparty` in `phase-9-harness/tools/run_interop_cell.py`: `INTEROP_CP_{RUN_ID, CELL_ID, CONFIG, IMAGE_DIGEST, SCRIPT_PATH, SCRIPT_DIGEST}`. ⚠️ **This is the single most load-bearing gap in the inherited work** — the counterparty emitters are opt-in on `INTEROP_CP_RUN_ID`, so until this lands no cell sets those keys, no stream file is opened, and not one line of the emitter code runs anywhere
- [ ] T020 Add the full `INTEROP_FIXPP_*` env block to the **gtest's** environment in `phase-9-harness/tools/run_interop_cell.py`: `RUN_ID`, `CELL_ID`, `CONFIG`, `ARM`, `SCRIPT_PATH`, `SCRIPT_DIGEST`, `READBACK_PATH` (data-model §1). ⚠️ `INTEROP_CP_*` is `env=cp_env` on the counterparty process only — the gtest needs its own block. `READBACK_PATH` is FR-024's consumption seam (FR-024)
- [ ] T021 Make the shim **compute** `script_digest` (lowercase-hex SHA-256 over the script bytes) and **compare** it against the value each side recomputes, failing **before** the gtest is launched on a mismatch (FR-008c · FR-013b) in `phase-9-harness/tools/run_interop_cell.py`
- [ ] T022 Add four dedicated conversation config templates `phase-9-harness/configs/conversation-*.cfg.in` for the eight new cells, carrying `UseDataDictionary=Y` with a FIX 4.4 `DataDictionary` path. ⛔ The **existing** `*-tls.cfg.in` templates are NOT edited — they are named by the `PD-*` and idle-cadence cells R-5 protects
- [ ] T023 Implement the **pre-conversation hello gate** (FR-016a) shim-side in `phase-9-harness/tools/run_interop_cell.py`, before the gtest is launched, reading the required minimum for each announced capability from `phase-9-harness/tools/interop_capability_minimums.yaml` (T014). ⛔ A cell for which **no minimum is declared** FAILS — not skip, not pass (FR-016a · FR-024 · C-2)
- [ ] T024 Create `phase-9-harness/tools/promote_interop_evidence.py` — **the named promotion command** (FR-014b), the **only** reader of a run artifact and the **only** writer of a `pass` row. It opens both streams, requires a `hello` **and** a `terminal` on each (E-1b), checks the join keys, and evaluates the corroboration
- [ ] T025 Invoke the promotion command from `phase-9-harness/tools/run_interop_cell.py` after each configuration's 8 cells and **before** reclaiming that configuration's build tree (FR-014b) — a reclaimed tree cannot be promoted from afterwards
- [ ] T026 Create `tests/interop/witness_evidence.yaml` with **three** sections, not two: witness rows, the `runs:` **ledger**, and **`validation_pairs:`** (data-model §10, §11, FR-012a). ⚠️ The pair section is the one that was schemad, constrained by E-7 and armed in Step 4 while **no artifact held it** — so zero pairs was green through every gate in this bundle
- [ ] T027 Extend `REQUIRED_FIELDS` in `tests/interop/cell_results_schema_check_test.py` **conditionally, on `kind: conversation` only — never globally**. An unconditional extension breaks the **pre-existing `status: pass` rows already committed**, none of which has an 089 run behind it. ⛔ **Do not write the count down** — `plan.md` § *External obligations* → the `REQUIRED_FIELDS` row carries the re-derivation recipe, and 089 itself adds rows to that file
- [ ] T028 Extend the status vocabulary in `tests/interop/cell_results_schema_check_test.py` with `error:enospc` and `aborted`; the shipped set `{pass, fail, skip, known-limitation, n/a}` is closed by an assertion (FR-014a · E-5)
- [ ] T029 Verify `test_ids_unique` in `tests/interop/cell_results_schema_check_test.py` is **NOT** replaced. Round 1 recorded that it must be, on the assumption that 8 ids had to serve 32 rows; the manifest/ledger split removes that assumption. Confirm the current shape still holds and record why it was left
- [ ] T030 Assert that `tests/interop/cell_results_schema_check_test.py` **opens no artifact path**. It is a ctest (`tests/interop/CMakeLists.txt:459`) provisioned in `tier1.yml`, `tier2.yml` and `tier3-libcxx.yml` on hosted runners that hold **no** run artifacts — a check that opens one fails there for a reason unrelated to what it tests

**Checkpoint**: The script and census exist with independent provenance; both counterparties emit and
the shim produces the env that makes them emit; the promotion command and the evidence artifacts
exist. User story implementation can begin.

---

## Phase 3: User Story 1 — Interop sessions run against the real dictionary (Priority: P1) 🎯 MVP

**Goal**: Every cell **this feature adds** runs against the production FIX 4.4 dictionary on both
sides, instead of a FIX 4.2 single-Heartbeat sentinel and `UseDataDictionary=N`.

**Independent Test**: Run **this feature's** `logon-hb-logout` and `NOS->ExecRpt` conversation cells
with the real dictionary on both sides and confirm they pass, with goldens captured and reviewed.

⚠️ **Scope**: the flips are scoped to **the cells this feature adds**. Flipping `SessionConfig::dictionary`
on an existing cell changes fixpp's own inbound parse for that cell — group detection is dictionary-driven
on the read path — which is the same class of collateral change R-5 declines to make peer-side.

- [ ] T031 [US1] Configure the fixpp session for this feature's cells with the production `dictionaries/FIX44.xml` rather than the `make_minimal_dictionary()` sentinel, in `tests/interop/happy/hp_support.hpp` (FR-001 · SC-006)
- [ ] T032 [P] [US1] Verify the four new conversation config templates from T022 render `UseDataDictionary=Y` with a FIX 4.4 `DataDictionary` path for this feature's cells only (FR-002)
- [ ] T033 [US1] Implement FR-011's run-time verification that a production dictionary is loaded in both arms, so a cell cannot silently run the FIX 4.2 sentinel. ⚠️ **Scope this claim precisely**: per FR-001/R-5a the production dictionary is on in **both** arms of every cell this feature adds, so FR-011's predicate is true in the validation-off arm too — its population equals its complement. It establishes *"not the sentinel"* and **nothing about whether the validator ran**
- [ ] T034 [US1] Re-capture goldens per cell, **verify-first**, for any cell whose serialization or field ordering the dictionary flip moves (FR-019). ⛔ Never `--update-goldens` across the matrix — it overwrites verified goldens and races on port-bind. Record the reviewed diff rather than auto-accepting it
- [ ] T035 [US1] Add the forced-miss arm: a cell with `dictionary_enabled == false` whose witnesses include groups ⇒ cell **FAILS** (data-model §1)

**Checkpoint**: US1 is independently testable — the two named cells pass against the real dictionary
on both sides with reviewed goldens.

---

## Phase 4: User Story 2 — The peer reports what it parsed, field by field (Priority: P1)

**Goal**: For any message fixpp sent, the exact fields the QuickFIX peer parsed and the values it
decoded are available as structured, machine-comparable data — not a re-serialized blob.

**Independent Test**: Send one NewOrderSingle from fixpp with a known field set; assert the peer's
emitted readback record names each field and reports each value exactly. Force a mutation in the sent
value and confirm the comparison goes RED.

- [ ] T036 [US2] Implement the comparator's exact-set field comparison in `tests/interop/support/`: each **readback** record compared against its paired **sent** record (FR-006), by **value-equality on decoded values**, not byte-equality on rendered text — existing normalization already special-cases `52`, `10`, `60`, `11`, `37`, `17`
- [ ] T037 [US2] Key records on `MsgSeqNum(34)`, the direction the message travelled, and an `occurrence` ordinal (FR-005 · C-9). ⚠️ `direction` is a component of both the join key and the **stable completeness key**; its wire values are pinned in `spec.md` § *Conversation census* and are **absolute** — written from fixpp's point of view regardless of which process emits the record. All three emitters must write the identical string
- [ ] T038 [P] [US2] Implement C-3/C-4: every body field the peer parsed appears, and group **instances** carry a path, not just a `NoXxx` count. ⚠️ C++ trap — instances live in `m_groups`; ⚠️ Java trap — `getGroups(int)` is `computeIfAbsent`, so the walk **mutates** the message it observes (C-5)
- [ ] T039 [P] [US2] Implement C-6's canonical header/trailer partition — the **union of both engines' built-in lists ∪ the dictionary's `<header>` block**, with tag `1156` (`ApplExtID`) classified as **header**. ⛔ An emitter that classifies 1156 by its **own** built-in list is a violation: QuickFIX-J's `isHeaderField` carries `ApplExtID` and QuickFIX-cpp's does not
- [ ] T040 [US2] Extend the cross-language golden fixture at `phase-9-harness/interop-readback-fixture/` from **two** emitters to **all three** (QuickFIX-cpp counterparty · QuickFIX-J counterparty · **fixpp**) and add **tag 1156** (C-7). ⛔ **Extend it, do not replace it** — the committed two-way half carries a re-derivation recipe and a proven can-it-fail check (C-7 · FR-004)
- [ ] T041 [US2] Implement FR-025's single decision for non-UTF-8 bytes and **all** escape classes, identically in all three hand-rolled emitters — none of the three has a JSON library, and the languages differ in string model (Java `String` is UTF-16, C++ `std::string` is bytes), so an unstated decision **diverges** rather than deferring
- [ ] T042 [US2] Implement FR-016c: a message for which **no** readback record arrived FAILS its cell; an empty or absent record is never a pass and never a skip
- [ ] T043 [P] [US2] Forced-miss arms for the comparator (quickstart Step 4): a wrong field value ⇒ RED naming the path and `value_mismatch`; an omitted required field ⇒ RED classified `missing`; a field the sent record does not declare ⇒ RED classified `spurious` — ⚠️ the class a **subset** comparison cannot see
- [ ] T044 [P] [US2] Forced-miss arm: a value containing each escape class — `"`, `\`, a byte `< 0x20`, multi-byte UTF-8, and a **non-UTF-8** sequence — from **each of the three emitters**
- [ ] T045 [P] [US2] Forced-miss arm: a field entry emitted in the **engine's walk order** rather than sorted by canonical parsed path, in **any one** of the three emitters ⇒ C-7 RED, that emitter's bytes diverging from the other two
- [ ] T046 [P] [US2] Forced-miss arms: group instances emitted as a bare `NoXxx` count with no members ⇒ RED (C-3 · C-4); enumeration driven by `getGroups(tag)` per field on the QuickFIX-J side ⇒ RED because the walk mutates the message (C-5)
- [ ] T047 [P] [US2] Forced-miss arm: the readback file opened in **append** mode across two runs ⇒ RED — stale records from run *n−1* must not satisfy run *n* (R-4)
- [ ] T048 [US2] **Spurious-hit** arm (FR-018): derive the `sent` record's `fields` **from the serialized frame** and mutate the frame after intent capture — a test-only hook rewrites `Account(1)` after the intent is taken, so the two sides agree while measuring the wrong thing ⇒ RED (C-8)
- [ ] T049 [US2] **Spurious-hit** arm: **empty intent vs empty readback** — the comparator is handed `∅` on both sides and must **reject**, never pass on `∅ == ∅` (FR-018 · FR-016c)
- [ ] T050 [US2] **Spurious-hit** arm: a **hand-written `typed_accessor_arm` attestation** — write the value as a literal in the counterparty source and remove the compile definition; the hello still announces a conforming value while nothing attests the arm ran ⇒ RED
- [ ] T051 [US2] **Spurious-hit** arm, **compile-time**: mutate `interop_counterparty_main.cpp` / `InteropCounterparty.java` (or the shared typed-read adapter TU the production call site depends on) so a declared typed read names a field the message does not declare ⇒ the build FAILS. ⚠️ Assert the arm's **own diagnostic**, not merely a non-zero exit

**Checkpoint**: US1 and US2 both work independently. The peer's parse is observable, comparable, and
every comparator assertion has been shown RED for the right reason.

---

## Phase 5: User Story 3 — Round-trip fidelity in both directions, all four combos (Priority: P2)

**Goal**: One scripted conversation — Logon, admin repertoire, business messages, Logout — in each of
the four role × flavour combinations, with every business message verified for exact field values in
the direction it travelled.

**Independent Test**: Run the conversation in all four combos; confirm each business message has a
named per-field witness in both directions, including the fixpp-acceptor direction that today has none.

- [ ] T052 [US3] Implement the eight conversation cells (4 combos × 2 arms) in `tests/interop/conversation/`, driven by `conversation_script.yaml`, running the whole repertoire **within a single session per cell** (FR-008 · FR-009 · SC-001)
- [ ] T053 [US3] Implement FR-003a's `sent` record on the **peer** side as the source of truth for the peer→fixpp direction. ⚠️ This is why FR-003a exists: the counterparty generates `OrderID`/`ExecID` at run time (`const int seq = ++id_counter_;` then `"ORD" + …` / `"EXC" + …`), so those values are knowable only inside the peer and **no declarative script can supply them**
- [ ] T054 [US3] Assert fixpp's typed read tier returns the exact field values the peer set when fixpp is the **acceptor** — closing the direction that today has no field-value assertion (FR-007 · SC-002)
- [ ] T055 [P] [US3] Implement FR-003b's per-message declaration of which fields are read via the peer's typed accessors. ⚠️ **No artifact-level observable can witness a typed accessor running** — both engines' typed getters return the caller's own object (`FieldMap::get` returns `(FIELD&)(MAP).getField(field)`; QuickFIX-J's `get(Symbol)` calls `getField(value)` then returns `value`), so nothing emitted can distinguish a typed read from a generic one. The witness is the **compile-time** arm (T016, T051), not a record field
- [ ] T056 [P] [US3] Exercise the admin repertoire — TestRequest/Heartbeat, ResendRequest, SequenceReset-GapFill, Reject — **within the same session**, not as separate cells (FR-008)
- [ ] T057 [US3] Implement C-11's **live-path** charset arm: `B-05` (`G` OrderCancelReplaceRequest, fixpp-originated) declares `EncodedTextLen(354)`/`EncodedText(355)` with a value containing the byte `0xff`, driven **over the wire** on C3/C4, and `value_b64` for path `355` must equal the base64 of the wire bytes. ⚠️ The synthetic C-7 fixture **cannot** discharge this — it constructs the message inside the emitter and never enters the engine's decoder
- [ ] T058 [P] [US3] Forced-miss arm: a message the peer should Reject ⇒ the cell fails. ⚠️ Tolerate QuickFIX-cpp **disconnecting** instead of rejecting — only QuickFIX-J 3.0.1 is confirmed to emit `Reject(35=3)` on the pinned malformed input (`KNOWN-LIMITATIONS.md:87-106`), and neither outcome may be treated as a fidelity pass
- [ ] T059 [P] [US3] Forced-miss arm: a replayed step delivered once where the census declares **two** occurrences ⇒ RED as **`occurrence_count_mismatch`**, not as a field-level `missing` (FR-005 · W-7)
- [ ] T060 [US3] Declare QuickFIX-J-only arms explicitly rather than skipping them silently: `L-021-3` records that QuickFIX-cpp strips `PossDupFlag(43)`/`OrigSendingTime(122)` and exposes no injection knob, so any arm needing peer-originated hostile input is QF-J-only

**Checkpoint**: All four combos run the full conversation with per-field witnesses in both directions.

---

## Phase 6: User Story 4 — Both inbound-validation arms, proven equivalent (Priority: P2)

**Goal**: The whole conversation runs twice per combo — validation off (the shipped default) and on —
and the two arms are shown to accept identical traffic, with the validator proven **alive**.

**Independent Test**: Run both arms of one combo and assert the accepted-message sets are identical;
then seed a message the dictionary should reject and confirm the arms diverge.

⚠️ **Agreement between the arms is exactly what a DEAD validator produces.** FR-011 does not close
that — verifying a dictionary is *loaded* establishes presence, not that the validator **ran** or that
it **can reject**. The divergence probe is the deliverable that does.

- [ ] T061 [US4] Run each cell under both arms and assert the accepted-message sets are **identical**, with the assertion stating so explicitly (FR-010 · SC-004)
- [ ] T062 [US4] Implement **FR-010a's divergence probe**: a message the loaded dictionary should reject, run as **two** `validator-positive-control` runs — one per arm, each with its **own `run_id`** — demonstrating the validator can reject (SC-004a)
- [ ] T063 [US4] Implement **FR-011a's runtime arm attestation**: fixpp's hello carries `has_validator` read from `Session::has_validator_for_test()` on the **live** session after `open()`, plus `dictionary_digest` (lowercase-hex SHA-256 of the dictionary XML the session loaded) — data-model §1a
- [ ] T064 [US4] Enforce E-6: the run's arm attestation MUST equal `arm == "validation-on"`, so a silently-misconfigured arm cannot read as agreement
- [ ] T065 [US4] Implement FR-012's named-finding reporting for any divergence between the arms, naming the message and the validator finding — a divergence is a finding **in either direction**: our dictionary rejecting legitimate peer traffic, or failing to reject what it should
- [ ] T066 [US4] Implement the `validation_pairs:` section per FR-012a and E-7: a pair MUST name **two DISTINCT runs with OPPOSITE arms**, and the 16-pair conformance inventory must be complete (E-7a · W-3d)
- [ ] T067 [P] [US4] Implement E-7b: at least one `authoritative: true` `kind: validator-positive-control` pair MUST exist
- [ ] T068 [P] [US4] Implement E-7c: for every `authoritative: true` pair, **both** referenced runs must **still** be `authoritative: true`, so a later retry that demotes a run also demotes every pair referencing it
- [ ] T069 [US4] **Spurious-hit** arm: launch a `validation-on` cell with `validate_inbound_messages` forced false — fixpp's hello carries `has_validator: false` while the row claims `arm: validation-on` ⇒ RED (SC-009c)
- [ ] T070 [P] [US4] **Spurious-hit** arm: a **degenerate validation pair** whose `off_run_id` equals `on_run_id`, and as a second fixture one naming two distinct runs that recorded the **same** arm ⇒ RED (E-7)
- [ ] T071 [P] [US4] **Spurious-hit** arm: an **empty `validation_pairs:` section**, the artifact otherwise complete ⇒ E-7a RED. ⚠️ This is the arm that closes *"zero pairs is green"* — force it against the **otherwise-complete** artifact, not a stub
- [ ] T072 [P] [US4] **Spurious-hit** arm: a `validation_pairs:` section carrying **15 of the 16** conformance pairs — the other 15 well-formed and E-7 passing on every one ⇒ RED on the inventory equality, not on pair well-formedness (E-7a · W-3d · SC-009d)

**Checkpoint**: Both arms run for every cell, their equivalence is asserted, and the validator is
proven alive rather than merely present.

---

## Phase 7: User Story 5 — Evidence a catalogue flip can stand on (Priority: P3)

**Goal**: A machine-readable record showing, per catalogue row, which named witness proved it, in
which direction, against which counterparty at which version, from a run that demonstrably happened.

**Independent Test**: Produce the evidence records, then confirm the schema check rejects a row
claiming a pass with no corroborating run artifact.

- [ ] T073 [US5] Implement FR-013/FR-013a's identity model in `tests/interop/witness_evidence.yaml` and the promotion command: the **logical cell** separated from the **run**, with `cell_id ≡ (combo_id, arm)`
- [ ] T074 [US5] Implement FR-014's corroboration: a `status: pass` MUST be corroborated by a stream that agrees with it, evaluated at **promotion time where the artifacts are** (E-1b), with a **machine-independent reference to the ledger entry** — never an absolute path into a run directory (FR-014b)
- [ ] T075 [US5] Implement E-1a's manifest row identity `(cell_id, config)` — **32 rows**, retries never committed — and E-1c's *exactly one `kind: conformance` run with `authoritative: true` per `(cell_id, config)`*, retries carrying `authoritative: false` (E-1a · E-1c · SC-009b)
- [ ] T076 [P] [US5] Implement E-2: `counterparty_version` sourced from the **hello record**, never from a config file — a config says what was requested, the hello says what ran
- [ ] T077 [P] [US5] Implement E-3: `counterparty_digest` is the digest actually used, not a tag (FR-016b)
- [ ] T078 [P] [US5] Implement E-4: each of the **four** configs emits its **own** row; folding one into another is a violation
- [ ] T079 [US5] Implement FR-014a/E-5's infrastructure-abort terminal state: a run killed by `ENOSPC` is recorded as **`error:enospc`** — never `pass`, `skip`, `n/a` or `fail`
- [ ] T080 [US5] Implement FR-015a's two-level result structure (cell / witness) with the vocabulary used consistently, and FR-015c's witness rows carrying `combo_id`, `cell_id`, `config`, `run_id`, `arm`, **`kind`** and **`authoritative`** (W-1)
- [ ] T081 [US5] Implement FR-015b's **exact-set completeness gate**: W-2 derives the per-run expected set **from the conversation script, never hand-listed**; W-3 makes a missing witness **FAIL**; W-4 accumulates rows across configs with no config overwriting an earlier one's; W-5 runs the gate again after the last configuration over the union, as a **second reading, never as the only one**
- [ ] T082 [US5] Implement W-2a: the script-derived set asserted **exactly equal** to `census.yaml`. ⛔ The expected population must be re-derived from the **census table**, not from the script file — deriving both sides from the script makes the equality agree with itself
- [ ] T083 [US5] Implement W-3a: π(`kind: conformance` authoritative rows of config `c`) = π(same, config `c'`) for every ordered pair of the four configs, **and** each equals the census's 100 keys. ⚠️ Scoped to `conformance` — a `validator-positive-control` run's rows never enter π
- [ ] T084 [P] [US5] Implement W-3b (the set of `(cell_id, config)` slots carrying a conformance authoritative run **equals** the 32-slot inventory — equality, not containment) and W-3c (exactly one such run per slot)
- [ ] T085 [P] [US5] Implement W-6: `skip` may not be produced by a missing readback record — that is `fail` (FR-016 · FR-016c)
- [ ] T086 [P] [US5] Forced-miss arms on the schema check: a falsified `cell_results.yaml` row (`pass`, no evidence) ⇒ RED (E-1); `pass` naming a **ledger entry that does not exist** ⇒ RED; a ledger `witness_count` differing from the census figure for that slot ⇒ RED; an `ENOSPC` abort recorded as `fail` ⇒ RED (E-5 · SC-005)
- [ ] T087 [P] [US5] Forced-miss arms on the completeness gate: delete one witness ⇒ W-3 RED; delete a business step from the script ⇒ the census's 100 keys disagree with the script-derived projection ⇒ RED; add a script step that produces no witness ⇒ RED in the other direction (W-2a)
- [ ] T088 [P] [US5] Forced-miss arm: a slot with **no** authoritative run ⇒ schema check RED on the 32-slot inventory **equality** (W-3b)
- [ ] T089 [US5] Forced-miss arm: a promoted bundle whose stream's `hello` carries a **different `run_id`** from the row ⇒ promotion RED **because the stream disagrees** — the fixture must separate *presence* from *agreement*
- [ ] T090 [US5] **Spurious-hit** arm: run one combo of one config and none of the other three — that config's π is missing 12–13 of the census's 100 keys while the other three are complete ⇒ completeness gate RED
- [ ] T091 [US5] **Spurious-hit** arm: drop one whole configuration from the matrix — that config contributes zero keys and the **union is unchanged** ⇒ RED. ⚠️ Deleting a single witness row does **not** exercise this: the union hides a whole-config absence
- [ ] T092 [US5] **Spurious-hit** arm: a second (retry) run for one `(cell_id, config)` slot — two runs claim one slot, and because π drops `run_id` the duplicate rows **collapse**, so the completeness gate alone cannot see it ⇒ RED via W-3c
- [ ] T093 [US5] **Spurious-hit** arm: a stream carrying a `hello` and **no `terminal`** record — the pre-conversation hello is present and every field it carries checks out, while nothing attests a conversation happened ⇒ RED (C-10)
- [ ] T094 [P] [US5] **Control** arms that must stay GREEN: a ledger carrying a `kind: validator-positive-control` run for a cell whose slot already holds a conformance run; and π with the 100-key equality over that same ledger — π ranges over `kind: conformance` rows only, so a control run's witness rows never enter W-3a

**Checkpoint**: The evidence output is sufficient for row 4c to cite a specific named witness per
catalogue row, and every gate over it has been shown RED for its own reason.

---

## Phase 8: Polish & Cross-Cutting Concerns

- [ ] T095 Execute the 32-run matrix (8 cells × 4 configs) per `quickstart.md` Step 3, sequencing the four **targeted** builds so the disk constraint is respected — four FULL builds cannot fit, four TARGETED builds can (FR-021 · FR-023a). Every configure/build is gated by `ci/disk-preflight.sh`. All 32 runs must complete and emit their own row, with **zero** folded (SC-010 · E-4)
- [ ] T096 Treat the **`tsan`** configuration as **bring-up, not a config flip** (FR-022). TSan has never run on this surface; timing changes under instrumentation can move heartbeat and test-request cadence enough to break a session that passes under `normal`. That outcome is a **finding to record and resolve**, not a reason to mark the arm `n/a`
- [ ] T097 [P] State FR-023b's **second bound** wherever a sanitizer result is reported: only fixpp is sanitizer-instrumented; the QuickFIX counterparties run as unmodified binaries (FR-023)
- [ ] T098 Implement SC-011: show each sanitizer arm genuinely executed the conversation, rather than reporting clean because it never ran
- [ ] T098a Implement the UBSan arms `quickstart.md` § *Step 4* names for FR-021a: the test-only plant in `library/tests/interop/support/` (off by default, enabled only by an explicit test variable, printing a marker line before the undefined behaviour); the **forced miss** — planted cell under `ubsan` ⇒ FAILS with UBSan's `runtime error:` line asserted in the cell log; its **mutation** — T008a's injection removed ⇒ the same planted cell PASSES; and the **spurious-hit** run — planted cell under `normal` ⇒ marker present, no `runtime error:`, cell PASSES. ⛔ Run each against the unmutated tree first (rule 3); roll mutations back from scratchpad copies, never `git checkout`
- [ ] T099 Run FR-020's regression obligation: every existing passing cell outside the narrow business set continues to pass, or each change is characterised. ⛔ **This is the measurement the counterparty republish currently lacks** — the inherited work argues safety from the opt-in gate rather than measuring it (FR-020 · SC-008). ⛔ **Read each cell's status from that run's own step log, never from the job's `.conclusion` — whatever the per-consumer re-derivation in `plan.md` § *External obligations* finds.** Wherever a consumer tolerates a skip, a cell that never ran would otherwise read as "continues to pass"; reading the step log unconditionally means no result is cited on the strength of a green job (FR-016b)
- [ ] T100 [P] **Control** (`quickstart.md` § *Step 4* → § *Controls*, not § *Forced-miss arms* — it was filed there while its own expectation called it a control): the **pre-existing `pass` rows already committed in `tests/interop/cell_results.yaml`, unmodified** ⇒ the schema check stays **GREEN**, proving the conditional-field rule of T027 did not break FR-020. ⛔ **Range over the population as it stands when the arm runs — re-derive it, never hardcode a count**: a hardcoded population silently stops covering the rows added after it was written, including the ones 089 itself adds
- [ ] T101 Implement FR-018a/SC-005a: run the cells against a counterparty build with the readback channel **removed** ⇒ **every** affected cell RED — zero pass, zero skip. Jointly exercises FR-016a and FR-016c
- [ ] T102 [P] Forced-miss arms on the capability handshake: a peer announcing a `readback_protocol` **older** than this cell's declared minimum ⇒ cell FAILS; a peer announcing **no hello at all** ⇒ cell FAILS, and ⚠️ assert the failure text does **not** contain `unavailable:` — `parse_gtest_status` greps that token into `skip:`, turning a hard failure into a silent skip (C-1 · C-2 · FR-016a)
- [ ] T103 Forced-miss arm: run a cell against an image pinned by digest that **omits** `typed_accessor_arm` — a counterparty published *before* the compile arm existed ⇒ cell FAILS (C-12). ⚠️ **The fixture is any counterparty image published BEFORE the typed-accessor compile arm existed** — the pre-089 digest T004(b) names for rollback is one such image. ⛔ **Do not trust a digest written here — re-derive an available one at run time**: `gh api users/CatalinSerafimescu/packages/container/fixpp-interop-counterparties/versions` lists the active versions, and `docker pull` **by digest** is the only pullability oracle — a pruned version answers `manifest unknown`, and this package's versions have been pruned and restored before, so presence is a fact about a moment
- [ ] T104 **Repoint 089's own cells at the new image, then evidence that they ran — the *depend* half of FR-026's ordering.** ⛔ **This task deliberately does NOT restate the republish sequence.** T003 performs publish-and-verify; T004 carries the sequencing rule and the rollback. An earlier revision of this task restated that sequence as a third copy and drifted into prescribing *"pin both consumers"* — the option FR-026 rejects in capitals, because pinning the existing cells away from the new image is what makes FR-020 undischargeable. The copy is removed rather than corrected: a third statement of an ordering is what generated the drift. (a) ⛔ **Only after FR-020 has reported green against the new image (T003(c)/T099)**, repoint 089's own cells to the new digest per FR-016b, reading that digest from the publish run's **step log** and never `.conclusion` (T003(b)). ⚠️ **FR-016b itself is untouched by FR-026** — 089's cells still pin by immutable digest for recoverability, and T077/C-12/E-3 depend on that; what `69a5112c` dissolved is the *override mechanism* for a workflow-level pin that no longer exists. (b) Confirm from the **step log** of each run that exercises a repointed cell that the cell **actually executed** — never from `.conclusion`. ⛔ A green job is not evidence that a cell ran (FR-016b); this step is owed whatever the per-consumer re-derivation in `plan.md` § *External obligations* finds, because it is 089's own evidence
- [ ] T105 [P] Verify the RC-A sweep still holds: walk **every** `##` heading of `data-model.md` — ⚠️ the numbering is **not contiguous**, so a `## N.` pattern silently drops `## 1a.` — and confirm each resolves to a row in `plan.md` § *External obligations* or to one of the two named exceptions (§8 the disk preflight reading, §7 the hand-authored conversation script)
- [ ] T106 [P] Run `quickstart.md` end to end as written, including Step 0's disk preflight, and confirm every arm in Step 4 has been executed and recorded. ⚠️ **Before believing any arm, run it against the unmutated tree and confirm it is GREEN there**, so a RED is attributable to the mutation rather than to a harness broken in a way that reddens everything. **Every RED cell must assert its OWN diagnostic, not merely a non-zero exit** (FR-017 · FR-018 · SC-003)
- [ ] T107 Update `brain/components/quickfix-compat.md` with what this feature settled — in particular that no artifact-level observable can witness a typed accessor running, and that the compile-time arm is the substitute — and run **both** halves of the brain freshness check locally, using the wrapper `research/G19-fix-fpml-iso20022/tools/brain-external-sweep.sh` for the external half. ⛔ Never bare `check_brain.py sweep`: it exits 0 on *"no refs_external declared"*
- [ ] T108 **SC-007's end-to-end citation demonstration — an ARTIFACT, not a checkpoint claim**: take one `kind: conformance` run that has been promoted with `authoritative: true`, and produce the citation a catalogue row would carry, all the way through: catalogue row → witness row (`combo_id`, `cell_id`, `config`, `run_id`, `arm`, direction, `script_step_id`, `occurrence`) → the `runs:` ledger entry it names → that entry's `terminal_state: completed` and `witness_count`. Record the resolved chain. ⛔ **The Phase 7 checkpoint prose does NOT discharge this.** SC-007 says *"demonstrated by producing the citation end to end for at least one row"*, and a checkpoint sentence asserting the evidence *"is sufficient for row 4c to cite a specific named witness"* is the *"X is declared / specified"* shape this bundle's round-2 acceptance rule scores **ABSENT**. ⚠️ The demonstration is of the **citation mechanism**, not of a status flip: the narrow message set maps to `A-001`, `A-003`, `A-004`, `A-006`, `A-007`, all **already `done`**, so this feature closes zero tail rows by itself (SC-007 · FR-015a · FR-014b)

### Mandatory close-out tasks (ALWAYS emit — Gate-B preconditions, Article XVII §8)

- [ ] T109 [P] **Catalogue close-out**: flip every feature-owned OFFICIAL row in `spec/feature-catalogue.md` from `in-progress`/`backlog` → `done` with its PR / evidence ref, AND add or update its matching `spec/coverage-index.md` entry. ⚠️ This feature **produces evidence and flips no catalogue rows of its own** — row 4c is the catalogue-closure feature that consumes this evidence — so if the feature-owned set is empty, record that verdict explicitly rather than leaving the task silently unexecuted
- [ ] T110 **Feature-completeness audit (FINAL task)**: assert against the merged tree that (i) every `tasks.md` row is `[X]` or carries an explicit waiver rationale; (ii) every spec `FR-` and `SC-` maps to a landed test AND a landed implementation; (iii) every feature-owned OFFICIAL catalogue row is `done` with a matching `coverage-index.md` entry. Record the verdict (100% or fully-waived) in `.specify/decisions/089-quickfix-interop-conversation-verify.md` under `## Completeness`, or in a sibling `089-quickfix-interop-conversation-completeness.md`. `/gate-b` pre-flight 4d **HARD-BLOCKS** without this record

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies — start immediately. T001 → T002 (thresholds before the gate). T003 → T004 (the publish T003 performs is what T004's sequencing rule and rollback protect).
- **Foundational (Phase 2)**: Depends on Phase 1. **BLOCKS all user stories.** T019 is the gate on everything the counterparty emits — without it the inherited emitters are unreachable code.
- **User Stories (Phase 3+)**: All depend on Phase 2.
  - **US1 (P1)** — no dependency on other stories.
  - **US2 (P1)** — depends on US1 (a readback from a peer parsing without a dictionary is generic field-map access, not typed parsing).
  - **US3 (P2)** — depends on US1 + US2.
  - **US4 (P2)** — depends on US1 + US3 (it runs the whole conversation twice).
  - **US5 (P3)** — depends on US3 + US4 producing runs to record.
- **Polish (Phase 8)**: Depends on all five stories. T104 additionally depends on T003/T004.

### Within Each User Story

- Forced-miss and spurious-hit arms are written **with** the assertion they arm, not deferred to Phase 8. FR-017/FR-018 make the arm part of the assertion's definition of done.
- A **forced MISS cannot catch a spurious HIT** — the two kinds do not substitute for each other. Every guard needs both.
- An arm is **not written until its observable exists**: name what differs before writing the arm, or it measures its own setup.

### Parallel Opportunities

- T005–T011 are all `[P]` — different files, no shared state.
- T014, T017 are `[P]` within Phase 2; T012 and T013 must **not** be parallelised by the same agent, since their whole value is independent provenance.
- Within US2: T043–T047 are independent arms on different fixtures.
- Within US5: T084–T088 and T094 are independent.

---

## Parallel Example: User Story 2

```bash
# The four independent forced-miss arms:
Task: "T043 comparator field-class arms (value_mismatch / missing / spurious)"
Task: "T044 escape-class arm across all three emitters"
Task: "T045 walk-order arm (C-7 byte divergence)"
Task: "T046 group-instance and computeIfAbsent arms (C-3/C-4/C-5)"
```

---

## Implementation Strategy

### MVP First

1. Phase 1 — the disk gate, or nothing later builds reliably.
2. Phase 2 — **especially T019**, without which no counterparty emitter code executes.
3. Phase 3 (US1) — **STOP and VALIDATE**: the two named cells pass against the real dictionary.
4. Phase 4 (US2) — the capability that does not exist today. This is the real MVP boundary: after
   US2, *"live QuickFIX interop"* means something other than *"the peer did not disconnect."*

### Incremental Delivery

US1 → US2 → US3 → US4 → US5, each independently testable. US5 is last to build and is what makes the
other four usable downstream by row 4c.

---

## Notes

- ⛔ **Do not modify anything under `reference-engines/`** — unmodified upstream engines.
- ⛔ **Never run `git checkout` / `git switch` in the shared library checkout** — another session may be working there. Roll back forced-RED mutations via scratchpad copies; a `git checkout <file>` during arm testing has already destroyed uncommitted work once in this feature's history.
- **Every claim of a zero must first be shown able to report non-zero.** This repository's dominant recorded defect is an instrument that reports clean because it *could not* report anything else.
- Never propagate a pinned dependency version verbatim from an anchor doc — verify against the registry.
- Commit after each task or logical group; push only at checkpoints.
