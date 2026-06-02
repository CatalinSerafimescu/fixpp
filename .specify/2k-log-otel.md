# 2k — Async Logger + OTel Observability Surface

| Field | Value |
|---|---|
| Status | Draft v0.5 — Gate A round 4 converged (Phase A, post-cap pass) |
| Date | 2026-05-09 |
| Owner | Opus (Phase A) |
| Inherits | `[const §XIII]`, `[const §XIV.2]`, `[const §XV.5]`, `[arch §4.7]`, `[arch §4.8]`, `[arch §5.4]`, `[arch §5.7]`, `[arch §5.8]`, `[arch §6]`, `[arch §10]`, `[arch §11]` row 4 |
| Cites | `[SYN §3.8]`, `[SYN §3.6 #21]` |
| Catalogue rows owned | LOG-001, LOG-002, LOG-003, LOG-004, OBS-001, OBS-002, OBS-003 |
| Convergence log | addresses Codex review (0 P1 / 0 P2 / 3 P3) and Opus adversarial review (0 new P1/P2, 1 new P3); see Appendix C |

---

## §1 Goals

2k locks the **async logger + OTel observability surface** that closes `[arch §10] row 2k` ("Producer/consumer split, sink interface, `quill` vs own benchmark spike") and operationalises `[SYN §3.8]` + `[SYN §3.6 #21]`. Concretely:

1. Lock the `fixpp::log::Logger` producer API: zero-alloc per record, format-deferred, lock-free MPSC queue, `Level`/`Category` compile-time and runtime filtering.
2. Lock the `fixpp::log::Sink` plugin interface at ≤ 5 pure-virtual methods per `[const §XIV.2]` / `[arch §6]`. v0.1 proposes **4 pure-virtual** (`open`, `emit`, `flush`, `close`) — under the cap.
3. Lock the three v1.0 default sinks: `FileSink` (rotating + async fsync), `OtlpLogSink` (OTLP log export), `SyslogSink` (POSIX syslog).
4. Lock the OTel surface: `fixpp::otel::TracerProvider` / `MeterProvider` wrappers, `SessionSpans` RAII helper for session lifecycle / parse / store / dispatch latency, `PrometheusExporter` + `OtlpExporter` dual export.
5. **Resolve `[arch §11] row 4`** — quill vs own impl. See §1.2 for the benchmark-spike specification and provisional recommendation.
6. Lock LOG-003: trace correlation — `trace_id`/`span_id` are pulled from `co_await fixpp::current_trace_context` (per `[2d §4.6]`) without `thread_local` per `[const §XIII.3]`.
7. Define the `EngineConfig` fields for logger and OTel providers (engine-anchor + session-override pattern per `[2d §4.5]`); the `EngineConfig` fields were added at 2d sign-off as forward stubs (`logger`, `tracer`, `meter`) — 2k's Appendix D §D.1 confirms and populates the full type declarations.
8. Introduce the `[1000, 1099]` error-code block for `FIXPP_ERR_LOG_*` / `FIXPP_ERR_OTEL_*` per `[2i §1.1]` reserved-block layout.

### §1.1 Magnitude domain — scope boundary

2k **owns**:
- The `fixpp::log::Logger` producer/consumer machinery: `Record`, `Level`, `Category`, MPSC queue, drain thread, `FIXPP_SLOG(...)`/`FIXPP_ELOG(...)`/`FIXPP_LOG0(...)` macros, `Logger::shutdown()`.
- The `fixpp::log::Sink` pure-virtual interface and its three default implementations.
- The `fixpp::otel::TracerProvider`, `fixpp::otel::MeterProvider` thin wrappers.
- The `fixpp::otel::SessionSpans` RAII span helper.
- The `fixpp::otel::PrometheusExporter` and `fixpp::otel::OtlpExporter`.
- The `[1000, 1099]` C-ABI error block: `FIXPP_ERR_LOG_*` / `FIXPP_ERR_OTEL_*`.
- The resolution of `[arch §11] row 4` (quill vs own).

2k **does not own**:
- `fixpp::current_trace_context` awaitable and `session_local<trace_context>` storage — owned by **2d** (`[2d §4.6]`). 2k only *consumes* the awaitable.
- `fixpp::core::Clock::now()` as the timestamp source — owned by **2d** (`[2d §4.1]`); `effective_clock.now()` is what log records call per `[2d §7.9]`.
- `fixpp::tap::TapConsumer` ring-buffer mechanics — owned by **2l**.
- `service/grpc/` `StreamLogs` / `StreamMetrics` wire shape — owned by **2j** (`[2j §4.7]`). 2k defines the interface a `Sink` or log-buffer exposes; 2j defines the RPC wire shape.
- C ABI for engine/session lifecycle — owned by **2i** / **session-module Phase-4 spec**.
- SWIG/Python bindings — owned by **2m**.

### §1.2 Quill vs own — benchmark-spike specification and provisional recommendation

**Background.** `[arch §11] row 4` and `[const §XIII.5]` both require a benchmark spike before locking the implementation. `[SYN §3.8]` evaluates four candidates: quill, spdlog async, NanoLog, and own impl. This section specifies the spike methodology precisely enough to execute it as a CI-runnable benchmark in Phase 3/4.

**Spike harness (TS-13 delivers this):**

1. **Benchmark target metric.** Enqueue latency per record: mean, p50, p99, p999, max. Measured on the **producer thread** only (the drain thread cost is explicitly not a hot-path ceiling per `[const §VIII.5]`). Tail latency (p99 and p999) is the primary criterion — a FIX hot path cares about worst-case enqueue stall, not mean.
2. **Load scenario.** Four producer threads each emitting 2.5M records (10M total) in a tight loop at three fill rates: 10%, 50%, and 95% of queue capacity. Queue capacity: 65,536 records (power of 2). **Using four producers is mandatory** — quill uses per-producer SPSC queues (no CAS loop) while an own MPSC ring uses a shared Dmitry Vyukov-style atomic sequence counter; contention only differentiates them at ≥2 producers. Each record is the locked 256-byte `Record` struct from §4.2 (one `utc_time_point`, one `Level`, one `Category`, one `uint32_t` format_id, four captured `uint64_t` args packed into `ArgValue` slots). Using the real 256-byte `Record` is mandatory — a synthetic 64-byte record cannot predict p99 latency differences due to cache-line fill effects.
3. **Validation criteria.** A candidate passes the spike if:
   - **Criterion A — zero alloc.** Running the benchmark under `mallocnesia` (the project's malloc interceptor per `[arch §5.2]`) produces zero allocation events on the producer path for all three fill rates.
   - **Criterion B — latency ceiling.** p99 enqueue latency ≤ 50 ns at 50% fill rate on the reference CI hardware (defined in `bench/baselines/README.md`).
   - **Criterion C — drop counter.** On overflow (95% fill rate with an intentional slow drain), the drop counter increments correctly; no memory unsafety under TSan.
   - **Criterion D — OTel correlation.** The record struct can carry `trace_id` (16 bytes) and `span_id` (8 bytes) as plain fields; no additional allocation for correlation data.
   - **Criterion E — PMR sink.** The logger's internal ring buffer can be allocated from a `std::pmr::memory_resource*` supplied at construction.
4. **Candidates evaluated.** (a) `quill` v11.x (latest tagged release per `[const §XV.17]`; `quill/11.1.0` on Conan Center — refreshed 2026-06-02 from the original v3.x, which was Conan Center's latest when this doc was authored). NB: quill 11.x is a major-API rewrite vs 3.x; the TS-13 spike harness targets the 11.x API. (b) own impl (bounded MPSC ring using a power-of-two atomic head/tail with the sequence-number Disruptor variant for minimal CAS contention).
5. **Decision rule.** If quill passes all five criteria → **adopt quill**; smaller maintenance footprint per `[SYN §3.8]`. If quill fails any criterion → **own impl**. The decision is recorded in Appendix C after Phase 3 spike execution.

**Provisional recommendation (v0.1, without spike execution).** Provisional recommendation is **own impl**, with the door left open to adopt quill if the spike passes Criterion A (zero alloc). The rationale:

- Quill's per-producer SPSC queue uses per-thread heap-allocated buffers at thread registration time. Whether this constitutes "zero alloc per record on the producer path" (Criterion A) versus "one allocation at thread registration, zero per record after" is a semantic question the spike must answer explicitly. If quill registers threads lazily (first `QUILL_LOG(...)` on a thread allocates the SPSC buffer), then any coroutine that first logs on a fresh strand thread would trigger an allocation — which, while rare, is technically a hot-path allocation risk if that thread is also the session strand's I/O thread.
- Own impl (Disruptor-variant MPSC ring) makes the zero-alloc guarantee trivially inspectable: the ring is pre-allocated at `Logger` construction; the producer path is a CAS on an atomic sequence counter followed by a slot write. No thread registration, no per-thread heap.
- The four Criteria A–D are achievable in own impl by construction; PMR integration (Criterion E) is straightforward since we control the ring's allocator.

**If the spike confirms quill passes Criterion A** (thread registration happens at `Logger::add_producer_thread()` which the engine calls once per session strand at session open — not on the hot path), the recommendation flips to adopt quill. This is the key measurement the spike must surface.

Until the spike executes, §4.3 specifies the `Logger` API in terms of **our own interface contract**, which is the contract whether the back-end is quill or own ring. The back-end is an implementation detail behind the `Logger` facade.

---

## §2 Non-goals (v1.0)

1. **No synchronous logging shim.** Synchronous logging on the hot path is banned per `[const §XV.5]`. No compatibility wrapper for synchronous logger users.
2. **No log aggregation, routing, or sampling.** OTel collector pipelines (Jaeger, Tempo, Loki, etc.) are out of scope; fixpp emits to OTLP and Prometheus; downstream routing is operator-owned.
3. **No custom OTel SDK.** fixpp wraps the official OpenTelemetry C++ SDK. No re-implementation of the OTel trace/metric/log data model.
4. **No W3C TraceContext injection into outbound FIX messages** (e.g., as a custom tag). Post-v1.0; session-level spans are engine-internal. Explicit non-goal to avoid the `[const §XV.1]` per-field allocation risk.
5. **No structured logging query language** (e.g., lnav schema alignment). Post-v1.0.
6. **No `dlopen`-based sink discovery.** Compile-time only per `[const §XIV.4]`. Dynamic sink loading is post-v1.0.
7. **No log forwarding to the C ABI** in v1.0. Log/OTel access is C++ API only. `c_api/log.h` and `c_api/otel.h` contain placeholder version macros and `#include` guards only; no `extern "C"` symbols in v1.0. See §5 for the full justification and the exact placeholder content.
8. **No per-record heap allocation** — this is not a non-goal but a hard invariant; stated here to make it explicit from the reader's first pass.

---

## §3 Inherited surface

### §3.1 From `[arch §4.7]` — `log` module surface inventory

> `fixpp::log::Logger` — facade. Producer side: zero-allocation per record `[const §XIII.2]`.
> `fixpp::log::Record` — POD: timestamp, level, format-id, captured args by value/view.
> `fixpp::log::Sink` — interface; ≤5 pure-virtual.
> `fixpp::log::FileSink`, `fixpp::log::OtlpLogSink`, `fixpp::log::SyslogSink` — default impls.

This doc inherits the above without change. It specialises: the exact `Sink` pure-virtual set (§4.4), the `Record` struct layout (§4.2), the `Level`/`Category` enum design (§4.1), and the queue mechanics (§4.3).

### §3.2 From `[arch §4.8]` — `otel` module surface inventory

> `fixpp::otel::TracerProvider`, `fixpp::otel::MeterProvider` — wrappers around the OpenTelemetry C++ SDK.
> `fixpp::otel::SessionSpans` — span helper for session lifecycle / parse / store / dispatch.
> `fixpp::otel::PrometheusExporter`, `fixpp::otel::OtlpExporter` — wired so a single collector pipeline exports both `[SYN §3.6 #21]`.

This doc inherits and fully specifies the above in §4.8–§4.10.

### §3.3 From `[arch §5.4]` — trace context (owned by 2d; 2k consumes)

> **Storage:** `SessionConfig.initial_trace_context` is read once at session open and stored in the session's `fixpp::core::session_local<trace_context>` slot. The slot lives inside the `Session` object.
> **Access:** `co_await fixpp::current_trace_context` returns the value bound to the current session serialisation domain; outside session scope, returns the `Engine`-level fallback context `[const §XIII.3]`.
> **`thread_local` is prohibited.**

2k consumes the `session_local<trace_context>` slot for LOG-003 correlation (via `Session::get_trace_context()` — see Appendix D §D.1 amendment). 2k also adds `Session::get_trace_context() const noexcept` to 2d's `Session` surface, which is the only new method required. `trace_context` is the type alias for `fixpp::otel::trace_context` (16-byte `trace_id` + 8-byte `span_id` + 1-byte flags + padding = 32 bytes per `[2d §1.2]`).

### §3.4 From `[arch §5.7]` — logging constraints

> **Producer:** zero-alloc per record; format-deferred; lock-free MPSC `[const §XIII.2]`.
> **Consumer:** dedicated drain thread; formats records; dispatches to sinks.
> **Sinks** implement the `Sink` interface (≤5 pure-virtual). File (rotating + async fsync), OTLP exporter, syslog ship in v1.0.
> **Adoption decision deferred** to **2k**'s benchmark spike — `quill` vs own impl.

This doc resolves the deferred adoption decision (§1.2 provisional recommendation + TS-13 spike).

### §3.5 From `[arch §5.8]` — backpressure

> **Telemetry/log/tap paths:** `drop-oldest` is permitted under bounded-queue overflow, with a recorded counter `[const §XIII.2]`.

This doc implements `overflow_policy::drop_newest` as the default `Logger` overflow policy (see §4.3 for why `drop_newest` at the C++ API level satisfies the `[const §XIII.2]` / `[arch §5.8]` "drop-oldest permitted" allowance — in a FIFO ring, dropping the newest-produced record preserves all older in-flight records). The `block` mode is available for non-session-strand producer threads. The drop counter is a `std::atomic<std::uint64_t>` on `Logger` (§4.3).

### §3.6 From `[const §XIII]` — Observability & Logging (full article)

> 1. OpenTelemetry from v1.0. Traces, metrics, logs all OTLP-exportable. Prometheus + OTLP dual export is the v1.0 minimum.
> 2. Async logging is mandatory. The in-process logger is zero-alloc producer, bounded MPSC queue, dedicated drain thread, deferred formatting. Telemetry and log queues are permitted to use `drop-oldest` under bounded-queue overflow.
> 3. OTel `trace_id` / `span_id` in every log record. Code paths outside session scope use the `co_await fixpp::current_trace_context` awaitable backed by a strand-stored context. `thread_local` propagation is **prohibited**.
> 4. Same sink interface backs OTel log export and file/stderr sinks. No double-write paths.
> 5. **Bench spike mandatory** for the in-house logger vs `quill` before locking the implementation choice.

All five clauses are directly addressed by this doc.

### §3.7 From `[const §XIV.2]` — ≤5 pure-virtual rule

> Each pluggable interface defines **≤5 pure-virtual methods**. Bigger surfaces are permitted only with an explicit design-doc justification reviewed at Gate A.

`fixpp::log::Sink` has exactly 4 pure-virtual methods in this doc (§4.4). Under the cap; no justification paragraph needed.

### §3.8 From `[2d §4.4]` — `EngineConfig` observability field stubs

Per `[2d §4.4]`, `EngineConfig` already carries:

```cpp
std::shared_ptr<fixpp::core::Logger>           logger;        // null → no-op.
std::shared_ptr<fixpp::otel::TracerProvider>   tracer;        // null → no-op trace context.
std::shared_ptr<fixpp::otel::MeterProvider>    meter;         // null → no-op metrics.
```

These stubs were introduced by 2d (which referenced 2k as the owner). 2k's Appendix D §D.1 confirms the field types match the public API defined in §4.3 and §4.8.

2k additionally adds (RC#6, v0.3) an `engine_trace_context` field to `EngineConfig` (Appendix D §D.1 extension):

```cpp
// Engine-level static lifecycle trace context for LOG-003 Tier 2 correlation.
// Used by FIXPP_ELOG(lvl, engine, cat, fmt, ...) to correlate control-plane
// and listener log records with the engine's root OTel span.
// Zeroed by default (no correlation); set at engine construction to the root
// span of the engine lifecycle.
fixpp::otel::trace_context  engine_trace_context {};
```

`engine.engine_trace_context()` is a `const noexcept` accessor returning an atomic snapshot of this field per `[2d §4.4]`.

---

## §4 Public C++ API

**Convention.** Every view-returning accessor carries `[[clang::lifetimebound]]`. Every `expected_t<T>`-returning method carries `[[nodiscard]]`. Value-returning methods that cannot fail in any defensible way do not return `expected_t<T>` (they abort on invariant violation per `[arch §5.3]`).

### §4.1 `fixpp::log::Level` and `fixpp::log::Category`

```cpp
// include/fixpp/log/level.hpp
namespace fixpp::log {

// Compile-time log level. Records below the compile-time cutoff compile to
// nothing. The cutoff is set by FIXPP_LOG_MIN_LEVEL (default: trace for debug
// builds, info for release builds, overridable via CMake option).
// Numeric values are stable: never reorder, only extend at the high end.
enum class Level : std::uint8_t {
    trace   = 0,
    debug   = 1,
    info    = 2,
    warn    = 3,
    error   = 4,
    fatal   = 5,
};

// Category is a small integer interned at compile time from a string literal.
// Interning happens at static initialisation via CRC32 of the literal; no
// heap allocation. The result is a uint16_t that the producer encodes in the
// Record.
//
// Default built-in categories (pre-defined compile-time constants):
//   cat::session, cat::wire, cat::transport, cat::tls,
//   cat::store, cat::otel, cat::control, cat::user.
using Category = std::uint16_t;

namespace cat {
    inline constexpr Category session   = 0x0001;
    inline constexpr Category wire      = 0x0002;
    inline constexpr Category transport = 0x0003;
    inline constexpr Category tls       = 0x0004;
    inline constexpr Category store     = 0x0005;
    inline constexpr Category otel      = 0x0006;
    inline constexpr Category control   = 0x0007;
    inline constexpr Category user      = 0x0008;
    // User-defined categories: use FIXPP_LOG_CATEGORY("my_cat") which
    // expands to the CRC32 of the literal at compile time.
}  // namespace cat

}  // namespace fixpp::log
```

**`FIXPP_LOG_MIN_LEVEL`:** a CMake-settable integer (`0`=trace..`5`=fatal). All three macros — `FIXPP_SLOG`, `FIXPP_ELOG`, and `FIXPP_LOG0` — gate-check at compile time via `if constexpr (level >= FIXPP_LOG_MIN_LEVEL)`; levels below the cutoff produce **zero bytes** in the final binary. See §4.3 for the full macro definitions.

**Runtime category filter.** `Logger` maintains a `std::atomic<uint64_t> enabled_categories_mask_`. A record whose category bit is clear is dropped before enqueueing. The mask is updated via `Logger::set_category_enabled(Category, bool)` (lock-free compare-exchange).

### §4.2 `fixpp::log::Record`

```cpp
// include/fixpp/log/record.hpp
namespace fixpp::log {

// Maximum number of variadic args captured per log call.
inline constexpr std::size_t k_max_args = 6;

// ArgValue — explicitly-sized 24-byte trivially-copyable tagged union.
// No heap allocation. No std::string_view (dangling pointer risk across the
// async queue boundary — the drain thread reads the record after the producer's
// stack frame is gone). String arguments must use one of two safe paths:
//
//   InlineStr: up to 15 bytes of inline storage (no null-terminator guarantee;
//              the `len` field gives the stored byte count). Truncated silently
//              at 15 bytes. Use for short runtime strings. Covers CompIDs,
//              MsgType values, FixedString fields, and most FIX log fragments.
//              Strings longer than 15 bytes must use StaticStr.
//   StaticStr: const char* with caller-asserted static (or arena) lifetime.
//              Use FIXPP_SLIT("...") for string literals; NEVER pass a
//              pointer to a stack-local or heap-temporary string.
//
// User contract: passing a stack-local string via StaticStr is UB. The
// FIXPP_SLIT("...") macro enforces static lifetime at the call site via
// a consteval lambda. For runtime short strings use InlineStr (safe).
//
// Size: 1 byte kind + 7 bytes padding + 16 bytes union = 24 bytes.
//   InlineStr = char data[15] + uint8_t len = 15 + 1 = 16 bytes (alignment 1;
//   in union aligned to 8 → union = 16 bytes). ArgValue = 1 + 7 + 16 = 24. ✓
//   Other union members: uint64_t (8), int64_t (8), double (8), bool (1),
//   const char* (8) — all ≤ 16 bytes.
// static_assert enforced below.
//
// RC#5 fix (v0.3): reduced InlineStr::data from char[23] to char[15] (16 bytes
// total for InlineStr including len). The original char[23]+uint8_t=24-byte
// InlineStr made sizeof(ArgValue) == 32 (not 24), causing the static_assert to
// fail at compile time. The from_inline null write data[n] with n==23 was also
// OOB on a 23-element array (UB / ASan trap). The fix caps the payload at 15
// bytes and removes the null-terminator guarantee; the drain thread uses `len`.
struct ArgValue {
    enum class Kind : uint8_t {
        empty      = 0,
        u64        = 1,
        i64        = 2,
        f64        = 3,
        bool_val   = 4,
        inline_str = 5,   // up to 15 bytes inline; NOT null-terminated; use len
        static_str = 6,   // const char* with caller-asserted stable lifetime
    };

    // Inline string storage: 15 bytes of payload + 1 length byte = 16 bytes.
    // data[0..len-1] holds the string payload. data is NOT null-terminated;
    // the drain thread reads exactly `len` bytes from data. `len` is in [0, 15].
    struct InlineStr {
        char     data[15];
        uint8_t  len;   // actual length (≤ 15); NOT null-terminated (use len).
    };

    Kind     kind {};
    uint8_t  _pad[7] {};
    union {
        uint64_t     u64;
        int64_t      i64;
        double       f64;
        bool         b;
        InlineStr    inl;          // 16 bytes (char[15] + uint8_t len), fits in union
        const char*  static_ptr;  // StaticStr: caller-asserted stable lifetime
    };

    // Convenience constructors.
    static ArgValue from_u64(uint64_t v)   noexcept { ArgValue a; a.kind = Kind::u64;  a.u64 = v; return a; }
    static ArgValue from_i64(int64_t  v)   noexcept { ArgValue a; a.kind = Kind::i64;  a.i64 = v; return a; }
    static ArgValue from_f64(double   v)   noexcept { ArgValue a; a.kind = Kind::f64;  a.f64 = v; return a; }
    static ArgValue from_bool(bool    v)   noexcept { ArgValue a; a.kind = Kind::bool_val; a.b = v; return a; }
    // Inline string: copies up to 15 bytes; safe for any runtime string.
    // Strings longer than 15 bytes are silently truncated to 15 bytes.
    // The stored bytes are NOT null-terminated; the drain thread uses len.
    static ArgValue from_inline(std::string_view sv) noexcept {
        ArgValue a; a.kind = Kind::inline_str;
        auto n = std::min(sv.size(), std::size_t{15});
        std::memcpy(a.inl.data, sv.data(), n);
        a.inl.len = static_cast<uint8_t>(n);
        return a;
    }
    // Static string: caller MUST guarantee the pointed-to bytes outlive the drain.
    // Use FIXPP_SLIT("...") to assert static lifetime at the call site.
    static ArgValue from_static(const char* p) noexcept {
        ArgValue a; a.kind = Kind::static_str; a.static_ptr = p; return a;
    }
};
// ArgValue size invariant: exactly 24 bytes for cache-line efficiency.
// With InlineStr = char[15] + uint8_t = 16 bytes, the union is 16 bytes,
// ArgValue = 1 (kind) + 7 (_pad) + 16 (union) = 24 bytes. ✓
// The == form is required (not <=) because Record sizing depends on
// sizeof(ArgValue) == 24 exactly: 48 header + 6×24 = 192 + 64 pad = 256.
// A weaker <= assertion would allow ArgValue to shrink on unusual ABIs
// without triggering a diagnostic at the ArgValue level, making the
// paired static_assert(sizeof(Record) == 256) the only failure indicator.
static_assert(sizeof(ArgValue) == 24, "ArgValue must be exactly 24 bytes for cache-line efficiency");
static_assert(std::is_trivially_copyable_v<ArgValue>,
              "ArgValue must be trivially copyable");

// FIXPP_SLIT("literal") — helper macro for static-lifetime string args.
// Expands to ArgValue::from_static("literal"); the string literal has
// static storage duration by the C++ standard so the pointer is permanently
// valid. Do NOT use with non-literal expressions.
#define FIXPP_SLIT(s) (::fixpp::log::ArgValue::from_static(s))

// A Record is a fixed-size POD placed directly onto the MPSC ring buffer.
// Size is a multiple of cache-line (64 bytes) to avoid false-sharing.
//
// Size math (v0.3, post RC#5 ArgValue fix):
//   Fixed header:
//     utc_time_point  (8) + trace_id (16) + span_id (8) = 32
//     level (1) + flags (1) + category (2) + format_id (4)
//       + arg_count (1) + _pad[5] (5) = 14
//     Total header = 32 + 16 = 48 bytes
//   Args: 6 × sizeof(ArgValue) = 6 × 24 = 144 bytes
//   Subtotal: 48 + 144 = 192 bytes → 3 cache lines at 64 bytes each
//   Padding to 256 bytes: 64 bytes of _cache_pad
//   Total: 256 bytes = 4 cache lines.
//
// Note: sizeof(ArgValue) == 24 exactly (RC#5 fix + v0.4 == assertion),
// so the 6-arg layout is exactly 144 bytes. The
// static_assert(sizeof(Record) == 256) confirms the cache-line alignment
// invariant.
struct alignas(64) Record {
    // Timestamp from effective_clock.now() (wall clock UTC) per [2d §7.9].
    fixpp::core::utc_time_point  timestamp;      //  8 bytes

    // OTel correlation fields (LOG-003). Zeroed when no session context
    // is available (non-session code paths; not a bug — see §6.4).
    std::array<std::uint8_t, 16> trace_id {};    // 16 bytes
    std::uint64_t                span_id  {};    //  8 bytes

    Level                        level;          //  1 byte
    std::uint8_t                 flags    {};    //  1 byte (reserved)
    Category                     category;       //  2 bytes
    std::uint32_t                format_id;      //  4 bytes (CRC32 of fmt literal)
    std::uint8_t                 arg_count;      //  1 byte
    std::uint8_t                 _pad[5]  {};    //  5 bytes
    // ---- 48 bytes so far ----

    // Captured args (up to k_max_args = 6 at exactly 24 bytes each = 144 bytes).
    std::array<ArgValue, k_max_args> args {};    // 144 bytes (6 × 24)

    // Total: 48 + 144 = 192 bytes → pad to 256 bytes (4 cache lines).
    std::uint8_t _cache_pad[64] {};              // 64 bytes padding
    // Total: 256 bytes = 4 cache lines.
};
static_assert(sizeof(Record) == 256, "Record must be 4 cache lines");
static_assert(std::is_trivially_copyable_v<Record>,
              "Record must be trivially copyable for ring-buffer placement");

}  // namespace fixpp::log
```

**Zero-alloc guarantee.** `Record` is a trivially-copyable fixed-size struct. Placing it onto the MPSC ring is a sequence-counter CAS plus a `memcpy` of 256 bytes. No heap allocation. No virtual dispatch. No system calls.

**String argument safety.** `ArgValue` contains no `std::string_view` field. The `InlineStr` variant copies up to 15 bytes of payload bytes directly into the 16-byte union member at the `FIXPP_SLOG`/`FIXPP_ELOG`/`FIXPP_LOG0` call site — the pointed-to bytes are not needed after the `ArgValue` is constructed. The drain thread reads exactly `inl.len` bytes from `inl.data`; no null-terminator is required. The `StaticStr` variant stores a `const char*` whose lifetime is asserted by the caller via `FIXPP_SLIT("literal")` (string literals have static storage duration). Neither variant can dangle across the async queue boundary. Per `[SYN §3.8]` "captured args by value."

### §4.3 `fixpp::log::Logger`

```cpp
// include/fixpp/log/logger.hpp
namespace fixpp::log {

enum class overflow_policy : std::uint8_t {
    // drop_newest: producer detects a full ring and drops the record it was
    // about to enqueue. Increments drop_count_. Zero overhead on consumer path.
    // read_sequence_ is std::atomic<uint64_t>; producers load with relaxed
    // ordering (a stale read can only cause an early drop — safe under drop_newest).
    //
    // Semantic note: in a FIFO ring, dropping the newest-produced record
    // preserves the oldest in-flight records. This satisfies [const §XIII.2]'s
    // "drop-oldest is permitted" allowance — "oldest" refers to the semantic
    // age of in-flight records, not the implementation mechanism.
    drop_newest = 0,  // default; permitted per [const §XIII.2] / [const §XV.15].

    // block: producer spins with std::this_thread::yield() until a slot is
    // available. WARNING: MUST NOT be used from a session-strand coroutine
    // (it pins the executor OS thread, stalling all sessions on that thread —
    // equivalent to std::mutex in a coroutine context, prohibited by
    // [const §XI.3]). Safe only for dedicated non-coroutine producer threads
    // (e.g., background control-plane threads not on the session executor).
    block       = 1,
};

struct LoggerConfig {
    // Ring capacity (number of Record slots). Must be a power of 2.
    // Default: 65536 records (16 MiB for 256-byte records).
    std::uint32_t    capacity    = 65536u;
    overflow_policy  on_overflow = overflow_policy::drop_newest;
    // PMR resource for the ring buffer allocation. Lifetime must outlive Logger.
    std::pmr::memory_resource* ring_resource = std::pmr::get_default_resource();
    // Drain thread CPU affinity (optional; negative = no affinity hint).
    int drain_cpu_affinity = -1;
    // Drain timeout used by shutdown(). After this deadline, in-flight records
    // are abandoned and log_drain_timeout (error 1004) is recorded.
    std::chrono::milliseconds drain_timeout = std::chrono::seconds{5};
};

class Logger {
public:
    explicit Logger(LoggerConfig config,
                    std::pmr::vector<std::unique_ptr<Sink>> sinks);
    ~Logger();

    // ── Producer API (called from any thread) ────────────────────────────

    // Low-level enqueue. Called by FIXPP_SLOG / FIXPP_ELOG / FIXPP_LOG0 macros.
    // Zero allocation on producer thread. Lock-free MPSC CAS on the ring
    // sequence counter.
    // trace_id and span_id are the OTel correlation fields for LOG-003.
    // Pass zeroed arrays for truly context-free callers (FIXPP_LOG0 / Tier 3).
    void enqueue(Level level, Category category, std::uint32_t format_id,
                 std::array<std::uint8_t, 16> const& trace_id,
                 std::uint64_t span_id,
                 fixpp::core::utc_time_point timestamp,
                 std::initializer_list<ArgValue> args) noexcept;

    // ── Configuration (thread-safe, atomic update) ───────────────────────
    void set_category_enabled(Category cat, bool enabled) noexcept;
    bool is_category_enabled(Category cat) const noexcept;

    // ── Drop / filter accounting ─────────────────────────────────────────
    [[nodiscard]] std::uint64_t drop_count()         const noexcept;  // overflow drops
    [[nodiscard]] std::uint64_t timeout_drop_count() const noexcept;  // records abandoned on drain timeout
    [[nodiscard]] std::uint64_t filter_count()       const noexcept;  // category-filtered
    void reset_drop_count()         noexcept;
    void reset_timeout_drop_count() noexcept;
    void reset_filter_count()       noexcept;

    // Per-sink error counter (exceptions caught by drain thread per sink).
    [[nodiscard]] std::uint64_t sink_error_count(std::size_t sink_index) const noexcept;

    // ── Flush / shutdown ─────────────────────────────────────────────────

    // Async flush — enqueues a flush sentinel into the ring; the drain thread,
    // upon processing the sentinel, posts a completion back to the caller's
    // executor via asio::post(caller_executor, handler). The caller's executor
    // is captured at call time via co_await asio::this_coro::executor.
    // One heap allocation (std::function completion handler) per async_flush().
    // Off the hot path; acceptable at shutdown.
    [[nodiscard]] asio::awaitable<void> async_flush();

    // Sync flush / shutdown — instructs the drain thread to process all
    // in-flight records, then calls flush(drain_timeout) on each Sink.
    // After drain_timeout, remaining records are abandoned; timeout_drop_count()
    // is incremented, and this method returns unexpected(log_drain_timeout).
    // The drain_timeout argument is the single source of truth; use
    // LoggerConfig::drain_timeout as the canonical value. Callable from any thread.
    [[nodiscard]] fixpp::core::expected_t<void> shutdown(
        std::chrono::milliseconds drain_timeout);

    // ── Sink management (called once at construction; not mid-flight) ────
    // Sinks are passed at construction and are not swappable mid-flight.
    // Matching [arch §6] rule 4 (factory entry point).

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace fixpp::log
```

**MPSC ring mechanics.** The ring uses a CAS-based MPSC sequence counter: `std::atomic<std::uint64_t> write_sequence_` (producers claim slots via CAS) and `alignas(64) std::atomic<std::uint64_t> read_sequence_` (advanced exclusively by the single drain-thread consumer via `memory_order_release` store; read by producers via `memory_order_relaxed` load). The `alignas(64)` on each of `write_sequence_` and `read_sequence_` places them on separate cache lines, eliminating false sharing between producers (writing `write_sequence_`) and the drain thread (writing `read_sequence_`).

**Producer path (drop_newest policy) — load-check-CAS:**
1. Load `w = write_sequence_.load(relaxed)`.
2. Load `r = read_sequence_.load(relaxed)`.
3. If `w - r >= capacity` → overflow: do NOT claim a slot; increment `drop_count_` atomically; return.
4. Otherwise: attempt `write_sequence_.compare_exchange_weak(w, w+1, acq_rel, relaxed)`.
5. If CAS fails (another producer won the race): retry from step 1.
6. On CAS success: producer owns slot `w % capacity`. Write the 256-byte `Record` into the slot, then store the slot's per-slot sequence number to signal the drain thread.
7. Drain thread: scans slots in `read_sequence_` order; when `slot[r % capacity].sequence == r + 1`: consume the record; advance `read_sequence_` (release store).

**Key invariant.** Step 3 checks BEFORE claiming a slot. On overflow, `write_sequence_` is never incremented, so the drain thread will never wait on a slot that was claimed but never written. No deadlock possible.

A relaxed load of `read_sequence_` in step 2 is safe: a stale (under-advanced) read makes the ring look fuller than it is, at worst causing an early drop — correct under `drop_newest`. On `block`: the producer spins `std::this_thread::yield()` until the step-3 check clears and the CAS succeeds; see caveat in `overflow_policy::block` doc-comment above.

**P1-2 fix (v0.3):** `read_sequence_` is `std::atomic<std::uint64_t>`, not plain `uint64_t`. Multiple producer threads read `read_sequence_` in the fullness check; a plain `uint64_t` read across threads is a data race under the C++17 memory model (TSan fires). The `memory_order_relaxed` producer load is sufficient — stale reads cause early drops, not corruption.

**Drain thread.** Spawned at `Logger` construction; lives until `shutdown()`. At each iteration: reads the next `Record` from the ring (drain thread owns `read_sequence_`), resolves `format_id` from a compile-time registry (`constexpr` map of `(format_id → format_string)` keyed by CRC32), formats via `std::vformat`, and fans out to each `Sink::emit(record)`. Exceptions from `Sink::emit` are caught, counted via `sink_error_count_[i]`, and do not propagate.

**`block` mode — session-strand prohibition.** `block` mode MUST NOT be used from a session-strand coroutine. Using `block` inside a session coroutine pins the executor OS thread at the `FIXPP_SLOG` / `FIXPP_ELOG` / `FIXPP_LOG0` call site, stalling all other sessions on that executor thread. This is equivalent to holding `std::mutex` in a coroutine context, prohibited by `[const §XI.3]`. `block` mode is safe only for dedicated non-coroutine producer threads (e.g., background control-plane threads that do not run on the session executor). The `Logger` does not enforce this at runtime in release builds but will `FIXPP_ASSERT` in debug builds if `on_overflow == block` and the calling thread is detected as a session executor thread.

**Logger ownership in `EngineConfig`.** `EngineConfig::logger` is a `std::shared_ptr<fixpp::log::Logger>`. `SessionConfig` may supply a `std::shared_ptr<fixpp::log::Logger> logger_override` (nullable; engine-anchor + session-override pattern per `[2d §4.5]`). Null effective logger → no-op (engine continues; records discarded).

---

### LOG-003 — Three-macro pattern (RC#3 + RC#6)

`thread_local` is prohibited per `[const §XIII.3]`. The single `FIXPP_LOG(...)` macro approach (§4.3 v0.1) was contradictory: it simultaneously claimed to require an explicit `co_await` before the call AND to call `session_ptr()` synchronously inside `detail::enqueue_record` — incompatible behaviors. v0.2 replaced it with a two-macro pattern (`FIXPP_SLOG` + `FIXPP_LOG_NOTRACE`). v0.3 completes the three-tier model (RC#6) by adding `FIXPP_ELOG` for engine-scope correlation and renaming `FIXPP_LOG_NOTRACE` to `FIXPP_LOG0` to make its zero-context semantics explicit.

**`FIXPP_SLOG(level, tc, cat, fmt, ...)` — Tier 1: session-strand path (fast path):**

```cpp
// For use inside session coroutines where a Session& is in scope.
// The caller obtains `tc` via `session.get_trace_context()` (a synchronous
// const noexcept accessor added to Session by 2k Appendix D §D.1) before
// calling the macro. Passing tc explicitly avoids any co_await inside the
// macro and makes the context source unambiguous at the call site.
//
// Typical call site:
//   auto const& tc = session.get_trace_context();
//   FIXPP_SLOG(info, tc, cat::session, "msg {}", ArgValue::from_u64(42));
#define FIXPP_SLOG(lvl, tc, cat, fmt, ...)                                    \
    do {                                                                      \
        if constexpr (static_cast<int>(::fixpp::log::Level::lvl)             \
                      >= FIXPP_LOG_MIN_LEVEL) {                               \
            ::fixpp::log::detail::enqueue_record(                            \
                ::fixpp::log::Level::lvl, cat,                               \
                FIXPP_FORMAT_ID(fmt),                                        \
                (tc).trace_id, (tc).span_id,                                 \
                ##__VA_ARGS__);                                               \
        }                                                                     \
    } while(false)
```

`tc` is a `fixpp::otel::trace_context const&` obtained by the caller on the session strand via `session.get_trace_context()` — a synchronous `const noexcept` member function that reads the `session_local<trace_context>` slot stored in the `Session` object (added by 2k Appendix D §D.1). No `co_await`. No `thread_local`. The explicit `tc` parameter makes the context source self-documenting and eliminates the cross-doc ambiguity between the global `fixpp::current_trace_context` awaitable (`[2d §4.6]`) and the per-session synchronous accessor.

**`FIXPP_ELOG(level, engine_ref, cat, fmt, ...)` — Tier 2: engine-scope path (control-plane / listener):**

```cpp
// For use in control-plane handlers, listener accept coroutines, and any
// context that holds an Engine& but no Session&.
// engine_ref.engine_trace_context() reads the engine-level static lifecycle
// trace_context (an atomic snapshot per [2d §4.4]). This provides engine-scope
// correlation — control-plane log records carry the engine's root span IDs
// rather than zeroed IDs, enabling correlation in the OTel backend.
// Use FIXPP_ELOG for control-plane handlers (2j-owned code paths) and for
// listener accept coroutines that have not yet opened a session.
#define FIXPP_ELOG(lvl, engine, cat, fmt, ...)                                \
    do {                                                                      \
        if constexpr (static_cast<int>(::fixpp::log::Level::lvl)             \
                      >= FIXPP_LOG_MIN_LEVEL) {                               \
            auto const& _tc = (engine).engine_trace_context();               \
            ::fixpp::log::detail::enqueue_record(                            \
                ::fixpp::log::Level::lvl, cat,                               \
                FIXPP_FORMAT_ID(fmt),                                        \
                _tc.trace_id, _tc.span_id,                                   \
                ##__VA_ARGS__);                                               \
        }                                                                     \
    } while(false)
```

`engine_ref` is an `Engine const&`. `engine.engine_trace_context()` is a thread-safe atomic snapshot accessor per `[2d §4.4]`. Use `FIXPP_ELOG` for all control-plane handlers and listener coroutines owned by `[2j]`.

**`FIXPP_LOG0(level, cat, fmt, ...)` — Tier 3: zero-context path (truly context-free sites):**

```cpp
// For use in destructors, static init, shutdown code, and any site where
// neither a Session& nor an Engine& is available.
// trace_id and span_id are zeroed in the Record. The OTel collector treats
// zeroed trace_id as an uncorrelated log record (expected behavior, not a bug).
// Use FIXPP_LOG0 ONLY for truly context-free sites. Control-plane handlers
// and listener coroutines that have an Engine& MUST use FIXPP_ELOG instead.
#define FIXPP_LOG0(lvl, cat, fmt, ...)                                        \
    do {                                                                      \
        if constexpr (static_cast<int>(::fixpp::log::Level::lvl)             \
                      >= FIXPP_LOG_MIN_LEVEL) {                               \
            ::fixpp::log::detail::enqueue_record_notrace(                    \
                ::fixpp::log::Level::lvl, cat,                               \
                FIXPP_FORMAT_ID(fmt), ##__VA_ARGS__);                        \
        }                                                                     \
    } while(false)
```

**`FIXPP_FORMAT_ID(fmt)`** evaluates to a `constexpr uint32_t` (CRC32 of `fmt`) so no string is passed to the producer path — formatting is deferred to the drain thread. The `if constexpr` ensures levels below the compile-time cutoff produce **zero bytes** in the final binary.

**Macro selection guide:**

| Macro | When to use | Trace context |
|---|---|---|
| `FIXPP_SLOG(lvl, tc, cat, fmt, ...)` | Inside session coroutines; caller obtains `tc = session.get_trace_context()` first | explicit `fixpp::otel::trace_context const& tc` (synchronous read; no `co_await`) |
| `FIXPP_ELOG(lvl, engine, cat, fmt, ...)` | Control-plane handlers, listener accept, any code with `Engine&` but no `Session&` | `engine.engine_trace_context()` (atomic snapshot) |
| `FIXPP_LOG0(lvl, cat, fmt, ...)` | Destructors, static init, shutdown — truly no context available | Zeroed `trace_id`/`span_id` |

### §4.4 `fixpp::log::Sink` — plugin interface

**Exactly 4 pure-virtual methods** (under the `[const §XIV.2]` ≤5 cap). Justification: each method is semantically irreducible.

```cpp
// include/fixpp/log/sink.hpp
namespace fixpp::log {

// Pluggable sink interface (exactly 4 pure-virtual methods; under the
// [const §XIV.2] ≤5 cap). All methods are called on the DRAIN THREAD only.
// The drain thread is the sole caller — no concurrent emit/flush/close.
//
// Configuration is strongly typed: each concrete sink takes its config in
// its constructor; open() is a no-arg "start running" signal. This eliminates
// the runtime downcast footgun of the v0.1 open(SinkConfig const&) design.
class Sink {
public:
    virtual ~Sink() = default;

    // (1) Open — called once after construction, before any emit().
    //     Implementations open file handles, connect OTLP endpoints, etc.
    //     Returns expected_t<void>; failure causes Logger to disable this
    //     sink (no-op for all subsequent calls) and record log_sink_open_failed.
    [[nodiscard]] virtual fixpp::core::expected_t<void> open() = 0;

    // (2) Emit — deliver one formatted record. Called on drain thread.
    //     MUST return quickly (< a few microseconds ideally). Sinks that
    //     need network I/O (OtlpLogSink) MUST buffer records internally and
    //     export asynchronously (see OtlpLogSink §4.6 for the BatchLogRecordProcessor
    //     pattern). Exceptions must NOT propagate; wrap in try/catch internally.
    virtual void emit(Record const& record) noexcept = 0;

    // (3) Flush — flush all in-flight buffered records within `deadline`.
    //     Called by Logger::shutdown(drain_timeout). Each Sink MUST return
    //     within deadline milliseconds from the call; exceeding the deadline
    //     returns silently (remaining records may be lost). Implementations
    //     are responsible for enforcing their own deadline (e.g., FileSink
    //     uses fdatasync with alarm; OtlpLogSink calls ForceFlush(deadline)).
    //     Note: fsync(2) can block arbitrarily on NFS/ext4 under load —
    //     the deadline escape valve is mandatory to avoid indefinite drain-thread stall.
    virtual void flush(std::chrono::milliseconds deadline) noexcept = 0;

    // (4) Close — teardown. Called once after flush(). Releases resources.
    //     Must not throw; engine shutdown must always complete.
    virtual void close() noexcept = 0;
};

// Factory entry point per [arch §6] rule 4.
// Configuration is captured at factory construction time; make() receives
// only the PMR resource so the Logger constructor (which takes
// pmr::vector<unique_ptr<Sink>>) does not need to carry config knowledge.
//
// N-P2-2 fix (v0.3): SinkFactory::make takes SinkConfig const& so concrete
// factories can be templated or type-erased while still forwarding config.
// Each concrete factory (e.g., FileSinkFactory) captures its config at
// construction; make() creates and returns a configured Sink. The Logger
// does NOT call SinkFactory::make() itself — callers build Sink instances
// (via a factory or via the concrete sink's static make(config) helper) and
// pass them to Logger's constructor. Logger takes ownership and never calls
// make() again.
class SinkFactory {
public:
    virtual ~SinkFactory() noexcept = default;
    [[nodiscard]] virtual std::unique_ptr<Sink>
        make(std::pmr::memory_resource* mr,
             SinkConfig const& cfg) = 0;
};

// Concrete factories for built-in sinks. Each captures its config at
// construction; make(mr, cfg) ignores cfg (already captured).
class FileSinkFactory final : public SinkFactory {
public:
    explicit FileSinkFactory(FileSinkConfig config);
    [[nodiscard]] std::unique_ptr<Sink>
        make(std::pmr::memory_resource* mr, SinkConfig const& cfg) override;
private:
    FileSinkConfig config_;
};

class OtlpLogSinkFactory final : public SinkFactory {
public:
    explicit OtlpLogSinkFactory(OtlpLogSinkConfig config);
    [[nodiscard]] std::unique_ptr<Sink>
        make(std::pmr::memory_resource* mr, SinkConfig const& cfg) override;
private:
    OtlpLogSinkConfig config_;
};

}  // namespace fixpp::log
```

**Why 4 and not 3 or 5.** `open()` is separate from the constructor because construction must be infallible (factory pattern per `[arch §6]`); open can fail. `emit()` and `flush()` are distinct because `emit()` is per-record (tight loop) while `flush()` is a bulk-drain with a deadline. `close()` is separate from `flush()` for two-phase teardown. Merging any two would force per-record overhead or lose flush-without-close. Count: 4 — under the `[const §XIV.2]` ≤5 cap; no justification paragraph needed.

### §4.5 `fixpp::log::FileSink`

```cpp
// include/fixpp/log/file_sink.hpp
namespace fixpp::log {

struct FileSinkConfig : SinkConfig {
    std::filesystem::path  directory;             // directory for log files.
    std::string            base_name = "fixpp";   // prefix for file names.
    std::uint64_t          max_file_bytes = 256u * 1024u * 1024u;  // 256 MiB.
    std::uint32_t          max_keep_count = 8;    // keep at most N rotated files.
    bool                   async_fsync   = true;  // fsync on flush (drain thread).
    // Log file naming:
    //   Live file:     <base_name>.log
    //   Rotated files: <base_name>.<close_timestamp_iso8601>.log
    // On rotation: rename current <base_name>.log to
    //   <base_name>.<close_timestamp_iso8601>.log, then open a fresh
    //   <base_name>.log. Oldest rotated file deleted when count > max_keep_count.
};

class FileSink final : public Sink {
public:
    explicit FileSink(FileSinkConfig config);

    [[nodiscard]] fixpp::core::expected_t<void> open() override;

    void emit(Record const& record) noexcept override;
    void flush(std::chrono::milliseconds deadline) noexcept override;
    void close() noexcept override;

    // Accessors.
    [[nodiscard, clang::lifetimebound]]
    std::filesystem::path const& current_path() const noexcept;
    [[nodiscard]] std::uint64_t  bytes_written() const noexcept;
    [[nodiscard]] std::uint32_t  rotation_count() const noexcept;

    static std::unique_ptr<FileSink> make(FileSinkConfig config);
};

}  // namespace fixpp::log
```

**Rotation.** When `bytes_written()` exceeds `max_file_bytes`, the drain thread renames the current file and opens a fresh file. Naming scheme: live file = `<base_name>.log`; on rotation, the live file is renamed to `<base_name>.<close_timestamp_iso8601>.log`. File I/O is synchronous on the drain thread (acceptable; drain thread is off the hot path).

**Flush contract.** `FileSink::flush(deadline)` calls `::fdatasync(fd)` on the drain thread. The `deadline` parameter is enforced internally: if `fdatasync` does not return within `deadline` (checked via a `timerfd` or `alarm`-based escape), the flush returns silently (records may be lost). This prevents the drain thread from blocking indefinitely on NFS/ext4 storage under load — a mandatory invariant per N-P1-3. The producer thread does **not** directly call `fsync`; it never blocks on I/O.

### §4.6 `fixpp::log::OtlpLogSink`

```cpp
// include/fixpp/log/otlp_log_sink.hpp
namespace fixpp::log {

struct OtlpLogSinkConfig : SinkConfig {
    std::string   endpoint;              // e.g. "http://collector:4318/v1/logs"
    bool          use_grpc = false;      // false = HTTP/protobuf; true = gRPC
    // TLS config: reuses [2g §4.1] cert_source for the OTLP endpoint.
    // null = plain HTTP (dev/test); non-null = TLS per [const §XII].
    std::shared_ptr<fixpp::tls::cert_source> cert_source;
    std::chrono::milliseconds export_timeout = std::chrono::seconds(10);
    std::size_t  max_export_batch = 512;   // records per OTLP export batch.
    std::uint32_t max_export_retries = 3;  // retries before drop + counter.
};

class OtlpLogSink final : public Sink {
public:
    explicit OtlpLogSink(OtlpLogSinkConfig config);

    [[nodiscard]] fixpp::core::expected_t<void> open() override;

    // Translates fixpp::log::Record → opentelemetry::logs::LogRecord:
    //   timestamp     → TimeUnixNano
    //   level         → SeverityNumber + SeverityText
    //   trace_id      → TraceId (16-byte hex)
    //   span_id       → SpanId (8-byte hex)
    //   format_id     → Body (resolved format string, formatted with args)
    //   category      → Attribute "fixpp.log.category"
    // Hands the LogRecord to the SDK's BatchLogRecordProcessor — non-blocking.
    // The batch processor buffers records internally and exports in background.
    // emit() returns immediately; no gRPC/HTTP call on the drain thread.
    void emit(Record const& record) noexcept override;
    // flush(deadline): calls OTel SDK BatchLogRecordProcessor::ForceFlush(deadline).
    // Returns within deadline; drops remaining records on timeout.
    void flush(std::chrono::milliseconds deadline) noexcept override;
    void close()  noexcept override;

    static std::unique_ptr<OtlpLogSink> make(OtlpLogSinkConfig config);
};

}  // namespace fixpp::log
```

**OBS-003 delivery.** `OtlpLogSink` is the LOG-002 + OTLP exporter entry. It satisfies the "same sink interface backs OTel log export" constraint from `[const §XIII.4]`. `emit()` hands records to the OTel C++ SDK's `BatchLogRecordProcessor` — the batch processor has its own internal queue and exports in a background thread managed by the SDK. `emit()` does NOT call the OTLP transport directly; it is non-blocking on the drain thread. The gRPC/HTTP transport is managed entirely by the SDK's internal background exporter, not by `fixpp::transport`.

**TLS for OTLP endpoint.** If `cert_source` is non-null, the OTel SDK's gRPC/HTTP channel is configured with TLS using the credentials obtained from the `cert_source`. This reuses the `[2g §4.1]` `cert_source` interface — the same operational cert that FIX sessions use can secure the OTLP channel.

### §4.7 `fixpp::log::SyslogSink`

```cpp
// include/fixpp/log/syslog_sink.hpp
namespace fixpp::log {

struct SyslogSinkConfig : SinkConfig {
    std::string ident = "fixpp";        // passed to openlog(3).
    int         facility = LOG_DAEMON;  // syslog facility.
    // Level mapping (LOG_* from <syslog.h>):
    //   trace, debug → LOG_DEBUG
    //   info         → LOG_INFO
    //   warn         → LOG_WARNING
    //   error        → LOG_ERR
    //   fatal        → LOG_CRIT
};

class SyslogSink final : public Sink {
public:
    explicit SyslogSink(SyslogSinkConfig config);

    // open()/emit()/flush()/close() are only called after a successful open().
    // Logger enforces this invariant: if open() returns an error, the sink
    // is disabled and emit/flush/close are never called.
    [[nodiscard]] fixpp::core::expected_t<void> open() override;

    void emit(Record const& record) noexcept override;  // calls syslog(3).
    void flush(std::chrono::milliseconds deadline) noexcept override;  // no-op (kernel-managed).
    void close()  noexcept override;  // calls closelog(3).

    static std::unique_ptr<SyslogSink> make(SyslogSinkConfig config);
};

}  // namespace fixpp::log
```

### §4.8 `fixpp::otel::TracerProvider` and `fixpp::otel::MeterProvider`

```cpp
// include/fixpp/otel/providers.hpp
namespace fixpp::otel {

// OTel resource attributes used at provider construction.
struct OtelResourceAttributes {
    std::string service_name;
    std::string service_version;
    std::string deployment_environment;
    // Additional key-value attributes (e.g., "host.name", "os.type").
    std::vector<std::pair<std::string, std::string>> extra;
};

struct OtelConfig {
    std::string                endpoint;   // OTLP endpoint (shared for traces+metrics).
    bool                       use_grpc = false;
    OtelResourceAttributes     resource;
    std::shared_ptr<fixpp::tls::cert_source> cert_source;  // null = no TLS.
    std::chrono::milliseconds  export_interval = std::chrono::seconds(60);
    std::chrono::milliseconds  export_timeout  = std::chrono::seconds(30);
};

// Thin RAII wrapper around opentelemetry::trace::TracerProvider.
// Lifetime: owned by EngineConfig as shared_ptr; survives all Sessions.
class TracerProvider {
public:
    explicit TracerProvider(OtelConfig config);
    ~TracerProvider();

    // Returns a tracer scoped to the named instrumentation library.
    // Lifetime: the returned pointer is borrowed from this provider;
    // must not outlive the TracerProvider.
    [[nodiscard, clang::lifetimebound]]
    opentelemetry::trace::Tracer* get_tracer(std::string_view name) const;

    // Shutdown and flush pending exports. Called at Engine::close().
    void shutdown();
};

// Thin RAII wrapper around opentelemetry::metrics::MeterProvider.
class MeterProvider {
public:
    explicit MeterProvider(OtelConfig config);
    ~MeterProvider();

    [[nodiscard, clang::lifetimebound]]
    opentelemetry::metrics::Meter* get_meter(std::string_view name) const;

    void shutdown();
};

}  // namespace fixpp::otel
```

**OTel SDK integration.** `TracerProvider` and `MeterProvider` are thin RAII wrappers; they do not re-implement any OTel data model. The OTel C++ SDK manages its own internal allocations (not under PMR control — this caveat is documented in §8 and is acceptable because the OTel SDK is a dependency, not engine-internal code).

**EngineConfig fields (confirmed by this doc; stubs added by 2d).** Per `[2d §4.4]`:
- `std::shared_ptr<fixpp::core::Logger>  logger` — 2k confirms the type is `fixpp::log::Logger` aliased as `fixpp::core::Logger` (the forward declaration in `core` per `[arch §4.1]` is the same type).
- `std::shared_ptr<fixpp::otel::TracerProvider>  tracer`
- `std::shared_ptr<fixpp::otel::MeterProvider>   meter`

`SessionConfig` adds:
```cpp
std::shared_ptr<fixpp::log::Logger>            logger_override;   // null → engine default.
std::shared_ptr<fixpp::otel::TracerProvider>   tracer_override;   // null → engine default.
```
Per `[2d §4.5]` engine-anchor + session-override pattern. `meter_override` is omitted: metrics are always engine-scoped (per-session metric override is post-v1.0; sessions share the same metric namespaces).

### §4.9 `fixpp::otel::SessionSpans`

```cpp
// include/fixpp/otel/session_spans.hpp
namespace fixpp::otel {

// RAII span helper for the four latency domains that OBS-001 requires
// to be traced: session lifecycle, parse, store, dispatch.
//
// Constructed once per session open; lives until session close.
// SessionSpan, ParseSpan, StoreSpan, DispatchSpan are inner RAII types
// that start/end their span on construction/destruction.
class SessionSpans {
public:
    // Construct from the session's effective TracerProvider.
    // comp_id is used as a span attribute (sender/target CompID pair).
    explicit SessionSpans(TracerProvider& provider,
                          std::string_view sender_comp_id,
                          std::string_view target_comp_id,
                          trace_context const& parent_ctx);
    ~SessionSpans();  // ends the session lifecycle span.

    // --- Session lifecycle span (wraps the session open..close window) ---
    // Started at SessionSpans construction; ended at destruction.
    // Attributes: fixpp.session.sender_comp_id, fixpp.session.target_comp_id.
    // trace_id / span_id are accessible via session_trace_context().
    [[nodiscard]] trace_context session_trace_context() const noexcept;

    // --- Per-message latency helpers (returned by value; start on construction) ---

    // RAII span for parse latency: from bytes-in to Record constructed.
    struct ParseSpan {
        explicit ParseSpan(SessionSpans& parent);
        ~ParseSpan();  // sets span status OK/ERROR, records latency_ns attribute.
        void set_msg_type(std::string_view msg_type) noexcept;
        void set_error() noexcept;
    private:
        opentelemetry::trace::Scope scope_;
    };

    // RAII span for store write latency: from store write start to completion.
    struct StoreSpan {
        explicit StoreSpan(SessionSpans& parent);
        ~StoreSpan();
        void set_seq_num(std::int64_t seq_num) noexcept;
        void set_error() noexcept;
    private:
        opentelemetry::trace::Scope scope_;
    };

    // RAII span for dispatch latency: from fromApp entry to return.
    struct DispatchSpan {
        explicit DispatchSpan(SessionSpans& parent);
        ~DispatchSpan();
        void set_msg_type(std::string_view msg_type) noexcept;
        void set_error() noexcept;
    private:
        opentelemetry::trace::Scope scope_;
    };

    // The session lifecycle span's tracer (parent for sub-spans).
    [[nodiscard, clang::lifetimebound]]
    opentelemetry::trace::Tracer* tracer() const noexcept;
};

}  // namespace fixpp::otel
```

**Parent-child span relationship.** `SessionSpans` holds the session lifecycle span as the parent. `ParseSpan`, `StoreSpan`, and `DispatchSpan` are child spans of the session lifecycle span, created using the **explicit parent context API**: `tracer->StartSpan(name, {}, StartSpanOptions{.parent = session_ctx_})` where `session_ctx_` is the `opentelemetry::context::Context` member stored in the `SessionSpans` object. `opentelemetry::trace::Scope` is NOT used for sub-spans — `Scope` mutates thread-local SDK context, which is prohibited by `[const §XIII.3]`. The sub-span constructors receive `SessionSpans& parent` and read `parent.session_ctx_` directly. This ensures correct parent span ID even when the `ParseSpan` is constructed on a different OS thread than the one that started the session lifecycle span. TS-12 asserts this invariant.

**Integration with LOG-003.** The `session_trace_context()` accessor returns the `trace_context` (trace_id + span_id) of the session lifecycle span. The session FSM stores this into the `session_local<trace_context>` slot at session open per `[2d §4.6]`; `FIXPP_SLOG(level, tc, ...)` call sites read it synchronously via `session.get_trace_context()` (the accessor added by Appendix D §D.1) and pass `tc` explicitly to the macro (no `co_await` needed).

### §4.10 `fixpp::otel::PrometheusExporter` and `fixpp::otel::OtlpExporter`

```cpp
// include/fixpp/otel/exporters.hpp
namespace fixpp::otel {

// Prometheus exporter: exposes /metrics HTTP endpoint.
// v1.0 minimum: embedded HTTP server (one dedicated thread, non-ASIO).
// The embedded server is the lowest-friction path with zero external process
// dependency; push-gateway integration is post-v1.0.
struct PrometheusConfig {
    std::string   host = "0.0.0.0";
    std::uint16_t port = 9464;          // default Prometheus port for OTel.
    std::string   metrics_path = "/metrics";
    // TLS for the metrics endpoint: null = plain HTTP (default, acceptable
    // for internal scraping in a k8s pod); non-null = TLS.
    std::shared_ptr<fixpp::tls::cert_source> cert_source;
};

// PrometheusExporter: OTel C++ SDK PrometheusExporter implements MetricReader
// (pull model, not PushMetricExporter). It exposes an embedded HTTP server
// that the Prometheus scraper polls. It is registered on a MeterProvider
// via AddMetricReader(), NOT via PeriodicExportingMetricReader.
class PrometheusExporter {
public:
    explicit PrometheusExporter(PrometheusConfig config);
    ~PrometheusExporter();  // stops HTTP server.

    // Returns the OTel SDK's MetricReader interface (pull model).
    // Register with MeterProvider::AddMetricReader().
    // NOTE: PrometheusExporter is a MetricReader, NOT a PushMetricExporter.
    // Do NOT pass to PeriodicExportingMetricReader.
    [[nodiscard, clang::lifetimebound]]
    opentelemetry::sdk::metrics::MetricReader* sdk_reader() noexcept;
};

// OTLP exporter for metrics: exports via gRPC or HTTP/protobuf (push model).
// Implements PushMetricExporter. Wrapped in PeriodicExportingMetricReader
// which is then registered on the MeterProvider via AddMetricReader().
class OtlpMetricExporter {
public:
    explicit OtlpMetricExporter(OtelConfig config);
    ~OtlpMetricExporter();

    // Returns a PeriodicExportingMetricReader wrapping this PushMetricExporter.
    // Register with MeterProvider::AddMetricReader().
    [[nodiscard, clang::lifetimebound]]
    opentelemetry::sdk::metrics::MetricReader* sdk_reader() noexcept;
};

// Builder helper to wire both exporters onto a single MeterProvider
// (OBS-002 dual-export requirement).
//
// The OTel C++ SDK pattern for dual export is: one MeterProvider with TWO
// MetricReaders, each registered via AddMetricReader():
//   Reader 1: PrometheusExporter (pull, embedded HTTP on port 9464).
//   Reader 2: PeriodicExportingMetricReader wrapping OtlpMetricExporter (push).
//
// Usage:
//   auto meter_provider = OtelDualExportBuilder{}
//       .with_prometheus(prometheus_cfg)
//       .with_otlp(otel_cfg)
//       .build();
//
// A single meter.add(counter, 1) call propagates to both readers automatically
// via the MeterProvider's internal fan-out.
class OtelDualExportBuilder {
public:
    OtelDualExportBuilder& with_prometheus(PrometheusConfig cfg);
    OtelDualExportBuilder& with_otlp(OtelConfig cfg);
    [[nodiscard]] std::shared_ptr<MeterProvider> build();
};

}  // namespace fixpp::otel
```

**Dual-export mechanics.** The OTel C++ SDK (≥ 1.12) pattern for dual export is two metric readers on one `MeterProvider`. `OtelDualExportBuilder::build()` calls `MeterProvider::AddMetricReader()` twice — once with the `PrometheusExporter`'s `MetricReader*` (pull model) and once with the `PeriodicExportingMetricReader` wrapping `OtlpMetricExporter`'s `PushMetricExporter*` (push model). The SDK's `MeterProvider` fans out metric observations to all registered readers. A single `meter.add(counter, 1)` call propagates to both readers automatically. There is no `MultiMetricExporter` in the public SDK API that accepts a `PrometheusExporter`; `MultiMetricExporter` is an SDK-internal detail that takes `PushMetricExporter*` items — `PrometheusExporter` has a different base type and cannot be passed to it.

**Prometheus embedded HTTP server.** v1.0 ships an embedded single-threaded HTTP server (not ASIO — the Prometheus exposition protocol is synchronous pull; a dedicated thread is simpler and correct). The server thread does not interact with the engine's ASIO executor. Port default: 9464 (OTel Prometheus receiver convention). Operators who want push-gateway instead of scrape can configure the OTLP exporter with a `prometheus-pushgateway` target; the built-in server is still started unless explicitly disabled (post-v1.0 config knob).

---

## §5 Public C ABI

**No C-level log/otel API in v1.0 (general case).** Users access log/otel exclusively through the C++ API. The `StreamLogs` gRPC RPC integration with the engine logger requires a C-ABI subscription surface (`fixpp_logger_subscribe_drain_cb` or equivalent in `c_api/log.h`); this surface is deferred to the 2j cross-doc amendment cycle (see §7 and §10 Q1). Until that amendment lands, `StreamLogs` operates off OTel metrics/traces only (via the `PrometheusExporter` and `OtlpExporter` that service/ can access through the OTel collector without any direct engine C-ABI call).

**Rationale.** Exposing log/otel through the C ABI would require stable serialisation of every OTel span type, every metric type, and every log record format across the ABI boundary. The OTel data model is large and evolving; coupling it to the C ABI would create a version-stability burden that is not justified for v1.0. The C ABI's `fixpp_error_t` system already provides error-level feedback to C consumers; structured observability is a C++ consumer concern.

**What `c_api/log.h` and `c_api/otel.h` contain in v1.0:**

```c
/* c_api/log.h — v1.0 placeholder.
 * No extern "C" symbols. The log API is C++ only in v1.0.
 * Post-v1.0 roadmap: fixpp_log_record_t, fixpp_logger_flush() may appear.
 */
#ifndef FIXPP_C_API_LOG_H
#define FIXPP_C_API_LOG_H
#include <stdint.h>
/* FIXPP_LOG_API_VERSION is the minor version of this header. */
#define FIXPP_LOG_API_VERSION 0u
#endif  /* FIXPP_C_API_LOG_H */

/* c_api/otel.h — v1.0 placeholder. */
#ifndef FIXPP_C_API_OTEL_H
#define FIXPP_C_API_OTEL_H
#include <stdint.h>
#define FIXPP_OTEL_API_VERSION 0u
#endif  /* FIXPP_C_API_OTEL_H */
```

Both headers are included from `fix/c_api.h` per `[arch §4.10]`; they are listed to confirm they exist and contain no ABI-affecting symbols (no symbols → no ABI drift risk, no `abidiff` entries, no reentrancy annotations needed).

---

## §6 Behavioral contract

### §6.1 Allocation / exceptions / threading

**Producer thread — zero allocation per record.**  
`FIXPP_SLOG(level, tc, cat, fmt, ...)`, `FIXPP_ELOG(level, engine, cat, fmt, ...)`, or `FIXPP_LOG0(level, cat, fmt, ...)` on the producer thread:
- Evaluates `if constexpr (level >= FIXPP_LOG_MIN_LEVEL)` — if false, the entire expression is zero bytes.
- For `FIXPP_SLOG`: the caller pre-calls `session.get_trace_context()` (one `const noexcept` member function call — no allocation, no `co_await`) to obtain `tc`, then passes it explicitly to the macro; for `FIXPP_ELOG`: reads `engine.engine_trace_context()` (one atomic load); for `FIXPP_LOG0`: uses zeroed trace fields.
- Calls `detail::enqueue_record(...)` which checks `enabled_categories_mask_` (one atomic load), claims a slot via CAS on the MPSC ring sequence counter (load-check-CAS; see §4.3), and copies the 256-byte `Record` into the slot.
- Total: one member call or atomic load (FIXPP_SLOG/FIXPP_ELOG), one atomic load (category mask), one CAS loop, one 256-byte memcpy. **No `operator new`, no `malloc`, no system calls, no locks.**

**Exceptions.** No exceptions are emitted across the log-queue boundary. The drain thread runs all sink calls inside `try/catch(...)`. Caught exceptions increment a per-sink error counter accessible via `Logger::sink_error_count(size_t sink_index)`. The drain thread continues running after a sink exception.

**Threading.** The producer can call `FIXPP_SLOG(...)` (session strand), `FIXPP_ELOG(...)` (engine scope), or `FIXPP_LOG0(...)` (any thread) from any thread. The MPSC ring is designed for N producers, 1 consumer. The drain thread is a dedicated OS thread spawned by `Logger`; it is **not** an ASIO strand thread — it runs independently of the ASIO executor. The drain thread MUST NOT call back into the engine or session while holding session state (it has no references to session objects; it only reads the pre-captured fields of `Record`).

**No `std::mutex` in producer coroutine context** per `[const §XI.3]`. The MPSC enqueue path uses only `std::atomic` operations and a `memcpy`; no mutex involved.

**`drop_newest` overflow** is the default policy, permitted per `[const §XIII.2]` and `[const §XV.15]` on the log path. In a FIFO ring, dropping the newest-produced record preserves all older in-flight records — semantically equivalent to "drop-oldest is permitted" under `[const §XIII.2]`. The drop counter is `std::atomic<uint64_t> drop_count_` on `Logger`.

**`block` mode — session-strand prohibition.** `block` mode MUST NOT be used from a session-strand coroutine. It pins the executor OS thread, stalling all sessions on that thread. This is equivalent to holding `std::mutex` in a coroutine context, prohibited by `[const §XI.3]`. `block` mode is safe only for dedicated non-coroutine producer threads not on the session executor. See §4.3 for the full caveat.

**No `thread_local` for trace context** per `[const §XIII.3]`. The three-macro pattern (`FIXPP_SLOG` / `FIXPP_ELOG` / `FIXPP_LOG0`) replaces the v0.1 `FIXPP_LOG` with its implicit context-acquisition mechanism. See §4.3 for the LOG-003 three-macro design.

### §6.2 Latency Tier 1 ceilings

**Producer path.** `FIXPP_SLOG(...)` / `FIXPP_ELOG(...)` / `FIXPP_LOG0(...)` on the producer thread: **≤ 50 ns mean** on the non-overflow path on the reference CI hardware. This ceiling is set at the lower end of the p99 for the quill-class loggers benchmarked in `[SYN §3.8]` (~30–80 ns for deferred-format async loggers). The own-impl Disruptor MPSC CAS is typically 15–30 ns uncontended on modern x86-64 with a hot cache line; the 256-byte `memcpy` adds ~5–10 ns for a warm L1 cache. Total expected mean: 25–40 ns. The 50 ns ceiling provides 25% headroom for NUMA effects and moderate contention (TS-8 enforces this in nightly bench).

**Drain thread throughput.** Not a hard ceiling (drain runs off the hot path). Expected throughput: ≥ 5M records/second at steady state (drain thread formats + fan-outs to a `FileSink`). This exceeds the FIX message rate on a single engine instance (~100K msg/s for high-throughput FIX) by 50×, so the drain thread should never fall behind except under burst.

### §6.3 Errors introduced by this design

2k owns the `[1000, 1099]` block per `[2i §1.1]` reserved-block layout.

| `fixpp::core::error` variant | `fixpp_error_t` value | Meaning |
|---|---|---|
| `log_queue_overflow` | 1000 | MPSC queue overflowed; record dropped (drop_newest mode). Increments `Logger::drop_count_` atomically. Not returned to the calling application (`FIXPP_SLOG`/`FIXPP_ELOG`/`FIXPP_LOG0` are void). Internal C++ variant only; not surfaced as a C-ABI error return in v1.0 (see note below). |
| `log_sink_open_failed` | 1001 | `Sink::open()` returned an error at Logger startup. The sink is disabled; Logger continues with remaining sinks. |
| `log_sink_write_failed` | 1002 | `Sink::emit()` threw an unexpected exception (caught by the drain thread). |
| `log_sink_flush_failed` | 1003 | `Sink::flush()` threw an unexpected exception. |
| `log_drain_timeout` | 1004 | `Logger::shutdown(drain_timeout)` timed out before the drain thread processed all in-flight records. Returned as `unexpected(log_drain_timeout)` from `shutdown()`; increments `timeout_drop_count()` (not `drop_count()` — overflow drops and timeout-abandoned records are tracked separately). Caller may log or ignore; records not yet drained are lost. |
| `otel_export_failed` | 1010 | OTel SDK reported an OTLP export failure (trace or metric batch). Recorded in an internal counter; engine continues. |
| `otel_provider_init_failed` | 1011 | `TracerProvider` or `MeterProvider` construction failed (e.g., invalid endpoint URL, OTel SDK error). Returned by constructors as an `expected_t<void>` failure. The engine substitutes a no-op provider and continues. |

Note on `log_queue_overflow` (1000): This variant occupies slot 1000 in the `[1000, 1099]` C-ABI block per `[2i §1.1]` reservation. It is NOT returned as a C-ABI error in v1.0 (the macros are `void` and the overflow counter is accessible only via `Logger::drop_count()` C++ accessor). The slot is reserved for a future v1.x C-ABI `fixpp_logger_drop_count()` accessor that would expose the counter to C consumers. Until then, the slot is occupied but maps to no `fixpp_error_t` return path.

Values 1005–1009 and 1012–1099 are reserved for v1.x (e.g., future `GrpcStreamSink` error variants, additional OTLP failure modes).

### §6.4 OTel trace context acquisition (LOG-003)

The three-macro pattern (RC#3 + RC#6, completed in v0.3; macro contract finalised in v0.4) eliminates the v0.1 contradictory single-macro design and completes the three-tier acquisition model. Three-tier acquisition:

**Tier 1 — Session strand (`FIXPP_SLOG`):** Session-strand callers call `session.get_trace_context()` to obtain the `trace_context` struct (a `const noexcept` accessor that reads the `session_local<trace_context>` slot stored in the `Session` object — added to `Session`'s public surface by 2k Appendix D §D.1), then pass it explicitly to `FIXPP_SLOG(level, tc, cat, fmt, ...)`. No `co_await` needed — the session holds the context as a direct member. No `thread_local`. The explicit `tc` parameter makes the context source unambiguous: it is `fixpp::otel::trace_context const&`, not the global `fixpp::current_trace_context` awaitable (`[2d §4.6]`). The `trace_id`/`span_id` are populated from the session lifecycle span set at `Session::open`.

**Tier 2 — Engine-scope non-session (`FIXPP_ELOG`):** Control-plane handlers and listener accept coroutines that hold an `Engine&` use `FIXPP_ELOG(level, engine, cat, fmt, ...)`. The macro reads `engine.engine_trace_context()` — an atomic snapshot of `EngineConfig::engine_trace_context` (per `[2d §4.4]`). This provides engine-scope correlation for `[2j]`-owned control-plane paths, ensuring their log records carry the engine's root span IDs and can be correlated in the OTel backend. `FIXPP_ELOG` is the Tier 2 call site; control-plane code MUST use it instead of `FIXPP_LOG0`.

**Tier 3 — Off-engine contexts (`FIXPP_LOG0`):** Used for destructors, static init, shutdown code, and any site where neither a `Session&` nor an `Engine&` is in scope. `trace_id` and `span_id` are zeroed in the `Record`. This is expected and documented behavior — a log record from a truly context-free site has no correlated span. The OTel collector treats zeroed `trace_id` as an uncorrelated record. `FIXPP_LOG0` MUST NOT be used in control-plane or listener code where an `Engine&` is available.

**`thread_local` is unconditionally prohibited** per `[const §XIII.3]`. The three-macro pattern ensures no path relies on `thread_local` for context propagation.

### §6.5 DoS surface

**Bounded MPSC queue.** Maximum depth configured at `Logger` construction (`LoggerConfig::capacity`). Records beyond capacity are dropped (or the caller spins in `block` mode). No unbounded growth. A malicious or runaway caller cannot OOM the engine via logging.

**`FileSink` disk usage.** `max_file_bytes * max_keep_count` bounds total disk consumption. On rotation, the oldest file is deleted before the file count exceeds `max_keep_count`. A runaway log rate will cause high rotation frequency but will not exceed the disk bound.

**`OtlpLogSink` retry cap.** Retries are capped at `max_export_retries` (default 3). A failed OTLP connection causes records to be dropped after 3 retries; the drop is recorded via `otel_export_failed` error counter. No retry storm: each export attempt has a bounded `export_timeout`.

**Drain thread CPU.** The drain thread may be CPU-pinned via `LoggerConfig::drain_cpu_affinity`. Default: no affinity (OS schedules). The drain thread does no I/O on the producer-facing hot path, so it does not compete with the ASIO executor threads in the typical deployment.

### §6.6 Shutdown / flush contract

On `Engine::close()`:
1. All sessions are closed (per `[2d §6.5]` two-phase close); session FSMs reach the `Disconnected` state.
2. `Engine::close()` calls `logger->shutdown(LoggerConfig::drain_timeout)` — instructs the drain thread to process all in-flight records, then calls `Sink::flush(deadline)` on each sink. `shutdown()` returns `[[nodiscard]] expected_t<void>`; if `drain_timeout` expires before all records are drained, `shutdown()` returns `unexpected(log_drain_timeout)` (error 1004) and increments `timeout_drop_count()`. The engine propagates or logs this condition and proceeds; lost records are observable via `logger->timeout_drop_count()`. `drop_count()` tracks only overflow drops (separate counters for separate failure modes).
3. `Engine::close()` calls `tracer_provider->shutdown()` and `meter_provider->shutdown()` — flushes pending OTLP exports.
4. `Logger`'s destructor joins the drain thread (exits after processing the flush sentinel).

The `drain_timeout` value comes from `LoggerConfig::drain_timeout` (default 5 seconds). To use a non-default timeout, callers set `LoggerConfig::drain_timeout` at engine construction time.

**Session-scoped records.** Sessions are closed before `logger->flush()` is called. Their queued log records are already in the MPSC ring by the time the session FSM completes teardown. The `logger->flush()` call drains them.

---

## §7 Integration with adjacent modules

**`[2d]` threading.** 2k CONSUMES `fixpp::current_trace_context` and `session_local<trace_context>`. Two amendments to 2d are made (Appendix D §D.1): (a) the `logger_override` / `tracer_override` fields are added to `SessionConfig` (replacing `log_sink_override`); (b) `Session::get_trace_context() const noexcept` is added to `Session`'s public surface as a read-only accessor to the `session_local<trace_context>` slot, required by the `FIXPP_SLOG` macro contract. The drain thread is a dedicated OS thread; it is NOT an ASIO strand thread. `Logger` is an `EngineConfig` field following `[2d §4.4]` engine-config pattern; `SessionConfig::logger_override` follows `[2d §4.5]`.

**`[2d §7.9]` clock source.** Log record timestamps come from `effective_clock.now()` where `effective_clock = SessionConfig::clock_override ?: EngineConfig::clock` per `[2d §7.9]`. In non-session contexts, `EngineConfig::clock->now()` is used directly. This means `mock_clock` in tests deterministically controls log record timestamps — test harnesses can verify time-sensitive log output.

**`[2e]` msgstore.** `FileSink`'s async-fsync pattern is analogous to `FileStore::flush_for_session_close()` in `[2e §6.1.4]`. Both defer fsync to a non-hot-path thread. No direct API dependency.

**`[2g]` tls.** `OtlpLogSink` and `OtlpExporter` may use TLS for the OTLP endpoint. Both reference `fixpp::tls::cert_source` per `[2g §4.1]` for credentials. The same `cert_source` instance (typically `file_cert_source`) that secures FIX TLS connections can be reused for OTLP TLS.

**`[2h]` transport.** No direct dependency. OTLP HTTP/gRPC transport is managed by the OTel C++ SDK's own transport layer, not by `fixpp::transport`. This is correct per the modular layering in `[arch §2.3]` (the `otel` module depends only on `core`; it does not include `transport/`).

**`[2j]` controlplane.** The `StreamLogs` RPC in `[2j §4.7]` exposes structured-log records over gRPC; the method-by-method contract for `ControlPlane::start()` / `stop()` / `health()` is in `[2j §4.3]`. Control-plane handlers that run under `ControlPlane::start()` (per `[2d §7.8]`) MUST use `FIXPP_ELOG(level, engine, cat, fmt, ...)` for LOG-003 Tier 2 correlation — not `FIXPP_LOG0`. Two candidate paths exist for `StreamLogs`; the v0.1 "Option A" recommendation is **retracted** because it violates `[arch §8]`:

- **Option A-retracted (`GrpcStreamSink` in `service/grpc/`):** BLOCKED. `service/grpc/` code cannot `#include <fixpp/log/sink.hpp>` per `[arch §8]` (the AGPL-boundary structural rule). `<fixpp/log/sink.hpp>` is `X = log` — a forbidden include from `service/`.
- **Option B (deferred to v1.x): C-ABI subscription surface.** Add `fixpp_logger_subscribe_drain_cb(fixpp_engine_t*, callback, ctx)` to `c_api/log.h`, allowing `service/grpc/` to subscribe to drain-thread output via C ABI. This requires lifting the §2 non-goal #7 partial restriction for this minimal symbol. Deferred: requires a 2i amendment adding the symbol to the `[1000, 1099]` block and a reentrancy annotation.

**v1.0 resolution: explicit deferral.** `StreamLogs` integration requires a C-ABI subscription surface in `c_api/log.h` which is deferred to the 2j cross-doc amendment cycle. Until that amendment lands, `StreamLogs` operates off OTel metrics/traces only — the `PrometheusExporter` (scrape endpoint) and `OtlpExporter` (push to collector) provide the observability surface that `service/` can access without any direct engine C-ABI log call. See §10 Q1 for the formal open question.

**`[2l]` tap (upcoming).** `fixpp::tap::TapConsumer` and `fixpp::log::Logger` are sibling abstractions per `[SYN §3.8]`. Both have a producer/consumer split with a bounded buffer and `drop-oldest` semantics on the non-session path. They are intentionally **not coupled** at the API level: tap carries raw FIX message bytes; logger carries structured log records. The `Sink` interface is specific to log records; tap has its own `TapConsumer` variant type.

**Phase 4 session-module spec.** `SessionConfig::logger_override`, `SessionConfig::tracer_override`, `SessionConfig::initial_trace_context`, and `SessionSpans` usage in FSM transitions (Logon → ParseSpan start, etc.) are consumed by the session-module spec. 2k defines these fields and the `SessionSpans` API; the session-module spec uses them.

---

## §8 PMR recap

| Component | PMR resource | Lifetime |
|---|---|---|
| `Logger` MPSC ring buffer | `LoggerConfig::ring_resource` (passed at construction); defaults to `std::pmr::get_default_resource()`. | `Logger` lifetime. |
| `Record` args | Captured by value only — `ArgValue` tagged union (`u64`, `i64`, `f64`, `bool`, `InlineStr<15>`, `StaticStr`). No `string_view`. No heap allocation. `InlineStr` copies ≤15 bytes inline at the call site (no null-terminator; drain thread uses `len`); `StaticStr` stores a `const char*` with caller-asserted static lifetime (`FIXPP_SLIT`). No PMR needed for args. | Copied into `Record` at the FIXPP_SLOG / FIXPP_ELOG / FIXPP_LOG0 call site; safe across the async queue boundary. |
| `Sink` instances | Each `Sink` is heap-allocated via `SinkFactory::make(pmr*)`. The `pmr*` may be the engine's default resource or a user-supplied arena. The `pmr::vector<unique_ptr<Sink>>` passed to `Logger` uses the `ring_resource` PMR by default. | `Logger` lifetime (owned as `pmr::vector<unique_ptr<Sink>>`). |
| `TracerProvider` / `MeterProvider` | Heap-allocated; wrapped in `shared_ptr` in `EngineConfig`. The OTel C++ SDK manages its own internal allocations — **not under PMR control**. | Engine lifetime (shared ownership via `shared_ptr`). |
| `SessionSpans` | Allocated from the session's `session_arena` (per `[2d §4.5]` `SessionConfig::session_arena`). | Session lifetime. |
| OTel SDK internals | OTel C++ SDK uses its own internal allocators. This is a known caveat: the SDK is a dependency, not engine-internal code. Its allocation behaviour is not under 2k's control. Documented here to avoid confusion during allocation auditing. | SDK lifetime. |
| Drain thread stack | OS-allocated (not PMR). One thread per `Logger`. Not engine-heap. | `Logger` lifetime. |

**Zero per-record allocation on producer thread** — upheld by the fixed 256-byte `Record` size and the lock-free ring's pre-allocated slots. The only "allocation" on the producer path is the CAS on the ring sequence counter, which is an atomic instruction, not a heap allocation.

---

## §9 Test seams (≥ 10)

**TS-1 — Compile-time level cutoff.** Build with `FIXPP_LOG_MIN_LEVEL=3` (warn). Verify via `nm`/`objdump` that no `debug` or `info` format strings appear in the binary's `.rodata` section. Alternative: run the benchmark with ASan + a `malloc` interceptor and confirm zero allocations at the call site even when calls are present in source.

**TS-2 — MPSC queue overflow (drop_newest).** Construct a `Logger` with `capacity=1`, `on_overflow=drop_newest`. Emit 100 records from one thread (drain thread sleeping). Assert: `Logger::drop_count() == 99`, exactly 1 record in the ring (the FIRST-enqueued record, since drop_newest preserves the oldest in-flight records), drain thread processes exactly 1 record. Run under TSan to verify no data race on `read_sequence_` (which is `std::atomic<uint64_t>` — producers load with `memory_order_relaxed`, drain thread stores with `memory_order_release`).

**TS-3 — MPSC queue overflow (block — non-coroutine thread only).** Construct a `Logger` with `capacity=1`, `on_overflow=block`. Emit 1 record (ring full). Start second emit from a **raw `std::thread`** (NOT a session-strand coroutine) while drain is paused. Assert: second thread blocks (does not return for ≥ 10 ms). Resume drain. Assert: second thread unblocks and record is processed. Note: this test must NOT use a session executor thread to avoid triggering the `[const §XI.3]` prohibition.

**TS-4 — `FileSink` rotation.** Write records until `bytes_written()` exceeds `max_file_bytes`. Assert: `rotation_count() >= 1`, file count in directory ≤ `max_keep_count`, oldest file deleted. Run under TSan.

**TS-5 — `FileSink` async fsync.** Inject a mock `fsync` hook (intercept `::fsync` via LD_PRELOAD or a function pointer injected into `FileSink` via test-only constructor). Call `logger->flush(timeout)`. Assert: the mock fsync hook was called on the drain thread (not the producer thread). Assert: the producer thread returns from `flush()` after the mock fsync completes, not before.

**TS-6 — LOG-003 correlation (three-macro coverage).** (a) Session strand: Open a session with a known `initial_trace_context` (trace_id = `0xAA...AA`, span_id = `0xBB...BB`). Call `auto const& tc = session.get_trace_context(); FIXPP_SLOG(info, tc, cat::session, "test {}", ArgValue::from_u64(42))`. Assert: `Record::trace_id == 0xAA...AA`, `Record::span_id == 0xBB...BB`. (b) Engine scope: Construct an `Engine` with `EngineConfig::engine_trace_context = {trace_id = 0xCC...CC, span_id = 0xDD...DD}`. Call `FIXPP_ELOG(info, engine, cat::control, "ctrl {}", ArgValue::from_u64(1))`. Assert: `Record::trace_id == 0xCC...CC`, `Record::span_id == 0xDD...DD`. (c) Zero context: Call `FIXPP_LOG0(info, cat::session, "test")` from a raw `std::thread` not associated with any session or engine. Assert: `Record::trace_id` is all-zeros, `Record::span_id == 0`.

**TS-7 — LOG-003 zero-trace fallback.** Call `FIXPP_LOG0(info, cat::session, "test")` from a raw `std::thread` not associated with any session or engine. Assert: `Record::trace_id` is all-zeros, `Record::span_id == 0`. No panic, no undefined behaviour. Run under ASan.

**TS-8 — Compile-time level + runtime category filter combination.** Build with `FIXPP_LOG_MIN_LEVEL=2` (info). Set `cat::wire` category disabled via `logger->set_category_enabled(cat::wire, false)`. Emit (using `FIXPP_LOG0` for simplicity): `FIXPP_LOG0(debug, cat::wire, "d", ...)` (compiles to nothing — TS-1 applies; zero overhead), `FIXPP_LOG0(info, cat::wire, "f", ...)` (compiles but filtered at runtime), `FIXPP_LOG0(info, cat::session, "s", ...)` (reaches sink). Assert: sink receives exactly 1 record (`cat::session`); `logger->drop_count() == 0` (overflow drop counter untouched); `logger->filter_count() == 1` (category filter counter incremented by the wire-info record).

**TS-9 — Latency regression benchmark (Tier 2 nightly).** A Google Benchmark in `bench/log_enqueue.cpp`. Measures `FIXPP_LOG0(info, cat::session, "msg {}", ArgValue::from_u64(42))` end-to-end enqueue latency: mean, p50, p99, p999, max over 10M iterations. Run with the drain thread sleeping (to isolate enqueue cost). Assert: p99 ≤ 50 ns on the reference CI hardware. Baseline stored in `bench/baselines/log_enqueue.json`.

**TS-10 — `OtlpLogSink` translation.** Inject a mock OTLP exporter (`opentelemetry::sdk::logs::LogRecordExporter` stub). Emit one `Record` with known fields (level=info, trace_id=`0xAA…AA`, span_id=`0xBB`, format_id=CRC32("msg {}"), args=[42]). Assert: the mock exporter's `Export()` call receives a `LogRecord` with matching `TimeUnixNano`, `SeverityNumber` (9 = INFO), `TraceId`, `SpanId`, and `Body` ("msg 42" after format resolution).

**TS-11 — `PrometheusExporter` dual export.** Configure `OtelDualExportBuilder` with both `PrometheusExporter` and `OtlpExporter` (mock OTLP). Increment a counter metric 3 times. Force an export cycle. Assert: (a) the mock OTLP exporter received the counter with value 3; (b) a GET to `http://localhost:9464/metrics` returns a text-format Prometheus exposition with the counter at value 3.

**TS-12 — `SessionSpans` lifecycle and parent-child span relationship.** Open a session, construct `SessionSpans`, create and destroy a `ParseSpan`. Close the session (destroy `SessionSpans`). Assert: mock OTel exporter received 2 spans (session lifecycle + parse); parse span's parent span_id == session lifecycle span's span_id; both spans have status OK; `ParseSpan` carries a `fixpp.parse.latency_ns` attribute > 0.

**TS-13 — Quill vs own benchmark spike harness.** A repeatable micro-benchmark in `bench/log_spike.cpp` that:
1. Instantiates `Logger` (own impl) and a `quill`-based logger (if quill is enabled via CMake option `FIXPP_LOG_SPIKE_QUILL=ON`).
2. Runs the load scenario from §1.2: **4 producer threads, 10M records total** (2.5M each), queue capacity 65536, at 10% / 50% / 95% fill rates. Record size: 256 bytes (the locked `Record` struct). CPU affinity: all 4 producer threads pinned to cores 0–3; drain thread pinned per `drain_cpu_affinity`; warmup: 1M records discarded before measurement.
3. Reports per-producer-thread: mean, p50, p99, p999, max enqueue latency for each candidate and fill rate. **p99 and p999 are the primary criteria** (tail latency on the FIX hot path).
4. Checks Criterion A (zero alloc per record on producer thread) via `mallocnesia` hook — fails if any malloc is detected during the measurement window (post-warmup).
5. Output is a JSON file in `bench/spike_results/log_spike_<date>.json`. The Phase 3 engineer runs this spike, records the result in Appendix C, and applies the implementation decision (own impl if quill fails A; quill if it passes all five criteria per §1.2).

---

## §10 Open questions

**Q1.** `StreamLogs` RPC in `[2j §4.7]` (proto schema) / `[2j §4.3]` (method-by-method contract for the ControlPlane interface) — `StreamLogs` integration requires a C-ABI subscription surface (`fixpp_logger_subscribe_drain_cb` or equivalent) in `c_api/log.h`. Both candidate paths in v0.1 were blocked: `GrpcStreamSink` in `service/grpc/` violates `[arch §8]`; the v0.1 §2 non-goal #7 blocked the C-ABI path. **v0.3 resolution: explicit v1.0 deferral.** `StreamLogs` ships without engine-log-stream integration in v1.0; the `[2j §4.7]` `StreamLogs` RPC operates off OTel metrics/traces only. The C-ABI subscription surface is a 2j cross-doc amendment target for v1.x. The formal amendment requires: (a) a new `fixpp_logger_subscribe_drain_cb` symbol in `c_api/log.h` with a `fixpp_log_record_t` C struct; (b) a 2i amendment adding the symbol to the `[1000, 1099]` block reentrancy table. Status: open; blocked on 2i amendment cycle.

**Q2.** W3C TraceContext injection into outbound FIX messages — explicitly post-v1.0 per §2 non-goals. Deferred to Phase 4 session-module spec + a future amendment.

**Q3.** OTel C++ SDK version pinning — lock the exact version in `CMakeLists.txt` / Conan recipe. Minimum constraint: OTel C++ SDK ≥ 1.12 (first version with stable logs API). **RESOLVED at 017 build scaffold (2026-06-02):** pinned exactly to `opentelemetry-cpp/1.26.0` (latest on Conan Center; upstream `1.27.0` is not yet packaged) with `with_abi_v2=True` (API V2, `OPENTELEMETRY_ABI_VERSION_NO=2`), `with_no_deprecated_code=True`, `with_prometheus=True` (the recipe default is `False` — required by OBS-002/FR-017), `with_otlp_http=True`. Recorded in `conanfile.py` + spec `017-log-otel` FR-023, which owns the pin.

**Q4.** `PrometheusExporter` embedded HTTP server port and thread model — the embedded single-threaded server is the v1.0 minimum. Post-v1.0 options: (a) push-gateway integration, (b) ASIO-based HTTP server (eliminates the dedicated server thread). The v1.0 minimum (embedded server on port 9464) is sufficient for the OBS-002 requirement.

**Q5.** `quill` Conan package name and version — **RESOLVED (2026-06-02):** pinned to `quill/11.1.0` (latest on Conan Center; refreshed from the original `quill/3.x`), pulled only when `FIXPP_LOG_SPIKE_QUILL=ON` (spike-only, TS-13). The benchmark spike harness targets the quill 11.x API.

**Q6.** `GrpcStreamSink` backpressure policy — `close-on-overflow` per `[2j §6.4]`. Confirm at 2j sign-off whether the `GrpcStreamSink` should also support `drop-oldest` (for less noisy gRPC stream teardowns on slow consumers) or if `close-on-overflow` is the sole policy. Defer to the 2j cross-doc amendment.

---

## §11 Hand-off

This doc unblocks:

- **`2l-tap.md`**: session-tap can reference the `Sink` abstraction pattern from 2k. Not hard-blocked but informed. 2l may introduce a `TapSink` that forwards tap records to an external consumer using the same interface shape.
- **017 build scaffold** (formerly labelled "Phase 3"): OTel C++ SDK Conan recipe `opentelemetry-cpp/1.26.0` (`with_abi_v2=True`/`with_prometheus=True`/`with_otlp_http=True`), quill Conan recipe `quill/11.1.0` (spike-only), `FIXPP_LOG_MIN_LEVEL` CMake option, `FIXPP_LOG_SPIKE_QUILL` CMake option.
- **Phase 4 session-module spec**: `SessionConfig::logger_override`, `SessionConfig::tracer_override`, `SessionConfig::initial_trace_context`, `SessionSpans` construction in the FSM open path, `ParseSpan` / `StoreSpan` / `DispatchSpan` usage in the message-processing coroutine.
- **2j cross-doc amendment (v1.x)**: C-ABI subscription surface `fixpp_logger_subscribe_drain_cb` in `c_api/log.h`; 2i amendment for the symbol's reentrancy annotation. Unblocks `StreamLogs` log-stream integration per §7 / Q1.

---

## Appendix A — Catalogue row coverage

| Catalogue ID | Design decision | Delivering §§ |
|---|---|---|
| **LOG-001** | Async logging core: `Logger` with lock-free MPSC ring (256-byte `Record` slots, Vyukov MPSC sequence counter; `read_sequence_` is `std::atomic<uint64_t>` with relaxed producer loads / release drain store; cache-line padded with `alignas(64)`), dedicated drain thread, `FIXPP_SLOG`/`FIXPP_ELOG`/`FIXPP_LOG0` macros, `drop_newest`/`block` overflow, separate `drop_count()` and `timeout_drop_count()` counters. | §4.1, §4.2, §4.3, §6.1, §6.2 |
| **LOG-002** | Sink interface (4 pure-virtual: `open`, `emit`, `flush(deadline)`, `close`) + three default impls: `FileSink` (rotating + deadline-bounded fdatasync), `OtlpLogSink` (BatchLogRecordProcessor-based OTLP), `SyslogSink` (POSIX syslog). | §4.4, §4.5, §4.6, §4.7 |
| **LOG-003** | OTel trace_id/span_id in every `Record`; three-macro pattern: `FIXPP_SLOG(lvl, tc, ...)` (session strand — caller passes explicit `tc = session.get_trace_context()`; see App D §D.1 for the `Session::get_trace_context()` accessor), `FIXPP_ELOG` (engine scope — reads `engine.engine_trace_context()` atomic snapshot), `FIXPP_LOG0` (zero context — destructors/static init); no `thread_local`. | §4.2, §4.3, §6.4, TS-6, TS-7 |
| **LOG-004** | Compile-time level cutoff via `FIXPP_LOG_MIN_LEVEL` + `if constexpr`; runtime per-category filtering via `enabled_categories_mask_` atomic bitmask; separate `filter_count()` and `drop_count()` counters. | §4.1, §4.3, TS-1, TS-8 |
| **OBS-001** | OTel traces: `SessionSpans` RAII helper with session lifecycle span + `ParseSpan` / `StoreSpan` / `DispatchSpan` child spans using explicit parent context (no thread-local Scope); attributes include CompIDs, latency_ns, msg_type, status. | §4.9, TS-12 |
| **OBS-002** | OTel metrics dual export: `OtelDualExportBuilder` registers `PrometheusExporter` (MetricReader, pull, port 9464) and `OtlpMetricExporter` (PushMetricExporter via PeriodicExportingMetricReader) on one `MeterProvider` via `AddMetricReader()`. | §4.10, TS-11 |
| **OBS-003** | OTel logs sink: `OtlpLogSink` is the LOG-002 Sink + OTel BatchLogRecordProcessor combined; same `Sink` interface; no double-write path. Satisfies `[const §XIII.4]`. | §4.6, TS-10 |

---

## Appendix B — Normative References

Per `[const §VI.5]` exact-coverage rule.

### B.1 FIX Protocol documents

None — this doc does not cite FIX wire-format specifications directly. The logging and observability surface is orthogonal to the FIX wire format.

### B.2 Internal documents

| Identifier | Full title + section pointer |
|---|---|
| `[const §V.1]` | fixpp Constitution, Article V §1 — AGPL-3.0 + commercial dual license; C ABI is the linkage isolation boundary |
| `[const §VI.4]` | fixpp Constitution, Article VI §4 — Bidirectional traceability: coverage-index.md maps spec sections → catalogue rows |
| `[const §VI.5]` | fixpp Constitution, Article VI §5 — Exact-coverage citation rule |
| `[const §VIII.5]` | fixpp Constitution, Article VIII §5 — Allocator policy on the hot path: zero `new`/`delete` between parse and `fromApp` callback |
| `[const §XI.3]` | fixpp Constitution, Article XI §3 — Awaitable mutex required in coroutine context; `std::mutex` (and spin-blocking) banned in coroutine contexts |
| `[const §XIII]` | fixpp Constitution, Article XIII — Observability & Logging (all 5 sub-clauses) |
| `[const §XIII.2]` | fixpp Constitution, Article XIII §2 — Async logging mandatory; `drop-oldest` (semantically) permitted on telemetry/log queues |
| `[const §XIII.3]` | fixpp Constitution, Article XIII §3 — `thread_local` prohibited for trace context; use `co_await fixpp::current_trace_context` |
| `[const §XIII.4]` | fixpp Constitution, Article XIII §4 — Same sink interface for OTel log export and file/stderr sinks |
| `[const §XIII.5]` | fixpp Constitution, Article XIII §5 — Bench spike mandatory for quill vs own logger |
| `[const §XIV.2]` | fixpp Constitution, Article XIV §2 — ≤5 pure-virtual methods per pluggable interface |
| `[const §XIV.4]` | fixpp Constitution, Article XIV §4 — Plugin discovery compile-time only in v1.0 |
| `[const §XV.1]` | fixpp Constitution, Article XV §1 — Per-field allocation risk; FIX-message-level OTel tagging explicitly post-v1.0 |
| `[const §XV.5]` | fixpp Constitution, Article XV §5 — Synchronous logging on the hot path is banned |
| `[const §XV.15]` | fixpp Constitution, Article XV §15 — `drop-oldest` never permitted on application/session message path; permitted on telemetry/log paths |
| `[const §XV.17]` | fixpp Constitution, Article XV §17 — Third-party library version pinning rule (Conan, tagged releases) |
| `[arch §2.3]` | fixpp Architecture §2.3 — Module whitelist for cross-module includes; `otel` module depends only on `core` |
| `[arch §4.1]` | fixpp Architecture §4.1 — `core` module surface: primitives, PMR, error, expected, time, trace-context awaitable |
| `[arch §4.7]` | fixpp Architecture §4.7 — `log` module surface inventory |
| `[arch §4.8]` | fixpp Architecture §4.8 — `otel` module surface inventory |
| `[arch §4.10]` | fixpp Architecture §4.10 — `capi` module: `fix/c_api.h` umbrella header |
| `[arch §5.2]` | fixpp Architecture §5.2 — PMR-awareness: owned containers use `std::pmr::polymorphic_allocator`; `mallocnesia` interceptor for zero-alloc validation |
| `[arch §5.3]` | fixpp Architecture §5.3 — Error model: no exceptions on hot path; abort on invariant violation |
| `[arch §5.4]` | fixpp Architecture §5.4 — Trace context (strand-stored; `current_trace_context` awaitable) |
| `[arch §5.7]` | fixpp Architecture §5.7 — Logging cross-cut constraints |
| `[arch §5.8]` | fixpp Architecture §5.8 — Backpressure: `drop-oldest` permitted on log/tap paths |
| `[arch §6]` | fixpp Architecture §6 — Plugin pattern (≤5 pure-virtual, factory, compile-time) |
| `[arch §8]` | fixpp Architecture §8 — Service-mode AGPL boundary: `service/` code may only include `<fixpp/service/...>` and C ABI; no `<fixpp/log/...>` etc. |
| `[arch §9.1]` | fixpp Architecture §9.1 — `[[clang::lifetimebound]]` and `[[nodiscard]]` discipline on view-returning accessors |
| `[arch §10]` | fixpp Architecture §10 — Hand-off to design docs 2a–2m (row 2k) |
| `[arch §11]` row 4 | fixpp Architecture §11 — Open architectural questions: quill vs own async logger |
| `[SYN §3.6 #21]` | SYNTHESIS.md §3.6 question #21 — Observability surface: OTel from day 1; Prometheus + OTLP dual export |
| `[SYN §3.8]` | SYNTHESIS.md §3.8 — Async logging: design constraints, candidate evaluation, quill/spdlog/NanoLog/own comparison; "captured args by value" invariant |
| `[2d §4.4]` | 2d-threading.md §4.4 — `fixpp::core::EngineConfig`: logger/tracer/meter field stubs, engine-level fallback trace_context atomic snapshot |
| `[2d §4.5]` | 2d-threading.md §4.5 — `fixpp::session::SessionConfig` frozen-at-open knobs; engine-anchor + session-override pattern |
| `[2d §4.6]` | 2d-threading.md §4.6 — `fixpp::current_trace_context` awaitable; `session_local<trace_context>` storage |
| `[2d §4.8]` | 2d-threading.md §4.8 — `fixpp::core::session_executor` wrapper; `session_ptr()` member-function accessor |
| `[2d §6.5]` | 2d-threading.md §6.5 — Two-phase session close: `Session::close()` cancellation and drain ordering |
| `[2d §7.9]` | 2d-threading.md §7.9 — Single-effective-clock rule; `effective_clock` for log/OBS timestamps |
| `[2e §6.1.4]` | 2e-msgstore.md §6.1.4 — Durable-before-transmit invariant; async fsync pattern |
| `[2g §4.1]` | 2g-tls.md §4.1 — `cert_source` interface: `load_leaf`, `load_chain`, `sign_callback` |
| `[2h §4.1]` | 2h-transport.md §4.1 — `Transport` plugin interface (≤5 pure-virtual); analogous plugin pattern |
| `[2i §1.1]` | 2i-capi.md §1.1 — `fixpp_error_t` numeric block layout; `[1000, 1099]` reserved for 2k |
| `[2j §4.3]` | 2j-controlplane.md §4.3 — `ControlPlane` method-by-method contract: `start()`, `stop()`, `health()`; control-plane handler execution context per `[2d §7.8]` |
| `[2j §4.7]` | 2j-controlplane.md §4.7 — `StreamLogs` / `StreamMetrics` gRPC RPC schema |
| `[2j §6.4]` | 2j-controlplane.md §6.4 — Control-plane stream backpressure: close-on-overflow |

### B.3 External specifications

| Identifier | Full title + pointer |
|---|---|
| `[OTel-trace]` | OpenTelemetry Specification — Tracing API: https://opentelemetry.io/docs/reference/specification/trace/ |
| `[OTel-metrics]` | OpenTelemetry Specification — Metrics API: https://opentelemetry.io/docs/reference/specification/metrics/ |
| `[OTel-logs]` | OpenTelemetry Specification — Logs Bridge API: https://opentelemetry.io/docs/reference/specification/logs/ |
| `[OTLP]` | OpenTelemetry Protocol (OTLP) Specification: https://opentelemetry.io/docs/reference/specification/protocol/otlp/ |
| `[Prometheus-exposition]` | Prometheus Data Model + Exposition Formats: https://prometheus.io/docs/instrumenting/exposition_formats/ |
| `[quill]` | quill C++ logging library — https://github.com/odygrd/quill; evaluated in §1.2 / TS-13 (compile-time decision; not a v1.0 hard dependency unless spike TS-13 passes Criterion A) |

---

## Appendix C — Convergence Log

### v0.1 → v0.2 (Gate A round 1, Phase A)

**Root causes addressed:**

| RC | Name | Findings collapsed | Fix applied in v0.2 |
|---|---|---|---|
| RC#1 | ArgValue layout + string_view lifetime | Codex P1-1, Codex P2-3 | Replaced `string_view` args with `ArgValue` tagged union (`u64`/`i64`/`f64`/`bool`/`InlineStr<23>`/`StaticStr`); static_assert(sizeof(ArgValue)==24); reduced k_max_args from 8→6 to keep Record at 256 bytes; added `FIXPP_SLIT` macro for static-string args; eliminated "outlive only macro expansion" lifetime claim |
| RC#2 | MPSC queue mechanics + drop semantics | Codex P1-2, Codex P2-2, Codex P3-2 | Renamed `drop_oldest` → `overflow_policy::drop_newest`; documented semantic equivalence to `[const §XIII.2]`; fixed MPSC mechanics (producer-side drop, no data race on non-atomic `read_sequence_`); spike spec updated to 4 producers and 256-byte record; added block-mode caveat re session-strand prohibition (`[const §XI.3]`); added `filter_count()` accessor |
| RC#3 | LOG-003 contradictory acquisition path | Codex P1-3, Opus N-P1-1 | Replaced single `FIXPP_LOG` macro with `FIXPP_SLOG(level, session, fmt, args...)` (session strand, reads `session.current_trace_context()` synchronously — no `co_await` in macro) + `FIXPP_LOG_NOTRACE(level, cat, fmt, args...)` (off-strand, zeroed trace fields); documented three-tier fallback in §6.4; eliminated "coroutine-frame-local variable set by detail machinery" claim |
| RC#4 | Citation discipline + Appendix B completeness | Codex P1-5, Codex P3-1 | Rebuilt Appendix B §B.2 to include all `[const §X.y]`, `[arch §X.y]`, `[SYN §X.y]`, `[2d §X.y]`, `[2i §X.y]`, `[2j §X.y]` cites used in doc body; added missing: `[arch §2.3]`, `[arch §4.1]`, `[arch §4.10]`, `[arch §5.2]`, `[arch §5.3]`, `[arch §8]`, `[arch §9.1]`, `[const §V.1]`, `[const §VI.4]`, `[const §XV.1]`, `[const §XV.17]`, `[2d §6.5]`; normalised `[SYN §3.6 Q21]` → `[SYN §3.6 #21]` throughout |

**Per-finding resolution:**

| Finding | Severity | Resolution |
|---|---|---|
| Codex P1-1 | P1 | Fixed by RC#1: `ArgValue` is now an explicitly-sized 24-byte tagged union; no `string_view`; `InlineStr<23>` and `StaticStr` are the only string paths |
| Codex P1-2 | P1 | Fixed by RC#2: renamed `drop_oldest` → `drop_newest`; corrected MPSC mechanics; updated TS-2/TS-3 |
| Codex P1-3 | P1 | Fixed by RC#3: two-macro pattern `FIXPP_SLOG` + `FIXPP_LOG_NOTRACE` |
| Codex P1-4 | P1 | Fixed: `GrpcStreamSink` recommendation retracted from §7; replaced with explicit v1.0 deferral; `StreamLogs` operates off OTel metrics/traces only until C-ABI subscription surface lands in v1.x |
| Codex P1-5 | P1 | Fixed by RC#4: Appendix B §B.2 rebuilt with all cites |
| Codex P1-6 | P1 | Fixed: §D.4 "Before" block replaced with byte-faithful quote from `coverage-index.md` (see §D.4 below) |
| Codex P2-1 | P2 | Fixed: `open(SinkConfig const&)` removed; each concrete sink takes its config in constructor; `open()` is now a no-arg "start running" signal; `SinkFactory::make(mr)` returns `expected_t<unique_ptr<Sink>>` |
| Codex P2-2 | P2 | Fixed by RC#2: spike spec updated to 4 producers + 256-byte record + p99/p999 primary criterion |
| Codex P2-3 | P2 | Fixed by RC#1: `ArgValue` is explicitly 24 bytes; `Record` sizing recomputed with k_max_args=6 × 24 bytes = 144 bytes + 48 bytes header + 64 bytes padding = 256 bytes; `static_assert` enforced |
| Codex P2-4 | P2→P1 (escalated) | Fixed by N-P1-2 fix: see row below |
| Codex P3-1 | P3 | Fixed by RC#4: `[SYN §3.6 Q21]` → `[SYN §3.6 #21]` globally |
| Codex P3-2 | P3 | Fixed by RC#2: spike scenario now explicitly uses 256-byte `Record` |
| Opus N-P1-1 | P1 | Fixed by RC#3: `FIXPP_LOG` macro replaced; `session_ptr()` impossibility eliminated |
| Opus N-P1-2 | P1 | Fixed: `PrometheusExporter::sdk_exporter()` returning `PushMetricExporter*` replaced with `sdk_reader()` returning `MetricReader*`; `OtlpExporter` renamed `OtlpMetricExporter`; dual-export mechanics rewritten to use `AddMetricReader()` twice on one `MeterProvider` |
| Opus N-P1-3 | P1 | Fixed: `Sink::flush()` signature changed to `flush(std::chrono::milliseconds deadline) noexcept`; `Logger::shutdown(drain_timeout)` replaces `flush(timeout)`; `drain_timeout` field added to `LoggerConfig`; §6.6 shutdown contract updated |
| Opus N-P1-4 | P1 | Fixed: both blocked paths acknowledged in §7; explicit v1.0 deferral documented; contradicts §2 non-goal #7 resolved by removing non-goal #7 conflict (v1.0 defers the C-ABI subscription surface, not the non-goal itself) |
| Opus N-P2-1 | P2 | Fixed: `block` mode caveat added to `overflow_policy::block` doc-comment, §6.1, §4.3 prose; MUST NOT use from session-strand coroutine per `[const §XI.3]` |
| Opus N-P2-2 | P2 | Fixed: `OtlpLogSink::emit` now explicitly hands records to `BatchLogRecordProcessor` (non-blocking); `flush(deadline)` calls `ForceFlush(deadline)`; §4.6 updated |
| Opus N-P2-3 | P2 | Fixed: `SessionSpans` sub-spans now use explicit parent context `StartSpan(name, {}, {.parent = session_ctx_})`; `Scope` not used; TS-12 updated to assert correct parent span ID across OS thread boundaries |
| Opus N-P2-4 | P2 | **Disagree (see note below)** |
| Opus N-P2-5 | P2 | Fixed: `async_flush()` wake-up mechanism specified — flush sentinel record enqueued into MPSC ring; drain thread posts `asio::post(caller_executor, handler)` on sentinel processing; one heap allocation per `async_flush()` call documented in §8 PMR |
| Opus N-P2-6 | P2 | Fixed: `log_queue_overflow` (1000) clarified as internal C++ variant only; slot reserved for future v1.x `fixpp_logger_drop_count()` C-ABI call; not a live C-ABI error return in v1.0 |
| Opus N-P2-7 | P2 | **Disagree (see note below)** |
| Opus N-P3-1 | P3 | Fixed: `Logger` constructor takes `std::pmr::vector<std::unique_ptr<Sink>>`; sink vector uses `ring_resource` PMR; §8 PMR recap updated |
| Opus N-P3-2 | P3 | Fixed: file naming scheme clarified: live file = `<base_name>.log`; rotated file = `<base_name>.<close_timestamp>.log` |
| Opus N-P3-3 | P3 | Fixed: added doc-comment to `SyslogSink` that `emit()`/`flush()`/`close()` are only called after successful `open()`; `Logger` enforces this invariant |
| Opus N-P3-4 | P3 | Fixed: added `Logger::filter_count() const noexcept` accessor; TS-8 updated to assert `filter_count() == 1` not `drop_count() == 0` for category-filtered records |
| Opus N-P3-5 | P3 | No change needed: §D.2 correctly marks `[arch §11]` row 4 as "PROVISIONAL: own impl" (not DONE); the provisional label is appropriate given TS-13 has not yet executed |
| Opus N-P3-6 | P3 | Fixed: `drain_timeout` added as a field to `LoggerConfig` (default 5 seconds); `Logger::shutdown(drain_timeout)` uses this field; §6.6 updated to reference `LoggerConfig::drain_timeout` |

**Disagreements (Opus findings not applied):**

| Finding | Reason not applied |
|---|---|
| Opus N-P2-4 (Category bitmask limited to 64 categories) | The 64-category bitmask is a documented intentional constraint, not a bug. The 8 built-in categories use bits 0–7; user-defined categories via `FIXPP_LOG_CATEGORY` must manually assign bit positions (not use CRC32 directly for bit mapping). The doc already states the bitmask covers bits 0–63 only. Adding `FIXPP_LOG_CATEGORY_BIT(n)` is a v1.x ergonomics improvement; the v1.0 API is sufficient. No fix applied; a note is added to the `enabled_categories_mask_` description clarifying that the bitmask supports ≤64 categories and user-defined category values must not collide in the low-6 bits. |
| Opus N-P2-7 (OtelConfig shared endpoint) | Splitting `OtelConfig` into per-signal endpoint configs is an ergonomics improvement for multi-collector deployments. For v1.0 the single shared `endpoint` field is sufficient (the OTel collector can forward to per-signal destinations via pipeline config). Per-signal endpoint override is deferred to v1.x. `OtlpLogSink` already has its own `endpoint` field (§4.6), providing the primary differentiation needed for log vs trace/metric routing. |

**Net-effect summary:** v0.2 locks the `ArgValue` 24-byte POD layout (no `string_view`; `InlineStr<23>` + `StaticStr` with `FIXPP_SLIT` macro), renames `drop_oldest` to `drop_newest` (semantically equivalent under `[const §XIII.2]` and mechanically correct for MPSC without data races), resolves the LOG-003 macro acquisition contradiction with a `FIXPP_SLOG` / `FIXPP_LOG_NOTRACE` two-macro pattern, fixes the OTel SDK API types (`PrometheusExporter` as `MetricReader`, dual export via `AddMetricReader()` twice), adds shutdown deadline to `Sink::flush(deadline)` and `Logger::shutdown(drain_timeout)`, resolves the AGPL boundary for `StreamLogs` by deferring the C-ABI subscription surface to the 2j cross-doc amendment cycle, and completes Appendix B citation discipline with 12 previously-missing cites added.

---

### v0.2 → v0.3 (Gate A round 2, Phase A)

**Root causes addressed:**

| RC | Name | Findings collapsed | Fix |
|---|---|---|---|
| RC#5 | ArgValue/InlineStr sizing | Codex P1-1, Opus N-P2-1 (sizing), Opus RC#5 | InlineStr capped at 15 bytes (`char data[15]; uint8_t len;` = 16 bytes); no null-terminator guarantee (drain thread uses `len`); `static_assert(sizeof(ArgValue) == 24, ...)`; `from_inline` truncates at `n = min(sv.size(), 15)` — always in-bounds; `[2k §4.2]` updated |
| RC#6 | Three-tier LOG-003 fallback Tier 2 unreachable | Codex P1-3, Codex P2-1, Opus RC#6 | Added `FIXPP_ELOG(level, engine_ref, cat, fmt_id, args...)` for control-plane paths; renamed `FIXPP_LOG_NOTRACE` → `FIXPP_LOG0` ("zero-context log"); `EngineConfig::engine_trace_context` field added (Appendix D §D.1 extension); §6.4 updated to document all three tiers with their macro call sites; `[2j §4.3]` added to §7, §10 Q1, and Appendix B §B.2 |

**Per-finding resolution:**

| Finding | Severity | Resolution |
|---|---|---|
| Codex P1-1 | P1 | Fixed by RC#5: `InlineStr::data` reduced from `char[23]` to `char[15]`; `from_inline` truncates at 15; no OOB write; `static_assert(sizeof(ArgValue) == 24, ...)` now passes |
| Codex P1-2 | P1 | Fixed: `read_sequence_` changed to `std::atomic<uint64_t>`; producers load with `memory_order_relaxed`; drain thread stores with `memory_order_release`; both `write_sequence_` and `read_sequence_` annotated with `alignas(64)` to prevent false sharing; §4.3 MPSC ring mechanics paragraph updated |
| Codex P1-3 | P1 | Fixed by RC#6: `FIXPP_ELOG` macro added for Tier 2; `FIXPP_LOG_NOTRACE` renamed to `FIXPP_LOG0` (Tier 3 only); §6.4 updated with call-site guidance |
| Codex P1-4 | P1 | Fixed: `OtlpLogSink::open(SinkConfig const& config) override` → `open() override`; `OtlpLogSink` now fully implements the `Sink` pure-virtual interface without the stale parameterized override |
| Codex P2-1 | P2 | Absorbed by RC#6: same root cause as Codex P1-3 |
| Codex P2-2 | P2 | Fixed: `timeout_drop_count()` accessor added to `Logger`; `reset_timeout_drop_count()` added; `Logger::shutdown()` changed to return `[[nodiscard]] fixpp::core::expected_t<void>`; §6.3 error table updated (log_drain_timeout returns from shutdown(), increments timeout_drop_count()); §6.6 updated |
| Codex P3-1 | P3 | Fixed: `[2j §4.3]` cite added to §7 (control-plane handler guidance) and §10 Q1 (StreamLogs discussion); `[2j §4.3]` row added to Appendix B §B.2 |
| Opus N-P1-1 / Codex N-P1-1 | P1 | Fixed: `FileSinkConfig` comment updated to match §4.5 rotation prose: live file = `<base_name>.log`; rotated = `<base_name>.<close_timestamp_iso8601>.log` |
| Opus N-P1-2 | P1 | Fixed: OBS-002 catalogue row in §D.3 updated to use `AddMetricReader()` language (two readers on one `MeterProvider`); `MultiMetricExporter` language removed; OBS-002 supplemental in §D.4 updated to match; §4.10 prose already correct (no change needed there) |
| Opus N-P2-1 | P2 | Absorbed by RC#5 |
| Opus N-P2-2 | P2 | Fixed: `SinkFactory` redesigned with `virtual std::unique_ptr<Sink> make(std::pmr::memory_resource*, SinkConfig const&) = 0`; `FileSinkFactory` and `OtlpLogSinkFactory` concrete factories defined in §4.4; §4.3 Logger API note clarified that Logger does not call `SinkFactory::make()` — callers construct sinks and pass them to the Logger constructor |
| Opus N-P3-1 | P3 | Absorbed by N-P2-2 fix: §4.3 Logger API note now clarifies the factory–Logger relationship explicitly |
| Opus N-P3-2 | P3 | No change: default parameter removed from `shutdown()` (single source of truth is `LoggerConfig::drain_timeout`). The `shutdown()` signature now requires an explicit `drain_timeout` argument, eliminating the mismatch between the method default and the config field. |
| Opus N-P3-3 | P3 | No change needed: `reset_drop_count()` TOCTOU note is a known pattern and acceptable as-is for v1.0. `timeout_drop_count()` reset pair added alongside the existing pattern; the same TOCTOU caveat applies. |

**Disagreements (Opus findings not applied in v0.3):**

| Finding | Reason not applied |
|---|---|
| No new disagreements in v0.3 | All P1/P2 findings applied. |

**Net-effect summary:** v0.3 eliminates the ArgValue OOB/sizing defect (RC#5: `InlineStr::data` reduced from 23 to 15 bytes; `static_assert(sizeof(ArgValue) == 24, ...)` now passes; `from_inline` truncates at 15 bytes without null-terminator guarantee), completes the three-macro LOG-003 API (RC#6: `FIXPP_SLOG` / `FIXPP_ELOG` / `FIXPP_LOG0`; `EngineConfig::engine_trace_context` field; `[2j §4.3]` cited in §7/§10/AppB), fixes the `read_sequence_` data race (`std::atomic<uint64_t>` with relaxed producer loads / release drain store; cache-line padded), eliminates the `OtlpLogSink::open(SinkConfig const&)` self-contradiction (no-arg `open()` throughout all concrete sinks), corrects the `FileSinkConfig` rotation comment to match §4.5 prose, removes stale `MultiMetricExporter` language from §D.3 OBS-002, separates `drop_count()` from `timeout_drop_count()`, makes `Logger::shutdown()` return `[[nodiscard]] expected_t<void>` (no default argument), and fills the `SinkFactory` documentation gap with `FileSinkFactory` and `OtlpLogSinkFactory` concrete definitions.

---

### v0.3 → v0.4 (Gate A round 3, Phase A)

**Root causes addressed:** None new — all remaining items are targeted fixes from rounds 1–3.

**Per-finding resolution:**

| Finding | Resolution |
|---|---|
| Opus N-P1-1: `FIXPP_SLOG` calls non-existent `Session::current_trace_context()` | Fixed in §4.3: `FIXPP_SLOG` now takes explicit `trace_context const& tc` arg; App D §D.1 adds `Session::get_trace_context() const noexcept` accessor |
| Codex P2-1 / Opus confirm: MPSC overflow deadlock | Fixed in §4.3: load-check-CAS-claim pattern prevents slot claim before overflow detection |
| Opus N-P2-1: `static_assert(sizeof(ArgValue) <= 24)` | Fixed: changed to `== 24` |
| Codex P3-1: stale `emit(record, deadline)` | Fixed: `emit(Record const& record)` |
| Codex P3-3: §D.1 line range label | Fixed: corrected to actual line range from live 2d-threading.md (lines 593–603) |

**Disagreements recorded:** None from round 3.

**Net-effect summary:** v0.4 completes the LOG-003 macro contract by making trace-context passing explicit (session.get_trace_context() + App D §D.1 amendment), fixes the MPSC overflow deadlock via load-check-CAS ordering, and sweeps three P3 editorial items. Phase A concluded at this version.

---

### v0.4 → v0.5 (Gate A round 4 converged, Phase A post-cap pass)

**Root causes addressed:** None new — P3 line-edits only.

**Per-finding resolution:**

| Finding | Severity | Resolution |
|---|---|---|
| P3-1: §4.3 step-2 memory ordering contradicts its own rationale | P3 | Step-2 load changed from `read_sequence_.load(acquire)` to `read_sequence_.load(relaxed)`; rationale paragraph ("A relaxed load … is safe") was already correct — step was the outlier |
| P3-2: Appendix C RC#5 Fix cell and net-effect summary still claim `<= 24` after v0.4 tightened to `== 24` | P3 | RC#5 Fix cell, Codex P1-1 per-finding row, and v0.2→v0.3 net-effect summary all updated to `static_assert(sizeof(ArgValue) == 24, ...)` to match §4.2 and the v0.3→v0.4 per-finding closure entry |
| P3-3: §D.2 heading says "flip disposition to DONE" but After block sets PROVISIONAL | P3 | §D.2 heading renamed to "flip disposition to PROVISIONAL (own impl)" to match the amendment's actual content |
| N-P3-1: `Session::get_trace_context()` returns `const&` but lacks `[[clang::lifetimebound]]` | P3 | App D §D.1 "After" block declaration updated to `[[nodiscard, clang::lifetimebound]]` per §4 preamble convention ("every view-returning accessor carries `[[clang::lifetimebound]]`") |

**Disagreements recorded:** None.

**Net-effect summary:** P3 line-edits only; design unchanged.

---

## Appendix D — Cross-doc amendments

Following the `[2j App D]` byte-faithful Before/After pattern. Each entry is applied by the orchestrator at 2k sign-off. Per the `[2j App D §D.1]` rewriter rule: the orchestrator MUST re-verify each Before block against `git show HEAD:<path>` at sign-off; if any drift, re-quote byte-faithfully before applying.

### §D.1 Confirm `EngineConfig` observability field types in `[2d §4.4]`

Per `[2d §4.4]`, `EngineConfig` already has three observability stubs. 2k confirms their types are correct as declared. No structural change to `2d-threading.md` is needed — the field names and types in the existing stub match 2k's defined API.

**No Before/After needed.** The existing `[2d §4.4]` lines (at `2d-threading.md` lines 441–443) already read:

```cpp
std::shared_ptr<fixpp::core::Logger>           logger;        // null → no-op.
std::shared_ptr<fixpp::otel::TracerProvider>   tracer;        // null → no-op trace context.
std::shared_ptr<fixpp::otel::MeterProvider>    meter;         // null → no-op metrics.
```

The type `fixpp::core::Logger` is the forward-declared name in the `core` module per `[arch §4.1]`; 2k's implementation lives in `fixpp::log::Logger` in the `log` module, with a type alias `fixpp::core::Logger = fixpp::log::Logger` (or a forward declaration that maps to the same type). The orchestrator verifies at sign-off that this alias is present in `include/fixpp/core/logger_fwd.hpp`.

Additionally, 2k adds the `logger_override` and `tracer_override` fields to `SessionConfig`. These are **new fields** requiring an amendment to `[2d §4.5]`.

**Before** (lines 593–603 of `library/.specify/2d-threading.md` — verbatim, byte-faithful; the observability-hooks comment block and the two fields it introduces):

```
    // ── Observability hooks (interface-level only; locked by 2k) ────────
    // Per C-P2-4 / N-P2-2: the trace_context_provider field is replaced
    // with a value-typed `initial_trace_context` (no heap-capable callable
    // in frozen config). Tests/users that need dynamic generation may set
    // `initial_trace_context` to the freshly-generated value at config
    // construction time — the field is read once at session open by
    // construction. The §6.7 `trace_context_provider_threw` error variant
    // is dropped (no callable to throw); replaced by validation on the
    // value-typed field at Session::open.
    fixpp::otel::trace_context  initial_trace_context {};
    std::shared_ptr<fixpp::log::Sink> log_sink_override;       // null → engine default.
```

**After** (the `initial_trace_context` field preserved verbatim; `log_sink_override` replaced with the full logger and tracer overrides per 2k's `§4.8` recommendation; comment updated):

```
    // ── Observability hooks (locked by 2k) ───────────────────────────────
    // initial_trace_context: value-typed trace_context set at session open.
    // Per [2d §4.5] engine-anchor + session-override pattern:
    //   effective_logger  = logger_override  ?: EngineConfig::logger
    //   effective_tracer  = tracer_override  ?: EngineConfig::tracer
    // meter_override is intentionally omitted: metrics are engine-scoped.
    fixpp::otel::trace_context                 initial_trace_context {};
    std::shared_ptr<fixpp::log::Logger>        logger_override;   // null → engine default.
    std::shared_ptr<fixpp::otel::TracerProvider> tracer_override; // null → engine default.
```

**Why.** `log_sink_override` was a coarser v0.1 stub (replacing the entire Sink, not the Logger). 2k replaces it with `logger_override` (a full `Logger` override, consistent with how `[2d §4.5]` handles `store_factory` and `cert_source` overrides — override the whole plugin, not just a sub-component). `meter_override` is omitted because all sessions in an engine share metric namespace (per §4.8 note).

**§D.1 amendment (v0.4) — Add `Session::get_trace_context() const noexcept` to `Session`'s public surface.**

**Before** (lines 789–835 of `library/.specify/2d-threading.md` — verbatim, byte-faithful; the `Session` class definition):

```
class Session {
public:
    // ... (full surface owned by session-module Phase-4 spec) ...

    // Two-phase close.
    //
    //   Phase 1 (graceful only) — opens a CHILD asio::cancellation_state
    //   composed below the session's root cancellation_state. Attempts a
    //   FIX `Logout` exchange (toAdmin(Logout) → async_write → wait for
    //   peer's Logout ACK or fail-fast on disconnect). The wait timer is a
    //   `Clock::sleep_until(...)` awaitable bound to the CHILD slot, with
    //   the deadline = effective_clock.steady_now() + close_timeout (a
    //   value picked at the session-module Phase-4 spec; 2d does NOT pick
    //   it per C-P2-8 + N-P2-1). Because the child state is independent of
    //   the root, the Logout async_write and its timeout sleep are NOT
    //   pre-cancelled by the eventual root total cancellation. When
    //   phase 1 resolves (peer ACK observed | child timeout fires | child
    //   state itself cancelled by an outer policy), phase 2 begins.
    //
    //   Phase 2 (always) — fires asio::cancellation_type::total on the
    //   session's ROOT cancellation_state, propagating to:
    //     - in-flight transport async_read     → operation_aborted.
    //     - in-flight transport async_write    → operation_aborted (any
    //       partially-written FIX bytes are unrecoverable; callers'
    //       toApp(...) had already returned).
    //     - heartbeat Clock::sleep_until      → operation_aborted.
    //     - async_mutex::lock (2f)            → operation_aborted (per
    //       [SYN §3.2 Q6b] item 3).
    //     - the application-callback dispatch (the strand's posted handler
    //       is reaped before invocation by the cancellable_dispatch
    //       primitive — §6.5 — which checks the cancellation_state at the
    //       hand-off boundary; if cancellation lands during invocation, the
    //       next co_await checkpoint observes the slot).
    //     - parser → fromApp chain            → cancelled at next
    //       checkpoint (see §6.5 deterministic three-case answer).
    //
    // Returns asio::awaitable<expected_t<void>> that completes when both
    // phases have drained: transport closed; PMR per-message arenas reset;
    // session_local<trace_context> slot cleared; per-session reusable timer
    // slot pool returned to the session arena.
    //
    // Idempotent — calling close() on an already-closing session returns
    // immediately with no error; calling close() on an already-closed
    // session returns session_already_closed (§6.7).
    [[nodiscard]] asio::awaitable<expected_t<void>>
        close(close_mode mode = close_mode::graceful) noexcept;
};
```

**After** (the `close()` declaration preserved verbatim; new `get_trace_context()` accessor inserted before the closing `}`):

```
class Session {
public:
    // ... (full surface owned by session-module Phase-4 spec) ...

    // Two-phase close.
    //
    //   Phase 1 (graceful only) — opens a CHILD asio::cancellation_state
    //   composed below the session's root cancellation_state. Attempts a
    //   FIX `Logout` exchange (toAdmin(Logout) → async_write → wait for
    //   peer's Logout ACK or fail-fast on disconnect). The wait timer is a
    //   `Clock::sleep_until(...)` awaitable bound to the CHILD slot, with
    //   the deadline = effective_clock.steady_now() + close_timeout (a
    //   value picked at the session-module Phase-4 spec; 2d does NOT pick
    //   it per C-P2-8 + N-P2-1). Because the child state is independent of
    //   the root, the Logout async_write and its timeout sleep are NOT
    //   pre-cancelled by the eventual root total cancellation. When
    //   phase 1 resolves (peer ACK observed | child timeout fires | child
    //   state itself cancelled by an outer policy), phase 2 begins.
    //
    //   Phase 2 (always) — fires asio::cancellation_type::total on the
    //   session's ROOT cancellation_state, propagating to:
    //     - in-flight transport async_read     → operation_aborted.
    //     - in-flight transport async_write    → operation_aborted (any
    //       partially-written FIX bytes are unrecoverable; callers'
    //       toApp(...) had already returned).
    //     - heartbeat Clock::sleep_until      → operation_aborted.
    //     - async_mutex::lock (2f)            → operation_aborted (per
    //       [SYN §3.2 Q6b] item 3).
    //     - the application-callback dispatch (the strand's posted handler
    //       is reaped before invocation by the cancellable_dispatch
    //       primitive — §6.5 — which checks the cancellation_state at the
    //       hand-off boundary; if cancellation lands during invocation, the
    //       next co_await checkpoint observes the slot).
    //     - parser → fromApp chain            → cancelled at next
    //       checkpoint (see §6.5 deterministic three-case answer).
    //
    // Returns asio::awaitable<expected_t<void>> that completes when both
    // phases have drained: transport closed; PMR per-message arenas reset;
    // session_local<trace_context> slot cleared; per-session reusable timer
    // slot pool returned to the session arena.
    //
    // Idempotent — calling close() on an already-closing session returns
    // immediately with no error; calling close() on an already-closed
    // session returns session_already_closed (§6.7).
    [[nodiscard]] asio::awaitable<expected_t<void>>
        close(close_mode mode = close_mode::graceful) noexcept;

    // OTel trace context accessor (added at 2k sign-off per [2k App D §D.1]).
    // Returns the session's current trace_context — a synchronous read-only
    // accessor to the session_local<trace_context> slot stored at session
    // open from SessionConfig::initial_trace_context. Called on the session
    // strand by FIXPP_SLOG callers: `auto const& tc = session.get_trace_context()`.
    // noexcept; never returns a null reference; the slot is populated at
    // session open and cleared at session close (after which this accessor
    // returns a default-constructed trace_context). MUST be called from
    // inside the session's serialisation domain (the session strand or the
    // user-attested direct executor) — the same precondition as
    // session_local<T>::load() per [2d §4.6].
    [[nodiscard, clang::lifetimebound]] fixpp::otel::trace_context const&
        get_trace_context() const noexcept;
};
```

**Why.** The `FIXPP_SLOG` macro (§4.3) requires a synchronous way to obtain the session's `trace_context` without `co_await`. The `fixpp::current_trace_context` singleton in `[2d §4.6]` is a global awaitable — not a `Session` member function. `get_trace_context()` bridges this gap: it is a thin `const noexcept` wrapper around `trace_slot_.load()` (the private `session_local<trace_context>` member). The method is scoped to the session serialisation domain (same precondition as the existing `session_local<T>::load()` callers in 2d).

### §D.2 `[arch §11]` row 4 — flip disposition to PROVISIONAL (own impl)

Per `[arch §11]`, row 4 currently reads:

**Before** (lines 597–598 of `library/.specify/architecture.md` — verbatim, byte-faithful):

```
| 4 | `quill` vs own async logger — adopt or build | **2k** | Bench-driven `[SYN §3.8]` |
```

**After** (disposition column updated; ownership column retains 2k):

```
| 4 | `quill` vs own async logger — adopt or build | **2k** | **PROVISIONAL: own impl** (benchmark spike TS-13 mandated at Phase 3; flip to DONE at 2k v0.2 sign-off after spike result; see `[2k §1.2]` and `[2k Appendix C]`) |
```

**Why.** The design doc signs off with a provisional recommendation (own impl, per §1.2 rationale). The row is not flipped to DONE until the TS-13 spike executes and the result is recorded in Appendix C. This preserves the `[arch §11]` tracking discipline: the row is live until fully resolved.

### §D.3 NEW catalogue rows LOG-001..004 + OBS-001..003 in `library/spec/feature-catalogue.md`

**Before** (lines 209–210 of `library/spec/feature-catalogue.md` — verbatim, byte-faithful; the last SVC row and the blank line before the NFR section):

```
| SVC-005 | OFFICIAL | service | Pluggable control plane interface — `fixpp::service::ControlPlane` (3 pure-virtual: `start`, `stop`, `health`; ≤5 cap with 2 slots of headroom for v1.x auth-token rotation + RPC re-mapping per [2j §10] Q5); default impl gRPC over Unix socket (Linux) / named pipe (Windows); alternative impls (JSON-over-Unix-socket sample, ...) link without rebuilding the engine via the AGPL-boundary structural rule per `[const §V.1]` / `[arch §8]`; `EngineConfig::control_plane_factory` engine-anchor per `[2j Appendix D §D.2]`; handlers run on the engine executor per `[2d §7.8]`; `CloseSession` RPC consumes `[2h §7.6]` graceful-drain shape; rotation RPCs (`RotatePinset` / `ReloadCertSource`) deferred to v1.x per `[2j §10]` Q1 + Q9 | all | [2j §4.1] / [arch §4.11] | backlog | `.specify/2j-controlplane.md` v0.3 | — | — | — |

## NFRs (Non-Functional Requirements)
```

**After** (SVC-005 row preserved verbatim; NEW Logging/Observability section inserted between SVC-005 and NFRs):

```
| SVC-005 | OFFICIAL | service | Pluggable control plane interface — `fixpp::service::ControlPlane` (3 pure-virtual: `start`, `stop`, `health`; ≤5 cap with 2 slots of headroom for v1.x auth-token rotation + RPC re-mapping per [2j §10] Q5); default impl gRPC over Unix socket (Linux) / named pipe (Windows); alternative impls (JSON-over-Unix-socket sample, ...) link without rebuilding the engine via the AGPL-boundary structural rule per `[const §V.1]` / `[arch §8]`; `EngineConfig::control_plane_factory` engine-anchor per `[2j Appendix D §D.2]`; handlers run on the engine executor per `[2d §7.8]`; `CloseSession` RPC consumes `[2h §7.6]` graceful-drain shape; rotation RPCs (`RotatePinset` / `ReloadCertSource`) deferred to v1.x per `[2j §10]` Q1 + Q9 | all | [2j §4.1] / [arch §4.11] | backlog | `.specify/2j-controlplane.md` v0.3 | — | — | — |

## Logging & Observability

| ID | Source | Category | Title | FIX version(s) | Spec ref | Status | /specify | PR | Tests | Verified |
|---|---|---|---|---|---|---|---|---|---|---|
| LOG-001 | OFFICIAL | service | Async logging core — `fixpp::log::Logger` with lock-free MPSC ring (256-byte `Record` slots, Vyukov MPSC sequence counter; `read_sequence_` `std::atomic<uint64_t>` cache-line padded), dedicated drain thread, `FIXPP_SLOG`/`FIXPP_ELOG`/`FIXPP_LOG0` macros, `drop_newest`/`block` overflow per `[const §XIII.2]` / `[const §XV.15]`; zero alloc on producer path per `[const §VIII.5]`; `FIXPP_LOG_MIN_LEVEL` CMake option | all | [2k §4.3] / [arch §4.7] | backlog | `.specify/2k-log-otel.md` v0.4 | — | — | — |
| LOG-002 | OFFICIAL | service | Logger sink interface — `fixpp::log::Sink` (4 pure-virtual: `open`, `emit`, `flush`, `close`; ≤5 cap per `[const §XIV.2]`); default impls: `FileSink` (rotating + async fsync), `OtlpLogSink` (OTLP log export per OBS-003), `SyslogSink` (POSIX syslog) | all | [2k §4.4] / [2k §4.5] / [2k §4.6] / [2k §4.7] | backlog | `.specify/2k-log-otel.md` v0.4 | — | — | — |
| LOG-003 | OFFICIAL | service | OTel trace_id/span_id in every log record — three-macro pattern: `FIXPP_SLOG` (session strand), `FIXPP_ELOG` (engine scope, `engine_trace_context()` atomic snapshot), `FIXPP_LOG0` (zero context — destructors/static init); `thread_local` prohibited per `[const §XIII.3]` | all | [2k §4.2] / [2k §4.3] / [2k §6.4] | backlog | `.specify/2k-log-otel.md` v0.4 | — | — | — |
| LOG-004 | OFFICIAL | service | Compile-time level cutoff + runtime per-category filtering — `FIXPP_LOG_MIN_LEVEL` constant gates `if constexpr`; `enabled_categories_mask_` atomic bitmask per `Logger` enables runtime per-category enable/disable | all | [2k §4.1] / [2k §4.3] | backlog | `.specify/2k-log-otel.md` v0.4 | — | — | — |
| OBS-001 | OFFICIAL | service | OTel traces — session lifecycle, parse latency, store latency, dispatch latency traced via `fixpp::otel::SessionSpans` RAII helper; parent-child span relationships; attributes: CompIDs, latency_ns, msg_type, span status | all | [2k §4.9] | backlog | `.specify/2k-log-otel.md` v0.4 | — | — | — |
| OBS-002 | OFFICIAL | service | OTel metrics with dual export — `OtelDualExportBuilder` registers `PrometheusExporter` (MetricReader, pull, embedded HTTP /metrics port 9464) + `OtlpMetricExporter` (PushMetricExporter via PeriodicExportingMetricReader) on one `MeterProvider` via `AddMetricReader()` per `[SYN §3.6 #21]` | all | [2k §4.10] | backlog | `.specify/2k-log-otel.md` v0.4 | — | — | — |
| OBS-003 | OFFICIAL | service | OTel logs sink — `OtlpLogSink` is the LOG-002 sink + OTLP exporter combined; translates `fixpp::log::Record` → `opentelemetry::logs::LogRecord`; same sink interface backs OTel log export per `[const §XIII.4]` | all | [2k §4.6] | backlog | `.specify/2k-log-otel.md` v0.4 | — | — | — |

## NFRs (Non-Functional Requirements)
```

**Why.** Per `[const §VI.4]` bidirectional traceability: LOG/OBS rows declared in `[SYN §3.8]` / `[SYN §3.6 #21]` are now spec-driven OFFICIAL rows with Spec ref columns pointing to 2k sections. Status remains `backlog` pending Phase 4 implementation.

### §D.4 NEW entries in `library/spec/coverage-index.md` — LOG/OBS supplemental notes

Following the `[2j App D §D.4]` pattern: design-doc-rooted catalogue rows that pin non-trivial structural invariants get supplemental notes in the coverage index.

**Before** (exact bytes from `library/spec/coverage-index.md` after the CA-002 supplemental paragraph — the SVC-005 supplemental paragraph and the trailing separator):

```
**SVC-005 supplemental:** Pluggable control-plane interface — `fixpp::service::ControlPlane` (3 pure-virtual methods: `start`, `stop`, `health`; under the `[const §XIV.2]` ≤ 5 cap with 2 slots reserved for v1.x `RotateAuthToken` / `RemapRpcs` per `[2j §10]` Q5). Source spec sections: `[arch §4.11] service` (the surface inventory) and `[2j §4.1] fixpp::service::ControlPlane — abstract interface`. Default impl `fixpp::service::grpc_control_plane` per `[2j §4.6]` (Unix domain socket on Linux / named pipe on Windows; TCP opt-in per `[arch §8.1]`). The proto schema `service/proto/fixpp_control.proto` per `[2j §4.7]` is on the `[arch §9.3]` "Stable from v1.0" tier; proto-evolution rules pinned in `[2j §4.7.1]` (additive-only expansion via MINOR bumps; removals are MAJOR breaks). v1.0 RPC surface: `OpenSession`, `CloseSession`, `Configure` (reserved-empty per `[2j §4.7.1]` additive expansion path), `StreamMetrics`, `StreamLogs`, `StreamSessionEvents`, `Health` (gRPC standard health-check). `RotatePinset` and `ReloadCertSource` are deferred to v1.x per `[2j §10]` Q1 + Q9 (the v1.0 cross-doc state has no AGPL-legal path: `[2i §2]` non-goal #6 declines the C-ABI rotation surface; `service/grpc/*.cpp` cannot include `<fixpp/tls/...>` per `[arch §8]`). AGPL-boundary structural enforcement per `[2j §4.4]` / `[2j §4.6]` + `tools/check_layers.py` lint per `[arch §8]` enforcement bullet (first-landing tracked at `[2j §10]` Q10). Stream backpressure: close-on-overflow with `control_plane_stream_overflow` per `[2j §4.8]` / `[2j §6.4]` (consistent with `[const §XV.15]` no-drop-oldest-on-app-paths; `[const §XIII.2]` permits but does not require drop-oldest on observability paths — v1.0 picks close-on-overflow for visibility). The proto-stability audit-trail file `tools/abi_history/proto_v1.txt` (NEW at 2j sign-off per `[2j App D §D.3]`) mirrors the `tools/abi_history/error_codes_v1.txt` precedent. Source: 2j v0.3 (2026-05-09); see `[2j §11]` drop-in language and `[2j Appendix A]`.

---
```

**After** (SVC-005 supplemental preserved verbatim; NEW LOG/OBS supplemental block appended before the `---` separator):

```
**SVC-005 supplemental:** Pluggable control-plane interface — `fixpp::service::ControlPlane` (3 pure-virtual methods: `start`, `stop`, `health`; under the `[const §XIV.2]` ≤ 5 cap with 2 slots reserved for v1.x `RotateAuthToken` / `RemapRpcs` per `[2j §10]` Q5). Source spec sections: `[arch §4.11] service` (the surface inventory) and `[2j §4.1] fixpp::service::ControlPlane — abstract interface`. Default impl `fixpp::service::grpc_control_plane` per `[2j §4.6]` (Unix domain socket on Linux / named pipe on Windows; TCP opt-in per `[arch §8.1]`). The proto schema `service/proto/fixpp_control.proto` per `[2j §4.7]` is on the `[arch §9.3]` "Stable from v1.0" tier; proto-evolution rules pinned in `[2j §4.7.1]` (additive-only expansion via MINOR bumps; removals are MAJOR breaks). v1.0 RPC surface: `OpenSession`, `CloseSession`, `Configure` (reserved-empty per `[2j §4.7.1]` additive expansion path), `StreamMetrics`, `StreamLogs`, `StreamSessionEvents`, `Health` (gRPC standard health-check). `RotatePinset` and `ReloadCertSource` are deferred to v1.x per `[2j §10]` Q1 + Q9 (the v1.0 cross-doc state has no AGPL-legal path: `[2i §2]` non-goal #6 declines the C-ABI rotation surface; `service/grpc/*.cpp` cannot include `<fixpp/tls/...>` per `[arch §8]`). AGPL-boundary structural enforcement per `[2j §4.4]` / `[2j §4.6]` + `tools/check_layers.py` lint per `[arch §8]` enforcement bullet (first-landing tracked at `[2j §10]` Q10). Stream backpressure: close-on-overflow with `control_plane_stream_overflow` per `[2j §4.8]` / `[2j §6.4]` (consistent with `[const §XV.15]` no-drop-oldest-on-app-paths; `[const §XIII.2]` permits but does not require drop-oldest on observability paths — v1.0 picks close-on-overflow for visibility). The proto-stability audit-trail file `tools/abi_history/proto_v1.txt` (NEW at 2j sign-off per `[2j App D §D.3]`) mirrors the `tools/abi_history/error_codes_v1.txt` precedent. Source: 2j v0.3 (2026-05-09); see `[2j §11]` drop-in language and `[2j Appendix A]`.

**LOG-001 supplemental:** Async logging core — `fixpp::log::Logger` with lock-free MPSC ring, Vyukov MPSC sequence counter (own impl per `[2k §1.2]` provisional recommendation; quill adoption contingent on TS-13 spike Criterion A pass). Zero alloc on producer path: one atomic increment + 256-byte `memcpy`. Drain thread: dedicated OS thread (not ASIO strand); formats records, dispatches to sinks. `read_sequence_` is `std::atomic<uint64_t>` (producers load with `memory_order_relaxed`; drain thread stores with `memory_order_release`); `write_sequence_` and `read_sequence_` are each `alignas(64)` to prevent false sharing. Overflow: `drop_newest` (default; producer-side drop; satisfies `[const §XIII.2]` "drop-oldest permitted" semantically) or `block` (non-session-strand only). Counters: `drop_count()` (overflow drops), `timeout_drop_count()` (records abandoned on drain timeout), `filter_count()` (category-filtered records) — all separate `std::atomic<uint64_t>` fields. `Logger::shutdown()` returns `[[nodiscard]] expected_t<void>`; timeout yields `unexpected(log_drain_timeout)`. Source: `[2k §4.3]` / `[arch §4.7]`.

**LOG-002 supplemental:** Sink interface — 4 pure-virtual (`open`, `emit`, `flush`, `close`; under `[const §XIV.2]` ≤5 cap; see `[2k §4.4]` for irreducibility justification). `FileSink`: rotating (rename+open), async fsync (drain thread only), `max_file_bytes` × `max_keep_count` disk bound. `OtlpLogSink`: OTel SDK OTLP log exporter wrapper; TLS reuses `[2g §4.1]` `cert_source`; retries capped per `max_export_retries`. `SyslogSink`: POSIX syslog(3). Source: `[2k §4.4]`–`[2k §4.7]`.

**LOG-003 supplemental:** Trace correlation — `Record::trace_id` (16 bytes) and `Record::span_id` (8 bytes). Three-macro pattern: `FIXPP_SLOG(level, tc, cat, fmt, args...)` (Tier 1 — session strand; caller obtains `tc` via `session.get_trace_context()` — a synchronous `const noexcept` accessor added to `Session` by `[2k App D §D.1]`; zero-cost one member call + 32-byte copy; no `co_await`); `FIXPP_ELOG(level, engine, cat, fmt, args...)` (Tier 2 — engine scope, reads `engine.engine_trace_context()` atomic snapshot per `[2d §4.4]`, for control-plane handlers and listener accept coroutines per `[2j §4.3]`); `FIXPP_LOG0(level, cat, fmt, args...)` (Tier 3 — zero context, for destructors/static init only). `EngineConfig::engine_trace_context` field (zeroed by default; set at engine construction to the root lifecycle span). `thread_local` unconditionally prohibited per `[const §XIII.3]`. Source: `[2k §4.2]`, `[2k §4.3]`, `[2k §6.4]`.

**OBS-001 supplemental:** OTel traces — `fixpp::otel::SessionSpans` RAII class; session lifecycle span (parent) + `ParseSpan` / `StoreSpan` / `DispatchSpan` (children). Parent-child relationship enables span nesting in Jaeger/Tempo. `session_trace_context()` accessor returns the session lifecycle span's `trace_context`; stored in `session_local<trace_context>` slot at session open per `[2d §4.6]`. All spans use the OTel C++ SDK trace API (no re-implementation). Source: `[2k §4.9]`.

**OBS-002 supplemental:** OTel metrics dual export — `OtelDualExportBuilder` produces a `MeterProvider` with two metric readers registered via `AddMetricReader()`: `PrometheusExporter` (MetricReader pull model, embedded single-threaded HTTP server on port 9464; plain HTTP unless `cert_source` is non-null) and `PeriodicExportingMetricReader` wrapping `OtlpMetricExporter` (push model, OTLP gRPC or HTTP/protobuf to the configured endpoint). A single `meter.add(counter, 1)` propagates to both readers via the SDK's internal fan-out. There is no `MultiMetricExporter` — `PrometheusExporter` is a `MetricReader`, not a `PushMetricExporter`, and cannot be passed to `MultiMetricExporter`. Source: `[2k §4.10]`.

**OBS-003 supplemental:** OTel logs sink — `OtlpLogSink` is the concrete `Sink` subclass that bridges the `fixpp::log::Sink` interface to the OTel logs bridge API. Field mapping: `Record::timestamp` → `TimeUnixNano`, `Record::level` → `SeverityNumber`/`SeverityText`, `Record::trace_id` → `TraceId`, `Record::span_id` → `SpanId`, resolved format string → `Body`, `Record::category` → attribute `fixpp.log.category`. Satisfies `[const §XIII.4]` (same sink interface for OTel and file sinks; no double-write path). Source: `[2k §4.6]`.

---
```
