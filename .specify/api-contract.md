# fixpp Public API Contract

> **Status:** user-signed-off v0.2 (2026-05-10) — Phase 2 Gate A converged after 2 rounds (Phase A, no resets). Round-2 verdict `ship-as-is` (0 P1, 0 P2, 1 P3 wording-precision in convergence-log meta-text). See `decisions/api-contract.md`.
> **Authority:** This document is the consolidated public-surface contract for `fixpp`. It is purely a **distillation** — every rule, name, header, target, macro, and numeric block stated here is sourced from `constitution.md`, `architecture.md`, or one of `2a`–`2m`. No new decisions are introduced; this document does not amend its sources, and on any conflict the source wins (constitution > architecture > 2a–2m design docs).
> **Citation form:** other documents cite this contract as `[api §N.m]`. This document cites the constitution as `[const §X.y]`, the architecture as `[arch §N.m]`, and design docs as `[2X §N.m]`.
> **Frozen rule:** every surface marked **Stable from v1.0** in §3 is frozen at v1.0 release. Subsequent breaking changes require a phase-gate event: a constitutional amendment under `[const §XX]` plus the matching SemVer MAJOR bump on either the library track or the C ABI track per §4.

---

## Table of Contents

1. [Scope](#1-scope)
2. [Stability tiers](#2-stability-tiers)
3. [Surfaces by tier](#3-surfaces-by-tier)
4. [Versioning](#4-versioning)
5. [Public C++ namespaces](#5-public-c-namespaces)
6. [Public header layout](#6-public-header-layout)
7. [C ABI surface](#7-c-abi-surface)
8. [CMake exported targets](#8-cmake-exported-targets)
9. [Pluggable interface roster](#9-pluggable-interface-roster)
10. [Per-doc surface index](#10-per-doc-surface-index)
11. [Frozen-until rule](#11-frozen-until-rule)
12. [Source map](#12-source-map)
13. [Appendix C — Convergence log](#appendix-c--convergence-log)

---

## 1. Scope

This contract covers the **v1.0 public surface** of `fixpp` as locked by Phase 2:

- C++ public API across the modules listed in `[arch §2.1]` (`core`, `dictionary`, `wire`, `session`, `transport`, `tls`, `log`, `otel`, `tap`, `service` interfaces, `bindings/python`).
- The C ABI under `<fix/c_api.h>` and its split sub-headers, including the `fixpp_error_t` numeric layout.
- The CMake target graph that distinguishes C++ consumers from C-ABI-only consumers.
- The versioning macros and SemVer policy for the library and the C ABI.
- The pluggable interface roster (≤5 pure-virtual per `[const §XIV.2]` and the documented `Application` exception).

**Out of scope:** internal (`fixpp::detail`, `<module>/detail/`) symbols; build-tree-only generated code; Phase 3 tooling (CMake presets, Conan profiles); Phase 4 `/specify` per-feature artifacts.

---

## 2. Stability tiers

Three tiers per `[arch §9.3]`:

- **Stable from v1.0** — frozen by this contract; breaking change requires constitutional amendment + MAJOR SemVer bump.
- **Provisional** — may change in patch releases without notice during early v1.x; **explicitly enumerated** in §3.2 below.
- **Internal** — anything in `fixpp::detail` or under `<module>/detail/`; no stability guarantee. Headers carry `\internal` for Doxygen and are excluded from the install set per `[arch §9.1]`.

**No transitive C++ leaks across the C ABI** per `[arch §9.1]`: `<fix/c_api.h>` includes only `<stddef.h>`, `<stdint.h>`, `<stdbool.h>`. Verified by CI grep.

---

## 3. Surfaces by tier

### 3.1 Stable from v1.0

- The C++ public API enumerated by namespace in §5 (every row marked Stable from v1.0).
- The C ABI (`<fix/c_api.h>` + the split sub-headers enumerated in §6).
- Each pluggable interface enumerated in §9 (`Transport`, `TlsTransport`, `Listener`, `TransportFactory`, `cert_source`, `MessageStore`, `MessageStoreFactory`, `Sink`, `ControlPlane`, `ControlPlaneFactory`, `Clock`, plus the `Application` callback interface) — individually Stable from v1.0; `TapConsumer` is the closed `std::variant` per §9 trailer.
- The gRPC control-plane proto schema (`service/proto/fixpp_control.proto`) per `[arch §9.3]` / `[2j §4.7]`.

### 3.2 Provisional (early v1.x)

Per `[arch §9.3]`:

- The iceoryx2 topic shape used by `Iox2Tap` / `fixppd` per `[2l §6]`.
- The `SecurityProfile::one_way_ca` deprecation timing per `[const §XII.5]`.
- The in-process logger's choice of implementation (`quill` vs in-house) per `[2k §6.1]` / `[arch §11]` Q4 — pending the bench-driven decision (no upstream TS-* identifier; see `[arch §11]` Q4 disposition "Bench-driven `[SYN §3.8]`").

### 3.3 Internal

- `fixpp::detail::*` and any `include/fixpp/<module>/detail/` headers.

---

## 4. Versioning

Two **independent** SemVer tracks per `[const §X.1]` / `[arch §9.2]`:

| Track | Macros | Bumps when |
|---|---|---|
| **Library** (C++ surface) | `FIXPP_VERSION_MAJOR/MINOR/PATCH` | Any breaking change to a tier-1 C++ symbol or removal of a Stable-from-v1.0 surface. |
| **C ABI** | `FIXPP_C_ABI_VERSION_MAJOR/MINOR/PATCH` | Any breaking change to a published C-ABI symbol; numeric meaning of any `fixpp_error_t` value changes. |

- Both macro families are emitted by `tools/cmake/version.cmake` per `[arch §9.2]`.
- ABI compatibility is verified in Tier 2 CI: `abidiff` on Linux, structural diff on Windows, against the previous tagged release per `[const §IX.5]` / `[arch §9.2]`.
- The C ABI may stay at MAJOR=1 across multiple library MAJOR bumps if the C surface remains compatible — the two tracks exist precisely to allow that.
- Runtime version accessor: `fixpp_version()` per `[2i]`.

---

## 5. Public C++ namespaces

Per `[arch §3]`. All Stable from v1.0 unless noted.

| Namespace | Purpose | Source |
|---|---|---|
| `fixpp` | Top-level identity and version constants. Code does not live here directly. | `[arch §3]` |
| `fixpp::core` | Primitives: `error`, `expected_t<T>`, `span`, `time_point`, PMR, `decimal<T>`, `decimal_traits<T>`, `Clock`, `session_executor`. | `[arch §4.1]`, `[2a]`, `[2d]` |
| `fixpp::sync` | Awaitable synchronisation: `async_mutex`. | `[arch §4.1]`, `[2f]` |
| `fixpp::dict` | Runtime dictionary, XML loader, dialect overlay, metadata accessors. | `[arch §4.2]`, `[2c]` |
| `fixpp::v42` / `fixpp::v44` / `fixpp::v50sp2` / `fixpp::vt11` | Generated typed messages and `constexpr` field metadata, version-namespaced. | `[const §I.1]`, `[arch §4.2]`, `[2c]` |
| `fixpp::wire` | `Parser`, `Writer`, `OffsetTable`, `Validator`, `Framer`. | `[arch §4.3]`, `[2b]` |
| `fixpp::session` | `Session`, `Application` callback interface, `MessageStore`, `SessionConfig`, `SecurityProfile`, `RejectPolicy`, `LockPolicy`, `Listener`. | `[arch §4.4]`, `[2d]`, `[2e]` |
| `fixpp::transport` | `Transport` interface, `TlsTransport` sub-interface, `Listener`, `TransportFactory`, `Endpoint`, `ReconnectPolicy`, ASIO TCP/TLS default impl. | `[arch §4.5]`, `[2h]` |
| `fixpp::tls` | `cert_source`, `Pinset`, `CipherPolicy`, file-backed default impl. | `[arch §4.6]`, `[2g]` |
| `fixpp::log` | `Logger`, `Sink` interface, `Record`, `Level`, `LogConfig`. Default sinks: `FileSink`, `OtlpLogSink`, `SyslogSink`. | `[arch §4.7]`, `[2k]` |
| `fixpp::otel` | `TracerProvider`, `MeterProvider`, span helpers, exporter setup. | `[arch §4.8]`, `[2k]` |
| `fixpp::tap` | `NoTap`, `RingBufferTap`, `Iox2Tap`, `SyncCallbackTap`, `TapConfig`, `TapRecord`, `TapShmRecord`, `TapConsumer = std::variant<...>`. | `[arch §4.9]`, `[2l]` |
| `fixpp::service` | Plugin interfaces only: `ControlPlane`, `ControlPlaneConfig`, `ControlPlaneFactory`. Default impl (`grpc_control_plane`) lives downstream of the C ABI. | `[arch §4.11]`, `[2j]` |
| `fixpp::current_trace_context` (free awaitable) | Returns the current `trace_context` from a strand-stored slot. **Not** `thread_local`. | `[const §XIII.3]`, `[2d]`, `[2k]` |
| `fixpp::detail` | Internal-only. Never user-callable. | `[arch §9.1]` |

---

## 6. Public header layout

Per `[arch §7.3]`:

```
include/
├── fix/
│   ├── c_api.h              # the ONE C-ABI entry header
│   └── c_api/               # split sub-headers (per `[2i §4.1]` lines 256–298)
│       ├── error.h          # 2i — fixpp_error_t enum, FIXPP_ERR_*
│       ├── version.h        # 2i — FIXPP_C_ABI_VERSION_*, fixpp_version()
│       ├── message.h        # 2i — fixpp_msg_t accessors / setters / clone
│       ├── engine.h         # 2j-owned (shape per 2i; semantics per 2j) — fixpp_engine_t lifecycle
│       ├── session.h        # 2j-owned (shape per 2i; semantics per 2j + Phase-4) — fixpp_session_t lifecycle
│       ├── dict.h           # 2c-owned (consumes 2i plumbing rules) — fixpp_dict_t accessors
│       ├── store.h          # 2e-owned — fixpp_store_t opaque type
│       ├── decimal.h        # 2a-owned — fixpp_decimal_parse / _format / _compare / _equal / _init per `[2a §5.2]`; 2i references but does not redefine per `[2i §3.8]`
│       ├── export.h         # 2i-owned — FIXPP_API_EXPORT visibility macros per `[2i §3]` lines 300–308
│       ├── log.h            # 2k — placeholder in v1.0 (no `extern "C"` symbols) per `[2k §5]` line 84 / lines 1062–1078
│       └── otel.h           # 2k — placeholder in v1.0 (no `extern "C"` symbols) per `[2k §5]` line 84 / lines 1062–1078
└── fixpp/
    ├── core/                # primitives, Clock, decimal<T>
    ├── sync/                # async_mutex
    ├── dict/                # runtime dict, XML loader, dialect overlay
    ├── wire/                # parser/writer/offset-table
    ├── transport/           # interface + ASIO impl
    ├── tls/                 # cert_source, Pinset, CipherPolicy
    ├── session/             # Session, Application, MessageStore, SessionConfig
    ├── log/                 # Logger, Sink
    ├── otel/                # TracerProvider, MeterProvider
    ├── tap/                 # NoTap / RingBufferTap / Iox2Tap / SyncCallbackTap
    ├── service/             # ControlPlane / ControlPlaneConfig / ControlPlaneFactory (interfaces only)
    └── v42/, v44/, v50sp2/, vt11/   # generated typed messages (build tree)
```

Detail headers (`include/fixpp/<module>/detail/`) are excluded from the install set and from Doxygen per `[arch §9.1]`.

---

## 7. C ABI surface

### 7.1 Entry header invariant

`<fix/c_api.h>` includes only `<stddef.h>`, `<stdint.h>`, `<stdbool.h>` per `[arch §9.1]` and `[2i]`. CI grep enforces.

### 7.2 Opaque handles

Per `[2i]`: `fixpp_engine_t`, `fixpp_session_t`, `fixpp_msg_t`, `fixpp_dict_t`, `fixpp_store_t`. All pointer-like; lifetime owned by the create/destroy pair documented per symbol.

### 7.3 Decimal at the boundary

Per `[const §X.3]` / `[2a §5.1]`: `fixpp_decimal_t` is a 16-byte / 8-byte-aligned PoD `(int64 mantissa, int8 exponent)` with `INT64_MIN` mantissa as the invalid sentinel and 7 trailing `_reserved` bytes ignored on read in v1.0 (forward-compat slot under future `FIXPP_C_ABI_DECIMAL_RESERVED_USED`). Helpers per `[2a §5.2]` lines 252–274: macros `FIXPP_DECIMAL_INITIALIZER`, `FIXPP_DECIMAL_INVALID`; functions `fixpp_decimal_parse`, `fixpp_decimal_format`, `fixpp_decimal_compare`, `fixpp_decimal_equal`, `fixpp_decimal_init`. The build-time engine-wide default type is selected via the `FIXPP_DECIMAL_T` macro, and a mismatch is **link-fatal** via `decimal_alias_sentinel<T>::tag` per `[2a]`.

### 7.4 `fixpp_error_t` numeric block layout

Per `[2i §4.3]`. The numeric value of any **published** variant is frozen for life of `FIXPP_C_ABI_VERSION_MAJOR == 1`; new variants append at unused slots; an audit trail at `tools/abi_history/error_codes_v1.txt` is checked-in append-only and verified by `tools/check_capi_occupancy.sh` + `abidiff` per `[const §IX.5]`.

`fixpp_error_t` codes are coalesced remediation-class groups; they are not a 1:1 listing of `fixpp::core::error` variants. Per-doc internal-variant counts live in each `[2X §6.X]` errors section and in `[2i §1.1]` magnitude-domain table; the coalescing assignment is ratified in `[2i §4.3]`. The "Published in v1.0" column below counts published numeric slots in `c_api/error.h` (`#define FIXPP_ERR_* ((fixpp_error_t) N)`), transcribed verbatim from `[2i §4.3]` lines 500–588.

| Block | Range | Owner | Published in v1.0 |
|---|---|---|---|
| `FIXPP_ERR_*` cross-cutting | `[0, 99]` | 2i | 11 (3 architectural sentinels per `[arch §5.3]`: `OK=0`, `CANCELLED=1`, `UNKNOWN=2`; 8 2i-introduced: `NULL_HANDLE=3`, `INVALID_HANDLE=4`, `VERSION_MISMATCH=5`, `BUFFER_TOO_SMALL=6`, `TYPE_MISMATCH=7`, `TAG_NOT_FOUND=8`, `INDEX_OUT_OF_RANGE=9`, `CAPI_CONFIG_INVALID=10`); `[11, 99]` reserved for cross-cutting growth |
| `FIXPP_ERR_WIRE_*` | `[100, 199]` | 2b | 3 (`INVALID_FRAME=100`, `LIMIT_EXCEEDED=101`, `CONFORMANCE=102`) |
| `FIXPP_ERR_DICT_*` | `[200, 299]` | 2c | 3 (`CONFIG=200`, `LIMIT_EXCEEDED=201`, `OOM=202`) |
| `FIXPP_ERR_THREAD_*` | `[300, 399]` | 2d | 3 (`CONFIG=300`, `SESSION_LIFECYCLE=301`, `RUNTIME=302`) |
| `FIXPP_ERR_STORE_*` | `[400, 499]` | 2e | 4 (`RUNTIME=400`, `CONSISTENCY=401`, `CONFIG=402`, `VISITOR=403`) |
| `FIXPP_ERR_SYNC_*` | `[500, 599]` | 2f | 1 (`RUNTIME=500`; `sync_lock_aborted` coalesces to `FIXPP_ERR_CANCELLED`) |
| `FIXPP_ERR_TLS_*` | `[600, 699]` | 2g | 4 (`CONFIG=600`, `HANDSHAKE=601`, `PINSET=602`, `RUNTIME=603`) |
| `FIXPP_ERR_TRANSPORT_*` | `[700, 799]` | 2h | 4 (`LIFECYCLE=700`, `IO=701`, `HANDSHAKE=702`, `CONFIG=703`) |
| `FIXPP_ERR_DECIMAL_*` | `[800, 899]` | 2a | 2 (`INVALID=800`, `PRECISION_LOSS=801`); 2a's `decimal_buffer_too_small` reuses `FIXPP_ERR_BUFFER_TOO_SMALL=6` from the cross-cutting block |
| `FIXPP_ERR_CTRL_*` | `[900, 999]` | 2j | 2 (`CONFIG=900`, `RUNTIME=901`); cancellation triple coalesces to `FIXPP_ERR_CANCELLED` |
| (reserved for 2k) | `[1000, 1099]` | 2k | 0 published in v1.0 (`c_api/log.h` and `c_api/otel.h` are placeholders per `[2k §5]` line 84); 7 internal `fixpp::core::error` variants pinned for v1.x publication per `[2k §6.3]` |
| (reserved for 2l) | `[1100, 1199]` | 2l | 0 published in v1.0; 4 internal variants pinned for v1.x publication per `[2l §6.7]` (`tap_ring_overflow=1100`, `tap_iox2_not_running=1101`, `tap_invalid_config=1102`, `tap_arena_exhausted=1103`) |
| `FIXPP_ERR_BINDING_*` | `[1200, 1299]` | 2m | 5 (`PYTHON_CALLBACK_RAISED=1200`, `SUBINTERPRETER=1201`, `OBJECT_LIFETIME=1202`, `WHEEL_ABI_MISMATCH=1203`, `CALLBACK_REENTRANT_CLOSE=1204`) |
| (reserved post-v1.x) | `[1300, 1399]` | reserved | 0 |
| (reserved future) | `[1400+]` | reserved | 0 |

**Cancellation translation rule** per `[2i §4.9]` (binding section) / `[const §XI.2]` (upstream cancellation primitive): every `*_aborted` / `*_cancelled` from sibling docs maps to `FIXPP_ERR_CANCELLED = 1` at the C ABI; the precedent is `[2f §6.5]`.

**Forward-compat** per `[arch §5.3]` / `[const §X.4]` / `[2i §4.4]`–`[2i §4.5]`: TO-consumer out-of-range mapped to `FIXPP_ERR_UNKNOWN`; FROM-consumer opaque pass-through tolerance (not actively rejected; consumer-version stamp recorded for diagnostics).

### 7.5 Exception trap split

Per `[2i §5.2]` lines 1228–1314: construction-vs-steady-state split. `guarded_call_construction` whitelists exactly three v1.0 entry points — `fixpp_engine_create`, `fixpp_dict_load_from_xml`, `fixpp_msg_create_outbound` — where a C++ exception is trapped and translated to a domain-appropriate `FIXPP_ERR_*_CONFIG` (or `FIXPP_ERR_CAPI_CONFIG_INVALID` for engine creation). `guarded_call_steady` is `std::abort` per `[arch §5.3]` invariant-violation rule (the no-throw hot path itself is `[const §VIII.5]`; the abort response is architectural, not constitutional). The whitelist is v1.0-exact; sourced from `[2i §5.2]`.

### 7.6 Reentrancy annotation

Per `[const §X.5]` / `[2i]`: every C-ABI symbol is annotated **thread-safe** / **single-thread** / **requires-session-lock** in its header comment. Grep-CI verifies the annotation exists.

---

## 8. CMake exported targets

Per `[arch §7.4]`:

| Target | Type | Purpose | Stability |
|---|---|---|---|
| `fixpp::core` … `fixpp::tap` | `OBJECT` | One per module; combined into the umbrella shared/static. | Internal contract; not redistributed standalone. |
| `fixpp::capi-objects` | `OBJECT` | `extern "C"` translation units. | Internal; combined into the umbrella. |
| `fixpp` | Shared/Static (umbrella) | **C++ public consumer target.** `INTERFACE_INCLUDE_DIRECTORIES = include/`. Brings in both `<fix/c_api.h>` and the entire `<fixpp/...>` C++ surface. Selected via `find_package(fixpp) COMPONENTS Cxx`. | Stable from v1.0 |
| `fixpp::capi` | Shared/Static (consumer target) | **C-ABI consumer target.** Same underlying shared library as `fixpp` (no duplicated object code), but `INTERFACE_INCLUDE_DIRECTORIES = include/fix/` only — C++ headers are not on its include path. | Stable from v1.0 |
| `fixpp::service-iface` | `INTERFACE` (header-only) | Public plugin interfaces under `include/fixpp/service/`. Depends transitively on `fixpp::core` headers it needs. | Stable from v1.0 |
| `fixpp-codegen` | Host tool | Codegen from `dictionaries/FIXxx.xml`. **Not exported.** | Internal |
| `fixppd` | Executable | Daemon. Depends on `fixpp::capi` + `fixpp::service-iface` (and external gRPC, iceoryx2). **Does not depend on `fixpp` (the C++ umbrella) or any of the C++ engine `OBJECT` libraries** — reaches the engine exclusively through `extern "C"` symbols. | Stable from v1.0 |
| `fixpp-python` | SWIG | Python wheel. Depends only on `fixpp::capi` and the SWIG-generated wrapper. | Stable from v1.0 |

**Boundary backstop** per `[arch §7.4]` / `[arch §8]` — *corrected by 086/FR-014, which requires that no statement about what `tools/check_layers.py` enforces remain untrue. This paragraph previously made two claims the script cannot support, and leaving it would have put this document in direct contradiction with `architecture.md` §8:*

- **A target linking both `fixpp::capi` and `fixpp` is rejected by convention and review, NOT by a lint.** `tools/check_layers.py` reads no CMake and no link interface — it is a source `#include`-edge lint over **`src/**` and `bindings/**`** (`tools/check_layers.py:2-7`, `:173-176`). Nothing mechanically detects the link combination, in-tree or installed; an installed CMake package cannot observe which targets a consumer links together.
- **The lint does not scan `service/`.** Its glob list is `src/**` and `bindings/**` only. The engine-internal `#include` rule for `service/` sources rests on convention plus review.

**What IS mechanical**, for consumers of the installed package: 086's include isolation. `fixpp::capi` exposes `${_IMPORT_PREFIX}/include/capi` and `fixpp::service` exposes `${_IMPORT_PREFIX}/include/service-iface`, so `#include <fixpp/wire/...>` does not resolve from either — asserted by three configure-time `try_compile` calls in `tests/consumer/`, each demonstrated able to fail.

---

## 9. Pluggable interface roster

Discipline: **≤5 pure-virtual methods per interface** per `[const §XIV.2]`, with a single Gate-A-justified exception (`Application` at 6 — see below).

| Interface | Module | Pure-virtuals | Default impl(s) | Source |
|---|---|---|---|---|
| `Transport` | transport | 5 (`async_connect`, `async_read_some`, `async_write`, `cancel`, `close`) — at cap | `asio_tls_transport` | `[2h]` |
| `TlsTransport` (sub-interface) | transport | 1 (`async_handshake`) | (ASIO impl) | `[2h]` |
| `Listener` | transport | 1 (`async_accept`) | (ASIO impl) | `[2h]` |
| `TransportFactory` | transport | 1 (`make`) | (ASIO factory) | `[2h]` |
| `cert_source` | tls | 2 (`load_credentials`, `load_trust_anchors`) | `file_cert_source` | `[2g]` |
| `MessageStore` | session | 4 (`store`, `retrieve`, `next_seqnum`, `reset` — all `awaitable<expected_t<...>>`) | `MemoryStore`, `FileStore` | `[2e]` |
| `MessageStoreFactory` | session | 1 (`make`) | (factory) | `[2e]` |
| `Sink` | log | 4 (`open`, `emit`, `flush`, `close`) | `FileSink`, `OtlpLogSink`, `SyslogSink` | `[2k]` |
| `ControlPlane` | service | 3 (`start`, `stop`, `health`); 2 slots reserved for v1.x `RotateAuthToken` / `RemapRpcs` | `grpc_control_plane` | `[2j]` |
| `ControlPlaneFactory` | service | 1 (`make`) | (factory) | `[2j]` |
| `Clock` | core | 4 (`now`, `steady_now`, `sleep_until`, `cancel_sleeps`) | `system_clock_source` | `[2d]` |
| `Application` (callback) | session | **6** (`onLogon`, `onLogout`, `toAdmin`, `fromAdmin`, `toApp`, `fromApp`) | (user-implemented) | `[arch §6]` (Gate-A-justified exception over `[const §XIV.2]`) |

**`TapConsumer` is a closed `std::variant` (not a virtual interface)** per `[2l]` / `[arch §4.9]` — `std::variant<NoTap, RingBufferTap, Iox2Tap, SyncCallbackTap>` — to keep the hot path free of virtual dispatch.

---

## 10. Per-doc surface index

Each row links the design doc to its public-surface footprint. **Source of truth lives in the design doc;** this table is a navigation aid.

| Doc | Subsystem | Public types | C ABI block | Catalogue rows | Convergence story |
|---|---|---|---|---|---|
| 2a | Decimal type + `decimal_traits<T>` | `fixpp::core::decimal<T>`, `pod_decimal`, `decimal_traits<T>`; C ABI `fixpp_decimal_t`, `FIXPP_DECIMAL_T`, `FIXPP_DECIMAL_INITIALIZER`, `FIXPP_DECIMAL_INVALID`, `fixpp_decimal_parse`, `fixpp_decimal_format`, `fixpp_decimal_compare`, `fixpp_decimal_equal`, `fixpp_decimal_init` | `[800, 899]` (2 published in v1.0; cross-cutting `FIXPP_ERR_BUFFER_TOO_SMALL=6` reused) | W-009 | [`decisions/2a-decimal.md`](../../decisions/2a-decimal.md) |
| 2b | Wire parser + offset-table | `fixpp::wire::Parser`, `OffsetTable`, `Writer`, `Validator`, `Framer`; lifetime contract via `[[clang::lifetimebound]]` | `[100, 199]` (3 published in v1.0) | W-001..W-014, OSS-006/008/013, parse/serialise/validate behaviour for A-001..A-034, M-001..M-012, P-001..P-008, C-001..C-003, R-001..R-005, N-001..N-003 | [`decisions/2b-wire.md`](../../decisions/2b-wire.md) |
| 2c | Dictionary codegen + dialect overlay | `fixpp::dict::Dictionary`, `XmlLoader`, `DialectOverlay`, `ComponentRef`, `FieldRef`, `GroupRef`; generated `fixpp::v42::*` / `v44::*` / `v50sp2::*` / `vt11::*` | `[200, 299]` (3 published in v1.0) | D-001..D-011, OSS-001/010; typed-message classes for A-001..A-013 + M/P/C/R/N (codegen scope) | [`decisions/2c-codegen.md`](../../decisions/2c-codegen.md) |
| 2d | Application threading + `Clock` | `fixpp::core::Clock` (+ `system_clock_source`, mock); `fixpp::core::session_executor`; `fixpp::current_trace_context`; default per-session strand on user-supplied `asio::any_io_executor` | `[300, 399]` (3 published in v1.0) | NFR-015 (NEW); cross-cuts S-035, S-003/S-004, LOG-001..004, OBS-001..003 | [`decisions/2d-threading.md`](../../decisions/2d-threading.md) |
| 2e | MessageStore async API + QuickFIX-shim | `fixpp::session::MessageStore` (4 PV awaitable surface); `MemoryStore`, `FileStore`; `MessageStoreFactory` | `[400, 499]` (4 published in v1.0) | S-011/012/013/014, OSS-002, COM-009 | [`decisions/2e-msgstore.md`](../../decisions/2e-msgstore.md) |
| 2f | Awaitable mutex `async_mutex` | `fixpp::sync::async_mutex` (≤56 B padded; `cancel_and_drain()`); `async_lock(mr)` PMR overload; session helper `async_lock_via_session_executor` | `[500, 599]` (1 published in v1.0; `sync_lock_aborted` coalesces to `FIXPP_ERR_CANCELLED`) | NFR-016 (NEW) | [`decisions/2f-async-mutex.md`](../../decisions/2f-async-mutex.md) |
| 2g | TLS `cert_source` + Pinset rotation | `fixpp::tls::cert_source` (2 PV); `credentials` value-type with optional HSM `sign_callback`; `file_cert_source`; `Pinset` add-then-remove with `pin_snapshot`; `consteval CipherPolicy` + `is_allowed()` runtime accessor; `SecurityProfile` enum | `[600, 699]` (4 published in v1.0) | T-006/007/008/011/013; T-039/040 split with 2h | [`decisions/2g-tls.md`](../../decisions/2g-tls.md) |
| 2h | Transport interface + ASIO TCP/TLS | `fixpp::transport::Transport` (5 PV at cap); `TlsTransport` sub-interface (`async_handshake`); `Listener`; `TransportFactory`; `Endpoint`; `ReconnectPolicy::defaults()`; value-typed `handshake_result`; `asio_tls_transport` default | `[700, 799]` (4 published in v1.0) | T-001..T-005, T-009/010/012; T-039/040 split with 2g | [`decisions/2h-transport.md`](../../decisions/2h-transport.md) |
| 2i | C ABI message rep + `fixpp_error_t` | Opaque handles (`fixpp_engine_t`, `fixpp_session_t`, `fixpp_msg_t`, `fixpp_dict_t`, `fixpp_store_t`); `fixpp_msg_get_*` / `_set_*`; `fixpp_msg_clone`; `fixpp_strerror`; `fixpp_version`; `fixpp_error_t` numeric layout (§7.4); split `<fix/c_api/*.h>` headers | `[0, 99]` (11 published in v1.0; owner of the layout itself) | CA-001..CA-010 | [`decisions/2i-capi.md`](../../decisions/2i-capi.md) |
| 2j | Control-plane interface + gRPC default | `fixpp::service::ControlPlane` (3 PV + 2 reserved); `ControlPlaneConfig`; `ControlPlaneFactory`; `grpc_control_plane` over UDS / named pipe; proto schema `service/proto/fixpp_control.proto` (7 RPCs) | `[900, 999]` (2 published in v1.0: `CTRL_CONFIG=900`, `CTRL_RUNTIME=901`) | SVC-001 (sole), SVC-004 (sole), SVC-005 (NEW pluggable interface) | [`decisions/2j-controlplane.md`](../../decisions/2j-controlplane.md) |
| 2k | Async logger + OTel observability | `fixpp::log::Logger` (zero-alloc producer; bounded MPSC; drain thread); `Sink` (4 PV); `Record`; `Level`; `LogConfig`; default sinks `FileSink`, `OtlpLogSink`, `SyslogSink`; `fixpp::otel::TracerProvider` / `MeterProvider`; `SessionSpans`; macros `FIXPP_SLOG`, `FIXPP_ELOG`, `FIXPP_LOG0`; `Session::get_trace_context() const noexcept [[nodiscard, clang::lifetimebound]]` | `[1000, 1099]` 2k-owned; 0 published in v1.0 (`c_api/log.h` / `c_api/otel.h` placeholders per `[2k §5]` line 84); 7 internal `fixpp::core::error` variants pinned for v1.x publication per `[2k §6.3]` | LOG-001..004 (sole), OBS-001..003 (sole) | [`decisions/2k-log-otel.md`](../../decisions/2k-log-otel.md) |
| 2l | Session-tap consumer | `fixpp::tap::TapConsumer = std::variant<NoTap, RingBufferTap, Iox2Tap, SyncCallbackTap>`; `TapConfig`; `TapRecord`; `TapShmRecord` (iceoryx2 cross-process shape — Provisional per §3.2); iceoryx2 service-name format `fixpp/<beginstring>/<sender>/<target>[/<qualifier>]` | `[1100, 1199]` 2l-owned; 0 published in v1.0 (block reserved); 4 variants pinned internally for v1.x publication per `[2l §6.7]` (`tap_ring_overflow=1100`, `tap_iox2_not_running=1101`, `tap_invalid_config=1102`, `tap_arena_exhausted=1103`) | SVC-002, SVC-003 (cross-cut); S-036, NFR-014, COM-008 | [`decisions/2l-tap.md`](../../decisions/2l-tap.md) |
| 2m | SWIG/Python binding shape | `fixpp` Python module (single abi3 (stable-ABI) wheel covering CPython 3.10–3.13+ (cp310 floor) per `[2m §1.1]` / `[arch §7.1]`; single-interpreter); `fixpp.FixppError` block-mapped exception hierarchy; opaque-handle Python wrappers reject `__reduce_ex__`; manylinux 2_28 wheel via `cibuildwheel` + `auditwheel repair` | `[1200, 1299]` (5 published in v1.0) | PY-001..PY-005 | [`decisions/2m-pybind.md`](../../decisions/2m-pybind.md) |

---

## 11. Frozen-until rule

A surface listed under §3.1 is **frozen** at v1.0 release. Any change with one of the following effects is a breaking change requiring (a) a constitutional amendment under `[const §XX]` and (b) a SemVer MAJOR bump on the affected track per §4:

- Renaming, removing, or changing the signature of a Stable-from-v1.0 C++ symbol.
- Renaming, removing, or changing the meaning of a Stable-from-v1.0 C-ABI symbol.
- Reassigning the numeric value of any published `fixpp_error_t` variant.
- Reordering, removing, or repurposing a CMake exported target listed in §8.
- Tightening the include set of `<fix/c_api.h>` (anything beyond `<stddef.h>`, `<stdint.h>`, `<stdbool.h>` would already be a violation of `[arch §9.1]`).
- Adding a pure-virtual method to any interface in §9 (would invalidate user implementations).

Non-breaking changes that **do not** require an amendment:

- Appending a new `fixpp_error_t` variant in an unused slot (recorded in `tools/abi_history/error_codes_v1.txt`).
- Adding a new public symbol that is opt-in and does not alter existing surfaces.
- Adding a new sub-header under `<fixpp/<module>/...>` for a new module that respects the layering rules in `[arch §2]` *and* whose allowed-include edges are added to `[arch §2.3]` (note: edits to `[arch §2.3]` themselves trigger `[const §XX]` amendment).
- Promoting a Provisional surface (§3.2) to Stable, after a normal review.

Provisional surfaces (§3.2) may change in patch releases without an amendment, **but** the change must be recorded in the release notes and must not invalidate the iceoryx2 topic-name format for `fixppd`-bound consumers without a documented migration path.

---

## 12. Source map

Every claim in this document is sourced from one or more of:

- `library/.specify/constitution.md` — Articles I (identity), II (compilers), III (build), V (license), VI (FIX coverage), VIII (ABI), IX (CI/quality), X (C ABI), XI (concurrency), XII (security), XIII (observability), XIV (interface discipline), XV (banned patterns), XVII (workflow), XX (amendment).
- `library/.specify/architecture.md` — §2 (modules), §3 (namespaces), §4 (per-module surface), §5 (cross-cutting), §6 (plugins), §7 (build/targets), §8 (service boundary), §9 (header discipline + versioning), §10 (hand-off table), Appendix B (Normative References).
- Each `library/.specify/2X-*.md` design doc and its convergence story under `decisions/2X-*.md`.

For any conflict, the source wins: `constitution.md` > `architecture.md` > `2X` design docs > this contract.

---

## Appendix C — Convergence log

### v0.1 → v0.2 (Gate A round 1, Phase A, 2026-05-10)

Combined post-judging tally: P1 = 6, P2 = 5, P3 = 3 across 3 root causes. Closing recommendation from `opus_api-contract_adversarial_review.md`: "v0.2 can ship after a single convergence pass." No full-rewrite signal; surgical edits only.

#### Root causes addressed

- **Root cause #1 — §7.4 / §10 confused two distinct quantities (internal `fixpp::core::error` variants vs published coalesced `fixpp_error_t` codes).** Re-tabulated §7.4 verbatim from the live `[2i §4.3]` published `c_api/error.h` excerpt (lines 500–588), under the published-coalesced definition (1–5 codes per block). Added a pre-table sentence clarifying coalesced-vs-variant. Propagated corrected occupancies into every §10 row's "C ABI block" parenthetical (with explicit "published in v1.0" framing). Touched: §7.4 (entire table + pre-table sentence + cancellation-translation cite + forward-compat cites), §10 rows 2a / 2b / 2c / 2d / 2e / 2f / 2g / 2h / 2i / 2j / 2k / 2l / 2m.
- **Root cause #2 — §7.5 and parts of §7.4 used convergence-story summaries instead of live spec text.** Replaced §7.5 whitelist with the live `[2i §5.2]` line 1230 list (`fixpp_engine_create`, `fixpp_dict_load_from_xml`, `fixpp_msg_create_outbound`); fixed cite from `[2i §4]` → `[2i §5.2]`; fixed steady-state-abort cite from `[const §VIII.5]` (no-throw hot path) → `[arch §5.3]` (invariant-violation rule); fixed §7.4 row-1 architectural-sentinel names (`OK`/`CANCELLED`/`UNKNOWN`, not `SUCCESS`/`UNKNOWN`/`VERSION_MISMATCH`) per `[2i §4.3]` lines 501–511. Touched: §7.5 (full rewrite), §7.4 row 1.
- **Root cause #3 — §3.1 / §10 surface roster softened constraints.** Reorganised §3.1 to cross-reference §9 and §5/§6 instead of re-enumerating (covers `Clock`); §10 row 2m now quotes `[2m §1]`/`[arch §7.1]` verbatim on the `cp310-only` mandatory wheel constraint with `3.11/3.12/3.13` best-effort. Touched: §3.1 (full rewrite), §3.2 (TS-13 wording), §10 row 2m. *(Audit note — superseded by the abi3 pivot: see PY-005 / `[arch §7.1]` amendment. Row 2m now names the single `cp310-abi3` wheel covering CPython 3.10–3.13+; this quote records the prior per-version resolution and is preserved unrewritten.)*

#### Per-finding resolution (Codex pass)

| # | Severity | Title | Resolution |
|---|---|---|---|
| 1 | P1 | `fixpp_error_t` numeric-block table fabricates occupancy + identifiers | Closed by Root cause #1: §7.4 re-tabulated from `[2i §4.3]`; per-block names + counts now match `c_api/error.h` verbatim. |
| 2 | P1 | `guarded_call_construction` whitelist contradicts 2i | Closed by Root cause #2: §7.5 whitelist replaced with live `[2i §5.2]` list; cite updated; abort cite repointed to `[arch §5.3]`. |
| 3 | P3 | Clarify "occupancy" vs "variants" for errors | Closed by Root cause #1's pre-table sentence (escalated to P1 framing per Opus judging). |

#### Per-finding resolution (Opus-new)

| # | Severity | Title | Resolution |
|---|---|---|---|
| N-P1-1 | P1 | `c_api/decimal.h` and `c_api/export.h` missing from §6 header layout | Added both rows to §6 with the counter-proposal comments and `[2i §3]` / `[2i §3.8]` cites; `log.h` / `otel.h` comments updated to "placeholder in v1.0" per `[2k §5]`. |
| N-P1-2 | P1 | §7.3 decimal helper list incomplete | §7.3 rewritten to enumerate the full 5 functions + 2 macros per `[2a §5.2]` lines 252–274; §10 row 2a updated in lockstep. |
| N-P1-3 | P1 | §7.4 architectural-sentinel names don't match `c_api/error.h` | Closed by Root cause #2: row 1 now reads `OK=0`, `CANCELLED=1`, `UNKNOWN=2` for the architectural set, with the 8 2i-introduced codes named individually. |
| N-P2-1 | P2 | §3.1 stable-tier roster omits `Clock` | Closed by Root cause #3: §3.1 now cross-references §9 (which lists `Clock`); the §3.1 prose explicitly names `Clock` among the pluggable interfaces. |
| N-P2-2 | P2 | §10 row 2m mis-states the mandatory Python wheel matrix | Closed by Root cause #3: row 2m now reads "CPython 3.10 mandatory; 3.11 / 3.12 / 3.13 best-effort per `[2m §1]` / `[arch §7.1]`". *(Audit note — superseded by the abi3 pivot: see PY-005 / `[arch §7.1]` amendment; row 2m now names the single `cp310-abi3` wheel. Quote preserved unrewritten.)* |
| N-P2-3 | P2 | §10 row 2l "no own block" claim conflicts with 2l reservation | Closed by Root cause #1: row 2l now reads "`[1100, 1199]` 2l-owned; 0 published in v1.0; 4 variants pinned internally for v1.x publication per `[2l §6.7]`" with the four variant names. |
| N-P2-4 | P2 | §7.4 forward-compat cite points at the wrong upstream section | §7.4 forward-compat trailer now cites `[arch §5.3]` / `[const §X.4]` / `[2i §4.4]`–`[2i §4.5]`. |
| N-P2-5 | P2 | §10 row 2k C-ABI block claim conflicts with both definitions | Closed by Root cause #1: row 2k now reads "`[1000, 1099]` 2k-owned; 0 published in v1.0; 7 internal variants pinned for v1.x publication per `[2k §6.3]`". |
| N-P3-1 | P3 | §7.5 cancellation-translation cite missing `[2i §4.9]` as binding | Cite order in §7.4 trailer reordered to `[2i §4.9]` primary, `[const §XI.2]` secondary, with `[2f §6.5]` named as precedent. |
| N-P3-2 | P3 | §3.2 provisional list "TS-13" identifier has no upstream pin | Replaced "TS-13 benchmark spike" with `[2k §6.1]` / `[arch §11]` Q4 ("Bench-driven `[SYN §3.8]`"). |
| N-P3-3 | P3 | §11 frozen-until rule's "Adding a new sub-header" carve-out under-states the §6 invariant | §11 bullet appended: "*and* the new module's allowed-include edges are added to `[arch §2.3]`" with note that edits to `[arch §2.3]` themselves trigger `[const §XX]` amendment. |

#### Disagreed findings

(none) — every Codex and Opus-new finding adopted as judged by the Opus adversarial review.

#### Net effect

§7.4 re-tabulated from the live `c_api/error.h` excerpt, §7.5 rewritten against live `[2i §5.2]`, §6 expanded with two missing split sub-headers, §7.3 expanded to the full decimal helper list, §10 every row touched (occupancy parentheticals + 2k/2l/2m wording), §3.1 reorganised to cross-reference §9, §3.2 wording aligned with `[arch §11]` Q4, §11 polished. No structural changes; no new sections introduced (Appendix C aside); the contract's overall shape — scope, tier definitions, namespace catalogue, header layout skeleton, CMake target catalogue, frozen-until rule, source map — is unchanged. 14 total findings closed (3 Codex + 11 Opus-new); convergence story now ready for user sign-off.

---

**End of v0.2 draft. Gate A round 1 converged 2026-05-10 (Phase A). Awaiting user sign-off.**
