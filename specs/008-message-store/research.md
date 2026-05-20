# Research — 008-message-store (Phase 0)

**Branch:** `008-message-store` | **Date:** 2026-05-20 | **Plan:** [plan.md](plan.md)
**Design anchor:** `.specify/2e-msgstore.md` v0.4 (Gate-A-converged, round 3, post-cap line-edit pass). On conflict the design doc wins.

Resolves spec NEEDS-CLARIFICATION items, captures the toolchain / integration / best-practice decisions referenced by the plan, and pins the realization of the 5 codebase-reality clarifications recorded in `spec.md` Session 2026-05-20. **D-IDs in this document are research-doc-local and are NOT error-code slot numbers.**

---

## D-1 — `seqnum_t` placeholder header authoring strategy

**Decision:** Author `include/fixpp/session/seqnum.hpp` **fresh** as a new header containing:

```cpp
namespace fixpp::session {
using seqnum_t = std::uint32_t;
inline constexpr seqnum_t seqnum_min = 1;
inline constexpr seqnum_t seqnum_max = std::numeric_limits<seqnum_t>::max();
}  // namespace fixpp::session
```

with an explicit cross-doc handoff comment naming the deferred Phase-4 session-module spec as the canonical owner per `[2e §3.1]` / `[2e §4.7]` / `[2e §10] Q9`.

**Rationale:** Spec Clarifications Session 2026-05-20 Q2 verified against the merged 007 baseline that `include/fixpp/session/` currently holds only `async_lock_via_session_executor.hpp`, `message_store_factory.hpp` (minimal stub), `security_profile.hpp`, `session.hpp`, `session_config.hpp` — **no `seqnum.hpp` exists in tree today**. There is no header to repoint; fresh authoring is the structurally-correct path. When the Phase-4 spec lands, the header is either re-exported from there or deleted with includes repointed (single-line edit per `[const §VI.5]` exact-coverage discipline).

**Alternatives considered:**

- *Repoint to an existing header.* Rejected — no candidate exists; manufacturing a candidate (e.g., adding `using seqnum_t = ...` to `session.hpp`) violates the design-doc § 3.1 ownership boundary and forces a second move when Phase-4 ships.
- *Defer the placeholder.* Rejected — `seqnum_t` is on the public API surface of `MessageStore::store` / `retrieve` / `next_seqnum`; the type must exist at `/implement` time. Deferring forces the design-doc convention into `int` / `uint32_t` literals scattered across implementation, which then need a sweeping rename when Phase-4 lands (`[const §VI.5]` exact-coverage violation).
- *Use `uint64_t` directly.* Rejected — `[2e §10] Q9` records the type-width decision as Phase-4-owned (32-bit observed-convention vs 64-bit overflow-eliminating tradeoff; 2× memory in counters + index entries). 2e MUST consume the convention, not foreclose it.

---

## D-2 — Single `core::error` enum vs separate `store_error` type

**Decision:** Append the 10 store variants to the existing `fixpp::core::error` single enum at unused slots **56–65**, non-renumbering per `[const §X.4]`. No separate `store_error` type; the C-ABI coalescing groups are documented for `2i` but the C++ surface is a single enum.

**Rationale:** Matches the 002 / 003 / 004 / 006 / 007 precedent (all five upstream features append to the same enum, each at their assigned slot range). A separate `store_error` type would require either (a) a `std::variant<core::error, store_error>` propagation surface — incompatible with the project-wide `expected_t<T>` pattern — or (b) a translation layer between layers, which is precisely the boilerplate `[arch §5.3]` `expected_t<T>` was designed to eliminate. The C-ABI prefix groups (`FIXPP_ERR_STORE_RUNTIME` / `_CONSISTENCY` / `_CONFIG` / `_VISITOR` + reuse of `FIXPP_ERR_CANCELLED` for `store_cancelled`) are mechanical mappings applied by `2i`'s translation layer per `[2d §6.7]` precedent.

**Alternatives considered:**

- *Separate enum + variant.* Rejected — adds a layer for no gain; the prefix-group documentation in the enum comment is sufficient for `2i`.
- *Per-module enum with implicit conversion.* Rejected — implicit conversions on error codes are an anti-pattern (loses the slot identity).

---

## D-3 — CRC32 implementation choice for per-record + sentinel CRC

**Decision:** Add `crc32c/1.1.2` (Google's BSD-3-Clause SSE4.2 / ARM CRC32 instructions when available, software fallback otherwise) as a Conan dependency — **pinned at /plan time** as the CRC32 (Castagnoli polynomial 0x1EDC6F41) source for the per-record + sentinel CRC32. AGPL-compatible per `[const §V.3]` (BSD-3-Clause is permissive; the no-LGPL rule does not bind it).

The choice is local to `FileStore`; nothing else in the codebase consumes a CRC32.

**Rationale:** Per-record CRC32 (FR-009) is the torn-write detector on the FileStore restart scan (FR-012) and inside the sentinel record (FR-009: `magic | version | session_triple_hash | crc32`). Performance matters — a 10⁴-message warm-up at default 200-byte frames computes 10⁴ CRC32 values during open. SSE4.2 / ARM CRC32 instructions are ~10× faster than the byte-table software loop; `crc32c/1.1.2` gives both with one Conan row. The C++ stdlib does not provide a CRC32 in C++23 (no `std::crc32` in `<numeric>` or elsewhere; `<crc>` is at best a C++26 proposal). A hand-rolled CRC32 would force us to implement the platform detection + dispatch, which is exactly what `crc32c` provides.

**Alternatives considered:**

- *Hand-rolled CRC32 byte-table.* Rejected — re-implements platform CPU-feature detection that `crc32c` already provides; ~10× slower on the hot path; no `2k`-equivalent module exists to vendor it cleanly.
- *zlib `crc32()`.* Rejected — would pull in zlib for a single function; `[const §III.2]` Conan-pinned + transitive override hygiene argues against single-symbol dependencies.
- *Toolchain-conditional decision.* Rejected — the prior framing ("if `std::crc32` is unavailable") was misleading: `std::crc32` does not exist in any C++ stdlib today (against Clang 22 / libc++ / libstdc++ — `__cpp_lib_*` SD-6 feature-test macros confirm none). The conditional was a phantom; pinning `crc32c/1.1.2` at /plan time eliminates the false branch.

---

## D-4 — FSM-dependent seam realization strategy

**Decision:** A **deterministic scripted test-double FSM** in the 2e test fixture drives the FSM-dependent seams (1 / 2 / 3 / 7 / 8 / 9 / 10 / 17 / 18 / 19), exactly mirroring 007-threading-clock's scripted test-double pattern for its FSM-dependent seams (3 / 9 / 11 / 15 / 16).

The scripted FSM:

- Reads a `(seqnum, direction, frame_bytes)` script from the test source.
- Issues `store(seq, frame, dir)` / `retrieve(begin, end, dir, visitor)` / `next_seqnum(dir, increment)` / `reset()` calls in the scripted order.
- Asserts **2e-owned properties only**: byte equality after round-trip, mutex serialisation under TSan, awaitable-visitor span lifetime, cancellation-result completion shape per method, FileStore atomicity / durability under SIGKILL, `flush_for_session_close()` graceful-close invocation timing.
- Does **NOT** assert FIX FSM correctness (Logon flow, ResendRequest emission, SequenceReset-GapFill semantics) — those are `005`'s concern.

When `005` is ramped, it replaces the scripted driver with the real FSM at the same seam (each scripted call becomes a real FSM transition; the assertion surface stays identical).

**Rationale:** Spec Clarifications Session 2026-05-20 Q1 records this as the agreed pattern, citing the 007 precedent. The 2e seams test the **store contract**, not the FSM that consumes it. A scripted driver pins the seqnum/direction sequence deterministically so seams 1–3 and 7–10 are reproducible without a real Logon → Heartbeat → Logout flow.

**Alternatives considered:**

- *Block 008 implementation until `005` ramps.* Rejected — would invert the prerequisite chain (`005` is downstream-blocked on `2e`).
- *Mock the entire `Session` type.* Rejected — `Session::session_arena()` / `Session::close()` / cancellation-state composition are real 007 surfaces; mocking them creates a parallel-implementation hazard.
- *Use the real `Session` skeleton without an FSM.* **Adopted partially** — the test fixture instantiates the real 007 `Session` minimal skeleton (it already supports `open`/`close` without FSM logic per 007 Clarifications) and the scripted driver issues the store calls **on the session strand** through that real `Session`. The "scripted FSM" is a thin coroutine in the test fixture that orchestrates the call sequence; the `Session` underneath is real.

---

## D-5 — `[const §VII.5]` / `[const §VII.6]` applicability

**Decision:** `[const §VII.5]` (FIX-TC conformance corpus) is **APPLICABLE** to this feature at the **store-side raw-frame round-trip** layer; `[const §VII.6]` (QuickFIX interop) is **N/A — no FIX FSM**.

**Seam 17** (`tests/conformance/test_store_corpus_replay.cpp`) loads a recorded FIX session corpus (representative subset of the FIX-TC TC-001..TC-017 test fixtures), replays each frame through `MemoryStore::store(seq, frame_bytes, dir)` × N then walks `retrieve(1, 0, dir, byte_compare_visitor)` byte-for-byte. Variant repeats for `FileStore`. The assertion is **byte equality** of every persisted frame against the original input — the store contract's `[FIX-SL §4.8]` recovery oracle.

The seam does NOT invoke the FIX FSM (no `Logon` / `ResendRequest` / `SequenceReset-GapFill` semantics); the recorded corpus is consumed as **opaque byte sequences with seqnum + direction metadata**.

**The FIX-TC TC-001..TC-017 cases that need the session FSM are deferred to `005` and `005`'s feature-completeness audit — NOT this feature's audit.** This is structurally non-applicability of the FSM-dependent half, structurally applicability of the store-side half. The feature-completeness audit gate per `feedback_feature_completeness_gate` MUST pass for `[FIX-SL §4.8]` / `[FIX-SL §4.8.3]` / `[FIX-SL §4.8.5]` recovery at the store-contract layer (S-014's store half), with S-014 staying `backlog` for the FSM half.

**Rationale:** `[const §VII.5]` mandates "Every PR must pass them in CI" for the conformance corpus. This feature ships **a subset** (store-side raw-frame round-trip) that runs every Tier-1 PR; the FSM-dependent subset that exercises the full TC-001..TC-017 cases lands with `005`. Same shape as 003-dictionary-codegen which discharged the dictionary half of catalogue rows D-007..D-009 while leaving the runtime-XML half for 002 (`002`-merged) — split discharge per `[const §VI.5]` exact-coverage with hybrid ownership.

**Alternatives considered:**

- *Declare full N/A.* Rejected — the store IS on the recovery path; declaring full N/A would force `005` to author a parallel store-side round-trip test, which is precisely what seam 17 already tests.
- *Block on `005`.* Rejected — would invert the prerequisite chain.

---

## D-6 — No C-ABI surface added; 10 new C++ error variants at slots 56–65

**Decision:** No `extern "C"` symbol or `fixpp_*` type added in this feature. The 10 store error variants live in `fixpp::core::error` at unused slots 56–65 per `[const §X.4]`. The C-ABI prefix-group mapping (FR-023) is **documented** in the `error.hpp` comment for `2i`'s consumption, but **not implemented** here.

C-ABI mapping for `2i`:

```text
FIXPP_ERR_STORE_RUNTIME      ← { store_io_failure (56),
                                 store_capacity_exhausted (59),
                                 store_seqnum_overflow (60) }
FIXPP_ERR_STORE_CONSISTENCY  ← { store_seqnum_gap (57),
                                 store_seqnum_out_of_order (58),
                                 store_seqnum_invalid (63),
                                 store_invalid_range (64) }
FIXPP_ERR_STORE_CONFIG       ← { store_factory_failed (61) }
FIXPP_ERR_STORE_VISITOR      ← { store_visitor_aborted (62) }
FIXPP_ERR_CANCELLED          ← { store_cancelled (65) }  # reused, joins 2d's dispatch_aborted / clock_sleeps_cancelled
```

**Slot allocation (design-doc table order, non-renumbering append at unused slots 56–65):**

| Slot | Variant | C-ABI group | Owner |
|------|---------|-------------|-------|
| 56 | `store_io_failure` | `FIXPP_ERR_STORE_RUNTIME` | 2e |
| 57 | `store_seqnum_gap` | `FIXPP_ERR_STORE_CONSISTENCY` | 2e |
| 58 | `store_seqnum_out_of_order` | `FIXPP_ERR_STORE_CONSISTENCY` | 2e |
| 59 | `store_capacity_exhausted` | `FIXPP_ERR_STORE_RUNTIME` | 2e |
| 60 | `store_seqnum_overflow` | `FIXPP_ERR_STORE_RUNTIME` | 2e |
| 61 | `store_factory_failed` | `FIXPP_ERR_STORE_CONFIG` | 2e |
| 62 | `store_visitor_aborted` | `FIXPP_ERR_STORE_VISITOR` | 2e |
| 63 | `store_seqnum_invalid` | `FIXPP_ERR_STORE_CONSISTENCY` | 2e |
| 64 | `store_invalid_range` | `FIXPP_ERR_STORE_CONSISTENCY` | 2e |
| 65 | `store_cancelled` | `FIXPP_ERR_CANCELLED` (reused) | 2e |

**Rationale:** Same time-bounded waiver shape as 002 / 003 / 004 / 006 / 007 — no C-ABI surface added by any of them; `2i` is the single owner of the C-ABI translation layer. The slot range 56–65 is contiguous with the 47–55 range 007 used; the next downstream feature (`2g` / `2h` / `2i` / `2j` / `2k`) inherits the cursor at slot 66.

**NOT defined (recorded for `2i` and future readers):**

- `store_concurrent_writer` — REMOVED in v0.2 per Codex P1-5 (FIFO-fair `async_mutex` makes the variant impossible).
- `store_shim_timeout` — REMOVED in v0.3 per Codex C-R2-P2-1 escalation (`[2e §4.8.B]` Path A retired; no runtime adapter, no shim timeout).

**Alternatives considered:**

- *Add the C-ABI surface in this PR.* Rejected — `2i` is a separate feature; mixing the C-ABI translation layer into 2e mixes scopes, violates the `[arch §10]` row 2e / row 2i ownership separation, and prevents `2i`'s atomic `tools/abi_history` audit-trail entry (which must capture the consolidated C-ABI surface across all upstream features).
- *Renumber slots 47–55.* Rejected — `[const §X.4]` non-renumbering rule is absolute pre-publication AND post-publication.

---

## D-7 — `file_io_executor` shape

**Decision:** `FileStore::Config::file_io_executor` is required-at-`FileStore`-construction per design-doc §4.3.2 line 665. The value is **resolved at `FileStoreFactory::make()` time** under the Config-supplied-wins rule:

1. If the factory's stored `Config.file_io_executor` is non-empty (caller passed their own executor at factory construction), that wins.
2. Otherwise, the engine threads `EngineConfig::file_io_executor` (a new `asio::any_io_executor` field added to `EngineConfig` by FR-024a per design-doc §4.3.2 line 669) into each `MessageStoreFactory::make()` invocation as the 5th `asio::any_io_executor file_io_executor` parameter (FR-005 / FR-024), and `FileStoreFactory::make()` populates the minted `FileStore::Config::file_io_executor` with this value.
3. If both are empty, `FileStoreFactory::make()` returns `store_factory_failed` (I-11 — there is no "no executor" operating mode for `FileStore`).

This preserves the design-doc §4.4 Config-only factory CTOR (no `EngineConfig&` back-channel on the constructor) AND the `[2e §4.3.2]:665` required-at-`FileStore`-construction contract (because `FileStore` itself is constructed inside `make()`, after the executor is resolved). It mirrors the exact threading pattern used for `EngineConfig::max_store_memory_per_session` → `make()`'s 4th parameter `max_store_memory_bytes` (FR-014a / D-10): both engine-resolved values flow into the factory at call time, not at construction time. The path-only `cfg_loader` (FR-030) therefore composes: it returns a `FileStoreFactory` whose `Config.file_io_executor` is empty, and the engine fills it in from `EngineConfig::file_io_executor` at session-open / `make()` time.

`EngineConfig` exposes the default `file_io_executor` (typically a 4-thread `asio::thread_pool` shared across all `FileStore`s in the engine, per design-doc §4.3.2 line 669); a single per-`FileStore`-instance configuration is one valid operating mode (no cross-thread races on the log file handle within one store), not the frozen contract. The session strand `co_await`s a completion that runs on `file_io_executor`; completions rebind to the session strand via `[2d §6.5]` `cancellable_dispatch`. The cancellation slot composes the same way.

**Rationale:**

- Cross-thread races on the log file handle (the file descriptor / `HANDLE` is shared between `pwrite` and `fdatasync`) are avoided within a single store either by a per-instance executor or by a shared pool whose work-stealing serialises the per-handle operations through the store's writer mutex — `[2e §10] Q10` records the EngineConfig-default-pool shape as DECIDED at design-doc §4.3.2.
- The 4-thread shared pool is the design-doc's typical case: it keeps `[const §XV.4]` (no sync I/O on session strand) and `[const §XV.1]` (no per-message alloc) compatible at engine scale, where a per-session/per-instance executor would burn N threads at idle (resource hazard) and a single per-engine 1-worker executor would serialise all sessions' I/O through one thread (latency hazard).

**Cancellation:** the awaitable returned from `FileStore::store(...)` carries a cancellation slot per `[const §XI.2]`; if the slot fires before `pwrite` is issued or before `fdatasync` returns success, the linearisation point per `[2e §6.1.4]` has not been reached → `expected_t::unexpected{store_cancelled}`, no state change. After `fdatasync` returns success the linearisation point IS reached → normal completion, durable state (cancellation cannot un-do durability). The `commit_interval(ms)` timer is also posted on `file_io_executor`; its cancellation surface composes the same way.

**Storage-class shape:** mirrors 007's `system_clock_source`'s lifetime-binding discipline — `[arch §5.4]` storage-class shape, `[const §VIII.5]` zero-global-heap discipline. The `file_io_executor` is constructed at engine open (typical case: the `EngineConfig`-exposed 4-thread `asio::thread_pool` shared across stores) from engine-PMR, NOT on every call; per-instance construction (a single-thread `asio::thread_pool`) is the alternate operating mode for tests / specialised deployments where a single store wants its own executor.

**Alternatives considered:**

- *Run `pwrite` / `fdatasync` on the session strand.* Rejected — violates `[const §XV.4]` (sync disk-I/O on every send) and freezes the session strand for the duration of an `fdatasync` (~100µs–10ms on NVMe).
- *Use ASIO's `asio::stream_file` / `asio::random_access_file` (C++20-aware async file API).* Deferred — these are `[asio 1.36.0]`-shipped but their cancellation semantics interact with Linux io_uring in ways the test matrix hasn't validated for crash-survival. T-impl decides whether to adopt them or stay on `posix::stream_descriptor` + `pwrite` + `fdatasync` on a thread.

---

## D-8 — `[2d §4.5]` / `[2d §4.7]` cross-doc amendments already applied at 007 merge; `[2e §4.4]` Appendix D §D.3 5-param `make()` amendment pre-applied at Path A inside this PR diff

**Decision:** Verified at this `/plan`-stage against the merged 007 baseline:

- `[2d §4.5]` Appendix D §D.1 (`SessionConfig::store_factory` typed `std::unique_ptr<MessageStoreFactory>`) — **shipped** at `include/fixpp/session/session_config.hpp:106`. The line reads `std::unique_ptr<MessageStoreFactory> store_factory;   // unique ownership`. The `MessageStoreFactory` polymorphic-bind-target stub at `include/fixpp/session/message_store_factory.hpp` is the supporting type.
- `[2d §4.7]` Appendix D §D.2 (`FileStore::flush_for_session_close()` row in the per-mode effect table + the §857 hook contract paragraph) — **shipped** at `.specify/2d-threading.md:853` (per-mode effect-table row) and `.specify/2d-threading.md:857` (the hook contract paragraph: "engine reaches the concrete FileStore via the has_flush_for_session_close concept...").
- `[2e §4.4]` Appendix D §D.3 (5-param `MessageStoreFactory::make()` signature — round-1 added 4th `max_store_memory_bytes`, fresh-loop round-2 added 5th `file_io_executor`) — **pre-applied at Path A (2026-05-20)** inside this PR diff, parallel to FR-037/038/039 hybrid-ownership pattern. Appendix D §D.3 was added to `.specify/2e-msgstore.md` at Gate A fresh-loop round-2 rewrite (2026-05-20); the live `[2e §4.4]` block at `.specify/2e-msgstore.md:712-732` now carries the 5-param shape and §D.3 records the byte-exact diff form (parallel to D.1/D.2's pre-application at 2e v0.4 sign-off, which were then shipped through 007's merge).

**This feature does NOT re-amend `[2d]`** (D.1 / D.2 are already applied). It DOES amend `[2e]` itself via Appendix D §D.3 (the 5-param `make()` shape post-dates the design-doc v0.4 sign-off). It realizes:

- The factory's pure-virtual 5-param `make(...)` method that returns `expected_t<std::unique_ptr<MessageStore>>` — i.e., it **extends in place** the 007 stub by adding the virtual method to the existing class (NOT a new class), and the `[2e §4.4]` block-edit makes the design-doc surface match the post-amendment shape.
- The concrete `FileStore::flush_for_session_close()` method.
- The `has_flush_for_session_close` concept.
- The seam (#19) that exercises both under `Session::close(graceful)` vs `Session::close(terminal)`.

**Rationale:** Spec Clarifications Session 2026-05-20 Q3 records this as DECIDED, with the verification anchor `2d-threading.md:552` / `:853` / `:857` and `session_config.hpp:106`. The verify command for the implementation team:

```bash
grep -n 'store_factory\|flush_for_session_close' \
  include/fixpp/session/session_config.hpp \
  .specify/2d-threading.md
```

should return the three load-bearing lines unchanged from the 007 merge.

---

## D-9 — `[arch §11]` Q3 + `coverage-index.md:76` + `feature-catalogue.md` OSS-002 / COM-009 + `EngineConfig::max_store_memory_per_session` — pre-applied at Path A inside this PR diff

**Decision:** All four body edits were **pre-applied at Path A (2026-05-20)** inside this PR diff as part of FR-014a / FR-037 / FR-038 / FR-039 (hybrid ownership per Clarifications Session 2026-05-20 Q4). Status promotion for catalogue rows S-011 / S-012 / S-013 stays merger-applied at `.specify/pipeline.md` step 19.

**Verified anchors (post-Path-A state):**

- `.specify/architecture.md:598` read `| 3 | QuickFIX-compat shim for synchronous MessageStore impls — feasible or document as known incompatibility | 2e | Phase 2 validates [SYN §3.2 Q7] |` at `/plan` time; **amended at Path A (2026-05-20)** to `CLOSED — Path B verdict per [2e §4.8.A]; v1.0 ships documented incompatibility + migration recipe + quickfix_compat::cfg_loader config-translation surface (no runtime adapter). Disposition applied by 008-message-store Phase-4 Gate A convergence (2026-05-20) per FR-039.`
- `spec/coverage-index.md:76` read `| §4.8 | Message recovery | Y | S-014 | — |` at `/plan` time; **amended at Path A (2026-05-20)** to `| §4.8 | Message recovery | Y | S-011, S-012, S-013, S-014 | — |` (note: store-side discharges S-011/S-012/S-013; S-014 stays `backlog` for `005`).
- `spec/feature-catalogue.md:240` (OSS-002 row) — **updated at Path A (2026-05-20)** with the v0.3 Path B verdict (no runtime adapter ships; documented incompatibility + `cfg_loader` config translation only).
- `spec/feature-catalogue.md:332` (COM-009 row) — **gained at Path A (2026-05-20)** a forward-compat note pointing at `[2e §10] Q2` (post-v1.0; row stays `backlog`).
- `include/fixpp/core/engine_config.hpp` adjacent to line 119 (`default_store_factory` field) — to gain `std::size_t max_store_memory_per_session = 1ULL << 30;` (FR-014a) AND `asio::any_io_executor file_io_executor;` (FR-024a per design-doc §4.3.2:669) at `/speckit-implement`.

**The S-011 / S-012 / S-013 row Status promotion (`backlog → done`) is applied by the merger at this feature's Gate-B merge** per `.specify/pipeline.md` step 19 — explicit human handoff, not auto-applied (D-12 below). This feature's PR diff ships the **structural cross-doc body edits** (Path-A-applied); it does NOT ship the Status promotion in the catalogue (which is the Gate-B-merge close-out's signal that the row's implementation is real, not just declared).

**Rationale:** Spec Clarifications Session 2026-05-20 Q4 records this as DECIDED, citing the 007 NFR-015 / `[2c App D]` precedent. The user's feedback memory `feedback_pipeline_mark_done_step` reinforces step 19 as the canonical close-out path.

---

## D-10 — `EngineConfig::max_store_memory_per_session` field shape and propagation

**Decision:**

- **Field shape:** `std::size_t max_store_memory_per_session = 1ULL << 30;` (1 GiB default per `[2e §1.2]`), placed in `EngineConfig` adjacent to the existing `default_store_factory` field (line 119 of `engine_config.hpp` as of the 007 baseline).
- **Engine-wide cap (no `SessionConfig` override)** per `[2e §1.2]` magnitude-domain framing. The cap models a deployment policy ("the engine refuses to allocate more than X bytes per session for store state"), not a per-session tuning knob.
- **Propagation to factories:** the engine threads the resolved `max_store_memory_per_session` value into each `MessageStoreFactory::make()` invocation as the 4th `std::size_t max_store_memory_bytes` parameter (FR-005 / FR-014a). The factory CTOR stays **Config-only** per the design-doc §4.4 frozen surface (`explicit MemoryStoreFactory(MemoryStore::Config c = {}) noexcept;` and `explicit FileStoreFactory(FileStore::Config c) noexcept;`); no `EngineConfig&` / `EngineConfig*` back-channel on the constructor. `make()` enforces the overflow-safe checked-arithmetic guard against the threaded-in cap value per FR-014 / I-11; on overflow / cap-exceeded conditions returns `store_factory_failed`. Storage-DoS construction guard per FR-014 / SC-004 / `[2e §1.2]` / round-2 N9 / C-R2-P1-3 close. This also resolves the round-1 N2 paired finding: the v0.4 frozen public surface is preserved AND the engine cap is threaded at call time.

**Rationale:** Spec Clarifications Session 2026-05-20 Q5 records this as DECIDED. `std::size_t` is the correct width for a memory cap (matches `sizeof` returns, no truncation hazard on 32-bit targets but 2e's Tier-1 platforms are all 64-bit). 1 GiB matches `[2e §1.2]`'s magnitude framing (default 10_000 entries per direction × 256 KiB max_frame_bytes worst case = ~5 GiB worst worst; 1 GiB enforces operator-explicit sizing for unusual cases).

**Alternatives considered:**

- *Per-`SessionConfig` override.* Rejected per `[2e §1.2]` — the cap is a deployment policy, not a session tuning knob.
- *Place on `MemoryStore::Config` / `FileStore::Config`.* Rejected — would require every config to carry the same cap; engine-wide single source of truth is cleaner.
- *Default to `SIZE_MAX` (effectively unbounded).* Rejected — the round-2 N9 / C-R2-P1-3 close was specifically to enforce a default storage-DoS guard. Defaulting to unbounded re-opens the hazard.

---

## D-11 — `flush_for_session_close()` dispatch mechanism (concept-shaped non-virtual)

**Decision:** `fixpp::session::detail::has_flush_for_session_close` is a **concept** (`requires { s.flush_for_session_close(); }`); engine dispatches at compile time via the concept, **gated by a factory-type tag retained at session open**. NOT RTTI; NOT `dynamic_cast`; NOT a separate virtual on `MessageStore` (which would push the cap to 5/5 needlessly).

Mechanism:

1. At `Session::open`, the `MessageStoreFactory*` that minted the store carries a `std::type_index` (or equivalent compile-time tag — final shape T-impl) recording the concrete factory type. The session stores it alongside the `std::unique_ptr<MessageStore>`.
2. At `Session::close(graceful)`, the engine's Session-close sequencer dispatches to:
   - `FileStore::flush_for_session_close()` directly when the tag matches `FileStoreFactory`.
   - No-op when the tag matches `MemoryStoreFactory` (or any user-supplied factory whose concrete `MessageStore` does not satisfy the `has_flush_for_session_close` concept).
3. The concept `requires { s.flush_for_session_close(); }` is a **compile-time** check on the concrete `FileStore` type; the engine's switch on the factory tag is a runtime check on the **factory**'s identity, which lifts the concept's compile-time information to runtime through the tag.

The `flush_for_session_close()` method on `FileStore` is **engine-internal**: friended to the engine's Session-close sequencer (a free function or a `Session` private member) so user code cannot accidentally call it. `MemoryStore` does NOT define the method (the concept's `requires` clause fails on it; engine skips the call).

**Rationale:** Opus N3-P2-1 closed the round-3 design-doc surface here. The design constraints:

- `[const §XIV.2]` ≤5 pure-virtual cap forbids adding `flush_for_session_close()` to `MessageStore` as a 5th virtual (already at 4; the cap would go to 5 with `flush_for_session_close()` as the 5th, which is **within** cap but spends the budget on a niche path).
- RTTI / `dynamic_cast` is a banned pattern at the engine internals level (no explicit `[const §XV]` row, but inconsistent with the rest of the engine's compile-time discipline).
- A concept-shaped non-virtual dispatch + factory-type tag gives the engine the per-concrete-impl knowledge it needs without spending the virtual-budget or pulling in RTTI.

The factory-type tag is a small `std::type_index`-sized field on `Session` (or in the `SessionConfig`'s frozen-at-open state); the cost is one word per session.

**Alternatives considered:**

- *Add `flush_for_session_close()` as a 5th pure-virtual on `MessageStore`.* Rejected — within cap but spends the budget; user-supplied impls that don't need the hook would need a no-op override; the concept-shaped path gives the no-op for free.
- *Use `dynamic_cast<FileStore*>(store_ptr)`.* Rejected — RTTI dependency; runtime cost; banned by general engine discipline.
- *Use a public method on `MessageStoreFactory` that returns a function pointer.* Rejected — adds a 2nd virtual to `MessageStoreFactory` (currently 1, `make()`); the factory tag is one word vs the indirection cost of a function pointer + virtual dispatch.

---

## D-12 — Catalogue Status promotion mechanics for S-011 / S-012 / S-013

**Decision:** At this feature's Gate-B merge, **the merger applies** the catalogue Status promotion (`backlog → done`) for rows S-011 / S-012 / S-013 by editing `library/spec/feature-catalogue.md` lines 28–30 with PR/Tests/Verified linkage per `.specify/pipeline.md` step 19. This is **not** auto-applied; a human merger must complete it (recorded failure mode: `feedback_pipeline_mark_done_step` — recurring forgotten "done" marks).

The verbatim binding sub-lines of `.specify/pipeline.md` step 19 (the "MARK DONE — close-out bookkeeping" step), quoted from `.specify/pipeline.md:99-104`:

> *"19. MARK DONE — close-out bookkeeping  MANDATORY; nothing automates these,*
> *                                      each is updated from memory and gets*
> *                                      dropped if not enumerated. Update ALL*
> *                                      that exist for the feature:*
> *    a. feature-catalogue.md row(s) → `done` (+ coverage-index.md if a*
> *       baseline legitimately moved)"*

(`.specify/pipeline.md:99-104`; the full step 19 enumerates 9 close-out surfaces a–i — `.specify/pipeline.md:99-126` — that the merger must update from memory because nothing automates them).

This feature does NOT promote in the spec body; the spec ships the **structural cross-doc edits** (FR-037 / FR-038 / FR-039) but leaves the Status field alone until the merger's step-19 close-out at Gate-B merge.

S-014 stays `backlog` (FSM half is `005`'s).

The step-19 close-out also records the PR / Tests / Verified linkage for S-011 / S-012 / S-013 (PR number, test target names, `/speckit-verify` decision-doc reference).

**Rationale:** Same pattern 007 used for NFR-015 (`feature-catalogue.md:227` was promoted at 007's Gate-B merge per the `[2c App D]` precedent). The user's feedback memory `feedback_pipeline_mark_done_step` explicitly flags this as the canonical close-out path and notes the recurring forgotten-mark hazard; pipeline step 19 is the explicit mitigation — an explicit-handoff at merge time, not a passive-voice promise.

---

## D-13 — Conformance-corpus shape

**Decision:** `tests/conformance/test_store_corpus_replay.cpp` (seam 17) is a **store-side raw-frame round-trip** test:

1. Load a representative recorded FIX session corpus (a small subset of the FIX-TC TC-001..TC-017 fixtures; final corpus selection T-impl, but the round-trip must cover at minimum one FIX 4.2 Logon + one NewOrderSingle + one ExecutionReport + one Heartbeat + one Logout payload).
2. For each frame, `MemoryStore::store(seq, frame_bytes, dir)` (`dir` from the corpus metadata).
3. `retrieve(1, 0, dir, byte_compare_visitor)` walks every persisted frame and asserts byte-identical bytes against the input.
4. Repeat for `FileStore`.

The seam does NOT invoke the FIX FSM (no Logon flow, no ResendRequest emission, no SequenceReset-GapFill semantics); the recorded corpus is consumed as **opaque byte sequences with seqnum + direction metadata**.

**Rationale:** Discharges S-011 / S-012 / S-013 + the storage half of S-014 at the conformance layer (D-5). The FIX-TC cases that require FSM state machines (TC-006 ResendRequest, TC-008 SequenceReset-GapFill, TC-013 Logout sequence) test FSM correctness — those tests land with `005` and consume this feature's `MessageStore` as the persistence substrate.

---

## D-14 — Cross-doc edit tracking

**Three structural edits in this feature's merge bundle:**

| Edit ID | File | Line(s) | FR | Owner |
|---------|------|---------|-----|-------|
| CD-1 | `spec/coverage-index.md` | 76 | FR-037 | 2e |
| CD-2 | `spec/feature-catalogue.md` | 240 (OSS-002), 332 (COM-009) | FR-038 | 2e |
| CD-3 | `.specify/architecture.md` | 598 | FR-039 | 2e |

**Merger-applied at Gate-B merge per `.specify/pipeline.md` step 19** (explicit human handoff — not auto-applied; recorded failure mode `feedback_pipeline_mark_done_step`):

| Edit ID | File | Line(s) | Action | Applied by |
|---------|------|---------|--------|------------|
| M-1 | `spec/feature-catalogue.md` | 28 (S-011), 29 (S-012), 30 (S-013) | Status `backlog → done` + PR / Tests / Verified linkage | merger (step 19) |

**Post-merge SHA tracking:** the merger's step-19 close-out also records the submodule SHA at merge and updates the parent repo's submodule pointer; the catalogue `Verified` column gets the `.specify/decisions/008-message-store-verify.md` short reference.

---

## D-15 — `static_assert` Path B guard on construction

**Decision:** Seam 11 (`test_quickfix_compat_path_b_guard.cpp`) asserts `static_assert(!std::is_constructible_v<fixpp::session::MessageStore, FakeQuickFixStore*>)` at compile time, where `FakeQuickFixStore` is a test-only struct with a synchronous `set(seq, body) -> void` / `get(begin, end, vec) -> void` shape (mirroring `quickfix::MessageStore`).

The `FakeQuickFixStore` is a **test-only type** in the test file's anonymous namespace; it is NOT a header-shipped helper (which would be a soft endorsement that the type makes sense as a public construction target).

**Rationale:** FR-032 / `[2e §9 seam 11]`. The guard verifies that no implicit construction path exists from a sync-shaped object into the async interface. The `static_assert` is the cheapest possible enforcement (compile-time; no runtime code).

**Alternatives considered:**

- *Use `concepts` to express "is not a QuickFIX-shaped sync store".* Rejected — defining the negative concept exposes the sync-shaped surface as a named API element; the `static_assert` on `is_constructible_v` is more direct.

---

## Best-practices references (consulted)

- **ASIO file I/O cancellation under io_uring.** `[asio 1.36.0]` release notes + asio-users discussion 2025-09-* confirm `posix::stream_descriptor`'s cancellation semantics are stable; the newer `random_access_file` is best-effort cancellable but `fdatasync` is not cancellable mid-syscall (the kernel runs it to completion). The 2e contract per `[2e §6.1.4]` (cancellation before `fdatasync` returns success → `store_cancelled`; after → durable) is consistent with this — the asio surface gives us "interrupted before kernel call entered" semantics, the kernel gives us atomic-from-our-perspective `fdatasync`. T-impl can use either descriptor flavour; the contract is the same.
- **POSIX `rename(2)` durability.** The Linux man page `man 2 rename` explicitly notes that `rename` is atomic but **not durable** without a parent-directory `fsync` after the syscall returns success. Round-3 C-R3-P1-2 pinned this as MANDATORY. The Windows equivalent is `MoveFileExW` with `MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH` — round-3 noted `MOVEFILE_WRITE_THROUGH` is required because NTFS metadata journaling alone does not durably commit the rename without the user-mode write-through flag.
- **`fdatasync` vs `fsync`.** Both flush the file's data; `fdatasync` skips metadata that isn't required for restart (mtime, atime). On the live log we use `fdatasync` (the file size IS metadata that's required for restart; `fdatasync` per the Linux man page does flush file-size changes). For the parent directory after `reset()`'s `rename`, `fsync` is required (the parent dir is a directory; `fdatasync` semantics on directories are not portable).
- **`MOVEFILE_WRITE_THROUGH` on Windows.** `MoveFileExW`'s `MOVEFILE_WRITE_THROUGH` flag forces the file system to flush its buffers before returning. Per the Windows API docs and round-3 C-R3-P1-2 pin: NTFS metadata journaling alone does not durably commit the rename; the flag is required.
- **CRC32 (Castagnoli polynomial 0x1EDC6F41) vs CRC32 (IEEE 0x04C11DB7).** `crc32c` provides Castagnoli (0x1EDC6F41) which is what SSE4.2 `crc32` instructions compute (Intel's hardware accelerates the Castagnoli variant). The on-disk header field is just "a 32-bit value computed by the impl's chosen polynomial"; we pin Castagnoli for hardware-accelerated speed. The choice is recorded in `[2e §6.3.1]` and `FileStore::Config` documents it.
- **`flock` vs `fcntl` advisory locks on Linux.** `flock` is per-file-descriptor and survives `fork`; `fcntl(F_SETLK)` is per-process. `[2e §6.3.5]` picks `flock` because the lock is logically attached to the FileStore instance (the open file); `fcntl` would unlock on any descriptor close, which is wrong for our model where the FileStore holds the only FD.
- **`LockFileEx` on Windows.** Equivalent to `flock` semantics; tied to the `HANDLE`, released on `CloseHandle`. `LOCKFILE_EXCLUSIVE_LOCK` flag is required for the FR-013 advisory exclusive lock.

---

## Integration patterns (consulted)

- **006-async-mutex `async_mutex::lock_async()` cancellation surface.** Per `[2f §4.5]` `sync_lock_aborted` is the variant when cancellation wins the CAS-arbitration race; the 2e mutex contract per FR-015 / FR-016 / FR-020 / `[2e §6.1.4]` composes: if the writer-mutex `lock_async()` is cancelled before the method's linearisation point, the awaitable returns `store_cancelled` (mapped from the layered `sync_lock_aborted` at the method-level surface). The PR #73 contract: the cancellation slot fires through both layers atomically.
- **007 `cancellable_dispatch`.** Per `[2d §6.5]` returns `asio::awaitable<expected_t<void>>` with the deterministic three-case contract (dispatched + ran; cancelled before dispatch; OOM at node alloc). 2e uses it on every `file_io_executor → session_strand` completion handoff (`FileStore::store`, `FileStore::reset`, `FileStore::flush_for_session_close`). The node is allocated from `session_arena` per `[2d §6.5]`.
- **007 `session_executor`.** Per `[2d §4.8]`, the engine binds all MessageStore method invocations to the session strand via `session_executor`; this feature consumes the type unchanged. The `session_ptr()` member is used to look up the per-session trace context (`co_await fixpp::current_trace_context`) in `[const §XIII.3]`-compliant code paths.
- **007 `Session::session_arena()`.** Per `[2d §4.5]` / `[2f Appendix D §D.1]` the engine-internal accessor returns the per-session PMR resource. `MemoryStore` MAY use it for the per-session slab; `FileStore` uses it for the `cancellable_dispatch` node + the `pwrite` buffer copy.
- **001 `core::error` / `expected_t<T>`.** The hot-path `expected_t<T>` model from `[arch §5.3]`; this feature appends 10 variants.
- **004 `wire::Writer::commit`.** Per `[2b §4.5]`, the post-commit span is what `MessageStore::store` consumes for outbound frames (FR-019); the pre-commit span carries the placeholder `BodyLength` digits and would persist wrong bytes. Seam 7 pins the ordering compile-time + runtime.

---

**Output:** This research.md resolves every NEEDS-CLARIFICATION. Phase 1 proceeds with data-model.md, contracts/, quickstart.md.
