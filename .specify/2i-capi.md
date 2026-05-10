# 2i — C ABI message representation + `fixpp_error_t` enum range

**Status:** Draft v0.3 — Gate A round 2 converged (Phase A — Opus designer)
**Date:** 2026-05-09
**Owner:** Opus (drafter)
**Inherits:** `[const §I.2]`, `[const §IV.2]`, `[const §V.1]`, `[const §VIII.5]`, `[const §X]` (full article — every sub-clause), `[const §XI.2]`, `[const §XIII]`, `[const §XIV.2]`, `[const §XV.15]`, `[const §XVII.1]`, `[const §XVIII.1]`, `[arch §1.2]`, `[arch §3]`, `[arch §4.10]`, `[arch §5.3]`, `[arch §5.5]`, `[arch §5.6]`, `[arch §9.1]`, `[arch §9.2]`, `[arch §10] row 2i`, `[SYN §3.5 #17]`, `[SYN §3.5 #19]`, `[2a §5.1]`, `[2a §5.2]`, `[2a §7.3]`, `[2a §7.4]`, `[2b §4.2]`, `[2b §4.3]`, `[2b §6.4]`, `[2b §6.7]`, `[2c §4.7]`, `[2c §4.8]`, `[2c §5]`, `[2c §6.7]`, `[2d §4.7]`, `[2d §6.5]`, `[2d §6.7]`, `[2d §7.6]`, `[2e §4.4]`, `[2e §6.7]`, `[2f §4.5]`, `[2f §6.5]`, `[2g §5]`, `[2g §6.6]`, `[2g §7.6]`, `[2h §5]`, `[2h §6.6]`, `[2h §7.8]`
**Cites:** see Appendix B (every reference grouped by source).
**Catalogue rows owned:** CA-001 (sole), CA-002 (sole), CA-003 (sole), CA-004 (sole), CA-008 (sole), CA-009 (sole), CA-010 (sole), CA-005 / CA-006 / CA-007 (shape-only cross-cut to 2j control plane + Phase-4 session-module spec — see Appendix A.2)
**Convergence log:** v0.2 addresses Codex review (4 P1 / 3 P2 / 1 P3) and Opus adversarial review (combined 6 P1 / 4 P2 / 3 P3; 3 root causes; 0 Codex findings disagreed; 5 new Opus findings); v0.3 addresses Codex round-2 review (2 P1 / 2 P2 / 1 P3 — both P1s disagreed/demoted by Opus; round-1-fix verification: 11 PASS / 2 FAIL claimed by Codex but FAILs disagreed by Opus) and Opus round-2 adversarial review (combined 0 P1 / 1 P2 / 4 P3, 0 new root causes); see Appendix C.

---

## §1 Goals

2i locks the C ABI surface for FIX message manipulation and the `fixpp_error_t` enum. Concretely:

1. Publish `<fix/c_api.h>` as the single umbrella header per `[arch §4.10]` / `[arch §7.3]`, with the per-domain split `c_api/error.h`, `c_api/version.h`, `c_api/message.h`, `c_api/dict.h`, `c_api/store.h`, `c_api/engine.h`, `c_api/session.h`, `c_api/log.h`, `c_api/otel.h`. 2i owns `c_api/error.h`, `c_api/version.h`, `c_api/message.h` end-to-end. The other split headers' shapes are owned by their domain docs (2c, 2e, 2j, 2k); 2i pins only the **opaque-handle plumbing rules** — declaration form, ownership/destroy discipline, reentrancy annotation — that those headers must follow.
2. Lock the **opaque-handle catalogue** per `[arch §4.10]`: `fixpp_engine_t`, `fixpp_session_t`, `fixpp_msg_t`, `fixpp_dict_t`, `fixpp_store_t`. Each is declared as an incomplete forward struct with a uniform destroy discipline; per-handle behaviour cross-references the owning sibling doc.
3. Lock `fixpp_error_t` as a bounded enum with explicit per-domain numeric blocks (`FIXPP_ERR_WIRE_*`, `FIXPP_ERR_DICT_*`, `FIXPP_ERR_THREAD_*`, `FIXPP_ERR_STORE_*`, `FIXPP_ERR_SYNC_*`, `FIXPP_ERR_TLS_*`, `FIXPP_ERR_TRANSPORT_*`, `FIXPP_ERR_DECIMAL_*`, plus the 2i-introduced cross-cutting block, plus reserved blocks for 2j / 2k / 2l / 2m / post-v1.x). Stability rules per `[const §X.4]` and `[SYN §3.5 #19]` — once published, a numeric value never changes meaning.
4. Lock the forward-compat translation behaviour per `[arch §5.3]` last bullet: out-of-range values from older consumers tolerated by the engine; out-of-range values flowing *to* a consumer mapped to `FIXPP_ERR_UNKNOWN`.
5. Lock `fixpp_strerror()` as the runtime lookup helper — alloc-free, thread-safe, table-backed.
6. Lock the **versioning macros** `FIXPP_C_ABI_VERSION_MAJOR/MINOR/PATCH` per `[const §X.1]` SemVer rules + the `fixpp_version()` runtime accessor (CA-004).
7. Publish field accessors (CA-008): `fixpp_msg_get_string`, `fixpp_msg_get_int`, `fixpp_msg_get_double`, `fixpp_msg_get_decimal` (PoD-at-the-boundary per `[const §X.3]` / `[2a §5.1]`).
8. Publish field setters (CA-009): `fixpp_msg_set_string`, `fixpp_msg_set_int`, `fixpp_msg_set_double`, `fixpp_msg_set_decimal`.
9. Publish the repeating-group accessor (CA-010): `fixpp_msg_get_group` + `fixpp_group_get_field_*`.
10. Pin the **C ↔ C++ translation boundary** at `fixpp::capi::detail::*` thunks: every `extern "C"` symbol is one thunk over an engine-internal C++ object; no exception ever crosses the boundary; trap-and-translate is the rule per `[const §VIII.5]` / `[arch §5.3]`.
11. Pin the **per-symbol reentrancy annotation** taxonomy per `[const §X.5]`: `FIXPP_THREAD_SAFE` / `FIXPP_SINGLE_THREAD` / `FIXPP_REQUIRES_SESSION_LOCK`. Every public C ABI symbol carries exactly one annotation in its declaring header (grep-CI enforced).
12. Close `[arch §10]` 2i row hand-off: 2j (engine/session lifecycle) consumes the handle-shape contract; 2k (log + otel C ABI) consumes the error-enum range; 2m (SWIG/Python) consumes the entire `extern "C"` surface as input.

### §1.1 Magnitude domain — what the `fixpp_error_t` numeric range is sized for

**Headroom requirement.** Each per-domain block must accommodate the current variant count plus ≥ 2× growth across v1.x. Counts are taken verbatim from each prior 2X doc's signed-off `§6.X` errors section (cite at point of use):

| Prefix | Owning doc | Current variants (per signed-off doc) | Source section |
|---|---|---|---|
| `FIXPP_ERR_DECIMAL_*` | 2a | 4 | `[2a §7.4]` |
| `FIXPP_ERR_WIRE_*` | 2b | 13 | `[2b §6.7]` |
| `FIXPP_ERR_DICT_*` | 2c | 20 | `[2c §6.7]` |
| `FIXPP_ERR_THREAD_*` | 2d | 9 (including `dispatch_aborted`, which still maps to `FIXPP_ERR_CANCELLED` at the C ABI per §4.9; the count discipline is variant-count, not coalescing-group count — every other prefix doc's count includes its cancellation row, e.g., `[2e §6.7]` includes `store_cancelled` in 10, `[2f §6.5]` includes `sync_lock_aborted` in 4, `[2g §6.6]` includes `tls_load_cancelled` in 15, `[2h §6.6]` includes 5 `transport_*_cancelled` rows in 22) | `[2d §6.7]` |
| `FIXPP_ERR_STORE_*` | 2e | 10 | `[2e §6.7]` |
| `FIXPP_ERR_SYNC_*` | 2f | 4 | `[2f §6.5]` |
| `FIXPP_ERR_TLS_*` | 2g | 15 | `[2g §6.6]` |
| `FIXPP_ERR_TRANSPORT_*` | 2h | 22 | `[2h §6.6]` |
| `FIXPP_ERR_CAPI_*` (2i-introduced) | 2i | 8 (this doc — §6.5; v0.2 adds `FIXPP_ERR_CAPI_CONFIG_INVALID` per RC#3 close) | §6.5 |
| `FIXPP_ERR_THREAD_SESSION_*` (lifecycle subset) | 2d (already counted above; rebranded sub-group) | (subset of 2d) | `[2d §6.7]` |
| `FIXPP_ERR_BINDING_*` (2m-introduced) | 2m | 5 | `[2m §6.7]` |

**Block width.** 100 codes per block (e.g., `FIXPP_ERR_WIRE_*` lives in `[100, 199]`). Worst-current-occupancy is 22 variants (2h transport); 2× growth = 44; 4× growth = 88. A 100-wide block accommodates ≥ 4× the worst current count and ≥ 5× the project median (~10–12 variants per doc). `int32_t` storage gives 2³¹ headroom; we use ≤ 1500 of those across v1.x for a budget of 15 100-wide blocks.

**Reserved blocks.** Five blocks are reserved for downstream / future docs that have not yet published their variant list: 2j (control plane), 2k (log + otel), 2l (session-tap consumer), 2m (SWIG/Python translation), plus one **post-v1.x growth** block. Reserved blocks are documented as "may not be used by 2i v0.1 publication"; assignment to a downstream doc happens at that doc's sign-off.

**Sentinel codes.** `FIXPP_ERR_OK = 0`, `FIXPP_ERR_CANCELLED = 1`, `FIXPP_ERR_UNKNOWN = 2`, plus the 2i-introduced `FIXPP_ERR_NULL_HANDLE`, `FIXPP_ERR_INVALID_HANDLE`, `FIXPP_ERR_VERSION_MISMATCH`, `FIXPP_ERR_BUFFER_TOO_SMALL`, `FIXPP_ERR_TYPE_MISMATCH`, `FIXPP_ERR_TAG_NOT_FOUND`, `FIXPP_ERR_INDEX_OUT_OF_RANGE`, `FIXPP_ERR_CAPI_CONFIG_INVALID` (the latter NEW in v0.2 / RC#3 close — the cross-cutting fallback for construction-time C-ABI thunk exceptions where no domain `_CONFIG` code applies). These live in the **cross-cutting block** `[0, 99]` and are intentionally low-numbered so no domain prefix can accidentally collide with the sentinels.

**Layout (final, this version):**

```
[0,    99]   FIXPP_ERR_CAPI_*       (cross-cutting; 2i-owned; 11 occupied = 3 architectural sentinels per [arch §5.3] + 8 2i-introduced)
[100,  199]  FIXPP_ERR_WIRE_*       (2b-owned; 13 occupied)
[200,  299]  FIXPP_ERR_DICT_*       (2c-owned; 20 occupied)
[300,  399]  FIXPP_ERR_THREAD_*     (2d-owned; 9 occupied; lifecycle sub-group rooted here)
[400,  499]  FIXPP_ERR_STORE_*      (2e-owned; 10 occupied)
[500,  599]  FIXPP_ERR_SYNC_*       (2f-owned; 4 occupied)
[600,  699]  FIXPP_ERR_TLS_*        (2g-owned; 15 occupied)
[700,  799]  FIXPP_ERR_TRANSPORT_*  (2h-owned; 22 occupied)
[800,  899]  FIXPP_ERR_DECIMAL_*    (2a-owned; 4 occupied — this is intentionally placed AFTER transport because 2a's family was authored first but is small; placement after the larger blocks gives the smaller-domain block more room without renumbering 2b-2h)
[900,  999]  FIXPP_ERR_CTRL_*       (2j-owned; 2 occupied per [2j §6.6]; assigned at 2j sign-off 2026-05-09)
[1000, 1099] RESERVED: 2k log + otel (FIXPP_ERR_LOG_*, FIXPP_ERR_OTEL_*)
[1100, 1199] RESERVED: 2l tap (FIXPP_ERR_TAP_*)
[1200, 1299] FIXPP_ERR_BINDING_*  (2m-owned per [2m §6.7]; 5 occupied; assigned at 2m sign-off 2026-05-10)
[1300, 1399] RESERVED: post-v1.x growth (one of: SOFH, FIX-Latest, SBE, FIXP, FAST per [const §XVIII.2])
[1400+]      RESERVED: future expansion
```

Each domain owner keeps the right to **densify** their own 100-wide block over v1.x without consulting 2i (e.g., 2h growing from 22 to 40 variants is a 2h amendment, not a 2i amendment); cross-block growth (a 2g variant overflowing into the `[700, 799]` 2h block) requires a 2i amendment per `[const §XX]` because it touches the C ABI surface.

### §1.2 Scope boundary — what 2i owns vs what it doesn't

**2i owns:**
- The numeric layout of `fixpp_error_t` (this doc, §4.3).
- The opaque-handle declaration form and destroy discipline (§4.2).
- The C-side message accessor / setter / group surface (§4.6, §4.7, §4.8).
- The versioning macros and `fixpp_version()` runtime accessor (§4.5).
- The `fixpp_strerror()` lookup helper (§4.4).
- The reentrancy-annotation taxonomy (§4.10).
- The C ↔ C++ thunk shape (§5).

**2i does NOT own:**
- **Per-handle behaviour** for `fixpp_engine_t` / `fixpp_session_t`: lifecycle, FSM, configure semantics, send / receive callbacks (CA-005 / CA-006 / CA-007). Owned by **2j** (control plane interface and gRPC default impl) and the **Phase-4 session-module spec** (the session FSM behaviour itself). 2i owns the **shape** of those entry points (opaque-handle plumbing, error-code translation, reentrancy annotation); the **semantics** behind them belong downstream. See §7.9.
- **Wire parser behaviour and offset table** (the C accessor thunks delegate into `wire::MessageView::get<Tag>()`). Owned by **2b**.
- **Dictionary loading and version selection.** The `fixpp_dict_t` opaque handle is owned by 2c at the C++ side (`dict::Dictionary`); 2i owns the C-ABI plumbing only. See §7.3.
- **Decimal type internals.** 2a owns `fixpp_decimal_t` (the PoD struct shape — it lives at `[2a §5.1]`); 2i owns the **field-accessor `fixpp_msg_get_decimal` / `fixpp_msg_set_decimal`** that wraps it. See §7.1.
- **Store API** behind `fixpp_store_t`. Owned by **2e**; 2i owns the handle-shape plumbing only. See §7.5.
- **TLS surface** — no `fixpp_cert_source_t` / `fixpp_pinset_t` C-ABI accessors are exposed in v1.0. The `[2g §7.6]` partition keeps cert/pinset rotation as a C++-only surface for v1.0; the C-ABI consumer triggers reload via 2j's control plane (a `ReloadCertSource` RPC, not a C-ABI call). See §7.7.
- **Transport surface.** No `fixpp_transport_t` C-ABI accessor in v1.0; the consumer uses `fixpp_session_t` + `fixpp_session_send` / receive callback. The transport handle exists in 2h's design (`[2h §5]`) for completeness but is delegated to 2j when an external consumer needs raw transport access. See §7.8.
- **C-ABI logging / otel symbols.** Owned by **2k** in headers `c_api/log.h` and `c_api/otel.h`. 2i provides the `fixpp_error_t` range those headers consume. See §7.10.
- **SWIG / Python translation.** 2m consumes 2i's C-ABI surface as input; the Python `FixppError` / GIL handling / async-queue handoff design is 2m's concern. See §7.12.

The `[const §XIV.2]` ≤5-pure-virtual cap **does not apply** to 2i's surface — there is no pure-virtual interface here; the entire C-ABI is `extern "C"` thunks. The article's *spirit* (small, focused interfaces) is honoured by the per-domain header split: each `c_api/<domain>.h` keeps its symbol set bounded.

---

## §2 Non-goals

Explicit non-goals for 2i v1.0:

1. **Engine / session lifecycle behaviour.** The C-ABI shape of `fixpp_engine_create` / `fixpp_session_open` / `fixpp_session_close` / `fixpp_session_send` / receive-callback registration is sketched here at the **shape** level only (signature, opaque-handle plumbing, error-code translation, reentrancy annotation). Per-handle FSM semantics, session-recovery rules, gap-fill behaviour, configuration validation rules — all owned by **2j** + Phase-4 session-module spec. CA-005 / CA-006 / CA-007 are cross-cut rows here.
2. **Per-message wire parsing.** The C accessor `fixpp_msg_get_string` thunks into `wire::MessageView::get<Tag>()` from 2b; 2i does not re-implement parsing.
3. **Dictionary / version selection.** The 2c version-namespacing model is internal; 2i's accessors are `(tag, view)`-based, dictionary-agnostic on read AND on set.
4. **SWIG / Python translation.** 2m owns the Python binding, the GIL handling, the exception-to-`FixppError` mapping, the async-queue handoff for receive callbacks. 2i's surface IS 2m's input; that is the only relationship.
5. **C-ABI logging / OpenTelemetry symbols.** `c_api/log.h` and `c_api/otel.h` are owned by **2k**. 2i provides the `fixpp_error_t` range those headers consume + the opaque-handle plumbing rule.
6. **TLS rotation accessors.** No `fixpp_pinset_add` / `fixpp_pinset_remove` / `fixpp_cert_source_reload` symbols in v1.0. The cert/pinset rotation surface stays C++-only per `[2g §7.6]`; cross-process / non-C++ rotation triggers go through 2j's control plane.
7. **Transport-level C-ABI accessors.** No `fixpp_transport_async_read` in v1.0. The C consumer talks to a session, not a transport.
8. **Streaming serialise API.** v1.0 has only single-message commit (`fixpp_msg_commit` returns the serialised span; the consumer ships it via `fixpp_session_send`). A streaming `Writer` C-ABI is post-v1; recorded in §10.
9. **Dynamic plugin loading via `dlopen` / `LoadLibrary`.** Per `[arch §1.2]` / `[const §XIV.4]` v1.0 plugins are compile-time only; there is no `fixpp_load_plugin(...)` C-ABI symbol.
10. **C++ types in C-ABI headers.** Per `[const §X.2]` / `[arch §9.1]` no `<atomic>`, no `<type_traits>`, no `<asio>`, no template syntax, no `nullptr` (use `NULL`), no `bool` without `<stdbool.h>`. The headers are pure C, also compilable as C++.

---

## §3 Inherited surface

This section quotes the inherited contract verbatim with file:line citations so the reader can re-verify against live source.

### §3.1 From `[arch §4.10]` — the capi/ surface inventory (the spine)

> **Public surface:** `include/fix/c_api.h` only. No transitive C++ headers leak.
>
> - Opaque handles: `fixpp_engine_t`, `fixpp_session_t`, `fixpp_msg_t`, `fixpp_dict_t`, `fixpp_store_t`.
> - Error type: `fixpp_error_t` — bounded enum with reserved range `[const §X.4]` `[SYN §3.5 Q19]`.
> - Decimal at the C boundary: PoD `(int64 mantissa, int8 exponent)` `[const §X.3]`.
> - Versioning macros: `FIXPP_C_ABI_VERSION_MAJOR/MINOR/PATCH` with the SemVer rules from `[const §X.1]`.
> - Reentrancy: each symbol is documented with one of `thread-safe` / `single-thread` / `requires-session-lock` `[const §X.5]`.
> - The `extern "C"` declarations are split by domain: `c_api/engine.h`, `c_api/session.h`, `c_api/message.h`, `c_api/dict.h`, `c_api/store.h`, `c_api/log.h`, `c_api/otel.h`, all included from `fix/c_api.h`.
>
> **Design doc:** **2i** owns the message representation and the error-enum range.
>
> **Catalogue rows:** CA-001 to CA-010.

Source: `library/.specify/architecture.md:318–331`.

### §3.2 From `[arch §5.3]` — error model translation boundary

> **C ABI translates** `fixpp::core::error` → `fixpp_error_t` at the boundary. Out-of-range values from older consumers are tolerated; out-of-range values *to* a consumer are mapped to `FIXPP_ERR_UNKNOWN` `[const §X.4]`.

Source: `library/.specify/architecture.md:389`. This is the **forward-compat tolerance contract**; §4.4 implements it on both directions.

### §3.3 From `[arch §5.5]` — lifetime model

> **Flyweights** are the rule for `wire::View`, typed messages, and offset-table accessors. They never own buffers `[SYN §3.1 Q2]`.
>
> **Owned types** (`Session`, `MessageStore`, `Engine`) follow standard value semantics; copy is deleted, move is enabled where natural.

Source: `library/.specify/architecture.md:399–401`. The C ABI surface inherits both halves: `fixpp_msg_t` exposes a flyweight-class accessor surface (returned `const char*` aliases the underlying wire buffer), and `fixpp_engine_t` / `fixpp_session_t` / `fixpp_store_t` are owned-handle types with explicit destroy.

### §3.4 From `[arch §5.6]` — frozen-at-open rule

> **`SessionConfig` is value-typed and frozen at session open.** No mid-session reconfiguration of: dictionary, security profile, message store, executor, lock policy, dialect overlay.

Source: `library/.specify/architecture.md:407`. The C ABI's `fixpp_session_open(...)` takes config-by-value at session open; mid-session reconfiguration is rejected. The C-ABI `fixpp_session_t` opaque handle becomes valid after a successful `fixpp_session_open` and stays valid until `fixpp_session_close`.

### §3.5 From `[const §X]` — full ABI policy

§X.1 — **The C ABI in `include/fix/c_api.h` is a versioned contract.** Every change to it is reviewed against the contract; Codex Gate A is mandatory.
§X.2 — **No C++ symbol leakage** through the C ABI. CI verifies via `nm` (Linux) and `dumpbin` (Windows): the public C ABI surface contains only `extern "C"` symbols.
§X.3 — **Decimal at the C ABI boundary:** PoD `(int64 mantissa, int8 exponent)`. C++ users get full template flexibility via `decimal_traits<T>` (per SYNTHESIS §3.1 Q5); the C ABI picks one shape and freezes it.
§X.4 — **Error reporting at the C ABI:** `fixpp_error_t` is a bounded enum with reserved range and explicit forwards-compatibility rules (per SYNTHESIS §3.5 Q19). Out-of-range values are mapped to a documented "unknown error" code on read; unknown values from old consumers are tolerated by the engine.
§X.5 — **Reentrancy contract** is documented per C ABI symbol (thread-safe / single-thread / requires-session-lock). No undocumented reentrancy.
§X.6 — **ABI-affecting features trigger all four mandatory controls (Appendix A):** `/clarify`, `/analyze`, Codex Gate A, user `/plan` sign-off.

Source: `library/.specify/constitution.md:131–138`.

### §3.6 From `[SYN §3.5 #17]` — message representation decided

> Per user: opaque `fixpp_msg_t` with `fixpp_msg_get_string/int/double` accessors per CA-008/009. The iovec-style raw view is *not* the C-ABI surface — it would force C consumers to re-implement parsing.

Source: `research/SYNTHESIS.md:370–371`. 2i implements this verbatim in §4.6 / §4.7 / §4.8.

### §3.7 From `[SYN §3.5 #19]` — error code stability

> Per user: `fixpp_error_t` is a fixpp-specific enum with assigned numeric ranges, not POSIX `errno`. Stability is part of the ABI contract; once a value is published, it never changes meaning.

Source: `research/SYNTHESIS.md:378–379`. 2i implements this in §4.3 (numeric ranges per domain prefix) + §4.4 (`fixpp_strerror` does not allocate; the table is append-only across versions).

### §3.8 From `[2a §5.1]` — `fixpp_decimal_t` PoD shape

```c
typedef struct fixpp_decimal {
    int64_t mantissa;
    int8_t  exponent;
    int8_t  _reserved[7];
} fixpp_decimal_t;
```

Source: `library/.specify/2a-decimal.md:233–240`. The shape is **frozen** for `FIXPP_C_ABI_VERSION_MAJOR == 1` per `[2a §5.1]`. 2i exposes `fixpp_msg_get_decimal` and `fixpp_msg_set_decimal` as the message-layer entry points that consume / produce this PoD; 2a owns the type, 2a's `fixpp_decimal_parse` / `fixpp_decimal_format` / `fixpp_decimal_compare` / `fixpp_decimal_equal` / `fixpp_decimal_init` boundary functions per `[2a §5.2]` are re-published verbatim in `c_api/decimal.h` (2a-owned header; 2i references but does not redefine).

### §3.9 From `[2b §6.4]` — flyweight lifetime contract

> Capturing a typed-message flyweight past `fromApp` return is undefined in release; debug builds trap via `[2b §6.4]`'s generation-counter mechanism (the typed accessor calls flow through `wire::MessageView::get<Tag>()` which calls `View::check_alive()`).

Source: `library/.specify/2c-codegen.md:1672–1673` (which itself cites `[2b §6.4]`). The C ABI inherits this contract for `fixpp_msg_get_string`'s returned `const char*`: the pointer aliases the underlying wire buffer; its lifetime is bounded by the `fixpp_msg_t` lifetime; calling any **mutating** accessor (the `fixpp_msg_set_*` family) on the same `fixpp_msg_t` invalidates all prior `fixpp_msg_get_string` returns. Documented explicitly in §4.6 contract block.

### §3.10 From `[2c §5]` — dictionary C-ABI commitments

2c's §5 commitments 1–6 (cited verbatim in `library/.specify/2c-codegen.md:1554–1572`) constrain how `fixpp_msg_t` carries a runtime resolved-message-version tag, how `fixpp_dict_t` lifetime is managed, how `fixpp_msg_reify` works, and how `application_version` maps to a C-ABI constant set. 2i honours those commitments by:

- **Commitment 1:** `fixpp_msg_t` accessors take a `fixpp_msg_t` opaque handle that internally carries the resolved per-message version (kind / session / application bytes per 2c). 2i exposes `fixpp_msg_version(fixpp_msg_t) → fixpp_resolved_msg_version_t` per `c_api/message.h`.
- **Commitment 2:** `fixpp_dict_t` is an opaque handle constructed via `fixpp_dict_load_from_xml(path, error_out) → fixpp_dict_t*`; release through `fixpp_dict_destroy(dict)`. The exact lifetime / refcounting rules are 2c's call (delegated to `c_api/dict.h`).
- **Commitment 3:** 2i picks the **tag-keyed** flavour for the v1.0 ABI surface (`fixpp_msg_get_int(msg, tag, ...)`, not a per-field-name `fixpp_NewOrderSingle_get_cl_ord_id`). Rationale in §4.6.
- **Commitment 5:** `FIXPP_APPL_VER_*` constants live in `c_api/dict.h` (2c-owned); 2i re-exposes them via the umbrella header.

### §3.11 From `[2d §6.7]` — threading errors + C-ABI coalescing

The threading layer's 9 variants per `[2d §6.7]` lines 1169–1181 (`executor_already_stopped`, `executor_not_serialised`, `clock_sleeps_cancelled`, `strand_dispatch_failed_oom`, `session_already_open`, `session_already_closed`, `invalid_session_config`, `clock_not_set`, `dispatch_aborted`) coalesce per `[2d §6.7]` into `FIXPP_ERR_THREAD_CONFIG` / `FIXPP_ERR_THREAD_SESSION_LIFECYCLE` / `FIXPP_ERR_THREAD_RUNTIME`, with cancellation (`clock_sleeps_cancelled`, `dispatch_aborted`) joining `FIXPP_ERR_CANCELLED`.

2i ratifies the coalescing assignment in §4.3.

### §3.12 From `[2d §4.7]` — cancellation propagation API

Per `[2d §4.7]` per-mode effect table: every async session/transport/store/sync op completes with one of `expected_t::unexpected{*_cancelled}` / `*_aborted` on `cancellation_type::total`. The C ABI translates **every** cancellation outcome — `dispatch_aborted`, `clock_sleeps_cancelled`, `store_cancelled`, `sync_lock_aborted`, `tls_load_cancelled`, `transport_*_cancelled`, `accept_cancelled` — uniformly to `FIXPP_ERR_CANCELLED` per `[const §XI.2]`. Justification: at the C ABI boundary the consumer cannot act differently on `dispatch_aborted` vs `transport_read_cancelled`; the FSM-level distinction is a C++-side concern. See §4.9 for the cancellation translation rule and §10 for the open-question discussion of whether to expose source-distinguishing variants in v1.x.

### §3.13 From `[2e §6.7]` — store errors

10 variants per `[2e §6.7]` lines 1110–1121, coalesced into `FIXPP_ERR_STORE_RUNTIME` / `FIXPP_ERR_STORE_CONSISTENCY` / `FIXPP_ERR_STORE_CONFIG` / `FIXPP_ERR_STORE_VISITOR`, with cancellation to `FIXPP_ERR_CANCELLED`. 2i ratifies.

### §3.14 From `[2f §6.5]` — sync_mutex errors + cancellation precedent

Per `[2f §6.5]` lines 1497–1500, the four variants are `sync_lock_aborted` (cancellation, joins `FIXPP_ERR_CANCELLED`), `sync_lock_alloc_failed`, `sync_lock_outside_session`, `sync_lock_drained` (the latter three joining `FIXPP_ERR_SYNC_RUNTIME`). The `sync_lock_aborted → FIXPP_ERR_CANCELLED` mapping per `[2d §6.7]` / `[2f §6.5]` is **the direct precedent** for 2i's broader cancellation translation rule (§4.9): when ASIO's `expected_t::unexpected` machinery surfaces a cancellation, the C ABI translates uniformly.

### §3.15 From `[2g §6.6]` — TLS errors

15 variants per `[2g §6.6]`. Coalesced into `FIXPP_ERR_TLS_CONFIG` / `FIXPP_ERR_TLS_HANDSHAKE` / `FIXPP_ERR_TLS_PINSET` / `FIXPP_ERR_TLS_RUNTIME`, cancellation to `FIXPP_ERR_CANCELLED`. 2i ratifies. Per `[2g §7.6]`: the TLS surface is C++-only in v1.0 — no `fixpp_cert_source_t` / `fixpp_pinset_t` ABI accessors. The C ABI consumer triggers cert/pinset reload via 2j's control plane (a `ReloadCertSource` RPC), not a C-ABI call.

### §3.16 From `[2h §6.6]` — transport errors

22 variants per `[2h §6.6]` lines 1171–1196. Coalesced into `FIXPP_ERR_TRANSPORT_LIFECYCLE` / `FIXPP_ERR_TRANSPORT_IO` / `FIXPP_ERR_TRANSPORT_HANDSHAKE` (joining `FIXPP_ERR_TLS_HANDSHAKE` at the C-ABI level — see §4.3 row 8) / `FIXPP_ERR_TRANSPORT_CONFIG`, cancellation to `FIXPP_ERR_CANCELLED`. 2i ratifies the coalescing assignment.

### §3.17 From `[2h §7.8]` — consumer drop-in for handle shape

Quoted verbatim from `library/.specify/2h-transport.md:1304–1306` (byte-faithful — H3 marker preserved; no blockquote conversion; no added bolding):

```
### §7.8 C ABI (2i) — handle shape

Per §5: 2h defines the C++ shapes; 2i defines the C symbols. The opaque-handle shapes (`fixpp_transport_t`, `fixpp_tls_transport_t`, `fixpp_listener_t`, `fixpp_transport_factory_t`), the PoD types (`fixpp_endpoint_t`, `fixpp_reconnect_policy_t`, `fixpp_connect_info_t`), and the `FIXPP_ERR_TRANSPORT_*` coalescing groups (§6.6) are 2h's hand-off to 2i.
```

2i's §7.8 declares which of those shapes ship in the v1.0 ABI surface (none of the transport-level handles do — see §1.2 non-goal #7) and which are deferred / delegated to 2j control plane.

---

## §4 Public C ABI

This is the heart of the doc. Each sub-section publishes the `extern "C"` declarations the consumer compiler will see, with the lifetime / error / reentrancy contract.

### §4.1 Header layout

The umbrella header `<fix/c_api.h>` includes all per-domain split headers:

```c
/* include/fix/c_api.h */
#ifndef FIXPP_C_API_H
#define FIXPP_C_API_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* The error code enum + fixpp_strerror() — 2i-owned */
#include <fix/c_api/error.h>

/* The version macros + fixpp_version() runtime accessor — 2i-owned */
#include <fix/c_api/version.h>

/* Decimal PoD type + boundary functions — 2a-owned */
#include <fix/c_api/decimal.h>

/* Message accessor / setter / group surface — 2i-owned */
#include <fix/c_api/message.h>

/* Dictionary handle + load functions — 2c-owned (consumes 2i's plumbing rules) */
#include <fix/c_api/dict.h>

/* MessageStore handle — 2e-owned */
#include <fix/c_api/store.h>

/* Engine handle + open/close — 2j-owned (shape per 2i; semantics per 2j) */
#include <fix/c_api/engine.h>

/* Session handle + open/send/close — 2j-owned (shape per 2i; semantics per 2j + Phase-4) */
#include <fix/c_api/session.h>

/* Logger sink + level config — 2k-owned */
#include <fix/c_api/log.h>

/* OpenTelemetry exporter wiring — 2k-owned */
#include <fix/c_api/otel.h>

#endif /* FIXPP_C_API_H */
```

**Visibility / export macros.** `FIXPP_API_EXPORT` is defined per platform in `<fix/c_api/export.h>` (also 2i-owned, included from each split header):

```c
/* include/fix/c_api/export.h */
#ifndef FIXPP_C_API_EXPORT_H
#define FIXPP_C_API_EXPORT_H

#if defined(_WIN32)
  #if defined(FIXPP_BUILDING_DLL)
    #define FIXPP_API_EXPORT __declspec(dllexport)
  #elif defined(FIXPP_DLL)
    #define FIXPP_API_EXPORT __declspec(dllimport)
  #else
    #define FIXPP_API_EXPORT  /* static link */
  #endif
#else
  #if defined(FIXPP_BUILDING_DLL)
    #define FIXPP_API_EXPORT __attribute__((visibility("default")))
  #else
    #define FIXPP_API_EXPORT
  #endif
#endif

#endif /* FIXPP_C_API_EXPORT_H */
```

**`extern "C"` boundary.** Every split header wraps its declarations in:

```c
#ifdef __cplusplus
extern "C" {
#endif
/* ... declarations ... */
#ifdef __cplusplus
}
#endif
```

The headers are simultaneously valid C99 (and later) and C++17 (and later). Per `[const §X.2]` / `[arch §9.1]`: only `<stddef.h>`, `<stdint.h>`, `<stdbool.h>` may appear at the top of any `c_api/*.h`. CI verifies via grep:

```
tools/check_capi_headers.sh — fails if any include under c_api/ uses <atomic>, <type_traits>, <asio>, <memory>, <string>, etc.
```

**No C++ types in any `c_api/*.h`.** This is the structural C-ABI rule per `[const §X.2]`. PoD structs only; opaque-handle forward declarations only; no template syntax; no C++-only literals (`nullptr` is forbidden — use `NULL`).

### §4.2 Opaque handle types

Per `[arch §4.10]` the v1.0 catalogue is `fixpp_engine_t`, `fixpp_session_t`, `fixpp_msg_t`, `fixpp_dict_t`, `fixpp_store_t`. Each is declared in its owning split header as an incomplete forward struct:

```c
/* in c_api/engine.h: */
typedef struct fixpp_engine fixpp_engine_t;

/* in c_api/session.h: */
typedef struct fixpp_session fixpp_session_t;

/* in c_api/message.h:    /* 2i-owned */
typedef struct fixpp_msg fixpp_msg_t;

/* in c_api/dict.h: */
typedef struct fixpp_dict fixpp_dict_t;

/* in c_api/store.h: */
typedef struct fixpp_store fixpp_store_t;
```

The struct definitions are **never** in a public header. They live in `src/capi/handle_definitions.cpp` as private C++ types that wrap the engine-internal C++ objects:

```cpp
// src/capi/handle_definitions.cpp (engine-internal; NOT shipped to consumers)
struct fixpp_engine {
    std::unique_ptr<fixpp::core::Engine>  engine_;
    uint32_t                              tag_;        /* type-tag: §4.2.2 */
    /* ... */
};
struct fixpp_session {
    fixpp::session::Session*              session_;    /* non-owning; engine owns */
    fixpp_engine_t*                       engine_;     /* parent */
    uint32_t                              tag_;
    /* ... */
};
struct fixpp_msg {
    fixpp::wire::MessageView<fixpp::wire::IndexMode>  view_;   /* the flyweight */
    fixpp::core::resolved_message_version             version_;
    uint64_t                              generation_;  /* §4.2.4 */
    uint32_t                              tag_;
    /* ... */
};
struct fixpp_dict {
    std::shared_ptr<const fixpp::dict::Dictionary>  dict_;
    uint32_t                              tag_;
};
struct fixpp_store {
    fixpp::session::MessageStore*         store_;      /* non-owning; session owns per [2e §3.1] N1 */
    fixpp_session_t*                      session_;    /* parent */
    uint32_t                              tag_;
};
```

**Opacity rule.** The consumer may **only** dereference the handle via the published `extern "C"` symbols. Casting a `fixpp_msg_t*` to a different type, doing pointer arithmetic, or `memcpy`-ing the struct contents is **undefined behaviour** at the ABI level and may produce an `FIXPP_ERR_INVALID_HANDLE` from any subsequent call (debug build) or silent corruption (release build).

#### §4.2.1 Ownership / destroy discipline

The opaque-handle catalogue splits into **owning** and **non-owning** handles by lifetime:

| Handle | Owning? | Lifetime | Created by | Destroyed by |
|---|---|---|---|---|
| `fixpp_engine_t` | Owning | Engine lifetime | `fixpp_engine_create(...)` (CA-005, owned by 2j) | `fixpp_engine_destroy(engine)` |
| `fixpp_session_t` | Non-owning observer (engine owns the underlying `Session*`) | Bounded by engine + session-open / -close cycle | `fixpp_session_open(engine, config, error_out)` (CA-005) | `fixpp_session_close(session)` then handle becomes invalid |
| `fixpp_msg_t` | Non-owning observer of a wire flyweight | Bounded by the **`fromApp` callback dispatch window** for inbound messages, or by the **commit cycle** for outbound messages — see §4.6 lifetime block | Engine constructs at parse time (inbound); `fixpp_msg_create_outbound(session, msg_type, error_out)` for outbound | Engine destroys at parse-window close (inbound); `fixpp_msg_destroy(msg)` for outbound (releases the per-message arena slot) |
| `fixpp_dict_t` | Owning (refcounted via shared_ptr) | Engine lifetime (registered into engine config) | `fixpp_dict_load_from_xml(path, error_out)` (2c-owned) | `fixpp_dict_destroy(dict)` |
| `fixpp_store_t` | Non-owning observer of a session-owned store per `[2e §6.7]` N1 | Bounded by session lifetime | Engine constructs at session-open via `SessionConfig::store_factory`; the consumer reaches it via `fixpp_session_get_store(session)` | Becomes invalid when `fixpp_session_close(session)` returns; no separate destroy call |

**Destroy is idempotent and never throws.** Each `*_destroy(handle)` is `thread-safe`, accepts `NULL` (returns silently), accepts an already-destroyed handle (returns silently — the destroyed handle's `tag_` is rewritten to `FIXPP_HANDLE_TAG_DEAD`; the function detects and no-ops). Calling any **non-destroy** function on a destroyed handle returns `FIXPP_ERR_INVALID_HANDLE`. This matches `[2e §6.7]` N1's documented `fixpp_store_t` invalidation pattern.

**`NULL` handle handling.** Every public function with a handle parameter checks for `NULL` first. A `NULL` handle returns `FIXPP_ERR_NULL_HANDLE` (numerically distinct from `FIXPP_ERR_INVALID_HANDLE` — the former is "you passed me NULL", the latter is "you passed a previously-valid handle that has been destroyed or corrupted"). Documented per-function in §4.6 / §4.7 / §4.8. Verified by §9 seam #9 (null-handle round-trip).

#### §4.2.2 Type tagging

Each opaque struct carries a `uint32_t tag_` field at a fixed offset (see the `handle_definitions.cpp` block above). The tag is set at construction to a per-type magic constant:

```cpp
constexpr uint32_t FIXPP_HANDLE_TAG_ENGINE  = 0xF1ECE001;
constexpr uint32_t FIXPP_HANDLE_TAG_SESSION = 0xF1ECE002;
constexpr uint32_t FIXPP_HANDLE_TAG_MSG     = 0xF1ECE003;
constexpr uint32_t FIXPP_HANDLE_TAG_DICT    = 0xF1ECE004;
constexpr uint32_t FIXPP_HANDLE_TAG_STORE   = 0xF1ECE005;
constexpr uint32_t FIXPP_HANDLE_TAG_DEAD    = 0xDEADD1ED;  /* destroyed */
```

Every entry-point thunk reads the tag through the C-API translation layer and rejects tag mismatches with `FIXPP_ERR_INVALID_HANDLE`. This catches the consumer accidentally passing a `fixpp_session_t*` to `fixpp_msg_get_string` (the tag mismatch fires before any read of the struct contents). Verified by §9 seam #11 (handle-type-mismatch test).

The tag value itself is **not** exposed to the consumer; it is engine-internal. The consumer treats the handle as a black-box pointer.

#### §4.2.3 The C ↔ C++ thunk shape

Every `extern "C"` symbol is one thunk over `fixpp::capi::detail::*` — a private C++ namespace per `[arch §3]`:

```cpp
// src/capi/message_get_string.cpp (engine-internal)
extern "C" FIXPP_API_EXPORT
fixpp_error_t fixpp_msg_get_string(fixpp_msg_t* msg,
                                   uint16_t      tag,
                                   const char**  value_out,
                                   size_t*       len_out)
{
    return fixpp::capi::detail::guarded_call([&]() noexcept -> fixpp_error_t {
        if (!msg)            return FIXPP_ERR_NULL_HANDLE;
        if (!value_out)      return FIXPP_ERR_NULL_HANDLE;
        if (!len_out)        return FIXPP_ERR_NULL_HANDLE;
        if (msg->tag_ != FIXPP_HANDLE_TAG_MSG) return FIXPP_ERR_INVALID_HANDLE;

        auto result = msg->view_.get_string(tag);
        if (!result.has_value()) {
            return fixpp::capi::detail::translate(result.error());
        }
        auto sv = *result;
        *value_out = sv.data();
        *len_out   = sv.size();
        return FIXPP_ERR_OK;
    });
}
```

The `guarded_call` template wraps the lambda in a top-level `try { ... } catch(...) { /* trap */ }` per §5. The `translate(error)` function maps `fixpp::core::error` → `fixpp_error_t` per the §4.3 table. No exception ever crosses `extern "C"`.

#### §4.2.4 Generation counter — debug-build flyweight invariant trap

For `fixpp_msg_t` only, the engine-internal struct carries `uint64_t generation_` matching the underlying wire-buffer pool generation per `[2b §6.4]`. The thunk reads the live pool generation on every accessor call and traps in debug builds (`std::abort` on mismatch). Release builds skip the check; release-mode-stale-handle behaviour is undefined per `[2b §6.4]`.

The check is debug-only because hot-path accessor latency cannot afford a generation check on every call (target ≤ 50 ns per `fixpp_msg_get_string` warm-cache; the check would add ~5 ns of cache-line touch).

### §4.3 `fixpp_error_t` enum — the master enum

Declared in `c_api/error.h`:

```c
/* include/fix/c_api/error.h */
#ifndef FIXPP_C_API_ERROR_H
#define FIXPP_C_API_ERROR_H

#include <stdint.h>
#include <fix/c_api/export.h>

#ifdef __cplusplus
extern "C" {
#endif

/* fixpp_error_t — bounded enum with reserved per-domain numeric blocks per
 * [const §X.4] / [SYN §3.5 #19]. Stability: once a numeric value is
 * published, it never changes meaning. New variants append within a domain
 * block.
 *
 * The header carries the values as int32_t-sized constants (NOT as a C
 * `enum` declaration), so that future minor-version growth (adding new
 * variants) does not change the enum's storage size and ABI signature.
 * The underlying type at the C ABI is `int32_t`. */
typedef int32_t fixpp_error_t;

/* ── Cross-cutting block [0, 99] — 2i-owned ─────────────────────────── */
#define FIXPP_ERR_OK                       ((fixpp_error_t)   0)
#define FIXPP_ERR_CANCELLED                ((fixpp_error_t)   1)
#define FIXPP_ERR_UNKNOWN                  ((fixpp_error_t)   2)
#define FIXPP_ERR_NULL_HANDLE              ((fixpp_error_t)   3)
#define FIXPP_ERR_INVALID_HANDLE           ((fixpp_error_t)   4)
#define FIXPP_ERR_VERSION_MISMATCH         ((fixpp_error_t)   5)
#define FIXPP_ERR_BUFFER_TOO_SMALL         ((fixpp_error_t)   6)
#define FIXPP_ERR_TYPE_MISMATCH            ((fixpp_error_t)   7)
#define FIXPP_ERR_TAG_NOT_FOUND            ((fixpp_error_t)   8)
#define FIXPP_ERR_INDEX_OUT_OF_RANGE       ((fixpp_error_t)   9)
#define FIXPP_ERR_CAPI_CONFIG_INVALID      ((fixpp_error_t)  10)
/* [11, 99] reserved for cross-cutting growth. */

/* ── Wire block [100, 199] — 2b-owned per [2b §6.7] ──────────────────── */
#define FIXPP_ERR_WIRE_INVALID_FRAME       ((fixpp_error_t) 100)
#define FIXPP_ERR_WIRE_LIMIT_EXCEEDED      ((fixpp_error_t) 101)
#define FIXPP_ERR_WIRE_CONFORMANCE         ((fixpp_error_t) 102)
/* [103, 199] reserved for 2b growth. */

/* ── Dict block [200, 299] — 2c-owned per [2c §6.7] ─────────────────── */
#define FIXPP_ERR_DICT_CONFIG              ((fixpp_error_t) 200)
#define FIXPP_ERR_DICT_LIMIT_EXCEEDED      ((fixpp_error_t) 201)
#define FIXPP_ERR_DICT_OOM                 ((fixpp_error_t) 202)  /* mapped from dict_*_oom variants */
/* [203, 299] reserved for 2c growth. */

/* ── Threading block [300, 399] — 2d-owned per [2d §6.7] (9 variants;
 *    cancellation pair `clock_sleeps_cancelled` / `dispatch_aborted`
 *    coalesces to FIXPP_ERR_CANCELLED per §4.9) ─────────────────────── */
#define FIXPP_ERR_THREAD_CONFIG            ((fixpp_error_t) 300)
#define FIXPP_ERR_THREAD_SESSION_LIFECYCLE ((fixpp_error_t) 301)
#define FIXPP_ERR_THREAD_RUNTIME           ((fixpp_error_t) 302)
/* [303, 399] reserved for 2d growth. */

/* ── Store block [400, 499] — 2e-owned per [2e §6.7] ────────────────── */
#define FIXPP_ERR_STORE_RUNTIME            ((fixpp_error_t) 400)
#define FIXPP_ERR_STORE_CONSISTENCY        ((fixpp_error_t) 401)
#define FIXPP_ERR_STORE_CONFIG             ((fixpp_error_t) 402)
#define FIXPP_ERR_STORE_VISITOR            ((fixpp_error_t) 403)
/* [404, 499] reserved for 2e growth. */

/* ── Sync block [500, 599] — 2f-owned per [2f §6.5] ─────────────────── */
#define FIXPP_ERR_SYNC_RUNTIME             ((fixpp_error_t) 500)
/* [501, 599] reserved for 2f growth (sync_lock_aborted maps to FIXPP_ERR_CANCELLED). */

/* ── TLS block [600, 699] — 2g-owned per [2g §6.6] ──────────────────── */
#define FIXPP_ERR_TLS_CONFIG               ((fixpp_error_t) 600)
#define FIXPP_ERR_TLS_HANDSHAKE            ((fixpp_error_t) 601)
#define FIXPP_ERR_TLS_PINSET               ((fixpp_error_t) 602)
#define FIXPP_ERR_TLS_RUNTIME              ((fixpp_error_t) 603)
/* [604, 699] reserved for 2g growth. */

/* ── Transport block [700, 799] — 2h-owned per [2h §6.6] ────────────── */
#define FIXPP_ERR_TRANSPORT_LIFECYCLE      ((fixpp_error_t) 700)
#define FIXPP_ERR_TRANSPORT_IO             ((fixpp_error_t) 701)
#define FIXPP_ERR_TRANSPORT_HANDSHAKE      ((fixpp_error_t) 702)
#define FIXPP_ERR_TRANSPORT_CONFIG         ((fixpp_error_t) 703)
/* [704, 799] reserved for 2h growth. */

/* ── Decimal block [800, 899] — 2a-owned per [2a §7.4] ──────────────── */
#define FIXPP_ERR_DECIMAL_INVALID          ((fixpp_error_t) 800)
#define FIXPP_ERR_DECIMAL_PRECISION_LOSS   ((fixpp_error_t) 801)
/* (FIXPP_ERR_BUFFER_TOO_SMALL is in the cross-cutting block; 2a's
 *  decimal_buffer_too_small reuses code 6.)
 * [802, 899] reserved for 2a growth. */

/* ── Control plane block [900, 999] — 2j-owned per [2j §6.6] (8 variants;
 *    cancellation triple `control_plane_start_cancelled` /
 *    `control_plane_stop_cancelled` / `control_plane_stream_cancelled`
 *    coalesces to FIXPP_ERR_CANCELLED per §4.9; numeric assignments added
 *    at 2j sign-off (2026-05-09) per [2j App D §D.3]) ───────────────── */
#define FIXPP_ERR_CTRL_CONFIG              ((fixpp_error_t) 900)
#define FIXPP_ERR_CTRL_RUNTIME             ((fixpp_error_t) 901)
/* [902, 999] reserved for 2j growth. */

/* ── Reserved blocks ─────────────────────────────────────────────────── */
/* [1000, 1099] reserved for 2k log + otel (FIXPP_ERR_LOG_*, FIXPP_ERR_OTEL_*) */
/* [1100, 1199] reserved for 2l tap (FIXPP_ERR_TAP_*) */

/* ── Bindings block [1200, 1299] — 2m-owned per [2m §6.7] (5 variants) ── */
#define FIXPP_ERR_BINDING_PYTHON_CALLBACK_RAISED   ((fixpp_error_t) 1200)
#define FIXPP_ERR_BINDING_SUBINTERPRETER           ((fixpp_error_t) 1201)
#define FIXPP_ERR_BINDING_OBJECT_LIFETIME          ((fixpp_error_t) 1202)
#define FIXPP_ERR_BINDING_WHEEL_ABI_MISMATCH       ((fixpp_error_t) 1203)
#define FIXPP_ERR_BINDING_CALLBACK_REENTRANT_CLOSE ((fixpp_error_t) 1204)
/* [1205, 1299] reserved for 2m growth. */

/* [1300, 1399] reserved for post-v1.x growth */
/* [1400+]      reserved for future expansion */

/* ── fixpp_strerror — convert a fixpp_error_t to a static const char* ── */
FIXPP_API_EXPORT
const char* fixpp_strerror(fixpp_error_t code);

#ifdef __cplusplus
}
#endif
#endif /* FIXPP_C_API_ERROR_H */
```

**Why per-block coalescing groups, not per-variant codes?** The 97 prior-doc variants (4 + 13 + 20 + 9 + 10 + 4 + 15 + 22 — re-derived live from sibling `[2X §6.X]` tables, see §1.1 magnitude-domain table for per-doc citations) cannot all be exposed as distinct C-ABI codes without flooding the consumer with implementation-detail noise. The per-doc `[2X §6.X]` sections each declare 3–4 coalescing groups that capture the **remediation class** the consumer needs (configuration error / runtime error / capacity error / cancellation). 2i ratifies the per-doc groupings as the published C-ABI surface. The full per-variant `fixpp::core::error` enum stays C++-internal; the C consumer sees only the coalesced groups + the cross-cutting sentinels + cancellation.

**Why `int32_t` and `#define`, not `enum`?** A C `enum` whose underlying type is unspecified can change size when new variants are added (some compilers pick the smallest type that fits all values). For a C-ABI-stable enum, we pick `int32_t` explicitly and use `#define` constants. New variants in v1.x append numeric values without changing the underlying type. The `fixpp_error_t` typedef is `int32_t`, period.

**Stability rule.** Once a numeric value is published in a tagged C ABI release (`FIXPP_C_ABI_VERSION_MAJOR == 1`), it never changes meaning. A future v1.x adds new variants at unused numeric slots within a block; it never re-defines an existing slot. A breaking change (re-defining a slot) requires `FIXPP_C_ABI_VERSION_MAJOR == 2`. This is enforced by:

- **Audit trail.** `tools/abi_history/error_codes_v1.txt` is a checked-in append-only file mapping every published numeric value to its symbolic name + the doc revision that introduced it. CI verifies that every code in the current header appears in the audit file with its ORIGINAL meaning preserved (no re-definitions).
- **Tier 2 abidiff.** Per `[const §IX.5]` the abidiff check on the C ABI surface fires on any breaking change. A re-defined `fixpp_error_t` value is a breaking change.
- **Occupancy drift gate.** `tools/check_capi_occupancy.sh` mechanically counts `| \`*_*\` |` rows in each sibling `[2X §6.X]` errors table (`2a §7.4`, `2b §6.7`, `2c §6.7`, `2d §6.7`, `2e §6.7`, `2f §6.5`, `2g §6.6`, `2h §6.6`) and asserts the counts published in this doc's §1.1 magnitude-domain table + the §1.1 final layout block + §3.11 prose + §4.3 inline comments + §6.5 prior-doc total + Appendix D.2 supplemental match. Drift fails CI. **Single source of truth** for per-block occupancy is the §1.1 magnitude-domain table; every other site in 2i derives from it, and the gate verifies the derivation. Added in v0.2 / RC#2 close (Codex P1-1 counter-proposal generalised). Runs in Tier 1.

### §4.4 `fixpp_strerror()` and forward-compat

```c
/* in c_api/error.h: */
FIXPP_API_EXPORT
const char* fixpp_strerror(fixpp_error_t code);
```

**Reentrancy:** `FIXPP_THREAD_SAFE`. May be called from any thread.
**Allocation:** ZERO. The function reads from a static `const char*` lookup table (one-pointer-per-code, indexed). No `malloc`, no `strdup`, no per-call buffer.
**Lifetime of returned pointer:** The returned `const char*` is a static string-literal pointer with `static` storage duration; valid for the lifetime of the engine binary. The caller MUST NOT `free()` it.

**Forward-compat behaviour (both directions, per `[arch §5.3]` last bullet):**

1. **Out-of-range value FROM consumer (consumer compiled against newer header, engine is older):** the engine sees a numeric value it doesn't recognize. Per `[const §X.4]` "unknown values from old consumers are tolerated by the engine" / `[arch §5.3]` line 389: the engine treats the value as **opaque pass-through**. The engine does **not** actively reject unknown FROM-consumer values; `FIXPP_ERR_VERSION_MISMATCH` (numeric 5) is reserved for the explicit major-version-mismatch case at engine construction (per §4.5), not for unknown-value-tolerance.
2. **Out-of-range value TO consumer (engine compiled against newer error set, consumer is older):** when the engine would surface a code the consumer's header doesn't define, the C-ABI translation layer **maps it to `FIXPP_ERR_UNKNOWN`** before returning. This is the §4.4 contract. The `fixpp_capi::detail::translate(error)` function checks the consumer's `FIXPP_C_ABI_VERSION_MAJOR/MINOR` (passed at engine construction time via `fixpp_engine_create(consumer_version)`) and downgrades any code introduced after that version to `FIXPP_ERR_UNKNOWN`.

```c
/* fixpp_strerror lookup table excerpt — engine-internal */
static const char* const k_strerror_table[] = {
    "OK",                                 /* FIXPP_ERR_OK = 0 */
    "operation cancelled",                /* FIXPP_ERR_CANCELLED = 1 */
    "unknown error code (from a newer engine)",  /* FIXPP_ERR_UNKNOWN = 2 */
    "null handle",                        /* FIXPP_ERR_NULL_HANDLE = 3 */
    "invalid or destroyed handle",        /* FIXPP_ERR_INVALID_HANDLE = 4 */
    "C ABI version mismatch",             /* FIXPP_ERR_VERSION_MISMATCH = 5 */
    "buffer too small",                   /* FIXPP_ERR_BUFFER_TOO_SMALL = 6 */
    "type mismatch (e.g., get_int on a STRING field)",  /* FIXPP_ERR_TYPE_MISMATCH = 7 */
    "tag not found in the message",       /* FIXPP_ERR_TAG_NOT_FOUND = 8 */
    "index out of range",                 /* FIXPP_ERR_INDEX_OUT_OF_RANGE = 9 */
    "C ABI config invalid (engine_create / dict_load / msg_create_outbound)",  /* FIXPP_ERR_CAPI_CONFIG_INVALID = 10 */
    /* [11, 99] reserved — return "reserved code" */
    /* [100, 199] WIRE — 3 entries used:
        100: "wire frame invalid",
        101: "wire size limit exceeded",
        102: "wire conformance check failed" */
    /* ... and so on ... */
};

const char* fixpp_strerror(fixpp_error_t code) {
    if (code < 0)                              return "invalid (negative) error code";
    if (code >= (fixpp_error_t)k_strerror_table_size) return "reserved or unknown error code";
    const char* s = k_strerror_table[code];
    return s ? s : "reserved error code";
}
```

The lookup-table shape is **flat indexed**, NOT a hash map. Lookup is O(1) with a single load + bounds check; the table is `const`-initialized at link time (not at process startup) so there is no race on first call.

### §4.5 Versioning macros

Declared in `c_api/version.h`:

```c
/* include/fix/c_api/version.h */
#ifndef FIXPP_C_API_VERSION_H
#define FIXPP_C_API_VERSION_H

#include <stdint.h>
#include <fix/c_api/export.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The C ABI surface SemVer per [const §X.1] / [arch §9.2].
 * MAJOR bumps on breaking change; MINOR bumps on additive change;
 * PATCH bumps on bug fix that doesn't touch the ABI surface. */
#define FIXPP_C_ABI_VERSION_MAJOR  1
#define FIXPP_C_ABI_VERSION_MINOR  0
#define FIXPP_C_ABI_VERSION_PATCH  0

/* Composite version macro for compile-time checks. */
#define FIXPP_C_ABI_VERSION \
    ((FIXPP_C_ABI_VERSION_MAJOR << 16) | \
     (FIXPP_C_ABI_VERSION_MINOR << 8)  | \
     (FIXPP_C_ABI_VERSION_PATCH))

/* Runtime version accessor — CA-004.
 * Returns the version of the engine binary the consumer is linked against.
 * The consumer compares this to FIXPP_C_ABI_VERSION at startup; mismatch on
 * MAJOR is a hard incompatibility (engine returns FIXPP_ERR_VERSION_MISMATCH
 * on any subsequent call). */
typedef struct fixpp_version {
    uint16_t major;
    uint16_t minor;
    uint16_t patch;
    uint16_t _reserved;
} fixpp_version_t;

FIXPP_API_EXPORT
fixpp_version_t fixpp_version(void);

/* Optional library-level version (the C++ surface) — separate track per
 * [arch §9.2]. Same shape; queried via fixpp_library_version(). */
FIXPP_API_EXPORT
fixpp_version_t fixpp_library_version(void);

#ifdef __cplusplus
}
#endif
#endif /* FIXPP_C_API_VERSION_H */
```

**Reentrancy:** `FIXPP_THREAD_SAFE`. Returns a value-typed PoD; no shared state.
**Allocation:** ZERO.

**Engine-binding version-check protocol.** When the consumer calls `fixpp_engine_create(consumer_abi_version_major, consumer_abi_version_minor, ...)` (the signature is owned by 2j; 2i pins the version-handling rule):

- If `consumer_abi_version_major != FIXPP_C_ABI_VERSION_MAJOR (engine)`: engine refuses to construct, returns `FIXPP_ERR_VERSION_MISMATCH`. No `fixpp_engine_t` is created.
- If `consumer_abi_version_major == FIXPP_C_ABI_VERSION_MAJOR (engine)` but `consumer_abi_version_minor < FIXPP_C_ABI_VERSION_MINOR (engine)`: engine constructs but stores `consumer_minor` for use by §4.4's forward-compat downgrade logic — error codes introduced after `consumer_minor` are mapped to `FIXPP_ERR_UNKNOWN` on return.
- If `consumer_abi_version_minor > FIXPP_C_ABI_VERSION_MINOR (engine)`: engine constructs (consumer can deal with our codes; they're a strict subset of what consumer expects). The engine logs a warning at `info` level (consumer is newer than us; no surface incompatibility per SemVer).

### §4.6 Field accessors (CA-008)

Declared in `c_api/message.h`:

```c
/* include/fix/c_api/message.h */
#ifndef FIXPP_C_API_MESSAGE_H
#define FIXPP_C_API_MESSAGE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <fix/c_api/error.h>
#include <fix/c_api/export.h>
#include <fix/c_api/decimal.h>  /* for fixpp_decimal_t per [2a §5.1] */

#ifdef __cplusplus
extern "C" {
#endif

/* Forward-decls — definitions are engine-internal per §4.2. */
typedef struct fixpp_msg fixpp_msg_t;
typedef struct fixpp_group fixpp_group_t;  /* opaque cursor — see §4.8 */

/* Resolved per-message version per [2c §5] commitment 1. */
typedef struct fixpp_resolved_msg_version {
    uint8_t kind;         /* 0 = session_admin, 1 = application */
    uint8_t session;      /* drawn from session_version */
    uint8_t application;  /* drawn from application_version; FIXPP_APPL_VER_UNKNOWN when kind == session_admin */
    uint8_t _reserved;
} fixpp_resolved_msg_version_t;

/* ── §4.6 Accessors (CA-008) ───────────────────────────────────────── */

/* Get the resolved message version (per [2c §5] commitment 1).
 *
 * Reentrancy: FIXPP_THREAD_SAFE on a const fixpp_msg_t* (the underlying
 * resolution is set at parse time and never mutated).
 *
 * Returns FIXPP_ERR_OK on success.
 * Returns FIXPP_ERR_NULL_HANDLE if msg is NULL or version_out is NULL.
 * Returns FIXPP_ERR_INVALID_HANDLE if msg has been destroyed. */
FIXPP_API_EXPORT
fixpp_error_t fixpp_msg_version(const fixpp_msg_t*             msg,
                                fixpp_resolved_msg_version_t*  version_out);

/* Get a STRING field by tag.
 *
 * On success, *value_out aliases the underlying wire buffer (zero-copy view
 * per [2b §6.4] flyweight rule). The caller MUST NOT free *value_out. The
 * pointer is valid until:
 *   (a) the next call to fixpp_msg_set_*(...) on the same msg, OR
 *   (b) for inbound messages, the fromApp callback returns, OR
 *   (c) for outbound messages, fixpp_msg_destroy(msg) is called.
 * Capturing the pointer past those events is undefined behaviour
 * (debug builds trap via the [2b §6.4] generation-counter mechanism;
 * release builds are silent).
 *
 * Reentrancy: FIXPP_REQUIRES_SESSION_LOCK — must be invoked on the
 * session strand for the owning session, OR from inside the fromApp
 * dispatch (which IS on the session strand per [2d §7.6]).
 *
 * Returns FIXPP_ERR_OK on success; *value_out + *len_out written.
 * Returns FIXPP_ERR_NULL_HANDLE on NULL msg/value_out/len_out.
 * Returns FIXPP_ERR_INVALID_HANDLE on destroyed msg.
 * Returns FIXPP_ERR_TAG_NOT_FOUND if the tag is absent from the message.
 * Returns FIXPP_ERR_TYPE_MISMATCH if the tag is dictionary-known to be
 *   non-STRING (caller should use the type-correct accessor). The caller
 *   may always fall back to the byte-typed flavour fixpp_msg_get_bytes(...)
 *   for fields whose type they don't know at compile time. */
FIXPP_API_EXPORT
fixpp_error_t fixpp_msg_get_string(const fixpp_msg_t* msg,
                                   uint16_t           tag,
                                   const char**       value_out,
                                   size_t*            len_out);

/* Get a raw-byte field view by tag — type-agnostic. */
FIXPP_API_EXPORT
fixpp_error_t fixpp_msg_get_bytes(const fixpp_msg_t*    msg,
                                  uint16_t              tag,
                                  const uint8_t**       bytes_out,
                                  size_t*               len_out);

/* Get an INT field by tag. Parses the on-wire ASCII into int64_t.
 *
 * Reentrancy: FIXPP_REQUIRES_SESSION_LOCK.
 *
 * Returns FIXPP_ERR_OK on success.
 * Returns FIXPP_ERR_NULL_HANDLE / FIXPP_ERR_INVALID_HANDLE / FIXPP_ERR_TAG_NOT_FOUND
 * Returns FIXPP_ERR_TYPE_MISMATCH if the dictionary marks the tag as non-INT.
 * Returns FIXPP_ERR_WIRE_INVALID_FRAME (numeric 100) if the wire bytes
 *   don't parse as a valid integer (e.g., contains non-digit chars). */
FIXPP_API_EXPORT
fixpp_error_t fixpp_msg_get_int(const fixpp_msg_t* msg,
                                uint16_t           tag,
                                int64_t*           value_out);

/* Get a DOUBLE field by tag. Parses the on-wire ASCII into IEEE 754 double.
 *
 * NOTE: this is provided for ergonomic non-financial fields (e.g.,
 * percentage, ratio, leverage). For PRICE / QTY / AMT use fixpp_msg_get_decimal
 * which preserves exact precision. The IEEE-754 conversion can lose precision
 * for values > 2^53 or with > 15 significant digits.
 *
 * Reentrancy: FIXPP_REQUIRES_SESSION_LOCK.
 *
 * Returns FIXPP_ERR_OK on success.
 * Returns FIXPP_ERR_NULL_HANDLE / FIXPP_ERR_INVALID_HANDLE / FIXPP_ERR_TAG_NOT_FOUND.
 * Returns FIXPP_ERR_TYPE_MISMATCH if the dictionary marks the tag as non-FLOAT-family.
 * Returns FIXPP_ERR_WIRE_INVALID_FRAME on parse failure. */
FIXPP_API_EXPORT
fixpp_error_t fixpp_msg_get_double(const fixpp_msg_t* msg,
                                   uint16_t           tag,
                                   double*            value_out);

/* Get a DECIMAL field by tag — PoD shape per [const §X.3] / [2a §5.1].
 *
 * This is the precision-preserving accessor for FIX FLOAT-family fields
 * (PRICE, QTY, AMT, PRICEOFFSET, PERCENTAGE).
 *
 * Reentrancy: FIXPP_REQUIRES_SESSION_LOCK.
 *
 * Returns FIXPP_ERR_OK on success.
 * Returns FIXPP_ERR_NULL_HANDLE / FIXPP_ERR_INVALID_HANDLE / FIXPP_ERR_TAG_NOT_FOUND.
 * Returns FIXPP_ERR_TYPE_MISMATCH if the dictionary marks the tag as non-FLOAT-family.
 * Returns FIXPP_ERR_DECIMAL_INVALID (numeric 800) on bad wire bytes.
 * Returns FIXPP_ERR_DECIMAL_PRECISION_LOSS (numeric 801) if the trait conversion
 *   lost precision (rare; only fires if the engine was built with a non-default
 *   FIXPP_DECIMAL_T per [2a §4.4]). */
FIXPP_API_EXPORT
fixpp_error_t fixpp_msg_get_decimal(const fixpp_msg_t* msg,
                                    uint16_t           tag,
                                    fixpp_decimal_t*   value_out);

/* Check whether a tag is present without reading its value.
 *
 * Reentrancy: FIXPP_REQUIRES_SESSION_LOCK.
 *
 * Returns FIXPP_ERR_OK; *present_out is set to true or false.
 * Returns FIXPP_ERR_NULL_HANDLE / FIXPP_ERR_INVALID_HANDLE. */
FIXPP_API_EXPORT
fixpp_error_t fixpp_msg_has_tag(const fixpp_msg_t* msg,
                                uint16_t           tag,
                                bool*              present_out);

/* Get the resolved MsgType (35) as a small ASCII string view (1–3 chars).
 * Convenience over fixpp_msg_get_string(msg, 35, ...). */
FIXPP_API_EXPORT
fixpp_error_t fixpp_msg_get_msg_type(const fixpp_msg_t* msg,
                                     const char**       value_out,
                                     size_t*            len_out);

#ifdef __cplusplus
}
#endif
#endif /* FIXPP_C_API_MESSAGE_H */
```

**Allocation contract on the read path.** Every accessor in §4.6 is **zero global-heap** — the returned pointer aliases the underlying wire buffer (per `[2b §6.4]` flyweight rule); no `malloc`, no `new`. PMR allocation may occur internally for the offset-table cache (`Index` mode per `[2b §4.4]`), but that allocation is from the per-message arena, not the global heap. Verified by §9 seam #4 (allocation guard).

**Latency Tier 1 ceiling.** ≤ 50 ns p99 on a warm-cache offset-table-hit access — **provisional until v1.0 bench data exists** (Codex P2-3 / Opus confirmed). Per `[2c §6.2]` the typed C++ accessor is ≤ 20 ns; the C-ABI thunk adds NULL check + tag check + `expected_t` unwrap + two pointer writes ≈ 5 ns of plumbing on x86_64 SysV with `noexcept` lambdas (each step is one `cmp`/`branch` or `store` and the compiler typically merges into the underlying call). The v0.1 budget claimed ~30 ns of plumbing — that figure was an unverified estimate; the realistic budget is closer to 5 ns. Verified by §9 seam #2, which checks **both** the absolute ≤ 50 ns ceiling **and** the delta vs the C++ accessor measured on the same runner — the delta MUST be ≤ 10 ns p99 (5 ns target with 2× headroom). If v1.0 bench data shows the absolute ceiling is achievable but the delta exceeds 10 ns, the thunk shape is shaved (per `[const §VIII.2]` perf-regression-budget enforcement) before the ceiling is widened.

**Type-mismatch handling.** When the dictionary knows the tag's type (always true for codegen-known tags; not always true for dialect-overlay-promoted tags or unknown-fields), the accessor checks the type and returns `FIXPP_ERR_TYPE_MISMATCH` if the caller asked for the wrong shape. For unknown / dialect-overlay-promoted tags, the type is **unknown at the C-ABI boundary**; the accessor returns `FIXPP_ERR_OK` with the requested-type interpretation if the bytes parse, or `FIXPP_ERR_WIRE_INVALID_FRAME` if they don't. Documented per-accessor.

### §4.7 Field setters (CA-009)

```c
/* in c_api/message.h continued: */

/* Create an outbound message — the inbound flyweight contract does not apply.
 *
 * The created msg is mutable; the consumer fills fields via the fixpp_msg_set_*
 * family below, then calls fixpp_session_send(session, msg) which serialises
 * + commits + sends.
 *
 * Reentrancy: FIXPP_REQUIRES_SESSION_LOCK (the session's per-message arena is
 * touched).
 *
 * Returns FIXPP_ERR_OK; *msg_out is set to a freshly-constructed handle.
 * Returns FIXPP_ERR_NULL_HANDLE on NULL session/msg_type/msg_out.
 * Returns FIXPP_ERR_INVALID_HANDLE on destroyed session.
 * Returns FIXPP_ERR_DICT_CONFIG (numeric 200) if msg_type is not in the
 *   session's dictionary. */
FIXPP_API_EXPORT
fixpp_error_t fixpp_msg_create_outbound(fixpp_session_t* session,
                                        const char*      msg_type,
                                        size_t           msg_type_len,
                                        fixpp_msg_t**    msg_out);

/* Destroy an outbound message — releases the per-message arena slot.
 * No-op on NULL or already-destroyed handle (per §4.2.1 destroy idempotency).
 *
 * Reentrancy: FIXPP_THREAD_SAFE (the destroy path takes the session lock
 * internally if needed).
 *
 * Returns FIXPP_ERR_OK always (the function returns an error code for
 * uniformity with the rest of the API; FIXPP_ERR_NULL_HANDLE is never
 * returned because NULL is the explicit no-op contract). */
FIXPP_API_EXPORT
fixpp_error_t fixpp_msg_destroy(fixpp_msg_t* msg);

/* Clone an inbound flyweight (or any fixpp_msg_t) into a freshly-constructed
 * outbound-shaped message. The clone is independent of the source: a different
 * per-message arena slot, a different generation token; cross-strand handoff
 * after clone is safe (the clone's lifetime is owner-controlled via
 * fixpp_msg_destroy, not bounded by the source's fromApp dispatch window).
 *
 * Bulk: one memcpy of the wire bytes plus an offset-table rebuild;
 * ≤ 1 µs warm-cache for a ~200-byte message per [2c §6.6] reify-equivalent
 * budget.
 *
 * Reentrancy: FIXPP_REQUIRES_SESSION_LOCK on the source msg's session
 * (the clone is constructed on the source's session strand; the resulting
 * fixpp_msg_t* may then be safely passed to another strand by the caller
 * — that's the whole point of clone).
 *
 * Returns FIXPP_ERR_OK on success; *clone_out is set.
 * Returns FIXPP_ERR_NULL_HANDLE on NULL src or clone_out.
 * Returns FIXPP_ERR_INVALID_HANDLE on destroyed src.
 * Returns FIXPP_ERR_VERSION_MISMATCH if src's resolved version is
 *   not in the engine's loaded dictionaries (rare; surfaces a 2c
 *   `dict_no_dictionary_for_application_version` on the cloned outbound
 *   per [2c §6.7]).
 *
 * Used as the v1.0 cross-strand-handoff escape hatch — see §6.3 and
 * §10 Q5. */
FIXPP_API_EXPORT
fixpp_error_t fixpp_msg_clone(const fixpp_msg_t* src,
                              fixpp_msg_t**      clone_out);

/* Set a STRING field. Bytes are copied into the per-message arena.
 *
 * Lifetime: the caller's buffer (value, len) is borrowed during the call
 * only; the engine deep-copies. The caller may free / reuse the buffer
 * immediately after the call returns.
 *
 * Mid-message invariant: a call to fixpp_msg_set_* invalidates ALL prior
 * fixpp_msg_get_* return pointers on the same fixpp_msg_t (per the §4.6
 * flyweight rule).
 *
 * Reentrancy: FIXPP_REQUIRES_SESSION_LOCK.
 *
 * Returns FIXPP_ERR_OK on success.
 * Returns FIXPP_ERR_NULL_HANDLE / FIXPP_ERR_INVALID_HANDLE.
 * Returns FIXPP_ERR_DICT_CONFIG if the tag is not in the dictionary.
 * Returns FIXPP_ERR_TYPE_MISMATCH if the dictionary marks the tag non-STRING.
 * Returns FIXPP_ERR_WIRE_LIMIT_EXCEEDED if writing the field would push the
 *   message past the per-session frame size cap (the [2b §1.2] frame_too_large
 *   bound is checked at commit time, but obvious overruns are caught here). */
FIXPP_API_EXPORT
fixpp_error_t fixpp_msg_set_string(fixpp_msg_t* msg,
                                   uint16_t     tag,
                                   const char*  value,
                                   size_t       len);

/* Set a raw-byte field (Length+Data pair handled per [2c §6.4] grammar). */
FIXPP_API_EXPORT
fixpp_error_t fixpp_msg_set_bytes(fixpp_msg_t*   msg,
                                  uint16_t       tag,
                                  const uint8_t* bytes,
                                  size_t         len);

/* Set an INT field — engine formats to ASCII. */
FIXPP_API_EXPORT
fixpp_error_t fixpp_msg_set_int(fixpp_msg_t* msg,
                                uint16_t     tag,
                                int64_t      value);

/* Set a DOUBLE field — engine formats to ASCII; precision preserved per
 * IEEE 754 -> string round-trip (~17 significant digits). For PRICE / QTY,
 * use fixpp_msg_set_decimal. */
FIXPP_API_EXPORT
fixpp_error_t fixpp_msg_set_double(fixpp_msg_t* msg,
                                   uint16_t     tag,
                                   double       value);

/* Set a DECIMAL field — PoD per [2a §5.1]; engine formats via [2a §6.2]. */
FIXPP_API_EXPORT
fixpp_error_t fixpp_msg_set_decimal(fixpp_msg_t*    msg,
                                    uint16_t        tag,
                                    fixpp_decimal_t value);

/* Remove a tag from the message (idempotent — no-op if not present). */
FIXPP_API_EXPORT
fixpp_error_t fixpp_msg_remove_tag(fixpp_msg_t* msg,
                                   uint16_t     tag);
```

**Allocation contract on the set path.** Setters DO allocate — bytes are deep-copied into the per-message arena (the `[arch §5.2]` per-session PMR resource). The arena is a `monotonic_buffer_resource` per `[arch §5.2]`; deallocations don't happen until the message is sent (or `fixpp_msg_destroy` is called, which releases the entire arena slot). Documented per-setter.

**No global-heap allocation in the set path either.** The per-message arena is engine-supplied; the consumer's allocator is never touched. Verified by §9 seam #4.

**Latency Tier 1 ceiling.** ≤ 100 ns p99 for `set_int` / `set_double` (warm cache, small message); ≤ 200 ns p99 for `set_string` with a < 64-byte value; setters with longer payloads scale linearly with the byte count (memcpy bound). The setter cost is dominated by the offset-table update + arena bump-pointer allocation; both are O(1) with cache-friendly layouts per `[2b §4.4]`. Verified by §9 seam #2.

### §4.8 Repeating-group accessor (CA-010)

```c
/* in c_api/message.h continued: */

/* Forward-decl per §4.2. */
typedef struct fixpp_group fixpp_group_t;

/* Get a repeating-group cursor. The cursor aliases the underlying message;
 * its lifetime is bounded by the parent fixpp_msg_t's lifetime (same rules
 * as §4.6 accessors).
 *
 * group_tag is the NoXxx tag (e.g., NoLegs = 555 for MultilegOrder).
 *
 * Reentrancy: FIXPP_REQUIRES_SESSION_LOCK.
 *
 * Returns FIXPP_ERR_OK on success; *group_out + *count_out written.
 * Returns FIXPP_ERR_NULL_HANDLE / FIXPP_ERR_INVALID_HANDLE.
 * Returns FIXPP_ERR_TAG_NOT_FOUND if the group is absent.
 * Returns FIXPP_ERR_TYPE_MISMATCH if group_tag is not a NumInGroup tag in
 *   the dictionary. */
FIXPP_API_EXPORT
fixpp_error_t fixpp_msg_get_group(const fixpp_msg_t*    msg,
                                  uint16_t              group_tag,
                                  const fixpp_group_t** group_out,
                                  size_t*               count_out);

/* Read a field from a specific entry of a group cursor.
 *
 * entry_index ranges [0, count); FIXPP_ERR_INDEX_OUT_OF_RANGE otherwise.
 *
 * Reentrancy: FIXPP_REQUIRES_SESSION_LOCK.
 *
 * Lifetime: the returned pointer aliases the underlying message bytes; same
 * lifetime rules as fixpp_msg_get_string.
 *
 * Returns FIXPP_ERR_OK on success.
 * Returns FIXPP_ERR_NULL_HANDLE / FIXPP_ERR_INVALID_HANDLE.
 * Returns FIXPP_ERR_INDEX_OUT_OF_RANGE if entry_index >= count.
 * Returns FIXPP_ERR_TAG_NOT_FOUND if the field is absent from this entry.
 * Returns FIXPP_ERR_TYPE_MISMATCH if the field type doesn't match the
 *   accessor flavour (string vs int vs double vs decimal). */
FIXPP_API_EXPORT
fixpp_error_t fixpp_group_get_field_string(const fixpp_group_t* group,
                                           size_t               entry_index,
                                           uint16_t             tag,
                                           const char**         value_out,
                                           size_t*              len_out);

FIXPP_API_EXPORT
fixpp_error_t fixpp_group_get_field_int(const fixpp_group_t* group,
                                        size_t               entry_index,
                                        uint16_t             tag,
                                        int64_t*             value_out);

FIXPP_API_EXPORT
fixpp_error_t fixpp_group_get_field_double(const fixpp_group_t* group,
                                           size_t               entry_index,
                                           uint16_t             tag,
                                           double*              value_out);

FIXPP_API_EXPORT
fixpp_error_t fixpp_group_get_field_decimal(const fixpp_group_t* group,
                                            size_t               entry_index,
                                            uint16_t             tag,
                                            fixpp_decimal_t*     value_out);

/* Get a nested group cursor (per [2c §4.7] / W-007 nested groups).
 *
 * Same lifetime rules as fixpp_msg_get_group; the returned cursor is
 * bounded by the parent group cursor's lifetime, which is bounded by the
 * parent message's lifetime. */
FIXPP_API_EXPORT
fixpp_error_t fixpp_group_get_nested_group(const fixpp_group_t*  group,
                                           size_t                entry_index,
                                           uint16_t              nested_group_tag,
                                           const fixpp_group_t** nested_out,
                                           size_t*               nested_count_out);

/* Setter side — group construction.
 *
 * Per [2c §4.7]: the typed Writer surface drives group emission via
 * begin_group(...) / add_entry() / end_group(). The C-ABI mirrors with:
 *
 *   fixpp_msg_group_begin(msg, group_tag, &builder)  -> builder cursor
 *   fixpp_group_builder_add_entry(builder)           -> writable entry handle
 *   fixpp_entry_set_string/int/double/decimal(...)
 *   fixpp_msg_group_end(msg, builder)                -> commits, builder invalidated */
FIXPP_API_EXPORT
fixpp_error_t fixpp_msg_group_begin(fixpp_msg_t*               msg,
                                    uint16_t                   group_tag,
                                    fixpp_group_builder_t**    builder_out);

typedef struct fixpp_group_builder fixpp_group_builder_t;
typedef struct fixpp_entry         fixpp_entry_t;

FIXPP_API_EXPORT
fixpp_error_t fixpp_group_builder_add_entry(fixpp_group_builder_t* builder,
                                            fixpp_entry_t**        entry_out);

FIXPP_API_EXPORT
fixpp_error_t fixpp_entry_set_string(fixpp_entry_t* entry,
                                     uint16_t       tag,
                                     const char*    value,
                                     size_t         len);

FIXPP_API_EXPORT
fixpp_error_t fixpp_entry_set_int(fixpp_entry_t* entry,
                                  uint16_t       tag,
                                  int64_t        value);

FIXPP_API_EXPORT
fixpp_error_t fixpp_entry_set_double(fixpp_entry_t* entry,
                                     uint16_t       tag,
                                     double         value);

FIXPP_API_EXPORT
fixpp_error_t fixpp_entry_set_decimal(fixpp_entry_t*  entry,
                                      uint16_t        tag,
                                      fixpp_decimal_t value);

FIXPP_API_EXPORT
fixpp_error_t fixpp_msg_group_end(fixpp_msg_t*           msg,
                                  fixpp_group_builder_t* builder);
```

**Cursor lifetime rule.** The `fixpp_group_t*` returned by `fixpp_msg_get_group` is **non-owning**; it points into the parent message's offset-table data structure. The cursor is invalidated when:

- The parent `fixpp_msg_t` is destroyed (`fixpp_msg_destroy(msg)` for outbound, parse-window-end for inbound).
- A mutating call (`fixpp_msg_set_*` or `fixpp_msg_remove_tag`) is made on the parent message.

After invalidation the cursor MUST NOT be used; debug builds trap, release builds are silent (per `[2b §6.4]` flyweight discipline). The §9 seam #10 (group-cursor lifetime under parent destruction) verifies the contract.

**Why two-step (`begin / add_entry / end`) for setters.** A repeating group's NumInGroup tag must be written **before** the first delimiter tag, but the count is not known until all entries are added. The Writer-side per `[2c §4.7]` reserves space for the NumInGroup tag at `begin`, then back-patches at `end`. The C-ABI mirrors this exact pattern so the C consumer can drive group emission without buffering all entries in their own memory.

### §4.9 Cancellation translation boundary

**Rule (binding contract).** Every C++ engine-side outcome that carries `expected_t::unexpected{*_aborted}` / `*_cancelled` / `sync_lock_aborted` / `clock_sleeps_cancelled` / `dispatch_aborted` / `tls_load_cancelled` / `transport_*_cancelled` / `accept_cancelled` / `store_cancelled` / `store_visitor_aborted_due_to_cancel` (the latter only when the visitor's `abort_error()` virtual returned a cancellation-like reason — see `[2e §6.7]`) is translated **uniformly** to `FIXPP_ERR_CANCELLED` (numeric `1`) at the C ABI boundary.

This is the precedent established by `[2f §6.5]` D.2: `sync_lock_aborted → FIXPP_ERR_CANCELLED`. The same precedent governs `[2d §6.7]` (`dispatch_aborted` / `clock_sleeps_cancelled`), `[2e §6.7]` (`store_cancelled`), `[2g §6.6]` (`tls_load_cancelled`), and `[2h §6.6]` (`transport_*_cancelled`).

**Why uniform, not source-distinguishing.** Three reasons:

1. **The C consumer cannot act differently.** Whether the cancellation came from a transport read, a TLS handshake, or a store write, the consumer's recovery action is the same: log the cancellation, do not interpret as an error condition, do not retry automatically, let the FSM transition (which is engine-side anyway). A source-distinguishing API would force the consumer to enumerate ten variants whose handling is identical.
2. **Source-distinguishing leaks engine internals.** Exposing `FIXPP_ERR_CANCELLED_TRANSPORT_READ` vs `FIXPP_ERR_CANCELLED_STORE_WRITE` ties the consumer's code to the engine's internal layering; a refactor that moves a cancellation site from one layer to another becomes a breaking ABI change.
3. **Stability cost.** Source-distinguishing variants would add ~10 codes to the cancellation surface; with the 2× headroom rule that's ~20 reserved slots that are harder to repurpose later.

**Trade-off / what we lose.** A consumer who genuinely wants to instrument *which* layer cancelled (for debugging or telemetry) cannot do so via the C ABI. They can use `fixpp_strerror()` on the auxiliary diagnostic-error-detail accessor (a future v1.x feature; tracked in §10 Q1). Or they can attach an OTel exporter and read the spans (`[const §XIII]` / `[arch §4.8]`). The C ABI is not the right surface for that introspection.

**Engine-side observable.** The engine MAY log the source-distinguishing variant at `info` level via `[const §XIII]` async logger before translating; the C consumer sees only `FIXPP_ERR_CANCELLED`.

### §4.10 Reentrancy / thread-safety annotations

Every public C-ABI symbol carries exactly one annotation chosen from:

| Macro | Semantics |
|---|---|
| `FIXPP_THREAD_SAFE` | May be called concurrently from any thread without external synchronisation. Examples: `fixpp_strerror`, `fixpp_version`, every `*_destroy`. |
| `FIXPP_SINGLE_THREAD` | The function is not reentrant; the caller MUST ensure no concurrent invocation against the same handle. Examples: `fixpp_engine_create` (one-time engine setup), `fixpp_dict_load_from_xml`. |
| `FIXPP_REQUIRES_SESSION_LOCK` | Must be invoked on the owning session's serialisation domain (the `session_executor` per `[2d §4.8]`) — either from inside a `fromApp` callback (which IS dispatched on the strand per `[2d §7.6]`) or by the consumer explicitly posting onto the session's strand. Concurrent calls from threads outside the session strand produce undefined behaviour. Examples: every `fixpp_msg_get_*`, `fixpp_msg_set_*`, `fixpp_session_send`. |

The annotations are documented per-function via Doxygen-style comments **and** as compile-time grep-able markers. The marker mechanism: each function declaration is preceded by the macro:

```c
FIXPP_THREAD_SAFE
FIXPP_API_EXPORT const char* fixpp_strerror(fixpp_error_t code);

FIXPP_REQUIRES_SESSION_LOCK
FIXPP_API_EXPORT fixpp_error_t fixpp_msg_get_string(...);
```

The macros expand to nothing at compile time:

```c
#define FIXPP_THREAD_SAFE             /* annotation */
#define FIXPP_SINGLE_THREAD           /* annotation */
#define FIXPP_REQUIRES_SESSION_LOCK   /* annotation */
```

**CI enforcement.** `tools/check_capi_reentrancy.sh` greps every declaration in `include/fix/c_api/*.h` and verifies exactly one of the three annotations precedes each `FIXPP_API_EXPORT` declaration. Missing or multiple annotations are a build failure. Verified by §9 seam #7.

**Gate algorithm — false-positive surface (Opus N-P3-2 close).** Three concrete false-positive risks must be addressed by the gate's implementation:

1. **Multi-line declarations.** The published shape is `<annotation>` on its own line preceding `FIXPP_API_EXPORT` on the next line preceding the function declaration on a third line. A naïve `grep -B1 FIXPP_API_EXPORT` matches the immediately-prior line; if a multi-line `/* ... */` doc comment is interposed between the annotation and `FIXPP_API_EXPORT`, the grep mis-identifies the comment's last line as the annotation site.
2. **Conditional declarations.** A `#if defined(FIXPP_BUILDING_DLL)` block that wraps a declaration would put the `FIXPP_API_EXPORT` inside the conditional; a textual gate cannot reliably traverse the conditional shape.
3. **Doxygen comments.** A `/** Reentrancy: thread-safe. */` doc-comment is the desirable Doxygen-driven shape but does not include the macro; the gate cannot use the doc-comment as the annotation source.

The gate's specified algorithm to close these:

- **(a)** The gate runs **after preprocessing** (`gcc -E -P -I include/fix/c_api/` with `-DFIXPP_BUILDING_DLL` set) so conditional blocks are resolved and macro shape is canonical.
- **(b)** The gate matches "annotation token immediately precedes `FIXPP_API_EXPORT` in source order, separated only by whitespace and `extern "C"` braces." Doxygen `/** ... */` comments are skipped (they are stripped by the `-P` preprocessor mode). Block comments inside the declaration's preamble are not allowed; CI fails if encountered (the published shape is `<annotation>\n<FIXPP_API_EXPORT>\n<decl>` — Doxygen above the annotation is fine, between annotation and `FIXPP_API_EXPORT` is a violation).
- **(c)** The §9 seam #7 (`tests/ci/test_capi_reentrancy_annotations.sh`) carries three negative-case fixtures: (i) a multi-line decl with a comment between annotation and `FIXPP_API_EXPORT` — gate must flag; (ii) a conditional `#if` wrapping just `FIXPP_API_EXPORT` without an annotation — gate must flag (after preprocessing); (iii) a missing-annotation declaration — gate must flag. The fixtures live in `tests/ci/fixtures/capi_reentrancy_negative/`.

**Published annotation table (v1.0).** Listed in §4.6, §4.7, §4.8 inline. The full per-symbol table is **generated at sign-off** from the header annotations into `docs/c_api_reentrancy.md` (engineering documentation; not part of the spec doc, not present in the repo at v0.3 authoring time — produced post-sign-off by §9 seam #7's reentrancy-annotation extractor over `include/fix/c_api/*.h`).

---

## §5 Public C++ API (capi-internal translation thunks)

The `fixpp::capi::detail::*` namespace (private; `// detail: not API` per `[arch §3]`) holds the C++-side translation layer. It is **not** a public C++ surface — but the doc must spec it because that's where the actual C ↔ C++ boundary lives.

### §5.1 Header location

`src/capi/detail/translation.hpp` — engine-internal; `#include`d only by `src/capi/*.cpp` translation units. The header is **never** in the install set.

### §5.2 The thunk shape — construction-time vs steady-state split

**Binding rule (RC#3 close).** Per `[arch §5.3]` lines 387–388:

> **Invariant violations:** `assert` in debug; `std::abort` in release. Examples: tag table out of bounds, internal queue invariant broken. These are bugs, not error returns. **Hot path is exception-free.** No `throw` between parse and `fromApp` `[const §VIII.5]`. Exceptions are reserved for construction-time configuration errors (e.g., bad dictionary XML), where the alternative is `expected_t<Engine>` and we choose throw for ergonomics.

The C-ABI thunk surface inherits the split. **Two flavours of `guarded_call` are published**, and every `extern "C"` symbol is placed on exactly one side:

- **Construction-time flavour (`guarded_call_construction`)** — used only by entry points whose invocation is the explicit C-ABI mirror of a constructor that may throw on bad config per `[arch §5.3]` carve-out. The whitelist for v1.0: `fixpp_engine_create`, `fixpp_dict_load_from_xml`, `fixpp_msg_create_outbound`. These thunks **catch** `std::exception&` and translate to a domain-appropriate `FIXPP_ERR_*_CONFIG` (or the new `FIXPP_ERR_CAPI_CONFIG_INVALID` for the engine-construction case where no domain prefix applies — see §6.5 below).
- **Steady-state flavour (`guarded_call_steady`)** — used by every other public C-ABI symbol: every `fixpp_msg_get_*`, every `fixpp_msg_set_*`, every `fixpp_group_*`, `fixpp_msg_destroy`, `fixpp_strerror`, `fixpp_version`, `fixpp_msg_clone`, plus every shape-pinned-here entry point that 2j later publishes on the steady-state path (`fixpp_session_send`, callback-trampoline thunks). An exception escaping a steady-state thunk implies an `assert` failure at the C++ layer or a memory-corruption / PMR-allocator-bypass bug per `[arch §5.3]`'s invariant-violation rule (`std::bad_alloc` from a steady-state path means the project's `[2a §4.2]` `trap_throw` was bypassed — itself an invariant violation). These thunks log the exception at fatal level via `fixpp::core::Logger` (engine-internal) and **`std::abort()`**.

The split matches `[arch §5.3]` exactly: construction-time exceptions are translated for ergonomics; steady-state exceptions are bugs not error returns and are surfaced via the architecturally-mandated `std::abort` path. The v0.1 uniform "translate to `FIXPP_ERR_UNKNOWN`" policy is **dropped** because (a) the steady-state hot path is exception-free per `[const §VIII.5]` — any escape is by definition an invariant violation; (b) translating to `FIXPP_ERR_UNKNOWN` lets the C consumer continue calling into the engine after potential memory corruption, which is a worst-of-both-worlds outcome (the bug is hidden, the consumer's recovery path is ad-hoc); (c) `[const §XV]` does not list "abort on invariant violation" as a banned pattern — the architecturally-mandated path is to `std::abort`.

```cpp
// src/capi/detail/translation.hpp
namespace fixpp::capi::detail {

/* Construction-time guarded_call.
 *
 * Whitelist of consumers (v1.0): fixpp_engine_create,
 * fixpp_dict_load_from_xml, fixpp_msg_create_outbound. Per [arch §5.3]
 * line 388 carve-out ("Exceptions are reserved for construction-time
 * configuration errors ... where the alternative is expected_t<Engine>
 * and we choose throw for ergonomics").
 *
 * The construction-time C-ABI thunk's job is to catch the configuration
 * exception and translate it to the consumer's error-code surface. The
 * domain-appropriate code is selected per call site: dictionary load
 * surfaces FIXPP_ERR_DICT_CONFIG; engine creation surfaces
 * FIXPP_ERR_CAPI_CONFIG_INVALID (see §6.5 — new variant introduced in
 * v0.2 / RC#3 close); outbound message creation surfaces
 * FIXPP_ERR_DICT_CONFIG when the msg_type is not in the dictionary. */
template <typename F>
fixpp_error_t guarded_call_construction(fixpp_error_t fallback_code,
                                        F&&           thunk_body) noexcept {
    try {
        return thunk_body();
    } catch (const std::bad_alloc&) {
        /* Construction-time OOM: surface as the domain config code so the
         * consumer can decide whether to retry with smaller config. The
         * engine-internal logger emits a fatal record before return. */
        return fallback_code;
    } catch (const std::exception&) {
        /* Bad XML, malformed config, etc. */
        return fallback_code;
    } catch (...) {
        /* Foreign exception (non-std::exception). Same disposition. */
        return fallback_code;
    }
}

/* Steady-state guarded_call.
 *
 * Used by every public C-ABI symbol that runs after construction completes:
 * every fixpp_msg_get_*, every fixpp_msg_set_*, every fixpp_group_*,
 * fixpp_msg_destroy, fixpp_msg_clone, fixpp_strerror, fixpp_version, plus
 * the steady-state shape-pinned-here entry points 2j later publishes
 * (fixpp_session_send, callback-trampoline thunks).
 *
 * Per [arch §5.3] / [const §VIII.5]: the hot path is exception-free; an
 * exception escaping a steady-state thunk implies an assert failure at
 * the C++ layer or a memory-corruption / PMR-allocator-bypass bug. We
 * std::abort, after a fatal-level log via fixpp::core::Logger.
 *
 * The std::bad_alloc arm specifically: any allocation on the engine
 * steady-state path goes through PMR resources whose throw is caught by
 * [2a §4.2] trap_throw and converted to expected_t::unexpected{*_oom}
 * at the source layer. Reaching guarded_call_steady's catch arm with
 * a std::bad_alloc means the source-layer trap was bypassed — itself an
 * invariant violation. */
template <typename F>
fixpp_error_t guarded_call_steady(F&& thunk_body) noexcept {
    try {
        return thunk_body();
    } catch (...) {
        fixpp::core::Logger::log_fatal_capi_thunk_exception_then_abort();
        std::abort();
    }
}

/* Translate a fixpp::core::error variant to a fixpp_error_t numeric code.
 *
 * The translation is a switch over the variant tag; the assignment is
 * the §4.3 numeric-block table. */
fixpp_error_t translate(fixpp::core::error e) noexcept;

/* The reverse direction is rarely needed (engine-side never reads
 * fixpp_error_t and turns it back into a C++ variant), but a thin helper
 * exists for the rare case where a callback path round-trips:
 *
 *   - The session FSM dispatches to a user-supplied receive callback that
 *     returns a fixpp_error_t; if the callback indicates rejection, the
 *     FSM needs to translate back to a fixpp::core::error to drive its
 *     own logic. */
fixpp::core::error translate_back(fixpp_error_t code) noexcept;

}  // namespace fixpp::capi::detail
```

**Per-symbol placement.** The §5.2 split rule constrains every `extern "C"` symbol to one of the two flavours; the full per-symbol mapping is **generated at sign-off** in `docs/c_api_thunk_split.md` (engineering documentation; not part of the spec doc, not present in the repo at v0.3 authoring time — produced post-sign-off from the header annotations and verified by §9 seam #5a/#5b). The design-doc rule is that every `extern "C"` symbol carries a comment naming `guarded_call_construction` or `guarded_call_steady` immediately above its definition, and CI grep enforces that exactly one of the two macros appears in every `src/capi/*.cpp` file (§9 seam #5 below splits into #5a / #5b to verify both arms — synthetic-throw on the construction side returns the fallback code; synthetic-throw on the steady-state side fires `SIGABRT`, which the test fixture traps).

**§10 Q2 disposition update.** v1.0 ships with the construction-vs-steady split: construction-time exceptions translate (per `[arch §5.3]` carve-out); steady-state exceptions `std::abort` (per `[arch §5.3]` invariant-violation rule). The v0.1 admission ("the trap decision is one of the explicit v0.1 trade-offs the round-1 review will likely surface") is closed by this rewrite.

### §5.3 Lifetime of C++ objects pointed to by opaque handles

Per §4.2.1, each opaque handle wraps either a `unique_ptr` (engine, dict) or a non-owning pointer (session — engine owns; store — session owns; msg — engine arena owns).

- **`fixpp_engine`** holds `std::unique_ptr<fixpp::core::Engine>`. `fixpp_engine_destroy` calls `unique_ptr` destruction; the engine's own destructor drains all open sessions per `[2d §6.6]`.
- **`fixpp_session`** holds `fixpp::session::Session*` (non-owning). The engine's `Engine::session_map_` is the owner. `fixpp_session_close` calls `Session::close()`; on completion the engine removes from `session_map_` and invalidates the C-ABI `fixpp_session_t` handle (rewrites the `tag_` to `FIXPP_HANDLE_TAG_DEAD`).
- **`fixpp_msg`** holds a `wire::MessageView<Index>` by value plus a generation counter per §4.2.4. For inbound messages, the engine constructs from the parser output and destroys at parse-window close. For outbound messages, the consumer drives construction via `fixpp_msg_create_outbound` and destruction via `fixpp_msg_destroy`.
- **`fixpp_dict`** holds `std::shared_ptr<const fixpp::dict::Dictionary>`. `fixpp_dict_destroy` releases the shared_ptr; the dictionary lives until all sessions using it are destroyed (sessions hold their own shared_ptr per `[2c §4.3]`).
- **`fixpp_store`** holds `fixpp::session::MessageStore*` (non-owning). The session owns per `[2e §6.7]` N1; on session close the handle becomes invalid.

### §5.4 Exception-safety boundary

- **Inside the thunk body:** `noexcept` lambdas only. The thunk body may call into engine C++ code that uses `expected_t<T>` per `[arch §5.3]` — never throws.
- **Across the `extern "C"` boundary:** **no exception ever crosses.** Per the §5.2 split: construction-time thunks trap-and-translate to `FIXPP_ERR_*_CONFIG`; steady-state thunks `std::abort` after a fatal log.
- **Construction-time exceptions** (e.g., `fixpp_engine_create` calling into engine code that throws on bad config; `fixpp_dict_load_from_xml` parsing a malformed XML; `fixpp_msg_create_outbound` rejecting an unknown `msg_type`) are caught by `guarded_call_construction` and translated to the appropriate `FIXPP_ERR_*_CONFIG` (or the new cross-cutting `FIXPP_ERR_CAPI_CONFIG_INVALID` for engine creation when no domain prefix applies — see §6.5) per `[arch §5.3]` carve-out.
- **Steady-state thunk exceptions** (e.g., a `std::bad_alloc` escaping `fixpp_msg_get_string` despite the PMR `[2a §4.2]` `trap_throw`; a foreign exception escaping a callback-trampoline thunk) are caught by `guarded_call_steady` and `std::abort()`'d after a fatal-level log per `[arch §5.3]` invariant-violation rule. No `FIXPP_ERR_UNKNOWN` is returned on the steady-state path; the abort is the architecturally-mandated trap.

---

## §6 Behavioral contract

### §6.1 Allocation discipline

| Operation | Path | Allocation |
|---|---|---|
| `fixpp_msg_get_*` (any flavour) | Read accessor, hot path (potentially inside `fromApp`) | **ZERO** global heap. Aliases the underlying wire buffer via `[2b §6.4]` flyweight rule. PMR allocation may occur for offset-table cache (per-message arena per `[arch §5.2]`); never the global heap. |
| `fixpp_msg_set_*` (any flavour) | Write setter, NOT-hot-path (outbound construction) | Bytes copied into the per-message arena (`[arch §5.2]` `monotonic_buffer_resource`). NOT the global heap. The arena is engine-supplied; the consumer's allocator is never touched. |
| `fixpp_msg_get_group` | Read accessor, hot path | ZERO global heap. The cursor is a small value-typed handle into the parent message's offset table. PMR allocation for per-entry indexing may occur in the per-message arena. |
| `fixpp_group_get_field_*` | Read accessor, hot path | ZERO global heap. Same flyweight rule. |
| `fixpp_msg_group_begin` / `_add_entry` / `_end` | Setter, NOT-hot-path | Per-message arena allocation for the group entries; never the global heap. |
| `fixpp_msg_create_outbound` | Construction, NOT-hot-path | One allocation from the per-message arena for the wire buffer's initial 4 KiB SBO; subsequent setters may grow this if needed (still arena-backed). |
| `fixpp_msg_destroy` | Destruction | ZERO global heap. Releases the arena slot via the session's arena reset. |
| `fixpp_strerror` | Diagnostic | ZERO. Static lookup table per §4.4. |
| `fixpp_version` / `fixpp_library_version` | Diagnostic | ZERO. Returns a value-typed PoD. |

**Verified by §9 seam #4** (allocation guard) — `mallocnesia` interceptor + `tools/check_alloc.py` post-link symbol scan, on Linux/Clang Tier 1, per `[2a §9]` / `[2b §9]` / `[2d §9]` / `[2g §9]` / `[2h §9]` precedent.

### §6.2 Exception safety

- **No exception crosses `extern "C"` from a steady-state thunk** — `std::abort` (with fatal log) is the trap, per the §5.2 / §5.4 construction-vs-steady split. The hot-path read accessor is the canonical steady-state thunk: it runs inside `fromApp`'s session strand per `[2d §7.6]`, and per `[const §VIII.5]` the parse-↔-`fromApp` window is exception-free; an exception escaping it is by definition an invariant violation per `[arch §5.3]` line 387. Verified by §9 seam #5b (synthetic-throw on a steady-state thunk fires `SIGABRT`; the test fixture traps it).
- **No exception crosses `extern "C"` from a construction-time thunk either.** Construction-time thunks (`fixpp_engine_create`, `fixpp_dict_load_from_xml`, `fixpp_msg_create_outbound`) catch and translate to a domain-appropriate config code per `[arch §5.3]` line 388 carve-out; verified by §9 seam #5a (synthetic-throw on a construction thunk → `FIXPP_ERR_*_CONFIG`).
- **The hot path is exception-free.** `[arch §5.3]` / `[const §VIII.5]` extends through the read-accessor C-ABI thunk (see §6.1 row 1); no `try/catch` overhead is paid on the happy path because modern compilers (clang ≥ 16, GCC ≥ 13, MSVC 19.40+) implement zero-cost exceptions. The steady-state `guarded_call_steady` wrapper costs zero on the happy path.
- **Construction-time exceptions** in engine code are caught at the construction thunk and translated to `FIXPP_ERR_THREAD_CONFIG` / `FIXPP_ERR_DICT_CONFIG` / `FIXPP_ERR_TRANSPORT_CONFIG` / `FIXPP_ERR_CAPI_CONFIG_INVALID` / etc. per the source layer. The source-layer doc owns the list of construction-time error codes (see §4.3 block assignments and §6.5 below for the new `FIXPP_ERR_CAPI_CONFIG_INVALID` code introduced in v0.2).

### §6.3 Threading

Every accessor / setter that touches a `fixpp_msg_t` is `FIXPP_REQUIRES_SESSION_LOCK` per §4.10. Concretely:

- The C consumer that registers a receive callback via `fixpp_session_register_callback(session, cb, userdata)` (signature owned by 2j) sees the callback fire on the session strand per `[2d §7.6]`. From inside the callback, the consumer may freely call `fixpp_msg_get_*` on the inbound message handle.
- The C consumer that constructs an outbound message via `fixpp_msg_create_outbound` MUST do so from the session strand. The recommended pattern is to drive outbound construction from inside a `fromApp` callback (the simplest correct shape) or to explicitly post a closure onto the session's strand via `fixpp_session_post(session, closure, userdata)` (signature owned by 2j).
- Cross-thread access patterns (constructing an outbound message on thread A and sending it on thread B) are **explicitly unsupported** in v1.0. The `fixpp_msg_t` handle is bound to the session strand; cross-thread use is undefined.

The v1.0 cross-strand handoff escape hatch is `fixpp_msg_clone(src, &clone)` declared in §4.7 (the C++-side `dict::reify` pattern from `[2c §4.8]` is engine-internal — the C-ABI consumer does not see `reify` directly). Clone is one bulk memcpy plus an offset-table rebuild; ≤ 1 µs warm-cache per `[2c §6.6]`. The clone's lifetime is owner-controlled via `fixpp_msg_destroy`; cross-strand handoff after clone is safe because the clone has its own per-message arena slot and its own generation token. Verified by §9 seam #13.

### §6.4 Latency Tier 1 ceilings

| Operation | Ceiling | Rationale |
|---|---|---|
| `fixpp_msg_get_string` warm-cache offset-table-hit | **≤ 50 ns p99 (provisional)** | C++ accessor ≤ 20 ns per `[2c §6.2]`; thunk plumbing ~5 ns target (NULL check ≈ 1 ns predictable branch + tag check ≈ 2 ns load+cmp+branch + `expected_t::has_value` ≈ 1 ns + two `*pointer_out` writes ≈ 1 ns each). Provisional until v1.0 bench data; CI checks delta vs C++ accessor ≤ 10 ns p99. |
| `fixpp_msg_get_int` warm-cache | **≤ 80 ns p99 (provisional)** | adds the `[2b §4.3]` fast int parse (~20 ns for 1–9 digit values) over the string accessor; same ~5 ns plumbing target. |
| `fixpp_msg_get_double` warm-cache | **≤ 200 ns p99** | dominated by `strtod`; CI flags > 5% regression. |
| `fixpp_msg_get_decimal` warm-cache | **≤ 80 ns p99 (provisional)** | per `[2a §6.5]` decimal parse ≤ 50 ns + thunk plumbing ~5 ns target + `decimal_traits<T>::to_pod` conversion ≤ 25 ns. The headroom (≤ 80 ns − 50 ns parse ≈ 30 ns) absorbs both the plumbing and the to-pod conversion; the plumbing component itself is ~5 ns target on the same shape as `_get_string`. |
| `fixpp_msg_set_int` warm-cache | **≤ 100 ns p99** | one int-to-ASCII format (~30 ns) + offset-table append (~30 ns) + arena bump (~20 ns) + thunk plumbing ~5 ns. |
| `fixpp_msg_set_string` warm-cache, ≤ 64-byte value | **≤ 200 ns p99** | dominated by memcpy of the value into the arena. Scales linearly with byte count for longer payloads. |
| `fixpp_msg_get_group` warm-cache, ≤ 8 entries | **≤ 100 ns p99** | offset-table find (~30 ns) + cursor construction (~30 ns) + thunk plumbing ~5 ns. |
| `fixpp_group_get_field_string` warm-cache | **≤ 60 ns p99** | adds an entry-index lookup over the message-level accessor; same ~5 ns plumbing. |
| `fixpp_strerror` | **≤ 10 ns p99** | bounds check + one indexed load on a ~200-element static `const char*` lookup table; one nullptr-check fallback (`return s ? s : "reserved error code";`) ≈ 4–6 ns realistic. CI flags > 5 % regression. |
| `fixpp_version` | **≤ 5 ns p99** | Returns 8-byte PoD `fixpp_version_t` from a static initializer; lower bound is the C-ABI return-value-shuffle cost on x86_64 SysV (~3 ns). |
| `fixpp_msg_clone` warm-cache, ~200-byte msg | **≤ 1 µs p99** | one bulk memcpy of the wire bytes + offset-table rebuild per `[2c §6.6]` reify-equivalent budget; verified by §9 seam #13. |

CI flags > 5 % regression on the hot-path rows. Per `[const §VIII.1]` / `[const §VIII.2]` / `[const §VIII.3]`. Verified by §9 seam #2 (absolute ceilings + delta-vs-C++-accessor check on the same runner).

### §6.5 Errors introduced by this design

**Counting convention (v0.3 phrasing tightness — Opus round-2 N-P3-2 close).** The cross-cutting `[0, 99]` block carries **11 occupied codes** at v0.3 = **3 architectural sentinels** (`FIXPP_ERR_OK`, `FIXPP_ERR_CANCELLED`, `FIXPP_ERR_UNKNOWN` per `[arch §5.3]`) + **8 2i-introduced variants** (`FIXPP_ERR_NULL_HANDLE` through `FIXPP_ERR_CAPI_CONFIG_INVALID`). The 8 introduced variants enumerated below grew the cross-cutting block from 10 occupied codes (v0.1: 3 sentinels + 7 introduced — `_NULL_HANDLE` through `_INDEX_OUT_OF_RANGE`) to 11 (v0.2: added `FIXPP_ERR_CAPI_CONFIG_INVALID = 10` under RC#3 close — the construction-time C-ABI thunk fallback for engine creation where no domain `_CONFIG` code applies). Throughout this doc, "8 introduced" refers to 2i-introduced variants only; "11 occupied" or "11 codes" refers to the full block including the 3 architectural sentinels.

2i introduces 8 new `fixpp_error_t` variants (the cross-cutting block sentinels at numeric codes `[3, 10]`; v0.2 adds `FIXPP_ERR_CAPI_CONFIG_INVALID = 10` per RC#3 close); the rest of the §4.3 layout is **re-publication** of variants owned by sibling docs (their definitions per their `[2X §6.X]` sections — see §3 inherited surface citations; the live total of prior-doc variants is 4 + 13 + 20 + 9 + 10 + 4 + 15 + 22 = 97 per `[2a §7.4]` / `[2b §6.7]` / `[2c §6.7]` / `[2d §6.7]` / `[2e §6.7]` / `[2f §6.5]` / `[2g §6.6]` / `[2h §6.6]`).

| `fixpp_error_t` variant | Numeric | Source section | Remediation class |
|---|---|---|---|
| `FIXPP_ERR_NULL_HANDLE` | 3 | §4.2.1 — any entry-point with a handle param received `NULL`. | Programmer error — pass a valid handle. |
| `FIXPP_ERR_INVALID_HANDLE` | 4 | §4.2.1 / §4.2.2 — handle passed has been destroyed (tag rewritten to `FIXPP_HANDLE_TAG_DEAD`) OR has the wrong type tag (caller passed a `fixpp_session_t*` to a `fixpp_msg_*` function). | Programmer error — check destroy ordering / handle types. |
| `FIXPP_ERR_VERSION_MISMATCH` | 5 | §4.5 — `fixpp_engine_create(consumer_major, ...)` got `consumer_major != engine_major`. Also fired by any function on an engine handle whose construction failed for the version reason. | Configuration error — rebuild consumer against the matching engine ABI. |
| `FIXPP_ERR_BUFFER_TOO_SMALL` | 6 | §4.4 / `[2a §5.2]` — caller-supplied output buffer is shorter than required. | Programmer error — size buffer per the published worst-case ceiling (`[2a §5.2]` 41 bytes for decimal format; per accessor for others). |
| `FIXPP_ERR_TYPE_MISMATCH` | 7 | §4.6 / §4.7 — caller used `fixpp_msg_get_int` (or `_double` / `_decimal`) on a tag the dictionary marks as a non-INT (or non-FLOAT) field. | Programmer error — use the type-correct accessor or fall back to `fixpp_msg_get_bytes` for type-erased access. |
| `FIXPP_ERR_TAG_NOT_FOUND` | 8 | §4.6 / §4.8 — the tag is absent from the message. | Caller's choice — either treat as missing-optional-field or fall through to validator-driven Session-Reject. |
| `FIXPP_ERR_INDEX_OUT_OF_RANGE` | 9 | §4.8 — `fixpp_group_get_field_*(group, entry_index, ...)` was called with `entry_index >= count`. | Programmer error — fix the loop bound (the group's count is returned by `fixpp_msg_get_group`'s `count_out` parameter). |
| `FIXPP_ERR_CAPI_CONFIG_INVALID` | 10 | §5.2 / §5.4 / §6.2 (NEW v0.2 / RC#3 close) — a construction-time C-ABI thunk (`fixpp_engine_create`, `fixpp_dict_load_from_xml` for the no-domain-prefix case, or any future construction-time C-ABI entry that lacks a domain prefix) caught a `std::exception` and chose this fallback code. Used only by `guarded_call_construction` per `[arch §5.3]` carve-out where no specific domain `_CONFIG` code applies. | Configuration error — inspect the engine-internal logger's fatal-level record for the exception detail; correct the config; retry. |

(8 new variants in the cross-cutting `[0, 99]` block — `FIXPP_ERR_NULL_HANDLE` through `FIXPP_ERR_INDEX_OUT_OF_RANGE` from v0.1 plus `FIXPP_ERR_CAPI_CONFIG_INVALID` added in v0.2 per RC#3 close; the cancellation sentinel `FIXPP_ERR_CANCELLED = 1` and `FIXPP_ERR_OK = 0` and `FIXPP_ERR_UNKNOWN = 2` are pre-existing per `[arch §5.3]` and are not new variants here — they are codified at this point per the brief's "lock the layout" requirement.)

The variants 2i merely **indexes** (re-publishes from sibling 2X docs in §4.3) are NOT in this table; they are 2b/2c/2d/2e/2f/2g/2h-owned per the citations in §3 / §4.3.

---

## §7 Integration with adjacent modules

### §7.1 2a (decimal PoD at C boundary)

Per `[2a §5.1]` lines 233–240 (verbatim quote in §3.8), `fixpp_decimal_t` is `(int64 mantissa, int8 exponent, int8 _reserved[7])` with `sizeof = 16`, `alignof = 8`. Per `[2a §5.2]` lines 252–274 the boundary functions `fixpp_decimal_parse / _format / _compare / _equal / _init` live in `c_api/decimal.h` (2a-owned).

**2i's role:** publish `fixpp_msg_get_decimal` and `fixpp_msg_set_decimal` (§4.6 / §4.7) that consume / produce the PoD. The thunk for `fixpp_msg_get_decimal` is:

```cpp
extern "C" FIXPP_API_EXPORT
fixpp_error_t fixpp_msg_get_decimal(const fixpp_msg_t* msg,
                                    uint16_t           tag,
                                    fixpp_decimal_t*   value_out) {
    return fixpp::capi::detail::guarded_call([&]() noexcept -> fixpp_error_t {
        if (!msg || !value_out) return FIXPP_ERR_NULL_HANDLE;
        if (msg->tag_ != FIXPP_HANDLE_TAG_MSG) return FIXPP_ERR_INVALID_HANDLE;
        auto result = msg->view_.get_decimal(tag);
        if (!result.has_value()) return fixpp::capi::detail::translate(result.error());
        /* Convert from C++ decimal<T> to PoD via [2a §6.4] cross-traits. */
        *value_out = fixpp::core::decimal_traits<FIXPP_DECIMAL_T>::to_pod(*result);
        return FIXPP_ERR_OK;
    });
}
```

The C++ `decimal<T>::to_pod` may produce `decimal_precision_loss` if the engine was built with a non-default `FIXPP_DECIMAL_T` per `[2a §4.4]`; the variant is translated to `FIXPP_ERR_DECIMAL_PRECISION_LOSS` (numeric 801) at the C ABI per §4.3.

### §7.2 2b (wire view ↔ C accessor lifetime)

Per `[2b §6.4]` flyweight lifetime contract (cross-cited in `[2c §6.5]` line 1672–1675): typed-message flyweights hold a `wire::MessageView<Index>` by reference; the view aliases the originating frame buffer; the buffer's lifetime is the per-message arena's slot, reset by the session FSM after `fromApp` returns.

**2i's role:** the C-ABI accessor returns a `const char*` aliasing that flyweight's bytes; the lifetime contract per §4.6 is the C-side restatement of `[2b §6.4]`. The §9 seam #10 verifies the contract by deliberately capturing a returned pointer past `fromApp` return and checking that the debug-build trap fires.

### §7.3 2c (dictionary / typed message)

Per `[2c §5]` commitments 1–6 (cross-cited in §3.10): 2i is **dictionary-agnostic on the read path AND on the set path**. The accessors take a `(tag, view)` pair, not a typed-class handle. The `fixpp_msg_t` opaque handle internally carries the resolved per-message version per `[2c §5]` commitment 1; the accessor uses the version to consult the right dictionary's metadata for type-checking purposes (`FIXPP_ERR_TYPE_MISMATCH` returns), but the accessor's signature does not name a typed class.

The `[2c §6.7]` shared variant `dict_no_dictionary_for_application_version` (introduced in 2c v1.2 per N2-P3-2; cross-cited in `[2d §6.7]` close of round-2 N2-P2-1) maps to `FIXPP_ERR_DICT_CONFIG` at the C ABI per §4.3.

### §7.4 2d (executor / cancellation translation)

Per `[2d §6.7]` (cited in §3.11): 2d's 9 variants coalesce into `FIXPP_ERR_THREAD_CONFIG` / `FIXPP_ERR_THREAD_SESSION_LIFECYCLE` / `FIXPP_ERR_THREAD_RUNTIME` / `FIXPP_ERR_CANCELLED` (the cancellation pair `clock_sleeps_cancelled` + `dispatch_aborted` joins the cross-cutting `FIXPP_ERR_CANCELLED`). 2i ratifies in §4.3.

Per `[2d §4.7]` (cited in §3.12): every ASIO-cancellation outcome translates to `FIXPP_ERR_CANCELLED` via the §4.9 uniform translation rule.

Per `[2d §7.6]`: the C-ABI accessor / setter family is `FIXPP_REQUIRES_SESSION_LOCK` because the underlying `wire::MessageView` is on the session strand.

### §7.5 2e (store handle shape)

Per `[2e §6.7]` (cited in §3.13) and `[2e §6.7]` N1 (cited in §4.2.1): `fixpp_store_t` is a non-owning observer of a session-owned `MessageStore`; the handle becomes invalid on `fixpp_session_close`. The per-method error codes coalesce per `[2e §6.7]` lines 1126–1131 into `FIXPP_ERR_STORE_RUNTIME` / `FIXPP_ERR_STORE_CONSISTENCY` / `FIXPP_ERR_STORE_CONFIG` / `FIXPP_ERR_STORE_VISITOR`. 2i ratifies.

The actual `fixpp_store_*` symbol set (e.g., `fixpp_store_get_next_seqnum`, `fixpp_store_retrieve`) is owned by 2e (or by 2j when those operations are exposed via the control plane); 2i's role is the handle-shape contract only.

### §7.6 2f (sync_lock_aborted → FIXPP_ERR_CANCELLED — direct precedent)

Per `[2f §6.5]` lines 1497–1506 (cited in §3.14) and `[2f Appendix D §D.2]`: `sync_lock_aborted → FIXPP_ERR_CANCELLED`. This is the **direct precedent** for §4.9's broader cancellation translation rule. 2i's §4.9 rule generalises the precedent to every cancellation source.

### §7.7 2g (no TLS surface in v1.0 C ABI)

Per `[2g §7.6]` lines 1052–1054 (cited in §1.2 non-goal #6): the TLS C-ABI surface is delegated to 2i. 2i's v1.0 decision is to expose **no `fixpp_cert_source_t` / `fixpp_pinset_t` accessors**. The 15 `[2g §6.6]` variants are indexed via the `FIXPP_ERR_TLS_*` block in §4.3, but the consumer cannot call rotation operations from the C ABI in v1.0.

A consumer that needs runtime cert / pinset rotation triggers it via 2j's control plane — the `ReloadCertSource` RPC mentioned in `[2g §7.7]`. The control plane is gRPC by default but pluggable; the consumer of the gRPC schema (regardless of language) can drive rotation that way.

**Why defer the TLS C-ABI accessors?** Two reasons: (a) the `cert_source` / `Pinset` C++ shapes are PMR-rich and use `expected_t<shared_ptr<...>>` returns extensively; the C-ABI translation would multiply the surface significantly, and v1.0 has no consumer asking for it; (b) cert / pinset operations are rare and not latency-critical, so a control-plane RPC is the right shape. Tracked for v1.x in §10 Q3.

### §7.8 2h (consumer drop-in honoured)

Per `[2h §7.8]` lines 1304–1306 (verbatim quote in §3.17): 2h hands off `fixpp_transport_t`, `fixpp_tls_transport_t`, `fixpp_listener_t`, `fixpp_transport_factory_t`, `fixpp_endpoint_t`, `fixpp_reconnect_policy_t`, `fixpp_connect_info_t`, plus the `FIXPP_ERR_TRANSPORT_*` coalescing groups.

**2i's response (this doc):**

- `FIXPP_ERR_TRANSPORT_*` coalescing groups: ratified in §4.3 as the `[700, 799]` numeric block per §3.16. Sub-groups `FIXPP_ERR_TRANSPORT_LIFECYCLE` / `FIXPP_ERR_TRANSPORT_IO` / `FIXPP_ERR_TRANSPORT_HANDSHAKE` / `FIXPP_ERR_TRANSPORT_CONFIG` per `[2h §6.6]` lines 1200–1204. Cancellation joins `FIXPP_ERR_CANCELLED` per §4.9.
- `FIXPP_ERR_TRANSPORT_HANDSHAKE` joins `FIXPP_ERR_TLS_HANDSHAKE` at the C-ABI level per `[2h §6.6]` line 1202: a single C-ABI consumer receiving either `FIXPP_ERR_TRANSPORT_HANDSHAKE` (numeric 702) or `FIXPP_ERR_TLS_HANDSHAKE` (numeric 601) on a TLS handshake failure can be told they are semantically equivalent. They are **not** numerically merged (different blocks per §1.1 layout); they are documented as a coalesced pair.
- `fixpp_transport_t` / `fixpp_tls_transport_t` / `fixpp_listener_t` / `fixpp_transport_factory_t` opaque handles: **deferred to v1.x.** No `fixpp_transport_*` C-ABI symbol ships in v1.0 per §1.2 non-goal #7. The C-ABI consumer interacts with sessions (`fixpp_session_*`), not transports. The handle-shape contract for those types (rule 1: `extern "C"` opaque struct forward declaration; rule 2: type-tag at fixed offset; rule 3: idempotent `*_destroy`) IS published in §4.2 for whenever 2j or a v1.x C-ABI extension wants to expose them.
- `fixpp_endpoint_t` / `fixpp_reconnect_policy_t` / `fixpp_connect_info_t` PoD types: also deferred to v1.x. The PoD-shape contract (rule 1: standard-layout C structs; rule 2: explicit `_reserved` bytes for forward compat per `[2a §5.1]` precedent) IS published in §4.2 for future use.

The consumer-side cross-doc commitment from `[2h §7.8]` is honoured: 2i has reviewed the shapes 2h enumerates, ratified the error-coalescing assignment, and explicitly deferred the transport handle / PoD types to a v1.x extension. No 2h shape is "fundamentally incompatible" with the C ABI per `[2h §5]` line 1059; the deferral is purely scope.

### §7.9 Hand-off to 2j (engine/session lifecycle)

CA-005 / CA-006 / CA-007 are **shape-only cross-cuts** to 2j (per Appendix A.2). 2i pins:

- The opaque-handle declaration form for `fixpp_engine_t` / `fixpp_session_t` (§4.2).
- The destroy discipline (§4.2.1).
- The C ↔ C++ thunk shape (§5).
- The reentrancy annotation (§4.10).
- The error-translation rules (§4.4 / §4.9).
- The version-binding protocol on `fixpp_engine_create` (§4.5).

**2j owns:**

- The `fixpp_engine_create(...) → fixpp_engine_t*` signature (the `EngineConfig` PoD shape; the gRPC control-plane registration; the OTel exporter wiring per `[arch §4.8]`).
- `fixpp_session_open(engine, session_config, error_out) → fixpp_session_t*` and `fixpp_session_close(session)`.
- `fixpp_session_send(session, msg) → fixpp_error_t` (CA-006).
- `fixpp_session_register_callback(session, cb, userdata) → fixpp_error_t` and the receive-callback signature (CA-007).
- Whether the receive-callback is sync (engine calls user fn directly on the strand) or async (engine queues; user drains via `fixpp_session_poll(session, ...)`); see §10 Q4.

### §7.10 Hand-off to 2k (C-ABI logging/otel)

`c_api/log.h` and `c_api/otel.h` are owned by 2k. 2i provides:

- The `FIXPP_ERR_LOG_*` and `FIXPP_ERR_OTEL_*` numeric block (`[1000, 1099]` per §1.1).
- The opaque-handle plumbing rules (a `fixpp_log_sink_t` type would follow §4.2).
- The reentrancy annotation taxonomy (a sink's `write` is `FIXPP_THREAD_SAFE` from the producer side per `[const §XIII.2]`).

2k publishes the logger / OTel C-ABI surface; 2i is the inheritance source.

### §7.11 Hand-off to 2l (no C-ABI surface for tap consumer in v1.0)

Per `[arch §4.9]` and `[SYN §3.6 #22]`: tap consumers are `RingBufferTap` / `Iox2Tap` / `SyncCallbackTap`. The **iceoryx2 cross-process publisher** is the cross-language consumption surface (a non-C++ subscriber attaches to the iceoryx2 topic; iceoryx2 has its own C ABI). The in-process tap consumer is C++-only in v1.0 — no `fixpp_tap_consumer_t` C-ABI handle.

The `FIXPP_ERR_TAP_*` block `[1100, 1199]` per §1.1 is reserved for v1.x in case 2l decides to expose an in-process C-ABI tap accessor.

### §7.12 Hand-off to 2m (SWIG/Python)

2i's surface IS 2m's input. The 2m design doc (drafted later in Phase 2) consumes:

- The full `<fix/c_api.h>` umbrella.
- The opaque-handle declarations.
- The `fixpp_error_t` enum (which 2m maps to a Python `FixppError` exception class with stable enum values per `[arch §4.12]`).
- The reentrancy annotations (which 2m maps to GIL-acquire / GIL-release patterns per `[SYN §3.5 #18]`).

2m has no incoming dependency on 2i beyond "2i must be signed off before 2m's design pass." Recorded in §11 hand-off.

---

## §8 PMR — recap

2i's allocation surface mostly does NOT involve PMR — the C ABI hides PMR behind opaque handles. Storage classes:

| Storage | Lifetime | Holds | Reset by |
|---|---|---|---|
| **Engine arena (per `[arch §5.2]`)** | Engine lifetime | Long-lifetime engine state (the `Engine` object, the dictionary registry, the version-binding consumer-version stamp). Engine-side; not directly visible at the C ABI. | `~fixpp_engine` |
| **Session arena (per `[arch §5.2]`)** | Session lifetime (frozen at session open per `[arch §5.6]`) | The session's `MessageStore` (`[2e §4.4]`); the `Pinset` (`[2g §4.3]`); the `SslCtxConfig` (`[2g §4.5]`); the `Transport` (`[2h §4.5]`); the session-local trace context (`[2d §4.6]`). Engine-side. | `Session::close()` |
| **Per-message arena** | One outbound message construction cycle, OR one inbound `fromApp` dispatch window | The wire buffer for outbound message construction; the offset-table cache for inbound parse; the cursor objects returned by `fixpp_msg_get_group`; the deep-copies made by `fixpp_msg_set_*`. | `fixpp_msg_destroy` (outbound) or `fromApp` return (inbound) |
| **Static lookup table** | Engine binary lifetime | The `fixpp_strerror` lookup table; the `fixpp_msg_get_msg_type` cached MsgType-to-symbol table. | (never; const-initialized) |
| **Caller-supplied buffer** | Caller-owned; no engine touch | The caller's output buffer for `fixpp_decimal_format(buf, len)` per `[2a §5.2]`. The engine writes into it during the call; on return the engine forgets the pointer. | (caller's responsibility) |

**Lifetime classes for caller-passed buffers vs engine-owned buffers:**

- **Caller-passed buffer (write target).** The engine writes during the call only; the buffer is forgotten on return. Caller may free / reuse immediately. Examples: `fixpp_decimal_format(d, dst, dst_cap, &written)` per `[2a §5.2]`.
- **Caller-passed buffer (read source).** The engine reads during the call only; the buffer is borrowed. Caller may free / reuse immediately on return. Examples: `fixpp_msg_set_string(msg, tag, value, len)` — the engine deep-copies `value[0..len)` into the per-message arena before returning.
- **Engine-owned buffer (read source returned to caller).** The engine returns a pointer into its own arena. The caller MUST NOT free; the lifetime is bounded by the rules in §4.6 (until next mutating call, OR until `fromApp` returns, OR until `fixpp_msg_destroy`). Examples: `fixpp_msg_get_string`'s `*value_out`.

**Per `[const §VIII.5]`: zero `new`/`delete` between parse and `fromApp`.** The C-ABI accessor hot path (any `fixpp_msg_get_*` invoked from inside a `fromApp` callback) MUST NOT `malloc`, `new`, or PMR-allocate from the global heap. Per-message arena allocation is allowed; global heap is not. The §9 seam #4 (allocation guard) verifies under `mallocnesia` on Linux/Clang Tier 1.

---

## §9 Test seams

Per `[arch §10]` requirement (4) and `[const §VII.4]`. 2i ships **13 seams** (≥ 10 brief minima; the extras cover ABI-version mismatch, type-mismatch, the cross-doc reentrancy-annotation grep, and the v0.2-added `fixpp_msg_clone` cross-strand-handoff verification). Each seam is referenced by **name** per the `[2d §9]` / `[2g §9]` / `[2h §9]` cross-referencing precedent — ordinals are not stable across review rounds; names are.

1. **Conformance corpus — round-trip C-ABI accessors against a known FIX 4.4 message.** Drive the `tests/conformance/` corpus (TC-001..TC-017 per `[const §VII.5]`) end-to-end through the C-ABI: parse a known wire message via `fixpp_msg_*`, walk every tag via `fixpp_msg_get_string`, verify byte-for-byte the C++ direct-access path produces the same values. Lives in `tests/conformance/test_capi_round_trip.cpp`.

2. **Latency regression — read accessor + setter.** Google Benchmark on the warm-cache `fixpp_msg_get_string` / `fixpp_msg_get_int` / `fixpp_msg_get_decimal` / `fixpp_msg_set_int` / `fixpp_msg_set_string` paths; verify the §6.4 ceilings. CI fails on > 5% regression on the hot rows. Lives in `bench/capi/bench_msg_accessors.cpp`.

3. **Allocation guard on the accessor hot path.** `tools/check_alloc.py` + `mallocnesia` (Linux/Clang Tier 1 per the `[2a §9]` / `[2b §9]` / `[2d §9]` / `[2g §9]` / `[2h §9]` precedent). 10⁴-frame test; zero global-heap `new`/`delete`/`malloc` on the C-ABI read accessor + setter chain when invoked from inside `fromApp`. PMR-arena allocations are expected. Lives in `tests/perf/test_capi_alloc_guard.cpp`.

4. **Fuzzer (parser-touching seam) — random byte sequences through the accessors.** libFuzzer-driven random byte streams parsed by the engine, then handed to the C-ABI consumer; ASan + UBSan invariants; verify no crash, no UAF, no UB on adversarial inputs. Specifically targets the `fixpp_msg_get_*` accessor surface (the natural fuzz frontier in 2i since the accessor surface is the C-ABI's first contact with consumer-driven access patterns). Required by `[const §VII.7]` parser-touching surface. Lives in `tests/fuzz/fuzz_capi_accessors.cpp`.

5. **Synthetic-throw fault injection — split: construction-time vs steady-state.** Two test fixtures cover the §5.2 split per RC#3:
   - **5a (construction-time).** The engine-internal C++ code is patched (test-build-only via `#define FIXPP_CAPI_INJECT_THROW_CONSTRUCTION`) to throw a `std::runtime_error` from inside `fixpp_engine_create` / `fixpp_dict_load_from_xml` / `fixpp_msg_create_outbound`; the test verifies the C-ABI return is the domain-appropriate `FIXPP_ERR_*_CONFIG` (or `FIXPP_ERR_CAPI_CONFIG_INVALID` per §6.5), the engine-internal logger emits a fatal-level log record with the exception message, and the process does NOT abort. Verifies the construction-time half of the §5.2 / §6.2 contract.
   - **5b (steady-state).** The engine-internal C++ code is patched (test-build-only via `#define FIXPP_CAPI_INJECT_THROW_STEADY`) to throw a `std::runtime_error` from inside a steady-state thunk (e.g., `fixpp_msg_get_string`); the test fixture traps `SIGABRT` via `sigaction(SIGABRT, ...)` + `setjmp/longjmp` and verifies that abort fires (i.e., the test process would die without the trap). Verifies the steady-state half — exception escape on the steady-state path triggers `std::abort` per `[arch §5.3]` invariant-violation rule, NOT translation to `FIXPP_ERR_UNKNOWN`.
   Both lives in `tests/capi/test_exception_trap.cpp`.

6. **Out-of-range `fixpp_error_t` round-trip — older-consumer / newer-engine compat.** Construct an engine binary linked against header version `(major=1, minor=2)`; construct a consumer linked against header version `(major=1, minor=0)`; have the engine surface a code that exists only in `minor=2` (e.g., a hypothetical new variant in the wire block). Verify the consumer receives `FIXPP_ERR_UNKNOWN` (not the new variant's numeric value); verify `fixpp_strerror(FIXPP_ERR_UNKNOWN)` returns "unknown error code (from a newer engine)". Verifies the §4.4 / `[arch §5.3]` last-bullet contract (downgrade direction). The reverse direction (older-engine / newer-consumer) is verified by injecting a code from beyond the engine's table; the engine treats it as opaque pass-through. Lives in `tests/capi/test_error_forward_compat.cpp`.

7. **Reentrancy contract grep CI rule.** A `tools/check_capi_reentrancy.sh` script that runs `gcc -E -P -DFIXPP_BUILDING_DLL` over `include/fix/c_api/*.h` and then verifies exactly one of `FIXPP_THREAD_SAFE` / `FIXPP_SINGLE_THREAD` / `FIXPP_REQUIRES_SESSION_LOCK` immediately precedes each `FIXPP_API_EXPORT` (separated only by whitespace + `extern "C"` braces) in source order. Missing or multiple annotations → CI failure. Doxygen comments above the annotation token are tolerated; comments between the annotation and `FIXPP_API_EXPORT` are a violation. Three negative-case fixtures (multi-line decl with interposed comment; conditional `#if` lacking annotation; missing-annotation decl) under `tests/ci/fixtures/capi_reentrancy_negative/` verify the gate catches each. Verifies the §4.10 contract + the Opus N-P3-2 false-positive surface. Implemented as a bash script under `tools/`; runs in Tier 1. Lives in `tests/ci/test_capi_reentrancy_annotations.sh`.

8. **Type-mismatch — `get_int` on a string field returns `FIXPP_ERR_TYPE_MISMATCH`, not garbage.** Construct an inbound message with a STRING-typed field at tag 35 (MsgType); call `fixpp_msg_get_int(msg, 35, &out)`; verify the return is `FIXPP_ERR_TYPE_MISMATCH` (numeric 7), `out` is left unmodified, and no `FIXPP_ERR_WIRE_INVALID_FRAME` (which would mean the bytes were attempted to be parsed). Lives in `tests/capi/test_type_mismatch.cpp`.

9. **Null-handle — every entry point returns `FIXPP_ERR_NULL_HANDLE` if passed NULL.** A property test that iterates every public C-ABI symbol via reflection over `<fix/c_api.h>`'s declaration list (the test pre-generates the table at build time from a header-scan), invokes each with one NULL parameter at a time, and verifies the return is `FIXPP_ERR_NULL_HANDLE`. Lives in `tests/capi/test_null_handle.cpp`.

10. **Decimal PoD round-trip preserves precision.** Construct outbound messages with various `fixpp_decimal_t` values (including edge cases: `{INT64_MIN, 0}` sentinel, `{0, 0}` zero, `{9223372036854775807, -38}` max, `{-9223372036854775807, -38}` min); send via `fixpp_msg_set_decimal`, parse the resulting wire bytes back via `fixpp_msg_get_decimal`; verify the round-tripped PoD `==` the original. Cross-doc with `[2a §9]` seam #5 (decimal correctness fuzzer). Lives in `tests/capi/test_decimal_round_trip.cpp`.

11. **Group cursor lifetime — invalidated when parent message is freed.** Construct an inbound message with a repeating group; obtain a `fixpp_group_t*` via `fixpp_msg_get_group`; trigger `fromApp` return (inbound case) or call `fixpp_msg_destroy` (outbound case); attempt to call `fixpp_group_get_field_string` on the captured cursor. Debug builds: assert the trap fires (the generation counter at §4.2.4 / `[2b §6.4]`). Release builds: assert no crash (the test runs in debug only; release-mode behaviour is documented as undefined). Lives in `tests/capi/test_group_cursor_lifetime.cpp`.

12. **Handle-type-mismatch — passing a `fixpp_session_t*` to `fixpp_msg_get_string` returns `FIXPP_ERR_INVALID_HANDLE`.** Construct a `fixpp_session_t*`; cast it to `fixpp_msg_t*` (test code only — UB in production); pass to `fixpp_msg_get_string`. Verify the C-ABI return is `FIXPP_ERR_INVALID_HANDLE` (numeric 4), no crash, no read past the wrong struct's bounds (the §4.2.2 type-tag check fires before any field-typed dereference). Lives in `tests/capi/test_handle_type_mismatch.cpp`.

13. **Cross-strand handoff via `fixpp_msg_clone` — the v1.0 escape hatch.** Construct an inbound `fixpp_msg_t*` inside a `fromApp` callback on session strand A; call `fixpp_msg_clone(inbound, &clone)`; verify the clone's generation counter is independent of the source; `co_await asio::post(strand_B)`; from strand B, walk the clone's fields via `fixpp_msg_get_string` / `_get_int` / `_get_decimal` and verify the values match the source byte-for-byte; trigger `fromApp` return on strand A (which invalidates `inbound`); verify the clone is still readable on strand B and `fixpp_msg_destroy(clone)` succeeds. Verifies the §6.3 / §10 Q5 contract that clone is the v1.0 cross-strand-handoff escape hatch. Latency seam: assert the clone operation completes in ≤ 1 µs warm-cache for a ~200-byte message per `[2c §6.6]`. Lives in `tests/capi/test_msg_clone_cross_strand.cpp`.

(13 seams. Brief minima 10. The three extras are #11 (group cursor lifetime — full cross-doc verification of the `[2b §6.4]` flyweight contract through the C ABI), #12 (handle-type-mismatch — the §4.2.2 tag-check verifier), and #13 (cross-strand `fixpp_msg_clone` handoff — added in v0.2 / Opus N-P2-2 close).)

---

## §10 Open questions

| # | Question | Disposition | Owner |
|---|---|---|---|
| 1 | **Source-distinguishing cancellation variants for v1.x?** §4.9 unifies all cancellation outcomes under `FIXPP_ERR_CANCELLED`; the §4.9 trade-off explicitly forecloses source distinction at the C ABI in v1.0. A v1.x consumer (e.g., a sophisticated recovery layer that wants different action on `transport_read_cancelled` vs `tls_load_cancelled`) might want this. **Disposition:** DEFER to post-v1; if a real consumer hits the limitation, expose via an auxiliary `fixpp_error_diagnostic_t` accessor in v1.x without touching the published numeric values (additive change per `[const §X.1]`). | post-v1 follow-up; 2i |
| 2 | **Trap policy on unexpected C++ exceptions — translate or `std::abort`?** **DECIDED in v0.2 / RC#3 close (Codex P2-1 → P1; Opus confirmed).** v1.0 ships the **construction-vs-steady split** per `[arch §5.3]` lines 387–388: construction-time thunks (`fixpp_engine_create`, `fixpp_dict_load_from_xml`, `fixpp_msg_create_outbound`) catch and translate to the domain-appropriate `FIXPP_ERR_*_CONFIG` code (or the new `FIXPP_ERR_CAPI_CONFIG_INVALID` for the engine-construction case where no domain prefix applies); steady-state thunks (every read accessor, every setter, every group accessor, every callback-trampoline) **`std::abort`** after a fatal log. The v0.1 uniform "translate to `FIXPP_ERR_UNKNOWN`" policy was wrong because (a) the steady-state hot path is exception-free per `[const §VIII.5]` so any escape is by definition an invariant violation; (b) translating after potential memory corruption hides bugs; (c) `[arch §5.3]` invariant-violation rule mandates `std::abort`. See §5.2 / §5.4 / §6.2 / §9 seam #5a / #5b. | DECIDED v0.2; 2i v1.0 |
| 3 | **C-ABI cert/pinset rotation surface in v1.x?** §7.7 defers the TLS rotation accessors to post-v1; rotation triggers go through 2j's control plane. **Disposition:** revisit when the first non-C++ consumer asks for in-process rotation. Until then, the control plane is sufficient. | v1.x feedback-driven; 2g + 2i jointly |
| 4 | **Receive callback shape (CA-007) — sync trampoline or `fixpp_session_poll(...)`?** Two shapes:<br>(a) **Sync trampoline** — engine calls user fn directly on the strand. Latency-optimal; user code runs in the strand; user MUST be quick (no I/O, no GIL acquire) or it stalls the session.<br>(b) **`fixpp_session_poll(session, msg_out)`** — engine queues; user drains in their own thread. Latency-suboptimal (one extra hop); user code runs anywhere; ergonomic for Python (no GIL contention).<br>**Disposition:** route to **2j**. 2i pins the **shape** (the receive-callback signature, the message handle's lifetime through it) but the policy choice is 2j's. The §10 Q4 entry exists so the round-1 reviewer notes the cross-doc dependency. | 2j |
| 5 | **Does `fixpp_msg_set_*` on a parsed inbound message create a copy or modify in place?** For receive-side mutation (e.g., a Python binding that wants to add a custom dialect tag to an inbound message before forwarding), the C-ABI shape needs to specify whether the mutation is destructive (modifies the underlying wire buffer in place) or copy-on-write (clones to a new arena). **Disposition:** **DECIDED v0.2 / Opus N-P2-2 close.** v1.0 picks **immutable inbound messages** — `fixpp_msg_set_*` on an inbound flyweight returns `FIXPP_ERR_INVALID_HANDLE` (the handle is a `const wire::MessageView`). A consumer that wants to mutate calls `fixpp_msg_clone(inbound, &mutable_copy)` first; the clone is a fresh outbound-shaped message. The `fixpp_msg_clone` symbol is published at §4.7 (added in v0.2 — the v0.1 doc referenced the symbol from §6.3 / §10 Q5 but failed to declare it; that gap is closed). Verified by §9 seam #13 (cross-strand handoff). | DECIDED v0.2; 2i v1.0 |
| 6 | **Streaming serialise API (stream a multi-frame outbound batch without per-frame `fixpp_session_send`)?** Some HFT consumers send burst sequences where the inter-frame latency matters. **Disposition:** DEFER to v1.x. The v1.0 single-frame `fixpp_session_send` is sufficient for the conformance corpus. Tracked in §2 non-goal #8. | post-v1 follow-up; 2j + 2i |
| 7 | **Does `fixpp_msg_t` carry the dictionary handle, or is it dictionary-agnostic?** §3.10 / §4.6 say dictionary-agnostic (the version tag travels with the message; dictionary lookup is engine-internal). A future v1.x might want explicit `fixpp_msg_get_dict(msg)` to support reflection-style consumer code. **Disposition:** DEFER. The v1.0 surface is dictionary-agnostic. | v1.x feedback-driven; 2c + 2i jointly |
| 8 | **`fixpp_strerror()` returning multiple-language strings?** Currently English-only. Some consumers (Japan exchanges, EU regulatory) want JIS / DE / ES / FR. **Disposition:** DEFER to v1.x as `fixpp_strerror_locale(code, locale)`. v1.0 is English-only; the implementation is a single static table. | post-v1 docs / i18n |

---

## §11 Hand-off

**Docs unblocked by 2i sign-off (downstream):**

- **2j (control plane interface + gRPC default impl)** — needs the opaque-handle plumbing rules (§4.2), the `fixpp_engine_t` / `fixpp_session_t` shape, the version-binding protocol on `fixpp_engine_create` (§4.5), the reentrancy annotation taxonomy (§4.10), the cancellation translation rule (§4.9). Without 2i, 2j's session lifecycle / send / receive surface cannot be drafted.
- **2k (async logger + OTel)** — needs the `FIXPP_ERR_LOG_*` / `FIXPP_ERR_OTEL_*` numeric block (§1.1 / §4.3), the opaque-handle plumbing rules (§4.2 — for `fixpp_log_sink_t`), the reentrancy taxonomy (§4.10).
- **2m (SWIG/Python)** — consumes the entire `extern "C"` surface (§4 / §5) for SWIG generation. Without 2i, the Python module's surface cannot be generated.
- **Tooling artifacts created at 2i sign-off (NEW v0.3 — Opus round-2 N-P3-3 close).** The 2i sign-off mechanically adds the following spec-driven CI artifacts to the repo (the rewriter does not create them; every PR landing 2i MUST include them): (1) `tools/abi_history/error_codes_v1.txt` — append-only audit trail mapping every published numeric value to its symbolic name + the doc revision that introduced it; one-time creation populated from the v0.3 §4.3 layout; per the §4.3 stability-rule paragraph + Appendix D §D.3 "Stability rule" sub-clause; (2) `tools/check_capi_occupancy.sh` — Tier 1 CI gate that mechanically counts `| \`*_*\` |` rows in each sibling `[2X §6.X]` errors table and asserts the published per-block counts in §1.1 / §4.3 / §6.5 / Appendix D.2 match (RC#2 drift-prevention gate); (3) `tests/ci/test_capi_reentrancy_annotations.sh` — Tier 1 CI gate that runs `gcc -E -P -DFIXPP_BUILDING_DLL` over `include/fix/c_api/*.h` and verifies exactly one of `FIXPP_THREAD_SAFE` / `FIXPP_SINGLE_THREAD` / `FIXPP_REQUIRES_SESSION_LOCK` immediately precedes each `FIXPP_API_EXPORT` (§4.10 + §9 seam #7 algorithm); (4) `tests/ci/fixtures/capi_reentrancy_negative/` — three negative-case fixtures (multi-line decl with interposed comment; conditional `#if` lacking annotation; missing-annotation decl) that the reentrancy-annotation gate must flag. Plus the abidiff Tier 2 check per `[const §IX.5]` (uses the existing `tools/abi_history/` infrastructure once `error_codes_v1.txt` lands).

**Cross-doc amendments declared at sign-off (orchestrator applies, per `[2c App D]` / `[2d App D]` / `[2e App D]` / `[2f App D]` / `[2g App D]` / `[2h App D]` precedent — the rewrite agent does NOT edit sibling docs in this draft):**

- **Appendix D §D.1** — `library/spec/feature-catalogue.md` rows CA-001..CA-010: append `covered by [2i §X.Y]` Gap notes per the discharge map in Appendix A.1 / A.2. (See Appendix D for byte-faithful before/after blocks.)
- **Appendix D §D.2** — `library/spec/coverage-index.md` `## Catalogue ID supplemental notes` section: append a new note for CA-002 (`fixpp_error_t` numeric range layout) pinning the §4.3 block table as the published audit source.
- **Appendix D §D.3** — `[const §X.4]` — extend with **(a) operational detail** for the existing both-directions principle (consumer-version stamp recorded at `fixpp_engine_create` time per `[2i §4.5]`, downgrade trigger using the stamped minor version, audit-trail file) AND **(b) a new Stability-rule sub-clause** crystallising `[SYN §3.5 #19]` "once-published-never-changes-meaning" as a binding constitutional rule, with the audit-trail file (`tools/abi_history/error_codes_v1.txt`) + occupancy gate (`tools/check_capi_occupancy.sh`) + abidiff per `[const §IX.5]` as the enforcement mechanisms. **Per `[const §XX]` this is an amendment-shaped change to Article X §4** — half (a) is clarifying language consistent with the existing principle, but half (b) is a new normative rule on top of `[const §X.4]` and is governed by the constitutional amendment process. The current `[const §X.4]` text already states both directions at the principle level (verified verbatim at `library/.specify/constitution.md:136`); the v0.2 / RC#1 close corrected the v0.1 mis-claim. The v0.1 "actively reject FROM-consumer" `FIXPP_ERR_VERSION_MISMATCH` clause was dropped because no normative source endorses it; `[arch §5.3]` line 389 explicitly calls the FROM-consumer side "tolerated" (Codex P2-2 / Opus confirmed). Framing tightened in v0.3 per Opus round-2 N-P3-4.

(2i does NOT edit `library/spec/feature-catalogue.md`, `library/spec/coverage-index.md`, `architecture.md`, `2a-decimal.md`, `2b-wire.md`, `2c-codegen.md`, `2d-threading.md`, `2e-msgstore.md`, `2f-async-mutex.md`, `2g-tls.md`, `2h-transport.md` directly per the brief's hard rule; the drop-ins are recorded in Appendix D verbatim for the orchestrator.)

---

## Appendix A — Catalogue row coverage

Per `[const §VI.1]` (every normative FIX spec section produces at least one OFFICIAL row in the catalogue) + `[const §VI.4]` (bidirectional traceability between catalogue rows and design-doc sections) and the per-doc precedent in `[2b Appendix A]`, `[2c Appendix A]`, `[2d Appendix A]`, `[2e Appendix A]`, `[2f Appendix A]`, `[2g Appendix A]`, `[2h Appendix A]`.

### A.1 Owned (sole)

| Row | Family | Catalogue text (verbatim from `library/spec/feature-catalogue.md`) | What 2i covers | 2i §/§§ |
|---|---|---|---|---|
| **CA-001** | OFFICIAL — c-api | "Opaque handle types — FixSession, FixMessage, FixDictionary (no C++ symbols in ABI)" | The `fixpp_engine_t` / `fixpp_session_t` / `fixpp_msg_t` / `fixpp_dict_t` / `fixpp_store_t` opaque-handle catalogue (§4.2), the declaration form (incomplete forward struct), the destroy discipline (§4.2.1), the type-tag mechanism (§4.2.2), the C ↔ C++ thunk shape (§5). | §4.2, §4.2.1, §4.2.2, §4.2.3, §5 |
| **CA-002** | OFFICIAL — c-api | "Error code enum + fixpp_strerror() — all error paths return numeric code" | The `fixpp_error_t` numeric layout (§4.3 — cross-cutting `[0, 99]` block carries 11 codes through v0.2 — `FIXPP_ERR_OK` / `_CANCELLED` / `_UNKNOWN` plus the 8 2i-introduced variants `_NULL_HANDLE` through `_CAPI_CONFIG_INVALID` per §6.5 — and 8 prior-doc-rooted domain blocks — `FIXPP_ERR_WIRE_*` / `_DICT_*` / `_THREAD_*` / `_STORE_*` / `_SYNC_*` / `_TLS_*` / `_TRANSPORT_*` / `_DECIMAL_*` — totalling 97 prior-doc variants per `[2a §7.4]` / `[2b §6.7]` / `[2c §6.7]` / `[2d §6.7]` / `[2e §6.7]` / `[2f §6.5]` / `[2g §6.6]` / `[2h §6.6]`; plus 5 reserved blocks for 2j / 2k / 2l / 2m / post-v1.x growth), the `fixpp_strerror()` lookup helper (§4.4), the forward-compat tolerance (§4.4 — both directions), the stability rule (§4.3 audit-trail mechanism). | §4.3, §4.4 |
| **CA-003** | OFFICIAL — c-api | "Thread-safety contract — explicit reentrancy guarantees per function" | The `FIXPP_THREAD_SAFE` / `FIXPP_SINGLE_THREAD` / `FIXPP_REQUIRES_SESSION_LOCK` annotation taxonomy (§4.10), the per-function annotation discipline (§4.6 / §4.7 / §4.8), the CI grep enforcement (§9 seam #7). | §4.10, §6.3, §9 seam #7 |
| **CA-004** | OFFICIAL — c-api | "Version negotiation — fixpp_version() / ABI version tag in header" | The `FIXPP_C_ABI_VERSION_MAJOR/MINOR/PATCH` macros (§4.5), the `fixpp_version()` runtime accessor (§4.5), the `fixpp_engine_create` version-binding protocol (§4.5 — major mismatch rejection, minor downgrade-on-return). | §4.5 |
| **CA-008** | OFFICIAL — c-api | "Field accessor — fixpp_msg_get_string / get_int / get_double by tag" | The `fixpp_msg_get_string` / `_get_int` / `_get_double` / `_get_decimal` / `_get_bytes` / `_has_tag` / `_get_msg_type` accessors (§4.6), the lifetime contract on returned `const char*` (§4.6 / §3.9), the type-mismatch detection (§4.6 + §9 seam #8). | §4.6, §3.9 |
| **CA-009** | OFFICIAL — c-api | "Field setter — fixpp_msg_set_string / set_int / set_double by tag" | The `fixpp_msg_set_string` / `_set_int` / `_set_double` / `_set_decimal` / `_set_bytes` / `_remove_tag` setters (§4.7), the per-message-arena allocation discipline (§4.7 + §6.1), the mid-message-invalidates-prior-views rule (§4.7). | §4.7 |
| **CA-010** | OFFICIAL — c-api | "Repeating group accessor — fixpp_msg_get_group / group_get_field" | The `fixpp_msg_get_group` cursor accessor (§4.8), `fixpp_group_get_field_string/_int/_double/_decimal` (§4.8), nested groups via `fixpp_group_get_nested_group` (§4.8), the setter family `fixpp_msg_group_begin/_add_entry/_end` (§4.8). | §4.8 |

### A.2 Cross-cuts (partitioned with 2j or Phase-4 session-module spec)

| Row | Family | Catalogue text (verbatim) | Partition declared in this doc | Side owned by 2i (shape) | Side owned elsewhere (behaviour) |
|---|---|---|---|---|---|
| **CA-005** | OFFICIAL — c-api | "Session lifecycle — fixpp_session_create / connect / disconnect / destroy" | §7.9 | The opaque-handle plumbing rules (§4.2), the destroy discipline (§4.2.1), the C ↔ C++ thunk shape (§5), the version-binding protocol on `fixpp_engine_create` (§4.5), the reentrancy annotation (§4.10), the error translation (§4.4). | The actual `fixpp_engine_create` / `fixpp_session_open` / `fixpp_session_close` signatures + behaviour, the FSM, the recovery semantics — owned by **2j** (control plane interface) and the **Phase-4 session-module spec** (the FSM behaviour). |
| **CA-006** | OFFICIAL — c-api | "Message send — fixpp_session_send(session, msg)" | §7.9 | The handle-shape plumbing for `fixpp_session_t` and `fixpp_msg_t` (§4.2), the cancellation translation rule for in-flight `async_write` (§4.9), the durable-before-transmit invariant honour by reference (§7.5 / `[2e §6.7]` / `[2h §6.7]`). | The actual `fixpp_session_send` signature + behaviour (sync vs async, error returns, durable-before-transmit ordering at the FSM level) — owned by **2j** and **Phase-4 session-module spec**. |
| **CA-007** | OFFICIAL — c-api | "Message receive callback — fixpp_session_on_message(session, cb, userdata)" | §7.9 / §10 Q4 | The receive-callback's `fixpp_msg_t*` lifetime through the dispatch window (§4.6 lifetime block), the reentrancy contract (§4.10), the cancellation translation if the consumer cancels mid-callback (§4.9). | The receive-callback signature, the sync-vs-async dispatch shape (sync trampoline vs `fixpp_session_poll`), the policy on whether to wake on every message or batch — owned by **2j**. |

---

## Appendix B — Normative References

Per `[const §VI.5]` Normative-References requirement (every `/specify` artifact must include a Normative References section listing the exact `[DocAbbrev §X.Y.Z] Title` entries cited). Format `[DocAbbrev §X.Y.Z] Section title` per `[const §VI.2]` canonical-format rule, drawn from `library/spec/coverage-index.md`.

### B.1 Coverage-index normative references (consumed by 2i)

2i is **not** a spec-section discharge — it is an implementation-design doc per the precedent `[2c Appendix B]` / `[2d Appendix B]`. The normative references it consumes are constitutional + architectural, not spec-section.

The CA-* catalogue rows themselves cite `[impl] implementation` per `library/spec/feature-catalogue.md:181–190` (the rows are design-rooted, not spec-section-rooted). 2i discharges them per Appendix A.1 / A.2.

### B.2 Constitutional clauses cited inline at point of use (per `[const §VI.5]` Normative-References requirement + `[const §VI.2]` canonical-format rule)

`[const §I.2]` (C ABI is adjacent, not primary surface);
`[const §IV.2]` (C ABI as legal isolation boundary for AGPL/commercial dual licensing);
`[const §V.1]` (C ABI as the legal isolation boundary);
`[const §VI.5]` (Normative-References requirement);
`[const §VII.4]` (no untested code — drives §9 seam list);
`[const §VII.5]` (conformance corpus drives §9 seam #1);
`[const §VII.7]` (parser-touching surface fuzz requirement — drives §9 seam #4);
`[const §VIII.1]` (perf-sensitive modules need benchmarks);
`[const §VIII.2]` (perf regression budgets);
`[const §VIII.3]` (perf bench frameworks);
`[const §VIII.5]` (zero allocation between parse and `fromApp` — extended to C-ABI accessor hot path per §6.1 / §8);
`[const §X.1]` (C ABI SemVer rules — drives §4.5 versioning macros);
`[const §X.2]` (no C++ symbol leakage — drives §4.1 header rule);
`[const §X.3]` (decimal PoD at boundary — `(int64 mantissa, int8 exponent)` per §4.6 / §7.1);
`[const §X.4]` (forward-compat tolerance + `FIXPP_ERR_UNKNOWN` translation — drives §4.3 / §4.4);
`[const §X.5]` (per-symbol reentrancy contract — drives §4.10);
`[const §X.6]` (ABI-affecting features trigger Gate A — drives this document's existence);
`[const §XI.2]` (ASIO native cancellation slots — drives §4.9 cancellation translation);
`[const §XIII]` (observability + async logger context for §5.2 trap-and-translate logging);
`[const §XIV.2]` (≤5 pure-virtual cap — recorded as non-applicable for 2i in §1.2);
`[const §XIV.4]` (no `dlopen` plugin loading — drives §2 non-goal #9);
`[const §XV.15]` (banned `drop-oldest` on app/session message paths — recorded by reference);
`[const §XVII.1]` (Codex Gate A required for design docs);
`[const §XVIII.1]` (v1.0 scope — drives §1.2 / §10 deferrals);
`[const §XVIII.2]` (post-1.0 roadmap — drives the §1.1 reserved blocks);
`[const §XX]` (amendments).

### B.3 Architectural sections cited (per `[const §VI.5]` Normative-References requirement + `[const §VI.2]` canonical-format rule)

`[arch §1.2]` (non-goals — no dynamic plugin loading);
`[arch §3]` (public namespaces — `fixpp::capi::detail::*` for translation thunks);
`[arch §4.1]` (core surface — `fixpp::core::error` is the upstream variant 2i translates);
`[arch §4.2]` (dictionary surface — recipient of `fixpp_dict_t` plumbing);
`[arch §4.3]` (wire surface — recipient of `fixpp_msg_t`'s underlying `wire::MessageView`);
`[arch §4.4]` (session module surface — recipient of `fixpp_session_t` plumbing);
`[arch §4.5]` (transport module surface — `fixpp_transport_t` deferred per §7.8);
`[arch §4.6]` (tls module surface — `fixpp_cert_source_t` / `fixpp_pinset_t` deferred per §7.7);
`[arch §4.7]` (log surface — `c_api/log.h` owned by 2k, error block reserved here);
`[arch §4.8]` (otel surface — `c_api/otel.h` owned by 2k, error block reserved here);
`[arch §4.9]` (tap surface — `fixpp_tap_consumer_t` deferred per §7.11);
`[arch §4.10]` (capi surface — the spine of this doc; 2i refines the per-header listing);
`[arch §4.11]` (service surface — service consumes C ABI only per `[const §V.1]`);
`[arch §4.12]` (bindings/python surface — 2m consumes 2i's surface as input);
`[arch §5.2]` (allocator policy — drives §6.1 / §8 PMR table);
`[arch §5.3]` (error model — translation boundary, drives §4.4 forward-compat rule);
`[arch §5.5]` (lifetime model — flyweights vs owned types, drives §4.2 / §4.6);
`[arch §5.6]` (frozen-at-open rule — drives §3.4);
`[arch §6]` (plugin pattern — non-applicable to 2i C ABI per §1.2);
`[arch §7.3]` (header surface layout — drives §4.1);
`[arch §8]` (service-mode boundary — service uses C ABI per `[const §V.1]`);
`[arch §9.1]` (header discipline — no C++ leakage drives §4.1);
`[arch §9.2]` (versioning — independent C-ABI track, drives §4.5);
`[arch §9.3]` (stability tiers — C ABI is "Stable from v1.0");
`[arch §10] row 2i` (handoff — "C ABI message rep + error enum — `fixpp_msg_t` accessors, `fixpp_error_t` ranges — §4.10; §5.3 error model").

### B.4 SYNTHESIS Q-IDs cited (exact)

`[SYN §3.1 Q5]` (decimal type DECIDED — template + `decimal_traits<T>` in C++; PoD `(int64 mantissa, int8 exp)` at the C ABI — drives §4.6 / §7.1);
`[SYN §3.5 #17]` (C ABI message representation DECIDED — opaque `fixpp_msg_t` with `fixpp_msg_get_*` accessors — drives §4.6 / §4.7 / §4.8);
`[SYN §3.5 #18]` (Python GIL DECIDED — informs 2m, recorded here as cross-cut to §7.12);
`[SYN §3.5 #19]` (`fixpp_error_t` DECIDED — fixpp-specific enum, not POSIX errno; once-published-never-changes — drives §4.3 / §4.4 stability rule).

### B.5 Sibling-doc citations (per `[const §VI.5]` Normative-References requirement + `[const §VI.2]` canonical-format rule)

`[2a §4.2]` (`trap_throw` pattern — surfaces in §5.2 trap-and-translate);
`[2a §4.4]` (`FIXPP_DECIMAL_T` build-time selection — surfaces in §7.1 trait dispatch);
`[2a §5.1]` (`fixpp_decimal_t` PoD shape — quoted verbatim in §3.8);
`[2a §5.2]` (boundary functions — `fixpp_decimal_parse` etc.; cross-cited in §3.8);
`[2a §6.4]` (cross-traits conversion — drives `decimal_precision_loss` translation in §7.1);
`[2a §6.5]` (latency target — drives §6.4 row 4);
`[2a §7.3]` (C ABI message representation hand-off — `fixpp_msg_field_decimal` is owned by 2i per §7.1);
`[2a §7.4]` (error model — `FIXPP_ERR_DECIMAL_INVALID` / `FIXPP_ERR_DECIMAL_PRECISION_LOSS` / `FIXPP_ERR_BUFFER_TOO_SMALL` — block layout in §4.3);
`[2b §4.2]` (`Framer::feed` — referenced for end-to-end flow);
`[2b §4.3]` (`Parser` field-iterator with fast int parse — drives §6.4 row 2 latency budget);
`[2b §6.4]` (lifetime contract on flyweight views — quoted in §3.9, drives §4.6 lifetime rules + §9 seam #11);
`[2b §6.7]` (per-doc-prefix discipline — `FIXPP_ERR_WIRE_*` ratified in §4.3);
`[2c §4.7]` (typed-message accessors — Writer's `begin_group` / `add_entry` / `end_group` precedent for §4.8 setter shape);
`[2c §4.8]` (`owning_message_t<>` + `dict::reify` — referenced for cross-strand handoff in §6.3);
`[2c §5]` (dictionary C-ABI commitments — quoted/cross-cited in §3.10);
`[2c §6.7]` (per-doc-prefix `FIXPP_ERR_DICT_*` — ratified in §4.3);
`[2d §4.7]` (cancellation propagation API — drives §3.12 / §4.9);
`[2d §4.8]` (`session_executor` — drives §6.3 / §4.10 reentrancy annotation);
`[2d §6.5]` (`cancellable_dispatch` primitive — referenced for cancellation translation precedent in §4.9);
`[2d §6.7]` (per-doc-prefix `FIXPP_ERR_THREAD_*` — ratified in §4.3; cancellation group per `[const §XI.2]`);
`[2d §7.6]` (transport ops on session strand — drives §6.3);
`[2e §4.4]` (`MessageStoreFactory` shape — referenced in §8 PMR table);
`[2e §6.7]` (per-doc-prefix `FIXPP_ERR_STORE_*` — ratified in §4.3; `fixpp_store_t` non-owning observer per N1, drives §4.2.1 / §7.5);
`[2f §4.5]` (cancellation contract — `sync_lock_aborted` produced — referenced in §4.9);
`[2f §6.5]` (per-doc-prefix `FIXPP_ERR_SYNC_*` — ratified in §4.3; cancellation precedent for §4.9);
`[2g §5]` (TLS C-ABI delegation to 2i — referenced in §7.7);
`[2g §6.6]` (per-doc-prefix `FIXPP_ERR_TLS_*` — ratified in §4.3);
`[2g §7.6]` (capi shape delegation — drives §7.7 deferral decision);
`[2g §7.8]` (TLS-event log records / OTel spans — informs §7.10);
`[2h §5]` (transport C-ABI shapes — quoted-by-reference in §7.8 for the deferred handle list);
`[2h §6.6]` (per-doc-prefix `FIXPP_ERR_TRANSPORT_*` — ratified in §4.3; sub-group coalescing in §3.16);
`[2h §6.7]` (durable-before-transmit invariant — referenced in Appendix A.2 row CA-006);
`[2h §7.8]` (consumer drop-in language for handle shape — quoted verbatim in §3.17 / §7.8).

Engineering-judgment decisions whose primary driver is engineering judgment rather than a specific spec section — the per-domain block width of 100 in §1.1, the 11 cross-cutting codes in §4.3 (3 architectural sentinels `OK` / `CANCELLED` / `UNKNOWN` per `[arch §5.3]` plus 8 2i-introduced variants `NULL_HANDLE` through `CAPI_CONFIG_INVALID`), the construction-vs-steady split in §5.2 (per `[arch §5.3]` lines 387–388 — DECIDED in v0.2 / RC#3 close), the deferral of TLS / transport handle accessors in §7.7 / §7.8, the latency Tier 1 ceilings in §6.4, the 13 test seams in §9 — cite `[const §X.y]` / `[arch §X.y]` / `[SYN §3.x #N]` / `[2X §X.y]` inline at point of use; they are not spec normatives and are intentionally listed as design-constraint references rather than coverage-index normatives in §B.1.

---

## Appendix C — Convergence log

### Round 1 (Phase A): v0.1 → v0.2 (2026-05-09)

**Phase A round 1 designation.** First / only convergence pass for Phase A so far. No reset has been used; the round-cap budget remains 3 rounds with up to 1 full-rewrite reset per the `/gate-a` 3-phase A/B/C convention.

**Reviews input:**
- Codex Gate A (Phase A round 1; tally P1=4, P2=3, P3=1): `research/reviews/codex_2i_capi_review.md`
- Opus adversarial review (Phase A round 1; **post-judging combined tally 6 P1 / 4 P2 / 3 P3**; **3 root causes**; closing recommendation: **"v0.2 can ship after a single convergence pass. Confidence: high."**): `research/reviews/opus_2i_capi_adversarial_review.md`

**Closing recommendation followed:** "v0.2 can ship after a single convergence pass."

**Root causes addressed (Opus, source of truth):**

- **RC#1 — Appendix D byte-faithfulness incomplete; the doc records cross-doc edits in a shape the orchestrator cannot apply.** Clusters Codex P1-2 (the `[2h §7.8]` "verbatim" quote of `2h-transport.md:1304–1306` was not byte-faithful — H3 `### ` heading converted to `> **bold**` blockquote; body paragraph wrapped in blockquote markers absent in source), Codex P1-3 (Appendix D.2 "Before"/"After" placeholder `[NFR-016 supplemental paragraph — long; preserved verbatim]` and the wrong claim that `coverage-index.md:442–453` "currently carries notes for `NFR-016`" — live block has D-008, NFR-015, AND NFR-016 supplementals), Codex P1-4 (Appendix D.2 "Why" cited `[const §VI.5]` as the "exact-citation/byte-faithfulness rule" — `[const §VI.5]` is actually the Normative-References rule per `constitution.md:80`), Codex P2-2 (Appendix D.3 misstated what `[const §X.4]` already guarantees and added a new `FIXPP_ERR_VERSION_MISMATCH`-on-FROM-consumer clause that conflicts with `[arch §5.3]`), Opus N-P1-1 (§D.1 byte-faithful at v0.1 authoring time but unprotected against drift). **Single fix.** §3.17 re-quoted with the live H3 marker and unwrapped paragraph (no blockquote, no bolding); §7.8 cross-references the same. Appendix D rewritten:
   - **§D.1** — added a Catalogue-column-header reference line above the Before block (using the actual live header `| ID | Source | Category | Title | FIX version(s) | Spec ref | Status | /specify | PR | Tests | Verified |` from `feature-catalogue.md:179`), confirmed Before block byte-faithful against `feature-catalogue.md:181–190`.
   - **§D.2** — replaced the `[NFR-016 supplemental paragraph — long; preserved verbatim]` placeholder with the live byte-faithful block from `coverage-index.md:442–454` (header at 442 + intro paragraph at 444 + D-008 supplemental at 446 + NFR-015 supplemental at 448 + NFR-016 supplemental at 450 + `---` separator at 452 + `## Post-1.0 Gap Registry` header at 454, all preserved verbatim including the long NFR-016 supplemental paragraph). The After block inserts the new `**CA-002 supplemental:**` paragraph between the existing NFR-016 supplemental and the `---` separator. The Why text cites `[2g App D]` / `[2h App D]` byte-faithful-drop-in precedent (not `[const §VI.5]`).
   - **§D.3** — corrected the v0.1 mis-claim ("`[const §X.4]` only declares the return-direction rule") — the live `constitution.md:136` already states both directions. The After block adds operational detail (consumer-version stamp at `fixpp_engine_create` time, downgrade trigger, audit trail, occupancy gate) consistent with the existing principle. **Dropped** the v0.1 `FIXPP_ERR_VERSION_MISMATCH` "actively reject FROM-consumer" clause because `[arch §5.3]` line 389 explicitly calls the FROM-consumer side "tolerated" (no normative source endorses active rejection).
   - **Appendix D header rule.** Added a rewriter rule: "At every rewrite, re-verify each Before block against `git show HEAD:library/...`; if any drift, re-quote." The byte-faithful-drop-in convention is recorded as a Phase 2 repo precedent (`[2g App D]` / `[2h App D]`), not a constitutional clause.

- **RC#2 — Stale published occupancy / total counts derived from a frozen-at-authoring snapshot of sibling docs rather than re-counted live; the discipline failure repeats across five sites.** Clusters Codex P1-1 (2d=8 prose vs 9-name enumeration vs `2d-threading.md:1173–1181` 9 rows; sibling total `4+13+20+8+10+4+15+22 = 96` was wrong — live total is 97), Codex P3-1 (`[2d §6.7]` line range cited as 1170–1180 truncates `dispatch_aborted` at line 1181), Opus N-P2-1 (the "**96** prior-doc variants" paragraph at `2i-capi.md:578` is the 5th drift site Codex missed). **Single fix.** Re-derived every per-domain count from live sibling tables (verified independently per Opus): 2a=4 (`2a-decimal.md:415–419`), 2b=13 (`2b-wire.md:678–693`), 2c=20 (`2c-codegen.md:1710–1731`), **2d=9** (`2d-threading.md:1169–1181` — including `dispatch_aborted`), 2e=10 (`2e-msgstore.md:1110–1121`), 2f=4 (`2f-async-mutex.md:1495–1500`), 2g=15 (`2g-tls.md:986–1004`), 2h=22 (`2h-transport.md:1173–1196`); live total = **97**. Sweep applied across all five sites: §1.1 magnitude-domain table row (THREAD = 9), §1.1 final layout block inline comment (`9 occupied`), §3.11 prose ("9 variants" + corrected line range `[2d §6.7]` lines 1169–1181), §4.3 inline comment (`9 variants` + cancellation pair note), §6.5 paragraph rewritten with the explicit live arithmetic `4 + 13 + 20 + 9 + 10 + 4 + 15 + 22 = 97`. Added a §4.3 stability-rule bullet for `tools/check_capi_occupancy.sh` — mechanically counts `| \`*_*\` |` rows in each sibling `[2X §6.X]` errors table and asserts the published counts match. Single source of truth declared explicitly: §1.1 magnitude-domain table is the SoT; every other site derives from it.

- **RC#3 — `extern "C"` exception trap conflates "construction-time configuration error" (where translate-to-`*_CONFIG` is correct per `[arch §5.3]` carve-out) with "steady-state thunk exception escape" (where `std::abort` is the architecturally-mandated path); the doc picks "translate to `FIXPP_ERR_UNKNOWN`" uniformly, which is wrong for the steady-state half.** Clusters Codex P2-1 (escalated to P1 by Opus — `extern "C"` exception trap chooses translate-to-UNKNOWN where architecture says abort on invariant violations) and is the sibling-RC for Opus N-P2-2 (`fixpp_msg_clone` symbol gap — the same root principle: engine state on the steady-state path is invariant-protected; non-invariant-protected escapes are bugs not errors). **Single fix.** Split §5.2 `guarded_call` into two flavours per Opus's counter-proposal:
   - **`guarded_call_construction(fallback_code, thunk_body)`** — used by the v1.0 whitelist `fixpp_engine_create`, `fixpp_dict_load_from_xml`, `fixpp_msg_create_outbound`. Catches `std::exception&` and translates to a domain-appropriate `FIXPP_ERR_*_CONFIG` code (or the new cross-cutting `FIXPP_ERR_CAPI_CONFIG_INVALID` for engine creation where no domain prefix applies). Per `[arch §5.3]` line 388 carve-out.
   - **`guarded_call_steady(thunk_body)`** — used by every read accessor, every setter, every group accessor, every cancellation-translation site, every callback-trampoline. **`std::abort()`** on any exception escape, after a fatal-level log via `fixpp::core::Logger`. Per `[arch §5.3]` invariant-violation rule + `[const §VIII.5]` exception-free hot path.
   §5.2 narrative + §5.4 + §6.2 + §10 Q2 + §9 seam #5 (split into #5a / #5b) updated in lockstep. New error variant `FIXPP_ERR_CAPI_CONFIG_INVALID = 10` introduced in the cross-cutting `[0, 99]` block per §6.5; sentinel-count in §1.1 / Appendix A.1 / Appendix B updated from 7 → 8 introduced variants. **§10 Q2 disposition flipped from "round-1 review confirmation" to DECIDED v0.2 / RC#3 close.** Why option (b) (split) chosen over option (a) (uniform abort or uniform translate): `[arch §5.3]` lines 387–388 explicitly bifurcates the policy — invariant violations `std::abort`, construction-time configuration errors translate; the C-ABI thunk surface inherits the bifurcation directly. Codex's counter-proposal (narrow + harden) and Opus's escalation (P2 → P1, separate construction vs steady-state flavours) converge on the same shape; this rewrite implements it.

**Per-finding resolution table:**

| Finding | Severity (after Opus judging) | Resolution | Section(s) edited |
|---|---|---|---|
| Codex P1-1 — Threading variant count is wrong (8 vs live 9) and breaks numeric-block occupancy + sibling totals | P1 (confirmed) — RC#2 cluster | RC#2 sweep across 5 sites: §1.1 row, §1.1 inline comment, §3.11 prose + line-range fix, §4.3 inline comment, §6.5 paragraph, Appendix D.2 supplemental. Live arithmetic `4+13+20+9+10+4+15+22 = 97` published explicitly. | §1.1, §3.11, §4.3, §6.5, Appendix D.2 |
| Codex P1-2 — `[2h §7.8]` "verbatim" quote not byte-faithful (H3 → blockquote+bold conversion) | P1 (confirmed) — RC#1 cluster | §3.17 re-quoted with literal H3 `### ` marker and unwrapped body paragraph from `2h-transport.md:1304–1306`; framing updated to "byte-faithful — H3 marker preserved; no blockquote conversion; no added bolding". | §3.17, §7.8 |
| Codex P1-3 — Appendix D.2 "Before/After" placeholder + wrong scope (claimed only NFR-016 in lines 442–453) | P1 (confirmed) — RC#1 cluster | RC#1: §D.2 fully rewritten with byte-faithful Before block from `coverage-index.md:442–454` including D-008, NFR-015, AND NFR-016 supplementals (plus header / intro / separator / next header) preserved verbatim; placeholder eliminated. After block inserts `**CA-002 supplemental:**` paragraph + blank line between NFR-016 and the `---` separator at line 452. | Appendix D.2 |
| Codex P1-4 — Mis-citation `[const §VI.5]` claimed as byte-faithfulness rule (it's the Normative-References rule per `constitution.md:80`) | P1 (confirmed) — RC#1 cluster | RC#1: §D.2 / §D.3 Why text rewritten to cite `[2g App D]` / `[2h App D]` byte-faithful-drop-in precedent (Phase 2 repo convention); `[const §VI.5]` cite removed from byte-faithfulness claims. Appendix D header note added documenting the precedent-not-constitution status. | Appendix D.2, Appendix D.3, Appendix D header |
| Codex P2-1 (escalated P2 → P1 by Opus) — `extern "C"` exception trap chooses "translate to UNKNOWN" where architecture says "abort on invariant violations" | **P1 (escalated by Opus)** — RC#3 cluster | RC#3: §5.2 `guarded_call` split into `guarded_call_construction` (whitelist of 3 entry points; translate to `FIXPP_ERR_*_CONFIG` per `[arch §5.3]` carve-out) and `guarded_call_steady` (every other thunk; `std::abort` after fatal log per `[arch §5.3]` invariant-violation rule). New `FIXPP_ERR_CAPI_CONFIG_INVALID = 10` for construction-time fallback. §5.4 / §6.2 / §10 Q2 / §9 seam #5 updated. §10 Q2 flipped to DECIDED v0.2. | §5.2, §5.4, §6.2, §6.5, §9 seam #5a/#5b, §10 Q2, §4.3 cross-cutting block |
| Codex P2-2 — Appendix D.3 misstated `[const §X.4]` and adds new `FIXPP_ERR_VERSION_MISMATCH` behaviour conflicting with `[arch §5.3]` | P2 (confirmed) — RC#1 cluster | RC#1: §D.3 corrected — acknowledges `[const §X.4]` already states both directions; `FIXPP_ERR_VERSION_MISMATCH`-on-FROM-consumer clause **dropped**; D.3 retains only the operational detail (consumer-version stamp, downgrade trigger, audit trail, occupancy gate). §4.4 prose aligned (FROM-consumer = opaque pass-through, not active rejection). | Appendix D.3, §4.4 |
| Codex P2-3 — Latency rationale admits ~30 ns C-ABI overhead conflicting with "thin thunk" expectation | P2 (confirmed) | §4.6 latency rationale rewritten: ~5 ns plumbing target (NULL check ≈ 1 ns + tag check ≈ 2 ns + `expected_t::has_value` ≈ 1 ns + two pointer writes ≈ 1 ns each). Marked "provisional until v1.0 bench data exists" per Codex's option (b). §6.4 latency table rationale rows rewritten (`get_string`, `get_int`, `get_decimal`, `set_int`, `get_group`, `group_get_field_string` all use the ~5 ns plumbing target). §9 seam #2 extended to check delta-vs-C++-accessor ≤ 10 ns p99 on the same runner (5 ns target with 2× headroom), in addition to the absolute ceiling. | §4.6, §6.4, §9 seam #2 |
| Codex P3-1 — §3.11 cites incomplete line range `[2d §6.7]` lines 1170–1180 (`dispatch_aborted` at line 1181) | P3 (confirmed) — RC#2 cluster | §3.11 line range corrected to `[2d §6.7]` lines 1169–1181 (full table including header). Folded into the same RC#2 sweep. | §3.11 |
| Opus N-P1-1 — §D.1 byte-faithful at v0.1 but unprotected against drift between v0.1 and sign-off | P1 (NEW) — RC#1 cluster | RC#1: Appendix D header rewriter rule added — "at every rewrite, re-verify each Before block against `git show HEAD:library/...`; if any drift, re-quote." Column-header reference line added above §D.1 Before block (using the actual live header `| ID | Source | Category | Title | FIX version(s) | Spec ref | Status | /specify | PR | Tests | Verified |`) so the orchestrator's diff is unambiguous. §D.1 Before block re-verified byte-faithful at v0.2 authoring time (2026-05-09). | Appendix D header, Appendix D.1 |
| Opus N-P2-1 — §6.5 paragraph "**96** prior-doc variants" is the 5th drift site Codex missed | P2 (NEW) — RC#2 cluster | RC#2 sweep close: §6.5 (was at line 578 in v0.1) rewritten to "97 prior-doc variants (4 + 13 + 20 + 9 + 10 + 4 + 15 + 22 — re-derived live from sibling `[2X §6.X]` tables, see §1.1 magnitude-domain table for per-doc citations)." | §6.5 |
| Opus N-P2-2 — `fixpp_msg_clone` referenced at §6.3 / §10 Q5 as v1.0 cross-strand-handoff escape hatch but never declared in §4.7 | P2 (NEW) — kindred-RC#3 (steady-state shape principle) | **Counter-proposal "declare-in-§4.7" chosen** per Opus's recommendation + the brief's rule (declare since the §10 Q5 closure becomes cleaner): added `fixpp_msg_clone(const fixpp_msg_t* src, fixpp_msg_t** clone_out)` declaration to §4.7 between `fixpp_msg_destroy` and the setters, with full doc-comment (lifetime contract, reentrancy `FIXPP_REQUIRES_SESSION_LOCK` on source's session, return codes including `FIXPP_ERR_VERSION_MISMATCH` on dictionary-version mismatch, latency ≤ 1 µs warm-cache per `[2c §6.6]`). §6.3 prose + §10 Q5 disposition aligned (Q5 flipped to DECIDED v0.2). New §9 seam #13 (cross-strand handoff via `fixpp_msg_clone`); seam total 12 → 13. §6.4 latency table row added for clone. | §4.7, §6.3, §6.4, §9 seam #13, §10 Q5 |
| Opus N-P3-1 — §6.4 latency rationale missing for `fixpp_strerror` and `fixpp_version` rows | P3 (NEW) | §6.4 row 9 (`fixpp_strerror`) and row 10 (`fixpp_version`) rationales rewritten to match the `[2a §6.5]` granularity standard: row 9 cites the ~200-element static `const char*` table + ≈ 4–6 ns realistic implementation; row 10 cites the 8-byte PoD + x86_64 SysV return-value-shuffle (~3 ns lower bound). | §6.4 |
| Opus N-P3-2 — §4.10 reentrancy CI grep gate false-positive surface unaddressed (multi-line decl, conditional `#if`, Doxygen comments) | P3 (NEW) | §4.10 + §9 seam #7 spec extended: gate runs **after preprocessing** (`gcc -E -P -DFIXPP_BUILDING_DLL`); matches "annotation token immediately precedes `FIXPP_API_EXPORT` in source order, separated only by whitespace and `extern "C"` braces"; Doxygen `/** ... */` comments above the annotation are tolerated, comments between annotation and `FIXPP_API_EXPORT` are violations. Three negative-case fixtures under `tests/ci/fixtures/capi_reentrancy_negative/` (multi-line decl with interposed comment; conditional `#if` lacking annotation; missing-annotation decl). | §4.10, §9 seam #7 |

**RC#3 option chosen (construction-time vs steady-state split).** Only one shape per `[arch §5.3]` lines 387–388 is sensible — the architecture itself bifurcates: invariant violations `std::abort`; construction-time configuration errors translate. The C-ABI thunk surface inherits the bifurcation directly (option (b) — split into two flavours). Option (a) — "narrow translate to construction-time only and steady-state aborts" is what Codex's original counter-proposal converges to under Opus's escalation, and is what `guarded_call_construction` / `guarded_call_steady` implement. No alternative was considered because the architectural bifurcation is unambiguous.

**Codex findings disagreed with — none.** Per the Opus review's tally line 207 ("Disagreed Codex findings: 0"): every Codex finding (4 P1 / 3 P2 / 1 P3) was judged either confirmed at rated severity or escalated; all Codex counter-proposals are applied.

**Net-effect summary:** v0.2 lands the v1.0 spine intact (numeric-block layout in §4.3 — `[0, 99]` cross-cutting + 8 owner blocks `[100, 899]` + 5 reserved + `[1400+]` future; `[arch §4.10]` 5-handle catalogue in §4.2 — `fixpp_engine_t` / `fixpp_session_t` / `fixpp_msg_t` / `fixpp_dict_t` / `fixpp_store_t` with uniform destroy + `tag_` magic constants + generation counter on `fixpp_msg_t`; §4.10 three-state reentrancy taxonomy + grep-CI gate; §4.9 uniform `FIXPP_ERR_CANCELLED` translation rule; §1.2 ownership boundary; §7.x integration sketches across all 8 prior-doc owners) and converges every finding through three root-cause-driven structural changes plus line-edits. **Net effect:** **+1 test seam** (12 → 13; new: #13 `cross-strand handoff via fixpp_msg_clone`; seam #5 split into #5a / #5b for the construction-vs-steady trap test); **+1 new error variant** (cross-cutting block grows from 7 to 8 introduced variants — added `FIXPP_ERR_CAPI_CONFIG_INVALID = 10` for the construction-time fallback under §5.2 split; §6.5 introduced-variants table grows from 7 rows to 8 rows; the prior-doc total stays at the live-derived 97); **+1 new published symbol** (`fixpp_msg_clone` declared in §4.7 — the v0.1 referenced symbol that wasn't declared); **±0 handle types** (still 5 of `[arch §4.10]`); **±0 §4.3 numeric-block layout** (the cross-cutting block grows from 7 + sentinels to 8 + sentinels but the block boundary `[0, 99]` is unchanged); **+1 CI gate** (`tools/check_capi_occupancy.sh` for RC#2's drift prevention); **§10 Q2 + §10 Q5** flipped to DECIDED v0.2; **0 sections added** (no new §X.Y top-level — pure convergence pass per the brief). Touched sections: status block, header `Convergence log` line, §1.1 magnitude-domain table (THREAD row + final layout block + sentinel count), §3.11 (count + line range), §3.17 (byte-faithful re-quote), §4.3 (cross-cutting block + new variant + occupancy gate), §4.4 (forward-compat behaviour FROM-consumer prose), §4.6 (latency rationale rewritten), §4.7 (new `fixpp_msg_clone` declaration), §4.10 (CI gate algorithm spec), §5.2 (full reshape — construction-vs-steady split), §5.4 (split prose), §6.2 (split prose), §6.3 (clone shape clean-up), §6.4 (latency table — every row rationale touched + new clone row), §6.5 (introduced-variants count + total prior-doc count + new variant row), §7.8 (cross-reference to §3.17 byte-faithful quote), §9 seam #2 (delta-check), §9 seam #5 (split into #5a / #5b), §9 seam #7 (false-positive surface spec), §9 seam #13 (NEW), §9 preamble (12 → 13 seams), §10 Q2 / Q5 dispositions, §11 Hand-off bullet 3 (D.3 reshape), Appendix A.1 row CA-002 (occupancy detail), Appendix B engineering-judgment paragraph (split decision recorded), Appendix C (this entry), Appendix D header (rewriter rule + precedent-not-constitution note), Appendix D.1 (column-header reference line), Appendix D.2 (full byte-faithful rewrite — placeholder eliminated), Appendix D.3 (full rewrite — `FIXPP_ERR_VERSION_MISMATCH` clause dropped + `[const §VI.5]` mis-cite corrected).

### Round 2 (Phase A): v0.2 → v0.3 (2026-05-09)

**Phase A round 2 designation.** This is the second convergence pass for Phase A. No reset has been used; the round-cap budget remains 3 rounds with up to 1 full-rewrite reset per the `/gate-a` 3-phase A/B/C convention. The round-2 reviews flag **0 new root causes** — round-1 RC#1 (Appendix D byte-faithfulness), RC#2 (occupancy / total drift; sweep clean across all five v0.1 sites + new `tools/check_capi_occupancy.sh` gate), RC#3 (`extern "C"` exception trap construction-vs-steady-state split) are all structurally closed in v0.2; round 2 is a single line-edit-class convergence pass.

**Reviews input:**
- Codex Gate A (Phase A round 2; tally P1=2, P2=2, P3=1; round-1-fix verification 11 PASS / 2 FAIL claimed): `research/reviews/codex_2i_2_capi_review.md`
- Opus adversarial review (Phase A round 2; **post-judging combined tally 0 P1 / 1 P2 / 4 P3**; **0 new root causes**; **1 Codex finding fully disagreed (round-2 P1-1); 2 Codex findings demoted (round-2 P1-2 → P3, round-2 P2-1 → P3)**; closing recommendation: **"v0.3 can ship after a single convergence pass. Confidence: high."**): `research/reviews/opus_2i_2_capi_adversarial_review.md`

**Closing recommendation followed:** "v0.3 can ship after a single convergence pass."

**Round-1 RCs carry-over verdict (round-2 confirmation).** Opus round-2 independently re-verified all three round-1 RCs are **structurally closed in v0.2**. **RC#1 (Appendix D byte-faithfulness):** §3.17 verbatim quote of `[2h §7.8]` matches `2h-transport.md:1304–1306` byte-for-byte at `2i-capi.md:240`; §D.1 Before block matches `feature-catalogue.md:181–190` byte-for-byte with the column-header reference line above; **§D.2 Before block matches `coverage-index.md:442–454` byte-for-byte** (verified line-by-line by Opus); §D.3 corrected; Appendix D rewriter rule + precedent-not-constitution note in place. **RC#2 (occupancy / total drift):** sweep clean across all five v0.1 sites; `grep -E '\b96\b'` returns zero hits; `tools/check_capi_occupancy.sh` published at §4.3 stability-rule paragraph. **RC#3 (construction-vs-steady-state split):** §5.2 / §5.4 / §6.2 / §10 Q2 / §9 seam #5a-#5b align; new `FIXPP_ERR_CAPI_CONFIG_INVALID = 10` lands cleanly. **Codex round-2's "11 PASS / 2 FAIL" verdict is wrong on both FAILs** per Opus's independent re-check: round-2 P1-1 (§D.2 Before-block byte-faithfulness FAIL) is a misread (Codex confused After-block content with Before-block content); round-2 P1-2 (`[const §VI.5]` mis-cite persists FAIL) is partially right — the §D.2 / §D.3 cites RC#1 actually targeted ARE corrected; the surviving residue at lines 1621 / 1647 / 1660 / 1684 / 1655 / 1719 are header-decoration sites in Appendix A and B, P3-class cite-hygiene drift not RC#1 regression. **Round-2 findings are line-edit class only.**

**Per-finding resolution table:**

| Finding | Severity (after Opus judging) | Status | Resolution |
|---|---|---|---|
| Codex round-2 P1-1 — Appendix D.2 "Before" block is not byte-faithful (alleged `**CA-002 supplemental:**` paragraph in Before block at line 1898) | **Disagreed by Opus — DEMOTED to non-finding (shelved with reasoning)** | **Disagreed-shelved** | **Opus disagreed — Codex's counter-proposal NOT applied — reason: Codex misread the §D.2 block boundaries.** §D.2 has TWO blocks: a **Before** block at `2i-capi.md:1906–1920` (between the opening triple-backtick at 1906 and closing triple-backtick at 1920) and an **After** block at `2i-capi.md:1924–1940` (1924 to 1940). Opus verified line-by-line that the Before block contains exactly the bytes of `coverage-index.md:442–454` (D-008 / NFR-015 / NFR-016 supplemental paragraphs preserved verbatim, separator at 452, `## Post-1.0 Gap Registry` at 454). The `**CA-002 supplemental:**` paragraph Codex flagged is in the **After** block (where it BELONGS, since the After block is the patched form), NOT the Before block. Opus's reasoning quoted: *"Codex's claim is that 'The "Before" code block contains a `**CA-002 supplemental:** ...` paragraph that does not exist in the live source block.' This is wrong on the structural reading of §D.2. ... The `**CA-002 supplemental:**` paragraph is inside the After block, NOT the Before block. ... Verified line-by-line against the live source. The Before block IS byte-faithful. Codex's round-2 P1-1 is a false-positive — not a regression of round-1 P1-3."* The §D.2 Before block is **untouched** in v0.3 — editing it would introduce a regression. Precedent: this is the source-of-truth entry for "byte-faithful Before block correctly verifies; Codex confused After-block content with Before-block content; do not edit." |
| Codex round-2 P1-2 — `[const §VI.5]` mis-citation persists outside Appendix D (at lines 1621 / 1684) | **P1 → P3 (demoted by Opus)** | **Demoted-applied** | Round-1 RC#1 target sites (the §D.2 / §D.3 "Why" cites that claimed `[const §VI.5]` was the byte-faithfulness rule) ARE corrected in v0.2 — RC#1's binding-clause defect is closed. Residual `[const §VI.5]` mis-labels survive at Appendix A header (line 1621) and Appendix B sub-headers (lines 1647 / 1655 / 1660 / 1684 / 1719) where the constitutional rule cited isn't actually about exactness of citation form. These are **cite-hygiene drift in section-header decoration**, not RC#1 binding-clause regression. Demoted P1 → P3 per Opus. **Applied:** one-pass cleanup across all six surviving sites. Line 1621 (Appendix A header — about catalogue row coverage, not Normative References): replaced "Per `[const §VI.5]` exact-coverage rule" with "Per `[const §VI.1]` (catalogue coverage) + `[const §VI.4]` (bidirectional traceability)". Line 1647 (Appendix B header — Normative References section, so §VI.5 IS structurally appropriate but the label was wrong): replaced "exact-coverage rule" with "Normative-References requirement"; added `[const §VI.2]` for the canonical-format rule. Line 1655 (B.2 sub-header), line 1684 (B.3 sub-header), line 1719 (B.5 sub-header): replaced "(exact, per `[const §VI.5]`)" with "(per `[const §VI.5]` Normative-References requirement + `[const §VI.2]` canonical-format rule)". Line 1660 (B.2 inline list): replaced "`[const §VI.5]` (exact-citation rule);" with "`[const §VI.5]` (Normative-References requirement);". Final grep confirms no surviving "exact-coverage rule" / "exact-citation rule" labels. |
| Codex round-2 P2-1 — V9(a) checklist mismatch: `[0, 99]` carries 11 codes not 10 (`[0,99]` block "10 published values" check) | **P2 → P3 (demoted by Opus)** | **Demoted-applied (phrasing tightness)** | The doc is internally consistent at 11 codes for v0.2 (3 architectural sentinels per `[arch §5.3]` + 8 2i-introduced ending at `_CAPI_CONFIG_INVALID = 10` per RC#3 close). The brief's verification-checklist "10 published values" expectation is stale relative to v0.1; v0.2's 11-code count is correct, and the doc consistently carries 11 across §1.1 / §4.3 / §6.5 / Appendix A.1 / Appendix D.2. Demoted P2 → P3 per Opus — phrasing tightness only, not a substantive defect. **Applied:** §1.1 line 56 layout-block phrasing tightened from `(cross-cutting; 2i-owned; 8 + sentinels)` to `(cross-cutting; 2i-owned; 11 occupied = 3 architectural sentinels per [arch §5.3] + 8 2i-introduced)`; §6.5 top gained a "Counting convention" paragraph making the 11 = 3 + 8 split explicit and noting the v0.1 → v0.2 grow path (10 → 11 occupied). Future reviews can no longer surface this finding. |
| Codex round-2 P2-2 — Spec references non-existent repo docs `docs/c_api_thunk_split.md` / `docs/c_api_reentrancy.md` | **P2 (confirmed at P2 by Opus)** | **Confirmed-applied** | Auditability gap — the spec assigns load-bearing content to docs that don't exist in the repo at v0.3 authoring time. **Applied per Opus's counter-proposal:** softened wording in both places to make the temporal status explicit (post-sign-off generation). §4.10 line 1193: `"The full table is reprinted in docs/c_api_reentrancy.md"` → `"The full per-symbol table is generated at sign-off from the header annotations into docs/c_api_reentrancy.md (engineering documentation; not part of the spec doc, not present in the repo at v0.3 authoring time — produced post-sign-off by §9 seam #7's reentrancy-annotation extractor over include/fix/c_api/*.h)."` §5.2 line 1304 ("Per-symbol placement"): `"The full table is in docs/c_api_thunk_split.md"` → `"The §5.2 split rule constrains every extern \"C\" symbol to one of the two flavours; the full per-symbol mapping is generated at sign-off in docs/c_api_thunk_split.md (engineering documentation; not part of the spec doc, not present in the repo at v0.3 authoring time — produced post-sign-off from the header annotations and verified by §9 seam #5a/#5b)."` |
| Codex round-2 P3-1 — Appendix A/B still labels §VI.5 as "exact-* rule" (redundant with Codex round-2 P1-2) | **P3 (confirmed; clustered with P1-2)** | **Confirmed-applied (clustered)** | Codex correctly notes redundancy with round-2 P1-2. Disposition same as P1-2 row above: applied as part of the one-pass cleanup of "exact-coverage rule" / "exact-citation rule" labels across all six surviving Appendix A / B header-decoration sites. Final grep `exact-coverage rule\|exact-citation rule` returns zero hits. |
| Opus round-2 N-P3-1 — `[const §VI.5]` cite-hygiene drift at `2i-capi.md:1647` (Appendix B header) and `:1660` (Appendix B.2 inline) — extends Codex round-2 P1-2 / P3-1 cluster | **P3 (NEW; clustered)** | **Confirmed-applied (clustered)** | Same defect class as Codex round-2 P1-2 at additional sites Codex missed. **Applied:** lines 1647, 1655, 1660, 1684, 1719 all corrected in the same one-pass sweep described in the Codex P1-2 row above. Constitutional anchors: `[const §VI.5]` retained at Appendix B sites where it IS the right cite (Appendix B is a Normative References section per §VI.5's substance); the mis-paraphrasing labels ("exact-coverage rule" / "exact-citation rule") are removed; `[const §VI.2]` is added as the canonical-format anchor where format is being asserted. |
| Opus round-2 N-P3-2 — §1.1 final layout block at line 56 reads "8 + sentinels" — phrasing tightness that invited Codex P2-1 confusion | **P3 (NEW; clustered with Codex P2-1)** | **Confirmed-applied (clustered)** | Same fix as the Codex P2-1 demoted-applied row above: §1.1 line 56 rewritten to "11 occupied = 3 architectural sentinels per [arch §5.3] + 8 2i-introduced"; §6.5 top gained a "Counting convention" clarification paragraph. |
| Opus round-2 N-P3-3 — `tools/check_capi_occupancy.sh` and `tools/abi_history/error_codes_v1.txt` referenced in §4.3 but not declared as new artifacts in §11 hand-off | **P3 (NEW)** | **Confirmed-applied** | Auditability gap on the tooling side (mirror of Codex round-2 P2-2). **Applied:** §11 hand-off gained a fourth bullet "Tooling artifacts created at 2i sign-off (NEW v0.3 — Opus round-2 N-P3-3 close)" enumerating the four spec-driven CI artifacts (`tools/abi_history/error_codes_v1.txt`, `tools/check_capi_occupancy.sh`, `tests/ci/test_capi_reentrancy_annotations.sh`, `tests/ci/fixtures/capi_reentrancy_negative/`) plus the abidiff Tier 2 check per `[const §IX.5]`. A reviewer at sign-off can now answer "what does 2i sign-off mechanically add to the repo?" from §11 alone. |
| Opus round-2 N-P3-4 — §11 Hand-off bullet 3 framing under-describes that §D.3 introduces a NEW Stability-rule sub-clause on `[const §X.4]`, not just operational-detail clarification | **P3 (NEW)** | **Confirmed-applied** | The §D.3 "After" block at line 1920 modifies `[const §X.4]` with TWO sub-clauses: (a) operational detail (consumer-version stamp + downgrade trigger + audit-trail file) — clarifying language, AND (b) a new "Stability rule" sub-clause crystallising `[SYN §3.5 #19]` "once-published-never-changes-meaning" as a binding constitutional rule. Half (b) is new normative content and is governed by `[const §XX]` (Amendments). **Applied:** §11 Hand-off bullet 3 rewritten to make the (a) + (b) split explicit and flag the half-(b) amendment-shape; Appendix D header gained a new "Amendment-shape note for §D.3" paragraph mirroring the §11 framing so the orchestrator's amendment-review step at sign-off treats §D.3 as a constitutional amendment, not just an operational-detail clarification. |

**Codex findings disagreed with — 1 (Codex round-2 P1-1).** Per Opus's adversarial review (line 41–59): Codex misread §D.2's Before-vs-After block boundaries and flagged After-block content as a Before-block byte-faithfulness violation. Opus verified line-by-line that the §D.2 Before block matches `coverage-index.md:442–454` byte-for-byte; the `**CA-002 supplemental:**` paragraph Codex flagged is in the After block, where it correctly belongs. **The §D.2 Before block is untouched in v0.3** — editing it would introduce a regression. Opus's reasoning quoted in the per-finding row above for orchestrator audit trail.

**Codex findings demoted — 2.** Codex round-2 P1-2 (`[const §VI.5]` mis-cite persists) demoted P1 → P3 because the round-1 RC#1 target sites (§D.2 / §D.3 binding-clause "Why" cites) ARE corrected in v0.2; the residue at Appendix A / B header-decoration sites is cite-hygiene drift, not RC#1 regression. Codex round-2 P2-1 (`[0, 99]` block "10 published values" check) demoted P2 → P3 because the doc is internally consistent at 11 codes for v0.2 (the brief's "10" expectation is stale relative to v0.1); the demoted fix is one phrasing tightness at §1.1 line 56 + §6.5 top. The demoted versions of both findings are applied (see per-finding rows above).

**Net-effect summary:** v0.3 is a single line-edit-class convergence pass over v0.2's three structural root-cause closures — no new RC, no new feature, no new structural change. **Net effect:** **±0 test seams** (still 13; #5a/#5b split + #13 cross-strand handoff unchanged from v0.2); **±0 error variants** (still 8 2i-introduced = `_NULL_HANDLE` through `_CAPI_CONFIG_INVALID`; cross-cutting block still 11 occupied = 3 sentinels + 8 introduced); **±0 published symbols** (still has `fixpp_msg_clone` from v0.2); **±0 handle types** (still 5 of `[arch §4.10]`); **±0 §4.3 numeric-block layout**; **±0 CI gates** (still 3 spec-driven Tier 1: occupancy, reentrancy-annotation, alloc-guard; plus abidiff Tier 2); **±0 Appendix D drop-ins** (still D.1 / D.2 / D.3); **§10 Q2 / Q5** still DECIDED v0.2; **0 sections added** — pure line-edit convergence pass; **+1 §11 hand-off bullet** (tooling artifacts); **+1 §6.5 "Counting convention" paragraph**; **+1 Appendix D header amendment-shape note for §D.3**; **6 cite-hygiene corrections** at Appendix A / B header-decoration sites (lines 1621, 1647, 1655, 1660, 1684, 1719) — one-pass cleanup of "exact-* rule" labels; **2 wording softenings** at §4.10 line 1193 and §5.2 line 1304 ("docs/c_api_*.md" temporal-status clarification); **1 Codex finding shelved** (round-2 P1-1 — Codex misread Before vs After block; reasoning recorded above); **2 Codex findings demoted** (round-2 P1-2 → P3, round-2 P2-1 → P3) and applied at the demoted severity. Touched sections: status block, header `Convergence log` line, §1.1 (layout-block phrasing tightness at line 56), §4.10 (line 1193 wording softening), §5.2 (line 1304 wording softening), §6.5 (Counting-convention paragraph at top), §11 Hand-off (NEW tooling-artifacts bullet; bullet 3 D.3 framing rewritten), Appendix A header (line 1621 cite correction), Appendix B header (line 1647 cite correction), B.2 sub-header (line 1655) + inline list (line 1660), B.3 sub-header (line 1684), B.5 sub-header (line 1719), Appendix C (this entry), Appendix D header (NEW amendment-shape note for §D.3). **§D.2 Before block is UNTOUCHED — it is byte-faithful per Opus's independent line-by-line verification, and Codex round-2 P1-1's counter-proposal (rewrite the Before block) is the source-of-truth shelved finding.** Same convergence shape 2g v0.2 → v0.3 and 2h v0.2 → v0.3 took at their round-2 line-edit passes (closing recommendation followed; 0 new RCs; line-edit residue only).

---

## Appendix D — Cross-doc edits (drop-ins) — declared but not yet applied

Mirror the `[2g App D]` / `[2h App D]` pattern. Each entry: byte-faithful "Before:" block (the literal bytes of the live source at the cited line range), "After:" block, Why.

**Rewriter rule (RC#1 close — added in v0.2).** At v0.2 authoring time and at every subsequent rewrite, re-verify each Before block against `git show HEAD:library/spec/feature-catalogue.md`, `git show HEAD:library/spec/coverage-index.md`, and `git show HEAD:library/.specify/constitution.md`; if any drift, re-quote the Before block byte-faithfully. The orchestrator's apply step at sign-off is a literal byte-patch; the Before block must match the live source verbatim or the patch will fail. The byte-faithful-drop-in convention is a Phase 2 repo precedent crystallised by `[2g App D]` / `[2h App D]`; it is binding by precedent for Phase 2 docs (no constitutional clause crystallises it — `[const §VI.5]` is the Normative-References rule, **not** the byte-faithfulness rule, per Codex P1-4 / Opus confirmed in v0.2 RC#1 close).

**Amendment-shape note for §D.3 (NEW v0.3 — Opus round-2 N-P3-4 close).** The §D.3 drop-in extends `[const §X.4]` with two distinct sub-clauses: **(a)** operational detail (consumer-version stamp, downgrade trigger, audit-trail file) — clarifying language consistent with the existing both-directions principle; AND **(b)** a new "Stability rule" sub-clause crystallising `[SYN §3.5 #19]` "once-published-never-changes-meaning" as a binding constitutional rule about ABI stability + audit trails. Half (b) is **new normative content** in the constitution — it expresses a binding rule the v0.1 / v0.2 constitution did not contain — and is therefore an **amendment-shaped change to Article X §4** governed by `[const §XX]` (Amendments). The orchestrator MUST treat §D.3 as a constitutional amendment at sign-off, not just an operational-detail clarification. §11 Hand-off bullet 3 reflects the same framing.

The orchestrator applies the drop-ins at 2i sign-off; the rewrite agent does not edit sibling docs in this draft.

### D.1 `library/spec/feature-catalogue.md` — append `covered by [2i §X.Y]` Gap notes for CA-001..CA-010

The catalogue rows currently mark every CA-* row as `backlog` / `[impl] implementation` / no Gap note. 2i sign-off discharges CA-001..CA-004 + CA-008..CA-010 sole; CA-005..CA-007 cross-cut. Drop-in: append a new column-style status update to each row — same shape as the `Status` flip from `backlog` to `done` will eventually use, but at design-doc-sign-off granularity the change is a Gap-note-style annotation (see `library/spec/coverage-index.md` "covered by [2g §X.Y]" / "covered by [2h §X.Y]" precedent).

**Catalogue column header reference** (from `library/spec/feature-catalogue.md:179` — the `## C ABI` section header at line 177 is followed by the column-header row at line 179 + the separator row at line 180; the data rows start at line 181):

```
| ID | Source | Category | Title | FIX version(s) | Spec ref | Status | /specify | PR | Tests | Verified |
|---|---|---|---|---|---|---|---|---|---|---|
```

**Before** (lines 181–190 of `library/spec/feature-catalogue.md` — verbatim, byte-faithful; the data rows under the `## C ABI` section preserved exactly as on disk):

```
| CA-001 | OFFICIAL | c-api | Opaque handle types — FixSession, FixMessage, FixDictionary (no C++ symbols in ABI) | all | [impl] implementation | backlog | — | — | — | — |
| CA-002 | OFFICIAL | c-api | Error code enum + fixpp_strerror() — all error paths return numeric code | all | [impl] implementation | backlog | — | — | — | — |
| CA-003 | OFFICIAL | c-api | Thread-safety contract — explicit reentrancy guarantees per function | all | [impl] implementation | backlog | — | — | — | — |
| CA-004 | OFFICIAL | c-api | Version negotiation — fixpp_version() / ABI version tag in header | all | [impl] implementation | backlog | — | — | — | — |
| CA-005 | OFFICIAL | c-api | Session lifecycle — fixpp_session_create / connect / disconnect / destroy | all | [impl] implementation | backlog | — | — | — | — |
| CA-006 | OFFICIAL | c-api | Message send — fixpp_session_send(session, msg) | all | [impl] implementation | backlog | — | — | — | — |
| CA-007 | OFFICIAL | c-api | Message receive callback — fixpp_session_on_message(session, cb, userdata) | all | [impl] implementation | backlog | — | — | — | — |
| CA-008 | OFFICIAL | c-api | Field accessor — fixpp_msg_get_string / get_int / get_double by tag | all | [impl] implementation | backlog | — | — | — | — |
| CA-009 | OFFICIAL | c-api | Field setter — fixpp_msg_set_string / set_int / set_double by tag | all | [impl] implementation | backlog | — | — | — | — |
| CA-010 | OFFICIAL | c-api | Repeating group accessor — fixpp_msg_get_group / group_get_field | all | [impl] implementation | backlog | — | — | — | — |
```

**After** (Spec ref column updated to cite the 2i section discharging the row; Status remains `backlog` because v1.0 implementation has not started — the design-doc-sign-off updates the Spec ref pointer, not the Status):

```
| CA-001 | OFFICIAL | c-api | Opaque handle types — FixSession, FixMessage, FixDictionary (no C++ symbols in ABI) | all | [2i §4.2] Opaque handle types | backlog | — | — | — | — |
| CA-002 | OFFICIAL | c-api | Error code enum + fixpp_strerror() — all error paths return numeric code | all | [2i §4.3] / [2i §4.4] | backlog | — | — | — | — |
| CA-003 | OFFICIAL | c-api | Thread-safety contract — explicit reentrancy guarantees per function | all | [2i §4.10] | backlog | — | — | — | — |
| CA-004 | OFFICIAL | c-api | Version negotiation — fixpp_version() / ABI version tag in header | all | [2i §4.5] | backlog | — | — | — | — |
| CA-005 | OFFICIAL | c-api | Session lifecycle — fixpp_session_create / connect / disconnect / destroy | all | [2i §7.9] (shape; behaviour owed to 2j + Phase-4) | backlog | — | — | — | — |
| CA-006 | OFFICIAL | c-api | Message send — fixpp_session_send(session, msg) | all | [2i §7.9] (shape; behaviour owed to 2j + Phase-4) | backlog | — | — | — | — |
| CA-007 | OFFICIAL | c-api | Message receive callback — fixpp_session_on_message(session, cb, userdata) | all | [2i §7.9] (shape; behaviour owed to 2j + Phase-4) | backlog | — | — | — | — |
| CA-008 | OFFICIAL | c-api | Field accessor — fixpp_msg_get_string / get_int / get_double by tag | all | [2i §4.6] | backlog | — | — | — | — |
| CA-009 | OFFICIAL | c-api | Field setter — fixpp_msg_set_string / set_int / set_double by tag | all | [2i §4.7] | backlog | — | — | — | — |
| CA-010 | OFFICIAL | c-api | Repeating group accessor — fixpp_msg_get_group / group_get_field | all | [2i §4.8] | backlog | — | — | — | — |
```

**Why.** Per `[const §VI.4]` bidirectional traceability + the `[2g App D §D.3]` / `[2h App D §D.3]` "covered by [2X §...]" precedent: the design doc that defines a row's surface should be reachable from the catalogue. The Status column stays `backlog` because the implementation has not landed; the Spec ref column pivots from the placeholder `[impl] implementation` to the actual design-doc section that pins the surface.

### D.2 `library/spec/coverage-index.md` § "Catalogue ID supplemental notes" — append CA-002 numeric-range pin

The supplemental-notes section at `library/spec/coverage-index.md:442–454` currently carries notes for `D-008` (codegen vs runtime-XML scope from 2c), `NFR-015` (Pluggable Clock from 2d), and `NFR-016` (awaitable mutex from 2f). 2i sign-off adds an entry for CA-002 pinning the §4.3 numeric-block table, inserted between the `NFR-016 supplemental:` paragraph (line 450) and the `---` separator (line 452).

**Before** (lines 442–454 of `library/spec/coverage-index.md` — verbatim, byte-faithful; the three supplemental notes plus the separator and the next header are preserved exactly as on disk):

```
## Catalogue ID supplemental notes

Notes that supplement specific catalogue rows (`feature-catalogue.md`) without rewriting the row text. These record dispositions that emerged from Phase 2 design decisions and provide the bidirectional-traceability anchor per `[const §VI.4]`.

**D-008 supplemental:** Codegen scope for v1.0 = FIX 4.2, FIX 4.4, FIX 5.0 SP2, FIXT.1.1. Runtime-XML-only scope = FIX 4.0, FIX 4.1, FIX 4.3, FIX 5.0, FIX 5.0 SP1. The row title in `feature-catalogue.md` covers the broader 4.0–5.0 SP2 surface; codegen vs runtime-XML disposition lives here, in the coverage index. Per `[2c §1.3]` and `[2c Appendix A]`. Source: 2c v1.3 sign-off (2026-05-08); see `[2c Appendix D §2]`.

**NFR-015 supplemental:** Pluggable Clock interface — `fixpp::core::Clock` (4 pure-virtual methods: `now`, `steady_now`, `sleep_until`, `cancel_sleeps`) carried by `EngineConfig`. Source spec sections: `[arch §1.1] Goals` (pluggable clocks promise) and `[2d §4.1] fixpp::core::Clock — interface, lifetime, threading`. Default impl `fixpp::core::system_clock_source` per `[2d §4.2]` (per-session reusable `steady_timer` slot keyed by `Session*` from `session_arena`); test impl `fixpp::core::mock_clock` per `[2d §4.3]` (pimpl per `[const §XI.3]`). The `effective_clock = SessionConfig::clock_override ?: EngineConfig::clock` rule (per `[2d §7.9]`) routes heartbeat (S-003 / S-004), SendingTime, S-035 session scheduling, and session-scoped LOG/OBS records through the per-session clock; engine-scope LOG/OBS records read `EngineConfig::clock` directly and carry a `clock_scope = engine` discriminator. NFR-015 covers the **clock seam only**; the consuming-row owners (the session-module Phase-4 spec for S-003/S-004/S-035, **2k** for LOG-001..004 + OBS-001..003) discharge their own rows. Source: 2d v0.4 sign-off (2026-05-08); see `[2d §11]` drop-in language and `[2d Appendix A]`.

**NFR-016 supplemental:** Awaitable mutex `fixpp::sync::async_mutex` — own implementation per `[SYN §3.2 Q6b]` (BSL-1.0 algorithm attribution to avast/asio-mutex; cppcoro / Lewis-Baker `std::atomic<uintptr_t>` state with three-state `not_locked`/`locked_no_waiters`/pointer-to-LIFO encoding + mutex-owned `next_drain_head_` residual FIFO chain; per-waiter three-state `std::atomic<waiter_phase>` machine `{ queued, granted, cancelled }` arbitrating unlock/cancel CAS with WINNER-ONLY post-CAS `*result_` writes per v1.4 CAS-then-publish). Source spec sections: `[arch §1.1] Goals` (concurrency primitives promise) and `[2f §4.1] fixpp::sync::async_mutex class — public surface`. The six-item design list per `[SYN §3.2 Q6b]` is delivered via `[2f §4.2]` (waiter embedded in awaiter inside the caller's coroutine frame), `[2f §4.3]` (PMR-aware fallback via explicit `async_lock(mr)` overload + session-side helper `async_lock_via_session_executor`), `[2f §4.5]` (ASIO `cancellation_type::total` honoured via per-waiter `phase_` CAS to `cancelled`; awaitable completes with `expected_t::unexpected{sync_lock_aborted}` at the 2f boundary, mapped to `FIXPP_ERR_CANCELLED` at the C ABI per `[2d §6.7]`), `[2f §4.6]` (per-mutex `dispatch`/`post` policy with default `dispatch` and ASIO `running_in_this_thread()` predicate), `[2f §4.7]` (`std::terminate()` precondition on destruction + explicit mutex-owned `cancel_and_drain()` drain primitive with lazy `std::atomic<std::shared_ptr<detail::drain_latch_state>> drain_latch_ptr_` non-expiring during the drain epoch, published before `draining_` per v1.4 / I-1; `signal_release()` + `signal_abort()` + `notify()` per v1.5 / I-7 / I-8), and `[2f §9]` test seams (≥ 30 covering FIFO fairness across drain cycles, cancellation mid-wait, destructor-with-waiters, contention stress, TSan + ASan clean, plus the v1.4 / v1.5 seams covering CAS-then-publish arbitration, deterministic latch publication, subscriber-wake-on-reaper-abort, and `notify()` non-terminal wake). Locked executor-compat surface per `[2d §7.4]` (completion on awaiter's bound executor; honours `cancellation_type::total`; default `dispatch`); cross-doc amendments to `[2d §4.5]` (engine-internal `Session::session_arena()` accessor), `[2d §4.7]` (per-mode effect-table row + paragraph contract on `expected_t::unexpected{sync_lock_aborted}` cancellation outcome), and `[2d §7.4]` (locked surface bullet rewording) applied at sign-off per `[2f Appendix D §D.1–§D.3]`. Direct consumers: `MessageStore` writer mutex per `[2e §6.4]` (the named hard hand-off gate from `[2e §3.1]` last bullet), pinset rotation per `[2g]`, seqnum counter per Phase-4 session-module spec. CI enforcement of `[const §XV.9]` `std::mutex`-in-coroutine-context ban via `tools/check_no_std_mutex_in_awaitable_headers.sh` grep gate (clang-tidy custom check is post-v1). NFR-016 is the **primitive seam**; no other catalogue rows discharge through it (the consuming-row owners discharge their own rows). Source: 2f v1.5 sign-off (2026-05-08); see `[2f §11]` drop-in language and `[2f Appendix A]`.

---

## Post-1.0 Gap Registry
```

**After** (insert a new `**CA-002 supplemental:**` paragraph plus a blank line between the existing `NFR-016 supplemental:` paragraph and the `---` separator at line 452 — the D-008 / NFR-015 / NFR-016 paragraphs and the rest of the file are preserved byte-faithfully):

```
## Catalogue ID supplemental notes

Notes that supplement specific catalogue rows (`feature-catalogue.md`) without rewriting the row text. These record dispositions that emerged from Phase 2 design decisions and provide the bidirectional-traceability anchor per `[const §VI.4]`.

**D-008 supplemental:** Codegen scope for v1.0 = FIX 4.2, FIX 4.4, FIX 5.0 SP2, FIXT.1.1. Runtime-XML-only scope = FIX 4.0, FIX 4.1, FIX 4.3, FIX 5.0, FIX 5.0 SP1. The row title in `feature-catalogue.md` covers the broader 4.0–5.0 SP2 surface; codegen vs runtime-XML disposition lives here, in the coverage index. Per `[2c §1.3]` and `[2c Appendix A]`. Source: 2c v1.3 sign-off (2026-05-08); see `[2c Appendix D §2]`.

**NFR-015 supplemental:** Pluggable Clock interface — `fixpp::core::Clock` (4 pure-virtual methods: `now`, `steady_now`, `sleep_until`, `cancel_sleeps`) carried by `EngineConfig`. Source spec sections: `[arch §1.1] Goals` (pluggable clocks promise) and `[2d §4.1] fixpp::core::Clock — interface, lifetime, threading`. Default impl `fixpp::core::system_clock_source` per `[2d §4.2]` (per-session reusable `steady_timer` slot keyed by `Session*` from `session_arena`); test impl `fixpp::core::mock_clock` per `[2d §4.3]` (pimpl per `[const §XI.3]`). The `effective_clock = SessionConfig::clock_override ?: EngineConfig::clock` rule (per `[2d §7.9]`) routes heartbeat (S-003 / S-004), SendingTime, S-035 session scheduling, and session-scoped LOG/OBS records through the per-session clock; engine-scope LOG/OBS records read `EngineConfig::clock` directly and carry a `clock_scope = engine` discriminator. NFR-015 covers the **clock seam only**; the consuming-row owners (the session-module Phase-4 spec for S-003/S-004/S-035, **2k** for LOG-001..004 + OBS-001..003) discharge their own rows. Source: 2d v0.4 sign-off (2026-05-08); see `[2d §11]` drop-in language and `[2d Appendix A]`.

**NFR-016 supplemental:** Awaitable mutex `fixpp::sync::async_mutex` — own implementation per `[SYN §3.2 Q6b]` (BSL-1.0 algorithm attribution to avast/asio-mutex; cppcoro / Lewis-Baker `std::atomic<uintptr_t>` state with three-state `not_locked`/`locked_no_waiters`/pointer-to-LIFO encoding + mutex-owned `next_drain_head_` residual FIFO chain; per-waiter three-state `std::atomic<waiter_phase>` machine `{ queued, granted, cancelled }` arbitrating unlock/cancel CAS with WINNER-ONLY post-CAS `*result_` writes per v1.4 CAS-then-publish). Source spec sections: `[arch §1.1] Goals` (concurrency primitives promise) and `[2f §4.1] fixpp::sync::async_mutex class — public surface`. The six-item design list per `[SYN §3.2 Q6b]` is delivered via `[2f §4.2]` (waiter embedded in awaiter inside the caller's coroutine frame), `[2f §4.3]` (PMR-aware fallback via explicit `async_lock(mr)` overload + session-side helper `async_lock_via_session_executor`), `[2f §4.5]` (ASIO `cancellation_type::total` honoured via per-waiter `phase_` CAS to `cancelled`; awaitable completes with `expected_t::unexpected{sync_lock_aborted}` at the 2f boundary, mapped to `FIXPP_ERR_CANCELLED` at the C ABI per `[2d §6.7]`), `[2f §4.6]` (per-mutex `dispatch`/`post` policy with default `dispatch` and ASIO `running_in_this_thread()` predicate), `[2f §4.7]` (`std::terminate()` precondition on destruction + explicit mutex-owned `cancel_and_drain()` drain primitive with lazy `std::atomic<std::shared_ptr<detail::drain_latch_state>> drain_latch_ptr_` non-expiring during the drain epoch, published before `draining_` per v1.4 / I-1; `signal_release()` + `signal_abort()` + `notify()` per v1.5 / I-7 / I-8), and `[2f §9]` test seams (≥ 30 covering FIFO fairness across drain cycles, cancellation mid-wait, destructor-with-waiters, contention stress, TSan + ASan clean, plus the v1.4 / v1.5 seams covering CAS-then-publish arbitration, deterministic latch publication, subscriber-wake-on-reaper-abort, and `notify()` non-terminal wake). Locked executor-compat surface per `[2d §7.4]` (completion on awaiter's bound executor; honours `cancellation_type::total`; default `dispatch`); cross-doc amendments to `[2d §4.5]` (engine-internal `Session::session_arena()` accessor), `[2d §4.7]` (per-mode effect-table row + paragraph contract on `expected_t::unexpected{sync_lock_aborted}` cancellation outcome), and `[2d §7.4]` (locked surface bullet rewording) applied at sign-off per `[2f Appendix D §D.1–§D.3]`. Direct consumers: `MessageStore` writer mutex per `[2e §6.4]` (the named hard hand-off gate from `[2e §3.1]` last bullet), pinset rotation per `[2g]`, seqnum counter per Phase-4 session-module spec. CI enforcement of `[const §XV.9]` `std::mutex`-in-coroutine-context ban via `tools/check_no_std_mutex_in_awaitable_headers.sh` grep gate (clang-tidy custom check is post-v1). NFR-016 is the **primitive seam**; no other catalogue rows discharge through it (the consuming-row owners discharge their own rows). Source: 2f v1.5 sign-off (2026-05-08); see `[2f §11]` drop-in language and `[2f Appendix A]`.

**CA-002 supplemental:** `fixpp_error_t` numeric-block layout per `[2i §4.3]` v0.2: cross-cutting block `[0, 99]` (2i-owned, 11 codes — `FIXPP_ERR_OK` / `_CANCELLED` / `_UNKNOWN` per `[arch §5.3]` plus 8 2i-introduced variants `_NULL_HANDLE` through `_CAPI_CONFIG_INVALID`); WIRE `[100, 199]` (2b-owned, 13 occupied per `[2b §6.7]`); DICT `[200, 299]` (2c-owned, 20 occupied per `[2c §6.7]`); THREAD `[300, 399]` (2d-owned, 9 occupied per `[2d §6.7]` — count includes `dispatch_aborted` which still maps to `FIXPP_ERR_CANCELLED` at the C ABI per `[2i §4.9]`); STORE `[400, 499]` (2e-owned, 10 occupied per `[2e §6.7]`); SYNC `[500, 599]` (2f-owned, 4 occupied per `[2f §6.5]`); TLS `[600, 699]` (2g-owned, 15 occupied per `[2g §6.6]`); TRANSPORT `[700, 799]` (2h-owned, 22 occupied per `[2h §6.6]`); DECIMAL `[800, 899]` (2a-owned, 4 occupied per `[2a §7.4]`); reserved `[900, 1399]` for 2j / 2k / 2l / 2m / post-v1 growth; `[1400+]` reserved for future expansion. Live total of prior-doc variants = 4 + 13 + 20 + 9 + 10 + 4 + 15 + 22 = 97. Stability rule per `[SYN §3.5 #19]` / `[const §X.4]`: once a numeric value is published in a tagged C ABI release, it never changes meaning. Audit trail via `tools/abi_history/error_codes_v1.txt` (append-only); CI verifies no re-definitions. Occupancy drift gate `tools/check_capi_occupancy.sh` mechanically counts sibling `[2X §6.X]` rows and asserts the published counts match. Per-block growth is a domain-doc amendment; cross-block growth is a 2i amendment per `[const §XX]`. Source: 2i v0.2 (2026-05-09); see `[2i §4.3]` numeric-block table and `[2i §4.4]` `fixpp_strerror()` lookup discipline.

---

## Post-1.0 Gap Registry
```

**Why.** Per the `[2g App D]` / `[2h App D]` byte-faithful-drop-in precedent + `[const §VI.4]` bidirectional traceability: design-rooted catalogue entries that pin numeric / structural invariants get a supplemental note in the coverage index, so a reader of the catalogue can find the design doc that fixed the numeric layout. The CA-002 row's Spec ref column points at `[2i §4.3]` after Appendix D §D.1; the supplemental note here gives the full block-by-block enumeration with the per-doc citations and the live total `4 + 13 + 20 + 9 + 10 + 4 + 15 + 22 = 97`. The "Before" block above is byte-faithful against `library/spec/coverage-index.md:442–454` at v0.2 authoring time (2026-05-09); per the rewriter rule at the head of Appendix D, the orchestrator MUST re-verify against the live source at sign-off and re-quote if any drift has occurred.

### D.3 `[const §X.4]` — operational detail on the `FIXPP_ERR_UNKNOWN` translation rule

The current `[const §X.4]` (verified verbatim at `library/.specify/constitution.md:136`) **already states both directions at the principle level**: "Out-of-range values are mapped to a documented 'unknown error' code on read; unknown values from old consumers are tolerated by the engine." The v0.1 D.3 framing — "`[const §X.4]` only declares the return-direction rule" — was wrong against the live constitution text and is corrected here. D.3's role is to add **operational detail** consistent with the existing principle, not to redefine the principle.

**Before** (line 136 of `library/.specify/constitution.md`, verbatim — single numbered list item within Article X):

```
4. **Error reporting at the C ABI:** `fixpp_error_t` is a bounded enum with reserved range and explicit forwards-compatibility rules (per SYNTHESIS §3.5 Q19). Out-of-range values are mapped to a documented "unknown error" code on read; unknown values from old consumers are tolerated by the engine.
```

**After** (extended item — adds the operational mechanism for the existing principle: the consumer-version-stamp at `fixpp_engine_create` time per `[2i §4.5]`, the downgrade trigger using the stamped minor version, the audit-trail mechanism. **No new "actively reject" rule** is introduced — the FROM-consumer half remains "tolerated by the engine" as opaque pass-through; the v0.1 D.3 "After" wording introduced a `FIXPP_ERR_VERSION_MISMATCH` clause that promoted "tolerate" into "actively reject", which is **dropped** because no normative source endorses that behaviour and `[arch §5.3]` line 389 explicitly calls the FROM-consumer side "tolerated"):

```
4. **Error reporting at the C ABI:** `fixpp_error_t` is a bounded enum with reserved range and explicit forwards-compatibility rules (per SYNTHESIS §3.5 Q19). Out-of-range values are mapped to a documented "unknown error" code on read; unknown values from old consumers are tolerated by the engine. **Operational detail (per `[2i §4.4]` / `[2i §4.5]`):** the engine's translation layer downgrades to `FIXPP_ERR_UNKNOWN` (numeric 2) on the return path based on the consumer's published ABI minor version recorded at `fixpp_engine_create` time per `[2i §4.5]` version-binding protocol; a code introduced after the consumer's minor version is mapped to `FIXPP_ERR_UNKNOWN` before return. The FROM-consumer direction stays opaque pass-through — the engine does not actively reject unknown FROM-consumer values; `FIXPP_ERR_VERSION_MISMATCH` (numeric 5) is reserved for the explicit major-version-mismatch case at engine construction (per `[2i §4.5]`), not for unknown-value-tolerance. **Stability rule:** once a numeric value is published in a tagged C ABI release (`FIXPP_C_ABI_VERSION_MAJOR == 1`), it never changes meaning; new variants append at unused numeric slots within their domain block. Audit trail via `tools/abi_history/error_codes_v1.txt` (checked-in append-only file); CI verifies no re-definitions per the abidiff check `[const §IX.5]` and the occupancy gate `tools/check_capi_occupancy.sh`. Numeric-block layout per `[2i §4.3]`.
```

**Why.** Per `[arch §5.3]` last bullet ("Out-of-range values from older consumers are tolerated; out-of-range values *to* a consumer are mapped to `FIXPP_ERR_UNKNOWN`") + `[SYN §3.5 #19]`: the constitutional rule already names both directions at the principle level; the operational detail (consumer-version stamp, downgrade trigger, audit trail, occupancy gate) is what 2i adds. The byte-faithful Before/After form follows the `[2g App D]` / `[2h App D]` precedent for Phase 2 sibling-doc edits. **The v0.1 framing — citing `[const §VI.5]` as the "exact-citation / byte-faithfulness rule" — was a category error: `[const §VI.5]` (verified at `library/.specify/constitution.md:80`) is the Normative-References rule, not a byte-faithful-drop-in rule.** The byte-faithful-drop-in convention is a Phase 2 repo precedent crystallised by `[2g App D]` / `[2h App D]`; it is binding by precedent for Phase 2 docs but not by an explicit constitutional clause. Codex P1-4 / Opus confirmed; corrected here at the head of Appendix D.

(2i does NOT edit `library/.specify/constitution.md`, `library/.specify/architecture.md`, or any sibling `2X` doc directly per the brief's hard rule; the drop-ins are recorded above verbatim for the orchestrator. Per the rewriter rule at the head of Appendix D, the orchestrator MUST re-verify each Before block against `git show HEAD:library/...` at sign-off and re-quote if any drift has occurred between v0.2 authoring time and sign-off.)
