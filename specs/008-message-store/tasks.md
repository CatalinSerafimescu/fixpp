---
description: "Phase 2 task plan for 008-message-store"
---

# Tasks: `MessageStore` Async API + Default Impls (`MemoryStore`, `FileStore`)

**Branch:** `008-message-store` | **Date:** 2026-05-20
**Input:** Design documents from `specs/008-message-store/`
**Prerequisites (required):** [plan.md](plan.md), [spec.md](spec.md), [research.md](research.md), [data-model.md](data-model.md), [contracts/](contracts/), [quickstart.md](quickstart.md)
**Design anchor:** `.specify/2e-msgstore.md` v0.4 — on conflict the design doc wins.

**Tests:** Test tasks are **required** (TDD per `[const §VII.3]`); the design doc / spec / plan enumerate **21 named test seams** that ship in this PR (FR-033). Test tasks for a story precede that story's implementation tasks per `[const §VII]`.

**Organization:** Tasks are grouped by user story to enable independent implementation and verification. T-IDs are stable references for `.specify/decisions/008-message-store-verify.md` §T per `[const §IX.1]` and the verify doc's exemption table.

## Format: `[ID] [P?] [Story] Description`

- **[P]:** Can run in parallel (different files, no dependencies on incomplete tasks)
- **[Story]:** Maps to user story (US1 / US2 / US3 / US4). Setup / Foundational / Polish phases have no story label.

## Path Conventions

All paths are relative to the library submodule root `research/G19-fix-fpml-iso20022/library/` (cwd for every Spec-Kit command per the parent `CLAUDE.md` Spec-Kit-in-submodule rule). C++ library; `include/fixpp/` for headers, `src/` for translation units, `tests/` for GoogleTest, `bench/` for Google Benchmark, `tools/` consumed unchanged.

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose:** Pre-flight Conan + build wiring + scaffold directories that this feature introduces. Setup tasks must complete before Foundational.

- [ ] T001 [P] Add `crc32c/1.1.2` Conan row to `conanfile.py` (BSD-3-Clause; CRC32C / Castagnoli polynomial 0x1EDC6F41 — research D-3) and re-run `conan install` to refresh the toolchain cache; verify `find_package(Crc32c CONFIG REQUIRED)` resolves in `cmake/Dependencies.cmake` (or equivalent existing hook).
- [ ] T002 [P] Create empty directories with a `.gitkeep` placeholder under `tests/conformance/`, `tests/perf/`, `bench/session/`, `bench/baselines/session/` (the existing `tests/session/`, `tests/fuzz/`, `bench/` survive unchanged).
- [ ] T003 Wire new GoogleTest / Google Benchmark targets into `tests/CMakeLists.txt`, `bench/CMakeLists.txt`, and the top-level `CMakeLists.txt` matrix for the 21 seams enumerated in plan.md §Project Structure (test target names mirror the test filenames `test_*` per the 007 precedent; bench targets `bench_memory_store`, `bench_file_store`). Link against `Crc32c::crc32c` for FileStore-touching targets only.

**Checkpoint:** Conan pinned, directories exist, CMake matrix resolves; no source code yet.

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose:** Types, error slots, engine-config deltas, and the `MessageStore` interface itself — every user story consumes these. **⚠️ CRITICAL:** No US tasks may begin until this phase completes.

- [ ] T004 [P] Create `include/fixpp/session/direction.hpp` per **E8 / FR-002**: `enum class direction_t : std::uint8_t { inbound = 0, outbound = 1 };` with `SPDX-License-Identifier: AGPL-3.0-or-later`; values reserved per `[const §X.4]`. Mirror contract `specs/008-message-store/contracts/direction.hpp`.
- [ ] T005 [P] Create `include/fixpp/session/seqnum.hpp` per **E9 / FR-003 / research D-1**: `using seqnum_t = std::uint32_t;` + `inline constexpr seqnum_t seqnum_min = 1;` + `inline constexpr seqnum_t seqnum_max = std::numeric_limits<seqnum_t>::max();` plus the cross-doc handoff comment naming the deferred Phase-4 session-module spec (`[2e §10] Q9`) as the canonical owner. Mirror contract `specs/008-message-store/contracts/seqnum.hpp`.
- [ ] T006 [P] Modify `include/fixpp/core/error.hpp` per **FR-021 / FR-023 / data-model §Error mapping / research D-6**: append **10** variants at unused slots **56–65** in design-doc table order — `store_io_failure` (56), `store_seqnum_gap` (57), `store_seqnum_out_of_order` (58), `store_capacity_exhausted` (59), `store_seqnum_overflow` (60), `store_factory_failed` (61), `store_visitor_aborted` (62), `store_seqnum_invalid` (63), `store_invalid_range` (64), `store_cancelled` (65). Add a header comment block documenting the `FIXPP_ERR_STORE_RUNTIME` / `..._CONSISTENCY` / `..._CONFIG` / `..._VISITOR` / `FIXPP_ERR_CANCELLED` group mapping for `2i`'s consumption. **Do NOT** define `store_concurrent_writer` (Codex P1-5) or `store_shim_timeout` (Codex C-R2-P2-1). Non-renumbering; pre-publication append per `[const §X.4]`.
- [ ] T007 Modify `include/fixpp/core/engine_config.hpp` per **FR-014a / FR-024a / data-model §Engine-config delta**: add `std::size_t max_store_memory_per_session = 1ULL << 30;` (1 GiB default per `[2e §1.2]`) AND `asio::any_io_executor file_io_executor;` (default-constructed empty per `[2e §4.3.2]`:669), both placed adjacent to the existing `default_store_factory` field at line 119. Non-breaking append; do NOT renumber or relocate existing fields. Update the validation pass (if any in 007) to forward both values into `MessageStoreFactory::make()` callsites — store-factory invocations live in `src/session/session.cpp` (007 baseline).
- [ ] T008 [P] Create `include/fixpp/session/retrieve_visitor.hpp` per **E2 / FR-004**: `enum class visit_result : std::uint8_t { cont = 0, stop = 1, abort = 2 };` + the `retrieve_visitor` class with deleted move/copy, virtual destructor, **one** pure-virtual `on_frame(seqnum_t, std::span<const std::byte> [[clang::lifetimebound]]) noexcept` returning `asio::awaitable<expected_t<visit_result>>`, and **one** overridable virtual hook `abort_error() const noexcept` defaulting to `store_visitor_aborted`. Mirror contract `specs/008-message-store/contracts/retrieve_visitor.hpp`.
- [ ] T031 [P] Create `include/fixpp/session/detail/has_flush_for_session_close.hpp` per **E11 / FR-028 / FR-029 / research D-11**: `template <class S> concept has_flush_for_session_close = requires(S& s) { { s.flush_for_session_close() } -> std::same_as<asio::awaitable<fixpp::core::expected_t<void>>>; };` in namespace `fixpp::session::detail`. Mirror contract `specs/008-message-store/contracts/has_flush_for_session_close.hpp`. Engine-internal — declaration only, no impl. **Foundational because T009's `flush_thunk_for<Self>()` `if constexpr` predicate consumes this concept** (A1 mechanism); the named concept is the canonical documentation form per FR-028 (T009 #includes this header and references the named concept, NOT an inline-requires duplicate).
- [ ] T009 Create `include/fixpp/session/message_store.hpp` per **E1 / FR-001 / FR-028 (factory-type-tag retention mechanism)**: the 4-pure-virtual interface (`store` / `retrieve` / `next_seqnum` / `reset`) with deleted move/copy + virtual destructor; each method `[[nodiscard]] noexcept`; `frame` / `visitor` carry `[[clang::lifetimebound]]`. **No public `flush()`** (N2). Also carries the **A1-pinned hook scaffolding** for FR-028 / I-17 concept-shaped non-virtual dispatch: the `flush_hook_fn` typedef (`asio::awaitable<fixpp::core::expected_t<void>> (*)(MessageStore&) noexcept`), the `template <class Self> static constexpr flush_hook_fn flush_thunk_for() noexcept` helper that uses `if constexpr (fixpp::session::detail::has_flush_for_session_close<Self>)` (T031's concept) to return either a typed thunk (`+[](MessageStore& base) noexcept { return static_cast<Self&>(base).flush_for_session_close(); }`) or `nullptr`, the protected `explicit MessageStore(flush_hook_fn hook = nullptr) noexcept` ctor stashing the hook in a private `flush_hook_fn flush_hook_` member, and the public `flush_hook_fn flush_hook() const noexcept` accessor. **This is the factory-type-tag retention mechanism** for FR-028 — engine reads `flush_hook()` once at session open per T032; close-path dispatch is one indirect non-virtual call through the stashed pointer. NO RTTI, NO `dynamic_cast`, NO extra pure-virtual (4/5 cap preserved per `[const §XIV.2]`). Mirror contract `specs/008-message-store/contracts/message_store.hpp` (contract header is silent on the engine-internal hook scaffolding around the public 4-pure-virtual interface). Depends on T004 / T005 / T006 / T008 / T031.
- [ ] T010 Modify `include/fixpp/session/message_store_factory.hpp` per **E5 / FR-005 / research D-7**: extend the 007-shipped minimal polymorphic bind-target **in place** by adding one pure-virtual `make(std::string_view sender, std::string_view target, std::pmr::memory_resource* mr, std::size_t max_store_memory_bytes, asio::any_io_executor file_io_executor) noexcept -> expected_t<std::unique_ptr<MessageStore>>`. Preserve the existing class identity, deleted move/copy, and virtual destructor — do NOT duplicate or replace the class. Mirror contract `specs/008-message-store/contracts/message_store_factory.hpp`. Depends on T009.
- [ ] T011 Modify `include/fixpp/session/session_config.hpp` callsites (if any) and `src/session/session.cpp` per **FR-005 / FR-024 / FR-025 / FR-026**: thread `EngineConfig::max_store_memory_per_session` AND `EngineConfig::file_io_executor` into each `MessageStoreFactory::make(...)` invocation at session open (the 4th and 5th parameters per FR-005); supply the engine-provided dedicated `std::pmr::monotonic_buffer_resource` as the 3rd `mr` argument when the caller has left `MemoryStore::Config::store_resource == nullptr` (FR-026 peer-not-sub-resource rule — the `store_arena`'s lifetime matches the store instance, NOT the per-message `session_arena` reset cadence); confirm the engine receives the `std::unique_ptr<MessageStore>` from `make()` and binds it to the `Session` as the sole owner with NO sharing across sessions / NO mid-session swap (FR-025; the unique_ptr surface is already in place at `session_config.hpp:106` per `[2d §4.5]` Appendix D §D.1). Do NOT change the existing 007-shipped `SessionConfig::store_factory` `std::unique_ptr<MessageStoreFactory>` field. Depends on T007 / T010.

**Checkpoint:** Types compile; the interface base + factory base are wired; user stories may proceed in parallel.

---

## Phase 3: User Story 1 — Persist and recover raw FIX frames across host crash (Priority: P1) 🎯 MVP

**Goal:** Outbound + inbound raw FIX frames are persisted in the correct ordering (`toApp → Writer::commit → store → async_write` per root cause #1) and recoverable byte-identically across SIGKILL via `FileStore`; `MemoryStore` provides the same round-trip behaviour for test / embedded use. Discharges **S-011 / S-012 / S-013 raw-frame round-trip** + the **store-side** of **S-014** + the **`[const §VII.5]` store-side raw-frame round-trip** layer.

**Independent Test:** seams 1 / 2 / 3 / 7 / 8 / 10 / 17 — round-trip + crash-survival + torn-write + outbound store-after-commit + retrieve with gaps + reset (used here for the recovery-substrate aspect) + conformance corpus replay. Driven by the deterministic scripted test-double FSM (research D-4 / Clarifications Q1).

### Tests for User Story 1 ⚠️ TDD — write these tests FIRST, ensure they FAIL before implementation

- [ ] T012 [P] [US1] Create the deterministic **scripted test-double FSM** fixture at `tests/session/_fixtures_/test_double_fsm.hpp` + `test_double_fsm.cpp` per **FR-034** / research D-4 / Clarifications Q1 — drives `toApp → Writer::commit → store → async_write` ordering and `retrieve` walks; asserts 2e-owned properties only (byte equality, mutex serialisation, awaitable-visitor span lifetime, cancellation completion shape, atomicity, durability). NOT a FIX FSM correctness test. Reused by US2 / US3 / US4 seams that need a driver.
- [ ] T013 [P] [US1] Author `tests/session/test_memory_store_round_trip.cpp` (seam 1) per **SC-001**: for each `N ∈ {1, 10, 100, 10000}` and each `direction ∈ {inbound, outbound}`, `store(seq, frame_n, dir)` × N then `retrieve(1, N, dir, byte_collecting_visitor)` produces a byte-identical sequence in seqnum order. Drives `MemoryStore` only (FileStore round-trip lives in seam 17).
- [ ] T014 [P] [US1] Author `tests/session/test_file_store_crash_survival.cpp` (seam 2) per **SC-002 / I-13 / I-16 / FR-010 / FR-013 / spec §Edge Cases (sentinel mismatch, directory contention)**: SIGKILL-fork harness with 100 successful `commit_per_message` `store()` calls; restart re-opens the live log, runs the restart algorithm, and `retrieve(1, 0, dir, visitor)` walks all 100 frames byte-identical. Include `commit_batched(N=64)` variant (loss bounded by N-1) and `commit_interval(ms=100)` variant (loss bounded by ms). **Edge-case sub-scenarios:** (a) **Sentinel mismatch on open** — corrupt the live log's sentinel `session_triple_hash` (or point a foreign file at the `<sender>__<target>.log` path) and assert `FileStoreFactory::make()` returns `store_factory_failed` per spec Edge Cases line 125; (b) **Directory contention** — open the same `<sender>__<target>.log` from a second `FileStoreFactory` instance and assert the second opener gets `store_factory_failed` via the advisory `flock` / `LockFileEx` lock per FR-013 / I-16 / spec Edge Cases line 126.
- [ ] T015 [P] [US1] Author `tests/session/test_file_store_torn_write.cpp` (seam 3; Tier-1 Linux + Tier-2 Windows variant) per **FR-012 / I-14**: programmatically truncate the live log mid-record; re-open; verify CRC32-bad record at tail is truncated cleanly (`ftruncate` + `fdatasync` Linux / `SetEndOfFile` + `FlushFileBuffers` Windows); verify surviving records still readable; verify stale `<...>.log.reset.tmp` from a crashed prior reset is unlinked before the scan.
- [ ] T016 [P] [US1] Author `tests/session/test_outbound_store_post_commit.cpp` (seam 7) per **AC US1 #3 / `[2b §4.5]`**: force the `BodyLength` digit-count grow path (2 → 3) at `Writer::commit`; FSM calls `store(seq, committed_span, outbound)`; persisted bytes carry the **finalised** BodyLength + CheckSum, never the pre-commit placeholder.
- [ ] T017 [P] [US1] Author `tests/session/test_retrieve_with_gaps.cpp` (seam 8) per **I-19 / `[FIX-SL §4.8.3]` / `[FIX-SL §4.8.5]`**: replay over `[42, 99]`, walks visitor in seqnum order; `retrieve(begin=0, …)` returns `store_seqnum_invalid` before any visitor call; `retrieve(begin=10, end=5)` returns `store_invalid_range`; `retrieve` over a never-persisted gap (mid-range) returns `store_seqnum_gap`; `retrieve(1, 0, …)` (end-of-store sentinel) treats the trailing edge as normal termination.
- [ ] T018 [P] [US1] Author `tests/session/test_store_reset.cpp` (seam 10) per **FR-010 / I-15 / SC-003**: shared between US1 (`reset()` as recovery substrate) and US3 (atomic-rename crash boundary). Verifies happy-path `reset()` truncates the log and re-initialises counters to `next_inbound = next_outbound = 1`. (Crash-cut variants live in US3 T034.)
- [ ] T019 [P] [US1] Author `tests/conformance/test_store_corpus_replay.cpp` (seam 17) per **SC-010 / `[const §VII.5]` / research D-13**: load a representative recorded FIX session byte corpus from `tests/conformance/corpus/`, round-trip every frame through `MemoryStore` AND `FileStore`, assert every frame byte-equal to the original input. **Store-side raw-frame round-trip only** — does NOT invoke a FIX FSM (the TC-001..TC-017 transitions are `005`'s discharge per research D-5).

### Implementation for User Story 1

- [ ] T020 [US1] Create `include/fixpp/session/memory_store.hpp` per **E3 / FR-006 / FR-007 / FR-026 / FR-029 / `[2e §4.2]`**: `MemoryStore final : public MessageStore` with `MemoryStore::Config { capacity_policy policy; size_t inbound_capacity; size_t outbound_capacity; size_t max_frame_bytes; std::pmr::memory_resource* store_resource; }` (defaults per E3); `capacity_policy : std::uint8_t { bounded=0, unbounded=1 }` with `[[clang::enum_extensibility(closed)]]` + `static_assert` at every switch (I-09; capacity-cap logic lands in T030 / US2). Implement the **one-PMR-allocation-at-construction** fixed-slot + fixed-slab layout (slot index table + payload slab arena); `store_arena` is a **peer** of `SessionConfig::session_arena` — NOT a sub-resource — so persisted bytes outlive any per-session-arena reset cadence (FR-026); when `Config.store_resource == nullptr` the factory binds a dedicated `std::pmr::monotonic_buffer_resource` whose lifetime matches the store instance (wired through T011's `make()` call site). Ctor invokes the `MessageStore` base with **default `nullptr`** — `MemoryStore` does NOT define `flush_for_session_close()` per FR-029; T009's A1 concept gate `flush_thunk_for<MemoryStore>()` returns `nullptr` and the engine skips the close-path call. Body of `store` / `retrieve` / `next_seqnum(_, false)` / `next_seqnum(_, true)` / `reset` (writer-mutex wiring lands in **T040** / US3; overflow check on `next_seqnum(_, true)` at `seqnum_max` also lives in T040 per FR-022). Depends on T009 / T010 / T012–T019.
- [ ] T021 [US1] **Pin `MemoryStore` as header-inline** per plan.md Project Structure (line 187) — the 007 / 006 precedent leaned header-inline for hot-path primitives and HALO-elision (`[const §XI.6]`) is friendlier to inline bodies. NO `src/session/memory_store.cpp` ships; update T003's CMake target list to omit it; add a `// MemoryStore is header-only — see include/fixpp/session/memory_store.hpp` comment to `src/session/CMakeLists.txt` recording the decision.
- [ ] T022 [US1] Create `include/fixpp/session/memory_store_factory.hpp` per **E6 / FR-014**: `MemoryStoreFactory final : public MessageStoreFactory` with `explicit MemoryStoreFactory(MemoryStore::Config cfg = {}) noexcept;` and the `make(...)` override; storage-DoS guard logic lands in T030 / US2. The 5th `file_io_executor` parameter is **accepted and silently discarded** per E6 (MemoryStore has no file-I/O work).
- [ ] T023 [US1] Create `include/fixpp/session/file_store.hpp` per **E4 / E10 / FR-008 / FR-009 / FR-028**: `FileStorePolicy` struct (`kind` enum `commit_per_message=0 / commit_batched=1 / commit_interval=2` + `batch_size` + `interval`); `FileStore final : public MessageStore` with the **7-field** `FileStore::Config` per design-doc §4.3 lines 507–537 + 1-arg `explicit FileStore(Config c) noexcept`. The ctor passes `MessageStore::flush_thunk_for<FileStore>()` to the base ctor — A1 / T009's concept gate evaluates `fixpp::session::detail::has_flush_for_session_close<FileStore>` at compile time and returns the typed thunk (non-null for `FileStore`). Declare `[[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> flush_for_session_close() noexcept` as **public** with the comment `// engine-internal: do not call directly — dispatched by the engine's Session-close sequencer per FR-028` (plan.md line 88 admits this alternative to friending; the concept must evaluate outside the friend list anyway, so doc-only discipline is the pragmatic choice). Concrete impl lands in T032 / US2. Public method signatures only — bodies in T024.
- [ ] T024 [US1] Implement `src/session/file_store.cpp` per **FR-009 / FR-011 / FR-012 / I-12 / I-13 / I-14 / `[2e §6.3]`**: on-disk record layout (16-byte header `kind | dir | reserved(2) | seq(4) | len(4) | crc32(4)` + payload + padding-to-8-byte align; CRC32 over `kind+dir+reserved+seq+len+bytes` via crc32c Castagnoli 0x1EDC6F41); sentinel record (`magic | version | session_triple_hash | crc32`); restart algorithm (sentinel verify → per-record CRC32 scan → torn-tail `ftruncate` + `fdatasync`); `store()` / `retrieve()` / `next_seqnum()` / `reset()` Linux code paths posting `pwrite` / `fdatasync` to `Config.file_io_executor` and rebinding via `[2d §6.5]` `cancellable_dispatch` (writer-mutex wiring lands in T040 / US3; `reset()` atomic-rename + durability primitive lands in T042 / US3). Counters record carries `next_inbound : uint32_t | next_outbound : uint32_t`.
- [ ] T025 [US1] Add Windows code paths to `src/session/file_store.cpp` per **`[const §II.3]` Tier-2 / FR-010 (Windows clause) / FR-011 / FR-012**: `FlushFileBuffers` for the per-record / batched / interval flush; `SetEndOfFile` for torn-tail truncation; `LockFileEx(LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY)` for the advisory open lock (T026 owns the factory side); `pwrite` equivalent via `WriteFile` with explicit offset. (Windows `MOVEFILE_WRITE_THROUGH` for `reset()` lands in T042 / US3.) Tier-2 only — guard with `#ifdef _WIN32`.
- [ ] T026 [US1] Create `include/fixpp/session/file_store_factory.hpp` + `src/session/file_store_factory.cpp` per **E7 / FR-013 / FR-014 / FR-024 / I-11 / I-13 / I-16**: `FileStoreFactory final : public MessageStoreFactory` with `explicit FileStoreFactory(FileStore::Config cfg) noexcept;` and `make(...)`. `make()` takes the advisory exclusive open lock (`flock` Linux / `LockFileEx` Windows; second opener → `store_factory_failed`); resolves `file_io_executor` per **Config-supplied-wins** (Config field non-empty → use it; otherwise the engine-threaded 5th-parameter `file_io_executor` populates the minted `FileStore::Config`; **both empty → `store_factory_failed`**); constructs the `FileStore` with the resolved Config (preserves `[2e §4.3.2]:665` required-at-construction). Storage-DoS guard lands in T030 / US2.

**Checkpoint:** US1 is fully functional with happy-path round-trip + crash-survival + torn-write + corpus replay. seams 1 / 2 / 3 / 7 / 8 / 10 / 17 pass under Tier-1; seam 3 Windows variant passes under Tier-2 nightly.

---

## Phase 4: User Story 2 — Bounded resource use and operator-visible failure (Priority: P1)

**Goal:** `bounded` `MemoryStore` reports `store_capacity_exhausted` (no silent drop, no termination); the storage-DoS construction guard rejects misconfigured `MemoryStore::Config`; `FileStore::flush_for_session_close()` drains under `Session::close(graceful)`.

**Independent Test:** seams 4 / 11 / 15 / 19 — capacity exhaustion, zero-allocator-calls under `bounded`, `flush_for_session_close` graceful-close drain.

### Tests for User Story 2 ⚠️ TDD

- [ ] T027 [P] [US2] Author `tests/session/test_memory_store_capacity.cpp` (seam 4) per **FR-006 / FR-014 / I-08 / I-11 / SC-004 / AC US2 #1 / #2 / #2a**: `outbound_capacity = 100` instance; 101st `store()` returns `expected_t::unexpected{store_capacity_exhausted}` with entry-array index unchanged and mutex released cleanly. `unbounded` variant stores 10⁵ frames without capacity error. **Storage-DoS construction guard sub-scenarios:** `(inbound + outbound) * max_frame_bytes > max_store_memory_bytes` → `store_factory_failed`; `inbound_capacity = SIZE_MAX/2 - 1, outbound_capacity = 2, max_frame_bytes = 8` overflow sub-scenario expecting `store_factory_failed` (overflow-safe checked arithmetic per FR-014); default `MemoryStore::Config` against default `max_store_memory_per_session = 1 GiB` → `store_factory_failed` by design per `[2e §1.2]`:54 (AC #2a).
- [ ] T028 [P] [US2] Author `tests/session/test_memory_store_zero_allocator_calls.cpp` (seam 15) per **FR-007 / I-10 / SC-007**: instrument a tracking PMR resource; construct `MemoryStore` with `bounded` policy; record allocator counter post-construction; execute 10⁴ `store()` calls; assert the tracking counter is **unchanged** from baseline.
- [ ] T029 [P] [US2] Author `tests/session/test_file_store_flush_for_session_close.cpp` (seam 19) per **FR-028 / I-17 / AC US2 #4 / #5**: `FileStore` under `commit_batched(N=64)` with 32 frames stored (32 < N-1 = 63, within the documented window); `Session::close(graceful)` invokes `flush_for_session_close()` via the concept-shaped non-virtual dispatch (factory-type tag — NOT `dynamic_cast`); the hook runs to completion outside phase-1's child timeout; re-open recovers all 32 frames byte-identical; `store_cancelled` is **NOT** surfaced under graceful close. `Session::close(terminal)` variant: hook is **NOT** invoked; up-to-N-1-record loss is the documented `commit_batched` contract.

### Implementation for User Story 2

- [ ] T030 [US2] Implement the storage-DoS construction guard in `memory_store_factory.cpp` (or header-inline) AND `file_store_factory.cpp` per **FR-014 / I-11**: overflow-safe checked arithmetic — `(a)` `inbound_capacity + outbound_capacity` overflow → `store_factory_failed`; `(b)` `(inbound_capacity + outbound_capacity) > 0 && max_frame_bytes > max_store_memory_bytes / (inbound_capacity + outbound_capacity)` → `store_factory_failed`; `(c)` `max_frame_bytes > Framer::Config::max_frame_bytes` → `store_factory_failed`. Same rule binds both factories; engine-resolved `max_store_memory_bytes` arrives as the 4th `make()` parameter. Implement the `bounded` / `unbounded` branches in `MemoryStore::store` per **I-08 / I-09** with the closed-2-value-enum guards (`static_assert` at every switch; runtime out-of-range-cast reject). Depends on T022 / T026 / T027.
- [ ] T032 [US2] Implement `FileStore::flush_for_session_close()` in `src/session/file_store.cpp` (public-with-doc-only-engine-internal-comment per T023; concept evaluates outside any friend list) per **FR-028 / I-17 / Appendix D §D.2**: drains any `commit_batched` / `commit_interval` pending records to disk via `fdatasync` (Linux) / `FlushFileBuffers` (Windows); returns `expected_t<void>{}` on success or `expected_t::unexpected{store_io_failure}` on mid-flush error; does NOT surface `store_cancelled` under graceful close. Wire the engine's Session-close sequencer in `src/session/session.cpp` per the **A1-pinned mechanism** (T009): at session open, call `store_->flush_hook()` ONCE and stash the returned `flush_hook_fn` on the `Session` instance as the factory-type tag; at `Session::close(graceful)`, if the stashed pointer is non-null `co_await (*hook)(*store_)` and surface `store_io_failure` on unexpected return; under `Session::close(terminal)` skip the hook entirely (Appendix D §D.2). Hook MUST run to completion outside phase-1's child timeout under graceful close. The `[2d §4.7]` per-mode effect-table row at `2d-threading.md:853` is already in place. Depends on T009 / T024 / T031.

**Checkpoint:** US1 and US2 are independently testable. Capacity errors, storage-DoS guard, and graceful-close flush all pass under Tier-1.

---

## Phase 5: User Story 3 — Concurrent-writer, cancellation, and `reset()` atomicity under host crash (Priority: P2)

**Goal:** All four methods serialise FIFO-fairly on the per-instance `async_mutex`; cancellation honours the per-method linearisation table; `FileStore::reset()` is atomic at the `rename` boundary plus the platform durability primitive (Linux parent-dir `fsync` MANDATORY / Windows `MOVEFILE_WRITE_THROUGH` MANDATORY).

**Independent Test:** seams 5 / 6 / 9 / 10 (crash-cut variants) / 16 / 18 / 20 — FIFO-fair concurrent writer, per-method cancellation, retrieve-visitor span lifetime + no-mutex-across-co_await, `reset()` atomic-rename crash, PMR poison on retrieve, session shutdown ordering, `store_seqnum_out_of_order`.

### Tests for User Story 3 ⚠️ TDD

- [ ] T033 [P] [US3] Author `tests/session/test_store_fifo_fair.cpp` (seam 5) per **FR-015 / FR-016 / I-01 / SC-005 / AC US3 #1**: two coroutines on an `asio::thread_pool` invoke `store()` on the same instance concurrently; both complete with `expected_t<void>{}` in FIFO-arrival order; neither receives `store_concurrent_writer` (the variant is not defined); TSan reports **0** races; ASan reports **0** UAFs.
- [ ] T034 [P] [US3] Author `tests/session/test_store_cancellation_contract.cpp` (seam 6) per **FR-020 / I-07 / I-13 / SC-006 / AC US3 #2 / #3**: for each of `store` / `retrieve` / `next_seqnum(_, true)` / `reset`, fire the cancellation slot **before** the method's linearisation point per `[2e §6.1.4]` table — assert `expected_t::unexpected{store_cancelled}`, no state change, mutex released; then **after** the linearisation point — assert normal completion with the operation's value and durability of the new state per the §6.3.5 platform primitives. Includes `FileStore::store(commit_per_message)` variant: cancel before `fdatasync` returns success → `store_cancelled` (frame NOT persisted, index NOT advanced); cancel after → durable, normal completion.
- [ ] T035 [P] [US3] Author `tests/session/test_retrieve_visitor.cpp` (seam 9) per **FR-017 / FR-019 / I-03 / I-04 / I-20 / AC US1 #4**: walk a 5-frame range with a visitor whose `on_frame` `co_await`s for ~100 µs per frame; under ASan-instrumented memory access assert the span returns the right bytes after the suspend; assert the store **does not hold** its writer mutex across the visitor's `co_await` (a recursive store-mutating call from inside the visitor's awaitable does NOT deadlock); mid-traversal `store()` from a sibling coroutine: assert the next visitor call observes the new state without UB and iteration stops at the original `end`; visitor returning `visit_result::abort` after frame 3 → awaitable returns `store_visitor_aborted` (default `abort_error()`).
- [ ] T036 [P] [US3] Author `tests/session/test_store_reset.cpp` **crash-cut variants** (seam 10 continued from T018) per **FR-010 / I-15 / SC-003 / AC US3 #4**: SIGKILL fork between (a) tmp-file open and tmp-file `fdatasync`; (b) tmp-file `fdatasync` and `rename`; (c) `rename` and parent-dir `fsync` (Linux only) — each path lands a coherent restart state (either prior intact log or fully-reset state; no partial / mixed-direction / "old frames + new counters" intermediate); the `reset()` awaitable did NOT return success in the prior-log cases.
- [ ] T037 [P] [US3] Author `tests/session/test_store_pmr_poison_retrieve.cpp` (seam 16) per **I-21 / `[2a §4.2]`**: visitor's caller-supplied `memory_resource` throws on allocation during the recovery path; the throw routes through `fixpp::core::detail::trap_throw` (no terminate); the awaitable completes with `expected_t::unexpected{store_visitor_aborted}`.
- [ ] T038 [P] [US3] Author `tests/session/test_store_shutdown_ordering.cpp` (seam 18) per **I-22 / SC-005**: 100 in-flight `store()` calls + `Session::close(terminal)` under TSan + ASan; all 100 awaitables complete (those past the linearisation point with `expected_t<void>{}`, those before with `expected_t::unexpected{store_cancelled}`); no UAF on `session_arena`; `~MessageStore` runs before `session_arena` release.
- [ ] T039 [P] [US3] Author `tests/session/test_store_seqnum_out_of_order.cpp` (seam 20) per **FR-018 / I-05 / AC US3 #5**: drive `store(seq=5, frame, outbound)` while `next_seqnum(outbound, false) == 1`; verify the verification fails **inside** the writer-mutex critical section (after acquire, before any slab memcpy / `pwrite`); awaitable returns `store_seqnum_out_of_order`; entry-array unchanged; mutex acquired-then-released cleanly.

### Implementation for User Story 3

- [ ] T040 [US3] Wire the per-instance `fixpp::sync::async_mutex` writer mutex into **all four** `MemoryStore` methods per **FR-015 / FR-018 / FR-022 / I-01 / I-05 / I-06 / I-18 / Opus N2-P2-2**: `store` / `retrieve` / `next_seqnum` / `reset` each `co_await` `mutex_.async_lock()` on entry (sentinel uncontended `[2f §4.3.2]` fast-path). Inside the critical section: deep-copy `frame` into slot-pool **before** any further suspension (I-02 / FR-019); verify `seq == next_seqnum(dir, false)` (FR-018 / I-05); release the mutex BEFORE the visitor's `co_await` on the `retrieve` path (FR-017 / I-03) and re-acquire to advance the snapshot iterator. Implement **`next_seqnum(dir, true)` overflow check** (FR-022 / I-18): if the current counter equals `seqnum_max`, return `expected_t::unexpected{store_seqnum_overflow}` WITHOUT incrementing — the variant is session-fatal but the store does NOT autonomously reset (the FSM in `005` surfaces this to user code per the FR-022 session-fatal contract). Implement mid-traversal mutation detection (next visitor call observes new state without UB; iteration stops at original `end`). NO `std::mutex`, NO `std::recursive_mutex`, NO `async_shared_mutex` (`[const §XV.9]` / `[SYN §3.2 Q6b]`). Depends on T020 / T033–T039.
- [ ] T041 [US3] Wire the per-instance `async_mutex` into **all four** `FileStore` methods per **FR-015 / FR-018 / FR-022 / I-01 / I-05 / I-06 / I-18**, plus the cancellable post to `Config.file_io_executor` per **FR-024 / I-13 / `[2d §6.5]`**: posts `pwrite` / `fdatasync` work via `cancellable_dispatch` and rebinds the completion to the session strand. Same deep-copy / `next_seqnum` verification as MemoryStore (T040); same `next_seqnum(dir, true)` overflow check at `seqnum_max` returning `store_seqnum_overflow` without incrementing (FR-022 / I-18) — for `FileStore` the check runs BEFORE the counter-record `pwrite` so a session-fatal overflow does NOT touch disk. `retrieve` releases the mutex before the visitor's `co_await` (I-03). Implement the §6.1.4 per-method cancellation linearisation: `store` linearises at `fdatasync` / `pwrite` (per policy); `next_seqnum(_, true)` linearises at counter-record `pwrite` + flush.
- [ ] T042 [US3] Implement `FileStore::reset()` atomic-rename in `src/session/file_store.cpp` per **FR-010 / I-15 / SC-003**: write `<...>.log.reset.tmp` (same directory as live log — no cross-filesystem) containing sentinel + counters reset to `1`; `fdatasync` the tmp; `rename(tmp, live)`; **Linux:** parent-directory `fsync` MANDATORY; **Windows:** `MoveFileExW(old, new, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)` MANDATORY (NOT optional — round-3 C-R3-P1-2). The success-return cannot happen until the platform durability primitive returns success. Edge cases: on open, unlink any pre-existing `<...>.log.reset.tmp` (the live log is the source of truth) before scanning records. Depends on T024 / T025 / T036.
- [ ] T043 [US3] Wire `trap_throw` on the retrieve recovery path per **I-21 / `[2a §4.2]`**: a PMR throw from the visitor's `memory_resource` is caught at the `retrieve()` boundary and surfaces as `expected_t::unexpected{store_visitor_aborted}`. No terminate. Depends on T040 / T041 / T037.

**Checkpoint:** US1, US2, US3 all independently functional. TSan/ASan/UBSan clean on every test target. The store is *safe* under concurrent-write / cancellation / mid-reset-crash / PMR-poison / session-close-races.

---

## Phase 6: User Story 4 — QuickFIX migration (Path B + optional `cfg_loader`) (Priority: P3)

**Goal:** v1.0 ships **Path B only** (documented incompatibility + 5-step migration recipe + config-translation helper). NO runtime adapter. The compile-time guard rejects any implicit construction path from a sync-shaped object into the async interface.

**Independent Test:** seams 11 / 12 — Path B compile-time `static_assert` guard, `cfg_loader` round-trip from a sample QuickFIX `.cfg`.

### Tests for User Story 4 ⚠️ TDD

- [ ] T044 [P] [US4] Author `tests/session/test_quickfix_compat_path_b_guard.cpp` (seam 11) per **FR-032 / AC US4 #2**: define a `FakeQuickFixStore` with the synchronous `set(seq, body) / get(begin, end, vec)` shape; the test target's compile step is the binding gate via `static_assert(!std::is_constructible_v<fixpp::session::MessageStore, FakeQuickFixStore*>);` placed at file scope — the file MUST compile cleanly today, and any future regression that admits an implicit construction path makes the `static_assert` fire and breaks the build. Add a one-line GoogleTest case with a comment recording that the binding gate is purely compile-time (no runtime assertion — the build-fails-on-regression behaviour is the entire test).
- [ ] T045 [P] [US4] Author `tests/session/test_quickfix_compat_cfg_loader.cpp` (seam 12) per **FR-030 / AC US4 #1 / SC-009**: feed a representative QuickFIX `.cfg` with `[DEFAULT]\nFileStorePath=/var/fix/store`; `cfg_to_file_store_factory("/path/to/file.cfg")` returns a `FileStoreFactory` whose `Config.directory == /var/fix/store`; mint a session via that factory (engine-threaded `file_io_executor` populates the minted `FileStore::Config`); round-trip a frame; verify byte-equality.

### Implementation for User Story 4

- [ ] T046 [US4] Create `include/fixpp/session/quickfix_compat/cfg_loader.hpp` per **E12 / FR-030**: single declaration `[[nodiscard]] expected_t<std::unique_ptr<FileStoreFactory>> cfg_to_file_store_factory(const std::filesystem::path& cfg_path) noexcept;` in namespace `fixpp::session::quickfix_compat`. **No** `EngineConfig&` back-channel; **no** runtime adapter (Path A retired in v0.3 per Codex C-R2-P2-1). Mirror contract `specs/008-message-store/contracts/cfg_loader.hpp`.
- [ ] T047 [US4] Implement `src/session/quickfix_compat/cfg_loader.cpp` per **E12 / FR-030**: parse the `[DEFAULT]` / `[SESSION]` block from the QuickFIX `.cfg`; extract `FileStorePath`; emit a `FileStoreFactory` whose `FileStore::Config.directory == FileStorePath` and whose `file_io_executor` is left default-constructed empty (engine populates at `make()` per FR-024 / I-13). Parse-failure observability is **deferred to 2k** (per fresh-loop round-1 RC#3); no `warn_log` obligation here.
- [ ] T048 [US4] Add a "MessageStore migration" section to `book/migration_from_quickfix.md` per **FR-031 / SC-009**: the §4.8.A.3 5-step recipe (inherit from `fixpp::session::MessageStore`; convert each method to `awaitable<expected_t<...>>`; replace `std::mutex` with `fixpp::sync::async_mutex`; post any sync `pwrite`/`fsync` to a dedicated executor; build a `MessageStoreFactory` returning `unique_ptr`) plus the equivalence table (`quickfix::MessageStore::set` ↔ `fixpp::session::MessageStore::store`, etc.).

**Checkpoint:** All four user stories independently functional.

---

## Phase 7: Polish & Cross-Cutting Concerns

**Purpose:** Cross-doc structural edits (FR-037 / FR-038 / FR-039 verifications), the cross-cutting alloc / bench / fuzz seams, feature-completeness audit, and `/speckit-verify` scaffolding. Catalogue Status promotion is **NOT** in this PR diff — the merger applies it at Gate-B merge per `.specify/pipeline.md` step 19 (research D-12; `feedback_pipeline_mark_done_step`).

- [ ] T049 [P] Author `bench/session/bench_memory_store.cpp` + `bench/session/bench_file_store.cpp` (seam 13) per **SC-008 / `[const §VIII.2]`**: Google Benchmark rows covering the latency ceilings table in plan.md Technical-Context (`MemoryStore::store` 200-byte / 1 KiB / `next_seqnum(false)` / `retrieve(100)` / `reset`; `FileStore::store(commit_per_message)` / `FileStore::reset`). Commit baseline files under `bench/baselines/session/` for the per-host hardware floor; CI fails on > 5% regression for MemoryStore rows / > 2× regression for FileStore disk-bound rows.
- [ ] T050 [P] Author `tests/perf/test_store_alloc_guard.cpp` (seam 14) per **SC-007 / FR-027 / `[const §VIII.5]`**: runs a 10⁴-message session under `mallocnesia` (`tools/mallocnesia/libmallocnesia.so` per memory `reference_mallocnesia_path`); asserts **0** global-heap `new` / `delete` / `malloc` between parse and `fromApp`. Cross-cuts with `tools/check_alloc.py` post-link symbol scan (consumed unchanged) — wire both into the CI matrix per T003.
- [ ] T051 [P] Author `tests/fuzz/fuzz_message_store.cpp` (seam 21) per **SC-011 / FR-033 / `[const §VII.7]` voluntary / `[const §IX.2]`**: libFuzzer harness driving random interleavings of `store / retrieve / reset / next_seqnum` against both `MemoryStore` and `FileStore`; runs under ASan + UBSan + TSan invariants on Linux/Clang Tier 1; 10-minute corpus run per quickstart.md.
- [ ] T052 [P] Verify (do NOT re-amend) `library/spec/coverage-index.md:76` per **FR-037**: line 76 reads `§4.8 | Message recovery | Y | S-011, S-012, S-013, S-014 | —` with a note that 2e discharges the **store-side** API + default impls and Phase-4 owns the FSM. (Pre-applied at Path A 2026-05-20 — verify the on-disk byte string, do not rewrite if matching.)
- [ ] T053 [P] Verify (do NOT re-amend) `library/spec/feature-catalogue.md` per **FR-038**: OSS-002 row carries the **Path B disposition** (v0.3 verdict; no runtime adapter); COM-009 row carries the **forward-compat note** pointing at `[2e §10] Q2` (row stays `backlog`). S-011 / S-012 / S-013 Status fields remain `backlog` in this PR diff — the merger applies `backlog → done` at Gate-B merge per `.specify/pipeline.md` step 19. **Do NOT** promote in this PR.
- [ ] T054 [P] Verify (do NOT re-amend) `.specify/architecture.md:598` per **FR-039**: line 598 reads `CLOSED — Path B verdict per [2e §4.8.A]; v1.0 ships documented incompatibility + migration recipe + quickfix_compat::cfg_loader config-translation surface (no runtime adapter). Disposition applied by 008-message-store Phase-4 Gate A convergence (2026-05-20) per FR-039.` (Pre-applied at Path A 2026-05-20.)
- [ ] T055 [P] Verify (do NOT re-amend) `.specify/2e-msgstore.md` Appendix D §D.3 + the live `[2e §4.4]` 5-param `make()` block per plan.md §Project Structure (pre-applied at Path A 2026-05-20).
- [ ] T056 [P] clang-tidy / clang-format / cppcheck / IWYU sweep on all newly-owned headers + translation units per `[const §IX.4]`; verify the `[[clang::lifetimebound]]` discipline on `frame` / `visitor` parameters emits no warnings; verify no `std::mutex` in any header that includes `asio::awaitable<...>` (the existing 006 grep gate covers this).
- [ ] T057 Run the full Tier-1 quickstart.md matrix locally (`linux-clang-debug`, `linux-clang-release`, `linux-clang-asan`, `linux-clang-ubsan`, `linux-clang-tsan`, `linux-clang-coverage`) per `[const §IX.2]` + `[const §IX.6]`; TSan **mandatory** clean per `[const §IX.2]`; coverage on the lcov DA/BRDA basis per `feedback_coverage_gate_lcov_basis`. Capture the alloc-guard run, the bench baselines, and the 10-minute libFuzzer corpus. Pre-condition for `/speckit-verify`.
- [ ] T058 Feature-completeness audit per `feedback_feature_completeness_gate`: cross-reference every FR (FR-001..FR-039 minus FR-035/FR-036 deferred to 2k) and every SC (SC-001..SC-012) against the implemented surface + the 21 seams; record the audit table in `.specify/decisions/008-message-store-completeness.md` (LOCAL ONLY, gitignored; 004 precedent `004-wire-codec-completeness.md`); **100% or explicitly waived** is the `/gate-b` precondition per `[const §XVII.8]`.
- [ ] T059 Scaffold `.specify/decisions/008-message-store-verify.md` (LOCAL ONLY, gitignored) per `[const §XVII.8]` / `[const §IX.1]` / `feedback_codecov_patch_vs_lcov_da_brda_gate`: enumerate the touch-surface files (data-model §Coverage discipline), the exempt-by-inspection rows (`error.hpp` enum-slot append; `engine_config.hpp` default-initialiser lines), and the §T section that will record per-T disposition (SPEC-FIXED / DD-DECIDED §X / WAIVED:<reason>) post-`/speckit-implement` per `[const §IX.1]` Article XVII §8 paired-evidence rule. Reserve a "Codecov/patch external soft gate" row pre-formatted with the PR #73 / PR #74 precedent (waiver-with-rationale; binding gate is per-file lcov DA/BRDA).

**Checkpoint:** Feature is implementation-complete, locally verified, and ready for `/simplify` (Article XVI §7) → `/speckit-verify` (Article XVII §8) → `/gate-b` (Article XVII §2).

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)** → **Foundational (Phase 2)** → **US1 (Phase 3)** in priority order
  - US2 (Phase 4) can begin after US1's T020 (`memory_store.hpp` declarations) and T023 (`file_store.hpp` declarations) land — capacity / flush impl is additive to the same files.
  - US3 (Phase 5) requires US1's T020 + T024 (file_store.cpp Linux paths) + T023 (file_store.hpp) — mutex wiring lives in the same files.
  - US4 (Phase 6) requires US1's T026 (`FileStoreFactory`) — `cfg_loader` returns a `FileStoreFactory`.
- **Polish (Phase 7)** depends on all desired user stories; T049 / T050 / T051 (bench / alloc / fuzz) need the full impl. T052–T055 (verify cross-doc) and T056 (lint sweep) can run earlier. T057–T059 are the pre-`/speckit-verify` gate.

### User Story Dependencies

| Story | Depends on (Foundational + prior US) | Independently testable after |
|---|---|---|
| **US1 (P1) MVP** | T004–T011 + T031 | T012–T026 complete; seams 1 / 2 / 3 / 7 / 8 / 10 / 17 green. |
| **US2 (P1)** | Foundational + US1 T020 (MemoryStore decls) + T023 (FileStore decls) | T027–T032 complete; seams 4 / 15 / 19 green. |
| **US3 (P2)** | Foundational + US1 T020 + T023 + T024 + T025 (file_store.cpp Linux + Windows decls) | T033–T043 complete; seams 5 / 6 / 9 / 10-crash-cuts / 16 / 18 / 20 green under TSan + ASan. |
| **US4 (P3)** | Foundational + US1 T026 (FileStoreFactory) | T044–T048 complete; seams 11 / 12 green. |

### Within Each User Story

- TDD: every `tests/*` task is authored to **fail first** before the corresponding implementation task lands (`[const §VII.3]`).
- Models / types before services / impls (Foundational T004–T010 are the "models").
- Services before integration (US1's MemoryStore impl T020 / FileStore impl T024 before US2's flush wiring T032).
- Each story may be paused at its checkpoint and validated independently.

### Parallel Opportunities

- All Setup tasks marked **[P]** can run in parallel (T001 / T002 are independent files).
- All Foundational tasks marked **[P]** can run in parallel (T004 / T005 / T006 / T008 touch different files); T009 / T010 / T011 serialise.
- Once Foundational completes, US1 test authoring (T012–T019) can run fully in parallel; US1 impl (T020–T026) parallelises across `memory_store` / `file_store` / `*_factory` files.
- US1, US2, US3, US4 can be staffed in parallel by different engineers once Foundational completes; conflicts only at the `MemoryStore` / `FileStore` files where US3's mutex wiring (T040 / T041) is the integration point.
- Polish tasks T049 / T050 / T051 / T052 / T053 / T054 / T055 / T056 are all parallelizable across different files.

---

## Parallel Example: User Story 1 (MVP) test authoring

```bash
# Launch all US1 tests in parallel (different files, no dependencies):
Task: "tests/session/test_memory_store_round_trip.cpp (seam 1)"   # T013
Task: "tests/session/test_file_store_crash_survival.cpp (seam 2)" # T014
Task: "tests/session/test_file_store_torn_write.cpp (seam 3)"     # T015
Task: "tests/session/test_outbound_store_post_commit.cpp (seam 7)"# T016
Task: "tests/session/test_retrieve_with_gaps.cpp (seam 8)"        # T017
Task: "tests/session/test_store_reset.cpp (seam 10 happy path)"   # T018
Task: "tests/conformance/test_store_corpus_replay.cpp (seam 17)"  # T019
```

```bash
# Launch all US1 impl headers in parallel (different files):
Task: "include/fixpp/session/memory_store.hpp"          # T020
Task: "include/fixpp/session/memory_store_factory.hpp"  # T022
Task: "include/fixpp/session/file_store.hpp"            # T023
# Then serialise into:
Task: "src/session/file_store.cpp Linux paths"          # T024
Task: "src/session/file_store.cpp Windows paths"        # T025
Task: "include/fixpp/session/file_store_factory.hpp"    # T026
```

---

## Implementation Strategy

### MVP first (User Story 1 only)

1. Phase 1 Setup → Phase 2 Foundational → Phase 3 US1.
2. **STOP and VALIDATE:** seams 1 / 2 / 3 / 7 / 8 / 10 / 17 green on Tier 1; FileStore crash-survival demonstrated on Linux; corpus replay byte-equal. MVP slice is the recovery substrate the FSM stakes everything on — without it, no later story matters.

### Incremental delivery

1. Setup + Foundational → types compile, EngineConfig deltas in place.
2. + US1 → recovery substrate works (seams 1/2/3/7/8/10/17) — MVP demo-able.
3. + US2 → operator-visible capacity errors + graceful-close drain (seams 4/15/19).
4. + US3 → safety under concurrent-writer / cancellation / reset-crash / PMR-poison / session-close (seams 5/6/9/10-cuts/16/18/20).
5. + US4 → QuickFIX migration discharge (seams 11/12).
6. Polish phase delivers bench + alloc + fuzz + completeness audit + verify scaffold; `/simplify` then `/speckit-verify` then `/gate-b`.

### Parallel team strategy

With multiple developers post-Foundational:

- **Developer A:** US1 (impl + tests).
- **Developer B:** US2 (depends on A's MemoryStore + FileStore declarations).
- **Developer C:** US3 (depends on A's `file_store.cpp` Linux + Windows skeletons — integration at the writer-mutex wiring).
- **Developer D:** US4 (depends on A's FileStoreFactory).
- Polish merges as a final integration sweep.

---

## Notes

- **[P]** = different files, no dependencies on incomplete tasks. Same-file edits serialise.
- **[Story]** label is the verify-doc traceability key — `.specify/decisions/008-message-store-verify.md` §T cites these IDs.
- Every C++ test target is GoogleTest (`[const §VII.1]`); benches are Google Benchmark; fuzzer is libFuzzer (`[const §VII.7]` voluntary at the store layer).
- TSan is **mandatory** on every test target per `[const §IX.2]` and the design doc Constitution-Check row.
- Cross-doc structural edits (FR-037 / FR-038 / FR-039) are **already on disk** from the Gate-A Path-A pass (2026-05-20); T052–T055 are **verification-not-amendment** tasks. Catalogue Status promotion (`backlog → done` for S-011 / S-012 / S-013) is the merger's responsibility at Gate-B merge per `.specify/pipeline.md` step 19 — `feedback_pipeline_mark_done_step`. S-014 stays `backlog` (FSM half is `005`'s).
- The libFuzzer fuzzer (seam 21) is **voluntary** per design-doc §9 — `[const §VII.7]` strictly doesn't require it (2e is not parser-touching), but it catches torn-state regressions across the single-log on-disk algorithm.
- Conformance corpus (seam 17) is **store-side raw-frame round-trip only** per research D-13; the TC-001..TC-017 FIX-FSM transitions are `005`'s discharge per research D-5.
- **Pipeline order recap:** `/plan` → Gate A → **`/tasks` (this)** → `/analyze` → `/implement` → `/simplify` → `/speckit-verify` → Gate B (constitution `[const §XVI.4]` bundle-local pipeline-order statement; `feedback_speckit_pipeline_order_gate_a_before_tasks`).
- After this `/tasks` step the next command is **`/speckit-analyze`** (the canonical post-tasks gate per `[const §XVI.4]` / `[const §XI.7]`), then `/speckit-implement`.
