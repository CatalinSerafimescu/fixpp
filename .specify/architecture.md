# fixpp Architecture

> **Status:** Draft v0.2 — Phase 2, Codex Gate A round 1 findings converged 2026-05-07; awaiting user sign-off.
> **Authority:** Module layering, public namespaces, design patterns. This document operationalises the rules set by `constitution.md`. On conflict, the constitution wins (Article XX); architectural choices that cannot satisfy a constitutional rule trigger an amendment, never a silent override.
> **Citation form:** other documents cite sections as `[arch §N.m]` (e.g., `[arch §3.2]`). This document cites the constitution as `[const §VIII.3]` and SYNTHESIS as `[SYN §3.1 Q5]`.
> **Scope rule:** this document fixes the *spine*. Per-subsystem detail (decimal type, parser internals, dictionary codegen, threading contract, store API, awaitable mutex, TLS interfaces, transport interface, C ABI message representation, control-plane interface, observability surface, session-tap API, SWIG binding shape) lives in the sibling design docs `2a`–`2m` listed in §10. Anything *more specific than module-level* in this file is intentionally provisional and may be tightened in those docs.

---

## Table of Contents

1. [Goals & Non-Goals](#1-goals--non-goals)
2. [Module Layering & Dependency Graph](#2-module-layering--dependency-graph)
3. [Public Namespaces](#3-public-namespaces)
4. [Per-Module Surface Inventory](#4-per-module-surface-inventory)
5. [Cross-Cutting Machinery](#5-cross-cutting-machinery)
6. [Plugin Pattern](#6-plugin-pattern)
7. [Build & Artifact Layout](#7-build--artifact-layout)
8. [Service-Mode Boundary](#8-service-mode-boundary)
9. [Header Discipline & Public-API Versioning](#9-header-discipline--public-api-versioning)
10. [Hand-off to Design Docs 2a–2m](#10-hand-off-to-design-docs-2am)
11. [Open Architectural Questions](#11-open-architectural-questions)
12. [Glossary](#12-glossary)

---

## 1. Goals & Non-Goals

### 1.1 Goals

- **One acyclic dependency graph** across the engine. A reader who skims this file should be able to predict, for any pair of modules, which way the include arrow points.
- **A small set of public surfaces** — the C++ API, the C ABI, the Python module, the gRPC service — each one explicitly enumerated and traceable to a module.
- **Cross-cutting machinery defined once.** Executors, allocators, errors, trace context, lifetimes, plugin pattern — locked here so the per-feature design docs can build on a stable spine.
- **Design seams for testability.** Every interface that touches the outside world (sockets, disks, clocks, OTel exporters) is pluggable so the session FSM and parser can be unit-tested without real I/O `[const §VII]` `[SYN §3.4 Q16]`.
- **The legal isolation boundary is structural, not bolted on.** The C ABI is the unique linkage seam between the AGPL engine and any non-AGPL consumer `[const §V.1]` `[const §IV.2]`.

### 1.2 Non-goals (v1.0)

- **No dynamic plugin loading.** All plugins are compile-time only `[const §XIV.4]`. `dlopen`/`LoadLibrary` is post-1.0.
- **No `clang-cl` on Windows.** MSVC is the sole Windows compiler `[const §II.2]`.
- **No `thread_local` for trace context.** Coroutines may resume on a different thread; `thread_local` is unsound for context propagation `[const §XIII.3]`.
- **No reliance on a single compiler version for HALO.** PMR fallbacks are part of the design, not an emergency hatch `[const §II.4]` `[SYN §3.2 Q6]`.
- **No standalone "header-only" claim for the engine.** Header-only applies only to the codegen output (typed messages) and a handful of utility headers in `core/`. The session/transport/store/log/service implementations live in compiled translation units behind a stable header surface.
- **No FIXP / SBE / FAST / SOFH / Orchestra in the v1.0 module set.** Those are scheduled per `[const §XVIII.2]`.

---

## 2. Module Layering & Dependency Graph

### 2.1 Modules (v1.0)

| # | Module | Path | Purpose | Public? |
|---|---|---|---|---|
| 1 | `core` | `src/core/` + `include/fixpp/core/` | Primitives, PMR allocators, error type, decimal traits, time, span, `expected`, `sync::async_mutex`, executor utilities, log interface, trace-context awaitable | Yes (C++) |
| 2 | `dictionary` | `src/dictionary/` + `include/fixpp/dict/` + generated `include/fixpp/v42/`, `v44/`, `v50sp2/` | XML loader, runtime dictionary, dialect overlays, generated typed messages | Yes (C++) |
| 3 | `wire` | `src/wire/` + `include/fixpp/wire/` | Parser, offset table, writer, validator, framer (multi-message TCP read), checksum/body-length | Yes (C++) |
| 4 | `session` | `src/session/` + `include/fixpp/session/` | `Session`, `Application` callback interface, FSM, seqnum mgmt, recovery, `MessageStore` interface, `SecurityProfile`, `SessionConfig` | Yes (C++) |
| 5 | `transport` | `src/transport/` + `include/fixpp/transport/` | `Transport` interface (≤5 pure-virtual), default ASIO TCP/TLS impl over OpenSSL, reconnect/back-off | Yes (C++) |
| 6 | `tls` | `src/tls/` + `include/fixpp/tls/` | `cert_source` interface, `Pinset`, cipher allow-list, `SecurityProfile` adapter | Yes (C++) |
| 7 | `log` | `src/log/` + `include/fixpp/log/` | Async logger core, sink interface, file/OTLP/syslog default sinks | Yes (C++) |
| 8 | `otel` | `src/otel/` + `include/fixpp/otel/` | Trace/metric/log span helpers, OTLP+Prometheus exporter wiring | Yes (C++) |
| 9 | `tap` | `src/tap/` + `include/fixpp/tap/` | Session tap ring buffer + iceoryx2 publisher + sync-callback hook | Yes (C++) |
| 10 | `capi` | `src/capi/` + `include/fix/c_api.h` | `extern "C"` opaque-handle API, `fixpp_error_t`, version macros | Yes (C ABI) |
| 11 | `service` | `service/` | `fixppd` daemon: gRPC control plane (default) + iceoryx2 data plane (opt-in) | Yes (binary + gRPC schema) |
| 12 | `bindings/python` | `bindings/python/` | SWIG `.i` files, pytest suite, wheel build | Yes (Python module) |
| 13 | `bindings/c` | `bindings/c/` | Example C consumers; not a library — example/test code | No (samples only) |

### 2.2 Dependency direction (acyclic)

```
                                ┌─────────────────────┐
                                │  bindings/python    │   (SWIG over C ABI)
                                │  bindings/c         │   (samples)
                                └──────────┬──────────┘
                                           │
                                           ▼
                                ┌─────────────────────┐
                                │  service/  fixppd   │   (gRPC + iceoryx2)
                                └──────────┬──────────┘
                                           │
                                           ▼
                                ┌─────────────────────┐
                                │       capi/         │   (extern "C", opaque handles)
                                └──┬──────┬──────┬────┘
                                   │      │      │
              ┌────────────────────┘      │      └────────────────────────┐
              ▼                           ▼                               ▼
       ┌─────────────┐             ┌─────────────┐                ┌─────────────┐
       │  session/   │             │  transport/ │                │   tap/      │
       └──┬──────────┘             └──────┬──────┘                └──────┬──────┘
          │                               │                              │
          ├─── wire/ ────┐                │                              │
          │              │                │                              │
          ├── dictionary/                 ├── tls/                       │
          │              │                │                              │
          │              │                │                              │
          ▼              ▼                ▼                              ▼
   ┌──────────────────────────────────────────────────────────────────────────┐
   │                                  core/                                   │
   │  primitives · PMR · error · expected · time · sync::async_mutex ·        │
   │  executor utils · decimal_traits · trace_context awaitable · log iface · │
   │  span                                                                    │
   └──────────────────────────────────────────────────────────────────────────┘
                                       ▲
                                       │
                      otel/, log/  also depend only on core/
```

### 2.3 Allowed edges (whitelist)

The build enforces this graph at the include level via `include-what-you-use` configuration plus a `tools/check_layers.py` lint:

| Module | May include from |
|---|---|
| `core` | (nothing project-internal) |
| `dictionary` | `core` |
| `wire` | `core`, `dictionary` |
| `tls` | `core` |
| `transport` | `core`, `tls`, `log` (interfaces only) |
| `log` | `core` |
| `otel` | `core`, `log` (sink interface) |
| `session` | `core`, `dictionary`, `wire`, `transport` (interface), `log` (interface), `otel` (interface) |
| `tap` | `core`, `wire` (read-only views), `log` (interface) |
| `capi` | `session`, `wire`, `dictionary`, `transport`, `tls`, `log`, `otel`, `tap`, `core` (read-only) |
| `service` | `capi` only — **never** the C++ headers |
| `bindings/python` | `capi` only |
| `bindings/c` | `capi` only |

**Forbidden:** any back-edge (e.g., `core` including `session/`), any cycle, and any non-C-ABI consumption from `service/` or the bindings. CI fails on violation.

### 2.4 Why this shape

- **`core/` is a true leaf.** Every cross-cutting primitive — allocators, error type, decimal traits, awaitable mutex, log/trace interfaces — lives here so it can't accidentally pick up dependencies on protocol code.
- **`wire/` and `dictionary/` are siblings, not nested.** Generated typed messages (`fixpp::v42::*`) need both: dictionary metadata for tag→type tables and wire primitives for parse/serialise. They sit in `include/fixpp/v42/` (generated) but compile against both modules.
- **`session/` depends on the `transport/` *interface*, not its impl.** This is the key test seam: a session FSM under test runs against an in-memory `MockTransport` `[const §VII]`. The default ASIO/OpenSSL transport is selected at link time.
- **The `capi/` layer is the choke point** for everything that crosses the AGPL boundary. Putting `service/` and `bindings/python` strictly downstream of it keeps the legal isolation structural rather than aspirational `[const §V.1]`.
- **`tap/` reads from `wire/` views directly.** Tap is a pure observer — it never mutates session state — so it gets a read-only edge to `wire/` for zero-copy access to message bytes without going through `session/`.

---

## 3. Public Namespaces

| Namespace | Purpose | Module | Notes |
|---|---|---|---|
| `fixpp` | Top-level identity, version constants, common types re-exported for convenience | n/a | Avoid putting code directly here; prefer a sub-namespace. |
| `fixpp::core` | Primitives: `error`, `expected_t<T>`, `span`, `time_point`, `pmr` aliases, `decimal<T>` & `decimal_traits<T>` | `core` | |
| `fixpp::sync` | `async_mutex`, future awaitable utilities | `core` | Lives under `core` physically; named separately to keep its discipline visible. |
| `fixpp::dict` | Runtime dictionary, XML loader, dialect overlays | `dictionary` | |
| `fixpp::v42`, `fixpp::v44`, `fixpp::v50sp2`, `fixpp::vt11` (FIXT.1.1) | Generated typed messages, version-namespaced `[SYN §3.3 Q12]` | `dictionary` (codegen output) | One `Messages.hpp` per version. |
| `fixpp::wire` | Parser, writer, offset table, validator, framer | `wire` | Low-level; most users go through typed messages. |
| `fixpp::transport` | `Transport` interface, `asio_tls_transport`, `Endpoint`, reconnect policy types | `transport` | |
| `fixpp::tls` | `cert_source`, `Pinset`, `CipherPolicy`, `SecurityProfile` enum | `tls` | |
| `fixpp::session` | `Session`, `Application`, `MessageStore`, `MemoryStore`, `FileStore`, `SessionConfig` | `session` | |
| `fixpp::log` | `Logger`, `Sink`, `Level`, `Record`, `LogConfig` | `log` | |
| `fixpp::otel` | `TracerProvider`, `MeterProvider`, span helpers, exporter setup | `otel` | |
| `fixpp::tap` | `RingBufferTap`, `Iox2Tap`, `SyncCallbackTap`, `TapConfig` | `tap` | |
| `fixpp::service` | Public service-mode plugin interfaces (`ControlPlane`, `ControlPlaneConfig`); the daemon binary `fixppd` and default plugin impls are downstream and **not** C++ API | `service` (interface part) | Interface headers only; see §4.11. |
| `fixpp::current_trace_context` | The free awaitable returning the current `trace_context` from the strand-stored slot `[const §XIII.3]` | `core` | Not `thread_local`; coroutine-correct. |
| `fixpp::detail` | Internal-only helpers; never user-callable | (any module) | Headers carry `// detail: not API` and are excluded from Doxygen. |

The C ABI lives in `extern "C"` and uses the `fixpp_` prefix (e.g., `fixpp_session_open`, `fixpp_error_t`). It does not enter any C++ namespace.

---

## 4. Per-Module Surface Inventory

For each module: the public-facing types/functions, the design-doc that owns the detail, and the catalogue rows it implements.

### 4.1 `core`

**Public surface (locked here):**

- `fixpp::core::error` — engine-side error type. Tagged enum + optional payload (catalogue row, source location for debug builds).
- `fixpp::core::expected_t<T>` — alias for `std::expected<T, fixpp::core::error>`. Returned from value-producing APIs that can fail with a recoverable engine error. Hard invariant violations abort; they do not return.
- `fixpp::core::decimal<T>` and `fixpp::core::decimal_traits<T>` — extension point. Default `T = pod_decimal { int64_t mantissa; int8_t exponent; }`; users specialise traits to plug in `decimal128`, `boost::multiprecision`, etc. `[SYN §3.1 Q5]` — owned by **2a**.
- `fixpp::core::time_point`, `fixpp::core::duration` — `std::chrono::system_clock` aliases plus `utc_time_to_fix_string` and `fix_string_to_utc_time` helpers.
- `fixpp::core::span<T>` — `std::span` alias; never owns memory. Used pervasively for buffer views.
- `fixpp::core::pmr` — convenience aliases for `std::pmr::memory_resource`, `std::pmr::polymorphic_allocator<T>`.
- `fixpp::sync::async_mutex` — owned by **2f**. The only mutex shape allowed in coroutine context `[const §XI.3]`.
- `fixpp::core::Logger`, `fixpp::core::Sink` (interfaces) — owned by **2k**. Implementations live in `log/`.
- `fixpp::core::Clock` — pluggable timing source. ≤5 pure-virtual: `now()` (UTC), `steady_now()` (monotonic), `sleep_until(...)` (awaitable), `cancel_sleeps()`. Default impl `fixpp::core::system_clock_source` wraps `std::chrono::system_clock` + `std::chrono::steady_clock` + ASIO `steady_timer`. Required test seam for heartbeat timers, SendingTime threshold checks, session scheduling (S-035), and log/OTel timestamps (`[arch §1.1]` promised pluggable clocks; this row delivers it). Owned by **2d** (timing belongs to the threading contract).
- `fixpp::current_trace_context` — strand-stored `trace_context` accessor `[const §XIII.3]`.

**Catalogue rows:** NFR-001 to NFR-014 (cross-cutting), LOG-001..004 (interface), OBS-001..003 (interface), SVC-005 (interface contract). **A new NFR row `NFR-015 — pluggable Clock interface` is required** in `feature-catalogue.md`; tracked in §11 row 7 and added when **2d** lands.

### 4.2 `dictionary`

**Public surface:**

- `fixpp::dict::Dictionary` — runtime, owns field metadata for one FIX version + dialect overlays.
- `fixpp::dict::XmlLoader` — QuickFIX-XML compatible loader (`FIX42.xml`, `FIX44.xml`, …).
- `fixpp::dict::DialectOverlay` — per-session overrides on top of a base dictionary `[SYN §3.3 Q13]`.
- `fixpp::v42::*`, `fixpp::v44::*`, `fixpp::v50sp2::*`, `fixpp::vt11::*` — generated typed messages.
- `fixpp::dict::ComponentRef`, `fixpp::dict::FieldRef`, `fixpp::dict::GroupRef` — metadata accessors used by typed messages and validator.

**Codegen pipeline (locked):**
1. `tools/codegen/fixpp-codegen` reads `dictionaries/FIXxx.xml`.
2. Emits `include/fixpp/<vXX>/Messages.hpp` (typed messages — flyweights), `include/fixpp/<vXX>/Fields.hpp` (`constexpr` field metadata tables), `include/fixpp/<vXX>/Validator.hpp` (per-message rules).
3. CMake target `fixpp::dict::generate-vXX` runs at configure time; outputs go into the build tree, not the source tree, so a dirty checkout never carries stale codegen.

**Design doc:** **2c** owns layout details, multi-version rules, header-only `constexpr` tables `[SYN §3.3 Q11–Q13]`.

**Catalogue rows:**

- **Dictionary itself:** D-001..D-011, OSS-001, OSS-010.
- **Application-message generated typed-message classes and `constexpr` field metadata** (typed-message *classes*; parse/serialise/validate behaviour belongs to wire §4.3): A-001..A-013 (order-management; codegen). A-014..A-034 (additional order-management variants; runtime-XML only in v1.0; codegen deferred to v1.x — see `[const §XVIII.7]`). M-001..M-012 (market data), P-001..P-008 (post-trade), C-001..C-003 (collateral/positions/account), R-001..R-005 (reg/IOI/news), N-001..N-003 (network counterparty / user request). Per `[const §XVIII]` the FIX-Latest range A-035..A-065 is post-1.0 and not owned by v1.0 codegen.
- **Drop:** A-024 (catalogue note: duplicate of A-018 — recommend removing per `[SYN §4.4]`).

### 4.3 `wire`

**Public surface:**

- `fixpp::wire::Parser` — header-only zero-copy parser; iterator + offset-table layered access `[SYN §3.1 Q1, Q3]`.
- `fixpp::wire::OffsetTable` — `(tag, offset, length)` index, eager or lazy depending on access mode.
- `fixpp::wire::Writer` — serialiser over a caller-supplied buffer; auto BodyLength + CheckSum at commit `[SYN §1.1]`.
- `fixpp::wire::Validator` — message-level structural and field-level type/required checks driven by dictionary metadata.
- `fixpp::wire::Framer` — multi-message TCP-read framing; partial-read handling `[SYN §1.1]`.

**Lifetime contract (operationalised):**

- All views are flyweights over a caller-owned buffer.
- C++ side: `[[clang::lifetimebound]]` on view constructors; partial on GCC; ignored on MSVC.
- Debug-build runtime check: views hold a generation counter from the originating buffer pool. Access after buffer reuse traps `[SYN §3.1 Q2]`.

**Design doc:** **2b** owns the eager/lazy hybrid measurement spike and the lifetime-enforcement detail.

**Catalogue rows:**

- **Wire infrastructure:** W-001..W-014, OSS-006..OSS-008, OSS-013.
- **Application-message parse / serialise / validate behaviour** (the *typed classes themselves* are owned by dictionary/codegen §4.2; wire owns how their bytes go on/off the wire): A-001..A-034, M-001..M-012, P-001..P-008, C-001..C-003, R-001..R-005, N-001..N-003. Each generated typed message reuses the same `wire::Parser` / `wire::Writer` / `wire::Validator` infrastructure; the validator consumes `dict::FieldRef` metadata to enforce required/optional/conditional rules per message.

### 4.4 `session`

**Public surface:**

- `fixpp::session::Session` — main user-facing handle. Coroutine-driven `[const §XI.1]`.
- `fixpp::session::Application` — callback interface (`onLogon` / `onLogout` / `toAdmin` / `fromAdmin` / `toApp` / `fromApp`) `[SYN §1.3]`.
- `fixpp::session::MessageStore` — interface; writes return `asio::awaitable<void>` `[SYN §3.2 Q7]`.
- `fixpp::session::MemoryStore`, `fixpp::session::FileStore` — default impls.
- `fixpp::session::SessionConfig` — frozen config struct: `SecurityProfile`, dictionary, `MessageStore` factory, executor opt-out, lock policy, recovery thresholds, dialect overlay, tap consumer, log/otel hooks.
- `fixpp::session::SecurityProfile` — enum: `mtls_ca` / `mtls_pinned` / `one_way_ca [[deprecated]]` `[const §XII.5]`.
- `fixpp::session::RejectPolicy`, `fixpp::session::LockPolicy` — enums per `[SYN §3.2 Q8, Q9]`.
- `fixpp::session::Listener` — multi-session acceptor.

**Threading default (locked):** every `Session` runs on a `strand` derived from a user-supplied executor. Application callbacks dispatch onto that strand by default `[const §XI.4]` `[SYN §3.2 Q6c]`. Owned by **2d**.

**Design docs touching session:**
- **2d** — application threading contract (default strand, executor opt-out).
- **2e** — `MessageStore` async API + QuickFIX-compat shim feasibility.
- **2f** — `async_mutex` (used here).
- **2g** — TLS `cert_source` integration (used here).
- **2l** — session-tap consumer API.

**Catalogue rows:** S-001 to S-038 (extended through S-037 NoMsgTypes-in-Logon `[FIX-SL §4.3.8]` and S-038 ApplicationSystemName/Version/Vendor `[FIX-SL §4.3.9]` — both added in Phase 1.6), OSS-002, OSS-005, OSS-009, COM-008, COM-010, COM-011.

### 4.5 `transport`

**Public surface:**

- `fixpp::transport::Transport` — interface. **≤5 pure-virtual methods** `[const §XIV.2]`: `async_connect`, `async_read_some`, `async_write`, `cancel`, `close`. (`async_handshake` for TLS is folded into a TLS-aware sub-interface; see **2h**.)
- `fixpp::transport::Endpoint`, `fixpp::transport::ReconnectPolicy` — value types.
- `fixpp::transport::asio_tls_transport` — default ASIO TCP/TLS over OpenSSL impl `[const §XII.1]`.

**Design doc:** **2h** owns the interface definition, ASIO impl, and the test-mock contract.

**Catalogue rows:** T-001 to T-013, plus T-039 (FIXS certificate parameters `[FIXS §3.4]`), T-040 (FIXS secrets distribution `[FIXS §4.1]`), T-041 (FIXS auth↔authorization linkage `[FIXS §4.4]`) — all added in Phase 1.6. Note: T-039/T-040 cross over to `tls/` (§4.6) for cert validation and storage; T-041 cross-cuts into `session/` (§4.4) for CompID-to-TLS-identity binding.

### 4.6 `tls`

**Public surface:**

- `fixpp::tls::cert_source` — interface; `load_leaf`, `load_chain`, `sign_callback` for HSM flows. ≤5 pure-virtual `[SYN §3.4 Q14]`.
- `fixpp::tls::file_cert_source` — default file-path impl.
- `fixpp::tls::Pinset` — `add(cert)` / `remove(cert)` API; FIXS RC1 §5 add-then-remove rotation `[SYN §3.4 Q15]`.
- `fixpp::tls::CipherPolicy` — compile-time allow-list per `[const §XII.3]`.

**Design doc:** **2g**.

**Catalogue rows:** T-006, T-007, T-008, T-011, T-013.

### 4.7 `log`

**Public surface:**

- `fixpp::log::Logger` — facade. Producer side: zero-allocation per record `[const §XIII.2]`.
- `fixpp::log::Record` — POD: timestamp, level, format-id, captured args by value/view.
- `fixpp::log::Sink` — interface; ≤5 pure-virtual.
- `fixpp::log::FileSink`, `fixpp::log::OtlpLogSink`, `fixpp::log::SyslogSink` — default impls.

**Design doc:** **2k** (combined with otel).

**Catalogue rows:** LOG-001 to LOG-004.

### 4.8 `otel`

**Public surface:**

- `fixpp::otel::TracerProvider`, `fixpp::otel::MeterProvider` — wrappers around the OpenTelemetry C++ SDK.
- `fixpp::otel::SessionSpans` — span helper for session lifecycle / parse / store / dispatch.
- `fixpp::otel::PrometheusExporter`, `fixpp::otel::OtlpExporter` — wired so a single collector pipeline exports both `[SYN §3.6 Q21]`.

**Design doc:** **2k**.

**Catalogue rows:** OBS-001, OBS-002, OBS-003.

### 4.9 `tap`

**Public surface:**

- `fixpp::tap::TapConsumer` — variant alias (`RingBufferTap` / `Iox2Tap` / `SyncCallbackTap`) `[SYN §3.6 Q22]`.
- `fixpp::tap::TapConfig` — buffer size, drop policy (`drop-oldest` permitted here per `[const §XIII.2]` and `[const §XV.15]`), backpressure hook.
- Tap reads `wire::View` instances; never copies on the producer side beyond the ring write.

**Design doc:** **2l**.

**Catalogue rows:** S-036, NFR-014, COM-008, SVC-002 (when configured for cross-process).

### 4.10 `capi`

**Public surface:** `include/fix/c_api.h` only. No transitive C++ headers leak.

- Opaque handles: `fixpp_engine_t`, `fixpp_session_t`, `fixpp_msg_t`, `fixpp_dict_t`, `fixpp_store_t`.
- Error type: `fixpp_error_t` — bounded enum with reserved range `[const §X.4]` `[SYN §3.5 Q19]`.
- Decimal at the C boundary: PoD `(int64 mantissa, int8 exponent)` `[const §X.3]`.
- Versioning macros: `FIXPP_C_ABI_VERSION_MAJOR/MINOR/PATCH` with the SemVer rules from `[const §X.1]`.
- Reentrancy: each symbol is documented with one of `thread-safe` / `single-thread` / `requires-session-lock` `[const §X.5]`.
- The `extern "C"` declarations are split by domain: `c_api/engine.h`, `c_api/session.h`, `c_api/message.h`, `c_api/dict.h`, `c_api/store.h`, `c_api/log.h`, `c_api/otel.h`, all included from `fix/c_api.h`.

**Design doc:** **2i** owns the message representation and the error-enum range.

**Catalogue rows:** CA-001 to CA-010.

### 4.11 `service`

The module has two parts with different stability and API status. The split resolves the apparent contradiction between SVC-005's "pluggable control plane" promise and the AGPL boundary rule (§8): the *interface* is a public C++ header like any other plugin contract; the *daemon and default impls* sit downstream of the C ABI.

**Public C++ interface surface (pluggable):**

- `fixpp::service::ControlPlane` — public abstract interface in `include/fixpp/service/control_plane.h`. ≤5 pure-virtual `[const §XIV.2]`. Lets shops swap the default gRPC implementation for JSON-over-Unix-socket or anything else without rebuilding the engine. Owned by **2j** (`SVC-005`).
- `fixpp::service::ControlPlaneConfig` — value-typed config passed at instantiation.
- gRPC schema: `service/proto/fixpp_control.proto` — session lifecycle (`OpenSession`, `CloseSession`, `Configure`), observability (`StreamMetrics`, `StreamLogs`). The proto is a stable contract; the C++ classes implementing the gRPC adapter are not.

**Internal (not C++ API):**

- `fixppd` binary — daemon. Consumes the engine through the **C ABI only** `[const §V.1]` (legal isolation). May `#include <fixpp/service/control_plane.h>` (the interface header above) but **must not** `#include` anything else under `include/fixpp/`.
- Default gRPC implementation of `ControlPlane` — translation-units under `service/grpc/`, links against `fixpp::capi` and the gRPC C++ runtime.
- Optional iceoryx2 publisher for the data plane `[const §XIV.3]`.

**Catalogue rows:** SVC-001, SVC-002, SVC-003, SVC-004, SVC-005.

### 4.12 `bindings/python`

**Public surface:** the Python `fixpp` module.

- SWIG `.i` files generate a CPython extension wrapping the C ABI.
- GIL handling: reacquire-and-call on receive callbacks (v1.0); async-queue handoff is documented as future work `[SYN §3.5 Q18]`.
- Exception translation maps `fixpp_error_t` → Python `FixppError` with stable enum values.
- `pip install fixpp` is the consumption surface; Linux x86_64 wheel is mandatory `[const §IV.3]`.

**Design doc:** **2m**.

**Catalogue rows:** PY-001 to PY-005.

---

## 5. Cross-Cutting Machinery

### 5.1 Executor model

- **Primitive:** `asio::any_io_executor`. The engine never picks a concrete executor; users pass one in.
- **Per-session strand.** Construction wraps the user executor in `asio::make_strand(...)` unless the user supplies an explicit `executor` opt-out in `SessionConfig`. Application callbacks always dispatch onto the strand `[const §XI.4]`.
- **Coroutine composition.** `asio::awaitable<T>` is the return type of every async session/transport entry point `[const §XI.1]`. Cancellation flows via ASIO native cancellation slots `[const §XI.2]`.
- **No `co_await` of `std::mutex`.** `fixpp::sync::async_mutex` is the only legal coroutine mutex `[const §XI.3]`.
- **HALO-first frame allocation.** The engine does not pin a compiler version. Where HALO doesn't fire on the inbound dispatch path, a per-awaiter override constructs the promise on the per-session PMR arena `[const §XI.6]` `[SYN §3.2 Q6]`.

### 5.2 Allocator policy

- **Public API is PMR-aware.** Every long-lived owned container in the engine uses `std::pmr::polymorphic_allocator<T>`. Users plug in arenas, mimalloc, jemalloc.
- **Per-session memory_resource.** `SessionConfig` carries a `std::pmr::memory_resource*` (default: monotonic, reset between message batches). The session's coroutine frames, the offset-table backing, and any retransmit copies pull from it.
- **Global default: mimalloc** `[SYN §1.5]`. Linked into the engine binary on Linux; users opt in on Windows.
- **Hot-path discipline.** Zero `new`/`delete` between parse and `fromApp` callback `[const §VIII.5]`. The `tools/check_alloc.py` post-link symbol scan and the bench-time `mallocnesia` interceptor guard against regressions.

### 5.3 Error model

- **Recoverable engine errors:** `fixpp::core::expected_t<T>` (alias for `std::expected<T, fixpp::core::error>`). Used for parser failures, validator failures, store I/O errors.
- **Coroutine cancellation:** propagated as `asio::error::operation_aborted` per ASIO convention. The C ABI maps it to a dedicated `fixpp_error_t::CANCELLED` `[const §XI.2]`.
- **Invariant violations:** `assert` in debug; `std::abort` in release. Examples: tag table out of bounds, internal queue invariant broken. These are bugs, not error returns.
- **Hot path is exception-free.** No `throw` between parse and `fromApp` `[const §VIII.5]`. Exceptions are reserved for construction-time configuration errors (e.g., bad dictionary XML), where the alternative is `expected_t<Engine>` and we choose throw for ergonomics.
- **C ABI translates** `fixpp::core::error` → `fixpp_error_t` at the boundary. Out-of-range values from older consumers are tolerated; out-of-range values *to* a consumer are mapped to `FIXPP_ERR_UNKNOWN` `[const §X.4]`.

### 5.4 Trace context

- **Storage:** `SessionConfig.trace_context_provider`, called once at session open, returns a `fixpp::otel::trace_context` stored on the session strand.
- **Access:** `co_await fixpp::current_trace_context` returns the value bound to the current strand; outside session scope, returns the `Engine`-level fallback context `[const §XIII.3]`.
- **`thread_local` is prohibited.** A coroutine that suspends on thread A may resume on thread B; a `thread_local` write made before suspension is not guaranteed visible after resume. Hence the strand-stored awaitable.
- **Logs and traces correlate at the backend.** Every `log::Record` carries the trace_id/span_id captured from the awaitable; no manual stitching at the OTel collector.

### 5.5 Lifetime model

- **Flyweights** are the rule for `wire::View`, typed messages, and offset-table accessors. They never own buffers `[SYN §3.1 Q2]`.
- **Owned types** (`Session`, `MessageStore`, `Engine`) follow standard value semantics; copy is deleted, move is enabled where natural.
- **`[[clang::lifetimebound]]`** marks every view-returning constructor and accessor — best-effort lifetime warnings on Clang and partial GCC; ignored on MSVC.
- **Debug-build generation counters** trap on use-after-reuse for buffer-pool views. Disabled in release.

### 5.6 Configuration shape

- **`SessionConfig` is value-typed and frozen at session open.** No mid-session reconfiguration of: dictionary, security profile, message store, executor, lock policy, dialect overlay. The supported pattern for any of these is close-and-reopen the session. Mutating ops on session-adjacent state that *do* admit mid-session change (e.g., pinset rotation per `[const §XII]`) go through their own APIs and are explicitly thread-aware. **Mid-session dialect-overlay swap is rejected categorically per `[2c §7.2]`** — there is no `Session::swap_dialect_overlay(...)` API in v1.0.
- **Two intake formats:** QuickFIX `[DEFAULT]` / `[SESSION]` CFG (mandated) and TOML (modern). New formats require justification per `[const §XV.16]`.
- **`EngineConfig`** sits one level up: shared executors, allocator factories, log/otel providers, the **`Clock` source** (§4.1, §6), and default plugin selections. `SessionConfig` inherits the `Clock` from its `EngineConfig` unless overridden — a test runs against an injected `mock_clock` to step time deterministically through heartbeat windows, SendingTime checks, and scheduled connect/disconnect transitions.

### 5.7 Logging

- **Producer:** zero-alloc per record; format-deferred; lock-free MPSC `[const §XIII.2]`.
- **Consumer:** dedicated drain thread; formats records; dispatches to sinks.
- **Sinks** implement the `Sink` interface (≤5 pure-virtual). File (rotating + async fsync), OTLP exporter, syslog ship in v1.0.
- **Adoption decision deferred** to **2k**'s benchmark spike — `quill` vs own impl.

### 5.8 Backpressure

- **App and session message paths:** `block` or `disconnect-and-recover` only. **`drop-oldest` is banned** on these paths `[const §XV.15]`.
- **Telemetry/log/tap paths:** `drop-oldest` is permitted under bounded-queue overflow, with a recorded counter `[const §XIII.2]`.

---

## 6. Plugin Pattern

**Each pluggable interface gets:**
1. A pure-virtual class in the relevant module's public header.
2. **≤5 pure-virtual methods** `[const §XIV.2]`. Larger surfaces require a one-paragraph justification reviewed at Gate A.
3. One default implementation shipped in v1.0.
4. A clear factory entry point that takes a `std::pmr::memory_resource*` plus interface-specific config.
5. Compile-time selection in v1.0; no `dlopen`.

**v1.0 pluggable list:**

| Interface | Default impl | Design doc | Notes |
|---|---|---|---|
| `fixpp::transport::Transport` | `asio_tls_transport` | **2h** | DPDK / Onload / SHM are post-1.0. |
| `fixpp::tls::cert_source` | `file_cert_source` | **2g** | HSM/KMS are user-side. |
| `fixpp::session::MessageStore` | `MemoryStore`, `FileStore` | **2e** | Async API; QuickFIX-compat shim on best-effort. |
| `fixpp::log::Sink` | `FileSink`, `OtlpLogSink`, `SyslogSink` | **2k** | |
| `fixpp::service::ControlPlane` (SVC-005) | gRPC | **2j** | JSON-over-Unix-socket is a sample alt impl. |
| `fixpp::tap::TapConsumer` (variant) | `RingBufferTap` (in-process) | **2l** | iceoryx2 opt-in cross-process. |
| `fixpp::core::Clock` | `system_clock_source` | **2d** | Test seam for heartbeat / SendingTime / session scheduling / log + OTel timestamps. Carried by `EngineConfig` (§5.6); mock impl steps time deterministically in tests. |

The `Application` callback interface (`onLogon`, `onLogout`, `toAdmin`, `fromAdmin`, `toApp`, `fromApp`) is six methods — over the ≤5 cap. **Justification (Gate-A-eligible):** all six are normative semantic distinctions, not boilerplate. `toAdmin`/`fromAdmin` and `toApp`/`fromApp` separate session-layer admin messages from application messages; the in/out distinction is required for users to mutate outbound (e.g., add dialect-specific tags before send) without seeing inbound. Removing any one collapses a normative semantic users rely on for compliance and audit `[SYN §1.3]`. Reviewed at Gate A on this document.

---

## 7. Build & Artifact Layout

### 7.1 Build outputs

| Artifact | Linux | Windows |
|---|---|---|
| Static library | `libfixpp.a` | `fixpp.lib` |
| Shared library | `libfixpp.so` | `fixpp.dll` + import lib `fixpp.lib` |
| C ABI | re-exported from the same shared library; `extern "C"` symbols only | same |
| Python extension | `_fixpp.so` (SWIG-generated) | `_fixpp.pyd` |
| Python wheel | `fixpp-<ver>-cp310-cp310-manylinux_2_28_x86_64.whl` (mandatory) | best-effort |
| Service binary | `fixppd` | `fixppd.exe` |
| Codegen tool | `fixpp-codegen` (build-only) | same |

### 7.2 Build trees

- `build/<preset>/` — out-of-source per Conan profile. Generated codegen output lives at `build/<preset>/_codegen/include/fixpp/<vXX>/`.
- The source tree never carries generated files — a `git status --porcelain` after configure must be empty.

### 7.3 Header surface

```
include/
├── fix/
│   └── c_api.h              # the ONE C-ABI entry header
├── fix/c_api/               # split sub-headers (engine.h, session.h, …)
└── fixpp/                   # C++ public surface
    ├── core/                # primitives
    ├── sync/                # async_mutex
    ├── dict/                # runtime dict; XML loader
    ├── wire/                # parser/writer/offset-table
    ├── transport/           # interface + ASIO impl headers
    ├── tls/                 # cert_source, Pinset, CipherPolicy
    ├── session/             # Session, Application, MessageStore
    ├── log/                 # Logger, Sink
    ├── otel/                # tracer/meter providers
    ├── tap/                 # ring buffer / iox2 / sync callback
    └── v42/, v44/, v50sp2/, vt11/   # generated typed messages (build tree)
```

### 7.4 CMake target layout

- `fixpp::core`, `fixpp::dict`, `fixpp::wire`, `fixpp::transport`, `fixpp::tls`, `fixpp::session`, `fixpp::log`, `fixpp::otel`, `fixpp::tap` — `OBJECT` libraries combined into the final `fixpp` shared/static.
- `fixpp::capi-objects` — `OBJECT` library producing the `extern "C"` translation units; combined into the same shared library.
- `fixpp` — the **C++ public umbrella**. Links every object library above. `INTERFACE_INCLUDE_DIRECTORIES = include/`, exposing `<fix/c_api.h>` and the entire `<fixpp/...>` C++ surface. This is what `find_package(fixpp) COMPONENTS Cxx` brings in for in-process C++ users.
- `fixpp::capi` — the **C-ABI consumer target**. Same underlying shared library as `fixpp` (no duplication of object code), but `INTERFACE_INCLUDE_DIRECTORIES = include/fix/` only. C-ABI consumers link this; they cannot accidentally `#include <fixpp/...>` because the C++ headers aren't in their interface include path.
- `fixpp::service-iface` — `INTERFACE` (header-only) target exposing `include/fixpp/service/` (just the public plugin interfaces — `ControlPlane`, `ControlPlaneConfig`). Depends transitively on `fixpp::core` headers it needs.
- `fixpp-codegen` — host tool; not exported.
- `fixppd` — depends on `fixpp::capi` and `fixpp::service-iface` (plus gRPC and iceoryx2 externs). It does **not** depend on `fixpp` or any of the C++ engine OBJECT libraries; it reaches the engine exclusively through `extern "C"` symbols.
- `fixpp-python` — SWIG target; depends only on `fixpp::capi` and the SWIG-generated wrapper.

**Constraint enforced by CMake property check:** any target downstream of `fixpp::capi` that also lists `fixpp` (the C++ umbrella) in its link interface fails the `tools/check_layers.py` lint. This is the structural backstop for the boundary rule in §8.

### 7.5 Conan profile mapping

CMake presets read Conan profiles per `[const §III.3]`. The preset names match the profile names (`linux-clang-asan` ↔ `linux-clang-asan`). Sanitizer presets enable `BUILD_SHARED_LIBS=ON` to surface ABI issues; coverage preset force-enables `-fprofile-instr-generate`.

---

## 8. Service-Mode Boundary

The `service/` directory exists to package the engine as a daemon. Its architectural rule:

> The `fixppd` daemon and any default plugin implementations under `service/` consume the engine **only through the C ABI**. They must not include engine internal headers (`<fixpp/wire/...>`, `<fixpp/session/...>`, `<fixpp/dict/...>`, etc.).

The rule applies to the binary and to default impl translation units. It does **not** restrict `include/fixpp/service/control_plane.h` or other public service-mode interface headers — those live under `include/fixpp/`, depend only on `core/`, and are part of the public C++ plugin surface like every other interface listed in §6. `fixppd` includes `<fixpp/service/control_plane.h>` (the interface), implements or instantiates a `ControlPlane`, and reaches engine functionality through the C ABI symbols exposed by `fixpp::capi`.

This is enforced by:
- A CMake target visibility rule: `fixppd` links `fixpp::capi` and `fixpp::service-iface` only; the C++ engine umbrella `fixpp` is **not** in its include search path or link interface.
- The `tools/check_layers.py` lint scans `service/` source for any `#include <fixpp/X/...>` where `X != service`, and fails the build on any hit. `<fixpp/service/...>` is allowed (interface). `<fixpp/wire/...>`, `<fixpp/session/...>`, etc. are forbidden.

**Why this matters:** the service is the most common deployment shape for non-C++ consumers and for HFT shops that do not want the engine in-process. Keeping it strictly downstream of the C ABI makes the legal isolation (AGPL boundary `[const §V.1]`) structural, lets the service be packaged separately under different licensing if a commercial deployment requires it, and gives us a built-in compatibility test for the C ABI: if the service builds, the C ABI is at least cohesive enough to host a non-trivial consumer.

### 8.1 Control plane (gRPC, default)

- Schema lives at `service/proto/fixpp_control.proto`.
- Transport: Unix domain socket (Linux), named pipe (Windows). TCP gRPC is opt-in via config and not the default for security reasons.
- Control-plane interface (`SVC-005`) lets shops swap gRPC for JSON-over-Unix-socket or anything else without rebuilding the engine. Owned by **2j**.

### 8.2 Data plane (iceoryx2, opt-in)

- iceoryx2 publish/subscribe over shared memory.
- Topic shape, ownership semantics, backpressure, fallback when iceoryx2 isn't running — all in **2l** (which also covers the in-process tap consumer).
- A consumer that needs only request/response (no high-volume message stream) can run gRPC-only and ignore iceoryx2.

---

## 9. Header Discipline & Public-API Versioning

### 9.1 Public vs internal headers

- Every header under `include/fixpp/` is part of the **C++ public API surface** unless it lives in `include/fixpp/<module>/detail/`. Detail headers are excluded from the install set.
- **Doxygen scopes:** only public headers are scanned. Detail headers carry `\internal` directives.
- **No transitive C++ leaks across `capi/`.** `include/fix/c_api.h` is `#include <stddef.h>`, `<stdint.h>`, `<stdbool.h>` only — no `<atomic>`, no `<type_traits>`, no `<asio>`. Verified by a CI grep.

### 9.2 Versioning

- **Library SemVer** (the C++ surface): `<MAJOR>.<MINOR>.<PATCH>`; pre-1.0 means anything can change.
- **C ABI SemVer** (independent track from `[const §X.1]`): the C ABI may stay at MAJOR=1 across multiple library MAJOR bumps, provided the surface stays compatible; that's the point of having a separate track.
- **Macros:** `FIXPP_VERSION_MAJOR/MINOR/PATCH` for the library, `FIXPP_C_ABI_VERSION_MAJOR/MINOR/PATCH` for the C ABI. Both are emitted into the build by `tools/cmake/version.cmake`.
- **`abidiff` (Linux) and structural diff (Windows)** run in Tier 2 against the previous tagged C ABI release `[const §IX.5]`.

### 9.3 Stability tiers

- **Stable from v1.0:** the C++ public API of `core`, `wire` accessors, `dictionary`, `session`, `transport` interface, `tls`, `log` interface, `tap` interface, the C ABI, the gRPC control-plane proto.
- **Provisional (may change in patch releases without notice during early v1.x):** the iceoryx2 topic shape, the `SecurityProfile::one_way_ca` deprecation timing, the in-process logger's choice of impl (quill vs own).
- **Internal (no stability guarantee):** anything in `fixpp::detail` or under `<module>/detail/`.

---

## 10. Hand-off to Design Docs 2a–2m

This document fixes the spine. Each design doc below owns the named subsystem in detail. All Phase 2 design docs are non-trivial by definition `[const §XVII.1]` and require Codex Gate A before `/tasks`.

| Doc | Subsystem | Owns | Cross-cutting hooks (defined here) |
|---|---|---|---|
| **2a** | Decimal type | `fixpp::core::decimal<T>`, `decimal_traits<T>`, PoD C-ABI shape | §4.1 surface; §5.3 error model |
| **2b** | Wire parser + offset table | Eager/lazy hybrid, lifetime-contract enforcement, validator | §4.3, §5.5 lifetimes |
| **2c** | Dictionary codegen | Header layout, multi-version coexistence, dialect overlay binding | §4.2; §3 namespaces |
| **2d** | Application threading contract | Default per-session strand, executor opt-out, callback dispatch; **`fixpp::core::Clock` interface** + default `system_clock_source` + mock impl + threading through `EngineConfig`/`SessionConfig` | §5.1 executor model; §4.1 core; §4.4 session; §6 plugin pattern |
| **2e** | MessageStore async API | `asio::awaitable<void>` writes, QuickFIX-compat shim feasibility | §6 plugin pattern; §4.4 |
| **2f** | Awaitable mutex | `fixpp::sync::async_mutex` (six-item design list per `[SYN §3.2 Q6b]`) | §5.1; §4.1 surface |
| **2g** | TLS cert_source + pinset | Interface, file impl, FIXS rotation API | §6 plugin pattern; §4.6 |
| **2h** | Transport interface | ≤5 pure-virtual surface, ASIO TCP/TLS default impl, mock seam | §6 plugin pattern; §4.5 |
| **2i** | C ABI message rep + error enum | `fixpp_msg_t` accessors, `fixpp_error_t` ranges | §4.10; §5.3 error model |
| **2j** | Control-plane interface | `SVC-005` shape, gRPC default impl | §6 plugin pattern; §8.1 |
| **2k** | Async logger + OTel | Producer/consumer split, sink interface, `quill` vs own benchmark spike | §5.7 logging; §4.7, §4.8 |
| **2l** | Session-tap consumer API | Ring buffer default, iceoryx2 opt-in, sync-callback caveat | §4.9; §5.8 backpressure |
| **2m** | SWIG/Python binding shape | Ownership transfer, GIL handling, exception translation, async callbacks | §4.12; §5.3 error model |

Each doc must:
1. Cite the catalogue rows it covers and the constitutional articles that constrain it.
2. State which `[arch §X.y]` sections it inherits from and where it diverges (with rationale).
3. Pass Codex Gate A before `/tasks` runs `[const §XVII.1]`.
4. End with an explicit list of test seams the design exposes.

---

## 11. Open Architectural Questions

> Mirrors `SYNTHESIS.md §3` items that touch the architectural spine. Each one is owned by a downstream doc; this list is the index.

| # | Question | Owner | Disposition |
|---|---|---|---|
| 1 | Eager vs lazy offset table — measurement spike to confirm Instrument-heavy footprint | **2b** | DECIDED hybrid `[SYN §3.1 Q1]`; spike pending |
| 2 | Coroutine HALO firing on inbound dispatch path across our compiler matrix | **2d**, **2f** | Verify by spike `[SYN §3.2 Q6]` |
| 3 | QuickFIX-compat shim for synchronous `MessageStore` impls — feasible or document as known incompatibility | **2e** | Phase 2 validates `[SYN §3.2 Q7]` |
| 4 | `quill` vs own async logger — adopt or build | **2k** | Bench-driven `[SYN §3.8]` |
| 5 | ControlPlane interface shape — full surface to lock in 2j (SVC-005 row) | **2j** | Phase 2 |
| 6 | TestRequestThreshold / SendingTimeThreshold defaults | session-module spec (Phase 4) | DEFERRED `[SYN §3.2 Q10]` |
| 7 | Add catalogue row `NFR-015 — pluggable Clock interface` to `feature-catalogue.md` | **2d** (along with threading contract decisions) | TODO — added when 2d lands; tracked here so the spine doesn't claim the row exists yet |

The remaining `SYNTHESIS §3` items are decided shape-wise at `[const]` and are now operational design tasks owned by 2a–2m; they are not architectural questions.

---

## 12. Glossary

- **Awaitable mutex** — A mutex whose `lock()` returns a coroutine awaitable, so a contended waiter suspends instead of pinning the executor thread `[const §XI.3]`.
- **Flyweight** — A value-typed view over a buffer it does not own; cost is a few pointers/sizes; lifetime is bounded by the underlying buffer `[SYN §1.1, §3.1 Q2]`.
- **HALO** — Heap Allocation eLision Optimization; the compiler eliding a coroutine's heap frame when its lifetime is bounded by the caller `[const §II.4]`.
- **Offset table** — `(tag, offset, length)` index over a parsed FIX message giving O(1) random access by tag `[SYN §1.1, §3.1 Q1]`.
- **PMR** — `std::pmr` polymorphic memory resource; the standard plug-in allocator interface used pervasively in the engine `[const §VIII.5]`.
- **SecurityProfile** — Enum picked at `Session` construction: `mtls_ca` / `mtls_pinned` / `one_way_ca [[deprecated]]` `[const §XII.5]`.
- **Strand** — ASIO's `make_strand(executor)` wrapper; it serialises completion handlers, ensuring a session's callbacks never run concurrently with each other on different threads.
- **Tap** — Per-session pluggable consumer that receives every in/out FIX message verbatim; foundation for analyzers, conformance testing, audit `[SYN §1.3]` `[const §XV.15]`.
- **Trace context** — Strand-stored OTel `trace_id`/`span_id` accessed via `co_await fixpp::current_trace_context`. **Not** `thread_local` — coroutines may resume on a different thread `[const §XIII.3]`.

---

## Appendix A — Constitution → Architecture cross-reference

| Constitution article | Architecture section | What this doc operationalises |
|---|---|---|
| §I (Identity) | §1 (Goals) | Confirms in-process C++ as primary, C ABI as adjacent. |
| §II (Compilers) | §1.2, §5.1 | Non-goals lock no-`clang-cl`; HALO without compiler-version pin. |
| §III (Build) | §7 | Build trees, CMake target layout, Conan preset mapping. |
| §IV (Distribution) | §7.1, §8 | Artifact list; service uses C ABI only. |
| §V (License) | §8 | Service mode as the structural AGPL boundary. |
| §VI (Spec coverage) | §4 | Each module section enumerates catalogue rows it implements. |
| §VII (Testing) | §1.1, §6, §10 | Test seams via plugin interfaces; per-doc test-seam contract. |
| §VIII (Performance) | §5.2, §5.3 | Allocator policy on hot path; no exceptions hot path. |
| §IX (Coverage / sanitizers) | §7.5 | Sanitizer presets; coverage on Linux/Clang only. |
| §X (ABI) | §4.10, §9.2 | C ABI surface shape, decimal PoD, version macros, abidiff. |
| §XI (Concurrency) | §5.1 | Executor model; per-session strand; cancellation slots. |
| §XII (Security / TLS) | §4.6 | `cert_source`, `Pinset`, `CipherPolicy`, `SecurityProfile`. |
| §XIII (Observability) | §4.7, §4.8, §5.4, §5.7 | Async logger, OTel surface, strand-stored trace context. |
| §XIV (Plugins) | §6 | ≤5 pure-virtual rule; v1.0 pluggable list. |
| §XV (Banned patterns) | §5.8, §9.1 | Backpressure rules; no thread_local trace; no C++ headers in C ABI. |
| §XVI (Spec Kit) | §10 | Per-doc handoff requirements: clarify/analyze/Gate A before tasks. |
| §XVII (Codex gates) | §10 | Each design doc requires Gate A. |
| §XVIII (Roadmap) | §1.2 | Non-goals enumerate the v1.x deferrals. |
| §XIX (Docs) | §9.1 | Doxygen scope = public headers; detail headers excluded. |
| §XX (Amendments) | (preamble) | Conflicts trigger amendment, not silent override. |

---

## Appendix B — Normative References

> Strict reading of `[const §VI.5]` binds the Normative References requirement to `/specify` artifacts; this spine document is not a `/specify` artifact in the Spec Kit sense. The section is included here voluntarily for the same traceability spirit, and sets precedent that design docs 2a–2m do the same. Each `/specify` artifact in Phase 4 will list its own scoped subset; this appendix is the spine-level superset.
>
> Format follows `[const §VI.2]`: `[DocAbbrev §X.Y.Z] Section title`, drawn from `library/spec/coverage-index.md`.

| Spec area | Normative reference | Architectural impact |
|---|---|---|
| Framing & integrity | `[FIX-SL §3.1] Header`, `[FIX-SL §3.2] Body`, `[FIX-SL §3.3] Trailer / CheckSum` | §4.3 wire (`Parser`/`Writer`/`Framer`); W-001..W-005 |
| Session establishment | `[FIX-SL §4.3] Establishing a FIX connection` | §4.4 session FSM (`Logon` flow); S-001 |
| Session keep-alive | `[FIX-SL §4.5.1] FIX connection keep-alive (heartbeat)` | §4.4 session; S-003, S-004; §4.1 `Clock` (timing seam) |
| Recovery | `[FIX-SL §4.5.2] Message recovery`, `[FIX-SL §4.5.3] Sequence reset` | §4.4 session; S-005, S-006, S-014 |
| Session termination | `[FIX-SL §4.6] FIX connection termination` | §4.4 session; S-002 |
| Session reject | `[FIX-SL §4.7] Session-level reject` | §4.4 session; S-007, S-034 |
| MsgType advertisement | `[FIX-SL §4.3.8] Specifying supported message types` | §4.4 session; S-037 |
| System identification | `[FIX-SL §4.3.9] Identification of application system` | §4.4 session; S-038 |
| Multi-version transport | `[FIXT §5] Application-version handling`, `[FIXT §5.1] DefaultApplVerID`, `[FIXT §5.3] ApplVerID per message` | §3 version namespaces (`fixpp::v42`, `fixpp::v44`, `fixpp::v50sp2`, `fixpp::vt11`); §4.2 dictionary multi-version coexistence; S-025, S-026 |
| TLS profile | `[FIXS §3] TLS profile` | §4.6 tls (`CipherPolicy`); T-006, T-007, T-013 |
| Mutual authentication | `[FIXS §3.3] Authentication` | §4.6 tls (`SecurityProfile::mtls_*`); §4.4 session security profile arg; T-008 |
| Certificate parameters | `[FIXS §3.4] Certificate parameters` | §4.6 tls (`cert_source` validation); T-039 |
| Secrets distribution | `[FIXS §4.1] Sharing secrets` | §4.6 tls (`cert_source` interface); T-040 |
| Auth ↔ authorization | `[FIXS §4.4] Authorization linked to authentication` | §4.4 session (CompID-to-TLS-identity binding); T-041 |
| Pinning & rotation | `[FIXS §5] Certificate pinning and rotation` | §4.6 tls (`Pinset` add-then-remove); T-011 |
| Pre-shared keys | `[FIXS §6] Pre-shared key` | §4.6 tls (deferred to T-012 P2; not in v1.0 priority surface) |
| Application messages — order management | `[FIX44 §...]` / `[FIX50SP2 §...]` per-message tables | §4.2 codegen output; §4.3 wire parse/serialise; A-001..A-034 |
| Application messages — market data | `[FIX44 §...]` / `[FIX50SP2 §...]` market-data tables | §4.2 codegen; §4.3 wire; M-001..M-012 |
| Application messages — post-trade | `[FIX44 §...]` / `[FIX50SP2 §...]` post-trade tables | §4.2 codegen; §4.3 wire; P-001..P-008, C-001..C-003 |
| Reg / IOI / news / network | `[FIX44 §...]` / `[FIX50SP2 §...]` regulatory and ancillary tables | §4.2 codegen; §4.3 wire; R-001..R-005, N-001..N-003 |
| Conformance | `[FIX-TC §1]`..`[FIX-TC §20]` 20 official scenarios | §1.1 test-seam goal; §4.4 session interop suite; TC-001..TC-017 |
| FIX-Latest application | `[FIX-Latest §...]` new MsgTypes A-035..A-065 | **Out of v1.0 scope**; v1.2 per `[const §XVIII.2]`; tracked in `coverage-index.md` Post-1.0 Gap Registry |

Architectural decisions whose primary driver is engineering judgment rather than a specific spec section (the executor model, the ≤5-pure-virtual rule, the C-ABI legal isolation, the strand-stored trace context, the async-mutex discipline) cite `[const §X.y]` and `[SYN §3.x Q#]` inline at point of use; they are not spec normatives and are intentionally omitted from this appendix.
