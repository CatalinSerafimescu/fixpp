# 2j — Control-Plane Interface + gRPC Default Implementation

**Status:** Draft v0.4 — Gate A round 2 converged (Phase A); post-sign-off targeted amendment 2026-08-29 (§6.5 executor topology deleted — superseded by feature 023 T010; see the §6.5 note)
**Date:** 2026-05-09
**Owner:** Opus (Phase A drafter)
**Inherits:** `[arch §1.1]`, `[arch §1.2]`, `[arch §2.3]`, `[arch §3]`, `[arch §4.10]`, `[arch §4.11]`, `[arch §5.1]`, `[arch §5.2]`, `[arch §5.3]`, `[arch §5.4]`, `[arch §5.6]`, `[arch §5.7]`, `[arch §6]`, `[arch §7.4]`, `[arch §8]`, `[arch §8.1]`, `[arch §8.2]`, `[arch §9.1]`, `[arch §9.2]`, `[arch §9.3]`, `[arch §10] row 2j`, `[const §I.2]`, `[const §IV.4]`, `[const §V.1]`, `[const §V.3]`, `[const §VI.2]`, `[const §VI.5]`, `[const §VII.4]`, `[const §VIII.5]`, `[const §X.1]`, `[const §X.2]`, `[const §X.4]`, `[const §X.5]`, `[const §XI.1]`, `[const §XI.2]`, `[const §XI.3]`, `[const §XII.5]`, `[const §XIII.1]`, `[const §XIII.2]`, `[const §XIII.3]`, `[const §XIV.1]`, `[const §XIV.2]`, `[const §XIV.3]`, `[const §XIV.4]`, `[const §XV.5]`, `[const §XV.15]`, `[const §XVII.1]`, `[const §XVIII.1]`, `[const §XX.1]`, `[SYN §3.6 #20]`, `[SYN §3.6 #21]`, `[SYN §3.6 #22]`, `[2c §4.9]`, `[2c §7.2]`, `[2d §4.4]`, `[2d §4.5]`, `[2d §4.7]`, `[2d §4.8]`, `[2d §6.5]`, `[2d §6.7]`, `[2d §7.6]`, `[2d §7.8]`, `[2e §4.4]`, `[2e §6.1.4]`, `[2e §6.7]`, `[2g §4.3]`, `[2g §4.5]`, `[2g §6.5]`, `[2g §7.6]`, `[2g §7.7]`, `[2h §4.1]`, `[2h §4.2]`, `[2h §6.4]`, `[2h §7.6]`, `[2i §1.1]`, `[2i §1.2]`, `[2i §2]`, `[2i §4.2]`, `[2i §4.3]`, `[2i §4.5]`, `[2i §4.9]`, `[2i §4.10]`, `[2i §5.2]`, `[2i §6.5]`, `[2i §7.9]`
**Cites:** see Appendix B (every reference grouped by source).
**Catalogue rows owned (sole):** **SVC-001**, **SVC-004**, **SVC-005** (NEW — declared by this doc; Appendix D §D.1 queues the catalogue-row drop-in for sign-off, modelled on the 2d/2f NFR-015/NFR-016 precedent). **Explicit non-overlap (not owned):** **SVC-002** (iceoryx2 data plane — owned by **2l** per `[arch §8.2]`), **SVC-003** (gRPC-only mode when iceoryx2 unavailable — owned by **2l** per `[arch §8.2]`); 2j publishes the boundary statement only (§1.2 / §7.9), not row coverage.
**Convergence log:** Appendix C — v0.3 addresses Codex round-2 review (0 P1 / 4 P2 / 3 P3) and Opus round-2 adversarial review (combined post-judging 2 P1 / 2 P2 / 4 P3; 0 new root causes; RC#3/#4 closed mechanically); see Appendix C.

---

## §1 Goals

2j locks the **control-plane plugin contract** that closes `[arch §10] row 2j` ("ControlPlane interface — `SVC-005` shape, gRPC default impl") and operationalises `[SYN §3.6 #20]` ("control plane transport — DECIDED pluggable interface; gRPC is the default impl"). Concretely:

1. Lock the `fixpp::service::ControlPlane` pure-virtual interface at ≤ 5 pure-virtual methods per `[const §XIV.2]` / `[arch §6]`. v0.2 publishes **3 pure-virtual** (`start`, `stop`, `health`) — well under the cap, with two slots of headroom for post-v1 extensions (RPC re-mapping, dynamic auth-token rotation per §10 Q5). The interface's *method signatures depend on `core/` only* per `[arch §2.3]` allowed-include edges and the `[const §V.1]` AGPL-boundary structural rule (§4.4).
2. Lock the `fixpp::service::ControlPlaneConfig` value-typed config consumed at instantiation per `[arch §6]` rule 4 (factory entry point taking `std::pmr::memory_resource*` + interface-specific config). v0.2 carries the listen address (Unix socket path on Linux, named pipe on Windows; TCP opt-in per `[arch §8.1]`), max-in-flight-RPC cap, max message size, max metadata size, OTel hooks, and the per-RPC arena reference (§4.2).
3. Lock the **gRPC default implementation surface** under `service/grpc/` translation-units. The default impl links `fixpp::capi` (the C ABI per `[arch §4.10]`) and the gRPC C++ runtime; it does NOT include any other engine internal headers per `[arch §8]` / `[const §V.1]`. The CMake target `fixppd` (the daemon) and the `service/grpc/*.cpp` translation-units enforce this through the `fixpp::service-iface` interface-only library + `tools/check_layers.py` lint per `[arch §7.4]` / `[arch §8]` (§4.6).
4. Lock the **proto schema** `service/proto/fixpp_control.proto` v1.0 RPC surface: `OpenSession`, `CloseSession` (graceful + force flavours), `Configure` (reserved-empty per `[arch §5.6]` carve-out), `StreamMetrics`, `StreamLogs`, `StreamSessionEvents`, `Health` (gRPC standard health-check). The proto's stability tier is `[arch §9.3]` ("Stable from v1.0 ... the gRPC control-plane proto"); §4.7.1 (NEW in v0.2 per RC#6) defines the additive-only / SemVer-by-analogy proto-evolution rules locally — this is an architectural-stability story, not a constitutional ABI-policy claim. **`RotatePinset` and `ReloadCertSource` are deferred to v1.x** (RC#5 close — see §10 Q1 / Q9 and §1.2 below): the v1.0 cross-doc state has no AGPL-boundary path for those RPCs (`[2i §2]` non-goal #6 declines the C-ABI rotation symbols, and `service/grpc/*.cpp` cannot include `<fixpp/tls/...>` per `[arch §8]`). The v1.0 surface above is implementable through 2i v0.3's existing C-ABI without a 2i v0.4 amendment. (§4.7, §4.7.1)
5. Lock the **engine-anchor + engine-executor handler** rule per `[2d §7.8]`. ControlPlane is engine-scoped (one ControlPlane per engine, one socket); the factory is `EngineConfig::control_plane_factory` (added at sign-off via Appendix D §D.2 amending `[2d §4.4]`). Per `[2d §7.8]` ("Control-plane handlers ... run on the engine executor outside any session serialisation domain"), the v1.0 default impl runs RPC handlers on `EngineConfig::executor` (the engine executor) — there is **no dedicated `EngineConfig::control_plane_executor` field** in v0.2 (RC#1 close: the v0.1 attempt to introduce one was a `[2d §7.8]` override). Operators that want the control plane on a separate pool can supply a strand or sub-executor per the `[2d §4.5]` `executor_override` pattern by wrapping the engine executor before passing it to the factory; the factory's `exec` parameter (§4.1) is the binding point. (§4.5, §6.5)
6. Lock the **AGPL-boundary structural enforcement** through three mutually-reinforcing mechanisms: (a) `include/fixpp/service/control_plane.h` includes `core/` only and forward-declares everything else; (b) the `service/grpc/*.cpp` translation-units link `fixpp::capi` + `fixpp::service-iface` + gRPC, but NOT `fixpp` (the C++ engine umbrella); (c) `tools/check_layers.py` greps every `service/` source file for `#include <fixpp/X/...>` where `X != service` and fails the build on hit (forward reference to the lint tool — `tools/check_layers.py` is named at `[arch §8]` and is owned by the architecture; first landing tracked in §9 seam #6 / §10 Q10). (§4.4, §4.6, §9 seams #6 / #7)
7. Stay **zero-allocation between parse and `fromApp`** per `[const §VIII.5]`: ControlPlane RPCs do NOT run on the inbound dispatch hot path — they run on the engine executor outside any session serialisation domain per `[2d §7.8]` (Goal 5) and reach engine state through the C ABI's session-locked accessors per `[2i §4.10]` `FIXPP_REQUIRES_SESSION_LOCK`. Any RPC handler that needs to read session state posts onto the session strand via `fixpp_session_*` C-ABI calls; the strand-side accessor honours the hot-path discipline. The per-RPC arena (a `monotonic_buffer_resource` per `[arch §5.2]`) holds gRPC request/response buffers and is reset at RPC completion (§6.1, §8).
8. Stay **exception-free across the AGPL boundary** per `[arch §5.3]` / `[2i §5.2]`: the gRPC default impl is C++ (the gRPC C++ runtime throws), but no exception escapes the `service/grpc/` translation-units into the engine. Every C-ABI call from the gRPC handler into the engine goes through the steady-state `guarded_call_steady` shape per `[2i §5.2]` (engine-internal); construction-time engine setup goes through `guarded_call_construction`. The gRPC handler's own exception window is bounded by a per-handler try/catch that translates uncaught exceptions to a gRPC `INTERNAL` status. (§4.6, §6.1)
9. Honour **ASIO native cancellation slots** per `[const §XI.2]` for streaming RPCs. `StreamMetrics`, `StreamLogs`, `StreamSessionEvents` are server-streaming gRPC RPCs that the consumer can cancel (close-on-half) at any time; the cancellation propagates through gRPC's `ServerContext::IsCancelled()` to the streaming coroutine, which completes with `expected_t::unexpected{control_plane_stream_cancelled}` per the `[2d §6.5]` `cancellable_dispatch` precedent (mapped to `FIXPP_ERR_CANCELLED` at the C ABI per `[2i §4.9]`). (§6.4, §6.6)
10. Lock the **observability surface** (SVC-004 partition): `Health` returns the engine + per-session health as a structured response (gRPC standard health-check protocol); `StreamMetrics` exposes an OTLP-equivalent stream of metric points correlated with the engine's `MeterProvider` per `[const §XIII.1]` (the bridge to Prometheus + OTLP collector pipeline is owned by 2k); `StreamLogs` exposes structured-log records correlated with `trace_id` / `span_id` per `[const §XIII.3]`; `StreamSessionEvents` exposes Logon/Logout/Reject/Heartbeat structured events per the session-module Phase-4 spec. The schema is owned by 2k for metrics + logs and by the session-module Phase-4 spec for session events; 2j owns the streaming wire shape only. Stream backpressure is **close-on-overflow** with `control_plane_stream_overflow` per RC#7 (§4.8 / §6.4) — *not* drop-oldest, distinct from the `[const §XIII.2]` permission. (§4.7, §4.8, §7.8)

### §1.1 Magnitude domain — RPC rates, message sizes, DoS bounds

The control-plane is **not** a hot-path. Capacity targets are deliberately conservative; production deployments run **operational** RPCs (open a session, query health) at rates measured in calls-per-minute, not calls-per-microsecond. Streaming RPCs are bounded-rate observers, not high-throughput data-plane channels — that role is iceoryx2's per `[arch §8.2]` (owned by 2l).

- **Max in-flight RPCs (server-side).** `ControlPlaneConfig::max_in_flight_rpcs = 64`. This bounds the gRPC server's pending-call queue. A 65th concurrent call is rejected with gRPC `RESOURCE_EXHAUSTED` before the handler runs (the gRPC server's flow-control hook). Operationally, 64 concurrent RPCs is wide enough for any reasonable consumer (typical observers maintain a single long-lived `StreamMetrics` + occasional sync calls); narrow enough that a runaway consumer cannot DoS the engine. The cap is a `ControlPlaneConfig::max_in_flight_rpcs` knob; operators raise explicitly per `[const §XII.5]` no-implicit-default pattern only if they have a real consumer count exceeding 64. **Deployment-shape rationale (N-P3-5 close):** 64 is engineering judgment without a measured deployment shape — production calibration is owed at v1.x; for context, a 100-session 1U-blade deployment expects ≤ 8 long-lived observers (one per ops/SRE consumer, plus headroom for short-lived health-probe calls), so 64 is ≈ 8× over-provisioned for that shape. Operators with a 1000-session multi-tenant deployment will likely raise the cap; the `[const §XII.5]` rule requires the raise to be explicit.
- **Max RPC request / response message size.** `ControlPlaneConfig::max_message_bytes = 4 * 1024 * 1024` (4 MiB). gRPC's default is 4 MiB; we honour it. A `Configure` RPC with a > 4 MiB payload is rejected with `RESOURCE_EXHAUSTED` before the handler runs. Streaming RPCs do not buffer past the per-message cap — each `StreamMetrics` record is a small protobuf (≤ 1 KiB typical), and the stream is server-driven.
- **Max metadata size.** `ControlPlaneConfig::max_metadata_bytes = 8 * 1024` (8 KiB). gRPC's default is 8 KiB across all metadata entries; we honour it. Custom auth tokens or trace headers stay well under.
- **`OpenSession` latency — split warm/cold (N-P2-4 close).** Warm-cache p99 **≤ 50 ms** (cached dictionary, cert source, plugin factories — the typical steady-state path). Cold-cache p99 **≤ 500 ms** (first call after engine start; cold dictionary load per `[2c §6.5]`; cold `cert_source` SSL_CTX construction per `[2g §6.5]`; plugin-factory `make` calls). Bench Tier 1 measures the warm-cache path; the cold-cache path is informational. `CloseSession` is cold-path (≈ 1–10 ms warm; bench ceiling ≤ 50 ms).
- **`StreamMetrics` per-record latency.** ≤ 100 µs p99 from the meter provider's `OnRecord` callback to the wire. The metric sampling cadence is operator-controlled (the consumer subscribes via the `StreamMetrics` request specifying a sampling interval); the wire emission cost is what 2j budgets.
- **`Health` latency.** ≤ 5 ms p99. The gRPC standard health-check is a single call returning a serving-status enum; the engine queries the session table and the IO executor's drain status.
- **DoS cap — auth.** v1.0 ships **mTLS-only auth** for the gRPC control plane: `ControlPlaneConfig::require_mtls = true` (default). The gRPC server binds an `SSL_CTX` configured through 2g's `cert_source` — typically the same operational cert that the FIX sessions use. Unix-domain-socket transport on Linux + named-pipe on Windows are mTLS-protected too (gRPC supports SSL over Unix sockets); the `Config::require_mtls = false` opt-out is permitted only with the listener bound to a kernel-namespace-isolated Unix socket (caller-side OS-level trust). The `mtls = false` opt-out emits a compile-time `[[deprecated]]` diagnostic per the `[const §XII.5]` no-implicit-default precedent.

### §1.2 Scope boundary — what 2j owns vs what it doesn't

2j **owns**:

- The `fixpp::service::ControlPlane` pure-virtual interface (≤ 5 pure-virtual; §4.1).
- The `fixpp::service::ControlPlaneConfig` value-typed config (§4.2).
- The proto schema `service/proto/fixpp_control.proto` v1.0 RPC surface — **`OpenSession`, `CloseSession`, `Configure` (reserved-empty), `StreamMetrics`, `StreamLogs`, `StreamSessionEvents`, `Health`** (§4.7); proto-evolution governance rules (§4.7.1, NEW in v0.2 per RC#6).
- The default gRPC implementation (`fixpp::service::grpc_control_plane`) translation-unit layout under `service/grpc/` (§4.6).
- The `EngineConfig::control_plane_factory` field shape (Appendix D §D.2 drop-in to `[2d §4.4]`). **No dedicated control-plane executor field** in v0.2 (RC#1 close): per `[2d §7.8]` handlers run on `EngineConfig::executor`; operators that want isolation supply a strand or sub-executor via the factory's `exec` parameter following `[2d §4.5]`'s `executor_override` pattern.
- The `error::control_plane_*` variants and the `FIXPP_ERR_CTRL_*` C-ABI block at `[900, 999]` per `[2i §1.1]` reserved-block layout (§6.6).
- The `StreamMetrics` / `StreamLogs` / `StreamSessionEvents` wire shape (the protobuf message layout and the streaming control flow), including the close-on-overflow backpressure rule (§4.8 / §6.4).

2j **does not own**:

- The `fixpp_engine_create` / `fixpp_session_open` / `fixpp_session_close` / `fixpp_session_send` C-ABI signatures and behaviour. The shape is pinned by `[2i §4.2]` / `[2i §4.10]` / `[2i §5.2]`; the *signatures* and FSM behaviour are owned by the **session-module Phase-4 spec**. 2j is a *consumer* of those C-ABI symbols, not an owner. (CA-005, CA-006, CA-007 are shape-cross-cut to 2i per `[2i §1.2]` non-owned, behaviour-cross-cut to the Phase-4 session-module spec; 2j does not claim them.)
- The metric / log / OTel record schemas. Owned by **2k** per `[arch §5.7]`. 2j carries them across the wire only.
- The session-event schema (Logon / Logout / Reject / Heartbeat structured events). Owned by the **session-module Phase-4 spec**. 2j carries them across the wire only.
- **SVC-002 (iceoryx2 data plane)** and **SVC-003 (gRPC-only mode)** — owned by **2l** per `[arch §8.2]` ("Topic shape, ownership semantics, backpressure, fallback when iceoryx2 isn't running — all in **2l**"). The control plane and the data plane are explicitly **non-overlapping** — control plane is request/response RPC; data plane is high-volume one-way pub/sub of FIX message bytes. 2j publishes only the boundary statement (this paragraph and §7.9); the row entries themselves remain 2l's. (RC#2 close — Appendix A.1 lists only SVC-001 / SVC-004 / SVC-005 as owned-sole.)
- The session-tap consumer API in v1.0. Owned by **2l** per `[arch §4.9]`. v1.0 ControlPlane does **not** publish a "stream tapped FIX messages" RPC — that's iceoryx2's job; gRPC over Unix-socket is wrong for high-volume one-way streaming. (Tracked in §10 Q4 if a user requests gRPC streaming of FIX messages for non-HFT environments.)
- **`RotatePinset` and `ReloadCertSource` RPCs in v1.0** (RC#5 close). Per `[2i §2]` non-goal #6 ("No `fixpp_pinset_add` / `fixpp_pinset_remove` / `fixpp_cert_source_reload` symbols in v1.0") and `[2g §7.6]` ("the C-ABI consumer triggers reload via 2j's control plane (a `ReloadCertSource` RPC, not a C-ABI call)" — the cited `[2i §1.2]` text reads identically), the v1.0 cross-doc state has no AGPL-legal path for these RPCs: 2i declines the C-ABI rotation surface; `service/grpc/*.cpp` cannot include `<fixpp/tls/...>` per `[arch §8]`. v1.0 cuts both RPCs from the proto and defers them to v1.x (see §10 Q1 / Q9). The `[2g §7.7]` reload-trigger hand-off and the `[2h §7.6]` close+reopen contract remain published — they will be discharged in v1.x once a 2i amendment publishes the rotation accessors (or an explicit AGPL-boundary carve-out is authored).
- The `Pinset::add` / `Pinset::remove` thread-safety contract. Owned by **2g** per `[2g §4.3]` / `[2g §6.5]` — recorded for v1.x integration only.
- The `cert_source` reload mechanism per se. Owned by **2g**; v1.0 ControlPlane does not invoke it (see RC#5 above).
- The transport graceful drain on `CloseSession`. The drain's mechanics are owned by **2h** per `[2h §6.4]` / `[2h §7.6]`; 2j is the trigger source.

The `[const §XIV.2]` ≤5-pure-virtual cap **applies** to `ControlPlane` (3 of 5 used in v0.2; 2 slots headroom). The `[const §V.1]` AGPL-boundary structural rule **applies** strictly: no engine-internal C++ header may be included from the gRPC default impl translation-units, and `tools/check_layers.py` enforces (named at `[arch §8]`; see §6 / §10 Q10 for the tool's first-landing tracking).

---

## §2 Non-goals (v1.0)

1. **No JSON-over-Unix-socket default.** The default is gRPC over Unix domain socket (Linux) / named pipe (Windows), per `[arch §8.1]` and `[const §XIV.1]`. JSON-over-Unix-socket is documented as a **sample alternate impl** of `ControlPlane` (an example consumer that swaps gRPC out without rebuilding the engine) but does not ship as a v1.0 default.
2. **No TCP gRPC by default.** TCP gRPC is opt-in via `ControlPlaneConfig::listen_endpoint = tcp_endpoint{...}` and not the default for security reasons per `[arch §8.1]`. Operators who need TCP gRPC also enable mTLS (`require_mtls = true`, default).
3. **No `dlopen`-based ControlPlane plugin loading** per `[const §XIV.4]`. Compile-time selection: the engine binds the `ControlPlane` factory at `Engine::open` time. A consumer who needs a non-gRPC ControlPlane links their alternative impl into `fixppd`'s build (the engine itself does not need a rebuild — the C ABI is the boundary).
4. **No high-volume FIX-message streaming via the control plane.** That role belongs to the iceoryx2 data plane per `[arch §8.2]` / 2l. The `StreamMetrics` / `StreamLogs` / `StreamSessionEvents` RPCs are bounded-rate **observability** streams, not message-flow streams. (§7.9)
5. **No mid-flight RPC mutation.** `Configure` accepts only mid-session-mutable knobs per `[arch §5.6]` carve-out (concretely: dialect-overlay swap is **rejected** per `[2c §7.2]` — there is no `Session::swap_dialect_overlay(...)` API in v1.0; only the pinset is mid-session-mutable, and that goes through `RotatePinset`, not `Configure`). The `Configure` RPC's v1.0 surface is currently empty (no mid-session-mutable knobs other than pinset, which has its own RPC); the RPC is reserved for v1.x post-spec extensions. This is an explicit non-goal: `Configure` does not become a "kitchen-sink" mutator path.
6. **No cross-engine federation.** v1.0's ControlPlane is one-engine-per-listener. A consumer that wants to manage multiple engines runs multiple `fixppd` daemons; an upstream load-balancer or service-mesh layer routes RPCs. Federation is post-v1 if a real consumer needs it.
7. **No dynamic auth-token rotation in v1.0.** mTLS auth is the primary path; auth-token (e.g., bearer-token) auth is not in v1.0's scope. Two of the five pure-virtual slots are intentionally reserved for post-v1 auth-token rotation hooks (§4.1).
8. **No per-RPC throttling / rate-limiting.** v1.0 enforces only the per-server `max_in_flight_rpcs` cap (§1.1). Per-consumer / per-RPC-method rate limiting is post-v1 if a real DoS scenario is observed.
9. **No control-plane-side persistence.** The ControlPlane is stateless — every RPC reads from the engine via the C ABI and writes responses. Operator state (e.g., "which pinset rotation is pending?") lives in the engine, not the ControlPlane. A consumer can poll `Health` for the current state.
10. **No `EncryptMethod(98)` ≠ 0 support.** The control plane is over TLS at the transport layer per `[const §XII.7]`; application-layer encryption is banned constitutionally and not exposed via any RPC.

---

## §3 Inherited surface

This section quotes the inherited contract verbatim (short excerpts) so the reader can re-verify against live source.

### §3.1 From `[arch §4.11]` — the service/ surface inventory (the spine)

> The module has two parts with different stability and API status. The split resolves the apparent contradiction between SVC-005's "pluggable control plane" promise and the AGPL boundary rule (§8): the *interface* is a public C++ header like any other plugin contract; the *daemon and default impls* sit downstream of the C ABI.
>
> **Public C++ interface surface (pluggable):**
>
> - `fixpp::service::ControlPlane` — public abstract interface in `include/fixpp/service/control_plane.h`. ≤5 pure-virtual `[const §XIV.2]`. Lets shops swap the default gRPC implementation for JSON-over-Unix-socket or anything else without rebuilding the engine. Owned by **2j** (`SVC-005`).
> - `fixpp::service::ControlPlaneConfig` — value-typed config passed at instantiation.
> - gRPC schema: `service/proto/fixpp_control.proto` — session lifecycle (`OpenSession`, `CloseSession`, `Configure`), observability (`StreamMetrics`, `StreamLogs`). The proto is a stable contract; the C++ classes implementing the gRPC adapter are not.
>
> **Internal (not C++ API):**
>
> - `fixppd` binary — daemon. Consumes the engine through the **C ABI only** `[const §V.1]` (legal isolation). May `#include <fixpp/service/control_plane.h>` (the interface header above) but **must not** `#include` anything else under `include/fixpp/`.
> - Default gRPC implementation of `ControlPlane` — translation-units under `service/grpc/`, links against `fixpp::capi` and the gRPC C++ runtime.
> - Optional iceoryx2 publisher for the data plane `[const §XIV.3]`.
>
> **Catalogue rows:** SVC-001, SVC-002, SVC-003, SVC-004, SVC-005.

This is the spine; §4 expands every bullet. SVC-005 is declared NEW by this doc per Appendix D §D.1.

### §3.2 From `[arch §8]` — Service-Mode Boundary (the AGPL boundary, structural)

> The `service/` directory exists to package the engine as a daemon. Its architectural rule:
>
> > The `fixppd` daemon and any default plugin implementations under `service/` consume the engine **only through the C ABI**. They must not include engine internal headers (`<fixpp/wire/...>`, `<fixpp/session/...>`, `<fixpp/dict/...>`, etc.).
>
> The rule applies to the binary and to default impl translation units. It does **not** restrict `include/fixpp/service/control_plane.h` or other public service-mode interface headers — those live under `include/fixpp/`, depend only on `core/`, and are part of the public C++ plugin surface like every other interface listed in §6. `fixppd` includes `<fixpp/service/control_plane.h>` (the interface), implements or instantiates a `ControlPlane`, and reaches engine functionality through the C ABI symbols exposed by `fixpp::capi`.
>
> This is enforced by:
> - A CMake target visibility rule: `fixppd` links `fixpp::capi` and `fixpp::service-iface` only; the C++ engine umbrella `fixpp` is **not** in its include search path or link interface.
> - The `tools/check_layers.py` lint scans `service/` source for any `#include <fixpp/X/...>` where `X != service`, and fails the build on any hit. `<fixpp/service/...>` is allowed (interface). `<fixpp/wire/...>`, `<fixpp/session/...>`, etc. are forbidden.

This is the structural rule §4.4 / §4.6 / §9 seam #6 implement.

### §3.3 From `[arch §8.1]` — Control plane (gRPC, default)

> - Schema lives at `service/proto/fixpp_control.proto`.
> - Transport: Unix domain socket (Linux), named pipe (Windows). TCP gRPC is opt-in via config and not the default for security reasons.
> - Control-plane interface (`SVC-005`) lets shops swap gRPC for JSON-over-Unix-socket or anything else without rebuilding the engine. Owned by **2j**.

§4.2 / §4.7 honour all three bullets.

### §3.4 From `[const §V.1]` — AGPL boundary (constitutional)

> **`fixpp` library:** AGPL-3.0 + commercial dual. The C ABI is the linkage isolation boundary for commercial users.

Per §3.2 / §4.4 / §4.6: the AGPL boundary is structural here — the `service/grpc/` translation-units consume the engine *exclusively* through `<fix/c_api.h>`, never through `<fixpp/X/...>` for any `X` other than `service`.

### §3.5 From `[const §XIV.1]` / `[const §XIV.2]` — pluggable interfaces + ≤5 pure-virtual cap

> **The following are pluggable, each with one default impl in v1.0:**
>
> - **Transport** (default: ASIO TCP/TLS over OpenSSL).
> - **Control plane** (default: gRPC over Unix socket / named pipe).
> - **Cert source** (default: file-based PEM/DER).
> - **Logger sinks** (default: in-process async logger + OTLP exporter).
> - **MessageStore** (default: in-memory; file-based impl also v1.0).
>
> **Interface surfaces are small.** Each pluggable interface defines **≤5 pure-virtual methods**. Bigger surfaces are permitted only with an explicit design-doc justification (one paragraph naming the necessary methods and why each is irreducible). The justification is reviewed at Gate A.

§4.1 honours: `ControlPlane` declares **3 pure-virtual** (`start`, `stop`, `health`); the cap is satisfied with two slots of headroom; no Gate-A justification paragraph needed.

### §3.6 From `[const §XIV.3]` — iceoryx2 opt-in

> **Data-plane SHM via iceoryx2** is opt-in for sidecar mode. The control plane (gRPC) works without it.

§7.9 honours: 2j's ControlPlane works without iceoryx2; the data plane is a separate concern owned by 2l.

### §3.7 From `[const §XIII.1]` — observability surface

> **OpenTelemetry instrumentation from v1.0.** Traces, metrics, logs all OTLP-exportable. Prometheus + OTLP dual export is the v1.0 minimum.

§4.7 / §4.8 honour: `StreamMetrics` is the gRPC bridge that exposes the engine's `MeterProvider` records; `StreamLogs` exposes the async logger's records correlated by `trace_id` per `[const §XIII.3]`. The schema is owned by 2k; 2j is the wire layer.

### §3.8 From `[arch §6]` — plugin pattern

> Each pluggable interface gets:
> 1. A pure-virtual class in the relevant module's public header.
> 2. **≤5 pure-virtual methods** `[const §XIV.2]`.
> 3. One default implementation shipped in v1.0.
> 4. A clear factory entry point that takes a `std::pmr::memory_resource*` plus interface-specific config.
> 5. Compile-time selection in v1.0; no `dlopen`.

`ControlPlane` is the v1.0 pluggable per `[arch §6]` table. §4.1 / §4.2 / §4.3 / §4.6 satisfy all five rules.

### §3.9 From `[arch §5.6]` — frozen-at-open / mid-session-mutable carve-out

> **`SessionConfig` is value-typed and frozen at session open.** No mid-session reconfiguration of: dictionary, security profile, message store, executor, lock policy, dialect overlay. The supported pattern for any of these is close-and-reopen the session. Mutating ops on session-adjacent state that *do* admit mid-session change (e.g., pinset rotation per `[const §XII]`) go through their own APIs and are explicitly thread-aware.

§4.7's `Configure` RPC enforces: in v1.0 the only mid-session-mutable knob is the pinset (which has its own dedicated `RotatePinset` RPC, not `Configure`); `Configure`'s v1.0 surface is reserved for v1.x. `ReloadCertSource` triggers the close-and-reopen pattern per `[2g §7.7]` / `[2h §7.6]`.

### §3.10 From `[2d §4.4]` — `EngineConfig` field shape

`EngineConfig` carries engine-level shared resources: executor, clock, dictionaries, default plugin selections (`MessageStoreFactory`, `cert_source`, `TransportFactory`). 2j extends this with **one field** — `control_plane_factory` (engine-anchor; the only ControlPlane per engine). RPC handlers run on `EngineConfig::executor` per `[2d §7.8]`; v0.2 does not add a separate `control_plane_executor` field (RC#1 close — see §6.5). Appendix D §D.2 queues the one-field amendment to `[2d §4.4]`.

### §3.11 From `[2d §4.7]` — cancellation propagation API

Every async session/transport/store/sync op completes with one of `expected_t::unexpected{*_cancelled}` / `*_aborted` on `cancellation_type::total`. The C ABI translates **every** cancellation outcome uniformly to `FIXPP_ERR_CANCELLED` per `[2i §4.9]`. 2j's streaming RPCs (`StreamMetrics`, `StreamLogs`, `StreamSessionEvents`) honour the same contract — server-streaming cancellation surfaces as `expected_t::unexpected{control_plane_stream_cancelled}` (§6.4 / §6.6).

### §3.12 From `[2g §7.7]` / `[2h §7.6]` — control-plane reload triggers (consumer drop-in; v1.x scope per RC#5)

Quoted verbatim from `library/.specify/2g-tls.md:1058`:

> The control-plane gRPC schema (`service/proto/fixpp_control.proto` per [arch §8.1]) carries an `RotatePinset` RPC and a `ReloadCertSource` RPC consumed by 2j's `ControlPlane` interface. The **handler** lives in 2j; the **action** is `Pinset::add` / `Pinset::remove` / `cert_source` swap (the latter via session close-and-reopen per [arch §5.6] — the cert_source itself is frozen at session open, only the pinset is mid-session-mutable). Appendix A claims neither row from 2j; this is a forward-compat hook only.

Quoted verbatim from `library/.specify/2h-transport.md:1286–1290`:

> Per `[arch §8.1]` / `[2g §7.7]`: the control plane (gRPC schema at `service/proto/fixpp_control.proto`) carries `CloseSession` (graceful), `RotatePinset`, and `ReloadCertSource` RPCs. The handler dispatches into the engine's session-management path; for transport-relevant ops:
>
> - **`CloseSession`** triggers `Session::close(graceful)` per `[2d §4.7]` two-phase close. 2h's `async_read` / `async_write` continue under the root state during phase 1; phase 2 fires `cancellation_type::total` and 2h's ops complete with `*_cancelled`. The FSM closes the transport.
> - **`RotatePinset`** triggers `Pinset::add(...)` / `Pinset::remove(...)` per `[2g §4.3]`; mid-session rotation does NOT affect the in-flight handshake per `[2g §6.5.1]` / §6.2 binding. The next handshake (e.g., on reconnect after a network blip) captures the post-rotation snapshot.
> - **`ReloadCertSource`** is a session close-and-reopen pattern per `[arch §5.6]` (the `cert_source` is frozen at session open). 2h provides the close path; the engine's session-management layer handles the reopen.

Plus the closing line at `library/.specify/2h-transport.md:1292`:

> 2h owns the call sites; 2j owns the gRPC schema + handler dispatch.

**v1.0 scope per RC#5.** v0.2 honours `CloseSession` as a v1.0 first-class RPC; `RotatePinset` and `ReloadCertSource` are deferred to v1.x because the v1.0 cross-doc state has no AGPL-legal path for them (`[2i §2]` non-goal #6 declines the C-ABI rotation symbols; `service/grpc/*.cpp` cannot include `<fixpp/tls/...>` per `[arch §8]`). The `[2g §7.7]` and `[2h §7.6]` paragraphs above remain published forward-compat hooks for the v1.x discharge; they do not constrain the v1.0 proto surface in §4.7.

### §3.13 From `[2i §1.1]` — reserved C-ABI error block for 2j

Per `[2i §1.1]` / `[2i §4.3]`, the numeric block `[900, 999]` is **reserved for 2j control plane** under the prefix `FIXPP_ERR_CTRL_*`. v0.2 occupies 2 of those 100 slots (`FIXPP_ERR_CTRL_CONFIG = 900`, `FIXPP_ERR_CTRL_RUNTIME = 901`; cancellation reuses the existing `FIXPP_ERR_CANCELLED = 1` per `[2i §4.9]`); see §6.6 for the engine-side `error::control_plane_*` variants and the C-ABI coalescing groups. The 100-slot block leaves room for v1.x growth (the deferred rotation surface per §10 Q1 / Q9 will land additional variants).

### §3.14 From `[2i §4.10]` — reentrancy annotation

Per `[2i §4.10]` every public C-ABI symbol carries one of `FIXPP_THREAD_SAFE` / `FIXPP_SINGLE_THREAD` / `FIXPP_REQUIRES_SESSION_LOCK`. **v0.2 introduces no new C-ABI symbols** (RC#1 / RC#5 close — see §5); the gRPC handler invokes only existing C-ABI symbols whose reentrancy annotations are already documented in 2i v0.3.

### §3.15 From `[2i §5.2]` — construction-vs-steady-state thunk split

Per `[2i §5.2]`, every C-ABI thunk is on one side of the split: construction-time thunks catch `std::exception` and translate to a domain-appropriate `FIXPP_ERR_*_CONFIG`; steady-state thunks `std::abort` on any escaping exception (a steady-state escape is by definition an invariant violation per `[arch §5.3]`). 2j's gRPC handlers consume both — `OpenSession` invokes `fixpp_session_open` (construction-time per `[2i §5.2]` whitelist) and `fixpp_session_send` (steady-state). The split is honoured by reference; 2j does not introduce new thunks (§5).

This document refines the inherited surface; it does **not** diverge.

---

## §4 Public C++ API

### §4.1 `fixpp::service::ControlPlane` — abstract interface (3 pure-virtual; ≤ 5 per `[const §XIV.2]`)

```cpp
// include/fixpp/service/control_plane.h
//
// AGPL-boundary rule per [const §V.1] / [arch §8]: this header depends on
// fixpp::core only. The control_plane_t opaque handle that the C ABI
// declares (per [2i §4.2]) is forward-declared here; the .cpp implementation
// of any derived class includes <fix/c_api.h> for engine access.
#pragma once

#include <asio/awaitable.hpp>
#include <asio/cancellation_type.hpp>
#include <memory>
#include <memory_resource>
#include <string_view>

#include <fixpp/core/expected.hpp>
#include <fixpp/core/error.hpp>          // fixpp::core::error variants
// NOTE: NO #include of <fixpp/wire/...>, <fixpp/session/...>, <fixpp/dict/...>
// per [arch §8] / [const §V.1]. Any reference to engine-internal types is
// intermediated through the C ABI, not through C++ headers.

// Forward-declared in <fix/c_api/engine.h> / <fix/c_api/session.h>:
extern "C" {
    typedef struct fixpp_engine  fixpp_engine_t;
    typedef struct fixpp_session fixpp_session_t;
}

namespace fixpp::service {

// ──────────────────────────────────────────────────────────────────────
// HealthStatus — value-typed health report returned by Health RPC and
// by ControlPlane::health() directly. Pinned 1:1 to the gRPC standard
// health-check protocol's serving-status enum (N-P3-2 close: 0..3 are
// the grpc.health.v1.HealthCheckResponse.ServingStatus values; 2j MUST
// NOT add additional enum values without a corresponding gRPC standard
// amendment — per-session detail goes in HealthStatus's other fields).
//
// Multi-field snapshot semantics (N-P1-3 close): HealthStatus is read
// transactionally on the impl side via a seqlock-protected snapshot
// (see §4.3.3); a consumer never observes a torn read across the four
// fields. If `is_always_lock_free<HealthStatus>` is true on the target
// platform, a single std::atomic<HealthStatus> load is permitted as
// an optimisation. Same convention as the [2d §4.4] engine_trace_context
// snapshot.
// ──────────────────────────────────────────────────────────────────────
enum class serving_status : std::uint8_t {
    unknown        = 0,
    serving        = 1,
    not_serving    = 2,
    service_unknown = 3,    // gRPC standard meaning: "I don't know about this service".
};

struct HealthStatus {
    serving_status engine_status;       // The engine itself.
    std::size_t    open_sessions;       // Count of fixpp_session_t handles currently in OPEN state.
    std::size_t    in_flight_rpcs;      // Current ControlPlane RPCs in flight.
    bool           draining;            // True if Engine::close() has been called (graceful drain).
};

// ──────────────────────────────────────────────────────────────────────
// ControlPlane — abstract pluggable interface. EXACTLY 3 pure-virtual
// methods (≤ 5 per [const §XIV.2]); 2 slots of headroom remain for
// post-v1 additions (e.g., dynamic auth-token rotation, RPC re-mapping
// — see §10 Q5).
//
// Lifetime: owned by the Engine (one ControlPlane per engine). The
// ControlPlane sees the engine through the opaque fixpp_engine_t* the
// factory was instantiated with — it never holds a typed C++ reference
// to fixpp::core::Engine, fixpp::session::Session, or any other engine
// internal class. Per [arch §8] the AGPL boundary is structural.
//
// Threading per [2d §7.8] (RC#1 close): RPC handlers run on the engine
// executor (EngineConfig::executor) outside any session serialisation
// domain. The factory's `exec` argument (see ControlPlaneFactory below)
// is by default the engine executor; operators that want isolation pass
// a strand or sub-executor following the [2d §4.5] executor_override
// pattern. To reach session state, a handler invokes the C ABI (e.g.,
// fixpp_session_send), which itself posts onto the session strand via
// the FIXPP_REQUIRES_SESSION_LOCK reentrancy contract per [2i §4.10].
// ──────────────────────────────────────────────────────────────────────
class ControlPlane {
public:
    virtual ~ControlPlane() = default;

    // (1) Start the control plane. The implementation binds its listening
    //     endpoint (Unix socket / named pipe / TCP), wires its handlers
    //     to the engine via the opaque fixpp_engine_t*, and begins
    //     accepting RPCs. The function returns immediately after the
    //     listener is bound; the actual RPC-handling loop runs on the
    //     factory's `exec` argument (typically EngineConfig::executor
    //     per [2d §7.8]).
    //
    //     Cancellation: per [const §XI.2] / [2d §4.7] — on
    //     cancellation_type::total the awaitable completes with
    //     expected_t::unexpected{control_plane_start_cancelled}.
    //     Construction-time configuration errors (bad listen path,
    //     malformed cert_source for mTLS) surface as
    //     control_plane_config_invalid via the [arch §5.3]
    //     construction-time carve-out routed through trap_throw at the
    //     factory entry point per [2g §4.2] precedent.
    //
    //     Returns asio::awaitable<expected_t<void>>.
    [[nodiscard]] virtual asio::awaitable<core::expected_t<void>>
        start() = 0;

    // (2) Stop the control plane gracefully. Stops accepting new RPCs;
    //     waits up to ControlPlaneConfig::stop_timeout for in-flight
    //     RPCs to complete; cancels (with control_plane_stream_cancelled)
    //     any streaming RPC still active at the timeout; closes the
    //     listener; releases all gRPC server resources.
    //
    //     Cancellation: on cancellation_type::total during stop, the
    //     awaitable completes with
    //     expected_t::unexpected{control_plane_stop_cancelled}; the
    //     listener is closed unconditionally (the function is a stop
    //     operation; a cancellation just means "skip the graceful
    //     part").
    //
    //     Returns asio::awaitable<expected_t<void>>.
    [[nodiscard]] virtual asio::awaitable<core::expected_t<void>>
        stop() = 0;

    // (3) Synchronously query the control plane's current health. This
    //     is the engine-side accessor for the gRPC standard health-check
    //     protocol — the impl's Health RPC handler typically returns the
    //     same HealthStatus this method produces.
    //
    //     Reentrancy: thread-safe — concurrent readers from any thread
    //     (the impl publishes the four-field HealthStatus through a
    //     seqlock-protected snapshot per N-P1-3 / §4.3.3; readers never
    //     observe a torn read).
    //
    //     Returns expected_t<HealthStatus>; an error is returned only if
    //     the impl is itself in an unrecoverable state (the seqlock
    //     publisher invariant broken — control_plane_internal_error per §6.6).
    [[nodiscard]] virtual core::expected_t<HealthStatus>
        health() const noexcept = 0;
};

// ──────────────────────────────────────────────────────────────────────
// ControlPlaneFactory — engine-anchor factory entry point per [arch §6]
// rule 4. The factory is held by EngineConfig::control_plane_factory
// (Appendix D §D.2 amends [2d §4.4]). The factory's make() method is
// invoked at Engine::open after the engine's IO executor is bound.
//
// Argument shape (Opus N-P1-2 close): make() takes an explicit
// `fixpp_engine_t*` handle in addition to the mr / exec / cfg trio.
// 2h's TransportFactory::make() is (exec, ssl_cfg, mr) — three args
// without an engine handle — because a Transport instance is bound to
// a *session*, not an engine; the transport reaches the engine only
// indirectly through the session strand. ControlPlane is engine-scoped
// (one per engine; see §1.2 and [arch §4.11]) — the factory needs the
// opaque engine handle so the impl can wire its handlers to the C ABI
// at construction time (see [2i §4.2] opaque-handle catalogue). The
// 4-arg shape is a deliberate narrow divergence from 2h; the divergence
// surface is one parameter and is documented here.
// ──────────────────────────────────────────────────────────────────────
class ControlPlaneFactory {
public:
    virtual ~ControlPlaneFactory() = default;

    // Construct a ControlPlane bound to the given opaque engine handle.
    //
    // mr      — the engine arena per [arch §5.2]; the impl uses this for
    //           all long-lifetime allocations (the gRPC server's per-call
    //           arena allocator, internal hash maps, etc.).
    // engine  — the opaque fixpp_engine_t* handle the impl invokes the
    //           C ABI through. The handle's lifetime is engine lifetime;
    //           the factory does not own it.
    // exec    — the executor the impl runs RPC handlers on. Per [2d §7.8]
    //           the engine binds this to EngineConfig::executor by default
    //           (handlers run on the engine executor outside any session
    //           serialisation domain). Operators that want isolation pass
    //           a strand or sub-executor here following [2d §4.5]'s
    //           executor_override pattern.
    // cfg     — value-typed config per §4.2.
    //
    // noexcept; an internal throw is routed through [2a §4.2] trap_throw
    // and surfaces as expected_t::unexpected{control_plane_factory_failed}
    // per the [2e §4.4] / [2g §4.2] / [2h §4.7] precedent (every plugin
    // factory's make() is noexcept).
    [[nodiscard]] virtual core::expected_t<std::unique_ptr<ControlPlane>>
        make(std::pmr::memory_resource* mr,
             fixpp_engine_t*            engine,
             asio::any_io_executor      exec,
             ControlPlaneConfig const&  cfg) noexcept = 0;
};

}  // namespace fixpp::service
```

**Pure-virtual count:** 3 (`start`, `stop`, `health`). Under the `[const §XIV.2]` cap of 5 with two slots of headroom (the post-v1 additions tracked in §10 Q5 are: a dynamic auth-token rotation hook, and an RPC re-mapping hook for service-mesh deployments). No Gate-A justification paragraph needed.

**Concept-vs-virtual:** chosen virtual because (a) `ControlPlane` is held by `std::unique_ptr<ControlPlane>` in the engine (one per engine, owned for engine lifetime); (b) the awaitable return types of `start` / `stop` are hard to express as a concept without leaking the implementation's promise type (mirrors the `[2g §4.1]` / `[2h §4.1]` rationale); (c) the impl is in the AGPL-boundary's downstream half — the engine's C++ side never sees a typed `ControlPlane` reference outside `service/`-side code, only the `unique_ptr` it holds — virtual is the natural shape.

**Annotations at the declaration site (per `[2b §6.4]` / `[2g §4.1]` declaration-site precedent — at the abstract base, not at override sites):**

- `[[nodiscard]]` on `start` / `stop` / `health` (every `expected_t<T>`-returning method per the surface convention).
- `[[clang::lifetimebound]]` is **not applicable** — `ControlPlane` does not return any view-typed accessors; `HealthStatus` is value-typed and copy-returned. Per the 2g RC#1 rule (every view-returning accessor carries the annotation at the abstract-base declaration), the *non-applicability* is the relevant compliance statement here.
- `noexcept` on `health` (synchronous; pure read of a seqlock-protected snapshot per N-P1-3).

### §4.2 `fixpp::service::ControlPlaneConfig` — value-typed config

```cpp
// include/fixpp/service/control_plane.h continued

namespace fixpp::service {

// Endpoint variant: Unix domain socket / named pipe / TCP.
struct unix_endpoint  { std::pmr::string socket_path; };       // Linux
struct named_pipe_endpoint { std::pmr::string pipe_name; };    // Windows
struct tcp_endpoint   { std::pmr::string host; std::uint16_t port; };

using listen_endpoint_t = std::variant<unix_endpoint, named_pipe_endpoint, tcp_endpoint>;

struct ControlPlaneConfig {
    // ── Endpoint (required) ──────────────────────────────────────────
    // The wire endpoint the control plane binds. Default (per [arch §8.1])
    // is unix_endpoint{"/var/run/fixppd.sock"} on Linux,
    // named_pipe_endpoint{"\\\\.\\pipe\\fixppd"} on Windows. Operators
    // explicitly opt into TCP per the security carve-out in §1.1.
    listen_endpoint_t       listen_endpoint;

    // ── Auth (mTLS-only by default per §1.1) ─────────────────────────
    // When require_mtls = true (default), the gRPC server uses the
    // engine's cert_source to bind an SSL_CTX. When false, the operator
    // accepts the OS-level trust boundary (kernel-namespace-isolated
    // Unix socket); the field carries a [[deprecated]] marker per
    // [const §XII.5] no-implicit-default precedent — explicit opt-out
    // emits a compile-time diagnostic.
    bool                    require_mtls = true;

    // ── DoS bounds per §1.1 ──────────────────────────────────────────
    std::uint32_t           max_in_flight_rpcs   = 64;
    std::uint32_t           max_message_bytes    = 4 * 1024 * 1024;   // 4 MiB
    std::uint32_t           max_metadata_bytes   = 8 * 1024;          // 8 KiB
    std::chrono::milliseconds  stop_timeout      = std::chrono::seconds{5};

    // ── Streaming RPCs per-stream backpressure (RC#7 close) ─────────
    // For StreamMetrics / StreamLogs / StreamSessionEvents, the per-
    // stream queue depth. Backpressure rule: CLOSE-ON-OVERFLOW. If a
    // slow consumer cannot drain at the rate the engine produces, the
    // stream is closed gracefully with control_plane_stream_overflow
    // (§6.6); the consumer reconnects to resume.
    //
    // Close-on-overflow is consistent with [const §XV.15] (which bans
    // drop-oldest on app/session message paths; the bandwidth-control
    // mechanism here is "consumer must drain or be cut off") and is
    // distinct from the [const §XIII.2] permission to drop-oldest on
    // observability paths — that permission is permissive, not
    // mandatory. v1.0 picks close-on-overflow for visibility (drop-
    // oldest hides loss; close surfaces it). See §4.8 / §6.4 / §9
    // seam #11 for the consumer-side behaviour.
    std::uint32_t           stream_queue_depth   = 1024;

    // ── Per-call arena (per [arch §5.2]) ────────────────────────────
    // The per-RPC arena that the gRPC server uses for request/response
    // buffers. NULL means "engine provides default" — typically the
    // EngineConfig::default_message_resource per [2d §4.4].
    std::pmr::memory_resource*  rpc_arena = nullptr;
};

}  // namespace fixpp::service
```

**Notes:**

- All `std::pmr::string` fields source their allocator from the `mr` argument the factory's `make(...)` is called with per `[arch §6]` rule 4.
- `listen_endpoint` is a `std::variant`, so the operator picks exactly one of the three transport shapes; the variant exhausts the v1.0 listen-endpoint surface (no other transport types in v1.0 per §2 non-goal #2).
- `max_in_flight_rpcs` / `max_message_bytes` / `max_metadata_bytes` / `stop_timeout` are operationally-tuned defaults; operators raise per their deployment profile per the `[const §XII.5]` no-implicit-default precedent (an explicit raise is required; the engine does not auto-grow).

### §4.3 Method-by-method contract

Every method's signature, preconditions, postconditions, cancellation behaviour, error variants, threading, and reentrancy.

#### §4.3.1 `start()`

```cpp
[[nodiscard]] virtual asio::awaitable<core::expected_t<void>> start() = 0;
```

- **Preconditions.** Construction has completed — the impl has been instantiated via `ControlPlaneFactory::make`. The opaque `fixpp_engine_t*` the factory was instantiated with is in OPEN state (`Engine::open` has succeeded). The control-plane executor (the `exec` argument to `make`) is alive.
- **Postconditions on success.** The listener is bound at `cfg.listen_endpoint` and accepting RPCs. The RPC-handling loop is running on `exec`. `health()` returns `HealthStatus{engine_status: serving, ...}`.
- **Postconditions on failure.** No listener is bound; no RPC loop is running; `health()` returns `HealthStatus{engine_status: not_serving, ...}`. The impl is safe to destruct without a `stop()` call.
- **Cancellation.** Per `[const §XI.2]` / `[2d §4.7]` — on `cancellation_type::total` the awaitable completes with `expected_t::unexpected{control_plane_start_cancelled}` (§6.6). Any partial state (e.g., a half-bound listener) is rolled back synchronously before the cancellation completes.
- **Error variants.** `control_plane_config_invalid` (bad listen endpoint, malformed mTLS config), `control_plane_listen_failed` (OS-level bind failure — port in use, permission denied, socket path not writeable), `control_plane_start_cancelled` (cancellation), `control_plane_factory_failed` (impl-internal trap from `[2a §4.2]` `trap_throw`).
- **Threading.** Runs on the control-plane executor (the `exec` argument to `make`). Does NOT run on a session strand or the engine's IO executor.
- **Reentrancy.** Single-call: invoking `start()` while a previous `start()` is in flight is undefined; the engine invokes `start()` exactly once at `Engine::open` time.

#### §4.3.2 `stop()`

```cpp
[[nodiscard]] virtual asio::awaitable<core::expected_t<void>> stop() = 0;
```

- **Preconditions.** `start()` has completed successfully. The impl is currently serving (or is in a failure state — `stop()` is a recovery path).
- **Postconditions on success.** Listener is closed; no in-flight RPC remains; the impl is safe to destruct. `health()` returns `HealthStatus{engine_status: not_serving, ...}` until destruction.
- **Postconditions on cancellation.** Listener is closed (cancellation does not block teardown); any in-flight RPC at cancellation time is summarily cancelled (the streaming-cancel taxonomy of `[2d §4.7]` phase 2).
- **Cancellation.** On `cancellation_type::total`, `expected_t::unexpected{control_plane_stop_cancelled}`. The listener is closed unconditionally before the awaitable completes.
- **Error variants.** `control_plane_stop_cancelled` (cancellation); `control_plane_internal_error` (impl-internal invariant broken at stop time — extremely rare).
- **Threading.** Runs on the control-plane executor.
- **Reentrancy.** Single-call: invoking `stop()` while a previous `stop()` is in flight is undefined; the engine invokes `stop()` exactly once at `Engine::close` time.

#### §4.3.3 `health()`

```cpp
[[nodiscard]] virtual core::expected_t<HealthStatus> health() const noexcept = 0;
```

- **Preconditions.** The impl is constructed (post-`make`); `start()` need not have completed (a pre-`start` `health()` returns `HealthStatus{engine_status: not_serving, ...}`).
- **Postconditions (N-P1-3 close).** Returns a transactionally-consistent snapshot of the impl's state. The four `HealthStatus` fields are read through a `seqlock`-protected snapshot on the impl side (mirroring the `[2d §4.4]` engine-level fallback `trace_context` precedent). A consumer never observes a torn read — `engine_status = serving` paired with a stale `open_sessions` count, etc., does not happen. Consecutive calls may return different values (the snapshot reflects engine state at one instant; the next call reflects state at a later instant). Optimisation: if `is_always_lock_free<HealthStatus>` is true on the target platform, a single `std::atomic<HealthStatus>` load is permitted.
- **Cancellation.** Synchronous; no cancellation slot is consumed.
- **Error variants.** `control_plane_internal_error` only (and only if the impl's seqlock or atomic counters are in an unrecoverable state — a bug). v1.0 default impl never returns an error from `health()`.
- **Threading.** Thread-safe; may be called from any thread without external synchronisation.
- **Reentrancy.** Thread-safe; concurrent calls from many threads are correct (the seqlock supports any number of concurrent readers; the writer is the engine's own observability fan-out).

### §4.4 Header layout (`include/fixpp/service/control_plane.h`)

The header follows the `[arch §8]` AGPL-boundary structural rule: it depends on `core/` only. Concretely:

**MUST include (whitelist):**
- `<asio/awaitable.hpp>` (for `asio::awaitable<T>` per `[const §XI.1]`).
- `<asio/cancellation_type.hpp>` (for `asio::cancellation_type` per `[const §XI.2]`).
- `<asio/any_io_executor.hpp>` (for the `exec` argument to `make`).
- `<memory>` (for `std::unique_ptr` and friends).
- `<memory_resource>` (for `std::pmr::memory_resource` and `std::pmr::string` per `[arch §5.2]`).
- `<string_view>` (for `std::string_view` accessors).
- `<variant>` (for `listen_endpoint_t`).
- `<chrono>` (for `std::chrono::milliseconds` / `std::chrono::seconds`).
- `<fixpp/core/expected.hpp>` (for `core::expected_t<T>` per `[arch §4.1]`).
- `<fixpp/core/error.hpp>` (for the `error::control_plane_*` variants per §6.6).

**MUST NOT include (forbidden):**
- `<fixpp/wire/...>` — the wire parser is engine-internal per `[arch §2.3]`.
- `<fixpp/session/...>` — the session FSM is engine-internal.
- `<fixpp/dict/...>` — the dictionary loader is engine-internal.
- `<fixpp/transport/...>` — the transport interface is engine-internal (the control plane reaches the transport via the C ABI, never directly).
- `<fixpp/tls/...>` — TLS lives engine-internally. v1.0 ControlPlane does not invoke `cert_source` reload via the proto schema (RC#5: `ReloadCertSource` is deferred to v1.x); the AGPL-boundary structural rule remains binding regardless.
- `<fixpp/log/...>`, `<fixpp/otel/...>` — log + OTel are engine-internal.
- `<fixpp/tap/...>` — the tap is engine-internal.
- Any `<fixpp/service/<other>.h>` (none exist in v1.0; reserved for forward compat).

The forward declaration of `fixpp_engine_t` (and `fixpp_session_t` if a future signature names it) at the top of the header is via an `extern "C"` block — the header does NOT include `<fix/c_api.h>` directly because that would pull in the entire C-ABI surface to every consumer of the interface header. Implementations include `<fix/c_api.h>` in their `.cpp` files.

**Enforcement.** `tools/check_layers.py` (named at `[arch §8]` and owned by the architecture; first-landing tracked in §10 Q10 — the tool does not yet exist in the repo, mirroring the `tools/check_alloc.py` forward-reference pattern across 2a/2b/2c/2d/2e/2f/2g/2h test seams) runs at build configure time and CI Tier 1; it grep-scans `include/fixpp/service/control_plane.h` for any forbidden include and fails the build on hit. The lint also scans `service/grpc/*.cpp` / `service/grpc/*.h` for any `#include <fixpp/X/...>` where `X != service`. (§9 seam #6.)

### §4.5 `[[clang::lifetimebound]]` on view-returning accessors

**Not applicable.** `ControlPlane` declares no view-returning accessors:

- `start()` / `stop()` return `asio::awaitable<expected_t<void>>` — no view.
- `health()` returns `expected_t<HealthStatus>` — `HealthStatus` is value-typed; no view.

The interface intentionally avoids view-typed returns to keep the header narrow and the lifetime contract simple. Streaming RPC outputs (`StreamMetrics` records, `StreamLogs` records) are protobuf messages whose lifetimes are bounded by the gRPC server's per-call arena — they never leak through the C++ interface, only across the wire.

If a future v1.x version adds a view-typed accessor, it follows the `[2b §6.4]` / `[2g §4.1]` declaration-site annotation precedent — `[[clang::lifetimebound]]` declared at the abstract-base method, not at override sites (per the 2g RC#1 close).

### §4.6 Default gRPC implementation surface (`service/grpc/`)

The default impl is `fixpp::service::grpc_control_plane`, with translation-units under `service/grpc/`. v0.2 fixes the file layout but not the per-translation-unit detailed shape (that's an implementation concern bounded by the contracts in §4.1 / §4.2 / §4.7).

**File layout (v1.0 surface — RC#5 close; rotation handlers deferred to v1.x):**

```
service/
├── proto/
│   └── fixpp_control.proto         # Proto schema; 2j-owned
├── grpc/
│   ├── grpc_control_plane.h        # private header (NOT in install set);
│   │                               # forward-declares grpc_control_plane.
│   ├── grpc_control_plane.cpp      # ControlPlane interface impl.
│   ├── grpc_session_handlers.cpp   # OpenSession / CloseSession / Configure.
│   ├── grpc_stream_handlers.cpp    # StreamMetrics / StreamLogs / StreamSessionEvents.
│   ├── grpc_health_handler.cpp     # Health (gRPC standard).
│   └── grpc_factory.cpp            # grpc_control_plane_factory + make_grpc_control_plane_factory().
└── fixppd/
    ├── main.cpp                    # daemon entry point (consumes via C ABI + service-iface).
    └── ...
```

(`grpc_pinset_handlers.cpp` is not in the v1.0 layout; v1.x adds it once the rotation surface is published per §10 Q1 / Q9.)

**Include discipline (per §4.4 / `[arch §8]`):**

Every `.cpp` under `service/grpc/` MUST include exactly:
- `<fixpp/service/control_plane.h>` (the public interface header — `service-iface` target).
- `<fix/c_api.h>` (the C ABI — `fixpp::capi` target).
- gRPC C++ headers (`<grpcpp/grpcpp.h>` etc.).
- C++ standard headers as needed.

It MUST NOT include any `<fixpp/X/...>` where `X != service`. CI enforces (§9 seam #6).

**Linking discipline (per `[arch §7.4]`):**

The CMake target `fixpp::service-grpc` links:
- `fixpp::service-iface` (header-only, exposing only `<fixpp/service/...>`).
- `fixpp::capi` (the C-ABI consumer target — exposes `<fix/c_api/...>` only, not `<fixpp/...>`).
- gRPC C++ runtime (external; via Conan).

It does NOT link `fixpp` (the C++ engine umbrella). The CMake property check per `[arch §7.4]` fails the build if `fixpp` accidentally appears in `fixpp::service-grpc`'s link interface.

**`fixppd` daemon target:**

`fixppd` is the main daemon binary per `[arch §4.11]`. It links:
- `fixpp::service-iface` (interface).
- `fixpp::capi` (the C ABI).
- `fixpp::service-grpc` (the default gRPC impl).
- gRPC, OpenSSL (for the gRPC TLS path; reaches engine certs via the C ABI cert-bridge — see §10 Q1).

It does NOT link `fixpp` (the C++ engine umbrella).

**Exception window (per `[arch §5.3]` / `[2i §5.2]`):**

Every gRPC handler is wrapped in a per-handler try/catch that catches all exceptions (the gRPC C++ runtime *can* throw — `std::bad_alloc`, gRPC's own `Status` exception conventions). The catch translates to a gRPC `Status::INTERNAL` response with a generic error message (the actual exception's `what()` is logged via the engine's Logger but NOT returned to the consumer to avoid leaking implementation details). C-ABI calls into the engine from inside the handler return `fixpp_error_t`; a non-OK code is translated to a gRPC `Status` per the §6.6 mapping.

```cpp
// service/grpc/grpc_session_handlers.cpp (sketch — engineering-detail; not normative)
::grpc::Status grpc_control_plane_impl::OpenSession(
        ::grpc::ServerContext* ctx,
        const fixpp::v1::OpenSessionRequest* req,
        fixpp::v1::OpenSessionResponse* resp) {
    try {
        // Build a fixpp_session_config_t from req via the C ABI's session-config setters.
        // (The session_config helpers are owned by 2j's session-handler module via the C ABI.)
        fixpp_session_config_t* cfg = nullptr;
        fixpp_error_t e = fixpp_session_config_create(&cfg);
        if (e != FIXPP_ERR_OK) return translate_to_grpc_status(e);
        // ... populate cfg from req ...
        fixpp_session_t* session = nullptr;
        e = fixpp_session_open(engine_handle_, cfg, &session);
        fixpp_session_config_destroy(cfg);
        if (e != FIXPP_ERR_OK) return translate_to_grpc_status(e);
        resp->set_session_id(session_id_for(session));
        return ::grpc::Status::OK;
    } catch (const std::exception& ex) {
        engine_log_fatal("OpenSession handler escaped exception: ", ex.what());
        return ::grpc::Status(::grpc::StatusCode::INTERNAL, "internal error");
    } catch (...) {
        engine_log_fatal("OpenSession handler escaped foreign exception");
        return ::grpc::Status(::grpc::StatusCode::INTERNAL, "internal error");
    }
}
```

The exception window is bounded by the per-handler try/catch; an exception NEVER crosses into the engine because every engine-side call is a C-ABI call (`extern "C"` `noexcept` thunks per `[2i §5.2]`).

### §4.7 Proto schema reference (`service/proto/fixpp_control.proto` v1.0)

The proto schema is on the architectural stability tier `[arch §9.3]` ("Stable from v1.0: ... the gRPC control-plane proto"). Once published in v1.0, every numeric field tag / message / enum value is frozen per the SemVer-by-analogy rules in §4.7.1.

**v1.0 RPC surface (the binding shape; rotation RPCs deferred to v1.x per RC#5 — see §10 Q1 / Q9):**

```proto
syntax = "proto3";
package fixpp.v1;

service Control {
    // ── Session lifecycle ────────────────────────────────────────────
    rpc OpenSession (OpenSessionRequest) returns (OpenSessionResponse);
    rpc CloseSession (CloseSessionRequest) returns (CloseSessionResponse);
    rpc Configure (ConfigureRequest) returns (ConfigureResponse);

    // ── Observability streams ────────────────────────────────────────
    rpc StreamMetrics (StreamMetricsRequest) returns (stream MetricRecord);
    rpc StreamLogs (StreamLogsRequest) returns (stream LogRecord);
    rpc StreamSessionEvents (StreamSessionEventsRequest) returns (stream SessionEvent);

    // ── Health (gRPC standard health-check shape; see grpc.health.v1) ─
    rpc Health (HealthRequest) returns (HealthResponse);

    // RotatePinset and ReloadCertSource are v1.x post-spec extensions
    // (RC#5 close — see §10 Q1 / Q9 + §3.12). Reserving the RPC names
    // is a documentation matter only; proto3 has no `reserved rpc` so
    // adding them in v1.x is a MINOR-bump-shape additive change per
    // §4.7.1.
}

// Message-shape commitments (the field tags are pinned per §4.7.1
// proto-evolution rules; concrete field-tag numbers are produced at
// sign-off and locked in the §D.4 audit-trail file):
//
//   OpenSessionRequest carries: sender_comp_id, target_comp_id,
//     begin_string, security_profile (enum), dictionary_id (string;
//     references EngineConfig::dictionaries), dialect_overlay_id
//     (optional), heartbeat_interval (optional duration), and a free-
//     form attributes map for v1.x extensibility.
//   CloseSessionRequest carries: session_id (string), force (bool —
//     true = phase-2 immediate; false = phase-1 graceful per [2d §4.7]).
//   ConfigureRequest is currently empty (§2 non-goal #5; reserved for
//     v1.x mid-session-mutable knobs — see §10 Q8 disposition + §4.7.1
//     additive-only justification).
//   StreamMetricsRequest carries: meter_filter (string; OTel-
//     compatible filter per [const §XIII.1]), sample_interval_ms
//     (uint32). Bounded by ControlPlaneConfig::stream_queue_depth.
//   StreamLogsRequest carries: level_filter (enum), correlated_trace_id
//     (optional bytes — restrict to this trace_id only). Bounded by
//     stream_queue_depth.
//   StreamSessionEventsRequest carries: session_id_filter (repeated
//     string; empty = all sessions). Bounded by stream_queue_depth.
//   HealthRequest follows grpc.health.v1.HealthCheckRequest shape;
//     HealthResponse follows grpc.health.v1.HealthCheckResponse with
//     the additional fixpp-specific fields from HealthStatus per §4.1.
//
// Streaming records (concrete field tags pinned at sign-off in §D.4
// per N-P2-6 close):
//
//   MetricRecord { timestamp_ns; instrument_kind; metric_name; value;
//                  attributes; trace_id (16 bytes); span_id (8 bytes);
//                  trace_flags (1 byte); clock_scope discriminator
//                  (record-schema owner: 2k per [arch §5.7]; v1.0 reserves
//                  a tag pending 2k's schema lock — 2d §7.9 names the
//                  producer-side semantics that the discriminator
//                  reflects); ... }
//   LogRecord    { timestamp_ns; level; format_id; args; trace_id
//                  (16 bytes); span_id (8 bytes); trace_flags (1 byte);
//                  clock_scope discriminator (record-schema owner: 2k
//                  per [arch §5.7]; v1.0 reserves a tag pending 2k's
//                  schema lock — 2d §7.9 names the producer-side
//                  semantics that the discriminator reflects); ... }
//   SessionEvent { timestamp_ns; session_id; event_kind; reason
//                  (optional); fix_fields (map); trace_id (16 bytes);
//                  span_id (8 bytes); ... }
```

**Notes on the schema:**

- The proto is **stable**: once a field tag is published, it never changes. Audited via `tools/abi_history/proto_v1.txt` (append-only file; precedent is `[2i §4.3]` `tools/abi_history/error_codes_v1.txt`). Appendix D §D.3 queues the file's creation at sign-off.
- Stream RPCs are server-streaming (the engine drives; the client consumes). Bidirectional streaming is not used in v1.0 — the consumer's request is a one-shot subscription specification, and the engine streams thereafter.
- **OTel correlation fields are wire-pinned (N-P2-6 close).** Each `MetricRecord`, `LogRecord`, and `SessionEvent` carries explicit `trace_id` (16 bytes), `span_id` (8 bytes), `trace_flags` (1 byte) fields at fixed proto tags so the consumer's OTel backend correlates without manual stitching. The seam #10 verification (§9) checks the round-trip against these tags.
- **No `idempotency_key` field in v1.0 (N-P2-2 close).** v0.1 published `idempotency_key` while §10 Q2 deferred engine-side support to v1.x; v0.2 resolves the contradiction by dropping the field from v1.0. Adding `idempotency_key` at v1.x is a MINOR-bump-shape additive change per §4.7.1.
- The proto's message field tags follow the convention: `1-15` for required-shape fields (single-byte tag in protobuf wire format), `16+` for optional / extension fields. Tag `100+` reserved for v1.x additions.

### §4.7.1 Proto evolution rules (NEW in v0.2 per RC#6)

`[arch §9.3]` declares the gRPC control-plane proto "Stable from v1.0" but does not pin a SemVer mapping for proto evolution. v0.2 authors the rules locally as the proto-owner doc; the rules are an architectural-stability story (this section + `[arch §9.3]`), **not** a constitutional ABI-policy claim. (RC#6 close: v0.1's "constitution-level amendment per `[const §X]`" rhetoric was an over-claim — `[const §X]` is "ABI Policy" for the C ABI per the constitution's own headings, not a proto-stability article. The dropped claim is replaced with the rules below; no constitutional amendment is owed.)

The rules apply to `service/proto/fixpp_control.proto` v1.0 and any future proto file under `service/proto/`:

1. **Field-tag stability.** Once published in a tagged release, a numeric field tag never changes meaning — neither the field name nor the field type may be altered. Removal of a published field is a MAJOR change.
2. **RPC additions.** Adding a new RPC (e.g., the v1.x `RotatePinset` / `ReloadCertSource` per §10 Q1 / Q9) is a MINOR-bump-shape additive change. Existing consumers continue to work; new consumers gain access to the new RPC.
3. **RPC removals.** Removing a published RPC is a MAJOR-bump-shape break. v1.0 will not remove any RPC from the surface in §4.7.
4. **Message-shape changes.** Adding an optional field to an existing message is a MINOR-bump-shape additive change. Changing a field's type is a MAJOR break. Removing a field is a MAJOR break. Renaming a field is a MAJOR break (proto field tags are stable, but consumer code sites typically reference field *names* — a rename breaks consumer code at compile / runtime).
5. **Enum value additions.** Adding a new enum value at an unused numeric position is a MINOR additive change. Renaming an enum value is a MAJOR break (same rationale as field renames). Removing or repurposing an existing enum value is a MAJOR break.
6. **Audit trail.** Every published field tag, RPC name, and enum value is recorded in `tools/abi_history/proto_v1.txt` (append-only; precedent `[2i §4.3]` `tools/abi_history/error_codes_v1.txt`). Appendix D §D.3 queues the file's creation. CI verifies no published symbol is removed or repurposed (the same shape `tools/check_capi_occupancy.sh` uses for the C-ABI numeric blocks, adapted for proto symbols).

The SemVer-bump-shape language (MINOR / MAJOR) maps to the **library** SemVer track per `[arch §9.2]` and adopts the SemVer-shape rules of `[const §X.4]` by analogy without claiming `[const §X]` covers proto evolution.

**`Configure` reserved-empty justification.** `Configure` ships in v1.0 as an empty RPC (§2 non-goal #5 limits its scope; the only mid-session-mutable knob — pinset rotation — is deferred per RC#5). Per rule (2) above, v1.x can add fields to `ConfigureRequest` and `ConfigureResponse` as MINOR-bump additive changes; per rule (3), v1.0 cannot remove the RPC at v1.x without a MAJOR break. Keeping `Configure` reserved-empty in v1.0 is the minimum-surface choice that preserves v1.x extensibility — a v1.0 omission would force a MAJOR bump if any mid-session-mutable knob lands at v1.x. **Operationally**, an empty `ConfigureRequest` parses to a zero-byte protobuf message; the handler returns an empty `ConfigureResponse`; the call consumes one slot of the consumer's `max_in_flight_rpcs` budget per §1.1 (a malicious consumer cannot use `Configure`'s low parse cost as a DoS vector — the cap is enforced before the handler dispatches; see §6.4).

### §4.8 Health / metrics / logs surface (per `[const §XIII.1]` / `[const §XIII.2]` / `[arch §4.8]`)

**Health (gRPC standard health-check protocol):**

The `Health` RPC follows the gRPC standard `grpc.health.v1.Health` service interface — every gRPC tooling that knows how to query health (e.g., `grpc_health_probe` in Kubernetes liveness probes) works out-of-the-box. The fixpp-specific extension exposes the `HealthStatus` fields from §4.1 in addition to the standard serving-status enum.

**Metrics (`StreamMetrics`):**

The `StreamMetrics` RPC bridges the engine's `MeterProvider` per `[arch §4.8]` to a gRPC stream. The wire format is OTLP-equivalent — each `MetricRecord` carries a metric name, instrument kind (counter / gauge / histogram), value, timestamp, and the standard set of OTel attributes (resource, instrumentation scope). The 2k design doc (forthcoming) owns the schema; 2j is the wire layer.

**Important per `[const §XIII.1]`:** the engine ALSO exports metrics via OTLP-direct (push to an OTel collector) AND Prometheus (pull endpoint). `StreamMetrics` is a **third** export path for consumers that prefer the gRPC stream shape. All three paths share the same `MeterProvider`; per `[const §XIII.4]` "Same sink interface backs OTel log export and file/stderr sinks. No double-write paths" — the underlying provider produces records once, and each export path subscribes.

**Logs (`StreamLogs`):**

Equivalent to `StreamMetrics` but for log records. Each `LogRecord` carries timestamp, level, format-id, captured args, plus the standard OTel correlation fields (`trace_id` / `span_id` per `[const §XIII.3]`). 2k owns the record schema; 2j is the wire layer.

**Session events (`StreamSessionEvents`):**

A separate stream for FIX session-FSM events (Logon, Logout, Reject, Heartbeat-out-of-window). Each `SessionEvent` carries `session_id`, event_kind, timestamp, optional reason field, and the FIX message's relevant fields (e.g., `Logout`'s `Text(58)`). The session-module Phase-4 spec owns the event schema; 2j is the wire layer.

**Observability backpressure (RC#7 close — close-on-overflow, not drop-oldest):**

When a slow consumer cannot drain a streaming RPC at the engine's production rate, the per-stream queue bounded by `ControlPlaneConfig::stream_queue_depth` triggers `control_plane_stream_overflow` (§6.6) and **the stream is closed gracefully** — the consumer receives a non-`OK` gRPC `Status` and reconnects to resume. This is **close-on-overflow**, not drop-oldest.

- `[const §XV.15]` bans drop-oldest on app/session message paths. Close-on-overflow is consistent with that ban: the bandwidth-control mechanism is "consumer must drain or be cut off"; no record is silently discarded after publishing.
- `[const §XIII.2]` *permits* (does not require) drop-oldest on observability paths. v1.0 picks close-on-overflow because drop-oldest hides loss from the consumer, while close-on-overflow surfaces the loss event explicitly via the `control_plane_stream_overflow` variant — operationally cleaner for SRE / ops consumers who want to know they fell behind.
- The OTel correlation fields enumerated above (N-P2-6) include `trace_id` / `span_id` on every record — when a stream closes via overflow, the consumer can correlate the last-emitted record's trace context with engine-side logs to diagnose the slow-consumer condition.

---

## §5 Public C ABI

The default gRPC impl talks to the engine through `<fix/c_api.h>` per `[arch §8]`. **v0.2 introduces no new C-ABI symbols** (RC#1 / RC#5 close — see Appendix C v0.1 → v0.2 entry); the v1.0 RPC surface is implementable through 2i v0.3's existing C-ABI plus the in-process engine state the impl already holds via `fixpp_engine_t*`.

### §5.1 Existing C-ABI surface 2j consumes (by RPC)

| RPC | C-ABI calls invoked | Owner doc |
|---|---|---|
| `OpenSession` | `fixpp_session_config_create`, `fixpp_session_config_set_*` family, `fixpp_session_open`, `fixpp_session_config_destroy` | `[2i §1.2]` shape; **session-module Phase-4 spec** behaviour (CA-005) |
| `CloseSession` | `fixpp_session_close` (graceful or force flag) | `[2i §1.2]` shape; Phase-4 (CA-005) |
| `Configure` | (v1.0 empty; reserved per §4.7.1 for v1.x additive expansion) | n/a in v1.0 |
| `StreamMetrics` | The metric-consumer registration is owed to **2k** (forthcoming). Until 2k publishes a C-ABI registration symbol, 2j's gRPC handler reaches the meter records via an engine-internal accessor that the gRPC handler is not allowed to touch directly (per `[arch §8]`); v1.0 implementation lands once 2k publishes the symbol. The 2j *wire shape* (proto schema) is locked in §4.7 regardless. | Owned by **2k** (forthcoming) |
| `StreamLogs` | The log-sink registration is owed to **2k** per `[arch §5.7]` async-logger sink interface (the 2k symbol shape is already named in `[2i §1.2]` non-goal #5 as 2k-territory). 2j carries the records across the wire only. | Owned by **2k** (forthcoming) |
| `StreamSessionEvents` | The session-event-consumer registration is owed to the **session-module Phase-4 spec** (the FSM owner). 2j carries the records across the wire only. | Owned by **session-module Phase-4 spec** |
| `Health` | The engine's session-table count, in-flight-RPC count, and drain status are visible to the impl via the `fixpp_engine_t*` opaque-handle accessors that 2i v0.3's existing surface already publishes (per `[2i §4.2]`). The exact accessor functions are 2i's call; 2j is the consumer. The impl populates `HealthStatus` from those accessors and publishes it through a seqlock per N-P1-3 / §4.3.3. | `[2i §4.2]` shape |

(There is no `RotatePinset` / `ReloadCertSource` row above — RC#5: those RPCs are deferred to v1.x; see §10 Q1 / Q9.)

### §5.2 No new C-ABI symbols introduced by 2j (RC#1 / RC#5 close)

v0.1 introduced seven new C-ABI symbols and three new PoD types into 2i's surface (`fixpp_engine_health`, `fixpp_pinset_add`, `fixpp_pinset_remove`, `fixpp_engine_reload_cert_source`, `fixpp_engine_register_metric_consumer` / `_unregister_metric_consumer`, `fixpp_engine_register_session_event_consumer` / `_unregister_session_event_consumer`, plus `fixpp_health_status_t` / `fixpp_metric_record_t` / `fixpp_session_event_t`) — each queued as a 2i v0.4 amendment via Appendix D §D.3. The Opus adversarial review confirmed (RC#1 / RC#5) that this construction silently overrode `[2i §2]` non-goal #6 (for the rotation symbols) and unnecessarily forced cross-doc-amendment work for symbols that are properly owed by the **session-module Phase-4 spec** (session-event registration), **2k** (metric-consumer / log-sink registration), or 2i v0.4 (the rotation surface, when v1.x adds it). v0.2 cuts every NEW symbol from §5.2:

- **`fixpp_pinset_add` / `fixpp_pinset_remove` / `fixpp_engine_reload_cert_source`** — dropped because RC#5 cuts the `RotatePinset` and `ReloadCertSource` RPCs from v1.0. v1.x will revisit; the path is a 2i v0.4 amendment owned by 2i (not 2j).
- **`fixpp_engine_register_metric_consumer` / `_unregister_metric_consumer`** — out of scope for 2j. The metric-consumer registration belongs to 2k per `[arch §5.7]` / `[2i §1.2]` non-goal #5 ("`c_api/log.h` and `c_api/otel.h` are owned by **2k**"). 2j's `StreamMetrics` *wire shape* is locked here in §4.7; the C-ABI symbol that backs it is owed to 2k.
- **`fixpp_engine_register_session_event_consumer` / `_unregister_session_event_consumer`** — out of scope for 2j. Session-event semantics belong to the session-module Phase-4 spec; the C-ABI registration symbol shape is that doc's call. 2j's `StreamSessionEvents` wire shape is locked here in §4.7; the backing C-ABI symbol is owed to the Phase-4 spec.
- **`fixpp_engine_health`** — dropped from 2j as a NEW symbol. 2i v0.3's existing `fixpp_engine_t*` accessor surface (per `[2i §4.2]` opaque-handle catalogue) provides the engine-internal counters; the impl reads them and populates `HealthStatus` directly. The `Health` RPC handler does not need a single coalesced "health snapshot" C-ABI thunk in v1.0 — that is a v1.x optimisation if benchmarks show the per-counter calls are too expensive.
- **`fixpp_health_status_t` / `fixpp_metric_record_t` / `fixpp_session_event_t` PoD types** — dropped from 2j. The PoD shape for any of these is owed to 2k or the Phase-4 spec, not 2j (per N-P3-1: 2j is a wire-layer consumer, not a shape owner).

### §5.3 v1.0 buildability

v0.2's RPC surface is **buildable against 2i v0.3 as published** (no 2i v0.4 amendment is owed). The cross-doc edits queued by 2j sign-off (Appendix D) are now narrowly scoped:

- §D.1 SVC-005 catalogue row in `library/spec/feature-catalogue.md` (NEW row).
- §D.2 `EngineConfig::control_plane_factory` field in `library/.specify/2d-threading.md` (one field, not two — the `control_plane_executor` field is dropped per RC#1).
- §D.3 `tools/abi_history/proto_v1.txt` (NEW append-only audit trail file) plus the `FIXPP_ERR_CTRL_CONFIG = 900` / `FIXPP_ERR_CTRL_RUNTIME = 901` numeric assignments + `tools/abi_history/error_codes_v1.txt` two-line append (the 2i numeric block at `[900, 999]` is already reserved per `[2i §1.1]` line 65; this is value assignment within the reserved block, not a 2i amendment).
- §D.4 `library/spec/coverage-index.md` "Service" section append.

(See Appendix D for the byte-faithful Before / After blocks per RC#3 close.)

---

## §6 Behavioral contract

### §6.1 Allocation / exceptions / threading rules

**Allocation.**
- The control-plane impl reads from the per-RPC arena (`ControlPlaneConfig::rpc_arena` per §4.2) for request/response buffers; resets it at RPC completion.
- Long-lived state (the gRPC server's internal hash maps, the listener socket / pipe, the SSL context for mTLS) lives in the engine arena (the `mr` argument to `make`).
- Per `[const §VIII.5]`, the parse-to-`fromApp` window is **NOT** in the control-plane scope — control-plane RPCs run on `EngineConfig::executor` (the engine executor) per `[2d §7.8]`, not on a session strand. On the engine executor, allocation is permitted (the RPC handler is cold-path), bounded by `[arch §5.2]` PMR discipline.
- Cross-boundary: when an RPC handler invokes a C-ABI function that reaches a session strand (e.g., `fixpp_session_close`), the strand-side execution honours its own `[const §VIII.5]` zero-allocation discipline. The control-plane handler's allocation is on the engine executor, *not* on the strand.

**Exceptions.**
- Per `[arch §5.3]` and §1 Goal 8: the gRPC C++ runtime can throw; the per-handler try/catch boundary catches all exceptions and translates to a gRPC `Status::INTERNAL` response.
- No exception ever crosses into engine code — every engine call is a C-ABI call (`extern "C"` `noexcept` thunk per `[2i §5.2]`).
- Construction-time exceptions (bad listen path, malformed config) are caught by the factory's `make(...)` per `[2g §4.2]` / `[2h §4.7]` precedent (every plugin factory's `make` is `noexcept`); surface as `expected_t::unexpected{control_plane_factory_failed}`.

**Threading.**
- The control-plane impl runs on `EngineConfig::executor` per `[2d §7.8]` (RC#1 close — no separate `control_plane_executor` field; operators that want isolation pre-wrap the engine executor before passing it to the factory per the `[2d §4.5]` `executor_override` pattern; see §6.5).
- For v1.0 default, the gRPC C++ runtime's `ServerCompletionQueue` polling is bound to the engine executor; gRPC server callbacks are serialised by the gRPC runtime per the `ServerCompletionQueue::Next()` model.
- A handler that needs session state posts onto the session strand via a C-ABI call (`fixpp_session_close` etc.). The handler's coroutine suspends; resumes on the engine executor; returns the gRPC response.
- Streaming RPCs run on the same executor; the per-stream queue (bounded by `stream_queue_depth`) is filled by the engine's observability fan-out (a separate thread per `[const §XIII.2]` async-logger pattern) and drained by the gRPC server callback running on the engine executor.

### §6.2 Cancellation taxonomy

Aligns with `[2d §4.7]` per-mode effect table; extends with the streaming-RPC variants.

| Operation | Phase-1 graceful | Phase-2 total | RPC-level cancel (gRPC half-close) |
|---|---|---|---|
| `start()` | runs to completion | `control_plane_start_cancelled`; partial state rolled back | n/a (control-plane is not the cancellation source) |
| `stop()` | runs to completion (the operation IS the graceful stop) | `control_plane_stop_cancelled`; listener forced closed | n/a |
| `health()` | n/a (synchronous) | n/a | n/a |
| `OpenSession` handler | runs to completion (a graceful close of the engine waits for in-flight `OpenSession` to finish before stopping the listener) | RPC handler cancels via `ServerContext::TryCancel`; the underlying `fixpp_session_open` C-ABI is **not** cancellation-aware mid-call (construction-time per `[2i §5.2]`) — so an in-flight `fixpp_session_open` runs to completion; the gRPC response is cancelled before the consumer gets it; the session is *opened* on the engine side but the consumer doesn't know its ID — recovery: `Health` lists the orphan, the consumer reconciles. (Tracked in §10 Q3 — the rough edge here may want a v1.x clean-up RPC.) | as phase-2 |
| `CloseSession` handler | the underlying `fixpp_session_close(graceful=true)` runs to completion (phase-1 of the session's two-phase close per `[2d §4.7]`); RPC returns | the underlying `fixpp_session_close` is forced (`graceful=false`); RPC returns; the per-mode cancellation effect surfaces in the session's pending operations per `[2d §4.7]` | as phase-2 |
| `StreamMetrics` / `StreamLogs` / `StreamSessionEvents` | runs to graceful end (the engine drains pending records and sends an empty-stream-end marker) | abrupt close: `control_plane_stream_cancelled`; consumer gets a non-OK `Status` | abrupt close on RPC cancel; same behaviour |

(Rows for `RotatePinset` and `ReloadCertSource` are absent in v0.2 — RC#5: those RPCs are deferred to v1.x; see §10 Q1 / Q9.)

### §6.3 Latency Tier 1 ceilings

The control plane is **not** a hot-path. Ceilings are loose; primary perf concern is that the control plane never stalls a session strand or the IO executor. Per the §1.1 magnitudes:

| Operation | Tier 1 ceiling (p99) | Notes |
|---|---|---|
| `Health` RPC | ≤ 5 ms | Read of seqlock-protected `HealthStatus` snapshot per §4.3.3 (N-P1-3). |
| `OpenSession` RPC (warm cache) | ≤ 50 ms | Cached dictionary, cert source, plugin factories. The benchable steady-state path. |
| `OpenSession` RPC (cold cache) | ≤ 500 ms (informational) | First call after engine start: cold dictionary load per `[2c §6.5]` (~50 ms), cold `cert_source` `SSL_CTX` construction per `[2g §6.5]` (~100 µs–10 ms), plugin-factory `make` cascade. Not a Tier 1 hard ceiling; bench Tier 1 measures the warm-cache path only. (N-P2-4 close.) |
| `CloseSession` (graceful) RPC | ≤ 1 s | Bounded by FIX `Logout` exchange + `tls_close_timeout` per `[2h §4.1]`. Pathologically up to `tls_close_timeout` if peer is unresponsive. |
| `CloseSession` (force) RPC | ≤ 50 ms | Phase-2 cancellation per `[2d §4.7]` is bounded. |
| `StreamMetrics` per-record | ≤ 100 µs | Bench seam #2. |
| `StreamLogs` per-record | ≤ 100 µs | Bench seam #2 (combined with metrics seam — same code path). |
| `StreamSessionEvents` per-record | ≤ 100 µs | Bench seam #2. |
| `start()` / `stop()` | ≤ 100 ms | Listener bind / unbind + gRPC server setup / teardown. |

(Rows for `RotatePinset` and `ReloadCertSource` are absent in v0.2 — RC#5; they will return at v1.x.)

**Cross-strand handoff budget (N-P3-6 close).** Where a handler dispatches onto a session strand via a `FIXPP_REQUIRES_SESSION_LOCK` C-ABI thunk per `[2i §4.10]` (e.g., `fixpp_session_close` from the `CloseSession` handler), the post-onto-strand-and-await-completion shape adds **≤ 100 µs p99** under engine idle and **≤ 5 ms p99** under engine load. This budget is included in the per-RPC ceilings above; bench measurement uses the `[2d §6.5]` `cancellable_dispatch` precedent point. The handler's executor identity (engine executor per `[2d §7.8]`) does NOT leak into the session strand — the `[2i §4.10]` thunk posts onto the strand internally and the handler's coroutine resumes on the engine executor.

CI fails on > 5% regression vs `bench/baselines/control_plane/` per `[const §VIII.2]`; bench seam #2 (§9) covers the latency-sensitive surface.

### §6.4 DoS surface — concrete numeric ceilings (recap of §1.1)

- `max_in_flight_rpcs = 64`. Server-side cap; 65th call rejected with `RESOURCE_EXHAUSTED` before handler runs (the gRPC server's flow-control hook fires *before* the handler dispatches, so cheap-to-parse RPCs like the empty-`Configure` cannot be used to bypass the cap — see §4.7.1 closing paragraph).
- `max_message_bytes = 4 MiB`. gRPC default; honoured.
- `max_metadata_bytes = 8 KiB`. gRPC default; honoured.
- `stream_queue_depth = 1024`. Per-stream backpressure bound; overflow triggers **close-on-overflow** with `control_plane_stream_overflow` (RC#7 close — §4.2 / §4.8 / §6.6).
- `stop_timeout = 5 s`. Bounded graceful stop; cancellation-after-timeout is forced.

A consumer's malicious behaviour (flooding RPCs, sending oversized messages, opening many concurrent streams) is bounded by these numeric ceilings; the engine logs the rejection at `warning` level via the async logger; further abuse at the same consumer triggers a TLS-level disconnect (mTLS enforcement is lower-layer per §1.1).

**Streaming RPC backpressure semantics (RC#7 close — close-on-overflow vs drop-oldest).** The streaming RPCs `StreamMetrics`, `StreamLogs`, `StreamSessionEvents` use **close-on-overflow**: when the per-stream queue (bounded by `stream_queue_depth`) cannot accept a new record from the engine's observability fan-out because the consumer has not drained at production rate, the stream is closed gracefully with `control_plane_stream_overflow` and the consumer reconnects to resume. This is *not* drop-oldest — drop-oldest would silently evict the oldest queued record and keep the stream open. The choice is operational: drop-oldest hides loss; close-on-overflow surfaces it via the `control_plane_stream_overflow` variant + the OTel correlation fields on the last-emitted record (per §4.7 / §4.8). `[const §XV.15]` bans drop-oldest on app/session message paths and is satisfied here by construction (no drop-oldest is performed); `[const §XIII.2]` permits drop-oldest on observability paths but does not require it. The seam #11 verification (§9) checks the close-on-overflow behaviour.

### §6.5 Threading — handlers run on the engine executor per `[2d §7.8]` (RC#1 close)

Per `[2d §7.8]` ("Control-plane handlers (gRPC `OpenSession`, `CloseSession`, `Configure`, `StreamMetrics`, `StreamLogs`) **run on the engine executor** *outside* any session serialisation domain"), the v1.0 default impl runs RPC handlers on `EngineConfig::executor` (the engine executor). v0.1's introduction of a dedicated `EngineConfig::control_plane_executor` field was a `[2d §7.8]` override that would have required a 2d v0.5 amendment with its own Gate A pass; v0.2 drops the field and aligns to the engine-executor model.

**Operator opt-in to a separate executor (without amending 2d).** The factory's `exec` parameter (§4.1 `ControlPlaneFactory::make`) is the binding point. Operators that want the control plane on a separate pool wrap the engine executor before passing it to the factory — for example, by binding a strand or an `asio::thread_pool::executor_type` to the factory at construction time per the `[2d §4.5]` `executor_override` pattern. The engine binds the wrapped executor as the handlers' `exec`; the wrapping is invisible to `[2d §7.8]`'s contract because the engine's IO executor remains the binding source. This pattern preserves the no-amendment property of v0.2 while letting an operator isolate the handler workload from the IO executor.

**Threading invariant.** A control-plane RPC handler cannot block, stall, or starve a session strand. Handlers that need session state cross into the session strand via the `FIXPP_REQUIRES_SESSION_LOCK` C-ABI post-onto-strand discipline (per `[2i §4.10]`). ⚠️ **The executor-topology sentence that stood here is DELETED (2026-08-29), not refreshed — see "Appendix Z" at the END of this file. Placed there deliberately: line-number citations point INTO this document and an insertion here would rot every one below it.**

**Cross-strand handoff.** A handler that needs session state invokes a C-ABI call (e.g., `fixpp_session_close` for the `CloseSession` handler). The C-ABI thunk for `FIXPP_REQUIRES_SESSION_LOCK` symbols (per `[2i §4.10]`) posts onto the session strand internally; the handler's coroutine suspends; resumes on the engine executor. The handler's executor identity does NOT leak into the session strand. Per `[2d §7.8]` the engine-fallback `current_trace_context` awaiter resolves the engine snapshot (no `session_executor` wrapper is in play on the engine executor) — this is the path control-plane handlers naturally take for OTel correlation.

**Coordination with `[2d §4.5]` `SessionConfig`.** When `OpenSession` is invoked, the gRPC handler builds a `SessionConfig` from the protobuf request and invokes `fixpp_session_open`. CA-005's protobuf-shape projection lives in `[2j §4.7]` `OpenSessionRequest`; the underlying C-ABI session-config-builder symbols are 2j-shape-co-owned with 2i + the Phase-4 session-module spec per §1.2's non-owned list (2j carries the protobuf wire shape only — the C-ABI signatures and FSM behaviour are not 2j's). The handler translates protobuf fields to `SessionConfig` fields per `[2d §4.5]`. The protobuf request carries only fields the operator can sensibly set from outside the engine (sender/target CompID, security profile, dictionary id, dialect overlay id, heartbeat interval); fields like `executor_override`, `clock_override`, PMR arenas, tap consumer are **not** exposed via the proto — the engine picks them per the `EngineConfig` defaults. (A consumer who needs custom executors, clocks, or arenas runs the engine in-process via the C++ API; they're not the control-plane consumer.)

### §6.6 Errors introduced by this design

Per the per-doc-prefix discipline established by `[2b §6.4]` (`FIXPP_ERR_WIRE_*`), `[2c §6.4]` (`FIXPP_ERR_DICT_*`), `[2d §6.7]` (`FIXPP_ERR_THREAD_*`), `[2e §6.4]` (`FIXPP_ERR_STORE_*`), `[2f §6.5]` (`FIXPP_ERR_SYNC_*`), `[2g §6.6]` (`FIXPP_ERR_TLS_*`), `[2h §6.6]` (`FIXPP_ERR_TRANSPORT_*`): 2j adopts the prefix **`FIXPP_ERR_CTRL_*`** for its C-ABI mapping target, owned by 2i per `[2i §1.1]`'s reserved block at **`[900, 999]`** (the explicit reservation made by 2i for "2j control plane" — see `[2i §1.1]` line 65: "RESERVED: 2j control plane (FIXPP_ERR_CTRL_*)").

Numeric range allocation. Per the per-doc-prefix convention each doc owns a non-overlapping numeric block in the engine `error` enum's variant ordering; 2j claims a contiguous block of **8 variants** under the `control_plane_*` prefix at `[900, 999]`.

| `fixpp::core::error` variant | Source section | Remediation class |
|---|---|---|
| `control_plane_config_invalid` | §4.3.1 — bad listen endpoint, malformed mTLS config, contradictory flags (`mtls = false` on TCP endpoint, etc.). Covers construction-time validation per `[arch §5.3]` carve-out. | Configuration error — fix the `ControlPlaneConfig` and retry. |
| `control_plane_listen_failed` | §4.3.1 — OS-level listen bind failure (port in use, permission denied, socket path unwritable, SELinux refusal). | Configuration / environment error — fix the listen path / port / permissions. |
| `control_plane_factory_failed` | §4.1 — `ControlPlaneFactory::make` PMR allocation throw routed through `[2a §4.2]` `trap_throw`, or impl-internal throw at construction. Mirrors `[2g §4.2]` / `[2h §4.7]` factory-failure precedent. | Configuration / arena bug; raise arena cap. |
| `control_plane_start_cancelled` | §4.3.1 / §6.2 — `start()` awaitable cancelled via `cancellation_type::total`. Joins the `[2d §6.5] cancellable_dispatch → dispatch_aborted`, `[2g §6.6] tls_load_cancelled`, `[2h §6.6] transport_*_cancelled`, `[2f §6.5] sync_lock_aborted` cancellation group. | Cancellation; not an error in most contexts. |
| `control_plane_stop_cancelled` | §4.3.2 / §6.2 — `stop()` awaitable cancelled. Same group as `control_plane_start_cancelled`. | Cancellation. |
| `control_plane_stream_cancelled` | §4.3 / §6.2 — a streaming RPC (`StreamMetrics`, `StreamLogs`, `StreamSessionEvents`, or the per-session phase of `ReloadCertSource`) was cancelled via the consumer's gRPC half-close or via `cancellation_type::total`. Same group. | Cancellation. |
| `control_plane_stream_overflow` | §4.2 / §4.8 / §6.4 — the per-stream queue overflowed (slow consumer); the stream is closed-on-overflow with this variant (RC#7 close: this is *not* drop-oldest — `[const §XV.15]` bans drop-oldest on app/session paths and is satisfied here by construction; `[const §XIII.2]` permits but does not require drop-oldest on observability paths, and v1.0 picks close-on-overflow for visibility). | Operational error — consumer drains slower than the engine produces; consumer reconnects to resume. |
| `control_plane_internal_error` | §4.3.3 / §4.6 — impl-internal invariant broken (extremely rare; would surface only if the gRPC default impl's translation layer caught a foreign exception that escaped a handler's try/catch boundary, or if `health()`'s atomic counters drift inconsistent). Engine-internal logger emits a fatal-level record before return. | Bug; report. |

(8 variants.)

C-ABI mapping per the per-doc-prefix discipline (the numeric block `[900, 999]` is already reserved by 2i v0.3 at `[2i §1.1]` line 65 — value assignment within the block is not a 2i amendment, only an audit-trail append):

- configuration errors (`control_plane_config_invalid`, `control_plane_listen_failed`, `control_plane_factory_failed`) → **`FIXPP_ERR_CTRL_CONFIG = 900`**;
- runtime errors (`control_plane_internal_error`, `control_plane_stream_overflow`) → **`FIXPP_ERR_CTRL_RUNTIME = 901`**;
- cancellation (`control_plane_start_cancelled`, `control_plane_stop_cancelled`, `control_plane_stream_cancelled`) reuses the existing **`FIXPP_ERR_CANCELLED = 1`** per `[const §XI.2]` / `[2i §4.9]`.

Appendix D §D.3 queues the byte-faithful audit-trail append to `tools/abi_history/error_codes_v1.txt` and the two-line addition to `[2i §4.3]`'s numeric-layout table.

---

## §7 Integration with adjacent modules

### §7.1 With 2c — dictionary version selection at `OpenSession`

The `OpenSession` RPC's `dictionary_id` field references one of the `EngineConfig::dictionaries` per `[2c §4.9]` `dict::version_registry`. The handler resolves the id (a string like `"FIX.4.4"` or `"FIX.5.0SP2"`) to a `std::shared_ptr<const Dictionary>` via the C ABI — the handler does NOT know about `dict::version_registry` directly (engine-internal); the C ABI's `fixpp_session_config_set_dictionary_id` thunk does the lookup. Mid-session dialect-overlay swap is **rejected categorically** per `[2c §7.2]`; 2j's `Configure` RPC v1.0 surface does NOT expose a dialect-overlay swap.

### §7.2 With 2d — session executor / SessionConfig population / Configure mid-session policy

Per `[2d §4.4]` / `[2d §4.5]` / `[2d §7.8]`: the engine-anchor `EngineConfig` carries the IO executor, clock, dictionaries, default plugin selections; per-session `SessionConfig` overrides as needed. 2j extends `EngineConfig` with **one field** — `control_plane_factory` (engine-anchor, the only ControlPlane per engine). RPC handlers run on `EngineConfig::executor` per `[2d §7.8]`; v0.2 does NOT add a `control_plane_executor` field (RC#1 close — see §6.5). Appendix D §D.2 queues the one-field amendment.

The `OpenSession` RPC handler builds a `SessionConfig` from the protobuf request — only the operator-settable fields per §6.5. The threading mode is forced to `per_session_strand` (the safe default per `[2d §4.5]`); a control-plane consumer cannot opt into `direct_executor` (which requires the user attestation `already_serialized_executor = true` — a control-plane RPC consumer cannot make that attestation about an executor it doesn't own).

The `Configure` RPC's mid-session policy: **v1.0 does not admit mid-session changes via `Configure`.** The `[arch §5.6]` carve-outs for mid-session-mutable knobs (the pinset; trace-context update through `Engine::set_engine_trace_context` per `[2d §4.4]`) are exposed via dedicated RPCs (`RotatePinset`) or are not exposed at all. `Configure` is reserved for v1.x extensions.

### §7.3 With 2e — `MessageStore` factory injection at `OpenSession`

Per `[2e §4.4]`: `MessageStoreFactory` is an engine-anchor + session-override pair. The `OpenSession` RPC's protobuf does NOT expose a `store_factory` field — the factory is selected from `EngineConfig::default_store_factory` (the engine's pre-loaded default). A consumer who needs a custom store runs the engine in-process via the C++ API; they're not the control-plane consumer.

The per-session arena (per `[2e §6.1.4]` durable-before-transmit invariant) is engine-internal; the C-ABI `fixpp_session_open` honours it.

### §7.4 With 2f — none directly

`fixpp::sync::async_mutex` per `[2f §4.1]` is a session-internal primitive; the control plane does not consume it. The control-plane impl's own internal locking (e.g., the gRPC server's hash-map of in-flight RPCs) is the gRPC C++ runtime's concern; 2j does not constrain.

### §7.5 With 2g — `RotatePinset` / `ReloadCertSource` RPCs (DEFERRED to v1.x per RC#5)

`[2g §7.7]` (quoted in §3.12) describes the v1.0 hand-off shape: the control-plane gRPC schema *will* carry `RotatePinset` and `ReloadCertSource`; the handler lives in 2j; the action is `Pinset::add` / `Pinset::remove` (per `[2g §4.3]`) or session close-and-reopen (per `[arch §5.6]`). 2g publishes the contract on `Pinset` rotation thread-safety — `add` / `remove` are `[2g §6.5]` add-then-remove with no atomic-swap shortcut.

**v1.0 deferral (RC#5 close).** The v1.0 cross-doc state has no AGPL-legal path for these RPCs:

- `[2i §2]` non-goal #6 explicitly declines the C-ABI rotation surface in v1.0 (`fixpp_pinset_add` / `fixpp_pinset_remove` / `fixpp_cert_source_reload`).
- `[arch §8]` forbids `service/grpc/*.cpp` from including `<fixpp/tls/...>`, so the gRPC handler cannot reach `Pinset::add` / `Pinset::remove` directly in C++.
- The v0.1 attempt to bridge the gap (introducing the C-ABI rotation symbols in 2j and queuing a 2i v0.4 amendment) is RC#1 / RC#5: 2j does not own 2i's surface; the rotation surface is owed to 2i v0.4 with its own Gate A pass.

v1.0 ControlPlane therefore omits both RPCs from the proto schema (§4.7); they are deferred to v1.x as post-spec extensions tracked in §10 Q1 (`ReloadCertSource`) and §10 Q9 (`RotatePinset`). The `[2g §7.7]` paragraph remains a published forward-compat hook — the discharge is owed to 2i v0.4 + a 2j v1.x amendment, not to 2j v1.0.

### §7.6 With 2h — graceful drain on `CloseSession`

Per `[2h §6.4]` two-phase close + `[2h §7.6]` (quoted in §3.12): `CloseSession(graceful=true)` triggers `Session::close(graceful)` per `[2d §4.7]` two-phase close; phase-1 attempts a FIX `Logout` exchange under a child cancellation state; phase-2 fires `cancellation_type::total`, propagating to in-flight transport ops and the heartbeat timer. `CloseSession(force=true)` skips phase-1 and goes straight to phase-2.

The cancellation taxonomy in §6.2 row "CloseSession handler" honours the `[2h §6.4]` per-mode effect table.

The TLS bidi-shutdown timeout default (`tls_close_timeout = 1 s` per `[2h §4.1]`) bounds graceful-close latency for TLS sessions; the `CloseSession` RPC's gRPC deadline accommodates.

(The `[2h §7.6]` paragraph also describes `RotatePinset` and `ReloadCertSource` triggers — those are v1.x scope per RC#5; the `CloseSession` paragraph is the v1.0 discharge.)

### §7.7 With 2i — every RPC handler invokes the C ABI

Per `[2i §4.2]` opaque-handle catalogue: every gRPC handler invokes the engine through `fixpp_engine_t*` (held by the impl since `make`) and `fixpp_session_t*` (resolved by the handler from a session_id string in the protobuf request — the engine-internal session-id-to-handle map is owned by 2j as part of the gRPC default impl's state).

Per `[2i §4.9]` cancellation translation rule: every cancellation outcome from the engine surfaces as `FIXPP_ERR_CANCELLED` at the C ABI; 2j's gRPC handler translates `FIXPP_ERR_CANCELLED` to a gRPC `Status::CANCELLED` per the mapping table in §6.6.

Per `[2i §5.2]` thunk split: 2j's handlers consume both flavours — `OpenSession` invokes `fixpp_session_open` (construction-time per `[2i §5.2]` whitelist; throws caught and translated); `fixpp_session_send` is steady-state (any escape is a bug, `std::abort`-ed at the engine layer; the gRPC handler's try/catch never sees this — the abort is unconditional).

The opaque-handle lifetime discipline per `[2i §4.2]` is honoured: the `fixpp_engine_t*` outlives the ControlPlane (the engine owns the ControlPlane); `fixpp_session_t*` lifetime is bounded by `fixpp_session_close` (the gRPC handler that holds a `fixpp_session_t*` between RPCs is the impl's responsibility — it must invalidate the cached handle on `CloseSession`).

### §7.8 With 2k (forthcoming) — `StreamLogs` / `StreamMetrics` / OTel correlation

Per `[const §XIII.1]` / `[const §XIII.2]` / `[const §XIII.3]` / `[arch §5.7]`: 2k owns the async logger + OpenTelemetry surface. 2j's `StreamLogs` and `StreamMetrics` RPCs are wire-layer bridges that consume 2k's `Logger` and `MeterProvider` records via the C ABI.

The C-ABI symbols 2j depends on are **owed by 2k** (forthcoming) — RC#1 close: v0.1's introduction of `fixpp_engine_register_metric_consumer` etc. as 2j-side NEW symbols was a 2k-territory override. The expected symbol shapes are:

- `fixpp_engine_register_log_sink(sink, userdata)` — async-logger sink registration per `[arch §5.7]`. Owed to 2k.
- `fixpp_engine_register_metric_consumer(callback, userdata)` — meter-record consumer registration. Owed to 2k.
- The corresponding unregister symbols per the standard register/unregister pair shape.

The wire shape (proto schema) is locked here in §4.7 regardless; the C-ABI binding lands when 2k publishes the symbols.

Per `[const §XIII.3]` strand-stored trace-context (the `[2d §7.8]` engine-fallback path for engine-executor handlers): every log record carries `trace_id` / `span_id` from the originating session strand's `session_local<trace_context>` slot (for session-scope records) or from the engine-level atomic snapshot (for engine-scope records). The streaming `LogRecord` protobuf carries these fields verbatim — concrete proto field tags are pinned in §4.7 (N-P2-6 close) and audit-trailed in §D.3. Consumer OTel backends correlate without manual stitching. Seam #10 (§9) verifies the round-trip.

### §7.9 With 2l (forthcoming) — explicit non-overlap with the data plane

Per `[arch §8.1]` / `[arch §8.2]` / `[SYN §3.6 #20]`: iceoryx2 (SVC-002) and the control plane solve **different problems**. iceoryx2 is the data plane (high-volume one-way SHM pub/sub of FIX message bytes); the control plane is request/response administrative RPC (session create/destroy/configure/observe; low-volume).

Concrete partition (per RC#2 close — see §1.2 and Appendix A):
- **2j (control plane, SVC-001 / SVC-004 / SVC-005)** = request/response RPCs over gRPC. Session lifecycle, observability streams. Bounded-rate. AGPL-boundary on the C ABI. Rotation RPCs (`RotatePinset`, `ReloadCertSource`) are deferred to v1.x per RC#5.
- **2l (data plane, SVC-002 / SVC-003)** = high-volume one-way pub/sub of FIX message bytes over iceoryx2 (or in-process tap consumer for non-cross-process cases). Owned by 2l per `[arch §8.2]` ("Topic shape, ownership semantics, backpressure, fallback when iceoryx2 isn't running — all in **2l**") / `[const §XIV.3]` opt-in rule.

The two surfaces are **explicitly non-overlapping**. v1.0's ControlPlane does NOT publish a "stream tapped FIX messages" RPC — that's iceoryx2's job. (§10 Q4 tracks the question of whether non-HFT consumers might want a gRPC streaming path for FIX message bytes; the v1.0 answer is no.)

The session-tap consumer API per `[SYN §3.6 #22]` is owned by 2l. 2j may *reference* `StreamSessionEvents` (Logon/Logout/Reject/Heartbeat structured events) which are session-FSM-level events, **not** the FIX message bytes themselves; the schema is owned by the session-module Phase-4 spec.

---

## §8 PMR — recap

Storage classes for 2j-owned data:

| Storage | Lifetime | Holds | Reset by |
|---|---|---|---|
| **Engine arena (per `[arch §5.2]`)** | Engine lifetime | The `ControlPlane` instance itself; the gRPC server's listener socket; the SSL_CTX for mTLS auth (if enabled); the gRPC server's internal hash maps (in-flight RPC table, session-id-to-handle map). | `~Engine` |
| **`ControlPlaneConfig::rpc_arena` (per-RPC arena)** | One RPC handler invocation | Request protobuf message bytes; response protobuf message bytes; per-handler temporaries. | RPC handler completion |
| **Per-stream arena (one per active streaming RPC)** | Stream lifetime (from RPC start to consumer half-close or engine drain) | The bounded queue of `MetricRecord` / `LogRecord` / `SessionEvent` protobuf bytes; per-stream gRPC state. | Stream completion |
| **gRPC's own internal allocator** | gRPC C++ runtime lifetime | The gRPC C++ runtime's connection state, completion queues, internal slabs. The runtime accepts a custom allocator via gRPC's `ResourceQuota` / `MemoryAllocator` API; v1.0 default impl wires this to the engine arena (an explicit `gpr_allocator` shim sits between gRPC and the PMR resource). | gRPC server shutdown |

**Lifetime classes for non-arena objects:**

- **`ControlPlane` instance** — engine lifetime; held via `std::unique_ptr<ControlPlane>` in the engine.
- **`ControlPlaneFactory` instance** — engine-anchor lifetime; held via `std::unique_ptr<ControlPlaneFactory>` in `EngineConfig::control_plane_factory` per Appendix D §D.2.
- **`fixpp_engine_t*` (the C-ABI handle the ControlPlane sees)** — engine lifetime; the engine outlives the ControlPlane.
- **`fixpp_session_t*` cached by an RPC handler** — bounded by `fixpp_session_close`; the gRPC default impl invalidates cached handles on `CloseSession` and on `Engine::close`.

Per `[const §VIII.5]`: zero `new`/`delete` between parse and `fromApp`. The control plane does NOT touch this window — RPC handlers run on `EngineConfig::executor` per `[2d §7.8]`, NOT on a session strand. Allocation on the engine executor is permitted (the RPC handler is cold-path); allocation on a session strand is bounded by the strand's own `[const §VIII.5]` discipline (the strand-side accessor honours it).

The §9 seam #4 (allocation guard) verifies under `mallocnesia` on Linux/Clang Tier 1 that the inbound parse → `fromApp` chain does not touch the global heap from a control-plane RPC. The engine executor itself is intentionally NOT under the alloc-guard for control-plane RPCs (it's permitted to allocate from the engine arena and the per-RPC arena; global heap touches there are bench-time concerns, not constitutional invariants).

---

## §9 Test seams

Per `[arch §10]` requirement (4) and `[const §VII.4]`. v0.2 ships **12 seams** (≥ 10 brief minima; the extras cover the AGPL-boundary lint, the JSON-over-Unix-socket alt-impl exercise, the OTel correlation seam, and the proto stability audit-trail seam). Each seam is referenced by **name** per the `[2d §9]` / `[2g §9]` / `[2h §9]` / `[2i §9]` cross-referencing precedent. The v0.1 seam #12 ("Pinset rotation through `RotatePinset` RPC") is **retired** in v0.2 per RC#5 (the rotation RPC is deferred to v1.x); a new seam — **"Proto schema stability audit-trail"** — fills the slot per RC#6 (proto-evolution governance verification).

1. **Conformance corpus integration — control plane against canned RPCs.** Drive a recorded scenario through the `ControlPlane` interface (in-process, against a `mock_engine_t*` that records C-ABI calls): `OpenSession` → `StreamSessionEvents` → `CloseSession` → `Health`. Verify byte-for-byte the gRPC wire representation matches the proto schema and the C-ABI calls fire in the expected order. Lives in `tests/conformance/test_control_plane_integration.cpp`.

2. **Latency regression — `Health` / `OpenSession` (warm cache) / streaming records.** Google Benchmark on the warm-cache RPC handlers. Verify §6.3 warm-cache ceilings (the cold-cache `OpenSession` row is informational, not Tier 1). CI fails on > 5% regression. Lives in `bench/service/bench_control_plane_rpcs.cpp`.

3. **Fuzzer (parser-touching seam — random protobuf bytes through the gRPC handlers).** libFuzzer-driven random protobuf wire bytes feeding the gRPC server's `OpenSession` / `CloseSession` / `Configure` request paths; ASan + UBSan invariants; verify no crash, no UAF, no UB on adversarial inputs. The grpc C++ runtime's protobuf parser is the natural fuzz frontier; the handler's logic (post-parse, post-validation) is the second fuzz frontier. Lives in `tests/fuzz/fuzz_control_plane_protobuf.cpp`.

4. **Allocation guard on the inbound parse → `fromApp` chain — control-plane RPCs do not touch the global heap on the FIX message hot path.** `tools/check_alloc.py` + `mallocnesia` (Linux/Clang Tier 1 per the `[2a §9]` / `[2b §9]` / `[2d §9]` / `[2g §9]` / `[2h §9]` / `[2i §9]` precedent). Drive a scenario where a long-running `StreamMetrics` RPC is in flight while a session strand processes 10⁴ inbound messages; verify zero global-heap `new`/`delete`/`malloc` on the session strand's parse → `fromApp` chain — the engine executor's own allocations on the control-plane handler don't count, but they don't leak into the session strand. Lives in `tests/perf/test_control_plane_session_alloc_guard.cpp`.

5. **Cancellation propagation — every RPC handler cancels cleanly.** For each method (`OpenSession`, `CloseSession`, every `Stream*` RPC), issue the await inside a coroutine bound to the engine executor (per `[2d §7.8]`); fire the awaiter's cancellation slot before completion; verify the awaitable returns the appropriate `control_plane_*_cancelled` variant per §6.6. Run under both `per_session_strand` and `direct_executor` modes for any session strand the handler interacts with per `[2d §4.8]`. Includes the streaming-RPC half-close case (consumer calls `WritesDone`). Lives in `tests/service/test_control_plane_cancellation_propagation.cpp`.

6. **AGPL boundary lint — preprocessor-time include scan via `tools/check_layers.py`.** Three negative-case fixtures: (i) a `service/grpc/foo.cpp` that `#include <fixpp/wire/parser.h>` — lint must fail; (ii) a `service/grpc/foo.cpp` that `#include <fixpp/session/session.h>` — must fail; (iii) the public `include/fixpp/service/control_plane.h` that `#include <fixpp/session/session_config.h>` — must fail. Plus three positive-case fixtures: (i) `service/grpc/foo.cpp` that includes `<fixpp/service/control_plane.h>` + `<fix/c_api.h>` — must pass; (ii) the umbrella `<fix/c_api.h>` — must pass; (iii) `<fixpp/service/control_plane.h>` — must pass. Verifies the §4.4 / §4.6 AGPL-boundary structural rule by catching include-time leaks (header-only inline functions that don't emit symbols). Forward-references `tools/check_layers.py` (named at `[arch §8]`; first-landing tracked in §10 Q10 — the tool does not yet exist in the repo, mirroring the `tools/check_alloc.py` precedent). Complementary to seam #7 (which catches link-time leaks). Lives in `tests/ci/test_agpl_boundary_lint.sh` + fixtures under `tests/ci/fixtures/agpl_boundary_negative/` + `agpl_boundary_positive/`.

7. **C-ABI-only `fixppd` daemon-build seam — post-link symbol scan via `nm`.** A Tier 1 CI step that builds `fixppd` against `fixpp::capi` + `fixpp::service-iface` only — explicitly excluding `fixpp` (the C++ engine umbrella) from the link interface. Run `nm fixppd | grep -E "fixpp::wire::|fixpp::session::|fixpp::dict::"` — must produce zero matches (no engine internal symbols leak into the daemon binary). Verifies the §4.4 / §4.6 / `[arch §7.4]` linking discipline by catching transitive symbol leaks (which the seam #6 source-text scan would miss). **Complementary to seam #6** (N-P2-5 close): seam #6 catches inline-only header leaks at preprocessor time; seam #7 catches transitive symbol leaks at link time; both are required and the division of labour is intentional. Lives in `tests/ci/test_fixppd_no_cxx_engine_link.sh`.

8. **JSON-over-Unix-socket alternative impl exercise — pluggability validation.** Implement a minimal `json_unix_control_plane` (a 200-line POC) that satisfies the `ControlPlane` interface; instantiate it as the `EngineConfig::control_plane_factory`; run a shrunken conformance corpus subset (`OpenSession` + `Health` + `CloseSession`) against it; verify the engine works end-to-end with a non-gRPC ControlPlane. Verifies the SVC-005 "swap without rebuilding the engine" promise per `[arch §8.1]`. Lives in `tests/service/test_json_unix_alt_control_plane.cpp` + `tests/service/json_unix_control_plane.cpp` (the POC; not in the v1.0 install set — test-only).

9. **mTLS handshake + auth seam.** Construct a `grpc_control_plane` with `require_mtls = true`; bind to a Unix socket; have a test client connect with a valid cert + a valid key (test CA); verify Health succeeds. Repeat with an expired cert — verify the connection is refused at the TLS layer. Cross-doc with `[2g §9]` / `[2h §9]`. Lives in `tests/service/test_control_plane_mtls.cpp`.

10. **OTel correlation seam — `trace_id` / `span_id` / `clock_scope` round-trip through `StreamLogs`.** Configure the engine with an OTel `TracerProvider` that produces a known trace_id; trigger a session activity that emits log records (session-scope, `clock_scope = session`) plus an engine-scope record (e.g., listener-accept log; `clock_scope = engine`); subscribe to `StreamLogs`; verify each `LogRecord` protobuf carries the matching `trace_id` / `span_id` (the proto field tags pinned in §4.7 / N-P2-6) per `[const §XIII.3]` and that the `clock_scope` discriminator field reflects the producer side per `[2d §7.9]`. Verifies the OTel correlation contract holds across the gRPC wire. Lives in `tests/service/test_control_plane_otel_correlation.cpp`.

11. **Streaming-RPC close-on-overflow — bounded queue depth surfaces `control_plane_stream_overflow` (RC#7 close).** Configure `stream_queue_depth = 16`; subscribe to `StreamMetrics` with a deliberately slow consumer (sleeps 100 ms per drained record); have the engine produce > 16 records in < 100 ms; verify (a) the stream is closed (not drop-oldest — the next RPC the engine sends after the 17th queued record fails with `control_plane_stream_overflow` per §6.6 / §4.8), (b) the consumer receives a non-OK gRPC `Status` with the variant in the `Status::error_message`, (c) reconnecting and re-subscribing resumes from the engine's current production point (no replay of dropped records — there are no dropped records, the queue was bounded and the stream was cut). Lives in `tests/service/test_control_plane_stream_overflow.cpp`.

12. **Proto schema stability audit-trail (RC#6 close — replaces v0.1 seam #12).** A Tier 1 CI step that parses `service/proto/fixpp_control.proto` (e.g., via `protoc --descriptor_set_out=...`), extracts every published RPC name + every message field tag + name + type + every enum value, compares against `tools/abi_history/proto_v1.txt` (the §D.3 audit-trail file), and fails CI if (i) any published symbol has been removed (MAJOR break per §4.7.1 rule 3 / 4 / 5), (ii) any published field tag has changed type or numeric position (MAJOR break per §4.7.1 rule 1 / 4), (iii) any new symbol in the proto is missing from the audit-trail file (the file is the system of record; new symbols must be appended). Models on the `[2i §9]` seam #6 / #7 occupancy / forward-compat seams + the `tools/check_capi_occupancy.sh` precedent. Lives in `tests/ci/test_proto_v1_stability.sh` + `tools/check_proto_stability.sh`.

(12 seams. Brief minima 10. The two extras are #6 (AGPL boundary lint — the structural enforcement seam introduced by 2j) and #8 (JSON-over-Unix-socket alt-impl — the pluggability validation seam). The retired v0.1 seam — "Pinset rotation through `RotatePinset` RPC" — does not return until v1.x discharges the rotation surface per §10 Q9.)

---

## §10 Open questions

| # | Question | Disposition | Owner |
|---|---|---|---|
| 1 | **`ReloadCertSource` RPC + cert-source-bridge for the gRPC server's mTLS.** Two coupled concerns: (a) does `fixppd`'s gRPC server consume the *same* `cert_source` the engine's FIX sessions use, or a separate one? Reusing simplifies operations; decoupling reduces blast-radius. (b) When operators need to rotate the engine's cert source without a full restart, what RPC fires the close-and-reopen? `[2g §7.7]` describes the v1.x hand-off shape (a `ReloadCertSource` RPC that triggers session close-and-reopen per `[arch §5.6]`). v1.0 cuts the RPC per RC#5 (`[2i §2]` non-goal #6 declines the C-ABI rotation surface); the operational workflow for cert rotation in v1.0 is to restart `fixppd`. | DEFER both halves to v1.x. (a) Cert-source-bridge concrete shape DEFER to **2g** — the cert-source-bridge symbol lives at 2g's surface. (b) `ReloadCertSource` RPC DEFER to **2j v1.x** following a 2i v0.4 amendment that publishes `fixpp_engine_reload_cert_source` (or an explicit AGPL-boundary carve-out for `service/grpc/grpc_pinset_handlers.cpp` to include `<fixpp/tls/...>`). | 2g (bridge); 2j v1.x (RPC) |
| 2 | **Idempotency-key window.** v0.1 published an `idempotency_key` field on every request and deferred engine-side support to v1.x — N-P2-2 contradiction. v0.2 resolves: `idempotency_key` is **dropped from the v1.0 proto** (cleaner); v1.x adds it as a MINOR-bump-shape additive change per §4.7.1. The 60-second de-dup window remains an operational tuning question for v1.x. | DECIDED v0.2 — drop `idempotency_key` from v1.0; revisit at v1.x. | DECIDED v0.2; 2j |
| 3 | **`OpenSession` cancellation rough edge** — §6.2 notes that `OpenSession` cancellation produces an "orphan" session (the engine opened the session; the consumer doesn't know its ID). v1.0 leaves this as a rough edge: the consumer calls `Health` to enumerate orphans and reconciles. A v1.x clean-up RPC would pick up these orphans automatically. | DEFER to v1.x; operationally the warm-cache `OpenSession` latency is short enough (≤ 50 ms p99 per §6.3) that orphan races are very rare. | post-v1 follow-up; 2j |
| 4 | **gRPC streaming of FIX message bytes for non-HFT consumers?** Some consumers (Java front-office apps) want streaming FIX message bytes but won't run iceoryx2 (deployment complexity). Should 2j publish a `StreamFixMessages` RPC bridging to 2l's tap? | DEFER to **2l**. v1.0's separation is iceoryx2 = data plane; 2j = control plane only. | 2l |
| 5 | **Auth-token rotation via dynamic mTLS** — for service-mesh deployments where each consumer has a short-lived cert (e.g., 1-hour SVID from SPIFFE/SPIRE), the engine needs to rotate the gRPC server's certs without a full restart. v0.2 doesn't address this; the two reserved pure-virtual slots in `ControlPlane` (§4.1) are intended for a v1.x `RotateAuthToken(...)` and a `RemapRpcs(...)` hook. | DEFER to v1.x. | post-v1 follow-up; 2j |
| 6 | **Cross-engine federation** — managing N engines through a single control-plane endpoint. v1.0 is one-engine-per-listener; federation is post-v1. | DEFER to post-v1 if a real consumer needs it. | post-v1 follow-up |
| 7 | **Per-RPC throttling / rate-limiting** — beyond the v1.0 per-server `max_in_flight_rpcs = 64` cap. Per-consumer / per-RPC-method rate limiting is post-v1. | DEFER to post-v1 if a real DoS scenario is observed. | post-v1 follow-up |
| 8 | **`Configure` reserved-empty justification** — §2 non-goal #5 makes `Configure` v1.0-empty; the proto schema still publishes it. v0.1 justified the choice with "constitution-level amendment per `[const §X]` ABI-policy parity" — RC#6 / Codex P2#2 confirmed that `[const §X]` is "ABI Policy" for the C ABI, not the proto. v0.2 §4.7.1 (NEW) authors proto-evolution rules locally; under those rules, keeping `Configure` reserved-empty is the minimum-surface choice that preserves v1.x extensibility (a removal at v1.x would be a MAJOR break). | DECIDED v0.2 — keep as reserved-empty under §4.7.1 additive-only expansion path; the v0.1 `[const §X]`-amendment rhetoric is dropped. | DECIDED v0.2; 2j |
| 9 | **`RotatePinset` RPC** — `[2g §7.7]` describes the v1.x hand-off shape; v1.0 cuts the RPC per RC#5. | DEFER to v1.x — discharged by a 2i v0.4 amendment publishing `fixpp_pinset_add` / `fixpp_pinset_remove` followed by a 2j v1.x amendment adding the RPC + handler. The `[2g §7.7]` paragraph remains a published forward-compat hook. | 2i v0.4 + 2j v1.x |
| 10 | **`tools/check_layers.py` first-landing.** Named at `[arch §8]` as the AGPL-boundary lint mechanism; referenced by §9 seam #6. The tool does not yet exist in the repo (mirroring the `tools/check_alloc.py` precedent across 2a/2b/2c/2d/2e/2f/2g/2h test seams); architecture-owned per `[arch §8]` enforcement bullet. v0.2 records the dependency; the tool's first landing is owed to the architecture (or to whichever Phase 2 doc lands first in service mode). | RECORD dependency; first-landing owed to **architecture** per `[arch §8]` enforcement bullet (or to the first service-mode Phase 4 implementation milestone). | architecture |

---

## §11 Hand-off

**Docs unblocked by 2j sign-off (downstream):**

- **2k (async logger + OTel)** — needs the `StreamMetrics` / `StreamLogs` wire shape (§4.7 / §4.8) including the OTel correlation field tags pinned in §D.3, and the close-on-overflow per-stream backpressure contract (§4.8 / §6.4). The C-ABI registration symbols (`fixpp_engine_register_metric_consumer` / `_log_sink`) are **owed by 2k**; v0.2 does not introduce them (RC#1 close).
- **2l (session-tap consumer)** — needs the explicit non-overlap statement (§7.9 / §1.2) so 2l's iceoryx2 + in-process-tap design knows what 2j does NOT cover. Without 2j, 2l would need to assume an over-broad scope. SVC-002 / SVC-003 catalogue rows remain 2l's per `[arch §8.2]` (RC#2 close).
- **Session-module Phase-4 spec** — needs the `OpenSession` / `CloseSession` / `Configure` proto request shapes (§4.7) so it can lock the session-config-builder C-ABI thunks (CA-005) the gRPC handlers consume. Without 2j, the session-module Phase-4 spec doesn't know what fields are operator-settable from outside the engine. The `StreamSessionEvents` C-ABI registration symbol shape is owed to that spec, not 2j.
- **2m (SWIG/Python)** — consumes 2i's C-ABI surface as input per `[2i §1.2]`. 2j publishes one v1.0 C-ABI symbol owed to 2m: **`[[nodiscard]] fixpp_error_t fixpp_session_post(fixpp_session_t* session, void (*closure)(void*), void* userdata)`** — the strand-post primitive `[2i §6.3]` line 1365 references as "signature owned by 2j". The reentrancy class is `FIXPP_THREAD_SAFE` (callable from any Python thread; the closure runs on the session strand). The thunk is a steady-state path per `[2i §5.2]`. 2m's §4.4 outbound `Message.__init__` is the v1.0 consumer; without this declaration, the Python idiom `Message(msg_type, session)` from non-strand threads has no honest implementation. (NEW v0.3 — closes 2m round-2 N-2-P1-1.)
- **2i v0.4 (post-v1.0)** — the rotation surface (`fixpp_pinset_add` / `fixpp_pinset_remove` / `fixpp_engine_reload_cert_source` or equivalents) is the prerequisite for v1.x discharging §10 Q1 + Q9. 2i v0.4 is owed at the v1.x milestone, not at v1.0.

**Cross-doc amendments declared at sign-off (orchestrator applies, per `[2c App D]` / `[2d App D]` / `[2e App D]` / `[2f App D]` / `[2g App D]` / `[2h App D]` / `[2i App D]` precedent — the rewrite agent does NOT edit sibling docs in this draft):**

- **Appendix D §D.1** — `library/spec/feature-catalogue.md` "Service (Daemon/Sidecar)" section: **NEW row SVC-005**. Modelled on the 2d / 2f precedent (NFR-015 / NFR-016 added at sign-off). Byte-faithful Before / After per RC#3 close.
- **Appendix D §D.2** — `[2d §4.4]` `EngineConfig` field-list: append **one field** — `std::unique_ptr<fixpp::service::ControlPlaneFactory> control_plane_factory;` (engine-anchor; null-permitted means "no control plane in this deployment"). The v0.1 second field (`control_plane_executor`) is **dropped** per RC#1: handlers run on `EngineConfig::executor` per `[2d §7.8]`. Byte-faithful Before / After per RC#3 close.
- **Appendix D §D.3** — Numeric assignments within 2i's already-reserved `[900, 999]` block (per `[2i §1.1]` line 65) + audit-trail file appends. Append two lines to `tools/abi_history/error_codes_v1.txt` for `FIXPP_ERR_CTRL_CONFIG = 900` / `FIXPP_ERR_CTRL_RUNTIME = 901`; append two rows to `[2i §4.3]`'s numeric-layout table. Plus create `tools/abi_history/proto_v1.txt` (NEW append-only file pinning every published proto symbol per §4.7.1 rule 6) — mirror of `[2i §4.3]`'s `tools/abi_history/error_codes_v1.txt` precedent. **No 2i v0.4 amendment is owed** (RC#1 / RC#5 close: v0.2 introduces no NEW C-ABI symbols).
- **Appendix D §D.4** — `library/spec/coverage-index.md` "Catalogue ID supplemental notes" section: append the SVC-005 supplemental note linking SVC-005 to `[2j §4.1]` / `[2j §4.6]` / `[2j §4.7]` / `[2j §4.7.1]` per Appendix A.1 (the §D.1 `Spec ref` pivots already point SVC-001 → `[2j §4.6]` / `[2j §4.7]` and SVC-004 → `[2j §4.7]` / `[2j §4.8]`, so SVC-001 / SVC-004 do not need a supplemental note here — only SVC-005 gets the note per the `NFR-015` / `NFR-016` / `CA-002` precedent). (SVC-002 / SVC-003 are 2l's; 2j does not append for them.) Byte-faithful Before / After per RC#3 close.

The v0.1 §D.6 ("No edit needed at `[const §XIV.1]`") is dropped from v0.2 — recording "no edit needed" as a numbered drop-in entry was rhetorical; the constitutional anchor remains intact regardless.

(2j does NOT edit `library/spec/feature-catalogue.md`, `library/spec/coverage-index.md`, `architecture.md`, `constitution.md`, `2a-decimal.md`, `2b-wire.md`, `2c-codegen.md`, `2d-threading.md`, `2e-msgstore.md`, `2f-async-mutex.md`, `2g-tls.md`, `2h-transport.md`, or `2i-capi.md` directly per the brief's hard rule; the drop-ins are recorded in Appendix D verbatim for the orchestrator.)

---

## Appendix A — Catalogue row coverage

Per `[const §VI.1]` and `[const §VI.4]` plus the per-doc precedent in `[2b Appendix A]`, `[2c Appendix A]`, `[2d Appendix A]`, `[2e Appendix A]`, `[2f Appendix A]`, `[2g Appendix A]`, `[2h Appendix A]`, `[2i Appendix A]`. v0.2 restructure per RC#2: A.1 lists only the rows 2j genuinely owns sole; A.2 records the explicit-non-overlap deferrals to 2l for SVC-002 / SVC-003.

### A.1 Owned (sole) — SVC-001, SVC-004, SVC-005

| Row | Family | Catalogue text (verbatim from `library/spec/feature-catalogue.md` lines 206–209; SVC-005 NEW per Appendix D §D.1) | What 2j covers | 2j §/§§ |
|---|---|---|---|---|
| **SVC-001** | OFFICIAL — service | "gRPC control plane — session create/config/teardown/observability over Unix socket / named pipe" | The default gRPC implementation of `ControlPlane` (`service/grpc/grpc_control_plane.{h,cpp}`); the proto schema (`service/proto/fixpp_control.proto`); the listener bind / accept / handler-dispatch loop on Unix socket (Linux) / named pipe (Windows). | §4.6, §4.7, §6.5 |
| **SVC-004** | OFFICIAL — service | "Service health / observability — gRPC health check + prometheus-compatible metrics" | The `Health` RPC (§4.7 + §4.8) follows the gRPC standard health-check protocol; the `StreamMetrics` RPC bridges the engine's `MeterProvider` to a gRPC stream, parallel to the engine's OTLP-direct + Prometheus-pull export paths per `[const §XIII.1]`. (The Prometheus exporter itself is owned by 2k; 2j's `StreamMetrics` is the gRPC bridge.) | §4.7, §4.8 |
| **SVC-005** | OFFICIAL — service (NEW; queued in Appendix D §D.1) | "Pluggable control plane interface — `fixpp::service::ControlPlane` (≤5 pure-virtual: `start`, `stop`, `health`); default impl gRPC over Unix socket / named pipe; alternative impls (JSON-over-Unix-socket, etc.) link without rebuilding the engine via the AGPL-boundary structural rule (`[const §V.1]` / `[arch §8]`)" | The `ControlPlane` pure-virtual interface (§4.1 — 3 of 5 pure-virtual slots used; 2 slots reserved for v1.x), the `ControlPlaneConfig` value-typed config (§4.2), the factory entry point per `[arch §6]` rule 4 (§4.1 `ControlPlaneFactory`), the AGPL-boundary structural rule (§4.4 / §4.6 / §9 seams #6 / #7), the JSON-over-Unix-socket alt-impl exercise that validates pluggability (§9 seam #8), and the proto-evolution governance rules (§4.7.1, NEW v0.2). | §4.1, §4.2, §4.4, §4.6, §4.7.1, §9 seams #6 / #7 / #8 |

### A.2 Explicit non-overlap (NOT owned by 2j) — SVC-002, SVC-003

Per RC#2 close: v0.1 listed SVC-002 and SVC-003 as "Owned (sole)" with the qualifier "Cross-cut shape only" — that's a category error (a row cannot be simultaneously sole-owned and cross-cut shape). v0.2 restructures: 2j publishes only the boundary statement; the rows remain 2l's.

| Row | Family | Catalogue text (verbatim from `library/spec/feature-catalogue.md` lines 206–207) | 2j relationship | Owner |
|---|---|---|---|---|
| **SVC-002** | OFFICIAL — service | "iceoryx2 data plane — zero-copy SHM publish/subscribe for hot-path FIX messages" | 2j publishes the explicit non-overlap statement (§7.9 / §1.2) demarcating 2l's iceoryx2 territory from 2j's gRPC territory. **2j does not claim the row.** | **2l** per `[arch §8.2]` ("Topic shape, ownership semantics, backpressure, fallback when iceoryx2 isn't running — all in **2l**") |
| **SVC-003** | OFFICIAL — service | "Data plane opt-in — gRPC-only mode when iceoryx2 unavailable" | 2j publishes the corollary that the ControlPlane operates without iceoryx2 (per `[const §XIV.3]` opt-in rule); the gRPC schema and default impl work with or without the data plane. **2j does not claim the row.** The 2l-owned iceoryx2 publisher initialisation (and its absence under "gRPC-only mode") is 2l's. | **2l** per `[arch §8.2]` |

### A.3 CA-005 / CA-006 / CA-007 — shape-cross-cut to 2i

The CA-005 / CA-006 / CA-007 rows that touch 2j's territory are explicitly **shape-cross-cuts to 2i** per `[2i §1.2]` / `[2i Appendix A]`; the C-ABI shape is 2i's, the *behaviour* (FSM / send / callback dispatch) is owned by the **session-module Phase-4 spec**. 2j is a *consumer* of those shapes via the C ABI per §5; 2j does NOT claim the rows.

---

## Appendix B — Normative references

Per `[const §VI.5]` Normative-References requirement (every `/specify` artifact must list the exact `[DocAbbrev §X.Y.Z] Title` entries cited). Format `[DocAbbrev §X.Y.Z] Section title` per `[const §VI.2]` canonical-format rule, drawn from `library/spec/coverage-index.md`.

### B.1 Coverage-index normative references

2j is **not** a spec-section discharge — the SVC-* catalogue rows themselves cite `[impl] implementation` per `library/spec/feature-catalogue.md:206–209` (and SVC-005 will cite `[impl]` once the Appendix D §D.1 drop-in lands), so 2j is implementation-design-rooted, not spec-section-rooted. The normative references it consumes are constitutional + architectural + sibling-doc, not spec-section. (The precedent for design-rooted / `[impl]`-rooted rows skipping coverage-index spec normatives is `[2i Appendix B.1]`.)

### B.2 Constitutional clauses cited (per `[const §VI.5]` + `[const §VI.2]`)

`[const §I.2]` (in-process C++23 primary, C ABI adjacent — drives the AGPL-boundary structural shape);
`[const §IV.4]` (service wrapper opt-in, gRPC default control plane, iceoryx2 opt-in data plane);
`[const §V.1]` (AGPL boundary on the C ABI — the structural rule of this doc);
`[const §V.3]` (no LGPL deps);
`[const §VI.2]` (canonical-format `[DocAbbrev §X.Y.Z] Section title` cite rule — drives RC#4 cite cleanup);
`[const §VI.5]` (Normative References section requirement — drives this Appendix B);
`[const §VII.4]` (no untested code — drives §9);
`[const §VI.1]` (catalogue rows + bidirectional traceability — drives Appendix A);
`[const §VI.4]` (bidirectional traceability — drives Appendix A.2 + Appendix D §D.4);
`[const §VIII.2]` (perf regression budgets — drives §6.3 Tier 1 ceilings + §9 seam #2);
`[const §VIII.5]` (zero allocation between parse and `fromApp` — the control-plane is OUT of this window per §6.1 / §8);
`[const §X]` (full article "ABI Policy" — referenced narratively in §4.7.1 / §10 Q8 to clarify that v0.1's "constitution-level amendment per `[const §X]`" claim was an over-claim per RC#6 close: `[const §X]` is the C-ABI policy, not proto-evolution governance);
`[const §X.1]` (C ABI SemVer rules — adopted by analogy in §4.7.1 for proto evolution);
`[const §X.2]` (no C++ symbol leakage through the C ABI — drives §4.4 / §4.6);
`[const §X.4]` (out-of-range C-ABI code mapping; SemVer-shape rules adopted by analogy in §4.7.1 for proto MINOR / MAJOR);
`[const §X.5]` (per-symbol reentrancy contract — drives §5.2 cleanup, RC#1 close);
`[const §XI.1]` (`asio::awaitable<T>` composition — drives `start` / `stop` shapes);
`[const §XI.2]` (ASIO native cancellation slots — drives §6.2);
`[const §XI.3]` (`async_mutex` required in coroutine context — recorded; 2j does not consume directly per §7.4);
`[const §XII]` (article "Security / TLS" as a whole — appears in the §3 inherited-surface verbatim quote of `[arch §5.6]` at line 174 referring to pinset rotation; inherited via verbatim quote, not consumed normatively at the article level by 2j);
`[const §XII.5]` (no implicit defaults; operators raise explicitly — drives §1.1 numeric ceilings rationale);
`[const §XII.7]` (TLS at the transport layer; application-layer encryption banned — drives §2 non-goal #10);
`[const §XIII.1]` (OpenTelemetry from v1.0; Prometheus + OTLP dual export);
`[const §XIII.2]` (async logging mandatory; `drop-oldest` permitted on observability paths only — RC#7 close: v0.2 picks close-on-overflow, not drop-oldest);
`[const §XIII.3]` (strand-stored trace context — drives §7.8 OTel correlation);
`[const §XIII.4]` (single sink interface; no double-write paths — recorded in §4.8);
`[const §XIV.1]` (v1.0 pluggable — control plane is on the list with gRPC default);
`[const §XIV.2]` (≤5 pure-virtual cap — `ControlPlane` at 3 of 5);
`[const §XIV.3]` (iceoryx2 opt-in — drives §7.9 non-overlap);
`[const §XIV.4]` (no `dlopen` plugin loading — drives §2 non-goal #3);
`[const §XV.5]` (no synchronous logging on the hot path — recorded for completeness);
`[const §XV.9]` (`std::mutex`-in-coroutine-context ban — appears in the §D.4 NFR-016 supplemental Before/After block as a verbatim quote of `coverage-index.md:450`; inherited via verbatim quote, not consumed normatively by 2j directly);
`[const §XV.15]` (no `drop-oldest` on app/session message paths — drives §4.8 / §6.4 close-on-overflow choice per RC#7);
`[const §XVII.1]` (Codex Gate A required for design docs);
`[const §XVIII.1]` (v1.0 scope locked — drives §1.2 / §10 deferrals);
`[const §XX]` (article "Amendments" as a whole — appears in the §D.4 CA-002 supplemental Before/After block as a verbatim quote of `coverage-index.md:452` (cross-block error-code growth = 2i amendment per `[const §XX]`); inherited via verbatim quote, not consumed normatively at the article level by 2j directly);
`[const §XX.1]` (Amendments — recorded for RC#5 / RC#6 deferral framing).

### B.3 Architectural sections cited (per `[const §VI.5]` + `[const §VI.2]`)

`[arch §1.1]` (Goals — pluggable I/O for testability);
`[arch §1.2]` (non-goals — no dynamic plugin loading);
`[arch §2.3]` (allowed include edges — `service/` may include from `capi/` only; the interface header `<fixpp/service/...>` may include `core/` only);
`[arch §3]` (public namespaces — `fixpp::service`);
`[arch §4.1]` (`core` module surface — `core::expected_t<T>`);
`[arch §4.8]` (`otel` module surface — `MeterProvider` records consumed by `StreamMetrics`);
`[arch §4.9]` (`tap` module surface — owned by 2l per `[arch §8.2]`; recorded for §1.2 / §7.9);
`[arch §4.10]` (capi surface — the C-ABI 2j consumes);
`[arch §4.11]` (service surface — the spine of this doc);
`[arch §5.1]` (executor model — drives §6.5);
`[arch §5.2]` (allocator policy — drives §6.1 / §8 PMR table);
`[arch §5.3]` (error model — drives §6.6 + §6.1 exception window);
`[arch §5.4]` (trace context — drives §7.8 OTel correlation);
`[arch §5.6]` (frozen-config rule — drives §3.9 / §7.2 `Configure` policy);
`[arch §5.7]` (logging integration — drives §4.8 / §7.8 / §1 Goal 10);
`[arch §6]` (plugin pattern — drives §4.1 / §4.2 / `ControlPlaneFactory` shape);
`[arch §7.4]` (CMake target layout — drives §4.6 linking discipline + §9 seam #7);
`[arch §8]` (service-mode boundary — the structural AGPL boundary; the spine of §4.4 / §4.6; names `tools/check_layers.py`);
`[arch §8.1]` (control plane gRPC default — drives §1 / §4.7);
`[arch §8.2]` (data plane iceoryx2 opt-in; "Topic shape, ownership semantics, backpressure ... all in 2l" — drives §7.9 / RC#2 close);
`[arch §9.1]` (header discipline — drives §4.4);
`[arch §9.2]` (library SemVer track — drives §4.7.1 SemVer-bump-shape mapping);
`[arch §9.3]` ("Stable from v1.0 ... the gRPC control-plane proto" — drives §4.7 / §4.7.1 / RC#6 close);
`[arch §10] row 2j` ("Control-plane interface — `SVC-005` shape, gRPC default impl" — the row this doc closes).

### B.4 SYNTHESIS Q-IDs cited (exact)

`[SYN §3.6 #20]` (Control plane transport — DECIDED pluggable interface + gRPC default impl — drives §1 / §4.1);
`[SYN §3.6 #21]` (Observability surface — DECIDED OpenTelemetry from day 1; Prometheus + OTLP dual — drives §4.8);
`[SYN §3.6 #22]` (Session-tap consumer API — owned by 2l; recorded for §7.9 explicit non-overlap).

### B.5 Sibling-doc citations (per `[const §VI.5]` + `[const §VI.2]`)

`[2a §4.2]` (`trap_throw` PMR-throw routing — drives §4.3.1 / §6.6 `control_plane_factory_failed`);
`[2a §9]` (alloc-guard test-seam precedent — recorded for §9 seam #4);
`[2b §6.4]` (`[[clang::lifetimebound]]` declaration-site precedent — drives §4.5);
`[2b §9]` (test-seam precedent — recorded for §9);
`[2c §4.9]` (`dict::version_registry` — drives §7.1);
`[2c §6.5]` (cold dictionary load latency — drives §1.1 / §6.3 cold-cache `OpenSession` row);
`[2c §7.2]` (mid-session dialect-overlay swap rejected — drives §3.9 / §7.2);
`[2d §4.4]` (`EngineConfig` field shape — Appendix D §D.2 amends with `control_plane_factory`);
`[2d §4.5]` (`SessionConfig` field shape and `executor_override` pattern — drives §7.2 + §6.5 RC#1 close);
`[2d §4.7]` (cancellation propagation API — two-phase close — drives §6.2);
`[2d §4.8]` (`session_executor` wrapper class — drives §6.5 cross-strand handoff context);
`[2d §6.5]` (`cancellable_dispatch` recipe — drives §6.4 cancellation precedent + §6.3 cross-strand budget);
`[2d §6.7]` (per-doc-prefix `FIXPP_ERR_THREAD_*` discipline — precedent for §6.6 `FIXPP_ERR_CTRL_*`);
`[2d §7.6]` (transport ops on session strand — recorded for §7.6);
`[2d §7.8]` (control-plane handlers run on the engine executor outside any session serialisation domain — RC#1 close: anchor for §1 Goal 5 / §6.5);
`[2d §7.9]` (`effective_clock` + `clock_scope` discriminator — drives §4.7 proto field commitment + §9 seam #10);
`[2d §9]` (test-seam cross-referencing precedent — recorded for §9);
`[2e §4.4]` (`MessageStoreFactory` — drives §7.3);
`[2e §6.1.4]` (durable-before-transmit invariant — recorded for §7.3);
`[2e §6.4]` (writer-mutex contract — recorded);
`[2e §6.7]` (per-doc-prefix `FIXPP_ERR_STORE_*` discipline — precedent);
`[2f §4.1]` (`fixpp::sync::async_mutex` — recorded for §7.4);
`[2f §6.5]` (per-doc-prefix `FIXPP_ERR_SYNC_*` discipline — precedent);
`[2g §4.1]` (`cert_source` interface; declaration-site annotation precedent — drives §4.1 / §4.5);
`[2g §4.2]` (`make_file_cert_source` factory `noexcept` precedent — drives §4.1 `ControlPlaneFactory::make`);
`[2g §4.3]` (`Pinset::add` / `Pinset::remove` — drives §7.5 v1.x deferral context);
`[2g §4.5]` (`peer_identity` parsed-cert value — recorded);
`[2g §6.5]` (`Pinset` add-then-remove rotation invariants — drives §7.5 v1.x deferral context + §1.1 cold cert_source latency);
`[2g §6.5.1]` (handshake-time `Pinset::snapshot()` captured-once — recorded for §7.5 v1.x context);
`[2g §6.6]` (`FIXPP_ERR_TLS_*` per-doc-prefix — precedent for §6.6);
`[2g §7.6]` (capi (2i) — C ABI shape delegation; the cited paragraph at line 1052 anchors the v1.0 partition);
`[2g §7.7]` (control-plane reload trigger consumer drop-in — quoted in §3.12; v1.x scope per RC#5);
`[2g §9]` (test-seam precedent — recorded for §9);
`[2h §4.1]` (`Transport` interface — recorded for §7.6 graceful drain);
`[2h §4.2]` (`TlsTransport` sub-interface — recorded);
`[2h §4.7]` (`TransportFactory` `noexcept` precedent — drives §4.1 `ControlPlaneFactory::make`);
`[2h §6.4]` (two-phase close + per-mode cancellation effect table — drives §6.2);
`[2h §6.6]` (`FIXPP_ERR_TRANSPORT_*` per-doc-prefix — precedent for §6.6);
`[2h §7.6]` (control-plane / cert-source reload triggers consumer drop-in — quoted in §3.12);
`[2h §9]` (test-seam precedent — recorded for §9);
`[2i §1.1]` (numeric block reservation `[900, 999]` for `FIXPP_ERR_CTRL_*` — drives §6.6 / Appendix D §D.3);
`[2i §1.2]` (non-owned shape cross-cuts — drives §1.2 / §5.1 / §7.7);
`[2i §2]` (non-goal #6 — "No `fixpp_pinset_add` / `fixpp_pinset_remove` / `fixpp_cert_source_reload` symbols in v1.0" — RC#5 close anchor);
`[2i §4.2]` (opaque-handle catalogue — drives §7.7 + §5.1 Health-row reasoning);
`[2i §4.3]` (numeric layout — drives §6.6 + Appendix D §D.3);
`[2i §4.5]` (versioning — recorded);
`[2i §4.9]` (cancellation translation rule — drives §6.2 / §6.6);
`[2i §4.10]` (reentrancy annotation — drives §6.5 cross-strand handoff path; v0.2 introduces no NEW C-ABI symbols);
`[2i §5.2]` (construction-vs-steady-state thunk split — drives §3.15 / §6.1 / §7.7);
`[2i §6.5]` (per-doc errors numeric block discipline — precedent for §6.6);
`[2i §7.9]` (hand-off to 2j: shape-cross-cut for CA-005/006/007 — recorded);
`[2i §9]` (test-seam precedent — recorded for §9).

**Sibling-doc Appendix-A precedent cites** (per `[const §VI.5]` exact-citation discipline; cited at line 1139 in §A's per-doc-precedent paragraph as the structural model for 2j's Appendix A): `[2b Appendix A]`, `[2c Appendix A]`, `[2d Appendix A]`, `[2e Appendix A]`, `[2f Appendix A]`, `[2g Appendix A]`, `[2h Appendix A]`, `[2i Appendix A]` — each names the corresponding sibling doc's catalogue-row coverage section that 2j's Appendix A models on (per the round-2 P2 close on Appendix B coverage gap; cited as a precedent-set, not enumerated row-by-row).

**Sibling-doc Appendix-D precedent cites** (per `[const §VI.5]`; cited at line 1118 in §11's hand-off paragraph as the cross-doc-amendment-via-orchestrator-precedent set, plus body usage at §D Appendix preamble line 1383): `[2c App D]`, `[2d App D]`, `[2e App D]`, `[2f App D]`, `[2g App D]`, `[2h App D]`, `[2i App D]` — each names the corresponding sibling doc's Appendix-D drop-in section that 2j's Appendix D models on. **Sub-section App-D cites** used in the §D.2 / §D.4 ownership-shape rationale: `[2e App D §D.1]` and `[2h App D §D.1]` (the `unique_ptr<Factory>` plugin-ownership precedent cited at §4.6 / §D.2 lines 1549–1551 / §D.2 "Why" paragraph line 1559); `[2i App D §D.1]` (the "covered by `[2X §...]`" Spec-ref-pivot precedent cited at §D.1 line 1402 / line 1416). **Format normalisation:** the short form `[2X App D]` / `[2X App D §D.N]` is canonical per `[const §VI.2]` and the body usage at lines 1118 / 1383; the long form `[2X Appendix D §D.N]` may appear interchangeably in narrative text and is equivalent.

Internal cross-references within this doc — `[2j §4.1]`, `[2j §4.4]`, `[2j §4.6]`, `[2j §4.7]`, `[2j §4.7.1]`, `[2j §4.8]`, `[2j §6.4]`, `[2j §10]`, `[2j §11]`, `[2j Appendix A]`, `[2j App D §D.2]`, `[2j App D §D.3]` — appear in the SVC-005 supplemental note (Appendix D §D.4) and in cross-section references in the body; they are not consumed normatively from outside this doc.

Other (non-2j) sibling-doc cite references appearing in narrative context: `[2c §1.3]`, `[2c §6.3]`, `[2c §6.4]`, `[2c §10]` appear inside quotation contexts or precedent-narrative (the `FIXPP_ERR_DICT_*` reference cite at §6.6 / the `[2c App D §2]` precedent reference); they are not load-bearing normative cites for 2j and are recorded here for completeness.

**Engineering-judgment decisions** (per RC#4 / Codex P1#5 close — v0.1's placeholder-cite construction `[const §X.y]` / `[arch §X.y]` / `[SYN §3.6 #N]` / `[2X §X.y]` is dropped). The following items are pure engineering judgment with no normative driver in the constitution, architecture, synthesis, or sibling docs; they are stated as engineering picks without a fake cite token, per `[const §VI.5]` exact-citation discipline:

- The specific gRPC default-impl translation-unit layout (§4.6) — engineering pick following the established `service/<plugin>/` flat-file pattern; not normatively constrained.
- The `ControlPlaneConfig` field list and field defaults (§4.2) — engineering picks bounded by `[const §XII.5]` (no implicit defaults; operators raise explicitly) which IS a normative driver and is cited at point of use; the *specific values* (`stream_queue_depth = 1024`, `max_in_flight_rpcs = 64`, `stop_timeout = 5 s`, `max_message_bytes = 4 MiB`, `max_metadata_bytes = 8 KiB`) are operational picks discussed in §1.1 with deployment-shape rationale (N-P3-5 close); v1.x bench-time tuning is owed.
- The per-RPC arena vs engine-arena split (§8) — follows `[arch §5.2]` PMR allocator policy (which IS cited) and the `[arch §6]` rule-4 factory-entry-point shape (cited); the *split point* itself (per-RPC arena for request/response buffers; engine arena for long-lived state) is engineering judgment.

The above items are not spec normatives and are intentionally not listed in §B.1.

---

## Appendix C — Convergence log

### v0.1 → v0.2 (Gate A round 1, Phase A)

Addresses Codex review (`research/reviews/codex_2j_controlplane_review.md` — 5 P1 / 5 P2 / 4 P3) and Opus adversarial review (`research/reviews/opus_2j_controlplane_adversarial_review.md` — combined post-judging tally 8 P1 / 8 P2 / 12 P3; 7 root causes; closing recommendation "Round-cap risk if RC#1, RC#5, and RC#6 are not all resolved cleanly in v0.2"). Opus's verdicts are the source of truth where they differ from Codex's.

**Root causes addressed (top of entry per Opus's clustering):**

- **RC#1 — Signed-off-sibling-override pattern.** v0.1 introduced a dedicated `EngineConfig::control_plane_executor` (Codex P1#3) and seven NEW C-ABI symbols + three NEW PoD types (Codex P1#1 plus four undisclosed-in-Codex symbols flagged by Opus's expansion of P1#1) into 2i's surface. v0.2 cuts both: handlers run on `EngineConfig::executor` per `[2d §7.8]` (operators that want isolation use the `[2d §4.5]` `executor_override` pattern at the factory's `exec` parameter); §5.2 introduces no NEW C-ABI symbols. Sections rewritten: §1 Goal 5, §1.2 owns-list, §3.14, §4.1 (ControlPlane class header comment + ControlPlaneFactory rationale block), §5 (whole section restructured), §6.5, §7.5, §7.8, §11 hand-off, Appendix D §D.2 (one field, not two), Appendix D §D.3 (numeric assignments only — no 2i v0.4 amendment owed).
- **RC#2 — Catalogue-row-ownership over-claim.** v0.1 listed SVC-002 / SVC-003 as "Owned (sole)" in Appendix A.1 with the qualifier "Cross-cut shape only," contradicting `[arch §8.2]` which assigns them to 2l. Internal §A.1-vs-§A.2 contradiction (Codex P1#2). v0.2 restructures Appendix A: A.1 lists SVC-001 / SVC-004 / SVC-005 only; A.2 records the explicit-non-overlap deferrals to 2l for SVC-002 / SVC-003; A.3 records the CA-005/006/007 shape-cross-cuts to 2i. Front-matter "Catalogue rows owned" line restructured. §1.2 owned/non-owned list updated. §7.9 prose tightened. §11 hand-off note added.
- **RC#3 — Appendix-D / inherited-surface mechanical-application discipline failure.** v0.1 declared Appendix D "Sketch only" (Codex P1#4); the §3.12 quote ranges drifted from byte-faithfulness (Codex P2#1). v0.2 authors Appendix D byte-faithfully now (the v0.1 "(Sketch only ...)" disclaimer is dropped); §3.12 cites tightened to `2g-tls.md:1058` and `2h-transport.md:1286–1290` + `2h-transport.md:1292` as separate quotes per Opus's preferred fix shape. The mechanical-application contract is now satisfiable at sign-off.
- **RC#4 — Citation discipline.** v0.1's explicit placeholder cite construction at the engineering-judgment paragraph (`[const §X.y]` / `[arch §X.y]` / `[SYN §3.6 #N]` / `[2X §X.y]`, Codex P1#5), plus bare `§X` cites in the Inherits list (Codex P3#2), plus the dangling `[2d App D §D.2]` reference (Codex P3#1), plus the tautological `[arch §11] row 5` self-reference (Opus N-P3-4). v0.2 rewrites the engineering-judgment paragraph to state engineering picks without fake cite tokens; the Inherits list is tightened to subsection cites; `[2d App D §D.2]` is dropped (no such cite is used anywhere in v0.2 — the §D.2 drop-in references `[2d §4.4]` and `[2j Appendix D §D.2]` only); `[arch §11] row 5` removed from the Inherits list (the row this doc closes).
- **RC#5 — Rotation-RPC AGPL-boundary structural hole.** Opus N-P1-1: `RotatePinset` and `ReloadCertSource` are not implementable in v1.0 because `[2i §2]` non-goal #6 declines the C-ABI rotation surface and `service/grpc/*.cpp` cannot include `<fixpp/tls/...>`. v0.2 cuts both RPCs from the v1.0 proto schema; §10 Q1 (cert-source bridge) and §10 Q9 (NEW: `RotatePinset` deferral) record the v1.x discharge path. §1 Goal 4 prose updated; §1.2 expanded with the deferral rationale; §3.12 marked "v1.x scope per RC#5"; §4.6 file-layout drops `grpc_pinset_handlers.cpp`; §4.7 proto schema drops the rotation RPCs; §6.2 cancellation taxonomy drops the rotation rows; §6.3 latency table drops the rotation rows; §7.5 reframed as v1.x deferred; §9 seam #12 retired (the rotation-pinset seam is replaced with the proto-stability audit-trail seam per RC#6); §11 hand-off updated.
- **RC#6 — Proto-evolution governance unspecified.** v0.1 claimed "constitution-level amendment per `[const §X]`" for proto changes (Codex P2#2 — `[const §X]` is "ABI Policy" for the C ABI, not the proto) and asserted "additions are MINOR bumps; removals are MAJOR" without authoring the rule (Codex P2#5; Opus N-P2-1). v0.2 authors a NEW §4.7.1 "Proto evolution rules" sub-section anchored at `[arch §9.3]` (the actual stability-tier home of the proto) and adopts SemVer-shape rules from `[const §X.4]` by analogy without claiming the constitution covers protos. §1 Goal 4 rewritten to drop the constitution-amendment rhetoric. `Configure` reserved-empty justification is now defensible under the §4.7.1 additive-only expansion path. §10 Q8 disposition rewritten. §9 seam #12 (NEW) provides the `tools/abi_history/proto_v1.txt` audit-trail verification.
- **RC#7 — Stream backpressure semantics conflated.** Opus N-P2-3: v0.1 conflated close-on-overflow (the actual v0.1 behaviour) with drop-oldest (the rhetorical claim citing `[const §XIII.2]`). v0.2 rewrites §4.2 / §4.8 / §6.4 / §6.6 prose: the rule is close-on-overflow with `control_plane_stream_overflow`; `[const §XV.15]` (no drop-oldest on app/session paths) is satisfied by construction; `[const §XIII.2]` permits but does not require drop-oldest on observability paths and v1.0 picks close-on-overflow for visibility. Seam #11 prose tightened to verify close-on-overflow specifically (not drop-oldest).

**Per-finding resolution table:**

| Finding | Opus verdict | Resolution location | Notes |
|---|---|---|---|
| Codex P1#1 (`fixpp_pinset_add` / `_remove` override `[2i §2]` non-goal #6) | Confirm @ P1; broader (7 NEW symbols, 3 NEW PoDs) | §1 Goal 4 / §1.2 / §5.2 / §3.12 / §4.6 / §4.7 / §6.2 / §6.3 / §7.5 / §10 Q9 / Appendix D | Subsumed under RC#1 + RC#5; rotation RPCs cut from v1.0 proto. |
| Codex P1#2 (SVC-002 / SVC-003 ownership claim) | Confirm @ P1 | Front-matter / §1.2 / §7.9 / Appendix A.1 + A.2 | RC#2 close. |
| Codex P1#3 (`EngineConfig::control_plane_executor` overrides `[2d §7.8]`) | Confirm @ P1 | §1 Goal 5 / §4.1 (class header + factory rationale) / §6.5 / Appendix D §D.2 | RC#1 close: handlers run on engine executor; one field add (not two). |
| Codex P1#4 (Appendix D "sketch only") | Confirm @ P1 | Appendix D rewritten byte-faithfully | RC#3 close. |
| Codex P1#5 (placeholder `[const §X.y]` cites) | Confirm @ P1 | Appendix B engineering-judgment paragraph / Inherits list / §10 Q8 | RC#4 close. |
| Codex P2#1 (§3.12 byte-faithfulness drift) | Confirm @ P2 | §3.12 cite ranges tightened to `2g-tls.md:1058` and `2h-transport.md:1286–1290` + `:1292` | RC#3 close (Opus's preferred fix shape applied). |
| Codex P2#2 (proto-change "constitution-level" claim) | Confirm @ P2 | §1 Goal 4 / §4.7.1 / §10 Q8 | RC#6 close. |
| Codex P2#3 (`unique_ptr<ControlPlaneFactory>` ergonomic break) | **Demote P2 → P3** | Front-matter retains `unique_ptr` (aligned with `[2e App D §D.1]` / `[2h App D §D.1]` `unique_ptr`-pattern trend per Opus's reading) | Opus disagreed with the demotion direction's Codex framing: "the 2j v0.1 pick of `unique_ptr<ControlPlaneFactory>` is *aligned* with the trend — the defect is not 'wrong type' but 'no rationale citing the `[2e App D §D.1]` / `[2h App D §D.1]` `unique_ptr`-pattern alignment.'" v0.2 keeps `unique_ptr` and the rationale is explicit in Appendix D §D.2 + the §4.1 ControlPlaneFactory comment. |
| Codex P2#4 (`tools/check_layers.py` doesn't exist) | **Demote P2 → P3** | §1 Goal 6 / §4.4 / §9 seam #6 / §10 Q10 | Forward-reference pattern matched to `tools/check_alloc.py` precedent; §10 Q10 (NEW) tracks first-landing dependency. |
| Codex P2#5 (`Configure` reserved-empty rationale missing) | Confirm @ P2 | §4.7.1 / §10 Q8 / §6.4 (DoS rationale) | RC#6 close. |
| Codex P3#1 (`[2d App D §D.2]` non-existent) | Confirm @ P3 | Appendix D §D.1 SVC-005 row text uses `[2j Appendix D §D.2]` (this doc's own drop-in) | RC#4 close. |
| Codex P3#2 (bare `§X` cites in Inherits list) | Confirm @ P3 | Front-matter Inherits list tightened | RC#4 close. |
| Codex P3#3 (callback-registration symbols' reentrancy taxonomy hand-waved) | Confirm @ P3 | Subsumed under RC#1 — the registration symbols are dropped from §5.2; the taxonomy gap goes away with the symbols. | RC#1 close. |
| Codex P3#4 (seam #12 references non-existent pinset accessor) | Confirm @ P3 | Seam #12 retired and replaced with the proto-stability audit-trail seam | RC#5 + RC#6 close. |
| Opus N-P1-1 (rotation RPCs not implementable in v1.0) | NEW finding | §1 Goal 4 / §1.2 / §3.12 / §4.6 / §4.7 / §6.2 / §6.3 / §7.5 / §10 Q1 + Q9 | RC#5 close. |
| Opus N-P1-2 (`ControlPlaneFactory::make` 4-arg shape divergence) | NEW finding (P1) | §4.1 ControlPlaneFactory class-comment block adds explicit divergence rationale (Opus's "narrow divergence acknowledged" framing) | The 4-arg shape is retained because `engine` is genuinely a per-make argument (engine-scoped factory, not session-scoped like 2h); the 1-parameter divergence from 2h's 3-arg shape is documented in code comments. |
| Opus N-P1-3 (`HealthStatus` not transactional across fields) | NEW finding (P1) | §4.1 (struct comment + class comment) / §4.3.3 postconditions | Adopted seqlock-or-`atomic<HealthStatus>` per the `[2d §4.4]` `engine_trace_context` precedent. |
| Opus N-P2-1 (proto-evolution governance unauthored) | NEW finding (P2) | §4.7.1 (NEW sub-section) | RC#6 close. |
| Opus N-P2-2 (`idempotency_key` published while engine support deferred) | NEW finding (P2) | §4.7 schema notes + §10 Q2 | Resolved by dropping `idempotency_key` from v1.0 proto; v1.x adds it via §4.7.1 additive expansion. |
| Opus N-P2-3 (close-on-overflow vs drop-oldest conflation) | NEW finding (P2) | §4.2 / §4.8 / §6.4 / §6.6 / §9 seam #11 | RC#7 close. |
| Opus N-P2-4 (`OpenSession` ≤ 50 ms p99 implausible for cold start) | NEW finding (P2) | §1.1 split warm/cold ceilings / §6.3 latency table split | Warm ≤ 50 ms (Tier 1); cold ≤ 500 ms (informational). |
| Opus N-P2-5 (seam #6 / #7 overlap on AGPL-boundary verification) | NEW finding (P2) | §9 seam #6 / #7 prose tightened to label complementary halves (preprocessor scan vs link scan) | Both seams retained; division of labour explicit. |
| Opus N-P2-6 (`trace_id` / `span_id` not in proto schema) | NEW finding (P2) | §4.7 schema notes (proto field-tag commitment for OTel correlation fields) / §D.3 audit-trail / §9 seam #10 | Concrete proto field tags committed; §D.3 audit trail mechanically applied. |
| Opus N-P3-1 (`fixpp_metric_record_t` / `_session_event_t` PoD shapes claimed unilaterally) | NEW finding (P3) | §5.2 (the symbols + types are dropped in v0.2; ownership returns to 2k / Phase-4) | RC#1 close. |
| Opus N-P3-2 (`serving_status` enum not normatively pinned to gRPC standard) | NEW finding (P3) | §4.1 enum comment block adds normative commitment to `grpc.health.v1.HealthCheckResponse.ServingStatus` | "2j MUST NOT add additional values without a corresponding gRPC standard amendment." |
| Opus N-P3-3 (`ControlPlane` 3-pure-virtual surface incomplete; missing `local_endpoint` accessor) | NEW finding (P3) | Not added in v0.2 — the §10 Q5 reserved slots are intentionally for the v1.x auth-token-rotation + RPC-remap hooks; a `local_endpoint` accessor is filed as a v1.x consideration if benchmarks / operators ask for it (the test seam can derive listen-endpoint from `ControlPlaneConfig::listen_endpoint` for `unix_endpoint` / `named_pipe_endpoint`; only `tcp_endpoint{":0"}` would need the accessor and that is not a v1.0 supported path). | Soft acknowledgement; not blocking. |
| Opus N-P3-4 (`[arch §11] row 5` is the row this doc closes) | NEW finding (P3) | Front-matter Inherits list drops `[arch §11] row 5` | RC#4 close. |
| Opus N-P3-5 (numeric ceilings without deployment-shape evidence) | NEW finding (P3) | §1.1 max-in-flight-RPCs paragraph adds 100-session-1U-blade rationale + v1.x bench-time tuning note | Engineering-judgment-with-rationale, not blocking. |
| Opus N-P3-6 (cross-strand handoff timing budget unspecified) | NEW finding (P3) | §6.3 cross-strand handoff sub-paragraph (≤ 100 µs idle / ≤ 5 ms loaded p99) | Anchored at `[2d §6.5]` `cancellable_dispatch` precedent. |

**Codex findings disagreed (Opus reasoning quoted verbatim):**

- **Codex P2#3** — Codex framed `unique_ptr<ControlPlaneFactory>` as an ergonomic break. Opus disagreed with the framing direction: *"the pattern across recent signed-off docs is actually moving TOWARDS `unique_ptr` for plugin factories, not `shared_ptr` ... the 2j v0.1 pick of `unique_ptr<ControlPlaneFactory>` is *aligned* with the trend — the defect is not 'wrong type' but 'no rationale citing the `[2e App D §D.1]` / `[2h App D §D.1]` `unique_ptr`-pattern alignment.'"* v0.2 keeps `unique_ptr` and adds the rationale.
- **Codex P2#4** — Codex framed `tools/check_layers.py`'s absence as a 2j-specific defect demanding the seam be replaced. Opus disagreed: *"`tools/check_layers.py` per `architecture.md:501` already covers the link-interface check at CMake time ... it is a known forward reference, not a 2j-specific defect ... 2j seam #6 is consistent with the architectural-naming pattern and is not aspirational."* v0.2 keeps the seam, adds a forward-reference note (matched to the `tools/check_alloc.py` precedent), and tracks first-landing in §10 Q10.

**Net-effect summary:**

| Axis | v0.1 → v0.2 |
|---|---|
| Root causes addressed | RC#1 (executor model), RC#2 (catalogue ownership), RC#3 (Appendix D byte-faithfulness), RC#4 (citation discipline), RC#5 (rotation RPCs deferred), RC#6 (proto governance), RC#7 (close-on-overflow) — all 7 |
| NEW C-ABI symbols introduced | 7 → 0 (RC#1 / RC#5 close) |
| NEW PoD types introduced | 3 → 0 (RC#1 close) |
| Cross-doc amendments owed | 2i v0.4 (NEW symbols) + 2d v0.5 (`control_plane_executor`) + cross-cuts to 2k / Phase-4 → **none owed**; v0.2 is buildable against 2i v0.3 |
| Pure-virtual count on `ControlPlane` | 3 (unchanged) |
| Error-variant count + block | 8 variants, `[900, 999]` block (unchanged); coalescing assignments now concrete (`FIXPP_ERR_CTRL_CONFIG = 900`, `FIXPP_ERR_CTRL_RUNTIME = 901`) |
| Test-seam count | 12 → 12 (one retired: rotation-pinset seam; one added: proto-stability audit-trail seam) |
| Appendix D drop-ins | 6 → 4 (D.5 collapsed into D.4; D.6 "no edit needed" entry dropped as rhetorical; v0.1 D.3 "NEW C-ABI symbols + 2i v0.4 amendment" reduced to numeric assignments + audit-trail appends within 2i v0.3's already-reserved block) |
| Proto schema RPC count | 9 RPCs → 7 RPCs (rotation RPCs deferred) |
| `Configure` reserved-empty justification | Constitution-amendment rhetoric → §4.7.1 architectural-stability framing |
| Codex findings disagreed (with Opus reasoning) | 0 → 2 (Codex P2#3 + Codex P2#4 — both Opus-Demoted P3 line-edit class) |
| Doc length | 1327 → 1639 |

The Opus closing recommendation framed the round-cap risk as "RC#1, RC#5, and RC#6 not all resolved cleanly in v0.2." All three are resolved structurally above; v0.2 is signable in one convergence pass per Opus's framing.

### v0.2 → v0.3 (Gate A round 2, Phase A)

Addresses Codex round-2 review (`research/reviews/codex_2j_2_controlplane_review.md` — 2 P1 / 1 P2 / 1 P3) and Opus round-2 adversarial review (`research/reviews/opus_2j_2_controlplane_adversarial_review.md` — combined post-judging tally 2 P1 / 2 P2 / 4 P3; 0 new root causes; closing recommendation "v0.3 can ship after a single convergence pass"). Opus's verdicts are the source of truth where they differ from Codex's.

**0 new root causes named.** All round-1 RCs (#1, #2, #5, #6, #7) confirmed Closed by Opus; **RC#3 closed mechanically** (Appendix D §D.2 nested-fence break fixed via 4-backtick outer fence; Appendix D §D.4 placeholder bytes replaced with byte-faithful live bytes from `library/spec/coverage-index.md:442–456`); **RC#4 closed mechanically** (residual `[2j §X.Y]` placeholder at §11 line 1123 replaced with concrete `[2j §4.1]` / `[2j §4.6]` / `[2j §4.7]` / `[2j §4.7.1]` references; Appendix B §B.5 expanded with the missing precedent cite groupings — sibling-doc Appendix-A cites cited at §A line 1139, sibling-doc Appendix-D cites cited at §11 line 1118 + §D Appendix preamble line 1383, plus the §D.1 sub-section App-D cites). The 8 confirmed Opus-verdict findings are line-edit class.

**Per-finding resolution table (round 2):**

| Finding | Severity (Opus verdict) | Resolution location | Notes |
|---|---|---|---|
| Codex P1-1 (§D.2 nested-fence boundary break) | Confirm @ P1 | Appendix D §D.2 Before / After: outer fence changed from 3-backtick `cpp` to 4-backtick `markdown` so the inner ` ```cpp ` from quoted 2d §4.4 line 414 survives as literal content | RC#3 close (mechanical). Sibling precedent: 2g §D.1 / 2i §D.2 use 3-backtick because they quote markdown without inner fences; 2j §D.2 is the first sibling Appendix-D drop-in to need 4-backtick escaping. |
| Codex P1-2 (§D.4 placeholder text breaks byte-faithfulness) | Confirm @ P1 | Appendix D §D.4 Before / After: placeholders replaced with full live bytes from `coverage-index.md:442–456` (D-008 / NFR-015 / NFR-016 / CA-002 supplementals); After block applies the SVC-005 supplemental insertion against the full Before bytes; outer fence is now 4-backtick to be consistent with §D.2 | RC#3 close (mechanical). Sibling precedent: `2i-capi.md:1907–1915` quotes NFR-015 / NFR-016 verbatim in full bytes — 2j §D.4 now matches that precedent. |
| Codex P2-1 (residual `[2j §X.Y]` + Appendix B coverage gap) | Confirm @ P2 (count downward-refined: ≈ 6–10 unique tokens missing, not 13) | §11 line 1123 `[2j §X.Y]` replaced with concrete `[2j §4.1]` / `[2j §4.6]` / `[2j §4.7]` / `[2j §4.7.1]` per §D.4 SVC-005 supplemental ref set; Appendix B §B.5 expanded with two new grouped precedent paragraphs (sibling-doc Appendix-A precedent cites + sibling-doc Appendix-D precedent cites including the §D.1 sub-section variants `[2e App D §D.1]` / `[2h App D §D.1]` / `[2i App D §D.1]`) plus internal-cross-reference list updated to short form `[2j App D §D.2]` / `[2j App D §D.3]` | RC#4 close (mechanical). |
| Codex P3-1 (front-matter `Inherits` omits `[arch §9.2]`) | Confirm @ P3 (with reasoning correction: §9.2 IS in §B.3 line 1226; the gap is in front-matter Inherits, not Appendix B) | Front-matter line 6 Inherits list: `[arch §9.2]` inserted between `[arch §9.1]` and `[arch §9.3]` | RC#4 close. |
| Opus N2-P2-1 (§6.5 line 920 ownership claim contradicts §1.2 explicit non-ownership for CA-005) | NEW finding @ P2 | §6.5 final paragraph rewritten: the parenthetical "owned by 2j semantically per `[2i §1.2]` non-goal #1 / shape-only-cross-cut" is replaced with an aligned framing — "CA-005's protobuf-shape projection lives in `[2j §4.7]` `OpenSessionRequest`; the underlying C-ABI session-config-builder symbols are 2j-shape-co-owned with 2i + the Phase-4 session-module spec per §1.2's non-owned list (2j carries the protobuf wire shape only — the C-ABI signatures and FSM behaviour are not 2j's)" | Internal-consistency fix; the §1.2 framing is preserved as the binding ownership statement. |
| Opus N2-P3-1 (§4.7 `MetricRecord` / `LogRecord` `clock_scope` cite mis-anchored to `[2d §7.9]` instead of `[arch §5.7]` / 2k schema territory) | NEW finding @ P3 | §4.7 schema notes (the `MetricRecord` / `LogRecord` field commentary at lines 734 / 738) re-anchor `clock_scope` to "(record-schema owner: 2k per `[arch §5.7]`; v1.0 reserves a tag pending 2k's schema lock — 2d §7.9 names the producer-side semantics that the discriminator reflects)" | Hand-off-discipline cleanup; §1.2 already correctly disclaims 2k record schemas, this aligns §4.7 prose with that. |
| Opus N2-P3-2 (§D.4 line 1637 retrospective paragraph references retired §D.5/§D.6 inconsistently with the §D body) | NEW finding @ P3 | §D.4 closing retrospective paragraph tightened to "The v0.1 §D.5 / §D.6 history is captured in Appendix C v0.1 → v0.2 entry; v0.2 consolidated those entries into the current four-drop-in shape (§D.1 / §D.2 / §D.3 / §D.4) and v0.3 preserves that consolidation." | Doc-hygiene nit. |
| Opus N2-P3-3 (Appendix B §B.5 mixes long form `[2j Appendix D §D.2]` with body short form `[2j App D]`) | NEW finding @ P3 | Appendix B §B.5 internal-cross-reference list updated to short form `[2j App D §D.2]` / `[2j App D §D.3]`; explicit equivalence note added "the short form is canonical per `[const §VI.2]` and the body usage at lines 1118 / 1383; the long form may appear interchangeably and is equivalent" | Format-consistency nit. |

**Codex findings disagreed (Opus reasoning quoted verbatim):**

- **Hypothetical "`health()` reentrancy" P2** (the round-2 brief mentioned this as a possibility; Codex round 2 did not actually publish it). Opus orchestrator hint flagged: *"the brief's hypothesised `health()` reentrancy P2 is NOT actually in Codex round-2's review — I would have disagreed if raised."* Per Opus's reasoning quoted verbatim: *"v0.2 §4.3.3 line 539 says `health()` 'may be called from any thread without external synchronisation'; §6.5 binds RPC handlers to the engine executor. These are independent axes: the IMPL-side `health()` is a `noexcept const` synchronous accessor over a seqlock-protected snapshot — concurrent-callable from any thread is the conventional contract for a thread-safe accessor. The RPC HANDLER for the `Health` gRPC RPC runs on the engine executor (per §6.5), but the `health()` C++ method itself is callable directly (e.g., by an in-process diagnostic, an OS-level admin tool, a non-RPC sidecar liveness probe). This is the conventional pattern: serialised-handler-impl on a specific executor + concurrent-callable thread-safe accessor on the same plumbing. No contradiction."* v0.3 makes no change to §4.3.3 / §6.5 on this axis.

**Net-effect summary (v0.2 → v0.3):**

| Axis | v0.2 → v0.3 |
|---|---|
| Root causes addressed | 0 new; RC#3 closed mechanically (was Partial); RC#4 closed mechanically (was Partial); RC#1 / RC#2 / RC#5 / RC#6 / RC#7 remain Closed |
| New C-ABI symbols introduced | 0 → 0 (unchanged) |
| New PoD types introduced | 0 → 0 (unchanged) |
| Cross-doc amendments owed | none → none (unchanged; v0.3 buildable against 2i v0.3 as published) |
| Pure-virtual count on `ControlPlane` | 3 → 3 (unchanged) |
| Error-variant count + block | 8 variants in `[900, 999]` → 8 variants in `[900, 999]` (unchanged) |
| Test-seam count | 12 → 12 (unchanged) |
| Appendix D drop-ins | 4 → 4 (unchanged; §D.2 / §D.4 byte-faithfulness fixed mechanically) |
| Proto schema RPC count | 7 → 7 (unchanged) |
| Front-matter Inherits cite count | 81 → 82 (`[arch §9.2]` added) |
| Appendix B §B.5 sibling-doc cite count | 49 + catch-all → 49 + 2 grouped precedent paragraphs (Appendix-A set; Appendix-D set + sub-section variants) + catch-all (≈ 17 cite tokens added) |
| Codex findings disagreed (with Opus reasoning) | 2 prior + 1 (hypothetical `health()` reentrancy) → 3 |
| Doc length | 1639 → 1693 (+54 lines via mechanical RC#3 / RC#4 close, Appendix B §B.5 expansion, and pre-sign-off P3 sweep — §D.4 range labels `442–454` → `442–456` + `[const §XII]` / `[const §XV.9]` / `[const §XX]` added to Appendix B §B.2 with "inherited via verbatim quote" parenthetical) |

The Opus closing recommendation framed the round-2 risk as "v0.3 can ship after a single convergence pass." All 8 Opus-verdict findings are addressed above; 0 new root causes. v0.3 is signable in one round-3 verification pass per Opus's framing.

---

## Appendix D — Cross-doc drop-ins (orchestrator-applied at sign-off)

Each entry follows the `[2g App D]` / `[2h App D]` / `[2i App D]` byte-faithful Before / After pattern (RC#3 close — v0.1's "Sketch only" disclaimer is dropped). Per the `[2i App D]` rewriter rule precedent: at sign-off the orchestrator MUST re-verify each Before block against `git show HEAD:<path>`; if any drift, re-quote byte-faithfully before applying.

### §D.1 NEW catalogue row SVC-005 in `library/spec/feature-catalogue.md`

The catalogue's "Service (Daemon/Sidecar)" section currently carries SVC-001 through SVC-004; 2j sign-off adds SVC-005 at the end of the same block.

**Before** (lines 202–209 of `library/spec/feature-catalogue.md` — verbatim, byte-faithful):

```
## Service (Daemon/Sidecar)

| ID | Source | Category | Title | FIX version(s) | Spec ref | Status | /specify | PR | Tests | Verified |
|---|---|---|---|---|---|---|---|---|---|---|
| SVC-001 | OFFICIAL | service | gRPC control plane — session create/config/teardown/observability over Unix socket / named pipe | all | [impl] implementation | backlog | — | — | — | — |
| SVC-002 | OFFICIAL | service | iceoryx2 data plane — zero-copy SHM publish/subscribe for hot-path FIX messages | all | [impl] implementation | backlog | — | — | — | — |
| SVC-003 | OFFICIAL | service | Data plane opt-in — gRPC-only mode when iceoryx2 unavailable | all | [impl] implementation | backlog | — | — | — | — |
| SVC-004 | OFFICIAL | service | Service health / observability — gRPC health check + prometheus-compatible metrics | all | [impl] implementation | backlog | — | — | — | — |
```

**After** (the four existing rows preserved verbatim; one NEW SVC-005 row appended; the `Spec ref` for SVC-001 / SVC-004 also updated from `[impl] implementation` to the 2j section that pins the surface, mirroring the `[2i App D §D.1]` "covered by [2X §...]" pattern):

```
## Service (Daemon/Sidecar)

| ID | Source | Category | Title | FIX version(s) | Spec ref | Status | /specify | PR | Tests | Verified |
|---|---|---|---|---|---|---|---|---|---|---|
| SVC-001 | OFFICIAL | service | gRPC control plane — session create/config/teardown/observability over Unix socket / named pipe | all | [2j §4.6] / [2j §4.7] | backlog | `.specify/2j-controlplane.md` v0.3 | — | — | — |
| SVC-002 | OFFICIAL | service | iceoryx2 data plane — zero-copy SHM publish/subscribe for hot-path FIX messages | all | [impl] implementation | backlog | — | — | — | — |
| SVC-003 | OFFICIAL | service | Data plane opt-in — gRPC-only mode when iceoryx2 unavailable | all | [impl] implementation | backlog | — | — | — | — |
| SVC-004 | OFFICIAL | service | Service health / observability — gRPC health check + prometheus-compatible metrics | all | [2j §4.7] / [2j §4.8] | backlog | `.specify/2j-controlplane.md` v0.3 | — | — | — |
| SVC-005 | OFFICIAL | service | Pluggable control plane interface — `fixpp::service::ControlPlane` (3 pure-virtual: `start`, `stop`, `health`; ≤5 cap with 2 slots of headroom for v1.x auth-token rotation + RPC re-mapping per [2j §10] Q5); default impl gRPC over Unix socket (Linux) / named pipe (Windows); alternative impls (JSON-over-Unix-socket sample, ...) link without rebuilding the engine via the AGPL-boundary structural rule per `[const §V.1]` / `[arch §8]`; `EngineConfig::control_plane_factory` engine-anchor per `[2j Appendix D §D.2]`; handlers run on the engine executor per `[2d §7.8]`; `CloseSession` RPC consumes `[2h §7.6]` graceful-drain shape; rotation RPCs (`RotatePinset` / `ReloadCertSource`) deferred to v1.x per `[2j §10]` Q1 + Q9 | all | [2j §4.1] / [arch §4.11] | backlog | `.specify/2j-controlplane.md` v0.3 | — | — | — |
```

**Why.** Per `[const §VI.4]` bidirectional traceability + the `[2g App D]` / `[2h App D]` / `[2i App D §D.1]` precedent: design-doc-rooted catalogue rows get a Spec ref pivot from `[impl] implementation` to the actual design-doc section that pins the surface. The Status column stays `backlog` because Phase 4 implementation has not landed; the Spec ref column flips at design-doc sign-off. SVC-002 / SVC-003 keep `[impl] implementation` because 2j does not own them (RC#2 close); 2l's eventual sign-off pivots their Spec ref column.

### §D.2 NEW field in `[2d §4.4]` `EngineConfig` (one field, RC#1 close)

The `[2d §4.4]` `EngineConfig` struct currently carries the engine-level shared resources (executor, clock, dictionaries, default plugin selections, engine_trace_context). 2j sign-off appends one field — `control_plane_factory` — preserving the engine_trace_context block at the bottom. The v0.1 second field (`control_plane_executor`) is **dropped** per RC#1: handlers run on `EngineConfig::executor` per `[2d §7.8]`.

**Before** (lines 412–466 of `library/.specify/2d-threading.md` — verbatim, byte-faithful; v0.3 RC#3 close: outer fence is 4-backtick to escape the inner ` ```cpp ` fence at quoted line 414, per the precedent that `[2g App D]` / `[2i App D §D.2]` could not exercise — those quote markdown without inner code fences — but 2j §D.2 must escape):

````markdown
### 4.4 `fixpp::core::EngineConfig` — engine-level shared resources

```cpp
// include/fixpp/core/engine_config.hpp
namespace fixpp::core {

struct EngineConfig {
    // ── Required ─────────────────────────────────────────────────────────
    asio::any_io_executor    executor;            // engine pool — sessions derive strands from this.
    std::shared_ptr<Clock>   clock;               // engine-anchor clock; rejected if null at Engine::open
                                                  // regardless of session overrides (root cause #2).

    // ── Dictionary registry (root cause #2 / C-P1-1; closes [2c §10] Q10) ─
    // Engine-anchored list of dictionaries the engine pre-loads at init. The
    // engine constructs `dict::version_registry` from this list at Engine::open;
    // sessions reach the registry via Engine::version_registry() to resolve
    // FIXT.1.1 per-message ApplVerID(1128) overrides at dispatch time per
    // [2c §4.9] / [2c §6.3] Frame 3. Lifetime: dictionaries are engine-owned
    // (shared_ptr<const Dictionary> for keepalive); the registry holds borrowed
    // pointers per [2c §4.9]'s `get(application_version) -> Dictionary const*`
    // shape. SessionConfig::dictionary remains the per-session anchor for
    // non-FIXT.1.1 sessions.
    std::vector<std::shared_ptr<const fixpp::dict::Dictionary>> dictionaries;

    // ── Required, but defaultable ────────────────────────────────────────
    std::pmr::memory_resource*    default_message_resource = std::pmr::get_default_resource();
    std::pmr::memory_resource*    default_session_resource = std::pmr::get_default_resource();

    // ── Observability (optional in the API; effectively required for production) ─
    std::shared_ptr<fixpp::core::Logger>           logger;        // null → no-op.
    std::shared_ptr<fixpp::otel::TracerProvider>   tracer;        // null → no-op trace context.
    std::shared_ptr<fixpp::otel::MeterProvider>    meter;         // null → no-op metrics.

    // ── Default plugin selections (a session may override each in SessionConfig) ─
    std::shared_ptr<fixpp::session::MessageStoreFactory> default_store_factory;
    std::shared_ptr<fixpp::tls::cert_source>             default_cert_source;
    std::shared_ptr<fixpp::transport::TransportFactory>  default_transport_factory;

    // ── Engine-level fallback trace_context (per N-P2-2) ────────────────
    // Storage: held by the engine in a `std::atomic<trace_context>` snapshot
    // (trace_context is 32 bytes, lock-free atomic on supported platforms;
    // if not lock-free, a `seqlock` is acceptable). The
    // `current_trace_context` awaiter (§4.6) reads this snapshot when no
    // session-domain `session_local` slot is reachable (control-plane handlers,
    // listener accept). Updates between Engine::open() and Engine::close()
    // are permitted on this snapshot only — the snapshot is set once at
    // engine open from this field and may be updated through
    // `Engine::set_engine_trace_context(trace_context)` (an engine-scope
    // mutator separate from the frozen-config rule, since the engine-level
    // fallback is observability-shaped, not a session FSM input).
    fixpp::otel::trace_context engine_trace_context {};
};

}  // namespace fixpp::core
```
````

**After** (the existing `EngineConfig` body preserved verbatim; one NEW field appended after the existing `engine_trace_context {};` line and inside the struct's closing brace; the surrounding comment / namespace structure preserved; v0.3 RC#3 close: outer fence is 4-backtick by symmetry with the Before block):

````markdown
### 4.4 `fixpp::core::EngineConfig` — engine-level shared resources

```cpp
// include/fixpp/core/engine_config.hpp
namespace fixpp::core {

struct EngineConfig {
    // ── Required ─────────────────────────────────────────────────────────
    asio::any_io_executor    executor;            // engine pool — sessions derive strands from this.
    std::shared_ptr<Clock>   clock;               // engine-anchor clock; rejected if null at Engine::open
                                                  // regardless of session overrides (root cause #2).

    // ── Dictionary registry (root cause #2 / C-P1-1; closes [2c §10] Q10) ─
    // Engine-anchored list of dictionaries the engine pre-loads at init. The
    // engine constructs `dict::version_registry` from this list at Engine::open;
    // sessions reach the registry via Engine::version_registry() to resolve
    // FIXT.1.1 per-message ApplVerID(1128) overrides at dispatch time per
    // [2c §4.9] / [2c §6.3] Frame 3. Lifetime: dictionaries are engine-owned
    // (shared_ptr<const Dictionary> for keepalive); the registry holds borrowed
    // pointers per [2c §4.9]'s `get(application_version) -> Dictionary const*`
    // shape. SessionConfig::dictionary remains the per-session anchor for
    // non-FIXT.1.1 sessions.
    std::vector<std::shared_ptr<const fixpp::dict::Dictionary>> dictionaries;

    // ── Required, but defaultable ────────────────────────────────────────
    std::pmr::memory_resource*    default_message_resource = std::pmr::get_default_resource();
    std::pmr::memory_resource*    default_session_resource = std::pmr::get_default_resource();

    // ── Observability (optional in the API; effectively required for production) ─
    std::shared_ptr<fixpp::core::Logger>           logger;        // null → no-op.
    std::shared_ptr<fixpp::otel::TracerProvider>   tracer;        // null → no-op trace context.
    std::shared_ptr<fixpp::otel::MeterProvider>    meter;         // null → no-op metrics.

    // ── Default plugin selections (a session may override each in SessionConfig) ─
    std::shared_ptr<fixpp::session::MessageStoreFactory> default_store_factory;
    std::shared_ptr<fixpp::tls::cert_source>             default_cert_source;
    std::shared_ptr<fixpp::transport::TransportFactory>  default_transport_factory;

    // ── Engine-level fallback trace_context (per N-P2-2) ────────────────
    // Storage: held by the engine in a `std::atomic<trace_context>` snapshot
    // (trace_context is 32 bytes, lock-free atomic on supported platforms;
    // if not lock-free, a `seqlock` is acceptable). The
    // `current_trace_context` awaiter (§4.6) reads this snapshot when no
    // session-domain `session_local` slot is reachable (control-plane handlers,
    // listener accept). Updates between Engine::open() and Engine::close()
    // are permitted on this snapshot only — the snapshot is set once at
    // engine open from this field and may be updated through
    // `Engine::set_engine_trace_context(trace_context)` (an engine-scope
    // mutator separate from the frozen-config rule, since the engine-level
    // fallback is observability-shaped, not a session FSM input).
    fixpp::otel::trace_context engine_trace_context {};

    // ── Control plane (engine-anchor; 2j) ────────────────────────────────
    // Engine-scoped pluggable control-plane per [arch §4.11] / [2j §4.1].
    // Null permitted — the deployment may run without a control plane;
    // Engine::open does not reject null. When non-null, Engine::open
    // invokes factory->make(mr, engine, exec, cfg) once after IO executor
    // binding; the resulting ControlPlane's lifetime is engine lifetime.
    // Handler executor: per [2d §7.8] handlers run on EngineConfig::executor
    // (the engine executor); operators that want isolation pre-wrap that
    // executor in a strand or sub-executor before passing it to the factory
    // following the [2d §4.5] executor_override pattern. There is no
    // separate `control_plane_executor` field (RC#1 close: a separate field
    // would override [2d §7.8]).
    // Ownership shape (`unique_ptr`): aligns with the [2e App D §D.1]
    // `MessageStoreFactory` and [2h App D §D.1] `TransportFactory`
    // unique-ownership pattern per [arch §5.6]'s frozen-at-open rule.
    std::unique_ptr<fixpp::service::ControlPlaneFactory> control_plane_factory;
};

}  // namespace fixpp::core
```
````

**Why.** Per `[arch §4.11]` service-mode boundary + `[2d §7.8]` engine-executor model + `[arch §5.6]` frozen-at-open: ControlPlane is engine-scoped (one per engine); the factory belongs on `EngineConfig` (not `SessionConfig`); `unique_ptr` ownership matches the `[2e App D §D.1]` / `[2h App D §D.1]` plugin-factory trend. The handler-executor field is **not** added — handlers run on `EngineConfig::executor` per `[2d §7.8]`; this preserves the no-2d-amendment property (v0.2 §D.2 is a one-field append, not a contract change).

### §D.3 Numeric assignments in 2i's reserved `[900, 999]` block + audit-trail appends

The 2i v0.3 numeric layout at `[2i §1.1]` line 65 already reserves `[900, 999]` for "**RESERVED: 2j control plane (FIXPP_ERR_CTRL_*)**"; v0.2 assigns concrete values inside that block. **No 2i v0.4 amendment is owed** (RC#1 / RC#5 close — this is value assignment within an already-reserved block, plus an audit-trail append).

**(D.3.a) Append two lines to `tools/abi_history/error_codes_v1.txt`** per `[2i §4.3]` audit-trail mechanism:

```
900 FIXPP_ERR_CTRL_CONFIG  introduced=2j-v0.2
901 FIXPP_ERR_CTRL_RUNTIME introduced=2j-v0.2
```

(The exact format must match the existing rows in `tools/abi_history/error_codes_v1.txt`; the orchestrator MUST verify against the live file at sign-off and re-format if drift.)

**(D.3.b) Append two rows to `[2i §4.3]` numeric-layout table.** The 2i §4.3 table currently lists every assigned numeric per the `[0, 899]` blocks; the orchestrator appends:

```
| 900  | FIXPP_ERR_CTRL_CONFIG   | 2j | configuration error (bad listen path, malformed mTLS, factory failed) |
| 901  | FIXPP_ERR_CTRL_RUNTIME  | 2j | runtime error (internal-invariant violation, stream overflow) |
```

(Cancellation `control_plane_*_cancelled` reuses the existing `FIXPP_ERR_CANCELLED = 1` per `[2i §4.9]`; no row is added for cancellation.)

**(D.3.c) Create `tools/abi_history/proto_v1.txt`** at 2j sign-off (NEW append-only file). Mirrors `tools/abi_history/error_codes_v1.txt` per `[2i §4.3]` precedent. The file pins every published `service/proto/fixpp_control.proto` symbol — RPC names, message field tags + names + types, enum values — with the doc revision (v0.2) that introduced it. Per §4.7.1 rule 6. The exact content is generated at sign-off from the locked-down proto schema; the file is the §9 seam #12 audit target.

**Why.** `[2i §1.1]` already reserves the numeric block; 2j sign-off picks values inside it (fully under the per-block-densification rule per `[2i §1.1]` "Each domain owner keeps the right to densify their own 100-wide block over v1.x without consulting 2i"). The audit-trail file mirrors the established precedent. Per RC#3, the Before / After are byte-faithful at sign-off (the orchestrator regenerates the audit-trail content from the live proto at apply time).

### §D.4 NEW entry in `library/spec/coverage-index.md` "Catalogue ID supplemental notes" — SVC-005 supplemental

The supplemental-notes section at `library/spec/coverage-index.md:442–456` currently carries notes for `D-008`, `NFR-015`, `NFR-016`, and `CA-002` (the latter added at 2i v0.2 / v0.3). 2j sign-off appends an SVC-005 supplemental note between the `CA-002 supplemental:` paragraph and the `---` separator.

**Before** (lines 442–456 of `library/spec/coverage-index.md` — verbatim, byte-faithful at v0.3 authoring time 2026-05-09; v0.3 RC#3 close: placeholder bytes from v0.2 are replaced with the full live bytes per the `[2i App D §D.2]` precedent at `2i-capi.md:1907–1915`; the orchestrator MUST re-verify against the live file at sign-off and re-quote if drift):

````markdown
## Catalogue ID supplemental notes

Notes that supplement specific catalogue rows (`feature-catalogue.md`) without rewriting the row text. These record dispositions that emerged from Phase 2 design decisions and provide the bidirectional-traceability anchor per `[const §VI.4]`.

**D-008 supplemental:** Codegen scope for v1.0 = FIX 4.2, FIX 4.4, FIX 5.0 SP2, FIXT.1.1. Runtime-XML-only scope = FIX 4.0, FIX 4.1, FIX 4.3, FIX 5.0, FIX 5.0 SP1. The row title in `feature-catalogue.md` covers the broader 4.0–5.0 SP2 surface; codegen vs runtime-XML disposition lives here, in the coverage index. Per `[2c §1.3]` and `[2c Appendix A]`. Source: 2c v1.3 sign-off (2026-05-08); see `[2c Appendix D §2]`.

**NFR-015 supplemental:** Pluggable Clock interface — `fixpp::core::Clock` (4 pure-virtual methods: `now`, `steady_now`, `sleep_until`, `cancel_sleeps`) carried by `EngineConfig`. Source spec sections: `[arch §1.1] Goals` (pluggable clocks promise) and `[2d §4.1] fixpp::core::Clock — interface, lifetime, threading`. Default impl `fixpp::core::system_clock_source` per `[2d §4.2]` (per-session reusable `steady_timer` slot keyed by `Session*` from `session_arena`); test impl `fixpp::core::mock_clock` per `[2d §4.3]` (pimpl per `[const §XI.3]`). The `effective_clock = SessionConfig::clock_override ?: EngineConfig::clock` rule (per `[2d §7.9]`) routes heartbeat (S-003 / S-004), SendingTime, S-035 session scheduling, and session-scoped LOG/OBS records through the per-session clock; engine-scope LOG/OBS records read `EngineConfig::clock` directly and carry a `clock_scope = engine` discriminator. NFR-015 covers the **clock seam only**; the consuming-row owners (the session-module Phase-4 spec for S-003/S-004/S-035, **2k** for LOG-001..004 + OBS-001..003) discharge their own rows. Source: 2d v0.4 sign-off (2026-05-08); see `[2d §11]` drop-in language and `[2d Appendix A]`.

**NFR-016 supplemental:** Awaitable mutex `fixpp::sync::async_mutex` — own implementation per `[SYN §3.2 Q6b]` (BSL-1.0 algorithm attribution to avast/asio-mutex; cppcoro / Lewis-Baker `std::atomic<uintptr_t>` state with three-state `not_locked`/`locked_no_waiters`/pointer-to-LIFO encoding + mutex-owned `next_drain_head_` residual FIFO chain; per-waiter three-state `std::atomic<waiter_phase>` machine `{ queued, granted, cancelled }` arbitrating unlock/cancel CAS with WINNER-ONLY post-CAS `*result_` writes per v1.4 CAS-then-publish). Source spec sections: `[arch §1.1] Goals` (concurrency primitives promise) and `[2f §4.1] fixpp::sync::async_mutex class — public surface`. The six-item design list per `[SYN §3.2 Q6b]` is delivered via `[2f §4.2]` (waiter embedded in awaiter inside the caller's coroutine frame), `[2f §4.3]` (PMR-aware fallback via explicit `async_lock(mr)` overload + session-side helper `async_lock_via_session_executor`), `[2f §4.5]` (ASIO `cancellation_type::total` honoured via per-waiter `phase_` CAS to `cancelled`; awaitable completes with `expected_t::unexpected{sync_lock_aborted}` at the 2f boundary, mapped to `FIXPP_ERR_CANCELLED` at the C ABI per `[2d §6.7]`), `[2f §4.6]` (per-mutex `dispatch`/`post` policy with default `dispatch` and ASIO `running_in_this_thread()` predicate), `[2f §4.7]` (`std::terminate()` precondition on destruction + explicit mutex-owned `cancel_and_drain()` drain primitive with lazy `std::atomic<std::shared_ptr<detail::drain_latch_state>> drain_latch_ptr_` non-expiring during the drain epoch, published before `draining_` per v1.4 / I-1; `signal_release()` + `signal_abort()` + `notify()` per v1.5 / I-7 / I-8), and `[2f §9]` test seams (≥ 30 covering FIFO fairness across drain cycles, cancellation mid-wait, destructor-with-waiters, contention stress, TSan + ASan clean, plus the v1.4 / v1.5 seams covering CAS-then-publish arbitration, deterministic latch publication, subscriber-wake-on-reaper-abort, and `notify()` non-terminal wake). Locked executor-compat surface per `[2d §7.4]` (completion on awaiter's bound executor; honours `cancellation_type::total`; default `dispatch`); cross-doc amendments to `[2d §4.5]` (engine-internal `Session::session_arena()` accessor), `[2d §4.7]` (per-mode effect-table row + paragraph contract on `expected_t::unexpected{sync_lock_aborted}` cancellation outcome), and `[2d §7.4]` (locked surface bullet rewording) applied at sign-off per `[2f Appendix D §D.1–§D.3]`. Direct consumers: `MessageStore` writer mutex per `[2e §6.4]` (the named hard hand-off gate from `[2e §3.1]` last bullet), pinset rotation per `[2g]`, seqnum counter per Phase-4 session-module spec. CI enforcement of `[const §XV.9]` `std::mutex`-in-coroutine-context ban via `tools/check_no_std_mutex_in_awaitable_headers.sh` grep gate (clang-tidy custom check is post-v1). NFR-016 is the **primitive seam**; no other catalogue rows discharge through it (the consuming-row owners discharge their own rows). Source: 2f v1.5 sign-off (2026-05-08); see `[2f §11]` drop-in language and `[2f Appendix A]`.

**CA-002 supplemental:** `fixpp_error_t` numeric-block layout per `[2i §4.3]` v0.2: cross-cutting block `[0, 99]` (2i-owned, 11 codes — `FIXPP_ERR_OK` / `_CANCELLED` / `_UNKNOWN` per `[arch §5.3]` plus 8 2i-introduced variants `_NULL_HANDLE` through `_CAPI_CONFIG_INVALID`); WIRE `[100, 199]` (2b-owned, 13 occupied per `[2b §6.7]`); DICT `[200, 299]` (2c-owned, 20 occupied per `[2c §6.7]`); THREAD `[300, 399]` (2d-owned, 9 occupied per `[2d §6.7]` — count includes `dispatch_aborted` which still maps to `FIXPP_ERR_CANCELLED` at the C ABI per `[2i §4.9]`); STORE `[400, 499]` (2e-owned, 10 occupied per `[2e §6.7]`); SYNC `[500, 599]` (2f-owned, 4 occupied per `[2f §6.5]`); TLS `[600, 699]` (2g-owned, 15 occupied per `[2g §6.6]`); TRANSPORT `[700, 799]` (2h-owned, 22 occupied per `[2h §6.6]`); DECIMAL `[800, 899]` (2a-owned, 4 occupied per `[2a §7.4]`); reserved `[900, 1399]` for 2j / 2k / 2l / 2m / post-v1 growth; `[1400+]` reserved for future expansion. Live total of prior-doc variants = 4 + 13 + 20 + 9 + 10 + 4 + 15 + 22 = 97. Stability rule per `[SYN §3.5 #19]` / `[const §X.4]`: once a numeric value is published in a tagged C ABI release, it never changes meaning. Audit trail via `tools/abi_history/error_codes_v1.txt` (append-only); CI verifies no re-definitions. Occupancy drift gate `tools/check_capi_occupancy.sh` mechanically counts sibling `[2X §6.X]` rows and asserts the published counts match. Per-block growth is a domain-doc amendment; cross-block growth is a 2i amendment per `[const §XX]`. Source: 2i v0.3 (2026-05-09); see `[2i §4.3]` numeric-block table and `[2i §4.4]` `fixpp_strerror()` lookup discipline.

---

## Post-1.0 Gap Registry
````

**After** (the existing four supplemental paragraphs preserved verbatim; one NEW SVC-005 supplemental paragraph appended between the `CA-002 supplemental:` paragraph and the `---` separator; v0.3 RC#3 close: After block now applies the diff against the full live Before bytes, not against placeholders):

````markdown
## Catalogue ID supplemental notes

Notes that supplement specific catalogue rows (`feature-catalogue.md`) without rewriting the row text. These record dispositions that emerged from Phase 2 design decisions and provide the bidirectional-traceability anchor per `[const §VI.4]`.

**D-008 supplemental:** Codegen scope for v1.0 = FIX 4.2, FIX 4.4, FIX 5.0 SP2, FIXT.1.1. Runtime-XML-only scope = FIX 4.0, FIX 4.1, FIX 4.3, FIX 5.0, FIX 5.0 SP1. The row title in `feature-catalogue.md` covers the broader 4.0–5.0 SP2 surface; codegen vs runtime-XML disposition lives here, in the coverage index. Per `[2c §1.3]` and `[2c Appendix A]`. Source: 2c v1.3 sign-off (2026-05-08); see `[2c Appendix D §2]`.

**NFR-015 supplemental:** Pluggable Clock interface — `fixpp::core::Clock` (4 pure-virtual methods: `now`, `steady_now`, `sleep_until`, `cancel_sleeps`) carried by `EngineConfig`. Source spec sections: `[arch §1.1] Goals` (pluggable clocks promise) and `[2d §4.1] fixpp::core::Clock — interface, lifetime, threading`. Default impl `fixpp::core::system_clock_source` per `[2d §4.2]` (per-session reusable `steady_timer` slot keyed by `Session*` from `session_arena`); test impl `fixpp::core::mock_clock` per `[2d §4.3]` (pimpl per `[const §XI.3]`). The `effective_clock = SessionConfig::clock_override ?: EngineConfig::clock` rule (per `[2d §7.9]`) routes heartbeat (S-003 / S-004), SendingTime, S-035 session scheduling, and session-scoped LOG/OBS records through the per-session clock; engine-scope LOG/OBS records read `EngineConfig::clock` directly and carry a `clock_scope = engine` discriminator. NFR-015 covers the **clock seam only**; the consuming-row owners (the session-module Phase-4 spec for S-003/S-004/S-035, **2k** for LOG-001..004 + OBS-001..003) discharge their own rows. Source: 2d v0.4 sign-off (2026-05-08); see `[2d §11]` drop-in language and `[2d Appendix A]`.

**NFR-016 supplemental:** Awaitable mutex `fixpp::sync::async_mutex` — own implementation per `[SYN §3.2 Q6b]` (BSL-1.0 algorithm attribution to avast/asio-mutex; cppcoro / Lewis-Baker `std::atomic<uintptr_t>` state with three-state `not_locked`/`locked_no_waiters`/pointer-to-LIFO encoding + mutex-owned `next_drain_head_` residual FIFO chain; per-waiter three-state `std::atomic<waiter_phase>` machine `{ queued, granted, cancelled }` arbitrating unlock/cancel CAS with WINNER-ONLY post-CAS `*result_` writes per v1.4 CAS-then-publish). Source spec sections: `[arch §1.1] Goals` (concurrency primitives promise) and `[2f §4.1] fixpp::sync::async_mutex class — public surface`. The six-item design list per `[SYN §3.2 Q6b]` is delivered via `[2f §4.2]` (waiter embedded in awaiter inside the caller's coroutine frame), `[2f §4.3]` (PMR-aware fallback via explicit `async_lock(mr)` overload + session-side helper `async_lock_via_session_executor`), `[2f §4.5]` (ASIO `cancellation_type::total` honoured via per-waiter `phase_` CAS to `cancelled`; awaitable completes with `expected_t::unexpected{sync_lock_aborted}` at the 2f boundary, mapped to `FIXPP_ERR_CANCELLED` at the C ABI per `[2d §6.7]`), `[2f §4.6]` (per-mutex `dispatch`/`post` policy with default `dispatch` and ASIO `running_in_this_thread()` predicate), `[2f §4.7]` (`std::terminate()` precondition on destruction + explicit mutex-owned `cancel_and_drain()` drain primitive with lazy `std::atomic<std::shared_ptr<detail::drain_latch_state>> drain_latch_ptr_` non-expiring during the drain epoch, published before `draining_` per v1.4 / I-1; `signal_release()` + `signal_abort()` + `notify()` per v1.5 / I-7 / I-8), and `[2f §9]` test seams (≥ 30 covering FIFO fairness across drain cycles, cancellation mid-wait, destructor-with-waiters, contention stress, TSan + ASan clean, plus the v1.4 / v1.5 seams covering CAS-then-publish arbitration, deterministic latch publication, subscriber-wake-on-reaper-abort, and `notify()` non-terminal wake). Locked executor-compat surface per `[2d §7.4]` (completion on awaiter's bound executor; honours `cancellation_type::total`; default `dispatch`); cross-doc amendments to `[2d §4.5]` (engine-internal `Session::session_arena()` accessor), `[2d §4.7]` (per-mode effect-table row + paragraph contract on `expected_t::unexpected{sync_lock_aborted}` cancellation outcome), and `[2d §7.4]` (locked surface bullet rewording) applied at sign-off per `[2f Appendix D §D.1–§D.3]`. Direct consumers: `MessageStore` writer mutex per `[2e §6.4]` (the named hard hand-off gate from `[2e §3.1]` last bullet), pinset rotation per `[2g]`, seqnum counter per Phase-4 session-module spec. CI enforcement of `[const §XV.9]` `std::mutex`-in-coroutine-context ban via `tools/check_no_std_mutex_in_awaitable_headers.sh` grep gate (clang-tidy custom check is post-v1). NFR-016 is the **primitive seam**; no other catalogue rows discharge through it (the consuming-row owners discharge their own rows). Source: 2f v1.5 sign-off (2026-05-08); see `[2f §11]` drop-in language and `[2f Appendix A]`.

**CA-002 supplemental:** `fixpp_error_t` numeric-block layout per `[2i §4.3]` v0.2: cross-cutting block `[0, 99]` (2i-owned, 11 codes — `FIXPP_ERR_OK` / `_CANCELLED` / `_UNKNOWN` per `[arch §5.3]` plus 8 2i-introduced variants `_NULL_HANDLE` through `_CAPI_CONFIG_INVALID`); WIRE `[100, 199]` (2b-owned, 13 occupied per `[2b §6.7]`); DICT `[200, 299]` (2c-owned, 20 occupied per `[2c §6.7]`); THREAD `[300, 399]` (2d-owned, 9 occupied per `[2d §6.7]` — count includes `dispatch_aborted` which still maps to `FIXPP_ERR_CANCELLED` at the C ABI per `[2i §4.9]`); STORE `[400, 499]` (2e-owned, 10 occupied per `[2e §6.7]`); SYNC `[500, 599]` (2f-owned, 4 occupied per `[2f §6.5]`); TLS `[600, 699]` (2g-owned, 15 occupied per `[2g §6.6]`); TRANSPORT `[700, 799]` (2h-owned, 22 occupied per `[2h §6.6]`); DECIMAL `[800, 899]` (2a-owned, 4 occupied per `[2a §7.4]`); reserved `[900, 1399]` for 2j / 2k / 2l / 2m / post-v1 growth; `[1400+]` reserved for future expansion. Live total of prior-doc variants = 4 + 13 + 20 + 9 + 10 + 4 + 15 + 22 = 97. Stability rule per `[SYN §3.5 #19]` / `[const §X.4]`: once a numeric value is published in a tagged C ABI release, it never changes meaning. Audit trail via `tools/abi_history/error_codes_v1.txt` (append-only); CI verifies no re-definitions. Occupancy drift gate `tools/check_capi_occupancy.sh` mechanically counts sibling `[2X §6.X]` rows and asserts the published counts match. Per-block growth is a domain-doc amendment; cross-block growth is a 2i amendment per `[const §XX]`. Source: 2i v0.3 (2026-05-09); see `[2i §4.3]` numeric-block table and `[2i §4.4]` `fixpp_strerror()` lookup discipline.

**SVC-005 supplemental:** Pluggable control-plane interface — `fixpp::service::ControlPlane` (3 pure-virtual methods: `start`, `stop`, `health`; under the `[const §XIV.2]` ≤ 5 cap with 2 slots reserved for v1.x `RotateAuthToken` / `RemapRpcs` per `[2j §10]` Q5). Source spec sections: `[arch §4.11] service` (the surface inventory) and `[2j §4.1] fixpp::service::ControlPlane — abstract interface`. Default impl `fixpp::service::grpc_control_plane` per `[2j §4.6]` (Unix domain socket on Linux / named pipe on Windows; TCP opt-in per `[arch §8.1]`). The proto schema `service/proto/fixpp_control.proto` per `[2j §4.7]` is on the `[arch §9.3]` "Stable from v1.0" tier; proto-evolution rules pinned in `[2j §4.7.1]` (additive-only expansion via MINOR bumps; removals are MAJOR breaks). v1.0 RPC surface: `OpenSession`, `CloseSession`, `Configure` (reserved-empty per `[2j §4.7.1]` additive expansion path), `StreamMetrics`, `StreamLogs`, `StreamSessionEvents`, `Health` (gRPC standard health-check). `RotatePinset` and `ReloadCertSource` are deferred to v1.x per `[2j §10]` Q1 + Q9 (the v1.0 cross-doc state has no AGPL-legal path: `[2i §2]` non-goal #6 declines the C-ABI rotation surface; `service/grpc/*.cpp` cannot include `<fixpp/tls/...>` per `[arch §8]`). AGPL-boundary structural enforcement per `[2j §4.4]` / `[2j §4.6]` + `tools/check_layers.py` lint per `[arch §8]` enforcement bullet (first-landing tracked at `[2j §10]` Q10). Stream backpressure: close-on-overflow with `control_plane_stream_overflow` per `[2j §4.8]` / `[2j §6.4]` (consistent with `[const §XV.15]` no-drop-oldest-on-app-paths; `[const §XIII.2]` permits but does not require drop-oldest on observability paths — v1.0 picks close-on-overflow for visibility). The proto-stability audit-trail file `tools/abi_history/proto_v1.txt` (NEW at 2j sign-off per `[2j App D §D.3]`) mirrors the `tools/abi_history/error_codes_v1.txt` precedent. Source: 2j v0.3 (2026-05-09); see `[2j §11]` drop-in language and `[2j Appendix A]`.

---

## Post-1.0 Gap Registry
````

**Why.** Mirrors the `NFR-015` / `NFR-016` / `CA-002` precedent: design-doc-rooted catalogue rows that pin a non-trivial structural invariant get a supplemental note in the coverage index. SVC-001 / SVC-004 are simpler — their `Spec ref` pivot in §D.1 alone is enough; SVC-005 is the NEW row + the structural rules (≤ 5 pure-virtual cap usage, proto-evolution rules, AGPL boundary, deferral list, backpressure choice) and warrants the supplemental note for cross-doc reachability per `[const §VI.4]`. (SVC-002 / SVC-003 do not get supplemental notes here — those are 2l's per RC#2 close; 2l's eventual sign-off adds them.)

The v0.1 §D.5 / §D.6 history is captured in Appendix C v0.1 → v0.2 entry; v0.2 consolidated those entries into the current four-drop-in shape (§D.1 / §D.2 / §D.3 / §D.4) and v0.3 preserves that consolidation.

---

## Appendix Z — post-sign-off amendment, 2026-08-29 (§6.5 / §6.3)

*Appended at the end of the file on purpose. An insertion higher up shifts every line-number
citation into this document — a defect this very amendment exists to stop repeating.*

> ⚠️ **§6.5's executor-topology sentence is DELETED, not refreshed.** It read: *"The engine executor is shared with the engine's listener accept and
> engine-bootstrap coroutines per `[2d §7.8]` — it is not shared with session strands."* The second
> clause was **falsified by feature 023 (T010)**: the accept loop is now `co_spawn`ed on the
> **per-session strand**, so accept work runs *inside* a session serialisation domain. No corrected
> topology is written in its place — restating it here would rot again on the next threading change,
> and the doc has no mechanism that would notice. **Derive it from the spawn site**, which cannot go
> stale silently because it is the thing being described:
>
> ```bash
> grep -n "co_spawn" src/session/engine.cpp        # which executor each role loop is spawned on
> grep -n "session_strand.emplace" src/session/engine.cpp   # what that strand is layered over
> ```
>
> **What still holds and what does not.** The *claim above* — a control-plane handler cannot stall a
> session strand — is 2j's own and is unaffected; it is a statement about the handler, not about the
> accept loop. The deleted sentence was a *supporting* topology assertion borrowed from `[2d §7.8]`,
> and borrowing is exactly how it went stale without anyone editing this file (same class as the
> `[const §XII.5]` fossil in `2g-tls.md`).
>
> ⚠️ **§6.3's cross-strand handoff budget is now UNVERIFIED, not verified-clean.** The `≤ 100 µs p99`
> idle / `≤ 5 ms p99` loaded figures were derived when the target session strand hosted no accept
> work. It now hosts an accept loop whose handshake leg is bounded at `tls_handshake_timeout`
> (`1500 ms` at `src/session/engine.cpp`) plus a bounded first-frame read. This is **not** a
> statement that the budget is wrong — an ASIO strand is released across every `co_await`, so a
> queued dispatch waits only for the current contiguous run, not for a whole handshake. It is a
> statement that the **premise the number was derived under no longer holds and the number was never
> re-derived.** Re-measure before citing it.
