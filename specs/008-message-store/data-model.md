# Data Model — 008-message-store (Phase 1)

**Branch:** `008-message-store` | **Date:** 2026-05-20 | **Plan:** [plan.md](plan.md) | **Research:** [research.md](research.md)
**Design anchor:** `.specify/2e-msgstore.md` v0.4. On conflict the design doc wins.

Entities E1..E12, invariants I-01..I-22, error slots 56–65 with C-ABI coalescing groups. The contracts/ headers are the type-level oracle; this document is the **behavioural** oracle.

---

## Entities

### E1 — `fixpp::session::MessageStore` (interface)

**Header:** `include/fixpp/session/message_store.hpp`
**Catalogue:** S-011 (NEW done at this feature's Gate-B merge)

```cpp
class MessageStore {
public:
    MessageStore()                               = default;
    MessageStore(const MessageStore&)            = delete;
    MessageStore& operator=(const MessageStore&) = delete;
    MessageStore(MessageStore&&)                 = delete;
    MessageStore& operator=(MessageStore&&)      = delete;
    virtual ~MessageStore()                      = default;

    [[nodiscard]] virtual asio::awaitable<fixpp::core::expected_t<void>>
    store(seqnum_t seq,
          std::span<const std::byte> frame [[clang::lifetimebound]],
          direction_t dir) noexcept = 0;

    [[nodiscard]] virtual asio::awaitable<fixpp::core::expected_t<void>>
    retrieve(seqnum_t begin,
             seqnum_t end,
             direction_t dir,
             retrieve_visitor& visitor [[clang::lifetimebound]]) noexcept = 0;

    [[nodiscard]] virtual asio::awaitable<fixpp::core::expected_t<seqnum_t>>
    next_seqnum(direction_t dir, bool increment) noexcept = 0;

    [[nodiscard]] virtual asio::awaitable<fixpp::core::expected_t<void>>
    reset() noexcept = 0;
};
```

**Pure-virtual count:** 4 / 5 (`[const §XIV.2]` cap, within budget — no justification paragraph required).
**No public `flush()`** (N2). Engine-internal `FileStore::flush_for_session_close()` is dispatched via the `has_flush_for_session_close` concept (E11), NOT a virtual on this interface.
**Deleted move/copy + virtual destructor** — polymorphic owned-by-`unique_ptr` bind target; no slicing risk.

### E2 — `fixpp::session::retrieve_visitor`

**Header:** `include/fixpp/session/retrieve_visitor.hpp`

```cpp
enum class visit_result : std::uint8_t { cont = 0, stop = 1, abort = 2 };

class retrieve_visitor {
public:
    retrieve_visitor()                                   = default;
    retrieve_visitor(const retrieve_visitor&)            = delete;
    retrieve_visitor& operator=(const retrieve_visitor&) = delete;
    retrieve_visitor(retrieve_visitor&&)                 = delete;
    retrieve_visitor& operator=(retrieve_visitor&&)      = delete;
    virtual ~retrieve_visitor()                          = default;

    [[nodiscard]] virtual asio::awaitable<fixpp::core::expected_t<visit_result>>
    on_frame(seqnum_t seq,
             std::span<const std::byte> frame [[clang::lifetimebound]]) noexcept = 0;

    [[nodiscard]] virtual fixpp::core::error abort_error() const noexcept {
        return fixpp::core::error::store_visitor_aborted;
    }
};
```

**Pure-virtual count:** 1 (`on_frame`); 1 overridable virtual hook with default (`abort_error()` — does NOT count against `MessageStore`'s plugin cap; lives on the visitor, not the store).
**The visitor's span is stable across the visitor's `co_await`** (I-04); the visitor MUST NOT retain it past the awaitable's completion.

### E3 — `fixpp::session::MemoryStore`

**Header:** `include/fixpp/session/memory_store.hpp` (+ `src/session/memory_store.cpp` if any out-of-line; mostly header-inline)
**Catalogue:** S-012 (NEW done at this feature's Gate-B merge)

```cpp
enum class capacity_policy : std::uint8_t { bounded = 0, unbounded = 1 };
// [[clang::enum_extensibility(closed)]] where supported; static_assert at every switch.
// evict_oldest is UNREPRESENTABLE per [const §XV.15] — not a public name, not a numeric value.

class MemoryStore final : public MessageStore {
public:
    struct Config {
        capacity_policy policy             = capacity_policy::bounded;
        std::size_t     inbound_capacity   = 10'000;
        std::size_t     outbound_capacity  = 10'000;
        std::size_t     max_frame_bytes    = 256 * 1024;  // 256 KiB default per design-doc §4.2 line 448
        std::pmr::memory_resource* store_resource = nullptr;  // null → engine provides dedicated monotonic_buffer_resource
    };

    explicit MemoryStore(Config cfg) noexcept;

    // store/retrieve/next_seqnum/reset overrides — see contracts/memory_store.hpp.
};
```

**One-PMR-allocation-at-construction** layout (fixed slot array + fixed slab) per FR-007 / `[2e §4.2]`. `store()` performs **zero allocator calls** under `bounded` policy after construction (seam 15 verifies via tracking-PMR counter).

### E4 — `fixpp::session::FileStore`

**Header:** `include/fixpp/session/file_store.hpp` + `src/session/file_store.cpp`
**Catalogue:** S-013 (NEW done at this feature's Gate-B merge)

```cpp
// FileStorePolicy — struct (NOT std::variant) per design-doc §4.3 line 507–537.
struct FileStorePolicy {
    enum class kind : std::uint8_t {
        commit_per_message = 0,
        commit_batched     = 1,
        commit_interval    = 2,
    };
    kind                       which       = kind::commit_per_message;
    std::size_t                batch_size  = 1;                                  // commit_batched only.
    std::chrono::milliseconds  interval    = std::chrono::milliseconds{100};     // commit_interval only.
};

class FileStore final : public MessageStore {
public:
    struct Config {
        std::filesystem::path     directory;
        std::string               sender_comp_id;
        std::string               target_comp_id;
        FileStorePolicy           policy            = {};
        std::size_t               max_frame_bytes   = 256 * 1024;
        asio::any_io_executor     file_io_executor;
        std::pmr::memory_resource* store_resource   = nullptr;
    };

    explicit FileStore(Config c) noexcept;
    ~FileStore() override;  // flushes / closes the log; idempotent

    // store/retrieve/next_seqnum/reset overrides — see contracts/file_store.hpp.

    // Engine-internal, non-virtual, non-public hook dispatched by the engine's
    // Session-close sequencer via has_flush_for_session_close concept (E11).
    // NOT in the contracts/ shape-oracle (engine-internal).
private:
    friend ... ;  // engine's close sequencer — exact friend declaration T-impl
    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>>
    flush_for_session_close() noexcept;
};
```

**On-disk layout** per `[2e §6.3.1]`:

- Single append-only log file per session: `<directory>/<sender>__<target>.log`.
- File starts with a 16-byte aligned **sentinel record** `[record_kind=sentinel(2) | dir=0 | reserved(2) | magic(4) | version(4) | session_triple_hash(...) | crc32(4)]` (exact byte layout T-impl, must align to 8-byte boundary).
- Each subsequent record: `[record_kind | dir | reserved(2) | seq(4) | len(4) | crc32(4) | bytes(len) | padding-to-8-byte-align]` (16-byte header + payload + padding).
- `record_kind ∈ { frame=0, counters=1, sentinel=2 }`.
- CRC32 covers `record_kind + dir + reserved + seq + len + bytes` (Castagnoli polynomial 0x1EDC6F41 for hardware acceleration per research D-3).
- Counters record carries `next_inbound : uint32_t | next_outbound : uint32_t` (the `seqnum_t` placeholder width; tied to the placeholder type per E9).

**Restart algorithm** (FR-012): on open, scan from offset 0, verify sentinel, verify every record's CRC32; on first bad CRC, `ftruncate` (Linux) / `SetEndOfFile` (Windows) to the bad record's start offset, then `fdatasync` (Linux) / `FlushFileBuffers` (Windows) the truncation, rebuild the in-memory index from the surviving frame records.

**Atomic-rename `reset()`** (FR-010, `[2e §6.3.4]`): write a fresh `<live>.log.reset.tmp` containing sentinel + counters reset to `next_inbound = next_outbound = 1`; `fdatasync` the tmp; `rename(tmp, live)`; parent-dir `fsync` (Linux MANDATORY) / `MoveFileExW(..., MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)` (Windows MANDATORY). The success-return cannot happen until the platform durability primitive returns success (I-15).

**Advisory open lock** (FR-013): `flock(LOCK_EX | LOCK_NB)` (Linux) / `LockFileEx(LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY)` (Windows) at open; second opener gets `store_factory_failed`.

**`file_io_executor` (Config field, caller-supplied)** (FR-024, research D-7): caller passes `asio::any_io_executor` (typical: an `EngineConfig`-exposed 4-thread `asio::thread_pool` shared across all FileStores per design-doc §4.3.2 line 669); all `pwrite` / `fdatasync` / `rename` work posted there; completions rebind to the session strand via `cancellable_dispatch` (`[2d §6.5]`).

### E5 — `fixpp::session::MessageStoreFactory`

**Header:** `include/fixpp/session/message_store_factory.hpp` (extended in place; existing 007 stub is the base)

```cpp
class MessageStoreFactory {
public:
    MessageStoreFactory()                                      = default;
    MessageStoreFactory(const MessageStoreFactory&)            = delete;
    MessageStoreFactory& operator=(const MessageStoreFactory&) = delete;
    MessageStoreFactory(MessageStoreFactory&&)                 = delete;
    MessageStoreFactory& operator=(MessageStoreFactory&&)      = delete;
    virtual ~MessageStoreFactory()                             = default;

    [[nodiscard]] virtual fixpp::core::expected_t<std::unique_ptr<MessageStore>>
    make(std::string_view sender,
         std::string_view target,
         std::pmr::memory_resource* mr,
         std::size_t max_store_memory_bytes,
         asio::any_io_executor file_io_executor) noexcept = 0;
};
```

**This is an in-place extension** of the 007-shipped polymorphic bind target. The class identity (`fixpp::session::MessageStoreFactory`) is preserved; the deleted move/copy + virtual destructor remain; we **add** the `make(...)` pure-virtual (N1; `unique_ptr` ownership per `[2e §4.4]` / `[arch §5.6]`). The 4th `max_store_memory_bytes` parameter is the engine-resolved cap value threaded in at call time so the factory CTOR stays Config-only per the design-doc §4.4 frozen surface — no `EngineConfig&` back-channel on the constructor. The 5th `file_io_executor` parameter is the engine-resolved `EngineConfig::file_io_executor` value threaded in by the engine at call time (FR-024 / I-13 / research D-7); `FileStoreFactory::make()` populates the minted `FileStore::Config::file_io_executor` with this value (preserving the `[2e §4.3.2]:665` required-at-construction contract on `FileStore` itself, since `FileStore` is constructed inside `make()`), with a Config-supplied executor winning if the factory's own Config already carried one (caller override). `MemoryStoreFactory::make()` ignores the parameter.

### E6 — `fixpp::session::MemoryStoreFactory`

**Header:** `include/fixpp/session/memory_store_factory.hpp` (+ optional `src/session/memory_store_factory.cpp`)

```cpp
class MemoryStoreFactory final : public MessageStoreFactory {
public:
    explicit MemoryStoreFactory(MemoryStore::Config cfg = {}) noexcept;

    [[nodiscard]] fixpp::core::expected_t<std::unique_ptr<MessageStore>>
    make(std::string_view sender,
         std::string_view target,
         std::pmr::memory_resource* mr,
         std::size_t max_store_memory_bytes,
         asio::any_io_executor file_io_executor) noexcept override;
};
```

**Storage-DoS guard** (FR-014 / SC-004 / I-11): `make()` enforces overflow-safe checked arithmetic against `max_store_memory_bytes` (the engine-resolved `EngineConfig::max_store_memory_per_session` threaded in by the engine at call time). See I-11 for the exact rule. The 5th `file_io_executor` parameter is ignored on this path (MemoryStore has no file-I/O work); both empty/default-constructed and non-empty values are **accepted and silently discarded** (no-op, NOT a misuse — `MemoryStore` has no file-I/O work, so any executor value is contractually meaningless on this path; the engine threads the same `EngineConfig::file_io_executor` value into every factory's `make()` per FR-005, and the MemoryStore path discards it rather than branching on emptiness).

### E7 — `fixpp::session::FileStoreFactory`

**Header:** `include/fixpp/session/file_store_factory.hpp` (+ optional `src/session/file_store_factory.cpp`)

```cpp
class FileStoreFactory final : public MessageStoreFactory {
public:
    explicit FileStoreFactory(FileStore::Config cfg) noexcept;

    [[nodiscard]] fixpp::core::expected_t<std::unique_ptr<MessageStore>>
    make(std::string_view sender,
         std::string_view target,
         std::pmr::memory_resource* mr,
         std::size_t max_store_memory_bytes,
         asio::any_io_executor file_io_executor) noexcept override;
};
```

**Same storage-DoS guard surface** as E6, plus opens the live log, takes the advisory lock, runs the restart algorithm. Resolves the `file_io_executor` per the Config-supplied-wins rule (FR-024 / I-13 / research D-7): if the factory's stored `Config.file_io_executor` is non-empty it is used; otherwise the engine-threaded 5th-parameter `file_io_executor` (sourced from `EngineConfig::file_io_executor` per design-doc §4.3.2:669) populates the minted `FileStore::Config`. If both are empty, returns `store_factory_failed`. The minted `FileStore`'s `Config::file_io_executor` is thus always non-empty at `FileStore` construction time, preserving `[2e §4.3.2]:665`.

### E8 — `fixpp::session::direction_t`

**Header:** `include/fixpp/session/direction.hpp`

```cpp
enum class direction_t : std::uint8_t { inbound = 0, outbound = 1 };
```

Frozen for v1.0 per FR-002; values reserved per `[const §X.4]`.

### E9 — `fixpp::session::seqnum_t` (placeholder)

**Header:** `include/fixpp/session/seqnum.hpp` (**authored fresh**, research D-1)

```cpp
namespace fixpp::session {
using seqnum_t = std::uint32_t;
inline constexpr seqnum_t seqnum_min = 1;
inline constexpr seqnum_t seqnum_max = std::numeric_limits<seqnum_t>::max();
// Cross-doc handoff: the canonical seqnum_t type is owned by the deferred
// Phase-4 session-module spec per [2e §3.1] / [2e §4.7] / [2e §10 Q9]. When
// that spec lands, this header is either re-exported from there or deleted
// with includes repointed (single-line edit per [const §VI.5]).
}
```

### E10 — `FileStorePolicy` (struct with `kind` enum)

**Header:** `include/fixpp/session/file_store.hpp` (declared alongside `FileStore::Config`)

```cpp
struct FileStorePolicy {
    enum class kind : std::uint8_t {
        commit_per_message = 0,
        commit_batched     = 1,
        commit_interval    = 2,
    };
    kind                       which       = kind::commit_per_message;
    std::size_t                batch_size  = 1;                                  // commit_batched only.
    std::chrono::milliseconds  interval    = std::chrono::milliseconds{100};     // commit_interval only.
};
```

Per-policy data-loss window documented in the block-comment per FR-011 (design-doc §4.3 lines 521–526):

- `commit_per_message`: 0% loss (fdatasync per record).
- `commit_batched(N)`: up to N-1 records may be lost since last batch boundary.
- `commit_interval(ms)`: ms-bounded loss window since last timer-fired flush.

### E11 — `fixpp::session::detail::has_flush_for_session_close` (concept)

**Header:** `include/fixpp/session/detail/has_flush_for_session_close.hpp`

```cpp
namespace fixpp::session::detail {
template <class S>
concept has_flush_for_session_close = requires(S& s) {
    { s.flush_for_session_close() } -> std::same_as<
        asio::awaitable<fixpp::core::expected_t<void>>>;
};
}  // namespace fixpp::session::detail
```

**Engine-internal**: callable only from the engine's Session-close sequencer (friended). Concept-shaped non-virtual dispatch gated by a factory-type tag retained at session open (Opus N3-P2-1; research D-11). **NOT** RTTI / `dynamic_cast`. **NOT** a virtual on `MessageStore` (would push the cap from 4/5 to 5/5 without justification).

`FileStore` defines `flush_for_session_close()`; `MemoryStore` does not (the concept's `requires` clause fails on it; engine skips the call); user-supplied impls similarly inherit the no-op default for free.

### E12 — `fixpp::session::quickfix_compat::cfg_loader`

**Header:** `include/fixpp/session/quickfix_compat/cfg_loader.hpp` + `src/session/quickfix_compat/cfg_loader.cpp`

```cpp
namespace fixpp::session::quickfix_compat {

[[nodiscard]] fixpp::core::expected_t<std::unique_ptr<FileStoreFactory>>
cfg_to_file_store_factory(const std::filesystem::path& cfg_path) noexcept;

}  // namespace fixpp::session::quickfix_compat
```

**Config translation only** — reads the `[DEFAULT]` / `[SESSION]` block from a QuickFIX `.cfg`, extracts `FileStorePath`, and emits an equivalent `FileStoreFactory`. **No runtime adapter** (Path A retired in v0.3 per Codex C-R2-P2-1; `[2e §4.8.B]`).

---

## Invariants

The invariants are the **behavioural** contract; the contracts/ headers are the **type-level** contract.

| ID | Statement | Owner | Enforcement |
|----|-----------|-------|-------------|
| I-01 | All four `MessageStore` methods acquire the per-instance `fixpp::sync::async_mutex` writer mutex on entry. The mutex is `async_mutex` regardless of `SessionConfig::lock_policy` (`[const §XI.5]`). | E1 / E3 / E4 | Static (grep gate `[const §XV.9]`) + dynamic (seam 5 FIFO-fair concurrent-writer under TSan; seam 20 verifies mutex acquired-then-released around `store_seqnum_out_of_order` reject). |
| I-02 | `store` deep-copies the `frame` span into store-owned storage **after acquiring the writer mutex and before any further suspension that could release the session strand** (i.e., before `pwrite` / `fdatasync` posts to `file_io_executor`) per `[2b §6.4]` view-escape and design-doc §6.3.3 step 3 (under the v1.0 single-session-serialisation-domain discipline the uncontended `async_mutex::async_lock()` does NOT suspend per `[2f §4.3.2]` fast-path). | E3 / E4 | Seam 9 (awaitable visitor + span lifetime) for the symmetric retrieve-side; seam 7 (outbound store-after-commit byte-equality) for the input-side. |
| I-03 | `retrieve` acquires the writer mutex at entry to validate `begin`/`end` and snapshot the index, but **releases the mutex before** the visitor's `co_await`. Mid-traversal mutation is detected; the next visitor call observes the new state without UB; already-visited frames are not re-visited; iteration stops at the original `end`. | E1 / E3 / E4 | Seam 9 + a TSan rendezvous in the same seam (concurrent `store()` during a `retrieve` walk completes without race; visitor sees stable per-frame span). |
| I-04 | `retrieve_visitor::on_frame`'s `frame` span is stable across the visitor's `co_await`. The visitor MUST NOT retain it past the awaitable's completion (compile-time `[[clang::lifetimebound]]`). | E2 | Seam 9 (`[[asan]]`-instrumented access to the span returns the right bytes after a 100-µs visitor `co_await`). |
| I-05 | `store` verifies `seq == next_seqnum(dir, false)` inside the writer-mutex critical section **after mutex acquire and before any slab memcpy / pwrite**. On mismatch returns `expected_t::unexpected{store_seqnum_out_of_order}`, no state mutation, mutex released cleanly. | E3 / E4 | Seam 20 (drive `store(seq=5)` while `next_seqnum(outbound, false) == 1`). |
| I-06 | `next_seqnum(dir, true)` increments inside the mutex (Opus N2-P2-2 — atomic-fetch-add wording retired in v0.2). | E3 / E4 | Seam 6 (cancellation contract per method; cancellation before increment → `store_cancelled`, no advance; after → durable). |
| I-07 | Cancellation result contract per method per `[2e §6.1.4]`: cancellation before linearisation → `expected_t::unexpected{store_cancelled}`, no state change; cancellation after → normal completion with the operation's value, state durable. | E3 / E4 | Seam 6 (per-method cancellation; FileStore's variant tests cancellation before/after `fdatasync` returns success per I-13). |
| I-08 | `capacity_policy::bounded` MemoryStore returns `store_capacity_exhausted` on the next `store()` after the per-direction cap is reached. No silent eviction. No termination. The entry-array index is unchanged. The writer mutex is released cleanly. | E3 | Seam 4 (101st store on a `outbound_capacity = 100` instance returns the variant). |
| I-09 | `capacity_policy::evict_oldest` is **unrepresentable** on the public API — not a public name, not a numeric value (`[const §XV.15]`). Closed 2-value enum + `[[clang::enum_extensibility(closed)]]` + `static_assert` at every switch + runtime out-of-range-cast reject. | E3 | Seam 4 variant (`unbounded`: 10⁵ frames stored without capacity error). Static enforcement via compile-time `static_assert` on enum values + clang-tidy. |
| I-10 | `MemoryStore::store` performs **zero allocator calls** under `bounded` policy after construction (FR-007). One PMR allocation at construction for slot+slab combined. | E3 | Seam 15 (tracking PMR resource counter unchanged after 10⁴ `store()` calls). |
| I-11 | `make()` MUST reject the Config via `store_factory_failed` if any of the following hold (overflow-safe checked arithmetic; the engine-resolved `EngineConfig::max_store_memory_per_session` is threaded in as `make()`'s 4th parameter `max_store_memory_bytes`): (a) `inbound_capacity + outbound_capacity` overflows `std::size_t`; (b) `(inbound_capacity + outbound_capacity) > 0` and `max_frame_bytes > max_store_memory_bytes / (inbound_capacity + outbound_capacity)`; (c) `max_frame_bytes` exceeds `Framer::Config::max_frame_bytes`. The same rule binds both factories (FR-014 / `[2e §1.2]`). Additionally, `FileStoreFactory::make()` MUST reject with `store_factory_failed` if BOTH the factory's stored `Config.file_io_executor` AND `make()`'s 5th-parameter `file_io_executor` are empty / default-constructed (executor injection per FR-024 / I-13 / research D-7 — there is no "no executor" operating mode for `FileStore`). | E6 / E7 | Seam 4 — primary test plus an overflow sub-scenario `inbound_capacity = SIZE_MAX/2 - 1, outbound_capacity = 2, max_frame_bytes = 8` expecting `store_factory_failed`. |
| I-12 | `FileStore` single append-only log per session at `<dir>/<sender>__<target>.log`; every record carries `kind | dir | seq | len | crc32 | bytes`; the file starts with a sentinel record `magic | version | session_triple_hash | crc32`. | E4 | Seam 1 + seam 2 (round-trip + crash-survival). |
| I-13 | `FileStore::store(commit_per_message)` linearisation point = successful return of `fdatasync` (Linux) / `FlushFileBuffers` (Windows). Cancellation before the syscall returns success → `store_cancelled`; cancellation after → durable, normal completion. | E4 | Seam 6 (FileStore variant) + seam 2 (crash-survival under `commit_per_message` shows 0% loss). |
| I-14 | `FileStore` open detects torn writes via per-record CRC32 on the restart scan and truncates the log to the last whole record. Stale `<live>.log.reset.tmp` from a crashed prior reset is unlinked before the scan. | E4 | Seam 3 (Linux Tier-1 + Windows Tier-2). |
| I-15 | `FileStore::reset()` is atomic at the `rename` of `<live>.log.reset.tmp` over `<live>.log` PLUS the platform durability primitive (Linux: parent-dir `fsync` MANDATORY; Windows: `MOVEFILE_WRITE_THROUGH` MANDATORY). Success-return cannot happen until the durability primitive has returned success (FR-010 / SC-003). The `.reset.tmp` lives in the same directory as the live log (no cross-filesystem `EXDEV`). | E4 | Seam 10 (atomic-rename + crash cuts at 3 boundaries). |
| I-16 | `FileStoreFactory::make()` takes an `flock` (Linux) / `LockFileEx` (Windows) advisory exclusive lock on the live log; second opener returns `store_factory_failed`. | E7 | Edge case test in seam 2 / seam 10 (second-opener variant). |
| I-17 | `flush_for_session_close()` is engine-internal, dispatched via the `has_flush_for_session_close` concept gated by a factory-type tag retained at session open. NOT RTTI / `dynamic_cast`. Runs to completion outside phase-1's child timeout under `Session::close(graceful)`. NOT invoked under `Session::close(terminal)` per Appendix D §D.2. Returns `expected_t<void>{}` on success or `store_io_failure` on mid-flush error; does NOT surface `store_cancelled` under graceful close. | E4 / E11 | Seam 19 (graceful-close drain under `commit_batched(N=64)` with 32 frames pending → all 32 frames durable; terminal-close variant → `flush_for_session_close()` NOT invoked). |
| I-18 | `store_seqnum_overflow` is session-fatal: once observed, the session cannot send further outbound messages until `reset()` is called. The store does NOT autonomously reset. The FSM surfaces this to user code (typically via `onLogout`-with-reason or a session-level error callback). | E3 / E4 | Seam 13 (latency regression incidentally exercises the `next_seqnum(outbound, true)` increment path; a dedicated overflow test ships with `005`). |
| I-19 | `retrieve(begin=0, …)` returns `store_seqnum_invalid` before any visitor invocation (FIX wire seqnums start at 1 per `[FIX-SL §4.1]`); `retrieve(begin, end, …)` with `end != 0 && end < begin` returns `store_invalid_range` before any visitor invocation; `retrieve` over a never-persisted gap returns `store_seqnum_gap` (unless the gap is at the trailing edge of `end == 0`). | E3 / E4 | Seam 8 (replay over arbitrary `[begin, end]` including gaps + invalid input). |
| I-20 | `retrieve_visitor::on_frame` returning `visit_result::abort` surfaces the visitor's `abort_error()` virtual return through `retrieve()`'s `expected_t<void>` (default impl returns `store_visitor_aborted`). | E1 / E2 | Seam 9 (visitor `co_return abort` after frame 3 → awaitable returns `store_visitor_aborted`). |
| I-21 | PMR poison on the recovery path: if the visitor's caller-supplied `memory_resource` throws on allocation, the throw routes through `fixpp::core::detail::trap_throw` per `[2a §4.2]` (no terminate; `[arch §5.3]` `expected_t<T>`) and surfaces as `expected_t::unexpected{store_visitor_aborted}`. | E1 / E2 | Seam 16 (`tests/session/test_store_pmr_poison_retrieve.cpp`). |
| I-22 | `Session::close(terminal)` with in-flight `store()` calls: 100 in-flight outstanding calls all complete (with `store_cancelled` for those whose linearisation point was not reached); no UAF on `session_arena`; `~MessageStore` runs before `session_arena` release. | E1 / E3 / E4 | Seam 18 (`tests/session/test_store_shutdown_ordering.cpp`, under TSan + ASan). |

---

## Error mapping (slot allocation)

The 10 new variants append to `fixpp::core::error` at unused slots **56–65**, non-renumbering per `[const §X.4]`. Design-doc table order (`[2e §6.7]`). Slot range continuous with 007's 47–55.

| Slot | Variant | Hot-path entry | C-ABI group (for `2i`) |
|------|---------|----------------|------------------------|
| 56 | `store_io_failure` | FileStore I/O fault (disk full, hardware error, ENOSPC, EACCES, mid-flush error from `flush_for_session_close()`) | `FIXPP_ERR_STORE_RUNTIME` |
| 57 | `store_seqnum_gap` | `retrieve` over a never-persisted gap (unless at trailing edge of `end == 0`) | `FIXPP_ERR_STORE_CONSISTENCY` |
| 58 | `store_seqnum_out_of_order` | `store(seq, ...)` with `seq != next_seqnum(dir, false)` (I-05) | `FIXPP_ERR_STORE_CONSISTENCY` |
| 59 | `store_capacity_exhausted` | `MemoryStore::store` under `bounded` policy at cap (I-08) | `FIXPP_ERR_STORE_RUNTIME` |
| 60 | `store_seqnum_overflow` | `next_seqnum(dir, true)` when current == `seqnum_max` (session-fatal; I-18) | `FIXPP_ERR_STORE_RUNTIME` |
| 61 | `store_factory_failed` | `MessageStoreFactory::make()` validation failure (storage-DoS, sentinel mismatch, advisory lock taken, OOM at config validation) | `FIXPP_ERR_STORE_CONFIG` |
| 62 | `store_visitor_aborted` | `retrieve_visitor::on_frame` returned `visit_result::abort` (default `abort_error()`); PMR poison routed via `trap_throw` (I-21) | `FIXPP_ERR_STORE_VISITOR` |
| 63 | `store_seqnum_invalid` | `retrieve(begin=0, …)` (I-19) | `FIXPP_ERR_STORE_CONSISTENCY` |
| 64 | `store_invalid_range` | `retrieve(begin, end, …)` with `end != 0 && end < begin` (I-19) | `FIXPP_ERR_STORE_CONSISTENCY` |
| 65 | `store_cancelled` | Cancellation winning before a method's linearisation point (I-07) | `FIXPP_ERR_CANCELLED` (reused; joins 007's `dispatch_aborted` / `clock_sleeps_cancelled`) |

**NOT introduced** (recorded for `2i` + future readers):

- `store_concurrent_writer` — REMOVED in v0.2 per Codex P1-5; FIFO-fair `async_mutex` makes the variant impossible (I-01).
- `store_shim_timeout` — REMOVED in v0.3 per Codex C-R2-P2-1 escalation (`[2e §4.8.B]` Path A retired; no runtime adapter).

The C-ABI prefix-group mapping is **documented** in the `error.hpp` comment for `2i`'s consumption (research D-6); no `extern "C"` symbol added in this feature.

---

## Engine-config delta

**File:** `include/fixpp/core/engine_config.hpp`

**Edit (FR-014a):** add `std::size_t max_store_memory_per_session = 1ULL << 30;` (1 GiB default) adjacent to the existing `default_store_factory` field (currently line 119 in the 007 baseline). Clarifications Q5 / research D-10.

**Edit (FR-024a):** add `asio::any_io_executor file_io_executor;` (default-constructed empty) adjacent to the existing `default_store_factory` field per design-doc §4.3.2:669 ("`EngineConfig` exposes a default `file_io_executor`"). The engine threads this value into each `MessageStoreFactory::make()` invocation as the 5th `file_io_executor` parameter (FR-005 / FR-024 / research D-7). Typical operating mode: caller constructs a 4-thread `asio::thread_pool` at engine-open and assigns its executor here (shared across all `FileStore`s in the engine per design-doc §4.3.2:669). The `FileStoreFactory::make()` path rejects with `store_factory_failed` if both the factory's `Config.file_io_executor` AND the engine-threaded value are empty (I-11).

**Non-breaking** — default values preserve prior `EngineConfig` semantics. A session whose store does not exceed 1 GiB is unaffected by `max_store_memory_per_session`; a session that uses `MemoryStore` (only) is unaffected by an empty `file_io_executor` (MemoryStore ignores it). Both fields are engine-wide (no `SessionConfig` override).

---

## Coverage discipline (for `/speckit-verify`)

`linux-clang-coverage` measures these touch surfaces:

- `include/fixpp/session/{message_store,retrieve_visitor,memory_store,file_store,memory_store_factory,file_store_factory,direction,seqnum}.hpp`
- `include/fixpp/session/detail/has_flush_for_session_close.hpp`
- `include/fixpp/session/quickfix_compat/cfg_loader.hpp`
- `src/session/{memory_store,file_store,memory_store_factory,file_store_factory}.cpp` (if any out-of-line)
- `src/session/quickfix_compat/cfg_loader.cpp`
- `include/fixpp/session/message_store_factory.hpp` (the extended-in-place add of `make()`)

**Not measured** (recorded exempt-by-inspection per `[const §IX.1]`):

- `include/fixpp/core/error.hpp` — enum-slot append (56–65) contributes zero instrumentable lines/branches (`llvm-cov` instruments statements/branches, not enum constants).
- `include/fixpp/core/engine_config.hpp` — the one default-initialiser line for `max_store_memory_per_session` is hit by every `validate_engine_config()` call (already covered by 007 tests).

Gate basis: per-file lcov DA/BRDA per `feedback_coverage_gate_lcov_basis`; targets ≥95% line / ≥85% branch on touched modules per `[const §IX.1]`. Below-target lines / branches MUST carry the Opus risk-assessment paired evidence in `.specify/decisions/008-message-store-verify.md` (genuine error/edge path → tested; defensive / unreachable / trivial-accessor → waived with one-line rationale; recorded-non-assessable-touch → exempt by inspection).

---

**Output:** This data-model.md is the behavioural oracle. Phase 1 contracts/ are the type-level oracle. `/tasks` generates implementation tasks against both.
