# 2l — Session-Tap Consumer API

| Field | Value |
|---|---|
| **Status** | Draft v0.4 — Gate A round 3 converged (Phase A) |
| **Date** | 2026-05-09 |
| **Owner** | Opus |
| **Inherits** | `[arch §4.9]`, `[arch §5.8]`, `[arch §8.2]`, `[arch §6]`, `[arch §5.3]`, `[arch §5.4]` |
| **Cites** | `[const §VI.5]`, `[const §VIII.5]`, `[const §XIII.2]`, `[const §XIV.2]`, `[const §XV.15]`, `[SYN §3.6 #22]` |
| **Catalogue rows owned** | S-036, NFR-014, COM-008, SVC-002, SVC-003 (fallback boundary) |
| **Convergence log** | addresses round-3 Codex review (0 P1 / 0 P2 / 3 P3) and Opus adversarial review (1 P1 / 0 P2 / 5 P3 post-judging), see Appendix C |

---

## §1 Goals

2l locks the **session-tap consumer API** (`[arch §4.9]`, `[SYN §3.6 #22]`) — the per-session hook that fires for every FIX message in and out. This doc owns:

1. The **`TapRecord`** value type written into the ring: schema version, direction, session ID, raw message bytes, timestamp, sequence number, and trace correlation fields.
2. The **`TapConsumer` variant** (`NoTap | RingBufferTap | Iox2Tap | SyncCallbackTap`) — the three active consumer shapes plus the `NoTap` sentinel, decided in `[SYN §3.6 #22]`.
3. The **`TapConfig`** struct: buffer size, drop policy, overflow counter, backpressure hook.
4. The **`RingBufferTap`** in-process default: SPSC ring, circular slab allocation, drain API.
5. The **`Iox2Tap`** iceoryx2 cross-process publisher: `TapShmRecord` wire layout, topic shape (`ServiceDescription` form), ownership semantics, backpressure, fallback when iceoryx2 is not running.
6. The **`SyncCallbackTap`** synchronous variant with its caveat-emptor documented contract.
7. The **producer-side path** inside the session dispatch loop: zero allocation on the I/O thread, what gets copied from `wire::View`, exact `wire::View` lifetime handling.
8. The **iceoryx2 topic shape** (SVC-002 primary detail) and the **gRPC-only fallback behaviour** when iceoryx2 is absent (SVC-003 boundary concern).
9. The **`SessionConfig` tap field** — confirmed amendment to `2d-threading.md`, noted explicitly in §7 and Appendix D. The `tap_consumer` field is already declared in `[2d §4.5]`; this doc confirms the shape and adds the `tap_config` amendment.
10. New `fixpp::core::error` variants in the `[1100, 1199]` block reserved for 2l by `[2i §1.1]`.

### §1.1 Scope boundary

**In scope:** everything listed in §1 above.

**Explicitly not in scope:**

- iceoryx2 installation, daemon-lifecycle management, or runtime watchdog. Those are operator/deployment concerns outside the library.
- SWIG/Python tap binding and GIL implications — owned by **2m**. This doc notes the boundary in §7.
- C ABI `fixpp_tap_*` symbols for non-C++ external tool subscription — deferred to a 2i Appendix D amendment or a Phase 3 C ABI extension doc. This doc covers the boundary decision in §5.
- The iceoryx2 version pin and CMake feature-flag build option — owned by the Phase 3 CMake/build doc.
- The `fixppd` daemon binary's internal wiring of the iceoryx2 subscriber — `fixppd` subscribes to the iceoryx2 SHM topic using the iceoryx2 `Subscriber<TapShmRecord>` API; it does **not** include `<fixpp/tap/...>` C++ tap headers. Its integration path is the `Iox2Tap` subscriber binding per §7.5 and RC#2.

**Note on `TapConsumer` extensibility:** The `TapConsumer` variant is an intentionally closed set for v1.0. User-defined tap backends are out of scope for v1.0; the recommended path for custom backends is to drain `RingBufferTap` and re-publish. See §10 for the post-v1.0 item.

---

## §2 Non-goals

1. **No per-record heap allocation** on the producer (I/O) thread. This is a hard invariant, not a goal — stated here to set first-pass expectations.
2. **No drop-oldest on the session message path.** The tap ring's drop-oldest policy is scoped to the tap path only; it never bleeds into the session dispatch or backpressure machinery per `[const §XV.15]`.
3. **No tap filtering at the producer side** in v1.0. Filtering (e.g., "tap only `NewOrderSingle`") is consumer-side logic; the producer always writes every in/out message verbatim. Post-v1.0.
4. **No tap record aggregation, compression, or encryption.** Raw bytes, as-received/sent. Post-v1.0.
5. **No multiple simultaneous consumers per session** for `RingBufferTap` in v1.0. The ring is SPSC — one producer (the session strand), one consumer (the user drain thread). Multi-consumer fan-out is post-v1.0.
6. **No bidirectional tap** (modifying the message before it is sent/dispatched). The tap is a pure observer; it never mutates session state per `[arch §2.4]`.
7. **No C ABI tap subscription symbols in v1.0.** External non-C++ tools use iceoryx2 SHM directly (SVC-002) or gRPC streaming (SVC-001). See §5.
8. **No dynamic plugin discovery.** Compile-time only per `[const §XIV.4]`.

---

## §3 Inherited surface

### §3.1 From `[arch §4.9]` — `tap` module surface inventory

> `fixpp::tap::TapConsumer` — variant alias (`NoTap` / `RingBufferTap` / `Iox2Tap` / `SyncCallbackTap`) `[SYN §3.6 #22]`. `NoTap` is the default sentinel (no tap configured).
>
> `fixpp::tap::TapConfig` — buffer size, drop policy (`drop-oldest` permitted here per `[const §XIII.2]` and `[const §XV.15]`), backpressure hook.
>
> Tap reads `wire::View` instances; never copies on the producer side beyond the ring write.

This doc inherits and fully specifies the above.

### §3.2 From `[arch §5.8]` — Backpressure

> **App and session message paths:** `block` or `disconnect-and-recover` only. **`drop-oldest` is banned** on these paths `[const §XV.15]`.
>
> **Telemetry/log/tap paths:** `drop-oldest` is permitted under bounded-queue overflow, with a recorded counter `[const §XIII.2]`.

This doc implements `TapConfig::drop_policy::drop_oldest` as the default overflow policy for `RingBufferTap` and `Iox2Tap`. The drop counter is `TapConfig::overflow_count` — a `mutable std::atomic<uint64_t>` on `TapConfig`, incremented by the concrete `TapConsumer` implementation for every dropped record. Users read it via `tap_config.overflow_count.load(std::memory_order_relaxed)`. `block` mode is available for `RingBufferTap`. Drop-oldest never applies to the session's message dispatch path; it is scoped to the tap path exclusively.

### §3.3 From `[arch §8.2]` — Data plane (iceoryx2)

> iceoryx2 publish/subscribe over shared memory.
>
> Topic shape, ownership semantics, backpressure, fallback when iceoryx2 isn't running — all in **2l** (which also covers the in-process tap consumer).
>
> A consumer that needs only request/response (no high-volume message stream) can run gRPC-only and ignore iceoryx2.

This doc delivers the full iceoryx2 topic shape specification in §4.5 and §6.4.

### §3.4 From `[SYN §3.6 #22]` — Session-tap consumer API (DECIDED)

> **Decision:** Ring buffer (b) is the in-process default; iceoryx2 publisher (c) is the opt-in cross-process variant; synchronous callback (a) is offered but documented as "you accept the latency risk" (caveat-emptor).
>
> The three options:
> (a) Synchronous callback — user fn called on the I/O thread per message: ✅ lowest latency / ❌ slow consumer stalls the session.
> (b) Async ring buffer — fixpp writes serialised message bytes into a per-session ring; user drains in their own thread: ✅ decouples consumer speed / ✅ drop-on-overflow is clear / ❌ one copy out of the I/O buffer.
> (c) iceoryx2 publisher — same as ring buffer, but cross-process: ✅ external tools attach without rebuilding / ✅ same data plane as SVC-002 / ❌ requires iceoryx2 dep for a feature not all users need.

This doc operationalises all three options.

### §3.5 From `[arch §6]` — Plugin pattern

> Each pluggable interface gets: a pure-virtual class, ≤5 pure-virtual methods `[const §XIV.2]`, one default impl, a PMR factory entry point, compile-time selection in v1.0; no `dlopen`.
>
> | Interface | Default impl | Design doc |
> |---|---|---|
> | `fixpp::tap::TapConsumer` (variant) | `RingBufferTap` (in-process) | **2l** |

`TapConsumer` is implemented as a `std::variant<NoTap, RingBufferTap, Iox2Tap, SyncCallbackTap>` — a discriminated union, not a virtual interface. `NoTap` is the first alternative and serves as the "tap not configured" sentinel; default-construction yields `NoTap{}`. Therefore the `[const §XIV.2]` ≤5 pure-virtual cap is not directly applicable: the variant itself has no virtual methods. The component types (`RingBufferTap`, `Iox2Tap`, `SyncCallbackTap`) are concrete value types with no virtual dispatch. The producer-side write path uses `std::visit` over the variant. This avoids vtable overhead on the hot path while preserving the three-way polymorphism mandated by `[SYN §3.6 #22]`.

The variant is a **closed set** for v1.0 (see §1.1). The `[arch §6]` pluggable-interface table note for the tap row should be read as "three-way variant (not runtime-virtual); user-extensible via a future `CustomTap` value type post-v1.0."

### §3.6 From `[arch §5.3]` — Error model

> **Recoverable engine errors:** `fixpp::core::expected_t<T>` (alias for `std::expected<T, fixpp::core::error>`).
>
> **Hot path is exception-free.** No `throw` between parse and `fromApp` `[const §VIII.5]`.
>
> **C ABI translates** `fixpp::core::error` → `fixpp_error_t` at the boundary.

New tap error variants occupy the `[1100, 1199]` block reserved by `[2i §1.1]` / `[2i §4.3]` block layout table.

### §3.7 From `[arch §5.4]` — Trace context (owned by 2d; 2l consumes)

> `co_await fixpp::current_trace_context` returns the trace context bound to the current session strand.

`TapRecord` carries the `fixpp::otel::trace_context` snapshot captured at the point of the tap write on the session strand. The trace context is read synchronously via `session.get_trace_context()` ([2k App D §D.1]) — a non-suspending accessor that reads the `session_local<trace_context>` slot directly. The `Session::get_trace_context()` synchronous accessor is added to `Session`'s public surface by `[2k App D §D.1]`; 2l consumes it here but does not independently amend `2d-threading.md` for this method. This is the same slot populated at session open from `SessionConfig::initial_trace_context`. See §7.1 for the dispatch-loop pseudocode.

### §3.8 From `[SYN §3.8]` — Sibling with 2k logger

> Audit-trail and conformance-test consumption (S-036 / NFR-014 / §3.6 #22) ride on the same machinery; the session-tap ring buffer and the logger are sibling abstractions.

The tap ring buffer and the logger ring are sibling abstractions: both have a zero-alloc producer side, a bounded ring, and `drop-oldest` semantics on overflow. They are intentionally **not coupled at the API level**: tap carries raw FIX message bytes; logger carries structured `fixpp::log::Record` instances. Both allocate their ring backing store from a `std::pmr::memory_resource*` supplied at construction. PMR alignment is confirmed in §5 (PMR recap) and §7.3 (sibling alignment).

---

## §4 Public C++ API

**Convention.** Every view-returning accessor carries `[[clang::lifetimebound]]`. Every `expected_t<T>`-returning method carries `[[nodiscard]]`. Value-returning methods that cannot fail (e.g., counter accessors) do not return `expected_t<T>`; invariant violations abort per `[arch §5.3]`.

### §4.1 `fixpp::tap::TapDirection`

```cpp
// include/fixpp/tap/tap_record.hpp
namespace fixpp::tap {

// Direction of the tapped FIX message relative to the session.
enum class TapDirection : uint8_t {
    inbound  = 0,   // received from the counterparty
    outbound = 1,   // sent to the counterparty
};

}  // namespace fixpp::tap
```

### §4.2 `fixpp::tap::TapRecord`

`TapRecord` is the value type written into the ring buffer slots by the producer (session dispatch loop) and read by the consumer (drain thread or iceoryx2 subscriber). It is stored in the ring's fixed-size slot array.

**Canonical storage model (RC#1 — model B):** The ring allocates a contiguous circular slab of `TapConfig::capacity × sizeof(TapSlot)` bytes at construction. Each slot is a `TapSlot { TapRecord header; std::byte payload[k_tap_record_max_bytes]; }`. The `TapRecord` header has no pointer fields; the payload lives in the adjacent `payload[]` array within the same slot. See the per-variant ownership contract below.

For `RingBufferTap`, the consumer accesses the ring slot's bytes window directly; no pointer chasing is needed. For `SyncCallbackTap`, the callback receives a separate header and payload span — see §4.6. For `Iox2Tap`, the `TapShmRecord` struct (§4.5) contains the bytes inline.

**Sequence number semantics** (`TapRecord::sequence_number`): monotonically increasing per-session counter that increments on every produced record, including records written in `drop_oldest` mode when overwriting old slots. Gaps in the sequence numbers observed by the consumer represent records that were overwritten by the ring's circular write before the consumer drained them. A gap of `delta` means `delta - 1` records were overwritten. The consumer should correlate gaps with `TapConfig::overflow_count` to confirm the accounting. If `block` mode is used, there are no gaps. See §6.3 for the full gap formula.

```cpp
// include/fixpp/tap/tap_record.hpp
namespace fixpp::tap {

// Maximum inline byte capacity for the FIX message stored in a TapRecord slot.
// Messages exceeding this threshold are truncated and the truncated flag is set.
// 4 KiB covers FX/equities messages with ample headroom; increase for
// equity-options venues (see [2b §1.2] frame-size discussion).
inline constexpr std::size_t k_tap_record_max_bytes = 4096;

// SessionId is a lightweight, fixed-size session identifier carried by the
// TapRecord.  It consists of:
//   - SenderCompID (up to 16 bytes, inline storage, no null terminator)
//   - TargetCompID (up to 16 bytes, inline storage, no null terminator)
//   - BeginString  (up to 10 bytes, inline storage, e.g. "FIX.4.4\0")
//   - SessionQualifier (up to 4 bytes; empty string if not configured)
// These fields uniquely identify a FIX session per [FIX-SL §4.3].
// The struct is value-typed (no pointers); the TapRecord owns a copy,
// so there is no lifetime dependency on the Session object.
struct SessionId {
    char    sender[16] {};       // SenderCompID; NOT null-terminated; use sender_len
    uint8_t sender_len {};
    char    target[16] {};       // TargetCompID; NOT null-terminated; use target_len
    uint8_t target_len {};
    char    begin_string[10] {}; // e.g. "FIX.4.4"; NUL-padded
    uint8_t qualifier_len {};
    char    qualifier[4] {};     // session qualifier; empty string if not configured
};
// Layout: 16 + 1 + 16 + 1 + 10 + 1 + 4 = 49 bytes.
// Use an upper-bound check rather than exact-size assertion to be robust
// against future field additions within this struct.
static_assert(sizeof(SessionId) <= 64,
    "SessionId must fit in 64 bytes; check field layout if adding fields");

// TapRecord — the value written into the ring slot by the session strand.
//
// Storage model (canonical — RC#1 / model B):
//   For RingBufferTap:
//     Each ring slot is a fixed TapSlot { TapRecord header; std::byte payload[k_tap_record_max_bytes]; }.
//     The consumer reads the header and payload from the slot directly.
//     The payload bytes are valid until the producer overwrites this slot (i.e.,
//     until TapConfig::capacity further records are written without the consumer draining).
//     Do not store a pointer to the payload past the point where the ring
//     may wrap back to this slot.
//
//   For Iox2Tap:
//     The wire format is TapShmRecord { TapRecord header; std::byte payload[k_tap_record_max_bytes]; }.
//     The subscriber receives a TapShmRecord sample; bytes_len bytes of
//     sample.payload[0..bytes_len-1] contain the raw FIX wire bytes.
//     See §4.5 and §6.4 for the full subscriber contract.
//
//   For SyncCallbackTap:
//     The callback receives (const TapRecord& header, std::span<const std::byte> payload).
//     The payload span is valid only for the duration of the callback.
//     See §4.6.
//
// In all variants, the TapRecord header is the same struct; only the backing
// store and payload pointer semantics differ.
struct TapRecord {
    // --- Schema version ---

    // Schema version for forward compatibility.  Value is 1 for v0.2+.
    // Subscribers SHOULD check schema_version == 1 before interpreting
    // other fields.  Future layout changes increment this field.
    uint8_t schema_version {1};

    // Direction of the message.
    TapDirection direction {};

    // Whether the message was truncated at k_tap_record_max_bytes.
    bool truncated {};

    uint8_t _pad0[5] {};  // explicit padding; static_assert enforces layout

    // Monotonically increasing per-session sequence number.  Increments by 1
    // for every record produced (including dropped/overwritten records in
    // drop_oldest mode).  A gap of `delta` in the consumer-observed sequence
    // numbers means `delta - 1` records were overwritten before draining.
    // See §6.3 for the full gap formula.
    uint64_t  sequence_number {};

    // Wall-clock timestamp captured on the session strand at the moment the
    // tap write was initiated.  Source: effective_clock.now() per [2d §7.9].
    fixpp::core::time_point timestamp {};

    // OTel trace context captured via [arch §5.4] / [2d §4.6].
    // Allows correlation of a tapped message with the session's parent span.
    // Zero-valued (all bytes 0x00) when no OTel context is active.
    fixpp::otel::trace_context trace_ctx {};

    // Session identification (value copy; no pointer to the Session).
    SessionId session_id {};

    // Byte count of the FIX message payload stored in the accompanying
    // payload window (bytes_len <= k_tap_record_max_bytes).
    // For RingBufferTap: payload follows the header in TapSlot::payload.
    // For Iox2Tap: payload follows the header in TapShmRecord::payload.
    // For SyncCallbackTap: payload is the span passed to the callback.
    std::size_t bytes_len {};
};
// Layout contract: checked at compile time.
static_assert(std::is_trivially_copyable_v<TapRecord>);
static_assert(alignof(TapRecord) <= 8);

}  // namespace fixpp::tap
```

**Fixed-slot ring layout:**

The ring buffer stores records in fixed-size slots:

```cpp
// Internal ring slot type — used by RingBufferTap.
// Not part of the public API but documented here for arena budget arithmetic.
// Located in src/tap/ring_buffer_tap.cpp (internal).
struct TapSlot {
    fixpp::tap::TapRecord header;
    std::byte             payload[fixpp::tap::k_tap_record_max_bytes];
};
```

Each slot occupies exactly `sizeof(TapRecord) + k_tap_record_max_bytes` bytes. The ring slot array is a contiguous allocation of `TapConfig::capacity` such slots from `TapConfig::tap_ring_arena`. No per-record allocation occurs after ring construction. The payload bytes in each slot are stable for the ring's lifetime; they are overwritten in-place when the ring wraps to that slot.

**Ownership contract summary:**

- The producer (session strand) writes raw FIX wire bytes into the current slot's `payload` window (`memcpy` of `min(wire_bytes.size(), k_tap_record_max_bytes)` bytes), then writes the `TapRecord` header into `slot.header`, then atomically advances the head index.
- The consumer reads the `TapRecord` header and the `payload` bytes from the slot. The payload is valid until the producer wraps the ring back to this slot (i.e., until `TapConfig::capacity` further records are produced without the consumer draining).
- The consumer must **not** hold a pointer into a slot past the point where the ring may overwrite it. No cross-ring-wrap borrowing.
- For `Iox2Tap`, the `TapShmRecord` struct (§4.5) contains both header and payload inline. The bytes live in iceoryx2 shared memory; freeing the `Sample` invalidates access. See §4.5 and §6.4.

### §4.3 `fixpp::tap::TapConfig`

```cpp
// include/fixpp/tap/tap_config.hpp
namespace fixpp::tap {

// Drop policy for the tap ring when it is full.
enum class TapDropPolicy : uint8_t {
    // Overwrite the oldest record in the ring with the newest.
    // Permitted on tap paths per [const §XIII.2] / [const §XV.15].
    // TapConfig::overflow_count is incremented atomically for every dropped record.
    drop_oldest = 0,

    // Non-stalling overflow handling: call on_overflow_block callback (if set),
    // then retry once.  If still full after retry, fall back to drop_oldest
    // and increment TapConfig::overflow_count.
    // NOT available for Iox2Tap (iceoryx2 has its own publisher-side policy).
    // NOT available for SyncCallbackTap (the callback is synchronous; no ring).
    // See §6.3 for the full block-mode contract.
    block = 1,
};

// TapConfig is value-typed and frozen at Session open.
// It is carried in SessionConfig alongside tap_consumer (see §7.1 / Appendix D).
//
// User-settable fields: capacity, drop_policy, max_message_bytes, tap_ring_arena,
//   on_overflow_block.
// Runtime-managed field: overflow_count — incremented by the TapConsumer
//   implementation on every dropped record; not user-settable.
//
// Note: TapConfig is unconditionally non-copyable (std::atomic member);
// move-construct or emplace into SessionConfig. Brace-initialisation is safe.
//
// iceoryx2 service name: defined in Iox2TapConfig::service_name (§4.5).
// TapConfig does not carry an iceoryx2 service name field; configure it via
// Iox2TapConfig when constructing an Iox2Tap.
struct TapConfig {
    // ── User-settable fields ─────────────────────────────────────────────

    // Number of TapSlot slots in the ring.  Must be a power of 2.
    // Default: 4096 records.  At sizeof(TapSlot) per slot (≈ sizeof(TapRecord) +
    // k_tap_record_max_bytes) the ring's byte backing store is approximately
    // 4096 × (128 + 4096) ≈ 17 MiB; the tap_ring_arena must accommodate this.
    // Users on low-rate sessions should reduce to 256 or 512.
    std::size_t capacity = 4096;

    // Drop policy.  Default: drop_oldest (safe for a telemetry path).
    // Use block only when the consumer is known to drain faster than the
    // session produces messages.
    TapDropPolicy drop_policy = TapDropPolicy::drop_oldest;

    // Maximum FIX message bytes captured per record.
    // Messages longer than this are truncated (TapRecord::truncated = true).
    // Must be <= k_tap_record_max_bytes.  Default: k_tap_record_max_bytes (4096).
    std::size_t max_message_bytes = k_tap_record_max_bytes;

    // PMR arena for ring buffer backing store (TapSlot array).
    // Null → std::pmr::get_default_resource().
    // Lifetime must outlive the TapConsumer.
    std::pmr::memory_resource* tap_ring_arena = nullptr;

    // Optional overflow notification callback (block mode only).
    // Called on the session strand when the ring is full and block mode is
    // active, before the single retry attempt.
    // The callable runs synchronously on the I/O thread; keep it trivially
    // fast (increment a counter, set a flag).
    // Null → no hook.  The producer retries once after the callback, then
    // falls back to drop_oldest if still full — see §6.3.
    std::function<void()> on_overflow_block = {};

    // ── Runtime-managed field (do not set; managed by TapConsumer) ───────

    // Cumulative count of records dropped since session open.
    // Incremented atomically by the concrete TapConsumer (RingBufferTap /
    // Iox2Tap) for every dropped record, including under drop_silently
    // fallback — satisfying [const §XIII.2].
    // Read via: tap_config.overflow_count.load(std::memory_order_relaxed).
    // NOT user-settable.  The atomic is mutable so const readers (e.g.,
    // monitoring callbacks on a const SessionConfig ref) can observe it.
    mutable std::atomic<uint64_t> overflow_count = 0;
};

}  // namespace fixpp::tap
```

**Default `TapConfig` values summary (for reference in Appendix D amendment):**
`{.capacity=4096, .drop_policy=TapDropPolicy::drop_oldest, .max_message_bytes=4096, .tap_ring_arena=nullptr, .on_overflow_block={}, .overflow_count=0}` (runtime-managed `overflow_count` not user-settable; iceoryx2 service name configured via `Iox2TapConfig::service_name` — §4.5)

### §4.4 `fixpp::tap::RingBufferTap`

`RingBufferTap` is the in-process default tap consumer. It owns a SPSC ring of fixed `TapSlot` entries backed by `TapConfig::tap_ring_arena`.

```cpp
// include/fixpp/tap/ring_buffer_tap.hpp
namespace fixpp::tap {

class RingBufferTap {
public:
    // Construction.
    // `config.capacity` must be a power of 2 and >= 2.
    // `config.tap_ring_arena` must outlive this object.
    // Throws std::invalid_argument if invariants violated (construction-time only).
    explicit RingBufferTap(TapConfig config);

    // Non-copyable, movable.
    RingBufferTap(const RingBufferTap&) = delete;
    RingBufferTap& operator=(const RingBufferTap&) = delete;
    RingBufferTap(RingBufferTap&&) noexcept;
    RingBufferTap& operator=(RingBufferTap&&) noexcept;

    ~RingBufferTap();

    // --- Consumer API ---

    // Drain up to `max_records` records from the ring, calling `callback`
    // for each record in FIFO order.  Returns the number of records drained.
    // Thread-safety: callable from exactly one consumer thread at a time
    // (SPSC contract — grounded in [2d §5.1] per-session strand; see §6.2).
    // Must not be called from the session I/O strand.
    //
    // The TapRecord reference and the span passed to `callback` are valid
    // only for the duration of that callback invocation.  Do not store the
    // payload pointer past callback return; the ring may wrap to that slot
    // on the next producer write cycle.
    template <typename Callback>
        requires std::invocable<Callback, const TapRecord&, std::span<const std::byte>>
    std::size_t drain(std::size_t max_records, Callback&& callback);

    // Drain all available records.  Equivalent to drain(ring_capacity(), cb).
    // ring_capacity() returns std::size_t matching TapConfig::capacity.
    template <typename Callback>
        requires std::invocable<Callback, const TapRecord&, std::span<const std::byte>>
    std::size_t drain(Callback&& callback);

    // --- Observability ---

    // Total records successfully written since construction.
    [[nodiscard]] uint64_t write_count() const noexcept;

    // Overflow count is NOT on RingBufferTap.  Read via:
    //   session_config.tap_config.overflow_count.load(std::memory_order_relaxed)
    // The TapConfig::overflow_count atomic is incremented by the producer
    // on every dropped record — see [2l §4.3] and [2l §6.3].

    // Ring capacity (in slots), as configured.  Equal to TapConfig::capacity.
    [[nodiscard]] std::size_t ring_capacity() const noexcept;

    // Approximate number of records currently available to drain.
    // Best-effort; may race with the producer.
    [[nodiscard]] std::size_t available() const noexcept;

private:
    struct Impl;
    // The Impl is allocated from tap_ring_arena at construction.
    Impl* impl_ = nullptr;
};

}  // namespace fixpp::tap
```

**Thread-safety contract:**

- **Producer side** (`write_record` — internal, called by the session dispatch loop on the session strand): `std::atomic` store on the head index; write happens before the head update (`std::memory_order::release`).
- **Consumer side** (`drain`): `std::atomic` load on the head index (`std::memory_order::acquire`); must be called from exactly one thread at a time (SPSC).
- The `write_count()` accessor uses `std::memory_order::relaxed` — it is a metric, not a synchronisation primitive. The overflow counter is `TapConfig::overflow_count` (a `mutable std::atomic<uint64_t>` on `TapConfig`); read it via `tap_config.overflow_count.load(std::memory_order_relaxed)`.

**Arena allocation:**

The ring slot array (an array of `TapConfig::capacity` `TapSlot` values) is allocated from `TapConfig::tap_ring_arena` at construction time. After construction, the producer path uses only in-place writes within the pre-allocated slot array. No bump-allocation, no per-record allocation. If `tap_ring_arena` is a `std::pmr::monotonic_buffer_resource` constructed over a pre-allocated buffer, the producer path is provably zero-alloc.

### §4.5 `fixpp::tap::Iox2Tap`

`Iox2Tap` is the iceoryx2 cross-process publisher variant. It requires iceoryx2 to be installed and the `iceoryx2` runtime to be reachable. It is opt-in: the `tap` CMake module is conditionally compiled with `FIXPP_ENABLE_IOX2=ON`.

**`TapShmRecord` — the iceoryx2 SHM wire format:**

```cpp
// include/fixpp/tap/iox2_tap.hpp
// Only available when FIXPP_ENABLE_IOX2=ON.
#if defined(FIXPP_ENABLE_IOX2)
namespace fixpp::tap {

// TapShmRecord is the fixed-size struct published to iceoryx2 subscribers.
// The publisher uses Publisher<TapShmRecord> — each sample is exactly
// sizeof(TapShmRecord) bytes, matching the slot size.
// The publisher sets slot.header.bytes_len to the actual byte count and
// writes the FIX wire bytes into slot.payload[0..bytes_len-1].
// The subscriber reads sample.header.bytes_len bytes from
// sample.payload[0..bytes_len-1].  No other pointer field exists in
// TapRecord; payload access is solely via the adjacent payload[] array.
struct TapShmRecord {
    TapRecord             header;                          // fixed metadata
    std::byte             payload[k_tap_record_max_bytes]; // inline FIX wire bytes
};
static_assert(std::is_trivially_copyable_v<TapShmRecord>);
static_assert(sizeof(TapShmRecord) == sizeof(TapRecord) + k_tap_record_max_bytes);

}  // namespace fixpp::tap
#endif  // FIXPP_ENABLE_IOX2
```

```cpp
#if defined(FIXPP_ENABLE_IOX2)
namespace fixpp::tap {

// Iox2TapConfig — iceoryx2-specific configuration.
struct Iox2TapConfig {
    // Service name (see §6.4 for the naming convention).
    // Leave empty to auto-derive from session's BeginString / SenderCompID /
    // TargetCompID / SessionQualifier.
    char service_name[128] {};

    // Service instance name.  Uniquely identifies the publisher among
    // all publishers for the same service name.  Default: "default".
    char instance_name[32] { "default" };

    // iceoryx2 event name (the "method" in iceoryx2 terminology).
    // Default: "tap-record". Fixed in v1.0.
    char event_name[32] { "tap-record" };

    // Maximum number of outstanding samples (publisher buffer depth).
    // iceoryx2 will drop new samples when all outstanding samples are
    // held by subscribers and none have been returned.  This is
    // iceoryx2's internal backpressure mechanism.
    // Default: 16.
    uint32_t max_publisher_history = 16;

    // Fallback policy when iceoryx2 is not running or unavailable.
    enum class FallbackPolicy : uint8_t {
        // Fall back to an in-process ring buffer (RingBufferTap).
        // The fallback ring uses the same TapConfig::tap_ring_arena.
        // The Iox2Tap logs a one-time warning when fallback is active.
        in_process_ring = 0,

        // Return error::tap_iox2_not_running from the session-open call.
        // The session will not open.
        error_on_open = 1,

        // Silently drop all tap records when iceoryx2 is unavailable.
        // NOTE: TapConfig::overflow_count is still incremented for every
        // suppressed record under this policy — see §6.4.
        drop_silently = 2,
    };

    FallbackPolicy fallback = FallbackPolicy::in_process_ring;
    uint8_t _pad[3] {};
};

class Iox2Tap {
public:
    // Construction.
    // Returns expected_t<Iox2Tap> — may fail if iceoryx2 is not running
    // and fallback == error_on_open.
    [[nodiscard]] static fixpp::core::expected_t<Iox2Tap>
    create(TapConfig config, Iox2TapConfig iox2_config);

    // Non-copyable, movable.
    Iox2Tap(const Iox2Tap&) = delete;
    Iox2Tap& operator=(const Iox2Tap&) = delete;
    Iox2Tap(Iox2Tap&&) noexcept;
    Iox2Tap& operator=(Iox2Tap&&) noexcept;
    ~Iox2Tap();

    // --- Consumer side (cross-process) ---
    //
    // Consumers do not interact with Iox2Tap from C++.  They are iceoryx2
    // subscribers that connect to the ServiceDescription published by this
    // object and receive TapShmRecord samples.
    //
    // fixppd integration: fixppd subscribes using Subscriber<TapShmRecord>
    // via the iceoryx2 subscriber API.  fixppd does NOT include
    // <fixpp/tap/...> C++ tap headers — only the iceoryx2 subscriber API
    // and the TapShmRecord layout header (which carries no engine-internal
    // dependencies).  This is the binding v1.0 fixppd integration path
    // (RC#2 / Codex P1-5 fix).

    // Returns the ServiceDescription used by this publisher.
    // Consumer processes use this description to create a subscriber.
    struct ServiceDescription {
        char service_name[128];
        char instance_name[32];
        char event_name[32];
    };
    [[nodiscard]] ServiceDescription service_description() const noexcept;

    // --- Observability ---

    // Overflow count is NOT on Iox2Tap.  Read via:
    //   session_config.tap_config.overflow_count.load(std::memory_order_relaxed)
    // The TapConfig::overflow_count atomic is incremented by the producer
    // on every dropped record (including under drop_silently fallback) — see §6.4.
    [[nodiscard]] uint64_t write_count()    const noexcept;

    // Whether the publisher is using the in-process fallback ring.
    [[nodiscard]] bool is_fallback_active() const noexcept;

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

}  // namespace fixpp::tap
#endif  // FIXPP_ENABLE_IOX2
```

**iceoryx2 topic naming convention** (canonical; see §6.4 for behavioural contract):

```
Service name:   fixpp/<beginstring>/<sendercompid>/<targetcompid>[/<sessionqualifier>]
                where <sessionqualifier> is empty string if not configured.
                (lower-case, alphanumeric + dash, slashes permitted;
                 max 128 characters per iceoryx2 constraint)

Instance name:  "default"  (single publisher per session in v1.0)

Event name:     "tap-record"  (fixed in v1.0)
```

Example: a FIX.4.4 session from `FIXPP_CLIENT` to `BROKER_EAST` without a session qualifier publishes to:
`fixpp/fix.4.4/fixpp-client/broker-east` / `default` / `tap-record`.

**BeginString inclusion ensures global uniqueness.** The `SessionId` struct (§4.2) includes `begin_string` precisely because `SenderCompID / TargetCompID / BeginString` together uniquely identify a FIX session per `[FIX-SL §4.3]`. The iceoryx2 service name must include all three components to match this guarantee.

**SessionQualifier handling.** `sessionqualifier` is appended (lower-cased, non-alphanumeric replaced with `-`) when `SessionId::qualifier_len > 0`. When `qualifier_len == 0`, the field is omitted from the service name (no trailing slash). With the fixed field sizes (`begin_string[10]`, `sender[16]`, `target[16]`, `qualifier[4]`), the maximum auto-derived service name length is `len("fixpp/") + 10 + 1 + 16 + 1 + 16 + 1 + 4 = 55` — well within the 128-character limit. If a manually set `Iox2TapConfig::service_name` exceeds 127 characters, `Iox2Tap::create()` returns `error::tap_invalid_config`.

### §4.6 `fixpp::tap::SyncCallbackTap`

> **WARNING — CAVEAT-EMPTOR VARIANT.** The synchronous callback runs on the session I/O strand. A slow callback **stalls the session**: heartbeat, send, receive, and all session FSM operations are blocked while the callback executes. Use `RingBufferTap` unless you have measured and accepted this cost. This variant is provided for integration testing and one-shot diagnostic use only — not for production message-rate workloads.

**Callback signature:** `void(const TapRecord& header, std::span<const std::byte> payload)`. The `header` and `payload` are both valid **only for the duration of the callback invocation**. The `payload` span points into the session's I/O buffer and **must not be held past callback return** — the session reclaims the I/O buffer after the callback returns.

```cpp
// include/fixpp/tap/sync_callback_tap.hpp
namespace fixpp::tap {

// Synchronous tap callback signature.
// `header` carries metadata (sequence_number, direction, session_id, etc.).
// `payload` is a view into the session's I/O buffer valid only during
// the callback invocation.  NEVER safe to store the span or a raw pointer
// derived from it past callback return.
// Exceptions thrown from the callback are caught and logged as errors;
// they do NOT propagate to the session FSM.  Throwing is strongly discouraged.
using SyncCallback =
    std::function<void(const TapRecord& header, std::span<const std::byte> payload)>;

// Recommended time budget: <= 1 µs per callback invocation at the median.
// A callback averaging 5 µs at 100,000 msg/s adds 500 ms of latency per second.

class SyncCallbackTap {
public:
    // Construction.  `callback` must be non-null.
    explicit SyncCallbackTap(SyncCallback callback);

    SyncCallbackTap(const SyncCallbackTap&) = delete;
    SyncCallbackTap& operator=(const SyncCallbackTap&) = delete;
    SyncCallbackTap(SyncCallbackTap&&) noexcept;
    SyncCallbackTap& operator=(SyncCallbackTap&&) noexcept;

    ~SyncCallbackTap();

    // --- Observability ---

    // Counts exceptions thrown and caught by the tap write path.
    [[nodiscard]] uint64_t exception_count() const noexcept;

private:
    SyncCallback callback_;
    std::atomic<uint64_t> exception_count_ {};
};

}  // namespace fixpp::tap
```

**Payload span lifetime (explicit note):** The `std::span<const std::byte> payload` passed to the callback is a view into the session's I/O buffer. It is valid only for the duration of the callback. If the user needs to retain the bytes, they must copy them (e.g., into a `std::vector<std::byte>`) before the callback returns. The `TapRecord::bytes_len` field carries the valid byte count.

### §4.7 `fixpp::tap::TapConsumer` variant and `TapProducer`

```cpp
// include/fixpp/tap/tap_consumer.hpp
namespace fixpp::tap {

// NoTap is the "tap not configured" sentinel.  Default-constructing
// TapConsumer yields NoTap{}.  The session dispatch loop checks for
// NoTap before calling tap_write, so no ring is allocated and no
// write overhead is incurred when the user has not configured a tap.
struct NoTap {};

// TapConsumer is a std::variant over the three tap shapes plus NoTap.
// The variant is stored by value in SessionConfig::tap_consumer (see [2d §4.5]).
// Producer-side write uses std::visit over the variant (no virtual dispatch).
// Default-constructed TapConsumer = NoTap{} (index 0 = no-op).
//
// The variant is an intentionally closed set for v1.0.  User-defined tap
// backends are out of scope; see §1.1 and §10.
using TapConsumer = std::variant<
    NoTap,          // default; "tap not configured"; no ring, no write cost
    RingBufferTap,
    Iox2Tap,        // only when FIXPP_ENABLE_IOX2
    SyncCallbackTap
>;

// TapProducer is the engine-internal write interface.
// It is NOT part of the public user-facing API.
// The session dispatch loop calls tap_write() via std::visit.
// Declared here for documentation clarity; defined in src/tap/tap_producer.cpp.
namespace detail {
    // Write a tap record to the consumer.  Called on the session I/O strand.
    // Zero allocation guarantee: the implementation is constrained to
    // in-place writes within the pre-allocated TapSlot array only.
    void tap_write(TapConsumer& consumer,
                   TapDirection direction,
                   fixpp::core::span<const std::byte> wire_bytes,
                   const SessionId& session_id,
                   fixpp::core::time_point timestamp,
                   const fixpp::otel::trace_context& trace_ctx,
                   uint64_t sequence_number) noexcept;
}  // namespace detail

}  // namespace fixpp::tap
```

**Why the producer path is `detail`-only:** The session dispatch loop is the sole producer. User code never calls `tap_write` directly. The `detail` namespace signals internal use; the function is not exported from the public header set.

---

## §5 Public C ABI

No new C ABI tap symbols are introduced in v1.0 for the following reasons:

1. **In-process consumers** use the C++ public API (`RingBufferTap::drain`, `SyncCallbackTap`) directly. The C ABI is for non-C++ consumers; those consumers do not link the engine in-process and therefore cannot call drain callbacks over a C++ variant.
2. **Cross-process consumers** attach to the iceoryx2 SHM topic using the iceoryx2 subscriber API directly (SVC-002). No C ABI shim is needed; the iceoryx2 C API is the consumer interface.
3. **gRPC-only deployments** (SVC-003 fallback): consumers that do not run iceoryx2 receive FIX message streams via the `fixppd` daemon's gRPC `StreamMessages` RPC (owned by `2j`). The tap ring feeds into the `fixppd` relay logic via the iceoryx2 SHM subscriber path, not via a new tap-specific C ABI symbol.

**Future path:** If a Phase 3 requirement arises for C-callable `fixpp_tap_subscribe` / `fixpp_tap_drain` symbols (e.g., for a C-language protocol analyzer), the `[1100, 1199]` error block reserved for 2l by `[2i §1.1]` / `[2i §4.3]` can accommodate those symbols, and a 2i Appendix D amendment would define them. This is explicitly deferred to Phase 3.

**Reentrancy note:** Should tap C ABI symbols be added in Phase 3, each must carry exactly one of `FIXPP_THREAD_SAFE` / `FIXPP_SINGLE_THREAD` / `FIXPP_REQUIRES_SESSION_LOCK` per `[const §X.5]` / `[2i §4.10]`.

---

## §6 Behavioral Contract

### §6.1 Producer-side guarantee: zero allocation on the I/O thread

The session dispatch loop calls `tap::detail::tap_write(...)` on the session strand for every inbound and outbound FIX message, before `fromApp` / `toApp` dispatch. The call guarantees:

- **No `malloc` / `new` / `::operator new`.** The implementation is restricted to:
  1. In-place writes within the pre-allocated `TapSlot` array (the ring's fixed slot backing store, allocated at construction from `TapConfig::tap_ring_arena`).
  2. Atomic operations on the ring head index.
  3. A `memcpy` of `min(wire_bytes.size(), TapConfig::max_message_bytes)` bytes into the current slot's `payload` window.
- **No exceptions thrown.** The function is `noexcept`. Any internal error (e.g., ring-full in drop-oldest mode) is handled by incrementing `TapConfig::overflow_count` and returning; it is never propagated to the session FSM.
- **No blocking.** In `drop_oldest` mode the write always completes in bounded time (a memory-order `release` store). In `block` mode the producer calls `on_overflow_block` (if set) and retries once — bounded, non-spinning, non-stalling; see §6.3.

**What "zero allocation" means:** In this doc, "zero allocation" means no calls to the global `malloc`/`free` / `::operator new` / `::operator delete`. PMR allocation at ring construction time is not a global heap call; after construction no PMR calls occur on the hot path.

**Verification:** The TS-9 allocation-guard test seam (§9) injects `std::pmr::null_memory_resource()` as the engine's global fallback and drives a session under `mallocnesia`. Any global-heap touch triggers an ASan trap.

### §6.2 Ring buffer mechanics

`RingBufferTap` uses a **SPSC (single-producer single-consumer) lock-free ring**.

- **Producer:** the session I/O strand (one writer, always).
- **Consumer:** the user drain thread (one reader, always).
- **Capacity:** `TapConfig::capacity` slots, a power of 2. The ring uses a head-tail pair of `std::atomic<uint32_t>` with index wraparound at `capacity` (power-of-2 mask: `index & (capacity - 1)`).
- **Memory ordering:** head write uses `std::memory_order::release`; tail read on the consumer uses `std::memory_order::acquire`. This provides the minimum necessary happens-before guarantee: the consumer sees the fully-written `TapSlot` when it observes the head advance.
- **Slot layout:** Each ring slot is a `TapSlot { TapRecord header; std::byte payload[k_tap_record_max_bytes]; }`. The slot array is a contiguous allocation of `capacity` such slots from `tap_ring_arena`. No per-record allocation occurs during operation.
- **Index wraparound:** head and tail indices are `uint32_t`. At `capacity` the index wraps via mask. No integer overflow for `uint32_t` at achievable message rates.

**SPSC safety guarantee grounded in `[2d §5.1]`:** The SPSC contract holds because all session writes (inbound and outbound) are serialised through the per-session strand. Under `[2d §4.5]`'s `threading_mode::per_session_strand` (the default), all session work dispatches on one strand — at most one concurrent writer to the tap ring at any time. Under `threading_mode::direct_executor`, the user attests `already_serialized_executor = true`, contracting that their executor is already serialised (per `[2d §6.1]`). If a caller bypasses the strand (e.g., calls `Session::send()` from a background thread without attesting serialisation), SPSC safety is the caller's responsibility — the engine treats this as UB per `[2d §6.1]`. This boundary is listed in §10.

### §6.3 Drop policy

**`drop_oldest`** (default):

- When the ring is full (producer would advance head past tail), the producer atomically advances the **consumer tail** by 1 (overwriting the oldest slot), then writes the new record into that slot.
- The overflow counter (`TapConfig::overflow_count`, a `mutable std::atomic<uint64_t>`) is incremented once per dropped record (`std::memory_order::relaxed`). The counter is cumulative; the consumer reads it via `tap_config.overflow_count.load(std::memory_order_relaxed)`.
- **Sequence number gap formula:** `TapRecord::sequence_number` increments on every produced record, including records that overwrite old slots. A gap of `delta` in the consumer-observed sequence numbers means the producer wrote `delta - 1` records that were overwritten before the consumer drained them. The consumer should correlate gaps with `TapConfig::overflow_count` to verify: `drops ≈ overflow_count_snapshot_end - overflow_count_snapshot_start` during a drain cycle. The sequence number is a monotone counter; it is not a FIX MsgSeqNum.

**`block`** (opt-in, non-stalling):

- When the ring is full, the producer:
  1. Calls `TapConfig::on_overflow_block` (if set) once — synchronously on the I/O strand. Keep it trivially fast.
  2. Retries the write once.
  3. If still full after the retry, falls back to `drop_oldest` behaviour and increments the overflow counter.
- This design is **non-stalling**: the producer never spins and never suspends. The session I/O strand is not blocked beyond one callback invocation and one retry attempt.
- `block` mode is not available for `Iox2Tap` (iceoryx2 uses its own publisher-side drop policy).
- The `tap_ring_overflow` error (§6.8) is returned only when `Iox2Tap::create()` fails — not from `tap_write`. `tap_write` is always `noexcept` and never surfaces this error.

**No gaps in `block` mode:** Because `block` mode falls back to `drop_oldest` on retry failure (and increments `TapConfig::overflow_count`), the sequence number still advances monotonically without gaps beyond those from the `drop_oldest` fallback. A pure `block` mode environment where the consumer always drains before the ring fills will observe no gaps and `tap_config.overflow_count.load() == 0`.

### §6.4 iceoryx2 topic contract

**Topic shape** (per iceoryx2 `ServiceDescription`):

| Field | Value | Derivation |
|---|---|---|
| Service name | `fixpp/<beginstring>/<sendercompid>/<targetcompid>[/<sessionqualifier>]` | Lower-cased; non-alphanumeric chars replaced with `-`. sessionqualifier omitted if empty. Max 128 chars. Auto-derived from SessionId if `Iox2TapConfig::service_name` is empty. |
| Instance name | `default` | Fixed in v1.0. |
| Event name | `tap-record` | Fixed in v1.0. |
| Payload type | `fixpp::tap::TapShmRecord` | Published as a fixed-size POD sample. `sizeof(TapRecord) + k_tap_record_max_bytes` bytes per sample. |
| Max publisher history | `Iox2TapConfig::max_publisher_history` | Default 16 samples. |

**Default `FallbackPolicy` is `in_process_ring`** (v1.0 decision per `[SYN §3.6 #22]`); other policies are opt-in.

**Publisher side** (the engine's `Iox2Tap`):

- One publisher per session. Created at session open; destroyed at session close.
- The publisher calls `publisher.loan_uninit()` to obtain a zero-copy SHM slot of `sizeof(TapShmRecord)` bytes (typed `Publisher<TapShmRecord>` — exactly one fixed-size sample per loan).
- Writes the `TapRecord` header into `slot.header` (setting `slot.header.bytes_len` to the actual byte count) and the FIX wire bytes into `slot.payload[0..bytes_len-1]`.
- Calls `publisher.send()`.
- If `publisher.loan_uninit()` fails (publisher buffer full — all `max_publisher_history` samples held by slow subscribers), `TapConfig::overflow_count` is incremented and the record is dropped (equivalent to `drop_oldest` behaviour at the iceoryx2 layer).

**Subscriber side** (external consumer process — binding v1.0 `fixppd` path):

- The subscriber creates a `Subscriber<TapShmRecord>` (iceoryx2 subscriber API) using the `ServiceDescription` returned by `Iox2Tap::service_description()`.
- `subscriber.receive()` returns an `Option<Sample<TapShmRecord>>`. The `Sample` owns the SHM slot; dropping the `Sample` returns the slot to iceoryx2.
- The subscriber reads `sample.header.bytes_len` bytes from `sample.payload[0..bytes_len-1]`.
- Lifetime: the payload bytes are valid as long as the `Sample` is held. Dropping the sample invalidates access.

**`fixppd` integration path (RC#2 — binding v1.0 decision):** `fixppd` subscribes to the iceoryx2 SHM topic using `Subscriber<TapShmRecord>` via the iceoryx2 subscriber API. `fixppd` does **not** include `<fixpp/tap/ring_buffer_tap.hpp>` or any other `<fixpp/tap/...>` C++ tap headers — only the iceoryx2 subscriber API and the `TapShmRecord` layout header are needed. This is boundary-compliant with `[arch §8]` / `[arch §2.3]`. The alternative of `RingBufferTap` + C++ drain from `fixppd` is a violation of the `[arch §8]` service-mode boundary and is not a supported `fixppd` integration path.

**Fallback when iceoryx2 is not running:**

| `FallbackPolicy` | Behaviour |
|---|---|
| `in_process_ring` (default) | `Iox2Tap` transparently creates a `RingBufferTap` using the same `TapConfig`. The session opens normally. `is_fallback_active()` returns `true`. |
| `error_on_open` | `Iox2Tap::create()` returns `error::tap_iox2_not_running`. The session does not open. |
| `drop_silently` | `Iox2Tap` is created; every `tap_write` call is a no-op. **Even under `drop_silently`, `TapConfig::overflow_count` is incremented for every suppressed record**, satisfying `[const §XIII.2]`. The "silent" aspect is that no error is returned; the counter (`tap_config.overflow_count.load(std::memory_order_relaxed)`) still advances so monitoring can detect data loss. |

**Reconnect behaviour:** If iceoryx2 becomes available after the session opens (e.g., the iceoryx2 daemon restarts), the `in_process_ring` fallback continues until the session is closed and reopened. Mid-session iceoryx2 reconnect is not supported in v1.0 (see §10).

**SVC-003 fallback note:** In gRPC-only mode (SVC-003, iceoryx2 not available), tap output is limited to in-process consumers; cross-process tap subscription via gRPC relay is a Phase 3 item. Specifically: when iceoryx2 is unavailable, the engine creates a `RingBufferTap` fallback (per `Iox2TapConfig::FallbackPolicy::in_process_ring`). `fixppd` has no way to drain this ring in v1.0 — it does not include `<fixpp/tap/ring_buffer_tap.hpp>` or any `<fixpp/tap/...>` C++ tap headers (per §7.4 / §7.5 / `[arch §8]`), and no C ABI tap-drain surface exists in v1.0 (per §5). The gRPC relay path for draining the in-process ring and forwarding tap data over `StreamMessages` (owned by `2j`) is deferred to Phase 3; see §5 and §10 Q2.

### §6.5 SyncCallbackTap latency caveat

> **IMPORTANT:** `SyncCallbackTap` is documented as caveat-emptor. The callback runs **synchronously on the session I/O strand**, inside the `tap::detail::tap_write` call. The session FSM, heartbeat, send, receive, and all coroutines waiting on the strand are all blocked until the callback returns.

**Recommended time budget:** ≤ 1 µs per callback invocation at the median. At 100,000 msg/s, a 1 µs callback adds 100 ms of pure serialised latency per second.

**Exception handling:** If the callback throws, `tap_write` catches the exception, increments `exception_count_`, and returns without propagating. The exception is logged at `Level::error` via the session's logger. Repeated throwing from a callback will surface in logs; the session continues operating.

**Permitted uses:** Conformance-test harnesses where the session produces messages at low rates and synchronous delivery is a simplification. One-shot diagnostics (log first 10 messages then switch to `RingBufferTap`). Integration tests asserting message content in-line.

**Prohibited uses:** Production message-rate workloads; any callback that performs I/O, acquires a lock shared with the session strand, or allocates from the global heap.

### §6.6 Allocation / exceptions / threading

**PMR arenas used** (see §8 for the full recap):

- `tap_ring_arena` (`TapConfig::tap_ring_arena`): session-lifetime; holds the `TapSlot` array. Allocated at session open; freed at session close.
- No per-message-lifetime arena is touched by the tap path. The message bytes are copied from the session's `message_arena`-backed `wire::View` into the `tap_ring_arena`-backed slot's payload window on the producer side; the `message_arena` is reset after `fromApp` returns without affecting the tap's copy.

**Exceptions:** The producer path is `noexcept`. Construction of `RingBufferTap` or `Iox2Tap` may throw `std::invalid_argument` for misconfigured parameters; this is construction-time, not hot-path.

**Threading:**
- **Producer:** always the session I/O strand (one writer). `tap_write` is never called from multiple threads concurrently per session.
- **Consumer (`RingBufferTap::drain`):** the user's consumer thread (one reader). Must not be the session I/O strand. The SPSC contract enforces this at the API level.
- **`SyncCallbackTap::callback_`:** called on the session I/O strand. The callback must not attempt to acquire a mutex also held by the session (deadlock).

### §6.7 Latency ceilings (Tier 1)

**Producer path (record write into ring):** ≤ 200 ns at P99 on the reference CI hardware (defined in `bench/baselines/README.md`), measured as the wall-clock duration of a single `tap::detail::tap_write` call under `TapDropPolicy::drop_oldest` on a `RingBufferTap` with `capacity = 4096`. **Achievable for messages ≤ 1500 bytes; for larger messages the ceiling scales linearly with copy size — see TS-8.**

The 200 ns ceiling accommodates:
- 1× `std::atomic` load (tail), 1× `std::atomic` store (head): ~5–10 ns each.
- 1× `memcpy` of up to 1500 bytes (typical FIX session-layer frame): at 40 GiB/s memory bandwidth ≈ 37 ns; at 4096 bytes ≈ 100 ns.
- `SessionId` copy (49 bytes): ~5 ns.
- `trace_context` copy (32 bytes): ~3 ns.
- Stack frame setup and cleanup: ~10 ns.
- Total estimated for ≤ 1500 bytes: ~70–100 ns; 200 ns gives ≥ 50% headroom.

For small messages (≤ 256 bytes, typical FX/equities session-layer frames), the copy time is ~6 ns; the ceiling will typically be ≤ 50 ns.

The benchmark gate (TS-8) enforces this ceiling at ±5% regression budget per `[const §VIII.2]`. For 4096-byte messages the measured P99 may exceed 200 ns on cache-cold paths; TS-8 measures at both sizes.

**Consumer drain callback latency:** not bounded by this doc. The consumer is user code; its latency is the user's responsibility. The ring decouples producer and consumer latency.

### §6.8 Errors introduced by this design

All new `fixpp::core::error` variants occupy the `[1100, 1199]` block reserved by `[2i §1.1]` / `[2i §4.3]` block layout table.

| C++ error variant | C ABI constant | Numeric | Description |
|---|---|---|---|
| `fixpp::core::error::tap_ring_overflow` | `FIXPP_ERR_TAP_RING_OVERFLOW` | 1100 | Reserved for future use. Not returned by `tap_write` (which is `noexcept`). May be used by a Phase 3 C ABI drain surface to indicate consumer buffer overflow. |
| `fixpp::core::error::tap_iox2_not_running` | `FIXPP_ERR_TAP_IOX2_NOT_RUNNING` | 1101 | `Iox2Tap::create()` called with `FallbackPolicy::error_on_open` and iceoryx2 is not reachable. Session open fails. |
| `fixpp::core::error::tap_invalid_config` | `FIXPP_ERR_TAP_INVALID_CONFIG` | 1102 | `TapConfig::capacity` is not a power of 2, or is zero; `SyncCallbackTap` constructed with a null callback; `Iox2TapConfig::service_name` exceeds 127 chars. Construction-time only. |
| `fixpp::core::error::tap_arena_exhausted` | `FIXPP_ERR_TAP_ARENA_EXHAUSTED` | 1103 | `tap_ring_arena` is a `null_memory_resource` or is otherwise exhausted during ring construction. Construction-time only. |

**Note on `tap_ring_overflow`:** In `drop_oldest` mode, overflow is not an error — it is a normal operational state signalled only via the overflow counter. In `block` mode, the producer is non-stalling and never blocks the strand (see §6.3). The `tap_ring_overflow` code is reserved for potential Phase 3 C ABI use.

---

## §7 Integration with adjacent modules

### §7.1 Session (`2d`): tap invocation in the dispatch loop and `SessionConfig` amendment

**`SessionConfig` tap field** (confirmed per `[2d §4.5]`):

The `[2d §4.5]` `SessionConfig` already declares:

```cpp
// ── Tap (locked by 2l) ──────────────────────────────────────────────
fixpp::tap::TapConsumer     tap_consumer;   // variant; default-constructed = no tap.
```

This doc confirms the field as-is. The "no tap" representation is default-constructed `TapConsumer` — i.e., `NoTap{}` (the first variant alternative, index 0). No ring is allocated and no write overhead is incurred when the user has not configured a tap. The `TapConfig` is carried as a separate field added by this doc's Appendix D amendment:

```cpp
// Amendment: new field to be added to [2d §4.5] SessionConfig
// (after the existing tap_consumer field — see Appendix D):
fixpp::tap::TapConfig tap_config {};
// Default: {.capacity=4096, .drop_policy=TapDropPolicy::drop_oldest, .max_message_bytes=4096,
//           .tap_ring_arena=nullptr, .on_overflow_block={}, .overflow_count=0}
```

Per `[arch §5.6]`, both fields are frozen at session open. This amendment is explicitly noted in Appendix D.

**Dispatch loop integration:**

The session dispatch loop (internal, `src/session/session_dispatch.cpp`) calls `tap::detail::tap_write` at two points per message:

1. **Inbound:** immediately after the `Framer` delivers a complete frame and before `fromAdmin` / `fromApp` is dispatched. The raw bytes are the framed wire bytes from the `message_arena`-backed I/O buffer.
2. **Outbound:** immediately after `Writer::commit()` finalises the outbound message bytes and before `Transport::async_write` delivers them.

The trace context is read synchronously from the session's `session_local<trace_context>` slot. The `co_await fixpp::current_trace_context` expression is **not** used here because `tap_write` is `noexcept` and not a coroutine context. Instead, the session dispatch loop reads the trace context via `session.get_trace_context()` ([2k App D §D.1]) — a synchronous non-suspending accessor that reads the `session_local<trace_context>` slot directly. The `session.get_trace_context()` synchronous accessor is added to `Session`'s public surface by `[2k App D §D.1]`; 2l consumes it here but does not independently amend `2d-threading.md` for this method.

```cpp
// Pseudocode — internal session dispatch path
// (not a coroutine; this runs on the session strand synchronously)
const auto& trace_ctx = session_.get_trace_context();  // synchronous read of session_local slot; see [2k App D §D.1]

if (!std::holds_alternative<tap::NoTap>(session_config_.tap_consumer)) {
    std::visit([&](auto& consumer) {
        tap::detail::tap_write(
            consumer,
            direction,
            wire_bytes,       // span into message_arena (inbound) or write_buffer (outbound)
            session_id_,
            effective_clock_.now(),
            trace_ctx,
            ++tap_sequence_number_
        );
    }, session_config_.tap_consumer);
}
```

The `tap_sequence_number_` is a `uint64_t` member of the internal `Session` object, incremented per tap write (not per FIX message sequence number — these are independent counters).

### §7.2 Wire (`2b`): what the producer copies from `wire::View`

**View lifetime contract** (from `[2b §6.6]`): all `wire::View` instances are flyweights over the session's `message_arena`-backed I/O buffer. The `message_arena` is reset after `fromApp` returns.

**What the producer copies:**

- The producer calls `tap_write` **before** `fromApp` dispatch (inbound) and does **not** hold or alias the `wire::View` after `tap_write` returns.
- `tap_write` copies the raw wire bytes (from `wire::View::data()` / the framed I/O buffer) into the current slot's `payload` window. After `tap_write` returns, the tap holds its own copy in the ring slot; the `wire::View` and `message_arena` are no longer referenced by the tap.
- **The tap never stores a `wire::View` instance.** The slot's `payload` bytes live in `tap_ring_arena`, not in `message_arena`. This is the lifetime isolation boundary: the tap's records survive `fromApp` and the subsequent `message_arena` reset.
- For outbound messages: `tap_write` is called with a span over the `Writer`-committed output buffer (allocated from the session's write buffer, not the `message_arena`). The copy into the slot's `payload` occurs before `async_write` is called; the write buffer is then recycled.

**Lifetime guard:** TS-10 (§9) verifies that no tap slot holds a dangling pointer into `message_arena` after the dispatch cycle. The guard uses debug-build generation counters from `[2b §5.5]`.

### §7.3 Logger (`2k`): sibling alignment

`TapRecord` and `fixpp::log::Record` are sibling abstractions per `[SYN §3.8]`. They share the following design properties:

| Property | `log::Record` (`2k`) | `TapRecord` (`2l`) |
|---|---|---|
| PMR backing | `LogConfig::ring_resource` | `TapConfig::tap_ring_arena` |
| Producer path | zero-alloc; MPSC ring | zero-alloc; SPSC ring |
| Consumer path | dedicated drain thread | user drain thread |
| Overflow policy | `drop_newest` (2k uses drop-newest, see `[2k §3.5]`) | `drop_oldest` (default) or `block` |
| Overflow counter | `std::atomic<uint64_t>` on `Logger` | `TapConfig::overflow_count` (`mutable std::atomic<uint64_t>`); read via `tap_config.overflow_count.load()` |

**Divergence from 2k:** The tap ring is SPSC (one session strand = one producer; one drain thread = one consumer). The logger ring is MPSC (multiple session strands can log simultaneously). This is intentional: each session has its own `TapConsumer` instance; tap messages are strictly per-session and do not need cross-session fan-in. The SPSC ring is simpler, cheaper (no CAS loop on the head), and sufficient.

**Separate arenas:** The tap ring (`tap_ring_arena`) and the logger ring (`LogConfig::ring_resource`) are intentionally **separate PMR resources**. They have different lifetimes (tap is session-scoped; logger is engine-scoped) and different scale (one tap ring per session; one logger ring for the entire engine). Coupling them would create a cross-lifetime dependency and complicate the arena cleanup ordering.

**`TapRecord` does NOT flow through the logger ring.** Tap records are not log records and do not go through `fixpp::log::Sink`. If a user wants to log a tapped message, they consume from `RingBufferTap::drain` and call `FIXPP_SLOG(...)` themselves.

### §7.4 C ABI (`2i`): no new symbols in v1.0

As documented in §5, no new `fixpp_tap_*` C ABI symbols are introduced in v1.0. The `[1100, 1199]` error block is reserved and partially occupied by §6.8 (4 variants; 95 slots remain for Phase 3 extensions).

The `fixppd` service module accesses tap **exclusively** via the iceoryx2 SHM subscriber path (binding v1.0 `fixppd` integration per RC#2 / §6.4). It subscribes using `Subscriber<TapShmRecord>` via the iceoryx2 API. It does **not** call `RingBufferTap::drain()` or include any `<fixpp/tap/...>` C++ tap headers. This is the boundary-compliant path per `[arch §8]` / `[arch §2.3]`.

### §7.5 Service / iceoryx2 (`2j`, `[arch §8.2]`)

The service-boundary rule from `[arch §8]` applies: `fixppd` accesses tap only through the iceoryx2 SHM topic directly — never through the C++ tap headers.

For the `fixppd` binary specifically:

- `fixppd` links `fixpp::capi` only, not the C++ engine umbrella per `[arch §7.4]`.
- It configures the session via the gRPC `OpenSession` RPC (owned by `2j`) with `tap_consumer = Iox2Tap(...)` encoded as a config parameter. The engine's C ABI layer translates the gRPC config into `SessionConfig` via the C++ API before session open — this translation occurs inside the engine's `fixpp::capi` thunks, not in `fixppd` source code.
- After session open, `fixppd` subscribes to the iceoryx2 SHM topic published by the engine's `Iox2Tap` instance, using the `ServiceDescription` returned via a C ABI accessor. The subscription uses only the iceoryx2 C/C++ subscriber API and the `TapShmRecord` layout — no `<fixpp/tap/...>` includes.
- `fixppd` **never** includes `<fixpp/tap/ring_buffer_tap.hpp>`, `<fixpp/tap/iox2_tap.hpp>`, or any other `<fixpp/tap/...>` header.

### §7.6 SWIG/Python (`2m`)

The Python tap subscription API is **not owned by this doc**. `2m` will specify how Python code subscribes to the tap ring buffer and what GIL implications apply. The expectation:

- Python consumers call `session.tap_consumer()` (a `2m` binding over the `SessionConfig::tap_consumer` field) to obtain a `RingBufferTap` wrapper.
- `RingBufferTap::drain(callback)` is exposed as a Python method; the GIL is reacquired before each callback invocation.
- `2m` owns the GIL reacquisition contract; this doc does not.

---

## §8 PMR Recap

| Component | `std::pmr::memory_resource*` | Arena name | Lifetime | Notes |
|---|---|---|---|---|
| `RingBufferTap` `TapSlot` array | `TapConfig::tap_ring_arena` | `tap_ring_arena` | Session (created at session open; destroyed at session close) | Pre-allocated: `capacity × sizeof(TapSlot)` bytes = `capacity × (sizeof(TapRecord) + k_tap_record_max_bytes)`. |
| `RingBufferTap::Impl` struct | `TapConfig::tap_ring_arena` | `tap_ring_arena` | Session | The pimpl is allocated from the arena at construction. |
| `Iox2Tap::Impl` struct | `TapConfig::tap_ring_arena` | `tap_ring_arena` | Session | Same arena; includes the fallback `RingBufferTap::Impl` if fallback is active. |
| iceoryx2 SHM samples (`TapShmRecord`) | iceoryx2 runtime (not PMR) | (iceoryx2 internal) | Subscriber-held | The iceoryx2 runtime manages SHM allocation of fixed-size `TapShmRecord` slots; not under PMR control. |
| `SyncCallbackTap::callback_` (`std::function`) | global heap (via `std::function`) | n/a | Session (destroyed with `SessionConfig`) | `std::function` may heap-allocate for large functors. This is construction-time only; not on the hot path. |

**`TapSlot` size:** `sizeof(TapRecord) + k_tap_record_max_bytes`. The `TapRecord` struct has:
- `uint8_t schema_version` (1 B)
- `TapDirection direction` (1 B)
- `bool truncated` (1 B)
- `uint8_t _pad0[5]` (5 B)
- `uint64_t sequence_number` (8 B)
- `fixpp::core::time_point timestamp` (~8 B; exact size from `[2d §4.1]`)
- `fixpp::otel::trace_context trace_ctx` (~32 B; exact size from `[2k §4.2]`)
- `SessionId session_id` (49 B)
- `std::size_t bytes_len` (8 B)

Approximate `sizeof(TapRecord)` ≈ 120–128 bytes depending on `time_point` and `trace_context` exact sizes; alignment padding may round up. Using 128 bytes as the working approximation:

**Total tap arena budget:** `capacity × (128 + 4096) + sizeof(Impl)`.

Default: `4096 × 4224 + ~256` ≈ **17.3 MiB**.

Users on low-rate sessions (session-layer admin messages only) should set `capacity = 256`, reducing the budget to ~1.1 MiB.

**Lifetime ordering at session close:**

1. `Session::close()` is called (per `[2d §5]` two-phase close model).
2. After phase 2 teardown (transport closed; `message_arena` reset; no pending `fromApp` calls), the `TapConsumer` variant is destroyed.
3. `TapConsumer` destruction frees the `tap_ring_arena` contents in reverse construction order.
4. The user's consumer thread must stop calling `drain()` before (or concurrent with) the session close. The recommended pattern: signal the drain thread before calling `session.close()`; wait for the drain thread to stop; then close the session. The SPSC contract requires the consumer to stop before the ring is destroyed.

---

## §9 Test Seams

All test seams live under `tests/tap/`.

**TS-1 — Conformance corpus with tap** (`tests/tap/test_tap_conformance.cpp`): Route all TC-001..TC-017 FIX session-layer conformance scenarios through `RingBufferTap`. After each scenario, drain the ring and verify: (a) every message appears in the correct order, (b) direction is correct for each message, (c) session IDs match, (d) sequence numbers are contiguous (no gaps), (e) message bytes are byte-for-byte identical to what the wire layer sent/received.

**TS-2 — `SyncCallbackTap` stall test** (`tests/tap/test_sync_callback_stall.cpp`): Configure a `SyncCallbackTap` with a callback that sleeps 10 ms. Drive the session at 100 msg/s. Verify: (a) the session stalls (heartbeat skew measurable), (b) the session recovers when the callback returns, (c) no session-level errors are raised (the stall is absorbed). This validates the caveat-emptor behaviour.

**TS-3 — `RingBufferTap` drop-oldest test** (`tests/tap/test_ring_drop_oldest.cpp`): Fill the ring past capacity (produce `capacity + 10` records without draining). Verify: (a) `tap_config.overflow_count.load()` equals 10, (b) the `capacity` most-recent records are accessible to the consumer (oldest dropped), (c) sequence numbers show a gap consistent with the gap formula in §6.3 (gap of `delta` means `delta - 1` overwritten records).

**TS-4 — `RingBufferTap` block-mode test** (`tests/tap/test_ring_block_mode.cpp`): Configure `TapConfig::drop_policy = TapDropPolicy::block`. Fill the ring. Verify: (a) when `TapConfig::on_overflow_block` is set, it is called when the ring is full; (b) after one retry, if the ring is still full, the producer falls back to `TapDropPolicy::drop_oldest` and increments `TapConfig::overflow_count`; (c) no producer spin or strand stall occurs; (d) the producer-side `tap_write` call completes in bounded time.

**TS-5 — `Iox2Tap` unit test (mocked iceoryx2)** (`tests/tap/test_iox2_tap_mock.cpp`): Using a process-local iceoryx2 mock, publish 1000 records and subscribe from a second thread using `Subscriber<TapShmRecord>`. Verify: (a) zero copies on the producer thread (via `mallocnesia`), (b) subscriber receives all records in order, (c) `sample.payload[0..bytes_len-1]` content matches the original wire bytes, (d) `sample.header.bytes_len` matches the original byte count.

**TS-6 — `Iox2Tap` fallback test** (`tests/tap/test_iox2_fallback.cpp`): Run with iceoryx2 unavailable. Verify all three `FallbackPolicy` modes behave as documented: `in_process_ring` produces records in the fallback ring; `error_on_open` returns `error::tap_iox2_not_running`; `drop_silently` silently discards all records but `tap_config.overflow_count.load() > 0` (counter is incremented for each suppressed record per `[const §XIII.2]`).

**TS-7 — iceoryx2 subscriber reconnect test** (`tests/tap/test_iox2_reconnect.cpp`): Start a session with `Iox2Tap`. Subscribe from a second process using `Subscriber<TapShmRecord>`. Disconnect the subscriber mid-session (kill the subscriber process). Reconnect. Verify: (a) the publisher continues operating, (b) the reconnected subscriber receives new records, (c) `tap_config.overflow_count.load()` reflects records dropped during the disconnection window.

**TS-8 — Ring buffer latency regression** (`bench/tap/bench_ring_write.cpp`): Google Benchmark: measure `tap::detail::tap_write` latency (mean, P50, P99, P999) for messages of size 64, 256, 512, 1024, 1500, 4096 bytes at ring capacities 256 and 4096. Verify P99 ≤ 200 ns for messages ≤ 1500 bytes; record the measured P99 for 4096-byte messages as a baseline (may exceed 200 ns; acceptable per §6.7). Baseline stored in `bench/baselines/tap_ring_write.json`. ±5% regression budget per `[const §VIII.2]`.

**TS-9 — Arena allocation guard** (`tests/tap/test_tap_alloc_guard.cpp`): Inject `std::pmr::null_memory_resource()` as the global default PMR resource. Drive a session through 10,000 inbound + outbound messages with `RingBufferTap` configured with an explicit pre-allocated arena. Verify no global heap allocation occurs on the producer path (`mallocnesia` interceptor detects any `malloc` / `::operator new` call and triggers test failure). This confirms the zero-global-alloc invariant from §6.1.

**TS-10 — `wire::View` lifetime guard** (`tests/tap/test_wire_view_lifetime.cpp`): After `tap_write` completes (inbound path), trigger a `message_arena` reset (simulating the post-`fromApp` reset). Verify that the tap slot's `payload` bytes still hold valid data (they are copied into the `TapSlot` in `tap_ring_arena`, not into `message_arena`). Uses the debug-build generation-counter mechanism from `[2b §5.5]` to assert that no `wire::View` reference is held by the ring after the arena reset.

**TS-11 — Concurrent sessions SPSC safety** (`tests/tap/test_concurrent_sessions.cpp`): Launch N=8 sessions simultaneously, each with its own `RingBufferTap`. Drive each session from a separate I/O strand thread. Drain each ring from a separate consumer thread. Verify no data corruption (byte-compare each record against what the session wrote). Run under TSan.

**TS-12 — OTel trace correlation** (`tests/tap/test_tap_trace_correlation.cpp`): Configure a session with an initial `trace_context` (non-zero `trace_id` and `span_id`). Drive the session through 100 messages. Drain the ring. Verify every `TapRecord::trace_ctx` matches the session's `initial_trace_context`. Verify that a session with no trace context produces `TapRecord::trace_ctx` with all bytes zero.

---

## §10 Open Questions

| # | Question | Owner | Status |
|---|---|---|---|
| 1 | Python tap subscription GIL handling: does `RingBufferTap::drain(callback)` reacquire the GIL before each callback invocation from a non-Python thread? | **2m** | Deferred |
| 2 | C ABI tap symbols (`fixpp_tap_subscribe`, `fixpp_tap_drain`) for non-C++ external tools that cannot use iceoryx2 | **2i** Appendix D or Phase 3 C ABI extension doc | Deferred to Phase 3 |
| 3 | iceoryx2 version pinning in `conanfile.py` and the `FIXPP_ENABLE_IOX2` CMake feature flag | Phase 3 CMake/build doc | Deferred |
| 4 | Multi-publisher scenario: one tap publisher per session vs. a shared engine-level fan-out publisher. Current design: one publisher per session (simpler; no cross-session coupling). | Open for Phase 4 review; current v1.0 decision is one-per-session. | Open |
| 5 | Mid-session iceoryx2 reconnect: current behaviour is "fallback ring continues until session close-reopen". A future version could attempt to re-register the publisher when iceoryx2 becomes available. | Phase 4 iceoryx2 integration doc | Deferred |
| 6 | `SyncCallbackTap` guard: should the engine enforce a timeout on the callback (e.g., via `alarm(2)` or a watchdog thread) to prevent unbounded session stalls? | Future hardening; constitutionally prohibited as a v1.0 requirement. | Deferred post-v1.0 |
| 7 | Tap record schema versioning for iceoryx2: if `TapRecord` layout changes in v1.x, `schema_version` (added in v0.2) enables subscriber version checks at zero protocol overhead. The document-at-first-layout-change discipline applies. | 2i Appendix D (C ABI stability model analogy) | Addressed in v0.2 (schema_version field added); versioning discipline to be formalised at Phase 4. |
| 8 | User-defined tap backends (custom `TapConsumer` variant arms): the `TapConsumer` variant is intentionally closed for v1.0. The recommended path for custom backends is to drain `RingBufferTap` and re-publish. A future `CustomTap` value type (function-pointer + `void*` context, zero allocation, no vtable) could be added in a v1.x amendment. | Post-v1.0 extensibility item. | Deferred post-v1.0 |
| 9 | SPSC safety under `direct_executor` mode bypass: if a caller bypasses the session strand (violating `[2d §6.1]` `already_serialized_executor` attestation) and calls `Session::send()` concurrently, the SPSC tap ring safety is the caller's responsibility. Debug-build assertions should detect this; runtime enforcement is deferred. | UB per `[2d §6.1]`; documented boundary. | Open for Phase 4 hardening. |

---

## §11 Hand-off

**Unblocked by 2l sign-off:**

- **2m** (SWIG/Python): can now specify the `RingBufferTap::drain` callback semantics for Python, GIL reacquisition contract, and the Python `TapRecord` binding shape.
- **Phase 3 (tooling / CI)**: can implement all 12 test seams (TS-1..TS-12) and the `bench_ring_write` benchmark. Can also implement the `FIXPP_ENABLE_IOX2` CMake flag and the iceoryx2 Conan dependency.
- **Phase 4 (service mode)**: can wire the `Iox2Tap` publisher in the `fixppd` binary. `2j` can finalise the `OpenSession` gRPC RPC fields for tap configuration.
- **2i Appendix D**: tap error codes `[1100, 1103]` can be added to `c_api/error.h` (they are already reserved; this is a no-op amendment to add the `FIXPP_ERR_TAP_*` constants).

---

## Appendix A — Catalogue Row Coverage

| ID | Title | This doc's contribution | Gap / cross-doc handoff |
|---|---|---|---|
| S-036 | Session tap / monitoring hook — pluggable callback to capture all in/out FIX messages for tooling | Owned end-to-end: `TapRecord`, `TapConsumer` variant, `TapConfig`, `RingBufferTap`, `SyncCallbackTap`, producer invocation in dispatch loop. | None; fully discharged by this doc. |
| NFR-014 | Session tap / monitoring hook for protocol analysis and conformance testing | Owned: `RingBufferTap::drain` API enables conformance harnesses. Test seam TS-1 routes all TC-001..TC-017 through the tap. | None; fully discharged. |
| COM-008 | FIX session monitoring / protocol analyzer | Owned: the tap consumer API is the protocol-analyzer integration point. `Iox2Tap` enables zero-rebuild external-tool attachment. | `2j` owns the gRPC `StreamMessages` relay path that feeds external tools when iceoryx2 is unavailable. |
| SVC-002 | iceoryx2 data plane — zero-copy SHM publish/subscribe for hot-path FIX messages | Owned: `Iox2Tap` is the SVC-002 publisher. Topic shape, `TapShmRecord` wire format, ownership semantics, backpressure, fallback all specified in §4.5 and §6.4. | Phase 3 CMake doc owns iceoryx2 version pinning. `2j` owns the `fixppd` daemon wiring. |
| SVC-003 | gRPC-only fallback mode when iceoryx2 unavailable | Boundary concern co-owned: `Iox2TapConfig::FallbackPolicy::in_process_ring` defines the engine-side fallback. The gRPC relay from the fallback ring to external consumers is owned by `2j`. | `2j` owns the `StreamMessages` RPC and the `fixppd` drain-and-relay logic. |

**`SessionConfig` amendment:** This doc requires one new field in `SessionConfig` (owned by `2d`): `tap_config: TapConfig`. The existing `tap_consumer: TapConsumer` field is already declared in `[2d §4.5]` — confirmed as-is. The `tap_config` field is a new addition documented in Appendix D.

---

## Appendix B — Normative References

Per `[const §VI.5]` exact-citation rule: every normative reference cited in this doc with exact section pointer.

| Reference | Exact pointer | Architectural impact |
|---|---|---|
| fixpp Architecture — tap module surface | `[arch §4.9]` | `TapConsumer` variant shape; `TapConfig` drop policy; tap reads wire views. |
| fixpp Architecture — backpressure | `[arch §5.8]` | `drop-oldest` permitted on tap paths; banned on session paths. |
| fixpp Architecture — iceoryx2 data plane | `[arch §8.2]` | Topic shape, ownership, fallback — all in 2l. |
| fixpp Architecture — plugin pattern | `[arch §6]` | `TapConsumer` as a compile-time-selected variant. |
| fixpp Architecture — error model | `[arch §5.3]` | `expected_t<T>`; no exceptions hot path; C ABI translation. |
| fixpp Architecture — trace context | `[arch §5.4]` | `TapRecord::trace_ctx` from session-local slot; synchronous read via `session.get_trace_context()` ([2k App D §D.1]). |
| fixpp Architecture — module dependency rules | `[arch §2.3]` | `tap/` may include `core/` and `wire/` (read-only views); not `session/`. |
| fixpp Architecture — service-mode boundary | `[arch §8]` | `fixppd` accesses tap via iceoryx2 SHM only; never includes `<fixpp/tap/...>` headers. |
| fixpp Architecture — hand-off to 2a–2m | `[arch §10]` row 2l | Scope confirmation. |
| Constitution — exact-citation rule | `[const §VI.5]` | Every reference in this doc is exact (not vague). |
| Constitution — zero allocation on hot path | `[const §VIII.5]` | Zero global-heap allocation on producer path; PMR in-place writes permitted. |
| Constitution — drop-oldest on telemetry paths | `[const §XIII.2]` | Drop-oldest permitted on tap paths with recorded counter. |
| Constitution — ≤5 pure-virtual rule | `[const §XIV.2]` | `TapConsumer` is a variant (no virtual); cap does not apply. Noted explicitly. |
| Constitution — drop-oldest banned on session paths | `[const §XV.15]` | Drop-oldest is scoped to tap path; session message dispatch uses `block` or `disconnect-and-recover`. |
| SYNTHESIS — session-tap consumer API decision | `[SYN §3.6 #22]` | Ring buffer default; iceoryx2 opt-in; sync callback caveat-emptor. |
| SYNTHESIS — sibling abstractions | `[SYN §3.8]` | Tap ring and logger are sibling abstractions; intentionally uncoupled at API level. |
| 2b wire — view lifetime contract | `[2b §6.6]` | Wire views are message-arena-lifetime; tap copies bytes into slot payload before message_arena reset. |
| 2b wire — debug generation counters | `[2b §5.5]` | Used by TS-10 lifetime guard. |
| 2d threading — `SessionConfig` | `[2d §4.5]` | `tap_consumer` field confirmed as-is; `tap_config` field added by Appendix D amendment. |
| 2d threading — trace context + session_local | `[2d §4.6]` | `TapRecord::trace_ctx` captured via synchronous `session.get_trace_context()` (reads `session_local<trace_context>` slot; method added to `Session` surface by `[2k App D §D.1]`). |
| 2k logger — `Session::get_trace_context()` accessor | `[2k App D §D.1]` | `Session::get_trace_context() const noexcept` added to `Session`'s public surface by 2k; consumed by 2l dispatch loop and §7.1 pseudocode. 2l does not duplicate this amendment. |
| 2d threading — effective clock | `[2d §7.9]` | `TapRecord::timestamp` from `effective_clock.now()`. |
| 2d threading — per-session strand SPSC grounding | `[2d §5.1]` | SPSC safety grounded in per-session strand serialisation. |
| 2i C ABI — error block layout | `[2i §1.1]` / `[2i §4.3]` | `[1100, 1199]` reserved for 2l; §6.8 occupies 4 codes. |
| 2k logger — sibling note | `[2k §7]` | Logger and tap are intentionally uncoupled at API level (see `[2k §7]` — "Integration with adjacent modules", `[2l]` tap paragraph). |
| FIX Session Layer — session identification | `[FIX-SL §4.3]` | SenderCompID / TargetCompID / BeginString uniquely identify a FIX session; `SessionId` struct carries these plus optional qualifier. |

---

## Appendix C — Convergence Log (v0.1 → v0.2)

### Root causes (addressed first; collapse multiple findings)

| Root cause | Title | Findings collapsed | Recommended fix | Resolution in v0.2 |
|---|---|---|---|---|
| RC#1 | Storage model contradiction — `TapRecord::bytes` pointer provenance and lifetime | Codex P1-1; Opus N-P1-2 | Declare model B (fixed circular slab) as normative. Introduce `TapShmRecord` for iceoryx2. Split `SyncCallbackTap` callback type to pass separate header + span. | **Done.** §4.2 rewritten: canonical model B (fixed `TapSlot` = `TapRecord + payload[k_tap_record_max_bytes]`). `TapShmRecord` introduced in §4.5. `SyncCallback` signature changed to `void(const TapRecord&, std::span<const std::byte>)`. §4.4 drain signature updated accordingly. "Bump-allocates" language removed throughout. §6.2 and §8 updated to match. |
| RC#2 | Service-mode boundary incoherence — `fixppd` integration path | Codex P1-3 (partially); Codex P1-5 | Declare `Iox2Tap` subscriber as binding v1.0 `fixppd` integration. Add `BeginString` to iceoryx2 service name. Remove contradictory `RingBufferTap` + C++ drain claim for `fixppd`. | **Done.** §4.5, §6.4, §7.4, §7.5 rewritten to declare `Iox2Tap` subscriber (via `Subscriber<TapShmRecord>`) as the sole `fixppd` integration path. Service name updated to `fixpp/<beginstring>/<sender>/<target>[/<qualifier>]` in §4.5 and §6.4. `fixppd` + C++ tap header claim removed everywhere. |

### Per-finding resolution table

| Finding | Title | Action | Section changed |
|---|---|---|---|
| Codex P1-1 | `TapRecord` byte-ownership internally contradictory | **Fixed via RC#1.** Canonical model B adopted. §4.2 ownership contract, §4.4 drain signature, §6.2 slab model, §8 arena budget all aligned. | §4.2, §4.4, §6.2, §8 |
| Codex P1-2 | `SessionConfig` tap fields contradict signed-off `[2d §4.5]` | **Fixed.** `tap_consumer` field confirmed as-is per `[2d §4.5]` (non-optional `TapConsumer` variant). `tap_config` added as a new field via a coordinated amendment in Appendix D (separate from the existing field). Dropped `std::optional<>` wrapper. | §7.1, Appendix D |
| Codex P1-3 | iceoryx2 topic naming collides for multi-session engines | **Fixed via RC#2.** `BeginString` added to service name: `fixpp/<beginstring>/<sender>/<target>[/<qualifier>]`. SessionQualifier handling documented. Length arithmetic verified (≤ 55 chars for auto-derived names). | §4.5, §6.4 |
| Codex P1-4 | Block mode spin-wait can stall the engine strand | **Fixed.** Spin-wait removed. `block` mode is now: call `on_overflow_block` callback once, retry once, fall back to `drop_oldest` if still full. Producer is always non-stalling and non-spinning. §6.3 rewritten. `tap_ring_overflow` error repurposed for Phase 3 reserved use (not from `tap_write`). | §4.3, §6.3, §6.8 |
| Codex P1-5 | Service-mode boundary contradicted by `fixppd` wiring | **Fixed via RC#2.** Single coherent `fixppd` path: `Iox2Tap` subscriber via iceoryx2 API only. RingBufferTap + C++ drain path for `fixppd` removed. | §7.4, §7.5 |
| Codex P2-6 | `TapConsumer` variant is a closed set; no extensibility | **Addressed.** Explicitly documented as intentionally closed for v1.0 in §1.1 and §3.5. Added to §10 as post-v1.0 item (Q8). Architecture table note clarified. | §1.1, §3.5, §10 |
| Codex P2-7 | SPSC producer assumption not grounded in `[2d]` threading model | **Fixed.** §6.2 now explicitly cites `[2d §5.1]` per-session strand as the SPSC guarantee. `direct_executor` mode and bypass UB noted. Added to §10 Q9. | §6.2, §10 |
| Codex P2-8 | `SyncCallbackTap` `bytes` meaning is context-dependent | **Fixed via RC#1.** `SyncCallback` signature now `void(const TapRecord&, std::span<const std::byte>)` — separate header and span. `TapRecord` retains stable ring/SHM semantics. `SyncCallbackTap` comment updated. §4.6 explicit span lifetime note added. | §4.6 |
| Codex P2-9 (downgraded P2→P3 by Opus) | 200 ns P99 ceiling optimistic for 4096B copies | **Fixed (P3 editorial).** Added qualifying sentence to §6.7: "Achievable for messages ≤ 1500 bytes; for larger messages the ceiling scales linearly with copy size — see TS-8." TS-8 updated to test both 1500 B and 4096 B cases. | §6.7, §9 TS-8 |
| Codex P2-10 | `drop_silently` overflow counter stays 0 — unobservable data loss | **Fixed.** `drop_silently` now increments `overflow_count` for every suppressed record. §6.4 fallback table and `FallbackPolicy::drop_silently` comment updated. TS-6 updated. | §4.5, §6.4, §9 TS-6 |
| Opus N-P1-1 | `co_await fixpp::current_trace_context` in non-coroutine context | **Fixed.** Replaced with synchronous `session.get_trace_context()` call in §7.1 pseudocode. Added note: `2k` owns the `get_trace_context()` accessor definition (per `[2d §4.6]`); 2l borrows it as a synchronous read of the `session_local` slot. §3.7 updated to reflect synchronous access. | §3.7, §7.1 |
| Opus N-P1-2 | `Iox2Tap` variable-length SHM payload is UB for typed `Subscriber<TapRecord>` | **Fixed via RC#1.** `TapShmRecord` introduced as a fixed-size `{ TapRecord header; std::byte payload[k_tap_record_max_bytes]; }` struct. Publisher uses `Publisher<TapShmRecord>`; subscriber uses `Subscriber<TapShmRecord>`. No out-of-bounds pointer arithmetic. §4.5 and §6.4 updated. | §4.5, §6.4 |
| Opus N-P1-3 | `TapRecord::sequence_number` gap semantics internally contradictory | **Fixed.** §4.2 and §6.3 rewritten: sequence_number increments on every produced record (including overwritten ones). Gap formula documented: gap of `delta` = `delta - 1` overwritten records. "Not for dropped records" language removed. TS-3 updated to match. | §4.1 (§4.2 intro prose), §6.3, §9 TS-3 |
| Opus N-P2-1 | `static_assert(sizeof(SessionId) == 45)` arithmetic error | **Fixed.** `SessionId` fields revised to `sender[16] + sender_len(1) + target[16] + target_len(1) + begin_string[10] + qualifier_len(1) + qualifier[4]` = 49 bytes. `begin_string` narrowed from 12 to 10 to match `[FIX-SL §4.3]` max `BeginString` length. `qualifier` field added for collision-free service naming. `static_assert` replaced with upper-bound `<= 64` check. | §4.2 |
| Opus N-P3-1 | `[SYN §3.6 Q22]` citation form inconsistent; should be `#22` | **Fixed.** All `[SYN §3.6 Q22]` → `[SYN §3.6 #22]` throughout the doc (header metadata, §1, §3.4, §3.5, §3.8, Appendix B, etc.). | Throughout |
| Opus N-P3-2 | `TapRecord` carries no schema version field | **Fixed.** `uint8_t schema_version {1}` added as first field of `TapRecord`. §6.4 documents subscriber check. §10 Q7 updated. | §4.2, §6.4, §10 |
| Opus N-P3-3 (downgraded from Codex P2-9) | 200 ns P99 ceiling optimistic | **Fixed** (combined with Codex P2-9 fix above). | §6.7, §9 TS-8 |
| Codex P3-11 | `Iox2Tap` fallback default not stated normatively in prose | **Fixed.** Added sentence to §6.4: "Default `FallbackPolicy` is `in_process_ring` (v1.0 decision per `[SYN §3.6 #22]`)." | §6.4 |
| Codex P3-12 | Tap error block cites `[2i §1.1]` only; should also cite `[2i §4.3]` | **Fixed.** §3.6 and §5 and §6.8 all cite `[2i §1.1]` / `[2i §4.3]` block layout table. | §3.6, §5, §6.8 |

### "Disagree" items

No Codex or Opus findings were marked "Disagree" in the adversarial review. All findings were confirmed at P1, P2, or P3 (with one downgrade from P2 to P3 for Codex P2-9 / N-P3-3 — applied as a P3 fix rather than disagreement).

### Net-effect summary

**Structural changes in v0.2:**

1. **Storage model canonicalised (RC#1):** The fixed circular slab (`TapSlot = TapRecord + payload[k_tap_record_max_bytes]`) is now the single normative model. All three competing descriptions in v0.1 (bump-allocation, ring-lifetime pointer, drain-cycle invalidation) have been resolved into one consistent contract. The `SyncCallbackTap` now receives a separate `(header, span)` pair instead of sharing the ring-backed `TapRecord` struct.

2. **`TapShmRecord` introduced (RC#1 / N-P1-2):** The iceoryx2 SHM wire format is now a typed fixed-size struct `TapShmRecord { TapRecord header; std::byte payload[k_tap_record_max_bytes]; }`. This eliminates the UB of variable-length SHM slot access via out-of-bounds pointer arithmetic.

3. **Service-mode boundary coherent (RC#2):** `fixppd` integration path is now singular and boundary-compliant: iceoryx2 `Subscriber<TapShmRecord>` only. The `RingBufferTap` + C++ drain claim for `fixppd` is removed.

4. **iceoryx2 service name globally unique:** `BeginString` added to the naming formula; `SessionQualifier` handling documented. Topic collision for multi-session engines eliminated.

5. **Block mode non-stalling:** Spin-wait removed. `block` mode is now a bounded single-retry with `drop_oldest` fallback. Strand-deadlock risk eliminated.

6. **`SessionConfig` amendment aligned with `[2d §4.5]`:** `tap_consumer` confirmed as non-optional variant per 2d. `tap_config` added as a new coordinated amendment (not a rewrite of the existing field).

7. **`co_await` compile error fixed:** Dispatch loop pseudocode uses synchronous `session.get_trace_context()` instead of `co_await fixpp::current_trace_context`.

8. **`SessionId` `static_assert` fixed:** Arithmetic corrected; `begin_string[10]` and `qualifier[4]` fields added; upper-bound check used.

9. **`sequence_number` gap semantics clarified:** Increments on every produced record; gap formula documented unambiguously.

10. **`schema_version` field added to `TapRecord`:** Forward compatibility for iceoryx2 subscribers across layout changes.

11. **`drop_silently` overflow counter fixed:** Counter now incremented even under `drop_silently` fallback.

12. **Citation form normalised:** All `[SYN §3.6 Q22]` → `[SYN §3.6 #22]`.

**Confirmed sound (not changed structurally):**
- The three-variant `TapConsumer` design (`RingBufferTap | Iox2Tap | SyncCallbackTap`).
- The PMR arena model in §8 (already correctly described the circular slab).
- The test seam inventory (TS-1..TS-12), updated for the storage model and new contracts.
- The error code block (`[1100, 1199]`) occupancy and reservation model.
- The `[const §XIII.2]` / `[const §XV.15]` backpressure policy alignment.
- The sibling relationship with 2k logger (SPSC vs MPSC, separate arenas, no API coupling).

---

## Appendix C — Convergence Log (v0.2 → v0.3)

### Root causes (addressed first; collapse multiple findings)

| Root cause | Title | Findings collapsed | Resolution in v0.3 |
|---|---|---|---|
| RC#1 | Phantom `bytes` field references (residual from v0.2 RC#1 fix) | Codex P1-1 (round 2) | **Done.** All references to `header.bytes`, `TapRecord::bytes`, and "bytes field in header" removed from §4.5 `TapShmRecord` comment block and §6.4 publisher/subscriber prose. `TapRecord` has no `bytes` field. §4.5 comment now reads "sets `slot.header.bytes_len` to the actual byte count"; §6.4 subscriber side now reads "reads `sample.header.bytes_len` bytes from `sample.payload[0..bytes_len-1]`". Appendix C RC#1 "Done" text updated to reflect that phantom references were also purged in v0.3. |
| RC#2 | Overflow counter ownership incoherence + `NoTap` sentinel defect | Codex P1-2, Codex P2-5 (escalated P1), N-P1-1, N-P2-1 | **Done.** Three-part fix: (a) `overflow_counter_ptr` removed from `TapConfig`; replaced with `mutable std::atomic<uint64_t> overflow_count = 0` directly on `TapConfig`. No dangling pointer, no post-construction mutation of a value type. (b) `NoTap {}` added as first variant arm of `TapConsumer`: `std::variant<NoTap, RingBufferTap, Iox2Tap, SyncCallbackTap>`. Default-constructed `TapConsumer` = `NoTap{}`. Dispatch loop guard updated to `!std::holds_alternative<tap::NoTap>(tap_consumer)`. (c) All counter references unified: `TapConfig::overflow_count` (the `mutable std::atomic<uint64_t>`) read via `tap_config.overflow_count.load(std::memory_order_relaxed)`. §3.2, §4.3, §4.7, §6.3, §6.4, §7.1, §7.3, TS-3, TS-4, TS-6, TS-7, Appendix D all updated. |
| RC#3 | Appendix D fidelity failure | Codex P1-3 (round 2) | **Done.** Appendix D §D.1 regenerated from scratch after RC#1 and RC#2 fixes. Uses exact field names from §4.3: `capacity`, `drop_policy`, `max_message_bytes`, `tap_ring_arena`, `on_overflow_block`, `overflow_count`. `ring_capacity` erased. `overflow_counter_ptr` and `iox2_service_name` removed (former eliminated by RC#2; latter moved to `Iox2TapConfig::service_name` per Codex P3-9 fix). Before/After blocks use the exact `[2d §4.5]` line 605–606 content as the baseline. Default values match the §4.3 summary exactly. `NoTap{}` default annotated on the `tap_consumer` line. |

### Per-finding resolution table (v0.2 → v0.3)

| Finding | Title | Action | Section(s) changed |
|---|---|---|---|
| Codex P1-1 (round 2) | Phantom `TapRecord::bytes` field referenced in normative iceoryx2 contract | **Fixed via RC#1.** §4.5 `TapShmRecord` comment block and §6.4 publisher/subscriber prose purged of all `header.bytes` / `TapRecord::bytes` references. | §4.5, §6.4 |
| Codex P1-2 (round 2) | Overflow counter naming/ownership contradictions | **Fixed via RC#2.** `overflow_counter_ptr` removed from `TapConfig`; `mutable std::atomic<uint64_t> overflow_count` added in its place. §3.2 counter-ownership prose updated. All three inconsistent identifiers unified to `TapConfig::overflow_count`. | §3.2, §4.3, §4.3 summary, §6.3, §6.4 |
| Codex P1-3 (round 2) | Appendix D is not byte-faithful to v0.2 `TapConfig` | **Fixed via RC#3.** Appendix D regenerated: exact field names from §4.3, correct `NoTap{}` default on `tap_consumer`, full default-values summary. `ring_capacity` → `capacity`; `iox2_service_name` removed. | Appendix D §D.1 |
| Codex P2-4 (round 2) | Appendix C makes at least one incorrect "Done" claim | **Fixed.** RC#1 "Done" text in Appendix C updated: phantom prose purging noted explicitly. v0.2 Appendix C RC#1 description now says "payload lifetime / `bytes_len`" language; v0.3 log entry confirms the residual phantom references were also eliminated. | Appendix C (v0.1→v0.2 RC#1 row updated, v0.2→v0.3 RC#1 row added) |
| Codex P2-5 (round 2; escalated P1 by Opus) | "No tap" representation relies on undocumented `RingBufferTap(capacity=0)` sentinel | **Fixed via RC#2.** `NoTap {}` is now the first variant arm of `TapConsumer`. §7.1 "no tap representation" updated: "default-constructed `TapConsumer` = `NoTap{}`". Dispatch loop guard: `!std::holds_alternative<tap::NoTap>(tap_consumer)`. The capacity=0 sentinel claim is removed. | §4.7, §7.1 |
| N-P1-1 (Opus round 2) | `TapConfig::overflow_counter_ptr` is a dangling-pointer-by-design field | **Fixed via RC#2.** `overflow_counter_ptr` removed entirely from `TapConfig`. `mutable std::atomic<uint64_t> overflow_count` added directly on `TapConfig`. No pointer, no aliasing hazard, no post-construction mutation of a value type. | §4.3 |
| N-P2-1 (Opus round 2) | `drop_silently` fallback table row names non-existent `TapConfig::overflow_count` (as a field) | **Fixed.** §6.4 fallback table `drop_silently` row now reads: "`TapConfig::overflow_count` is incremented for every suppressed record; read via `tap_config.overflow_count.load(std::memory_order_relaxed)`". | §6.4 |
| N-P2-2 (Opus round 2) | TS-3 sequence-number gap formula cite is "§4.1" (TapDirection enum) instead of §6.3 | **Fixed.** TS-3 now cites "§6.3" for the gap formula. §4.2 `TapRecord` comment cross-reference also fixed: "See §4.1 and §6.3" → "See §6.3". | §4.2, §9 TS-3 |
| Codex P3-7 (round 2) | `TapDropPolicy` vs `TapDropPolicy::block` naming inconsistency in TS-4 | **Fixed.** TS-4 now references exact enum names `TapDropPolicy::block` and `TapDropPolicy::drop_oldest` and field name `TapConfig::on_overflow_block`. | §9 TS-4 |
| Codex P3-8 (round 2) | Appendix B reference pointer non-canonical (`[2k §7 integration with 2l]`) | **Fixed.** Appendix B updated to `[2k §7]` (the exact section title "§7 Integration with adjacent modules") with a clarifying note pointing to the `[2l]` tap paragraph in that section. | Appendix B |
| Codex P3-9 (round 2) | `TapConfig::iox2_service_name` vs `Iox2TapConfig::service_name` duplication | **Fixed.** `iox2_service_name` removed from `TapConfig`. iceoryx2 service name is now exclusively configured via `Iox2TapConfig::service_name` (§4.5). `TapConfig` header comment, §4.3 struct definition, §4.3 default-values summary, and Appendix D all updated. One definition; no precedence ambiguity. | §4.3, Appendix D |
| Codex P3-10 (round 2) | Minor cross-reference typo: sequence-number "See §4.1" | **Fixed via N-P2-2 fix above.** "See §4.1 and §6.3" → "See §6.3" in §4.2 `TapRecord` comment. | §4.2 |
| N-P3-1 (Opus round 2) | `TapConfig` mixes user-settable and internal-only fields without annotation | **Fixed.** `TapConfig` struct header comment now explicitly separates user-settable fields (`capacity`, `drop_policy`, `max_message_bytes`, `tap_ring_arena`, `on_overflow_block`) from the runtime-managed field (`overflow_count`). Inline section dividers added in the struct. | §4.3 |

### "Disagree" items (v0.2 → v0.3)

| Finding | Opus verdict | Reasoning | Action |
|---|---|---|---|
| Codex P2-6 (round 2) — iceoryx2 service-name max-length constraint (100 vs 128) | **Disagree; downgraded to P3** | v0.2 is internally consistent at 128 chars throughout. The 100-char figure appeared only in round-1 Codex focus-area recap, not in any iceoryx2 source reference. The worst-case auto-derived name is 55 chars (verified in §4.5). The `tap_invalid_config` error makes the 128-char constraint explicit at runtime. No design correctness issue. | No structural fix applied. Recorded in Appendix B note. |

### Net-effect summary (v0.2 → v0.3)

**Structural changes in v0.3:**

1. **`NoTap` sentinel introduced (RC#2):** `std::variant<NoTap, RingBufferTap, Iox2Tap, SyncCallbackTap>` replaces the previous `std::variant<RingBufferTap, Iox2Tap, SyncCallbackTap>`. Default-constructed `TapConsumer` = `NoTap{}`. Sessions with no tap configured incur zero overhead. The capacity=0 sentinel anti-pattern is eliminated.

2. **`overflow_counter_ptr` eliminated (RC#2):** The dangling-pointer-by-design pattern in `TapConfig` is removed. The overflow counter is now `mutable std::atomic<uint64_t> overflow_count` directly on `TapConfig` — a proper value-typed field. No aliasing, no post-construction mutation. All counter references unified to `TapConfig::overflow_count`.

3. **Phantom `bytes` field removed from normative contract (RC#1):** §4.5 and §6.4 no longer reference a non-existent `TapRecord::bytes` field. Subscriber contract now correctly reads "`sample.header.bytes_len` bytes from `sample.payload[0..bytes_len-1]`".

4. **`iox2_service_name` consolidated (Codex P3-9):** Removed from `TapConfig`. iceoryx2 service name now exclusively in `Iox2TapConfig::service_name`. One definition; no precedence ambiguity.

5. **Appendix D byte-faithful (RC#3):** Regenerated from the finalised §4.3 `TapConfig`. Uses exact field names (`capacity` not `ring_capacity`). Omits removed fields (`overflow_counter_ptr`, `iox2_service_name`). Includes `NoTap{}` default annotation.

6. **Cross-reference typos fixed:** TS-3 cites §6.3 (not §4.1). §4.2 `TapRecord` comment cites §6.3 (not §4.1). Appendix B `[2k §7]` canonical form.

**Confirmed sound (not changed structurally):**
- The four-variant `TapConsumer` design (`NoTap | RingBufferTap | Iox2Tap | SyncCallbackTap`).
- All RC#1 and RC#2 architectural decisions from v0.2 (storage model, `TapShmRecord`, service-mode boundary, block mode, SPSC grounding, `co_await` fix, `SessionId` layout, sequence number semantics, `schema_version`).
- The PMR arena model in §8.
- The error code block (`[1100, 1199]`).
- The `[const §XIII.2]` / `[const §XV.15]` backpressure policy alignment.

---

## Appendix C — Convergence Log (v0.3 → v0.4)

### Per-finding resolution table (v0.3 → v0.4)

| Finding | Priority | Title | Action | Section(s) changed |
|---|---|---|---|---|
| Opus N-P1-1 | P1 | `Session::get_trace_context()` cross-doc gap — method not in signed-off `[2d §4.5]` surface | **Fixed.** All call sites of `session.get_trace_context()` (§3.7, §7.1 prose, §7.1 pseudocode) now cite `([2k App D §D.1])`. §3.7 adds the normative sentence: "The `session.get_trace_context()` synchronous accessor is added to `Session`'s public surface by `[2k App D §D.1]`; 2l consumes it here but does not independently amend `2d-threading.md` for this method." §7.1 prose updated identically. Pseudocode comment updated to `// see [2k App D §D.1]`. Appendix B `[2d §4.6]` row annotated with the 2k amendment; new Appendix B row for `[2k App D §D.1]` added. Appendix D §D.2 note added: 2l does NOT add a duplicate `Session::get_trace_context()` amendment. | §3.7, §7.1, Appendix B, Appendix D §D.2 |
| Codex P3-1 | P3 | `TapConfig` copyability phrasing "not copyable after first use" misleading | **Fixed.** Reworded to: "TapConfig is unconditionally non-copyable (std::atomic member); move-construct or emplace into SessionConfig. Brace-initialisation is safe." | §4.3 |
| Codex P3-2 | P3 | `TapConfig::capacity` is `std::size_t` but `RingBufferTap::ring_capacity()` returns `uint32_t` | **Fixed.** `ring_capacity()` and `available()` return types changed to `std::size_t` to match `TapConfig::capacity`. Comment updated: "Equal to TapConfig::capacity." | §4.4 |
| Codex P3-3 | P3 | §7.1 default summary uses unqualified enumerator `drop_oldest` | **Fixed.** Both the §7.1 amendment snippet comment and the §4.3 default-values summary now use the fully-qualified form `TapDropPolicy::drop_oldest`. | §4.3, §7.1 |
| Opus N-P3-1 | P3 | SVC-003 fallback note implies `fixppd` calls `RingBufferTap::drain()`, contradicting service-mode boundary | **Fixed.** §6.4 SVC-003 fallback note rewritten: states that in gRPC-only mode tap output is limited to in-process consumers; cross-process tap subscription via gRPC relay is a Phase 3 item; explicitly notes `fixppd` cannot drain the ring in v1.0 (no C++ tap headers, no C ABI drain surface). References §5 and §10 Q2. | §6.4 |
| Opus N-P3-2 | P3 | Informal `ring_capacity` persists in §4.2 and §4.4 body prose | **Fixed.** All four informal `ring_capacity` occurrences in §4.2 (code comment, fixed-slot ring layout prose, ownership contract summary) and §4.4 (arena allocation note) replaced with the correct form: `TapConfig::capacity` (for configuration-field references) or `ring_capacity()` (for the accessor method, where the parentheses make the method call unambiguous). | §4.2, §4.4 |

### "Disagree" items (v0.3 → v0.4)

None. All Codex P3 and Opus findings accepted.

### Net-effect summary (v0.3 → v0.4)

**Changes in v0.4:**

1. **Cross-doc `Session::get_trace_context()` alignment (N-P1-1):** Every call site of `session.get_trace_context()` in the doc now carries an explicit `([2k App D §D.1])` citation. §3.7 and §7.1 prose contain the normative statement that this method is owned by 2k, not by 2d §4.6 or by 2l. Appendix B has a new normative reference row for `[2k App D §D.1]`. Appendix D §D.2 confirms that 2l does not add a duplicate amendment. The tap architecture is unchanged.

2. **`TapConfig` copyability phrasing corrected (P3-1):** "not copyable after first use" → "unconditionally non-copyable (std::atomic member)". No API change; documentation accuracy only.

3. **`ring_capacity()` / `available()` return types unified to `std::size_t` (P3-2):** Eliminates the type-drift between `TapConfig::capacity` (`std::size_t`) and `ring_capacity()` (previously `uint32_t`). No semantic change to the ring mechanics; the internal `uint32_t` head/tail indices are an implementation detail.

4. **`TapDropPolicy::drop_oldest` fully qualified in all default-values summaries (P3-3):** Both §4.3 and §7.1 amendment snippets now use the qualified form. Copy-paste into application code will compile without manual qualification.

5. **SVC-003 fallback note clarified (N-P3-1):** §6.4 no longer implies `fixppd` has ring-drain capability in v1.0. The note now correctly scopes the gRPC relay to Phase 3 and cites the existing §5 and §10 Q2 deferrals.

6. **Informal `ring_capacity` replaced in body prose (N-P3-2):** §4.2 and §4.4 use `TapConfig::capacity` for configuration-field references and `ring_capacity()` (parenthesised) for the accessor. No ambiguity between field name and method name remains.

**Confirmed sound (not changed structurally):**
- All v0.3 architectural decisions: `NoTap` sentinel, fixed circular slab, SPSC ring, `RingBufferTap::drain` API, `Iox2Tap` publisher shape, `SyncCallbackTap` caveat-emptor contract, boundary-compliant `fixppd` path via iceoryx2 subscriber only.
- Appendix D §D.1 (`tap_config` field amendment to `SessionConfig`) — unchanged.
- Error code block `[1100, 1199]`.
- All 12 test seams (TS-1..TS-12) — unchanged.

---

## Appendix D — Required Amendments to Sibling Docs

### §D.1 Amendment to `2d-threading.md` — new `TapConfig tap_config` field in `SessionConfig`

**Target section:** `[2d §4.5] fixpp::session::SessionConfig — session-level frozen-at-open knobs`

**Context (current state of `[2d §4.5]` — lines 605–606):**

```cpp
// ── Tap (locked by 2l) ──────────────────────────────────────────────
fixpp::tap::TapConsumer     tap_consumer;          // variant; default-constructed = no tap.
```

**Before (existing `tap_consumer` declaration in `[2d §4.5]`):**

```cpp
// ── Tap (locked by 2l) ──────────────────────────────────────────────
fixpp::tap::TapConsumer     tap_consumer;          // variant; default-constructed = no tap.
```

**After (with `tap_config` field added and `tap_consumer` annotation updated):**

```cpp
// ── Tap (locked by 2l) ──────────────────────────────────────────────
// tap_consumer default = NoTap{} (first variant arm; no ring allocated,
// no write overhead).  Set to RingBufferTap, Iox2Tap, or SyncCallbackTap
// to enable tapping.  See [2l §4.7] for the NoTap sentinel and variant.
fixpp::tap::TapConsumer     tap_consumer;          // default = NoTap{}; see [2l §4.7]

// Tap configuration companion.  Consulted by the session dispatch loop
// when tap_consumer is not NoTap{}.  Exact field reference: [2l §4.3].
//
// User-settable fields (set before session open):
//   capacity          = 4096          — TapSlot ring capacity (power of 2)
//   drop_policy       = drop_oldest   — TapDropPolicy enum; see [2l §4.3]
//   max_message_bytes = 4096          — truncation threshold (≤ k_tap_record_max_bytes)
//   tap_ring_arena    = nullptr       — PMR arena; null → get_default_resource()
//   on_overflow_block = {}            — optional block-mode overflow hook
//
// Runtime-managed field (do not set):
//   overflow_count    = 0             — mutable std::atomic<uint64_t>;
//                                       read via tap_config.overflow_count.load()
//
// Note: TapConfig is not copyable (contains a std::atomic); move-construct
// or emplace into SessionConfig.
fixpp::tap::TapConfig       tap_config {};
```

**Default values (exact):**
`{.capacity=4096, .drop_policy=TapDropPolicy::drop_oldest, .max_message_bytes=4096, .tap_ring_arena=nullptr, .on_overflow_block={}, .overflow_count=0}`

**Note entry in `[2d §4.5]`:** Add a note bullet:

> **`tap_consumer` / `tap_config`** — updated and extended at 2l sign-off (2026-05-09). `tap_consumer` default is `NoTap{}` (the first variant arm of `std::variant<NoTap, RingBufferTap, Iox2Tap, SyncCallbackTap>`). `tap_config` is the configuration companion carrying capacity, drop policy, PMR arena, overflow hook, and the runtime-managed overflow counter. iceoryx2 service name is configured via `Iox2TapConfig::service_name` when constructing an `Iox2Tap` — not via `TapConfig`. See `[2l §4.3]` and `[2l §4.7]` for the full specification.

**Rationale:** 2l owns the tap consumer API; `SessionConfig` is the injection point. `TapConfig` is the per-session configuration companion to `TapConsumer`. Keeping them separate (rather than embedding `TapConfig` inside each variant type) follows the existing `SessionConfig` pattern of carrying cross-cutting configuration fields alongside the primary type field (see `clock_override`-related fields, `dialect_overlay`, etc.). The `NoTap` sentinel ensures default-constructed `SessionConfig` incurs zero tap overhead without requiring a special "capacity = 0" constructor on `RingBufferTap`.

---

### §D.2 Note — `Session::get_trace_context()` is NOT a 2l amendment

`Session::get_trace_context() const noexcept` — the synchronous accessor used in §7.1 and §3.7 — is added to `Session`'s public surface by `[2k App D §D.1]`, not by this document. 2l consumes the method (the dispatch loop calls it on every tap write) but does not duplicate the amendment. At sign-off of 2k, the 2d-threading.md `Session` class gains this accessor; 2l's compilation dependency on it is satisfied by the 2k amendment.

No separate 2d amendment is required from 2l's side for this method.
