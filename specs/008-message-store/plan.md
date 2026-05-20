# Implementation Plan — 008-message-store

**Branch**: `008-message-store` | **Date**: 2026-05-20 | **Spec**: [spec.md](spec.md)
**Design anchor**: `.specify/2e-msgstore.md` **v0.4** (Gate-A-converged, round 3, post-cap line-edit pass; convergence log Appendix C), **including the cross-doc amendments already applied at v0.4 sign-off** — `[2d §4.5]` (`SessionConfig::store_factory` typed `std::unique_ptr<MessageStoreFactory>` per Appendix D §D.1, shipped in `007-threading-clock` `session_config.hpp:106`) and `[2d §4.7]` (`FileStore::flush_for_session_close()` per-mode effect-table row + hook contract paragraph per Appendix D §D.2, at `2d-threading.md:853`). On conflict the anchor wins; an inconsistency is a defect in this plan.

> **Authority anchor:** This feature realizes the **signed-off Phase-2 design doc `.specify/2e-msgstore.md` v0.4** as shipped code. Where this plan and the design doc disagree, **the design doc wins; an inconsistency is a defect in this bundle.** 008 is the **third and final** prerequisite (`2f-async-mutex [006, merged]` → `2d-threading [007, merged]` → `2e-msgstore [008]`) that the deferred `005-session-establishment-fsm` consumes. Catalogue rows owned (in part): **S-011** (Message store interface, NEW done), **S-012** (`MemoryStore`, NEW done), **S-013** (`FileStore`, NEW done), **S-014** (Session recovery — **store-side contract only**: `retrieve(begin,end)` shape + raw-frame discipline; FSM half is `005`'s, so S-014 stays `backlog`); **OSS-002** (QuickFIX MessageStore — Path B verdict per `[arch §11]` Q3; no runtime adapter); **COM-009** (Replicable MessageStore — post-v1.0 forward-compat invariant only). The S-011/S-012/S-013 Status promotion (`backlog → done`) is the **orchestrator-applied** Gate-B-merge bookkeeping step per `pipeline.md` step 19, following the `[2c App D]` / 007 NFR-015 precedent (research D-12). The `[arch §11]` Q3 disposition flip, `coverage-index.md:76` row update, `feature-catalogue.md` OSS-002 + COM-009 row updates, and `EngineConfig::max_store_memory_per_session` field addition are shipped **inside this feature's merge bundle** (FR-037 / FR-038 / FR-039 / FR-014a; hybrid ownership per Clarifications Session 2026-05-20 Q4 and FR-037).

## Normative References

Per `[const §VI.5]` exact-coverage discipline. Pure list, no commentary. Sourced from spec `## Normative References` and design-doc Appendix B.

- `[FIX-SL §4.1]` Sequence numbers (wire seqnum semantics; does NOT bound `seqnum_t` type).
- `[FIX-SL §4.4]` / `[FIX-SL §4.4.2]` / `[FIX-SL §4.4.3]` Sequence reset (operator-driven; the `ResetSeqNumFlag(141)=Y` Logon recovery path the `store_seqnum_overflow` variant surfaces).
- `[FIX-SL §4.5]` Message exchange (graceful-Logout durability — load-bearing for `flush_for_session_close()`).
- `[FIX-SL §4.5.4]` / `[FIX-SL §4.8]` / `[FIX-SL §4.8.3]` / `[FIX-SL §4.8.5]` / `[FIX-SL §4.8.6]` / `[FIX-SL §4.8.8]` Message recovery, ResendRequest, GapFill, hard reset — the wire-side oracle for what the store must persist and replay.
- `[const §I.1]` v1.0 surface; `[const §II.3]` Tier 2 platform support (Windows/MSVC for FileStore crash-survival); `[const §VI.5]` exact-coverage citations; `[const §VII]` testing (≥10 seams — this feature ships **21**); `[const §VII.5]` conformance corpus (**APPLICABLE** here — seam 17 store-side raw-frame round-trip); `[const §VIII.5]` zero global-`new`/`delete` parse → `fromApp`; `[const §IX.1]` coverage thresholds; `[const §IX.2]` Tier-1 sanitizers (TSan + ASan + UBSan); `[const §X.4]` forwards-compat reserved range (10 new variants append-only at unused slots **56–65**); `[const §X.5]` C-ABI handle invalidation; `[const §XI.1]`–`[const §XI.6]` coroutines / cancellation / awaitable-mutex / strand / mutex-always-on-store-write / HALO; `[const §XIV.1]` MessageStore is a pluggable interface (in-memory + file-based defaults shipped); `[const §XIV.2]` ≤5 pure-virtual cap (**4 / 5** — within cap); `[const §XV.1]` no heap-alloc per message on hot path; `[const §XV.4]` no synchronous disk-I/O on every send (banned QuickFIX `FileStore` pattern — `FileStore::store(commit_per_message)` runs on `file_io_executor`, not the session strand); `[const §XV.9]` `std::mutex` in coroutine context banned (no transitional carve-out); `[const §XV.15]` no `drop-oldest` on app/session message path (`capacity_policy::evict_oldest` UNREPRESENTABLE); `[const §XVII.1]` Codex Gate A required for design docs; `[const §XVIII.5]` no early shipping of post-v1 protocols.
- `[SYN §3.2 Q6b]` `async_shared_mutex` out of v1.0 (RW-mutex / shared-mutex banned for v1.0 stores); `[SYN §3.2 Q7]` DECIDED async-API shape (`asio::awaitable<...>`); `[SYN §3.2 Q8]` DECIDED store-write path always uses mutex regardless of policy.
- `[arch §1.1]` v1.0 goals; `[arch §4.4]` session module surface; `[arch §5.1]` executor model; `[arch §5.3]` `expected_t<T>` error model; `[arch §5.4]` storage/lifetime classes; `[arch §5.6]` mid-session reconfiguration ban + `unique_ptr` ownership; `[arch §6]` plugin pattern; `[arch §10]` row 2e handoff; `[arch §11]` Q3 (QuickFIX-compat-shim disposition — **CLOSED** by this feature at Path B).
- Sibling docs (consumed, not modified): `[2a §4.2]` `trap_throw`; `[2a §10] Q3` raw-frame storage confirmation; `[2b §4.2]` `frame_view::bytes()`; `[2b §4.5]` `Writer::commit` finalises BodyLength + CheckSum (outbound call-ordering pin, root cause #1); `[2b §6.4]` view-escape (deep-copy-before-suspension justification); `[2b §6.6]` allocation/exceptions/threading; `[2b §7.4]` MessageStore consumes raw frames not typed payloads; `[2c §1.1]` / `[2c §7.2]` no `Dictionary&` held by store; `[2d §4.4]` `EngineConfig::clock` / `executor`; `[2d §4.5]` `SessionConfig::store_factory` `unique_ptr` field; `[2d §4.6]` `fixpp::current_trace_context`; `[2d §4.7]` cancellation propagation + two-phase close + `flush_for_session_close()` row; `[2d §4.8]` `session_executor` wrapper; `[2d §6.5]` `cancellable_dispatch`; `[2d §6.7]` `dispatch_aborted` / `clock_sleeps_cancelled` in `FIXPP_ERR_CANCELLED` group (`store_cancelled` joins here); `[2d §7.3]` MessageStore strand-binding; `[2d §7.4]` executor-compat surface; `[2d §7.9]` `effective_clock` (consumer-side, v1.0 defaults do not persist timestamps); `[2f §4.1.1]` / `[2f §4.3.2]` / `[2f §6.5]` `async_mutex` contract.

## Summary

This feature delivers the **`MessageStore` plugin interface, two default impls (`MemoryStore`, `FileStore`), the `MessageStoreFactory` `unique_ptr` ownership shape, and the awaitable `retrieve_visitor`**, realizing `.specify/2e-msgstore.md` v0.4 as shipped code:

- **`fixpp::session::MessageStore`** — abstract plugin interface, **exactly 4 pure-virtual** (`store`, `retrieve`, `next_seqnum`, `reset`) per `[2e §4.1]`; ≤5 cap (`[const §XIV.2]`). Every method `[[nodiscard]] noexcept`; `frame`/`visitor` carry `[[clang::lifetimebound]]`. **No public `flush()`** (N2). `include/fixpp/session/message_store.hpp`.
- **`fixpp::session::retrieve_visitor`** — awaitable per-frame visitor (root cause #2; `[2e §4.5]`). One pure-virtual `on_frame(seqnum_t, std::span<const std::byte>) -> awaitable<expected_t<visit_result>>` + one overridable virtual hook `abort_error() -> fixpp::core::error` (default `store_visitor_aborted`). `visit_result : std::uint8_t { cont=0, stop=1, abort=2 }`. `include/fixpp/session/retrieve_visitor.hpp`.
- **`fixpp::session::MemoryStore`** — `final` in-memory impl; `MemoryStore::Config { capacity_policy policy = bounded; size_t inbound_capacity = 10'000; size_t outbound_capacity = 10'000; size_t max_frame_bytes; std::pmr::memory_resource* store_resource; }` (`[2e §4.2]`); fixed-slot + fixed-slab one-PMR-allocation-at-construction layout; **zero allocator calls in `store()` after construction under `bounded` policy** (N9 / FR-007 / seam 15). `capacity_policy : std::uint8_t { bounded=0, unbounded=1 }` — closed 2-value enum + `[[clang::enum_extensibility(closed)]]` + `static_assert`; `evict_oldest` is UNREPRESENTABLE (`[const §XV.15]`). `include/fixpp/session/memory_store.hpp` + `src/session/memory_store.cpp`.
- **`fixpp::session::FileStore`** — `final` file-based impl; `FileStore::Config { std::filesystem::path dir; FileStorePolicy policy; size_t max_frame_bytes; std::pmr::memory_resource* store_resource; }`; **single append-only log per session** `<sender>__<target>.log` with 16-byte per-record header (kind/dir/reserved/seq/len/crc32) + payload + 8-byte align padding; sentinel record (`magic | version | session_triple_hash | crc32`) at file start; per-record CRC32 torn-write detection on restart; atomic-rename `reset()` via `<...>.log.reset.tmp` + platform durability primitive (Linux: parent-dir `fsync` **MANDATORY**; Windows: `MoveFileExW(.., MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)` **MANDATORY**, round-3 C-R3-P1-2); advisory open-lock (Linux: `flock`; Windows: `LockFileEx`); per-instance `file_io_executor` for `pwrite`/`fdatasync` work, rebinding to the session strand on completion via `[2d §6.5]`'s `cancellable_dispatch` shape (`[2e §4.3]` / §6.3 / §6.3.5). `include/fixpp/session/file_store.hpp` + `src/session/file_store.cpp`.
- **`FileStorePolicy`** — `commit_per_message` (sync `fdatasync` per record, latency floor); `commit_batched(N)` (flush every Nth record, half-batch loss window); `commit_interval(ms)` (timer-fired flush on `file_io_executor` per `[2e §10] Q10`, ms-bounded loss window) — `[2e §4.3.1]`.
- **`fixpp::session::MessageStoreFactory`** — **extends in place** the minimal stub at `include/fixpp/session/message_store_factory.hpp` (007-shipped polymorphic bind-target with deleted move/copy + virtual destructor base). Adds one pure-virtual `make(std::string_view sender, std::string_view target, std::pmr::memory_resource* mr) -> expected_t<std::unique_ptr<MessageStore>>` (N1; `[2e §4.4]`). NOT replaced, NOT duplicated.
- **`fixpp::session::MemoryStoreFactory` / `fixpp::session::FileStoreFactory`** — the two default factory impls; both consume `EngineConfig::max_store_memory_per_session` at `make()` for the storage-DoS construction guard (round-2 N9 / C-R2-P1-3 / FR-014). `include/fixpp/session/memory_store_factory.hpp` + `include/fixpp/session/file_store_factory.hpp`.
- **`fixpp::session::direction_t`** — `enum class : std::uint8_t { inbound = 0, outbound = 1 }`; values frozen for v1.0; reserved per `[const §X.4]`. `include/fixpp/session/direction.hpp`.
- **`fixpp::session::seqnum_t`** — `using seqnum_t = std::uint32_t` placeholder with `seqnum_min = 1` / `seqnum_max = numeric_limits<uint32_t>::max()` constants per `[2e §4.7]` / `[2e §10] Q9` (root cause #3 / Codex P1-3). **Authored fresh** by this feature (Clarifications 2026-05-20 Q2 — verified no existing `<fixpp/session/seqnum.hpp>` on the 007 baseline); when the Phase-4 session-module spec lands the header is re-exported from there or deleted with includes repointed (single-line edit per `[const §VI.5]`). `include/fixpp/session/seqnum.hpp`.
- **`fixpp::session::detail::has_flush_for_session_close`** — concept (`requires { s.flush_for_session_close(); }`) gating engine-internal Session-close durability hook. **Concept-shaped non-virtual dispatch** (Opus N3-P2-1) gated by a factory-type tag retained at session open — **NOT RTTI / `dynamic_cast`**. `FileStore` defines `flush_for_session_close()` (engine-internal, non-virtual, non-public — engine + `FileStore` are friends); `MemoryStore` does not (the concept's `requires` clause fails and the engine skips the call); user-supplied impls inherit the no-op default for free. Hook runs to completion outside phase-1's child timeout under `Session::close(graceful)`; NOT invoked under `Session::close(terminal)` per Appendix D §D.2. Returns `expected_t<void>{}` or `expected_t::unexpected{store_io_failure}` on mid-flush error; does NOT surface `store_cancelled` under graceful close. `include/fixpp/session/detail/has_flush_for_session_close.hpp` (concept) + the method on `FileStore`.
- **`fixpp::session::quickfix_compat::cfg_loader`** — `cfg_to_file_store_factory(std::filesystem::path) -> expected_t<std::unique_ptr<FileStoreFactory>>` reader (config translation only; **no runtime adapter** — Path A retired in v0.3 per Codex C-R2-P2-1; `[2e §4.8.A.2]` / `[2e §4.8.B]`). `include/fixpp/session/quickfix_compat/cfg_loader.hpp` + `src/session/quickfix_compat/cfg_loader.cpp`.
- **Additive edits** to existing files (non-renumbering, non-replacement):
  - `include/fixpp/core/error.hpp` — **10** new variants appended at unused slots **56–65** per `[const §X.4]`; design-doc table order: `store_io_failure`, `store_seqnum_gap`, `store_seqnum_out_of_order`, `store_capacity_exhausted`, `store_seqnum_overflow`, `store_factory_failed`, `store_visitor_aborted`, `store_seqnum_invalid`, `store_invalid_range`, `store_cancelled`. Variants `store_concurrent_writer` (Codex P1-5 — FIFO-fair mutex makes the variant impossible) and `store_shim_timeout` (C-R2-P2-1 — Path A retired) are **NOT defined**.
  - `include/fixpp/core/engine_config.hpp` — new field `std::size_t max_store_memory_per_session = 1ULL << 30;` (1 GiB default per `[2e §1.2]`), placed adjacent to `default_store_factory` (FR-014a; engine-wide cap, no SessionConfig override).
  - `include/fixpp/session/message_store_factory.hpp` — extended in place to add the pure-virtual `make(...)` method (the existing minimal polymorphic bind-target stays the base; we ADD the virtual method, not duplicate the class).
- **`tests/session/`** — 21 named test seams per `[2e §9]`; **`tests/conformance/test_store_corpus_replay.cpp`** (seam 17, store-side raw-frame round-trip; `[const §VII.5]`-applicable); **`tests/fuzz/fuzz_message_store.cpp`** (seam 21, libFuzzer under ASan + UBSan + TSan invariants); **`bench/session/bench_memory_store.cpp`** + **`bench/session/bench_file_store.cpp`** with baselines under `bench/baselines/session/`.
- **Cross-doc structural edits in this merge bundle** (FR-037 / FR-038 / FR-039 — bundle-internal, NOT orchestrator-applied):
  - `spec/coverage-index.md:76` row amended to `§4.8 | Message recovery | Y | S-011, S-012, S-013, S-014 | —`.
  - `spec/feature-catalogue.md` OSS-002 row updated with Path B disposition (v0.3 verdict, no runtime adapter ships); COM-009 row gains a forward-compat note pointing at `[2e §10] Q2` (row stays `backlog`).
  - `.specify/architecture.md:598` (`[arch §11]` Q3 row) amended to `CLOSED in 2e v0.3: Path B only (documented incompatibility); see [2e §4.8.A]. v0.2's Path A subset wrapper retired in round 2 per Codex C-R2-P2-1 escalation.`
- **Orchestrator-applied at Gate-B merge** (`pipeline.md` step 19 — NOT this feature): `feature-catalogue.md` rows S-011 / S-012 / S-013 Status `backlog → done` with PR/Tests/Verified linkage. S-014 stays `backlog` (FSM half is `005`'s).

**Not shipped here:** the FIX FSM that consumes the 4-method seam (`005`-owned; per Clarifications Session 2026-05-20 Q1, a **deterministic scripted test-double FSM** in the 2e test fixture drives seams 1/2/3/7/8/9/10/17/18/19 — analogous to 007's pattern for its FSM-dependent seams); the canonical `seqnum_t` type (Phase-4 session-module spec `[2e §10] Q9`); the C-ABI symbol shapes for store (`2i`); the runtime `quickfix_compat::sync_message_store_adapter` (retired in v0.3 per Codex C-R2-P2-1 — `[2e §4.8.B]`); the replicated/cloud `MessageStore` (COM-009 post-v1.0 `[const §XVIII.5]`); the audit-log sidecar (`[2e §10] Q3`, user-side composition); the `2k` OTel attribute set (`[2e §10] Q6`); the orchestrator-applied catalogue Status promotions (above).

**Consumed-not-built upstream:** `001` `core::error`/`expected_t`/`trap_throw` (`[2a §4.2]`, merged); `004` `wire::Writer::commit` post-commit span (`[2b §4.5]`, merged) + `wire::Framer` `frame_view::bytes()` (`[2b §4.2]`); `006`/`2f` `async_mutex` (merged) — per-instance writer mutex per `[const §XI.3]` and `[2f §6.5]` cancellation shape; `007`/`2d` (merged) — `session_executor` wrapper, `Session::session_arena()` accessor, `cancellable_dispatch` (`[2d §6.5]`), two-phase `Session::close` model, `SessionConfig::store_factory` `unique_ptr` field at `session_config.hpp:106`, the minimal `MessageStoreFactory` bind-target stub at `message_store_factory.hpp`, and the `[2d §4.7]` per-mode effect-table row at `2d-threading.md:853`.

**C++-only, no C-ABI surface added here** (C-ABI handle shape `fixpp_store_t` + `FIXPP_ERR_STORE_*` numeric coalescing are `2i`'s; this feature delivers the C++ error variants + documents the prefix-group mapping for `2i` per FR-023).

## Technical Context

**Language/Version:** C++23 (`[const §II.1]`). Coroutines (`asio::awaitable<T>`), ASIO native cancellation slots, `std::pmr`, `std::expected` (via `core::expected_t`), `std::unique_ptr`, `std::span`, `std::filesystem`, `std::chrono`, `std::byte`, `[[nodiscard]]`, `[[clang::lifetimebound]]`, `[[clang::enum_extensibility(closed)]]` where supported. No fallback to earlier standards.

**Primary Dependencies:** ASIO (`asio::awaitable`, `asio::strand`, `asio::any_io_executor`, `asio::cancellation_state`/`cancellation_slot`/`cancellation_type`, `asio::this_coro`, `asio::dispatch`, `asio::posix::stream_descriptor` Linux / `asio::windows::random_access_handle` Windows for the `file_io_executor` work), `fixpp::core` (`expected_t`, `error`, `detail::trap_throw`, `EngineConfig`, `session_executor`, `cancellable_dispatch`, `system_clock_source` — 001/007 merged), `fixpp::session` (`Session::session_arena()`, the minimal `MessageStoreFactory` stub, `SessionConfig::store_factory` — 007 merged), `fixpp::sync::async_mutex` (006 merged), `fixpp::wire` (`Writer::commit`'s post-commit span, `Framer::feed`'s `frame_view` — 004 merged), `fixpp::dict` (not consumed — store is dictionary-agnostic per `[2c §1.1]`; listed only for build alignment). GoogleTest 1.17.0 + GoogleMock, Google Benchmark 1.9.5, libFuzzer (Linux/Clang Tier 1). **No new Conan row** — `asio/1.36.0`, GTest, Benchmark, OpenSSL all already pinned by upstream features; `crc32c` (Conan: `crc32c/1.1.2`) **may** be added if `std::crc32` is unavailable on the toolchain matrix (decision deferred to T002 — see research D-3). `[const §III.2]`.

**Project Type:** C++23 library spanning `session/` (`MessageStore`, `MemoryStore`, `FileStore`, `MessageStoreFactory`, `retrieve_visitor`, `direction_t`, `seqnum_t`, `has_flush_for_session_close` concept, `quickfix_compat::cfg_loader`) and a **narrow additive edit** to `core/` (`engine_config.hpp` field + `error.hpp` slots 56–65). Header-dominant for the interface + visitor + concept where HALO visibility on the awaitable promise frame matters; `MemoryStore` MAY be header-only (one `.cpp` for the slot-pool helpers if needed), `FileStore` is out-of-line (`src/session/file_store.cpp`) — file-I/O + platform-specific durability primitives + libfmt-style logging keep template instantiation cost manageable. `cfg_loader` is out-of-line (`src/session/quickfix_compat/cfg_loader.cpp` — parser). No SWIG, no C-ABI in this PR.

**Performance Goals (Linux/Clang/x86_64, warm cache, release):** per `[2e §6.6]` Tier-1 ceilings. CI fails on >5% regression vs `bench/baselines/session/` for `MemoryStore` rows; > 2× for `FileStore` I/O-bound rows (`[const §VIII.2]`, soft-floor wider for disk-bound paths):

| Operation | Workload | Ceiling |
|---|---|---|
| `MemoryStore::store(seq, 200-byte frame, outbound)` | single call, mutex-uncontended | **≤ 200 ns** |
| `MemoryStore::store(seq, 1 KiB frame, outbound)` | single call, mutex-uncontended | ≤ 800 ns |
| `MemoryStore::next_seqnum(dir, false)` | single call, mutex-uncontended | ≤ 50 ns |
| `MemoryStore::retrieve(100-frame range, visitor cont)` | one visitor call per frame, no `co_await` | ≤ 8 µs |
| `MemoryStore::reset` | at default capacity (10_000 × 16 B × 2 = 320 KiB entry-array zero) | ≤ 25 µs |
| `FileStore::store(commit_per_message, 200-byte frame)` | NVMe `fdatasync` floor (soft ceiling, > 2× regression fail) | ≤ 250 µs |
| `FileStore::reset` | atomic-rename + platform durability primitive (soft ceiling) | ≤ 5 ms |

Bench harnesses in `bench/session/` enforce these via Google Benchmark (`[const §VIII.1]`); ±5% MemoryStore, > 2× FileStore (`[const §VIII.2]`). FileStore disk-bound rows are bench-soft / Tier-1-NVMe-only — `bench/baselines/session/` captures the per-host hardware floor; regression-on-different-hardware is reported, not blocked.

**Constraints:**

- Zero global `new`/`delete` between parse and `fromApp` (`[const §VIII.5]`): `MemoryStore::store` allocates only from `store_arena` (fixed-slot/slab at construction); the awaitable promise frame is HALO-elided where it fires, PMR fallback to `session_arena` otherwise. `FileStore` allocates the `pwrite` buffer (small) from `store_arena` per call; the `file_io_executor`'s posted-handler frame is HALO/PMR per `[2d §6.5]`. `tools/check_alloc.py` post-link symbol scan + 10⁴-message `mallocnesia` run verify zero global-heap calls (seams 14 / 15).
- No exception across the parse → `fromApp` window (`[arch §5.3]`); cancellation is **not** an exception — `asio::error::operation_aborted` and `expected_t::unexpected{store_cancelled}` are the surfaces; PMR throw routes through `fixpp::core::detail::trap_throw` per `[2a §4.2]` (no terminate; surfaces as `expected_t::unexpected{store_visitor_aborted}` on the retrieve-recovery path).
- ASIO native cancellation slots end-to-end; **no parallel `stop_token`** (`[const §XI.2]`). The per-method cancellation linearisation point is the §6.1.4 table (FR-020); cancellation before that point → `store_cancelled`, no state change; cancellation after → normal completion, durable state.
- All four methods acquire the per-instance writer mutex on entry (Opus N2-P2-2; FR-015) — the v0.2 atomic-fetch-add wording for `next_seqnum` was retired in round 2; **no `async_shared_mutex` / RW-mutex** (`[SYN §3.2 Q6b]`); **no `std::mutex` / `std::recursive_mutex`** (`[const §XV.9]` no carve-out). `retrieve` acquires the mutex to validate `begin`/`end` and snapshot the index, then **releases before** the visitor's `co_await` (FR-017); mid-traversal mutation is detected and the next visitor call observes the new state without UB.
- `store_seqnum_out_of_order` verification is **inside** the writer-mutex critical section after acquire, before any slab memcpy / `pwrite` (Opus N2-P2-3; FR-018); on mismatch the awaitable returns the variant with no state mutation.
- `store` takes a deep copy of the `frame` span into store-owned storage **before** the awaitable's first suspension point per `[2b §6.4]` / `[2b §6.6]` view-escape (FR-019); the wire-side per-message arena does NOT guarantee the bytes live past suspension.
- `FileStore::reset()` is atomic at the `rename` boundary **plus** the platform durability primitive (Linux: parent-dir `fsync` MANDATORY; Windows: `MOVEFILE_WRITE_THROUGH` MANDATORY — round-3 C-R3-P1-2; FR-010 / SC-003).
- `MemoryStore::Config::capacity_policy` is a closed 2-value enum + `[[clang::enum_extensibility(closed)]]` + `static_assert` at every switch + runtime out-of-range-cast reject; `evict_oldest` is UNREPRESENTABLE (`[const §XV.15]`).
- `[arch §2.3]` leaf rule — `core/` does NOT back-edge into `session/`. The two `core/` edits are: (a) a new `std::size_t` field on `EngineConfig` (no `session/` reference); (b) new `error` enum slots (free standing). `core::cancellable_dispatch` already takes the project types by value/pointer.
- `FileStore` `file_io_executor` is a single per-instance execution context per `[2e §10] Q10` (avoids cross-thread races on the log file handle); completions rebind to the session strand via `[2d §6.5]` `cancellable_dispatch`.
- `flush_for_session_close()` is **engine-internal, non-virtual, non-public** (engine + `FileStore` are friends), dispatched via the concept-shaped `has_flush_for_session_close` requires-clause gated by a factory-type tag retained at session open — **NOT RTTI / `dynamic_cast`** (Opus N3-P2-1; FR-028).
- The `2d` cross-doc amendments (`[2d §4.5]` D.1 `unique_ptr<MessageStoreFactory>` + `[2d §4.7]` D.2 `flush_for_session_close()` row) are **already shipped** as of 007's merge — D.1 at `session_config.hpp:106`, D.2 at `2d-threading.md:853`. This feature realizes the hook **implementation** + the concept + the seam exercising it (FR-028 / seam 19); it does NOT amend `[2d]` again.

**Scale/Scope:** ~11 owned headers (`session/`: `message_store`, `retrieve_visitor`, `memory_store`, `file_store`, `memory_store_factory`, `file_store_factory`, `direction`, `seqnum`, `quickfix_compat/cfg_loader`, `detail/has_flush_for_session_close`; plus extending `message_store_factory.hpp`) + 4 `.cpp` files (`memory_store.cpp` if any out-of-line, `file_store.cpp`, `quickfix_compat/cfg_loader.cpp`, optional `message_store_factory.cpp`) + 10 `error.hpp` slots (56–65) + 1 `EngineConfig` field + 21 test files (`tests/session/` ×18 + `tests/conformance/` ×1 + `tests/perf/` ×1 + `tests/fuzz/` ×1) + 2 bench files + baselines + `book/migration_from_quickfix.md` MessageStore section (FR-031) + the 3 structural cross-doc edits (FR-037/038/039). Magnitude domain per `[2e §1.2]` (default 10_000-per-direction `MemoryStore` slots ⇒ ≤ ~640 KiB index + payload arena scoped to `max_frame_bytes`; `FileStore` single-log-per-session bounded by disk; storage-DoS guard 1 GiB engine-wide default). Estimate ~3000–3800 LOC (test-heavy for 21 seams incl. SIGKILL crash-survival fork harnesses, Windows torn-write Tier-2 variant, libFuzzer). Closes — at this PR's merge bundle — S-011/S-012/S-013 (catalogue Status promotion is orchestrator step 19), OSS-002 (Path B verdict; row updated), COM-009 (forward-compat note); `[arch §11]` Q3 (CLOSED disposition). S-014 stays `backlog` until `005` lands.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-evaluated post-Phase 1 design.* Canonical citation form `[const §Roman.arabic]`. **Mood:** at `/speckit-plan`-stage these rows assert *planned conformance*; delivered/verified evidence is produced by `/speckit-implement` + `/speckit-verify`. The citation-verification pass at the end of this file was run against `.specify/constitution.md`.

| Article cited | Topic | How this feature satisfies it |
|---|---|---|
| `[const §I.1]`, `[const §I.3]`, `[const §I.4]` | Identity/mission; catalogue tracker; no silent omission | Owns **S-011** (NEW done), **S-012** (NEW done), **S-013** (NEW done), **S-014** (partial — store-side raw-frame round-trip + retrieve query shape only; FSM half stays `backlog` for `005`); discharges **OSS-002** at Path B verdict; updates **COM-009** with forward-compat note. The S-011/S-012/S-013 `feature-catalogue.md` rows already exist (lines 28/29/30); this feature's PR merge bundle ships the structural cross-doc edits (FR-037/038/039); the catalogue Status promotion (`backlog → done`) is orchestrator step 19 at Gate-B merge per the 007 NFR-015 / `[2c App D]` precedent (research D-12). FSM boundary explicit (Clarifications Session 2026-05-20 Q1 / D-4). |
| `[const §II.1]` | C++23, no fallback | Coroutines, ASIO native cancellation, `std::pmr`, `std::expected`, `std::unique_ptr`, `std::span`, `std::filesystem`, `std::chrono`, `std::byte`, `[[nodiscard]]`, `[[clang::lifetimebound]]`; no fallback. |
| `[const §II.3]` | Tier 2 platform support (Windows/MSVC) | `FileStore` crash-survival, torn-write detection, atomic-rename, `flush_for_session_close()`, and `flock`-equivalent all have Windows code paths exercised by seams 2 / 3 / 10 / 19 on the MSVC Tier-2 matrix per `[const §II.3]`. `MOVEFILE_WRITE_THROUGH` mandatory per round-3 C-R3-P1-2. |
| `[const §III.2]` | Conan, pinned deps | **No new Conan row** baseline — `asio/1.36.0`, GTest 1.17.0, Benchmark 1.9.5, OpenSSL all already pinned by upstream features. **Possible add:** `crc32c/1.1.2` IF `std::crc32` is unavailable on the toolchain matrix — decision deferred to T002 (research D-3). If added, the row is the only Conan change. |
| `[const §V.1]`, `[const §V.4]` | AGPL-3.0; vendored attribution | Every new header carries `SPDX-License-Identifier: AGPL-3.0-or-later`. **No vendored algorithm** — `MemoryStore` / `FileStore` / `cfg_loader` are all original; `crc32c` (if adopted) is BSD-3-Clause, compatible with AGPL via `[const §V.3]` no-LGPL rule (BSD allows it). |
| `[const §VI.4]`, `[const §VI.5]` | Bidirectional traceability + Normative References | spec + plan carry the Authority-anchor blockquote + `## Normative References` with exact `[const §...]` / `[FIX-SL §...]` / `[arch §...]` / `[SYN §...]` / `[2X §...]` entries. `coverage-index.md:76` amended in this bundle (FR-037) so S-011/S-012/S-013/S-014 all trace to `[FIX-SL §4.8]`. |
| `[const §VII.1]`, `[const §VII.3]`, `[const §VII.4]` | GoogleTest + TDD + no untested code | `tasks.md` ordered red-green-refactor; every C++ test target GoogleTest; **21 named seams** cover every behavioural contract from `[2e §9]`. |
| `[const §VII.5]` | Conformance corpus (TC-001..TC-017 every PR) | **APPLICABLE** — but at the **store-side raw-frame round-trip** layer only. Seam 17 (`tests/conformance/test_store_corpus_replay.cpp`) loads a recorded FIX session corpus, replays each frame through `MemoryStore` + `FileStore`, and asserts byte-identical replay (`store(seq, bytes, dir)` × N then `retrieve(1, 0, dir, byte_compare_visitor)` byte-equal). It does NOT exercise the FIX FSM (Logon → ResendRequest → SequenceReset-GapFill → Logout flow) — that requires `005`. **Recorded boundary:** the FIX-TC TC-001..TC-017 cases that need the session FSM are deferred to `005` and `005`'s feature-completeness audit; the store-side subset that 2e ships is real conformance discharge for the store contract, not a waiver. SC-010 + FR-033's `tests/conformance/test_store_corpus_replay.cpp` carry this. |
| `[const §VII.6]` | Interop test (QuickFIX) | **N/A — no FIX FSM in 2e.** Interop Logon → NewOrderSingle → ExecutionReport → Logout needs the session FSM (`005`). 2e ships QuickFIX-compat verification at the **config-translation layer only** (seam 12 `cfg_loader`: feed a QuickFIX CFG, mint a session, round-trip a frame, byte-equal) + compile-time **Path B guard** (seam 11). No `quickfix::Session` runtime adapter (Path A retired `[2e §4.8.B]` Codex C-R2-P2-1). Recorded D-5. |
| `[const §VII.7]` | Fuzzing (parser-touching modules) | **Strictly N/A — 2e is not parser-touching** — but 2e **voluntarily ships** the libFuzzer message-store fuzzer (seam 21, `tests/fuzz/fuzz_message_store.cpp`) per `[2e §9 seam 21]` Gate-A discretion (the store is on the session message path; fuzzing random interleavings of `store/retrieve/reset/next_seqnum` against `MemoryStore` + `FileStore` catches torn-state regressions across the round-2 single-log on-disk algorithm). Run under ASan + UBSan + TSan invariants per `[const §IX.4]`-extended. Recorded D-3. |
| `[const §VIII.1]`, `[const §VIII.2]`, `[const §VIII.5]` | Benchmark + ±5% + zero hot-path alloc | `bench/session/bench_memory_store.cpp` + `bench/session/bench_file_store.cpp` + `bench/baselines/session/` enforce the Technical-Context ceilings; FileStore disk-bound rows bench-soft (> 2× regression fail). Zero global `new`/`delete` parse → `fromApp` verified by `tools/check_alloc.py` + 10⁴-message `mallocnesia` run (seam 14); `MemoryStore::store` reports **0 allocator calls** under `bounded` policy after construction via the tracking-PMR counter (seam 15; FR-007). |
| `[const §IX.1]` | ≥95% line / ≥85% branch on touched modules | Planned: `linux-clang-coverage` measures the owned `session/` headers (+ `.cpp`) + the two `core/` edits as the Tier-1 gate; enforced at `/speckit-verify` on the **lcov DA/BRDA basis** per the recorded coverage-gate-lcov-basis feedback (header-inline/`if constexpr` paths judged on zero-hit DA / not-taken BRDA, not the `llvm-cov report` aggregate). **Recorded coverage-exempt-by-inspection:** `include/fixpp/core/error.hpp` enum-slot append (56–65) contributes zero instrumentable lines/branches (`llvm-cov` instruments statements/branches, not enum constants) — coverage-exempt by inspection per `[const §IX.1]`'s recorded-non-assessable-touch rule; same precedent as 007 (`[2d §6.7]`). The `EngineConfig` field addition contributes one default-initialiser line, hit by every `validate_engine_config()` call (already covered by 007). |
| `[const §IX.2]` | Tier-1 sanitizers | ASan + UBSan + **TSan mandatory** on every test — 2e is a threading-touching feature; FIFO-fair concurrent-writer (seam 5), cancellation-result contract (seam 6), retrieve visitor + span lifetime (seam 9), session shutdown ordering (seam 18), `flush_for_session_close` (seam 19), `store_seqnum_out_of_order` (seam 20), fuzzer (seam 21) all run under TSan. Race/leak seams additionally ASan-clean. UBSan on every PR Tier-1; UBSan-on-MSVC is N/A per `[const §IX.3]`. |
| `[const §IX.4]` | Tier-1 static analysis | clang-tidy + clang-format + cppcheck + IWYU on all owned `session/` headers + `src/session/`. The `[[clang::lifetimebound]]` discipline (FR-001 / FR-004) emits clang-tidy warnings on misuse via the lifetime-bound check. |
| `[const §IX.5]` | abidiff vs last tagged ABI | **N/A — no C-ABI surface added.** C-ABI `fixpp_store_t` handle + `FIXPP_ERR_STORE_*` numeric coalescing are `2i`'s (`[2e §5]` / FR-023); zero `extern "C"` symbols introduced. 10 new `core::error` variants are C++-internal, appended non-renumbering at unused slots **56–65**, pre-publication (`[const §X.4]`). Recorded D-6. |
| `[const §IX.6]` | Two-tier CI | Tier 1: every preset from quickstart §3 (incl. TSan, alloc-guard, fuzz, coverage). Tier 2: Windows MSVC for `FileStore` torn-write + `flush_for_session_close` graceful-close + `MOVEFILE_WRITE_THROUGH` manual / nightly. |
| `[const §X.2]`, `[const §X.4]` | No C++ leakage through C ABI; forwards-compat error codes | No C-ABI symbol/type introduced (`[2e §5]`). **10** new `core::error` variants appended at unused slots **56–65**, non-renumbering (planned/pre-publication; pinned at Gate A / `/tasks` per data-model); design-doc table order. C-ABI coalescing for `2i`: `FIXPP_ERR_STORE_RUNTIME ← {store_io_failure, store_capacity_exhausted, store_seqnum_overflow}`; `FIXPP_ERR_STORE_CONSISTENCY ← {store_seqnum_gap, store_seqnum_out_of_order, store_seqnum_invalid, store_invalid_range}`; `FIXPP_ERR_STORE_CONFIG ← {store_factory_failed}`; `FIXPP_ERR_STORE_VISITOR ← {store_visitor_aborted}`; `FIXPP_ERR_CANCELLED ← {store_cancelled}` (per-doc-prefix discipline; final coalescing `2i`'s call — FR-023). |
| `[const §XI.1]`–`[const §XI.6]` | Coroutines; ASIO cancellation; awaitable mutex; threading default; lock policy; HALO | Feature consumes the merged 2d threading contract: `MessageStore` methods are `asio::awaitable<T>` (`[const §XI.1]`); cancellation is ASIO native (`[const §XI.2]`); the per-instance writer mutex is `fixpp::sync::async_mutex` (`[const §XI.3]`) — **regardless of `SessionConfig::lock_policy`** (`[const §XI.5]` store-write-path-always-uses-mutex `[SYN §3.2 Q8]`); `MessageStore` invocations are on the session strand via `session_executor` (`[const §XI.4]` per-session-strand default); `FileStore` posts I/O work to a per-instance `file_io_executor` and rebinds to the session strand on completion via `cancellable_dispatch` (`[2d §6.5]`); awaitable promise frames are HALO-eligible with PMR fallback (`[const §XI.6]`). |
| `[const §XI.7]` | Threading/concurrency-affecting → all four mandatory controls | **All four are triggered.** `/speckit-clarify` done (5 codebase-reality scoping clarifications recorded in spec Session 2026-05-20; 0 design-doc clarifications — design doc signed-off / Gate-A-converged through v0.4). Codex Gate A (this Phase-4 bundle Gate A, separate from the Phase-2 design-doc Gate A converged via `/gate-a-ph2`), `/speckit-analyze` (post-`/tasks`), and user `/plan` sign-off follow per the bundle-local pipeline-order statement in `[const §XVI.4]` below. |
| `[const §XII.5]` | Security profile no-implicit-default | Consumed unchanged from 007 (`SessionConfig::security_profile` default-constructs to a sentinel rejected at `Session::open`). This feature adds no security surface; the `flock` / `LockFileEx` advisory locks are filesystem-level, not TLS. |
| `[const §XIII.1]`, `[const §XIII.2]`, `[const §XIII.3]` | OpenTelemetry; async logging; no `thread_local` trace context | FR-035 / FR-036: structured-log events on `store_io_failure` (error, fields `{store_kind, direction, seq, file_path, errno}`), `retrieve` entry/exit (info), and `commit_per_message` flush stalls > 100× baseline (warn); one OTel span per `store()` call with `flush` work as child span. Implementation routes through `2k`'s `Logger` / `Sink` / `TracerProvider` interfaces (no direct `fprintf` / `std::cerr`); attribute set is `2k`'s call (`[2e §10] Q6`). Trace-context access via `co_await fixpp::current_trace_context` (007 awaitable), NOT `thread_local`. |
| `[const §XIV.1]` | MessageStore is a pluggable interface (in-memory + file-based defaults shipped) | **`MessageStore` IS the plugin** (`[const §XIV.1]` row 5). Two default impls shipped in v1.0: `MemoryStore` (in-memory; test/embedded), `FileStore` (file-based; production). User-supplied impls compose. |
| `[const §XIV.2]` | ≤5 pure-virtual on pluggable interfaces | **`MessageStore` IS a plugin: exactly 4 pure-virtual (4/5 — within cap; no justification paragraph required).** `store`, `retrieve`, `next_seqnum`, `reset`. `retrieve_visitor` has 1 pure-virtual (`on_frame`) + 1 overridable virtual hook (`abort_error()` with default — counts as 0 pure-virtual against the cap; the hook is intentionally virtual-with-default per `[2e §4.5]`). `MessageStoreFactory` has 1 pure-virtual (`make`). All within `[const §XIV.2]` cap. |
| `[const §XV.1]` | No heap-alloc per message on hot path | `MemoryStore::store` zero allocator calls under `bounded` policy after construction (FR-007 / seam 15); `FileStore::store` allocates only from `store_arena` (PMR-fixed); the `pwrite` buffer is from the same arena. Verified by seam 14 (mallocnesia) + seam 15 (tracking-PMR counter). |
| `[const §XV.4]` | No synchronous disk-I/O on every send (banned QuickFIX `FileStore` pattern) | `FileStore` posts `pwrite` + `fdatasync` work to a per-instance `file_io_executor` per `[2e §4.3.2]` / `[2e §10] Q10`, never on the session strand. Completions rebind to the session strand via `cancellable_dispatch`. `commit_per_message` policy executes `fdatasync` **on `file_io_executor`** (not the session strand) — the session strand is suspended on the awaitable, not blocked on syscall. The banned QuickFIX-`FileStore` pattern (sync `fwrite` + `fflush` on the I/O thread on every send) is structurally impossible in this design. |
| `[const §XV.9]` | `std::mutex` in coroutine context banned | Per-instance writer mutex is `fixpp::sync::async_mutex` (FR-015) — **no transitional carve-out**; no `std::recursive_mutex`; no `std::mutex` in any header that includes `asio::awaitable<...>`. Enforced by the existing 006-shipped clang-tidy / grep gate. |
| `[const §XV.15]` | Banned `drop-oldest` on app/session message path | `MemoryStore::Config::capacity_policy` is a closed 2-value enum (`bounded`, `unbounded`); `evict_oldest` is **UNREPRESENTABLE** — not a public name, not a numeric value; `[[clang::enum_extensibility(closed)]]` + `static_assert` at every switch + runtime out-of-range-cast reject. `bounded` returns `store_capacity_exhausted` on overflow (no silent drop); `unbounded` grows without limit (test/embedded only). FR-006 / SC-004 / seam 4. |
| `[const §XVI.3]` | `/clarify` mandatory pre-`/plan` (threading + error semantics + session FSM hand-off) | `/speckit-clarify` run; **5 questions** — all *codebase-reality scoping* (FSM-dependent-seam realization via scripted test-double, `seqnum_t` placeholder authoring, `[2d]` amendments-already-applied vs `[arch]`/catalogue/coverage-index owed-by-this-feature, hybrid catalogue ownership, `EngineConfig::max_store_memory_per_session` field shape), **0 design-doc clarifications** (signed-off / Gate-A-converged). Recorded in `spec.md` Clarifications Session 2026-05-20. |
| `[const §XVI.4]` | `/analyze` mandatory before `/implement` | **`[const §XVI.4]` mandates `/speckit-analyze` MANDATORY for the threading/error/session-FSM-hand-off trigger set and runs before `/implement`** (drift check constitution ↔ spec ↔ plan ↔ tasks). **Bundle-local pipeline-order statement** (single in-bundle source of truth — all other mentions cross-reference this row): `/plan` → Gate A → `/tasks` → `/analyze` → `/implement` → `/simplify` → `/speckit-verify` → Gate B. Derived from `[const §XVI.1]` (command set) + `[const §XVII.1]` (Gate-A-blockers-resolved-before-`/tasks`) + `[const §XVI.4]` (`/analyze` before `/implement`) + `[const §XVI.7]` (`/simplify` between `/implement` and `/speckit-verify` per pipeline step 9.5) + `[const §XVII.8]` (`/speckit-verify` mandatory post-`/implement`). |
| `[const §XVII.1]`, `[const §XVII.2]`, `[const §XVII.3]`, `[const §XVII.7]`, `[const §XVII.8]` | Gate A; Gate B; author ≠ reviewer; local build gate; `/speckit-verify` mandatory | `gate_a_required: yes` — **Public C++ API (new `MessageStore` interface + concrete impls) + Concurrency/Threading (writer-mutex contract, cancellation-result contract, session-shutdown ordering) + Error semantics (10 new variants) + Session FSM recovery contract (`retrieve(begin,end)` query shape per `[FIX-SL §4.8.3]` / `[FIX-SL §4.8.5]`) + new pluggable interface (`MessageStore` per `[const §XIV.1]`)** per Appendix A. Phase-2 design doc signed-off / Gate-A-converged via `/gate-a-ph2` (rounds 1–3 + post-cap line-edit pass; convergence log Appendix C), but the **Phase-4 bundle Gate A is its own review of record**. Both Codex passes (rescue + `/codex:adversarial-review`) per `feedback_gate_a_codex_dual_pass`. Gate B mandatory pre-merge; author ≠ reviewer per `[const §XVII.3]` (Codex's Gate B comes from a fresh Codex session, not the implementer); local build gate precondition; `/speckit-verify` mandatory post-`/implement`. |
| `[const §XVII.8]` Label-evidence rule | `gate-{a,b}-{done,waived}` requires paired evidence | The `/speckit-verify` decision record at `.specify/decisions/008-message-store-verify.md` (LOCAL ONLY, gitignored) is the paired evidence for any `gate-b-done` / `gate-b-waived` label. The codecov/patch external soft gate is treated per `feedback_codecov_patch_vs_lcov_da_brda_gate`: the binding gate is per-file lcov DA/BRDA with Article IX §1 written justifications recorded in verify.md §T (T-numbering pinned at `/tasks`); a Codecov gap below the per-PR threshold is recorded as an **explicit waiver-with-rationale** citing the verify-doc exemption table + PR #73 / PR #74 precedent. |
| `[const §XVIII.5]` | No early shipping of post-v1 protocols | Replicated / cross-process / cloud `MessageStore` (COM-009) is **post-v1.0**; this feature ships zero design for it (forward-compat-invariant note added to COM-009's catalogue row per FR-038; no impl, no header). |

**Gates — PASS. Complexity Tracking is EMPTY.** No constitution violation requiring justification. `[const §XIV.1]` satisfied (`MessageStore` IS the plugin; two default impls shipped). `[const §XIV.2]` satisfied with a real plugin (4 / 5 — within cap, no justification paragraph required). `[const §VII.5]` **applicable at the store-side raw-frame round-trip layer only** (seam 17); the FIX-TC TC-001..TC-017 cases that need the session FSM are deferred to `005` and `005`'s feature-completeness audit — NOT this feature's audit (recorded D-5). `[const §VII.6]` N/A (no FIX FSM here; interop config-translation discharged at seams 11/12). `[const §VII.7]` strictly N/A (not parser-touching) but a libFuzzer fuzzer is shipped voluntarily per design-doc Gate-A discretion. `[const §IX.5]` N/A (no C-ABI surface added). No Article XVII §1 recorded Gate-A-blocker waiver needed at this stage.

## Project Structure

### Documentation (this feature)

```text
specs/008-message-store/
├── plan.md              # This file (/speckit-plan output)
├── spec.md              # /speckit-specify output (already on disk)
├── research.md          # Phase 0 — D-1..D-N decisions
├── data-model.md        # Phase 1 — E1..E_N entities, invariants I-01..I-N, error slots 56–65
├── quickstart.md        # Phase 1 — build/test/TSan/coverage matrix
├── contracts/           # Phase 1 — 12 shape-oracle headers (one per public surface + engine-internal concept)
│   ├── message_store.hpp
│   ├── retrieve_visitor.hpp
│   ├── memory_store.hpp
│   ├── file_store.hpp
│   ├── message_store_factory.hpp     # extends 007's minimal stub
│   ├── memory_store_factory.hpp
│   ├── file_store_factory.hpp
│   ├── direction.hpp
│   ├── seqnum.hpp
│   ├── cfg_loader.hpp
│   ├── has_flush_for_session_close.hpp  # engine-internal concept (E11; FR-028)
│   └── store_errors.hpp              # the 10 slots 56–65
├── checklists/
│   └── requirements.md  # already on disk from /speckit-specify
└── tasks.md             # Phase 2 output (/speckit-tasks — NOT created by /speckit-plan)
```

### Source Code (library submodule root: `research/G19-fix-fpml-iso20022/library/`)

```text
include/fixpp/
├── core/
│   ├── engine_config.hpp                    # MODIFY: +max_store_memory_per_session field
│   └── error.hpp                            # MODIFY: +10 variants at slots 56–65
└── session/
    ├── direction.hpp                        # NEW: direction_t enum
    ├── seqnum.hpp                           # NEW: placeholder alias + constants
    ├── message_store.hpp                    # NEW: 4-pure-virtual interface
    ├── retrieve_visitor.hpp                 # NEW: awaitable per-frame visitor
    ├── memory_store.hpp                     # NEW: final in-memory impl
    ├── file_store.hpp                       # NEW: final file-based impl
    ├── memory_store_factory.hpp             # NEW: MessageStoreFactory impl
    ├── file_store_factory.hpp               # NEW: MessageStoreFactory impl
    ├── message_store_factory.hpp            # MODIFY: extend minimal stub with make() pure-virtual
    ├── detail/
    │   └── has_flush_for_session_close.hpp  # NEW: concept-shaped non-virtual dispatch
    └── quickfix_compat/
        └── cfg_loader.hpp                   # NEW: cfg_to_file_store_factory

src/session/
├── memory_store.cpp                         # NEW (if any out-of-line; mostly header-inline)
├── file_store.cpp                           # NEW: pwrite/fdatasync/rename impl + Windows code paths
├── message_store_factory.cpp                # NEW (optional out-of-line if MemoryStore/FileStoreFactory go OOL)
└── quickfix_compat/
    └── cfg_loader.cpp                       # NEW: QuickFIX .cfg parser

tests/
├── session/                                 # 18 of 21 seams live here
│   ├── test_memory_store_round_trip.cpp                    # seam 1
│   ├── test_file_store_crash_survival.cpp                  # seam 2
│   ├── test_file_store_torn_write.cpp                      # seam 3 (Tier-1 + Tier-2)
│   ├── test_memory_store_capacity.cpp                      # seam 4
│   ├── test_store_fifo_fair.cpp                            # seam 5
│   ├── test_store_cancellation_contract.cpp                # seam 6
│   ├── test_outbound_store_post_commit.cpp                 # seam 7
│   ├── test_retrieve_with_gaps.cpp                         # seam 8
│   ├── test_retrieve_visitor.cpp                           # seam 9
│   ├── test_store_reset.cpp                                # seam 10
│   ├── test_quickfix_compat_path_b_guard.cpp               # seam 11
│   ├── test_quickfix_compat_cfg_loader.cpp                 # seam 12
│   ├── test_memory_store_zero_allocator_calls.cpp          # seam 15
│   ├── test_store_pmr_poison_retrieve.cpp                  # seam 16
│   ├── test_store_shutdown_ordering.cpp                    # seam 18
│   ├── test_file_store_flush_for_session_close.cpp         # seam 19
│   └── test_store_seqnum_out_of_order.cpp                  # seam 20
├── conformance/
│   └── test_store_corpus_replay.cpp                        # seam 17 (raw-frame round-trip)
├── perf/
│   └── test_store_alloc_guard.cpp                          # seam 14 (mallocnesia + tools/check_alloc.py)
└── fuzz/
    └── fuzz_message_store.cpp                              # seam 21 (libFuzzer)

bench/session/
├── bench_memory_store.cpp                                  # seam 13 (latency regression)
└── bench_file_store.cpp                                    # FileStore disk-bound rows

bench/baselines/session/                                    # per-host hardware floors

book/migration_from_quickfix.md                             # MODIFY: +MessageStore section (FR-031)

spec/
├── coverage-index.md                                       # MODIFY: line 76 row (FR-037)
└── feature-catalogue.md                                    # MODIFY: OSS-002 + COM-009 rows (FR-038)

.specify/
└── architecture.md                                         # MODIFY: line 598 §11 Q3 row (FR-039)

tools/
└── check_alloc.py                                          # CONSUMED unchanged (existing tool)
```

**Structure Decision**: Mirrors 007's split (header-dominant `include/`, out-of-line `src/` for I/O-heavy / platform-specific code) with the additional `session/quickfix_compat/` subdirectory for the `cfg_loader`. No new top-level directory; `session/detail/` exists already for 007's internal helpers and we add `has_flush_for_session_close.hpp` there. The header tree above is the canonical layout — `/tasks` references it verbatim.

## Complexity Tracking

> **Constitution Check passes. This section is intentionally empty.**

No violation requires justification:

- `[const §XIV.2]` (≤5 pure-virtual) satisfied with **4 / 5** — no justification paragraph required.
- `[const §XV.15]` (banned `drop-oldest`) satisfied — `evict_oldest` is structurally unrepresentable on the public API.
- `[const §XV.4]` (banned sync-disk-I/O-per-send) satisfied — `FileStore` posts I/O work to `file_io_executor`, never on the session strand.
- `[const §IX.5]` (abidiff) N/A — no C-ABI symbol added.
- `[const §VII.6]` (interop test) N/A — no FIX FSM; QuickFIX-compat discharged at config translation only.

---

## Phase 0 — Outline & Research

Phase 0 artifact: [research.md](research.md). Resolves spec NEEDS-CLARIFICATION items + per-tech best-practices + integration patterns. No design-doc NEEDS-CLARIFICATION are open (design-doc signed-off through v0.4). The 5 `Session 2026-05-20` clarifications in `spec.md` are recorded as D-4 / D-8 / D-9 / D-10 / D-11.

**Open research items resolved in research.md (D-IDs are research-doc-local, not error-code slots):**

- **D-1.** `seqnum_t` placeholder header authoring — fresh creation per Clarifications Q2 (no existing header to repoint).
- **D-2.** Single `core::error` enum vs separate `store_error` type — single enum (consistent with 002/003/004/006/007 precedent).
- **D-3.** `crc32c` vs `std::crc32` vs hand-rolled CRC32 for the per-record CRC32 + sentinel CRC32 — toolchain probe at T002 decides; if `std::crc32` unavailable on the matrix, add `crc32c/1.1.2` Conan row.
- **D-4.** FSM-dependent seam realization — deterministic scripted test-double FSM in 2e test fixture per Clarifications Q1 (analogue of 007's pattern).
- **D-5.** `[const §VII.5]` / `[const §VII.6]` applicability — store-side raw-frame round-trip is `[const §VII.5]`-applicable (seam 17); FIX FSM TC-001..TC-017 cases deferred to `005`.
- **D-6.** No C-ABI surface added; 10 new C++ error variants at unused slots 56–65; coalescing groups documented for `2i`.
- **D-7.** `file_io_executor` shape — per-`FileStore`-instance ASIO `io_context` with one worker thread (`[2e §10] Q10`); cancellation slot composes via `cancellable_dispatch`.
- **D-8.** `[2d §4.5]` / `[2d §4.7]` cross-doc amendments already applied at 007 merge (D.1 at `session_config.hpp:106`, D.2 at `2d-threading.md:853`) per Clarifications Q3; this feature realizes the hook, not the amendments.
- **D-9.** `[arch §11]` Q3 + `coverage-index.md:76` + `feature-catalogue.md` OSS-002 / COM-009 amendments + `EngineConfig::max_store_memory_per_session` field shipped **inside this feature's merge bundle** (FR-014a / FR-037 / FR-038 / FR-039); S-011/S-012/S-013 Status promotion is orchestrator step 19 at Gate-B merge per the 007 precedent (Clarifications Q4).
- **D-10.** `max_store_memory_per_session` value-shape and propagation — `std::size_t` field on `EngineConfig`, default `1ULL << 30` (1 GiB), engine-wide (no SessionConfig override) per Clarifications Q5; factories receive an `EngineConfig*` or value reference at engine-open time for `make()` validation.
- **D-11.** `flush_for_session_close()` dispatch mechanism — concept-shaped non-virtual via `has_flush_for_session_close` + factory-type tag at session open (Opus N3-P2-1); NOT RTTI / `dynamic_cast`.
- **D-12.** Catalogue Status promotion mechanics for S-011/S-012/S-013 — orchestrator step 19 at Gate-B merge per 007 NFR-015 / `[2c App D]` precedent; this feature does NOT promote.
- **D-13.** Conformance-corpus shape — `tests/conformance/test_store_corpus_replay.cpp` is a **store-side raw-frame round-trip** test loading recorded FIX session bytes; it does NOT invoke the FIX FSM (deferred `005`); it discharges S-011/S-012/S-013 + the storage half of S-014.
- **D-14.** Cross-doc edit tracking — three edits in this bundle (FR-037 `coverage-index.md:76`; FR-038 `feature-catalogue.md` OSS-002 / COM-009; FR-039 `architecture.md:598`); pipeline step 19 records the post-merge submodule SHA + catalogue Status promotion.

**Output:** [research.md](research.md) with all NEEDS-CLARIFICATION resolved.

## Phase 1 — Design & Contracts

Phase 1 artifacts:

- **[data-model.md](data-model.md)** — E1..E12 entities (MessageStore, MemoryStore, FileStore, MessageStoreFactory, MemoryStoreFactory, FileStoreFactory, retrieve_visitor, direction_t, seqnum_t, FileStorePolicy, has_flush_for_session_close concept, cfg_loader), invariants I-01..I-N (writer-mutex-on-every-method, deep-copy-before-suspension, retrieve-releases-mutex-across-co_await, atomic-rename + platform durability primitive, `flush_for_session_close` graceful-only, `capacity_policy::evict_oldest` unrepresentable, …), error-slot table 56–65 with C-ABI coalescing groups documented for `2i`.
- **[contracts/](contracts/)** — 10 shape-oracle headers (one per public surface; declaration-only with `SPDX-License-Identifier: AGPL-3.0-or-later`, no implementation). The contracts directory is the **frozen-at-/plan public-API oracle** — `/tasks` generates implementation tasks against these declarations, and the eventual `include/fixpp/session/*.hpp` source headers `static_assert` against the contracts' types where feasible (e.g., method signature equality via `std::is_same_v` on the awaitable return types).
- **[quickstart.md](quickstart.md)** — build / test (`linux-clang-debug`, `linux-clang-release`, `linux-clang-asan`, `linux-clang-ubsan`, `linux-clang-tsan`, `linux-clang-coverage`) / TSan **mandatory** / ASan + UBSan / alloc-guard (`tools/check_alloc.py` + `mallocnesia`) / fuzz (10-minute libFuzzer corpus run) / bench / coverage / `/speckit-verify` / `/gate-a` / `/gate-b` recipes; Windows MSVC Tier-2 invocation for seams 2 / 3 / 19 (crash-survival + torn-write + `MOVEFILE_WRITE_THROUGH`).
- **Agent context update** — the active-feature line in the submodule `CLAUDE.md` (between `<!-- SPECKIT START -->` and `<!-- SPECKIT END -->` markers) is repointed to `specs/008-message-store/plan.md` post-`/plan`.

## Post-Phase-1 Constitution Re-Check

Re-evaluated after Phase 1 design generation. **No new violations introduced.**

- `[const §XIV.2]` still 4 / 5 (the concept-shaped `has_flush_for_session_close` is **not** a pure-virtual on `MessageStore`; it's a free `requires`-clause on a separately-declared `FileStore` method — within cap).
- `[const §XV.15]` still satisfied (the data-model E6 `capacity_policy` enum is closed 2-value with `[[clang::enum_extensibility(closed)]]`; switches `static_assert`-cover both values).
- The 10 new error slots (56–65) are still non-renumbering vs the 007-merged baseline (highest used slot = 55).
- The `EngineConfig::max_store_memory_per_session` field addition is a **non-breaking append** (default value preserves prior `EngineConfig` semantics — a session whose store does not exceed 1 GiB is unaffected; the field is engine-wide, no SessionConfig surface change).

**Gates remain PASS. Complexity Tracking remains EMPTY.**

---

**Branch:** `008-message-store` (in submodule `research/G19-fix-fpml-iso20022/library/`)
**Plan:** `specs/008-message-store/plan.md` (this file)
**Generated artifacts (Phase 0 + Phase 1):**

- `specs/008-message-store/research.md`
- `specs/008-message-store/data-model.md`
- `specs/008-message-store/quickstart.md`
- `specs/008-message-store/contracts/{message_store,retrieve_visitor,memory_store,file_store,message_store_factory,memory_store_factory,file_store_factory,direction,seqnum,cfg_loader,has_flush_for_session_close,store_errors}.hpp` (12 files)
- Submodule `CLAUDE.md` Active-feature line repointed to this plan.

**Next:** `/speckit-tasks` → `/speckit-analyze` → `/gate-a` → `/speckit-implement` → `/simplify` → `/speckit-verify` → `/gate-b`.

---

**Citation verification** — every `[const §...]` / `[FIX-SL §...]` / `[arch §...]` / `[SYN §...]` / `[2X §...]` reference in this plan was checked against `.specify/constitution.md` v0.1 and the in-tree design docs. No dangling references.
